// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKInputHandler.h"
#include "Character/SKCharacter.h"
#include "Movement/SKMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

//////////////////////////////////////////////////////////////////////////
// USKInputHandler

USKInputHandler::USKInputHandler()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

//////////////////////////////////////////////////////////////////////////
// 初始化

void USKInputHandler::SetupInput(UEnhancedInputComponent* Input)
{
	if (!Input) return;

	// ── 移动/视角 ──
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &USKInputHandler::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &USKInputHandler::OnLook);

	// ── 跳跃 ──
	Input->BindAction(JumpAction, ETriggerEvent::Started,   this, &USKInputHandler::OnJumpStarted);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &USKInputHandler::OnJumpCompleted);

	// ── 闪避/冲刺 ──
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &USKInputHandler::OnDodgeStarted);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &USKInputHandler::OnDodgeCompleted);

	// ── 蹲下 ──
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &USKInputHandler::OnCrouchStarted);

	// ── 战斗 ──
	Input->BindAction(AttackAction,     ETriggerEvent::Started,   this, &USKInputHandler::OnAttackStarted);
	Input->BindAction(AttackAction,     ETriggerEvent::Completed, this, &USKInputHandler::OnAttackCompleted);
	Input->BindAction(GuardAction,      ETriggerEvent::Started,   this, &USKInputHandler::OnGuardStarted);
	Input->BindAction(GuardAction,      ETriggerEvent::Completed, this, &USKInputHandler::OnGuardCompleted);
	Input->BindAction(LockOnAction,     ETriggerEvent::Started,   this, &USKInputHandler::OnLockOnStarted);
	Input->BindAction(ProstheticAction, ETriggerEvent::Started,   this, &USKInputHandler::OnProstheticStarted);
	Input->BindAction(ProstheticAction, ETriggerEvent::Completed, this, &USKInputHandler::OnProstheticCompleted);
	Input->BindAction(GrappleAction,    ETriggerEvent::Started,   this, &USKInputHandler::OnGrappleStarted);

	// ── 交互/道具 ──
	Input->BindAction(InteractAction,      ETriggerEvent::Started, this, &USKInputHandler::OnInteractStarted);
	Input->BindAction(UseItemAction,       ETriggerEvent::Started, this, &USKInputHandler::OnUseItemStarted);
	Input->BindAction(HealingGourdAction,  ETriggerEvent::Started, this, &USKInputHandler::OnHealingGourdStarted);
	Input->BindAction(CycleItemNextAction, ETriggerEvent::Started, this, &USKInputHandler::OnCycleItemNextStarted);
	Input->BindAction(CycleItemPrevAction, ETriggerEvent::Started, this, &USKInputHandler::OnCycleItemPrevStarted);

	// ── 系统 ──
	Input->BindAction(PauseAction, ETriggerEvent::Started, this, &USKInputHandler::OnPauseStarted);
	Input->BindAction(MenuAction,  ETriggerEvent::Started, this, &USKInputHandler::OnMenuStarted);
}

