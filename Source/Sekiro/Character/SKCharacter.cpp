// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKCharacter.h"
#include "Weapon/SKWeaponComponent.h"
#include "Input/SKInputManager.h"
#include "Camera/SKCameraManagerComponent.h"
#include "UI/SKLockOnIndicatorComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/SKMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"

// ============================================================================
// ASKCharacter
// ============================================================================

ASKCharacter::ASKCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USKMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	// 蹲姿 Idle 的 RootPos 比站立 Idle 低约 36.1 cm；由 192 cm 站立胶囊等比例保留身体余量，
	// 得到约 156 cm 的蹲姿总高。半径保持 42 cm，半高取 78 cm。
	MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	MoveComp->SetCrouchedHalfHeight(78.f);
	MoveComp->bOrientRotationToMovement = false;
	MoveComp->RotationRate = FRotator(0.f, 500.f, 0.f);
	MoveComp->JumpZVelocity = 700.f;
	MoveComp->AirControl = 0.35f;
	MoveComp->MinAnalogWalkSpeed = 20.f;
	MoveComp->BrakingDecelerationWalking = 2000.f;
	MoveComp->BrakingDecelerationFalling = 0.f;

	bAllowAirDodge = false;
	bIsDodging = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	WeaponComponent = CreateDefaultSubobject<USKWeaponComponent>(TEXT("WeaponComponent"));
	InputManager = CreateDefaultSubobject<USKInputManager>(TEXT("InputManager"));
	CameraManager = CreateDefaultSubobject<USKCameraManagerComponent>(TEXT("CameraManager"));
	LockOnIndicator = CreateDefaultSubobject<USKLockOnIndicatorComponent>(TEXT("LockOnIndicator"));
}

void ASKCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (WeaponComponent)
	{
		WeaponComponent->InitWeapon();
	}
}

void ASKCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (InputManager)
		{
			InputManager->AddMappingContext(PC);
		}
	}
}

void ASKCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	// InputManager 内部绑定全部 InputAction（Move/Look/Jump/Attack/Guard/Dodge 等）
	if (InputManager)
	{
		InputManager->SetupInput(Input);
	}
}

// ── 闪避状态 ──────────────────────────────────────────────

void ASKCharacter::SetDodging(bool bActive)
{
	bIsDodging = bActive;
}

void ASKCharacter::SetDodgeDirection(float Fwd, float Lateral)
{
	DodgeDirection = Fwd;
	DodgeDirectionLateral = Lateral;
}
