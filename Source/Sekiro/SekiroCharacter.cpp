// Copyright Epic Games, Inc. All Rights Reserved.

#include "SekiroCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

//////////////////////////////////////////////////////////////////////////
// ASekiroCharacter

ASekiroCharacter::ASekiroCharacter()
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
	MoveComp->MaxWalkSpeed = 500.f;
	MoveComp->MinAnalogWalkSpeed = 20.f;
	MoveComp->BrakingDecelerationWalking = 2000.f;

	// 闪避属性
	MoveComp->BrakingDecelerationFalling = 0.f;
	MoveComp->MaxWalkSpeedCrouched = CrouchSpeed;
	bAllowAirDodge = false;

	bIsSprinting = false;
	bIsDodging  = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ASekiroCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void ASekiroCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 根据冲刺状态调整 MaxWalkSpeed（蹲下由 UCharacterMovementComponent::MaxWalkSpeedCrouched 处理）
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp && !bIsCrouched)
	{
		MoveComp->MaxWalkSpeed = bIsSprinting ? SprintSpeed : 500.f;
	}
}

//////////////////////////////////////////////////////////////////////////
// Input 绑定

void ASekiroCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	// ── 移动/视角 ──
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASekiroCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASekiroCharacter::Look);

	// ── 跳跃 ──
	Input->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

	// ── 闪避/冲刺 ──
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &ASekiroCharacter::DodgePressed);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &ASekiroCharacter::DodgeReleased);
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &ASekiroCharacter::SprintPressed);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &ASekiroCharacter::SprintReleased);

	// ── 蹲下 ──
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ASekiroCharacter::CrouchToggle);

	// ── 战斗（桩）──
	Input->BindAction(AttackAction,    ETriggerEvent::Started,   this, &ASekiroCharacter::AttackPressed);
	Input->BindAction(AttackAction,    ETriggerEvent::Completed, this, &ASekiroCharacter::AttackReleased);
	Input->BindAction(GuardAction,     ETriggerEvent::Started,   this, &ASekiroCharacter::GuardPressed);
	Input->BindAction(GuardAction,     ETriggerEvent::Completed, this, &ASekiroCharacter::GuardReleased);
	Input->BindAction(LockOnAction,    ETriggerEvent::Started,   this, &ASekiroCharacter::LockOnPressed);
	Input->BindAction(ProstheticAction,ETriggerEvent::Started,   this, &ASekiroCharacter::ProstheticPressed);
	Input->BindAction(ProstheticAction,ETriggerEvent::Completed, this, &ASekiroCharacter::ProstheticReleased);
	Input->BindAction(GrappleAction,   ETriggerEvent::Started,   this, &ASekiroCharacter::GrapplePressed);

	// ── 交互/道具（桩）──
	Input->BindAction(InteractAction,      ETriggerEvent::Started, this, &ASekiroCharacter::InteractPressed);
	Input->BindAction(UseItemAction,       ETriggerEvent::Started, this, &ASekiroCharacter::UseItemPressed);
	Input->BindAction(HealingGourdAction,  ETriggerEvent::Started, this, &ASekiroCharacter::HealingGourdPressed);
	Input->BindAction(CycleItemNextAction, ETriggerEvent::Started, this, &ASekiroCharacter::CycleItemNext);
	Input->BindAction(CycleItemPrevAction, ETriggerEvent::Started, this, &ASekiroCharacter::CycleItemPrev);

	// ── 系统（桩）──
	Input->BindAction(PauseAction, ETriggerEvent::Started, this, &ASekiroCharacter::PausePressed);
	Input->BindAction(MenuAction,  ETriggerEvent::Started, this, &ASekiroCharacter::MenuPressed);
}

//////////////////////////////////////////////////////////////////////////
// 移动 — 核心

void ASekiroCharacter::Move(const FInputActionValue& Value)
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

	// ── 冲刺判定：有输入 + 地面 → 保持冲刺速度 ──
	if (bIsSprinting)
	{
		if (InputMagnitude < 0.1f || GetCharacterMovement()->IsFalling())
		{
			bIsSprinting = false;
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

void ASekiroCharacter::Look(const FInputActionValue& Value)
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

void ASekiroCharacter::SprintPressed()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	bIsSprinting = true;
}

void ASekiroCharacter::SprintReleased()
{
	bIsSprinting = false;
}

//////////////////////////////////////////////////////////////////////////
// 蹲下（切换）

void ASekiroCharacter::CrouchToggle()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
		bIsSprinting = false;
	}
}

//////////////////////////////////////////////////////////////////////////
// 闪避 / 垫步

void ASekiroCharacter::DodgePressed()
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

void ASekiroCharacter::DodgeReleased()
{
	bIsDodging = false;
}

//////////////////////////////////////////////////////////////////////////
// 战斗 — 桩

void ASekiroCharacter::AttackPressed()   {}
void ASekiroCharacter::AttackReleased()  {}
void ASekiroCharacter::GuardPressed()    {}
void ASekiroCharacter::GuardReleased()   {}
void ASekiroCharacter::LockOnPressed()   {}
void ASekiroCharacter::ProstheticPressed()  {}
void ASekiroCharacter::ProstheticReleased() {}
void ASekiroCharacter::GrapplePressed()  {}

//////////////////////////////////////////////////////////////////////////
// 交互/道具 — 桩

void ASekiroCharacter::InteractPressed()     {}
void ASekiroCharacter::UseItemPressed()      {}
void ASekiroCharacter::HealingGourdPressed() {}
void ASekiroCharacter::CycleItemNext()       {}
void ASekiroCharacter::CycleItemPrev()       {}

//////////////////////////////////////////////////////////////////////////
// 系统 — 桩

void ASekiroCharacter::PausePressed() {}
void ASekiroCharacter::MenuPressed()  {}
