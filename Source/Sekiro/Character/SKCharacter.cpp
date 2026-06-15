// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKCharacter.h"
#include "Weapon/SKWeaponComponent.h"
#include "Input/SKInputHandler.h"
#include "Animation/SKAnimationController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/SKMovementComponent.h"
#include "GameFramework/Controller.h"
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
	MoveComp->bOrientRotationToMovement = true;
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
	InputHandler = CreateDefaultSubobject<USKInputHandler>(TEXT("InputHandler"));
	AnimController = CreateDefaultSubobject<USKAnimationController>(TEXT("AnimController"));
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
		if (InputHandler)
		{
			InputHandler->AddMappingContext(PC);
		}
	}
}

void ASKCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	// InputHandler 内部绑定全部 InputAction（Move/Look/Jump/Attack/Guard/Dodge 等）
	if (InputHandler)
	{
		InputHandler->SetupInput(Input);
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
