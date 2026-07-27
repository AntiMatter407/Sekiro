// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKCharacter.h"
#include "Weapon/SKWeaponComponent.h"
#include "Input/SKInputManager.h"
#include "Camera/SKCameraManagerComponent.h"
#include "Combat/SKCombatComponent.h"
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

/**
 * 使用项目玩家移动组件构造标准主角，并委托给可保留派生类移动组件覆盖的公共初始化路径。
 * 本函数只在 UObject 构造阶段调用；ObjectInitializer 由引擎提供，不应跨构造过程保存。
 *
 * @param ObjectInitializer 当前角色的默认子对象初始化器；函数会为 CharacterMovement 指定玩家移动类型。
 */
ASKCharacter::ASKCharacter(const FObjectInitializer& ObjectInitializer)
	: ASKCharacter(
		ObjectInitializer.SetDefaultSubobjectClass<USKMovementComponent>(ACharacter::CharacterMovementComponentName),
		FSKMovementComponentOverrideTag())
{
}

/**
 * 初始化玩家与 AI 角色共享的胶囊、移动参数、武器和战斗组件，并创建未被派生类抑制的可选玩家组件。
 * 本函数只在 UObject 构造阶段调用；不访问世界、控制器或运行时 Lua 环境。
 *
 * @param ObjectInitializer 已包含最终 CharacterMovement 类型及可选子对象抑制规则的初始化器。
 * @param OverrideTag 仅用于区分派生类构造路径的类型标签，不携带运行时状态。
 */
ASKCharacter::ASKCharacter(
	const FObjectInitializer& ObjectInitializer,
	FSKMovementComponentOverrideTag OverrideTag)
	: Super(ObjectInitializer)
{
	static_cast<void>(OverrideTag);

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

	CameraBoom = CreateOptionalDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	if (CameraBoom)
	{
		CameraBoom->SetupAttachment(RootComponent);
		CameraBoom->TargetArmLength = 400.0f;
		CameraBoom->bUsePawnControlRotation = true;
		// 原生默认值保证 Lua 环境尚未创建时镜头仍可过滤 Root Motion 位移抖动；Gameplay Camera Lua 可在运行时覆盖调参。
		CameraBoom->bEnableCameraLag = true;
		CameraBoom->CameraLagSpeed = 25.0f;
		CameraBoom->CameraLagMaxDistance = 30.0f;
		CameraBoom->bUseCameraLagSubstepping = true;
		CameraBoom->CameraLagMaxTimeStep = 1.0f / 60.0f;
	}

	FollowCamera = CreateOptionalDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	if (FollowCamera)
	{
		if (CameraBoom)
		{
			FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
		}
		else
		{
			FollowCamera->SetupAttachment(RootComponent);
		}
		FollowCamera->bUsePawnControlRotation = false;
	}

	// 沿用旧蓝图序列化的默认子对象名称；类型与业务实现均已迁移到 WeaponManager。
	WeaponManager = CreateDefaultSubobject<USKWeaponComponent>(TEXT("WeaponComponent"));
	InputManager = CreateOptionalDefaultSubobject<USKInputManager>(TEXT("InputManager"));
	CombatComponent = CreateDefaultSubobject<USKCombatComponent>(TEXT("CombatComponent"));
	CameraManager = CreateOptionalDefaultSubobject<USKCameraManagerComponent>(TEXT("CameraManager"));
	LockOnIndicator = CreateOptionalDefaultSubobject<USKLockOnIndicatorComponent>(TEXT("LockOnIndicator"));
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