void USKInputHandler::AddMappingContext(APlayerController* PC)
{
	if (!PC || !DefaultMappingContext) return;

	if (UEnhancedInputLocalPlayerSubsystem* Sub =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Sub->AddMappingContext(DefaultMappingContext, 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 消费型意图

bool USKInputHandler::ConsumeAttackPressed()
{
	if (bAttackPressed)
	{
		bAttackPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeJumpPressed()
{
	if (bJumpPressed)
	{
		bJumpPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeDodgePressed()
{
	if (bDodgePressed)
	{
		bDodgePressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeInteractPressed()
{
	if (bInteractPressed)
	{
		bInteractPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeUseItemPressed()
{
	if (bUseItemPressed)
	{
		bUseItemPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeHealingGourdPressed()
{
	if (bHealingGourdPressed)
	{
		bHealingGourdPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeGrapplePressed()
{
	if (bGrapplePressed)
	{
		bGrapplePressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeProstheticPressed()
{
	if (bProstheticPressed)
	{
		bProstheticPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeLockOnPressed()
{
	if (bLockOnPressed)
	{
		bLockOnPressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeCrouchToggled()
{
	if (bCrouchToggled)
	{
		bCrouchToggled = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeCycleItemNext()
{
	if (bCycleItemNext)
	{
		bCycleItemNext = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeCycleItemPrev()
{
	if (bCycleItemPrev)
	{
		bCycleItemPrev = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumePausePressed()
{
	if (bPausePressed)
	{
		bPausePressed = false;
		return true;
	}
	return false;
}

bool USKInputHandler::ConsumeMenuPressed()
{
	if (bMenuPressed)
	{
		bMenuPressed = false;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// 输入缓冲

bool USKInputHandler::ConsumeBufferedInput(FName Action)
{
	// 升序遍历：移除匹配的第一个条目（FIFO 消费，先入先出）
	for (int32 i = 0; i < InputBuffer.Num(); ++i)
	{
		if (InputBuffer[i].Action == Action)
		{
			InputBuffer.RemoveAt(i);
			return true;
		}
	}
	return false;
}

void USKInputHandler::ClearInputBuffer()
{
	InputBuffer.Reset();
}

//////////////////////////////////////////////////////////////////////////
// 持续型意图

FVector2D USKInputHandler::GetMoveIntent() const
{
	return MoveIntent;
}

FVector2D USKInputHandler::GetLookIntent() const
{
	return LookIntent;
}

bool USKInputHandler::IsAttackHeld() const
{
	return bAttackHeld;
}

bool USKInputHandler::IsGuardHeld() const
{
	return bGuardHeld;
}

bool USKInputHandler::IsDodgeHeld() const
{
	return bDodgeHeld;
}

float USKInputHandler::GetAttackHoldTime() const
{
	return AttackHoldTime;
}

float USKInputHandler::GetProstheticHoldTime() const
{
	return ProstheticHoldTime;
}

//////////////////////////////////////////////////////////////////////////
// 连段

int32 USKInputHandler::GetComboIndex() const
{
	return ComboIndex;
}

float USKInputHandler::GetTimeSinceLastAttack() const
{
	return TimeSinceLastAttack;
}

//////////////////////////////////////////////////////////////////////////
// 生命周期

void USKInputHandler::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	UE_LOG(LogTemp, Log, TEXT("InputHandler[%s]: BeginPlay Owner=%s Class=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OwnerCharacter.Get()),
		*GetNameSafe(OwnerCharacter.IsValid() ? OwnerCharacter->GetClass() : nullptr));
}

void USKInputHandler::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ── 长按计时 ──
	if (bAttackHeld)
	{
		AttackHoldTime += DeltaTime;
	}
	if (bProstheticHeld)
	{
		ProstheticHoldTime += DeltaTime;
	}

	// ── 连段窗口 ──
	TimeSinceLastAttack += DeltaTime;
	if (TimeSinceLastAttack > 0.5f)
	{
		ComboIndex = 0;
	}

	// ── 输入缓冲管理 ──
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// 移除超时条目
	InputBuffer.RemoveAll([Now](const FSKBufferedInput& E) {
		return (Now - E.Timestamp) > E.Lifetime;
	});
	// 限制队列最大长度 6（移除最旧的条目）
	while (InputBuffer.Num() > 6)
	{
		InputBuffer.RemoveAt(0);
	}

	// ── 冲刺状态检查（无输入或离地时退出冲刺）──
	if (OwnerCharacter.IsValid())
	{
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement()))
		{
			if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
			{
				if (MoveIntent.Size() < 0.1f || OwnerCharacter->GetCharacterMovement()->IsFalling())
				{
					MoveComp->CurrentMovementTier = ESKMovementTier::Run;
				}
			}
		}
	}

	// ── 消费型意图帧末清零（未被 Consume 的残留标记）──
	bAttackPressed = false;
	bJumpPressed = false;
	bDodgePressed = false;
	bInteractPressed = false;
	bUseItemPressed = false;
	bHealingGourdPressed = false;
	bGrapplePressed = false;
	bProstheticPressed = false;
	bLockOnPressed = false;
	bCycleItemNext = false;
	bCycleItemPrev = false;
	bPausePressed = false;
	bMenuPressed = false;
	bCrouchToggled = false;
}

//////////////////////////////////////////////////////////////////////////
// 移动/视角回调

void USKInputHandler::OnMove(const FInputActionValue& Value)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	const FVector2D Input = Value.Get<FVector2D>();
	MoveIntent = Input.GetSafeNormal();

	const FVector2D Normalized = Input.GetSafeNormal();

	// ── 世界空间方向 ──
	const AController* Controller = Owner->GetController();
	if (!Controller) return;

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	Owner->AddMovementInput(Forward, Normalized.Y);
	Owner->AddMovementInput(Right,   Normalized.X);

	// ── 冲刺状态更新 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
		{
			const float InputMagnitude = Input.Size();
			if (InputMagnitude < 0.1f || Owner->GetCharacterMovement()->IsFalling())
			{
				MoveComp->CurrentMovementTier = ESKMovementTier::Run;
			}
		}
	}

	// ── 闪避方向更新 ──
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner && SekiroOwner->IsDodging() && Input.Size() > 0.1f)
	{
		SekiroOwner->SetDodgeDirection(Normalized.Y, Normalized.X);
	}
}

void USKInputHandler::OnLook(const FInputActionValue& Value)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	FVector2D LookAxis = Value.Get<FVector2D>();
	LookIntent = LookAxis;

	// 灵敏度
	LookAxis.X *= LookSensitivityYaw;
	LookAxis.Y *= LookSensitivityPitch;

	// Pitch 反转
	if (bInvertPitch)
	{
		LookAxis.Y *= -1.f;
	}

	Owner->AddControllerYawInput(LookAxis.X);
	Owner->AddControllerPitchInput(LookAxis.Y);
}

//////////////////////////////////////////////////////////////////////////
// 跳跃回调

void USKInputHandler::OnJumpStarted(const FInputActionValue& Value)
{
	bJumpPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Jump");
	Entry.Priority = 5;                          // ESKActionPriority::Jump
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Jump();
	}
}

void USKInputHandler::OnJumpCompleted(const FInputActionValue& Value)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->StopJumping();
	}
}

//////////////////////////////////////////////////////////////////////////
// 闪避/冲刺回调

void USKInputHandler::OnDodgeStarted(const FInputActionValue& Value)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	// ── 闪避 ──
	bDodgePressed = true;
	bDodgeHeld = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Dodge");
	Entry.Priority = 7;                          // ESKActionPriority::Dodge
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);

	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner)
	{
		const bool bInAir = Owner->GetCharacterMovement()->IsFalling();
		if (bInAir && !SekiroOwner->CanAirDodge())
		{
			return;
		}
		SekiroOwner->SetDodging(true);
	}

	// ── 冲刺 ──
	if (Owner->bIsCrouched)
	{
		Owner->UnCrouch();
	}
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ESKMovementTier::Sprint;
	}
}

void USKInputHandler::OnDodgeCompleted(const FInputActionValue& Value)
{
	bDodgeHeld = false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	// ── 闪避结束 ──
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner)
	{
		SekiroOwner->SetDodging(false);
	}

	// ── 冲刺退出 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ESKMovementTier::Run;
	}
}

//////////////////////////////////////////////////////////////////////////
// 蹲下回调

void USKInputHandler::OnCrouchStarted(const FInputActionValue& Value)
{
	bCrouchToggled = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (Owner->bIsCrouched)
	{
		Owner->UnCrouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Run;
		}
	}
	else
	{
		Owner->Crouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Crouch;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 战斗回调

void USKInputHandler::OnAttackStarted(const FInputActionValue& Value)
{
	bAttackPressed = true;
	bAttackHeld = true;
	AttackHoldTime = 0.f;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Attack");
	Entry.Priority = 2;                          // ESKActionPriority::Attack
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);

	// 连段计数：0.5s 窗口内连续触发 → ComboIndex++
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (TimeSinceLastAttack <= 0.5f && ComboIndex > 0)
	{
		ComboIndex++;
	}
	else
	{
		ComboIndex = 1;
	}
	TimeSinceLastAttack = 0.f;
}

void USKInputHandler::OnAttackCompleted(const FInputActionValue& Value)
{
	bAttackHeld = false;
	AttackHoldTime = 0.f;
}

void USKInputHandler::OnGuardStarted(const FInputActionValue& Value)
{
	bGuardHeld = true;

	// Guard 也入缓冲，但消费方优先使用 IsGuardHeld 持续状态
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Guard");
	Entry.Priority = 5;                          // ESKActionPriority::Guard
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputHandler::OnGuardCompleted(const FInputActionValue& Value)
{
	bGuardHeld = false;
}

void USKInputHandler::OnLockOnStarted(const FInputActionValue& Value)
{
	bLockOnPressed = true;
}

void USKInputHandler::OnProstheticStarted(const FInputActionValue& Value)
{
	bProstheticPressed = true;
	bProstheticHeld = true;
	ProstheticHoldTime = 0.f;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Prosthetic");
	Entry.Priority = 4;                          // ESKActionPriority::Prosthetic
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputHandler::OnProstheticCompleted(const FInputActionValue& Value)
{
	bProstheticHeld = false;
	ProstheticHoldTime = 0.f;
}

void USKInputHandler::OnGrappleStarted(const FInputActionValue& Value)
{
	bGrapplePressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Grapple");
	Entry.Priority = 2;                          // ESKActionPriority::Attack（与Attack同级）
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

//////////////////////////////////////////////////////////////////////////
// 交互/道具回调

void USKInputHandler::OnInteractStarted(const FInputActionValue& Value)
{
	bInteractPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Interact");
	Entry.Priority = 10;                         // ESKActionPriority::Deathblow
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputHandler::OnUseItemStarted(const FInputActionValue& Value)
{
	bUseItemPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputHandler::OnHealingGourdStarted(const FInputActionValue& Value)
{
	bHealingGourdPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputHandler::OnCycleItemNextStarted(const FInputActionValue& Value)
{
	bCycleItemNext = true;
}

void USKInputHandler::OnCycleItemPrevStarted(const FInputActionValue& Value)
{
	bCycleItemPrev = true;
}

//////////////////////////////////////////////////////////////////////////
// 系统回调

void USKInputHandler::OnPauseStarted(const FInputActionValue& Value)
{
	bPausePressed = true;
}

void USKInputHandler::OnMenuStarted(const FInputActionValue& Value)
{
	bMenuPressed = true;
}
