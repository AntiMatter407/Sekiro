// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKCharacter.h"
#include "Weapon/SKWeaponComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/SKMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

//////////////////////////////////////////////////////////////////////////
// ASKCharacter

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

	// 闪避属性
	MoveComp->BrakingDecelerationFalling = 0.f;
	bAllowAirDodge = false;

	bIsDodging  = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	WeaponComponent = CreateDefaultSubobject<USKWeaponComponent>(TEXT("WeaponComponent"));
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
		if (UEnhancedInputLocalPlayerSubsystem* Sub =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Input 绑定

void ASKCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	// ── 移动/视角 ──
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASKCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASKCharacter::Look);

	// ── 跳跃 ──
	Input->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

	// ── 闪避/冲刺 ──
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &ASKCharacter::DodgePressed);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &ASKCharacter::DodgeReleased);
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &ASKCharacter::SprintPressed);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &ASKCharacter::SprintReleased);

	// ── 蹲下 ──
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ASKCharacter::CrouchToggle);

	// ── 战斗（桩）──
	Input->BindAction(AttackAction,    ETriggerEvent::Started,   this, &ASKCharacter::AttackPressed);
	Input->BindAction(AttackAction,    ETriggerEvent::Completed, this, &ASKCharacter::AttackReleased);
	Input->BindAction(GuardAction,     ETriggerEvent::Started,   this, &ASKCharacter::GuardPressed);
	Input->BindAction(GuardAction,     ETriggerEvent::Completed, this, &ASKCharacter::GuardReleased);
	Input->BindAction(LockOnAction,    ETriggerEvent::Started,   this, &ASKCharacter::LockOnPressed);
	Input->BindAction(ProstheticAction,ETriggerEvent::Started,   this, &ASKCharacter::ProstheticPressed);
	Input->BindAction(ProstheticAction,ETriggerEvent::Completed, this, &ASKCharacter::ProstheticReleased);
	Input->BindAction(GrappleAction,   ETriggerEvent::Started,   this, &ASKCharacter::GrapplePressed);

	// ── 交互/道具（桩）──
	Input->BindAction(InteractAction,      ETriggerEvent::Started, this, &ASKCharacter::InteractPressed);
	Input->BindAction(UseItemAction,       ETriggerEvent::Started, this, &ASKCharacter::UseItemPressed);
	Input->BindAction(HealingGourdAction,  ETriggerEvent::Started, this, &ASKCharacter::HealingGourdPressed);
	Input->BindAction(CycleItemNextAction, ETriggerEvent::Started, this, &ASKCharacter::CycleItemNext);
	Input->BindAction(CycleItemPrevAction, ETriggerEvent::Started, this, &ASKCharacter::CycleItemPrev);

	// ── 系统（桩）──
	Input->BindAction(PauseAction, ETriggerEvent::Started, this, &ASKCharacter::PausePressed);
	Input->BindAction(MenuAction,  ETriggerEvent::Started, this, &ASKCharacter::MenuPressed);
}

//////////////////////////////////////////////////////////////////////////
// 移动 — 核心

void ASKCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();

	if (!Controller) return;

	// 记录上一帧输入方向用于闪避方向判断
	const float InputMagnitude = Input.Size();

	// ── 世界空间方向 ──
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// 标准化：避免斜向输入速度超标
	const FVector2D Normalized = Input.GetSafeNormal();
	AddMovementInput(Forward, Normalized.Y);
	AddMovementInput(Right,   Normalized.X);

	// ── 冲刺判定：无输入或离地时退出冲刺 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
		{
			if (InputMagnitude < 0.1f || GetCharacterMovement()->IsFalling())
			{
				MoveComp->CurrentMovementTier = ESKMovementTier::Run;
			}
		}
	}

	// ── 闪避方向更新 ──
	if (bIsDodging && InputMagnitude > 0.1f)
	{
		// 计算玩家输入在镜头前方向上的投影，作为闪避前后方向
		DodgeDirection = Normalized.Y; // +1=前, -1=后
		DodgeDirectionLateral = Normalized.X; // -1=左, +1=右
	}
}

//////////////////////////////////////////////////////////////////////////
// 视角 — 灵敏度 + 反转

void ASKCharacter::Look(const FInputActionValue& Value)
{
	if (!Controller) return;

	FVector2D LookAxis = Value.Get<FVector2D>();

	// 灵敏度
	LookAxis.X *= LookSensitivityYaw;
	LookAxis.Y *= LookSensitivityPitch;

	// Pitch 反转
	if (bInvertPitch)
	{
		LookAxis.Y *= -1.f;
	}

	AddControllerYawInput(LookAxis.X);
	AddControllerPitchInput(LookAxis.Y);
}

//////////////////////////////////////////////////////////////////////////
// 冲刺（按住闪避键 + 移动）

void ASKCharacter::SprintPressed()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ESKMovementTier::Sprint;
	}
}

void ASKCharacter::SprintReleased()
{
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ESKMovementTier::Run;
	}
}

//////////////////////////////////////////////////////////////////////////
// 蹲下（切换）

void ASKCharacter::CrouchToggle()
{
	if (bIsCrouched)
	{
		UnCrouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Run;
		}
	}
	else
	{
		Crouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Crouch;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 闪避 / 垫步

void ASKCharacter::DodgePressed()
{
	const bool bInAir = GetCharacterMovement()->IsFalling();
	if (bInAir && !bAllowAirDodge)
	{
		return;
	}

	bIsDodging = true;
	DodgeDirection = 0.f;

	// TODO: 根据 DodgeDirection 驱动动画蓝图选择对应方向的闪避动画
	//       子类/蓝图覆盖此函数以播放 AnimMontage + 施加位移
}

void ASKCharacter::DodgeReleased()
{
	bIsDodging = false;
}

//////////////////////////////////////////////////////////////////////////
// 战斗 — 桩

void ASKCharacter::AttackPressed()   {}
void ASKCharacter::AttackReleased()  {}
void ASKCharacter::GuardPressed()    {}
void ASKCharacter::GuardReleased()   {}
void ASKCharacter::LockOnPressed()   {}
void ASKCharacter::ProstheticPressed()  {}
void ASKCharacter::ProstheticReleased() {}
void ASKCharacter::GrapplePressed()  {}

//////////////////////////////////////////////////////////////////////////
// 交互/道具 — 桩

void ASKCharacter::InteractPressed()     {}
void ASKCharacter::UseItemPressed()      {}
void ASKCharacter::HealingGourdPressed() {}
void ASKCharacter::CycleItemNext()       {}
void ASKCharacter::CycleItemPrev()       {}

//////////////////////////////////////////////////////////////////////////
// 系统 — 桩

void ASKCharacter::PausePressed() {}
void ASKCharacter::MenuPressed()  {}
