// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKInputManager.h"
#include "Animation/AnimInstance.h"
#include "Camera/SKCameraManagerComponent.h"
#include "Character/SKCharacter.h"
#include "Combat/SKCombatComponent.h"
#include "Movement/SKMovementComponent.h"
#include "Weapon/SKWeapon.h"
#include "Weapon/SKWeaponManagerComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
namespace
{
	static bool ResolveSKScreenInputWorldDirection(const ACharacter* Owner, float InputX, float InputY, FVector& OutDirection)
	{
		OutDirection = FVector::ZeroVector;
		if (!Owner) return false;

		const AController* Controller = Owner->GetController();
		if (!Controller) return false;

		const FVector2D Input(InputX, InputY);
		if (Input.IsNearlyZero()) return false;

		const FVector2D Normalized = Input.GetSafeNormal();
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		OutDirection = (Forward * Normalized.Y + Right * Normalized.X).GetSafeNormal2D();
		return !OutDirection.IsNearlyZero();
	}

    /**
     * 判断离散输入名是否属于禁战区域需要屏蔽的上半身战斗动作。
     * 本函数只做稳定语义名比较，不访问 UObject，可在游戏线程输入路径中调用。
     *
     * @param ActionName 输入缓冲或 Lua 接口使用的动作名，不区分大小写。
     * @return Attack、Guard、Prosthetic 或 Grapple 时返回 true，否则返回 false。
     */
    static bool IsSKRestrictedCombatAction(FName ActionName)
    {
        const FString NormalizedName = ActionName.ToString().ToLower();
        return NormalizedName == TEXT("attack")
            || NormalizedName == TEXT("guard")
            || NormalizedName == TEXT("prosthetic")
            || NormalizedName == TEXT("grapple");
    }
}

//////////////////////////////////////////////////////////////////////////
// USKInputManager

USKInputManager::USKInputManager()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

//////////////////////////////////////////////////////////////////////////
// 初始化

void USKInputManager::SetupInput(UEnhancedInputComponent* Input)
{
	if (!Input) return;

	// ── 移动/视角 ──
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &USKInputManager::OnMoveInput);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &USKInputManager::OnMoveCompletedInput);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &USKInputManager::OnLookInput);

	// ── 跳跃 ──
	Input->BindAction(JumpAction, ETriggerEvent::Started,   this, &USKInputManager::OnJumpStartedInput);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &USKInputManager::OnJumpCompletedInput);

	// ── 闪避/冲刺 ──
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &USKInputManager::OnDodgeStartedInput);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &USKInputManager::OnDodgeCompletedInput);
	Input->BindAction(WalkModifierAction, ETriggerEvent::Started, this, &USKInputManager::OnWalkModifierStartedInput);
	Input->BindAction(WalkModifierAction, ETriggerEvent::Completed, this, &USKInputManager::OnWalkModifierCompletedInput);

	// ── 蹲下 ──
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &USKInputManager::OnCrouchStartedInput);

	// ── 战斗 ──
	Input->BindAction(AttackAction,     ETriggerEvent::Started,   this, &USKInputManager::OnAttackStartedInput);
	Input->BindAction(AttackAction,     ETriggerEvent::Completed, this, &USKInputManager::OnAttackCompletedInput);
	Input->BindAction(GuardAction,      ETriggerEvent::Started,   this, &USKInputManager::OnGuardStartedInput);
	Input->BindAction(GuardAction,      ETriggerEvent::Completed, this, &USKInputManager::OnGuardCompletedInput);
	Input->BindAction(LockOnAction,     ETriggerEvent::Started,   this, &USKInputManager::OnLockOnStartedInput);
	Input->BindAction(ProstheticAction, ETriggerEvent::Started,   this, &USKInputManager::OnProstheticStartedInput);
	Input->BindAction(ProstheticAction, ETriggerEvent::Completed, this, &USKInputManager::OnProstheticCompletedInput);
	Input->BindAction(GrappleAction,    ETriggerEvent::Started,   this, &USKInputManager::OnGrappleStartedInput);

	// ── 交互/道具 ──
	Input->BindAction(InteractAction,      ETriggerEvent::Started, this, &USKInputManager::OnInteractStartedInput);
	Input->BindAction(UseItemAction,       ETriggerEvent::Started, this, &USKInputManager::OnUseItemStartedInput);
	Input->BindAction(HealingGourdAction,  ETriggerEvent::Started, this, &USKInputManager::OnHealingGourdStartedInput);
	Input->BindAction(CycleItemNextAction, ETriggerEvent::Started, this, &USKInputManager::OnCycleItemNextStartedInput);
	Input->BindAction(CycleItemPrevAction, ETriggerEvent::Started, this, &USKInputManager::OnCycleItemPrevStartedInput);

	// ── 系统 ──
	Input->BindAction(PauseAction, ETriggerEvent::Started, this, &USKInputManager::OnPauseStartedInput);
	Input->BindAction(MenuAction,  ETriggerEvent::Started, this, &USKInputManager::OnMenuStartedInput);
}

void USKInputManager::AddMappingContext(APlayerController* PC)
{
	if (!PC || !DefaultMappingContext) return;

	if (UEnhancedInputLocalPlayerSubsystem* Sub =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Sub->AddMappingContext(DefaultMappingContext, 0);
	}
}

/**
 * 增加一个禁战区域引用计数，并在首次进入时仅发布受限状态。
 * 本函数不清理输入、不改变姿态或移动档位，也不播放武器动画；具体策略由 Lua 编排。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void USKInputManager::EnterRestrictedZone()
{
    RestrictedZoneCount++;
}

/**
 * 减少一个禁战区域引用计数；计数归零只代表退出全部区域，不主动恢复任何输入状态。
 * 多余离开调用会被限制在零并记录告警，必须在游戏线程调用。
 */
void USKInputManager::ExitRestrictedZone()
{
    if (RestrictedZoneCount <= 0)
    {
        RestrictedZoneCount = 0;
        UE_LOG(LogTemp, Warning, TEXT("SKInputManager received unmatched restricted-zone exit. Owner=%s"), *GetNameSafe(GetOwner()));
        return;
    }

    RestrictedZoneCount--;
}

/**
 * 查询角色是否位于至少一个禁战区域，不修改输入状态。
 *
 * @return 区域计数大于零时返回 true，否则返回 false。
 */
bool USKInputManager::IsRestrictedZoneActive() const
{
    return RestrictedZoneCount > 0;
}

/**
 * 查询当前成对登记的禁战区域数量，不修改输入状态。
 *
 * @return 非负区域计数；多个区域重叠时可能大于一。
 */
int32 USKInputManager::GetRestrictedZoneCount() const
{
    return RestrictedZoneCount;
}

/**
 * 清除收拔刀过渡期间禁止的上半身战斗与新 Dodge/Step 意图，并移除同名缓冲条目。
 * 函数会结束攻击、防御、义手和冲刺按住状态，但不会清除 bDodgeActive 或角色 Dodging，
 * 因而已经开始的 Step 可以完成；必须由 Lua 在游戏线程按区域边沿显式调用。
 */
void USKInputManager::ClearRestrictedActionStateForScript()
{
    bAttackPressed = false;
    bGrapplePressed = false;
    bProstheticPressed = false;
    bDodgePressed = false;
    bAttackHeld = false;
    bGuardHeld = false;
    bProstheticHeld = false;
    bDodgeHeld = false;
    AttackHoldTime = 0.f;
    ProstheticHoldTime = 0.f;
    DodgeHoldTime = 0.f;

    InputBuffer.RemoveAll([](const FSKBufferedInput& Entry)
    {
        return IsSKRestrictedCombatAction(Entry.Action)
            || Entry.Action.ToString().Equals(TEXT("Dodge"), ESearchCase::IgnoreCase);
    });
}

/**
 * 查询所属角色 WeaponManager 最近启动的上半身 Slot 动画是否仍在播放或混合。
 * 本接口只提供跨组件只读桥接，不决定退出区域后的输入锁策略；该策略由 Lua 编排。
 * 必须在游戏线程调用。
 *
 * @return WeaponManager 存在且其 Slot 动画仍在播放时返回 true，否则返回 false。
 */
bool USKInputManager::IsOwnerWeaponSlotAnimationPlaying() const
{
    const ASKCharacter* Character = Cast<ASKCharacter>(GetOwner());
    const USKWeaponManagerComponent* WeaponManager = Character ? Character->GetWeaponManager() : nullptr;
    return WeaponManager && WeaponManager->IsCharacterSlotAnimationPlaying();
}

/**
 * 查询所属角色当前武器的展示状态并转换为稳定名称，供 Lua 判断 Drawn/Sheathed。
 * 本接口不切换挂载、不播放动画；角色、管理器或武器缺失时返回 None，避免脚本误判为已拔刀。
 * 必须在游戏线程调用。
 *
 * @return Drawn、Sheathed 或依赖缺失时的 None。
 */
FName USKInputManager::GetOwnerWeaponPresentationName() const
{
    const ASKCharacter* Character = Cast<ASKCharacter>(GetOwner());
    const USKWeaponManagerComponent* WeaponManager = Character ? Character->GetWeaponManager() : nullptr;
    const ASKWeapon* Weapon = WeaponManager ? WeaponManager->GetCurrentWeapon() : nullptr;
    if (!Weapon) return NAME_None;

    return Weapon->GetWeaponPresentation() == ESKWeaponPresentation::Sheathed
        ? FName(TEXT("Sheathed"))
        : FName(TEXT("Drawn"));
}

/**
 * 按稳定原因名增删一个外部玩法输入锁，允许多个系统安全叠加所有权。
 * 首个锁生效时立即清理现有玩法输入，避免旧 Held、缓冲或移动意图在锁内继续执行；
 * 移除未知原因是幂等操作，Reason 为 None 时拒绝修改。只能在游戏线程调用。
 *
 * @param Reason 调用系统拥有的非 None 原因名；调用方只能释放自己添加的原因。
 * @param bLocked true 添加原因，false 移除原因。
 */
void USKInputManager::SetExternalInputLock(FName Reason, bool bLocked)
{
    if (Reason.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("SKInputManager rejected external input lock with None reason. Owner=%s"), *GetNameSafe(GetOwner()));
        return;
    }

    const bool bWasLocked = IsExternalInputLocked();
    if (bLocked)
    {
        ExternalInputLockReasons.Add(Reason);
    }
    else
    {
        ExternalInputLockReasons.Remove(Reason);
    }

    if (!bWasLocked && IsExternalInputLocked()) ClearAllGameplayInputForScript();
}

/**
 * 查询是否存在至少一个外部玩法输入锁，不修改锁集合或输入状态。
 * 只能在游戏线程读取。
 *
 * @return 任一原因仍持有锁时返回 true，否则返回 false。
 */
bool USKInputManager::IsExternalInputLocked() const
{
    return !ExternalInputLockReasons.IsEmpty();
}

/**
 * 清除移动、动作按下/按住、缓冲、连段和 Dodge 活动窗口，并终止角色跳跃与冲刺意图。
 * 当前仍按住的成对物理动作会被锁存到释放集合，确保解锁后仅收到旧 Completed 边沿时不会误触发；
 * LookIntent、Pause、Menu 和锁原因集合保持不变。本函数只清理意图，不停止战斗动画或 Root Motion，
 * 只能在游戏线程调用。
 */
void USKInputManager::ClearAllGameplayInputForScript()
{
    if (!MoveIntent.IsNearlyZero()) SuppressedActionsUntilRelease.Add(TEXT("Move"));
    if (bAttackHeld) SuppressedActionsUntilRelease.Add(TEXT("Attack"));
    if (bGuardHeld) SuppressedActionsUntilRelease.Add(TEXT("Guard"));
    if (bDodgeHeld) SuppressedActionsUntilRelease.Add(TEXT("Dodge"));
    if (bWalkHeld) SuppressedActionsUntilRelease.Add(TEXT("Walk"));
    if (bProstheticHeld) SuppressedActionsUntilRelease.Add(TEXT("Prosthetic"));

    MoveIntent = FVector2D::ZeroVector;
    MoveInputAmount = 0.f;
    MoveInputReleaseBufferRemaining = 0.f;

    bAttackPressed = false;
    bJumpPressed = false;
    bDodgePressed = false;
    bInteractPressed = false;
    bUseItemPressed = false;
    bHealingGourdPressed = false;
    bGrapplePressed = false;
    bProstheticPressed = false;
    bLockOnPressed = false;
    bCrouchToggled = false;
    bCycleItemNext = false;
    bCycleItemPrev = false;

    bAttackHeld = false;
    bGuardHeld = false;
    bDodgeHeld = false;
    bWalkHeld = false;
    bProstheticHeld = false;

    AttackHoldTime = 0.f;
    ProstheticHoldTime = 0.f;
    DodgeHoldTime = 0.f;
    DodgeActiveTimeRemaining = 0.f;
    bDodgeActive = false;
    ComboIndex = 0;
    TimeSinceLastAttack = 0.f;
    AttackPressedTimeSeconds = 0.0;
    GuardPressedTimeSeconds = 0.0;
    ActiveAttackInputSerial = 0;
    ActiveGuardInputSerial = 0;
    InputBuffer.Reset();

    ACharacter* Owner = OwnerCharacter.Get();
    if (!Owner) return;

    Owner->ConsumeMovementInputVector();
    Owner->StopJumping();
    if (ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner))
    {
        SekiroOwner->SetDodging(false);
    }
    if (USKMovementComponent* MovementComponent = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
    {
        MovementComponent->CurrentMovementTier = ESKMovementTier::Idle;
    }
}

//////////////////////////////////////////////////////////////////////////
// 消费型意图

bool USKInputManager::ConsumeAttackPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bAttackPressed)
	{
		bAttackPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeJumpPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bJumpPressed)
	{
		bJumpPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeDodgePressed()
{
	if (IsExternalInputLocked()) return false;
	if (bDodgePressed)
	{
		bDodgePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeInteractPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bInteractPressed)
	{
		bInteractPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeUseItemPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bUseItemPressed)
	{
		bUseItemPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeHealingGourdPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bHealingGourdPressed)
	{
		bHealingGourdPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeGrapplePressed()
{
	if (IsExternalInputLocked()) return false;
	if (bGrapplePressed)
	{
		bGrapplePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeProstheticPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bProstheticPressed)
	{
		bProstheticPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeLockOnPressed()
{
	if (IsExternalInputLocked()) return false;
	if (bLockOnPressed)
	{
		bLockOnPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCrouchToggled()
{
	if (IsExternalInputLocked()) return false;
	if (bCrouchToggled)
	{
		bCrouchToggled = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCycleItemNext()
{
	if (IsExternalInputLocked()) return false;
	if (bCycleItemNext)
	{
		bCycleItemNext = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCycleItemPrev()
{
	if (IsExternalInputLocked()) return false;
	if (bCycleItemPrev)
	{
		bCycleItemPrev = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumePausePressed()
{
	if (bPausePressed)
	{
		bPausePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeMenuPressed()
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

bool USKInputManager::ConsumeBufferedInput(FName Action)
{
	if (IsExternalInputLocked()) return false;

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

void USKInputManager::ClearInputBuffer()
{
	InputBuffer.Reset();
}

//////////////////////////////////////////////////////////////////////////
// 持续型意图

FVector2D USKInputManager::GetMoveIntent() const
{
	return IsExternalInputLocked() ? FVector2D::ZeroVector : MoveIntent;
}

float USKInputManager::GetMoveInputAmount() const
{
	return IsExternalInputLocked() ? 0.f : MoveInputAmount;
}

FVector2D USKInputManager::GetLookIntent() const
{
	return LookIntent;
}

bool USKInputManager::IsAttackHeld() const
{
	return !IsExternalInputLocked() && bAttackHeld;
}

bool USKInputManager::IsGuardHeld() const
{
	return !IsExternalInputLocked() && bGuardHeld;
}

bool USKInputManager::IsProstheticHeld() const
{
	return !IsExternalInputLocked() && bProstheticHeld;
}

bool USKInputManager::IsDodgeHeld() const
{
	return !IsExternalInputLocked() && bDodgeHeld;
}

bool USKInputManager::IsDodgeActive() const
{
	return !IsExternalInputLocked() && bDodgeActive;
}

bool USKInputManager::IsWalkHeld() const
{
	return !IsExternalInputLocked() && bWalkHeld;
}

float USKInputManager::GetAttackHoldTime() const
{
	return IsExternalInputLocked() ? 0.f : AttackHoldTime;
}

float USKInputManager::GetProstheticHoldTime() const
{
	return IsExternalInputLocked() ? 0.f : ProstheticHoldTime;
}

//////////////////////////////////////////////////////////////////////////
// 连段

int32 USKInputManager::GetComboIndex() const
{
	return ComboIndex;
}

float USKInputManager::GetTimeSinceLastAttack() const
{
	return TimeSinceLastAttack;
}

//////////////////////////////////////////////////////////////////////////
// Lua 输入宿主

void USKInputManager::SetLuaInputModuleName(const FString& ModuleName)
{
	LuaInputModuleName = ModuleName;
}

FString USKInputManager::GetLuaInputModuleName() const
{
	return LuaInputModuleName;
}

FString USKInputManager::GetModuleName_Implementation() const
{
	return LuaInputModuleName;
}

float USKInputManager::GetSprintHoldThreshold() const
{
	return SprintHoldThreshold;
}

float USKInputManager::GetDodgeActiveDuration() const
{
	return DodgeActiveDuration;
}

float USKInputManager::GetMoveInputReleaseBufferDuration() const
{
	return MoveInputReleaseBufferDuration;
}

float USKInputManager::GetMoveInputReleaseBufferRemaining() const
{
	return MoveInputReleaseBufferRemaining;
}

void USKInputManager::SetMoveInputReleaseBufferRemaining(float RemainingTime)
{
	if (IsExternalInputLocked()) return;
	MoveInputReleaseBufferRemaining = FMath::Max(0.f, RemainingTime);
}

float USKInputManager::GetAnalogWalkEnterThreshold() const
{
	return AnalogWalkEnterThreshold;
}

float USKInputManager::GetAnalogRunEnterThreshold() const
{
	return AnalogRunEnterThreshold;
}

float USKInputManager::GetDodgeHoldTime() const
{
	return DodgeHoldTime;
}

void USKInputManager::SetDodgeHoldTime(float NewDodgeHoldTime)
{
	if (IsExternalInputLocked()) return;
	DodgeHoldTime = FMath::Max(0.f, NewDodgeHoldTime);
}

float USKInputManager::GetDodgeActiveTimeRemaining() const
{
	return DodgeActiveTimeRemaining;
}

void USKInputManager::SetDodgeActiveState(bool bNewDodgeActive, float ActiveTimeRemaining)
{
	if (IsExternalInputLocked() && bNewDodgeActive) return;
	bDodgeActive = bNewDodgeActive;
	DodgeActiveTimeRemaining = FMath::Max(0.f, ActiveTimeRemaining);
}

void USKInputManager::SetAttackHoldTime(float NewAttackHoldTime)
{
	if (IsExternalInputLocked()) return;
	AttackHoldTime = FMath::Max(0.f, NewAttackHoldTime);
}

void USKInputManager::SetProstheticHoldTime(float NewProstheticHoldTime)
{
	if (IsExternalInputLocked()) return;
	ProstheticHoldTime = FMath::Max(0.f, NewProstheticHoldTime);
}

void USKInputManager::SetComboState(int32 NewComboIndex, float NewTimeSinceLastAttack)
{
	if (IsExternalInputLocked()) return;
	ComboIndex = FMath::Max(0, NewComboIndex);
	TimeSinceLastAttack = FMath::Max(0.f, NewTimeSinceLastAttack);
}

float USKInputManager::GetWorldTimeSecondsForScript() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

/**
 * 查询与输入组件属于同一 Actor 的战斗动作宿主，供 Lua 在执行物理跳跃前完成动作仲裁。
 * 本函数只在游戏线程执行组件查找，不创建组件、不缓存引用，也不改变战斗状态。
 *
 * @return 找到时返回由 Owner 持有的 USKCombatComponent；Owner 或组件不存在时返回 nullptr。
 */
USKCombatComponent* USKInputManager::GetOwnerCombatComponent() const
{
	AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<USKCombatComponent>() : nullptr;
}

void USKInputManager::SetMoveIntentForScript(float InputX, float InputY, float InputAmount, float ReleaseBufferRemaining)
{
	if (IsExternalInputLocked())
	{
		ClearMoveIntentForScript();
		return;
	}

	MoveIntent = FVector2D(InputX, InputY);
	MoveInputAmount = FMath::Clamp(InputAmount, 0.f, 1.f);
	MoveInputReleaseBufferRemaining = FMath::Max(0.f, ReleaseBufferRemaining);
}

void USKInputManager::ClearMoveIntentForScript()
{
	MoveIntent = FVector2D::ZeroVector;
	MoveInputAmount = 0.f;
	MoveInputReleaseBufferRemaining = 0.f;
}

void USKInputManager::SetLookIntentForScript(float InputX, float InputY)
{
	LookIntent = FVector2D(InputX, InputY);
}

bool USKInputManager::AddMovementInputFromScreen(float InputX, float InputY)
{
	if (IsExternalInputLocked()) return false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	const AController* Controller = Owner->GetController();
	if (!Controller) return false;

	const FVector2D Input(InputX, InputY);
	if (Input.IsNearlyZero()) return false;

	const FVector2D Normalized = Input.GetSafeNormal();
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	Owner->AddMovementInput(Forward, Normalized.Y);
	Owner->AddMovementInput(Right, Normalized.X);
	return true;
}

bool USKInputManager::AddMovementImpulseFromScreen(float InputX, float InputY, float VelocityChange)
{
	if (IsExternalInputLocked()) return false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
	if (!Movement) return false;

	FVector WorldDirection = FVector::ZeroVector;
	if (!ResolveSKScreenInputWorldDirection(Owner, InputX, InputY, WorldDirection)) return false;

	const float SafeVelocityChange = FMath::Max(0.f, VelocityChange);
	if (SafeVelocityChange <= UE_KINDA_SMALL_NUMBER) return false;

	Movement->AddImpulse(WorldDirection * SafeVelocityChange, true);
	return true;
}

/**
 * 按控制器朝向把屏幕空间输入转换为世界方向，并精确替换角色当前水平速度。
 * 该接口只提供通用物理写入，不决定何时起跳或使用何种速度；应在游戏线程、CharacterMovement 求值前调用。
 * 水平速度为零时允许输入轴同时为零，用于明确清除上一帧残留惯性；垂直速度始终保持不变。
 * 非零写入会把本帧 MaxWalkSpeed 至少提高到目标速度，避免输入和 Movement Lua 同帧更新时被旧速度上限截断；后续帧仍由 Movement 策略覆盖。
 *
 * @param InputX 屏幕空间横向输入，通常为 [-1, 1]，负数表示左移。
 * @param InputY 屏幕空间纵向输入，通常为 [-1, 1]，负数表示后退。
 * @param HorizontalSpeed 目标水平速度，单位 cm/s；负值按零处理。
 * @return 成功写入或清除水平速度时返回 true；角色、移动组件或非零输入方向无效时返回 false。
 */
bool USKInputManager::SetHorizontalVelocityFromScreen(float InputX, float InputY, float HorizontalSpeed)
{
	if (IsExternalInputLocked()) return false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
	if (!Movement) return false;

	const float SafeHorizontalSpeed = FMath::Max(0.f, HorizontalSpeed);
	if (SafeHorizontalSpeed <= UE_KINDA_SMALL_NUMBER)
	{
		Movement->Velocity.X = 0.f;
		Movement->Velocity.Y = 0.f;
		return true;
	}

	FVector WorldDirection = FVector::ZeroVector;
	if (!ResolveSKScreenInputWorldDirection(Owner, InputX, InputY, WorldDirection)) return false;

	const FVector HorizontalVelocity = WorldDirection * SafeHorizontalSpeed;
	Movement->MaxWalkSpeed = FMath::Max(Movement->MaxWalkSpeed, SafeHorizontalSpeed);
	Movement->Velocity.X = HorizontalVelocity.X;
	Movement->Velocity.Y = HorizontalVelocity.Y;
	return true;
}

/**
 * 切换所属角色动画实例的 Root Motion 消费模式，供 Lua 在物理跳跃发生前明确转移位移所有权。
 * 忽略模式只阻止动画 Root Motion 覆盖 CharacterMovement 速度，不停止动画姿势、曲线或 Notify 求值。
 * 只能在游戏线程调用；Lua 必须在角色落地后恢复 RootMotionFromEverything，避免影响地面动画位移。
 *
 * @param bIgnored true 使用 IgnoreRootMotion，false 恢复 RootMotionFromEverything。
 * @return 成功找到角色 Mesh 和 AnimInstance 并设置模式时返回 true，否则返回 false。
 */
bool USKInputManager::SetOwnerAnimRootMotionIgnored(bool bIgnored)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner || !Owner->GetMesh()) return false;

	UAnimInstance* AnimInstance = Owner->GetMesh()->GetAnimInstance();
	if (!AnimInstance) return false;

	AnimInstance->SetRootMotionMode(
		bIgnored ? ERootMotionMode::IgnoreRootMotion : ERootMotionMode::RootMotionFromEverything);
	return true;
}

bool USKInputManager::AddLookInputToCamera(float InputX, float InputY)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	const FVector2D LookAxis(InputX, InputY);
	if (USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>())
	{
		CameraManager->AddLookInput(LookAxis);
		return true;
	}

	Owner->AddControllerYawInput(LookAxis.X);
	Owner->AddControllerPitchInput(LookAxis.Y);
	return true;
}

bool USKInputManager::IsOwnerFalling() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	return Owner && Owner->GetCharacterMovement() && Owner->GetCharacterMovement()->IsFalling();
}

bool USKInputManager::IsOwnerCrouched() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	return Owner && Owner->bIsCrouched;
}

bool USKInputManager::IsOwnerDodging() const
{
	const ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	return SekiroOwner && SekiroOwner->IsDodging();
}

bool USKInputManager::CanOwnerAirDodge() const
{
	const ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	return SekiroOwner && SekiroOwner->CanAirDodge();
}

void USKInputManager::JumpOwner()
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Jump();
	}
}

void USKInputManager::StopJumpingOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->StopJumping();
	}
}

void USKInputManager::CrouchOwner()
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Crouch();
	}
}

void USKInputManager::UnCrouchOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->UnCrouch();
	}
}

void USKInputManager::SetOwnerDodging(bool bNewDodging)
{
	if (IsExternalInputLocked() && bNewDodging) return;

	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	if (SekiroOwner)
	{
		SekiroOwner->SetDodging(bNewDodging);
	}
}

void USKInputManager::SetOwnerDodgeDirection(float ForwardAmount, float LateralAmount)
{
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	if (SekiroOwner)
	{
		SekiroOwner->SetDodgeDirection(ForwardAmount, LateralAmount);
	}
}

FName USKInputManager::GetMovementTierName() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return FName(TEXT("Run"));

	const USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return FName(TEXT("Run"));

	switch (MoveComp->CurrentMovementTier)
	{
	case ESKMovementTier::Idle:
		return FName(TEXT("Idle"));
	case ESKMovementTier::Walk:
		return FName(TEXT("Walk"));
	case ESKMovementTier::Run:
		return FName(TEXT("Run"));
	case ESKMovementTier::Sprint:
		return FName(TEXT("Sprint"));
	case ESKMovementTier::Crouch:
		return FName(TEXT("Crouch"));
	default:
		return FName(TEXT("Run"));
	}
}

void USKInputManager::SetMovementTierByName(FName TierName)
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return;

	MoveComp->CurrentMovementTier = ResolveMovementTierByName(TierName);
}

void USKInputManager::SetPressedFlag(FName ActionName, bool bPressed)
{
	const FString NormalizedName = ActionName.ToString().ToLower();
	if (IsExternalInputLocked()
		&& NormalizedName != TEXT("pause")
		&& NormalizedName != TEXT("menu"))
	{
		return;
	}

	if (NormalizedName == TEXT("attack")) bAttackPressed = bPressed;
	else if (NormalizedName == TEXT("jump")) bJumpPressed = bPressed;
	else if (NormalizedName == TEXT("dodge")) bDodgePressed = bPressed;
	else if (NormalizedName == TEXT("interact")) bInteractPressed = bPressed;
	else if (NormalizedName == TEXT("useitem") || NormalizedName == TEXT("item")) bUseItemPressed = bPressed;
	else if (NormalizedName == TEXT("healinggourd")) bHealingGourdPressed = bPressed;
	else if (NormalizedName == TEXT("grapple")) bGrapplePressed = bPressed;
	else if (NormalizedName == TEXT("prosthetic")) bProstheticPressed = bPressed;
	else if (NormalizedName == TEXT("lockon")) bLockOnPressed = bPressed;
	else if (NormalizedName == TEXT("crouch")) bCrouchToggled = bPressed;
	else if (NormalizedName == TEXT("cycleitemnext")) bCycleItemNext = bPressed;
	else if (NormalizedName == TEXT("cycleitemprev")) bCycleItemPrev = bPressed;
	else if (NormalizedName == TEXT("pause")) bPausePressed = bPressed;
	else if (NormalizedName == TEXT("menu")) bMenuPressed = bPressed;
}

void USKInputManager::SetHeldFlag(FName ActionName, bool bHeld)
{
	if (IsExternalInputLocked()) return;

	const FString NormalizedName = ActionName.ToString().ToLower();
	if (NormalizedName == TEXT("attack")) bAttackHeld = bHeld;
	else if (NormalizedName == TEXT("guard")) bGuardHeld = bHeld;
	else if (NormalizedName == TEXT("dodge")) bDodgeHeld = bHeld;
	else if (NormalizedName == TEXT("walk")) bWalkHeld = bHeld;
	else if (NormalizedName == TEXT("prosthetic")) bProstheticHeld = bHeld;
}

void USKInputManager::AddBufferedInput(FName Action, int32 Priority, float Lifetime)
{
	if (IsExternalInputLocked()) return;

	FSKBufferedInput Entry;
	Entry.Action = Action;
	Entry.Priority = Priority;
	Entry.Timestamp = GetWorldTimeSecondsForScript();
	Entry.Lifetime = FMath::Max(0.f, Lifetime);
	InputBuffer.Add(Entry);
}

void USKInputManager::PruneInputBufferForScript()
{
	const float Now = GetWorldTimeSecondsForScript();
	InputBuffer.RemoveAll([Now](const FSKBufferedInput& Entry) {
		return (Now - Entry.Timestamp) > Entry.Lifetime;
	});
	while (InputBuffer.Num() > 6)
	{
		InputBuffer.RemoveAt(0);
	}
}

void USKInputManager::ClearPressedFlagsForScript()
{
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

bool USKInputManager::ToggleLockTargetInViewForScript()
{
	if (IsExternalInputLocked()) return false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>();
	return CameraManager && CameraManager->ToggleLockTargetInView();
}

//////////////////////////////////////////////////////////////////////////
// 生命周期

/**
 * 缓存所属角色，并为原生 UnLua 组件补发一次标准 ReceiveBeginPlay 生命周期。
 * UE 只会在蓝图生成类或非原生组件的 UActorComponent::BeginPlay 中派发 ReceiveBeginPlay，
 * 因此本函数仅为纯原生类补发；蓝图子类继续使用引擎派发路径，避免 Lua 初始化执行两次。
 * 本函数由 UE 在游戏线程调用，不绑定输入，也不直接调用 Lua Initialize。
 */
void USKInputManager::BeginPlay()
{
	const bool bEngineDispatchesReceiveBeginPlay =
		GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
		|| !GetClass()->HasAnyClassFlags(CLASS_Native);

	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	UE_LOG(LogTemp, Log, TEXT("InputManager[%s]: BeginPlay Owner=%s Class=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OwnerCharacter.Get()),
		*GetNameSafe(OwnerCharacter.IsValid() ? OwnerCharacter->GetClass() : nullptr));

	if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

void USKInputManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	HandleInputTick(DeltaTime);
}

/**
 * 执行输入组件每帧的原生回退维护，包括长按计时、输入缓冲清理、冲刺状态和一次性标记清零。
 * Lua 未覆盖 HandleInputTick 时由反射系统调用；覆盖时 Lua 负责以等价时机维护这些状态。
 *
 * @param DeltaTime 本帧游戏线程 Tick 的间隔秒数。
 * @return 无返回值。
 * @thread 仅在游戏线程的 UActorComponent Tick 阶段调用，可访问角色和移动组件状态。
 */
void USKInputManager::HandleInputTick_Implementation(float DeltaTime)
{
	if (IsExternalInputLocked()) return;

	// ── 长按计时 ──
	if (bAttackHeld)
	{
		AttackHoldTime += DeltaTime;
	}
	if (bProstheticHeld)
	{
		ProstheticHoldTime += DeltaTime;
	}
	if (bDodgeHeld)
	{
		DodgeHoldTime += DeltaTime;
	}
	if (DodgeActiveTimeRemaining > 0.f)
	{
		DodgeActiveTimeRemaining = FMath::Max(0.f, DodgeActiveTimeRemaining - DeltaTime);
		if (DodgeActiveTimeRemaining <= 0.f)
		{
			bDodgeActive = false;
			if (ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get()))
			{
				SekiroOwner->SetDodging(false);
			}
		}
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
			if (bDodgeHeld
				&& DodgeHoldTime >= SprintHoldThreshold
				&& MoveIntent.Size() >= 0.1f
				&& !OwnerCharacter->GetCharacterMovement()->IsFalling()
				&& MoveComp->CurrentMovementTier != ESKMovementTier::Sprint)
			{
				MoveComp->CurrentMovementTier = ESKMovementTier::Sprint;
			}
			else if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
			{
				if (MoveIntent.Size() < 0.1f || OwnerCharacter->GetCharacterMovement()->IsFalling())
				{
					ApplyDesiredMovementTier(MoveInputAmount);
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

/**
 * Enhanced Input 适配器：将引擎专用 FInputActionValue 转换为反射入口所需的基础数值。
 * 其余 OnXxxInput 适配器遵循相同边界：不包含 Gameplay 决策，只在游戏线程将输入边沿分发给 HandleXxx。
 *
 * @param Value Enhanced Input 本次触发的动作值。
 * @return 无返回值。
 * @thread 仅在游戏线程的 Enhanced Input 分发阶段调用。
 */
void USKInputManager::OnMoveInput(const FInputActionValue& Value)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Move"));
		return;
	}
	if (SuppressedActionsUntilRelease.Contains(TEXT("Move"))) return;

	const FVector2D Input = Value.Get<FVector2D>();
	HandleMoveInput(Input.X, Input.Y);
}

void USKInputManager::OnMoveCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Move")) > 0;
	if (bWasSuppressed || IsExternalInputLocked())
	{
		ClearMoveIntentForScript();
		return;
	}

	HandleMoveCompleted();
}

void USKInputManager::OnLookInput(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	HandleLookInput(Input.X, Input.Y);
}

void USKInputManager::OnJumpStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Jump"));
		return;
	}
	HandleJumpStarted();
}

void USKInputManager::OnJumpCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Jump")) > 0;
	if (bWasSuppressed || IsExternalInputLocked()) return;
	HandleJumpCompleted();
}

void USKInputManager::OnDodgeStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Dodge"));
		return;
	}
	HandleDodgeStarted();
}

void USKInputManager::OnDodgeCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Dodge")) > 0;
	if (bWasSuppressed || IsExternalInputLocked()) return;
	HandleDodgeCompleted();
}

void USKInputManager::OnWalkModifierStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Walk"));
		return;
	}
	HandleWalkModifierStarted();
}

void USKInputManager::OnWalkModifierCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Walk")) > 0;
	if (bWasSuppressed || IsExternalInputLocked()) return;
	HandleWalkModifierCompleted();
}

void USKInputManager::OnCrouchStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleCrouchStarted();
}

void USKInputManager::OnAttackStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Attack"));
		return;
	}

	const double EventTimeSeconds = GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	++NextCombatInputSerial;
	if (NextCombatInputSerial <= 0) NextCombatInputSerial = 1;
	ActiveAttackInputSerial = NextCombatInputSerial;
	AttackPressedTimeSeconds = EventTimeSeconds;
	PublishCombatInputEvent(ESKCombatInputAction::Attack, ESKCombatInputPhase::Started, ActiveAttackInputSerial, EventTimeSeconds, 0.f);
	HandleAttackStarted();
}

void USKInputManager::OnAttackCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Attack")) > 0;
	if (bWasSuppressed || IsExternalInputLocked())
	{
		ActiveAttackInputSerial = 0;
		return;
	}

	const double EventTimeSeconds = GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	const float HoldDuration = ActiveAttackInputSerial > 0 ? static_cast<float>(FMath::Max(0.0, EventTimeSeconds - AttackPressedTimeSeconds)) : 0.f;
	PublishCombatInputEvent(ESKCombatInputAction::Attack, ESKCombatInputPhase::Completed, ActiveAttackInputSerial, EventTimeSeconds, HoldDuration);
	ActiveAttackInputSerial = 0;
	HandleAttackCompleted();
}

void USKInputManager::OnGuardStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Guard"));
		return;
	}

	const double EventTimeSeconds = GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	++NextCombatInputSerial;
	if (NextCombatInputSerial <= 0) NextCombatInputSerial = 1;
	ActiveGuardInputSerial = NextCombatInputSerial;
	GuardPressedTimeSeconds = EventTimeSeconds;
	PublishCombatInputEvent(ESKCombatInputAction::Guard, ESKCombatInputPhase::Started, ActiveGuardInputSerial, EventTimeSeconds, 0.f);
	HandleGuardStarted();
}

void USKInputManager::OnGuardCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Guard")) > 0;
	if (bWasSuppressed || IsExternalInputLocked())
	{
		ActiveGuardInputSerial = 0;
		return;
	}

	const double EventTimeSeconds = GetWorld() ? static_cast<double>(GetWorld()->GetTimeSeconds()) : 0.0;
	const float HoldDuration = ActiveGuardInputSerial > 0 ? static_cast<float>(FMath::Max(0.0, EventTimeSeconds - GuardPressedTimeSeconds)) : 0.f;
	PublishCombatInputEvent(ESKCombatInputAction::Guard, ESKCombatInputPhase::Completed, ActiveGuardInputSerial, EventTimeSeconds, HoldDuration);
	ActiveGuardInputSerial = 0;
	HandleGuardCompleted();
}

void USKInputManager::OnLockOnStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleLockOnStarted();
}

void USKInputManager::OnProstheticStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked())
	{
		SuppressedActionsUntilRelease.Add(TEXT("Prosthetic"));
		return;
	}
	HandleProstheticStarted();
}

void USKInputManager::OnProstheticCompletedInput(const FInputActionValue& /*Value*/)
{
	const bool bWasSuppressed = SuppressedActionsUntilRelease.Remove(TEXT("Prosthetic")) > 0;
	if (bWasSuppressed || IsExternalInputLocked()) return;
	HandleProstheticCompleted();
}

void USKInputManager::OnGrappleStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleGrappleStarted();
}

void USKInputManager::OnInteractStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleInteractStarted();
}

void USKInputManager::OnUseItemStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleUseItemStarted();
}

void USKInputManager::OnHealingGourdStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleHealingGourdStarted();
}

void USKInputManager::OnCycleItemNextStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleCycleItemNextStarted();
}

void USKInputManager::OnCycleItemPrevStartedInput(const FInputActionValue& /*Value*/)
{
	if (IsExternalInputLocked()) return;
	HandleCycleItemPrevStarted();
}

void USKInputManager::OnPauseStartedInput(const FInputActionValue& /*Value*/) { HandlePauseStarted(); }
void USKInputManager::OnMenuStartedInput(const FInputActionValue& /*Value*/) { HandleMenuStarted(); }

void USKInputManager::HandleMoveInput_Implementation(float InputX, float InputY)
{
	if (IsExternalInputLocked()) return;

	const FVector2D LuaInput(InputX, InputY);

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	const FVector2D Input = LuaInput;
	const float RawInputAmount = FMath::Clamp(Input.Size(), 0.f, 1.f);
	const bool bHasRawMoveInput = RawInputAmount > 0.1f;
	if (!bHasRawMoveInput) return;

	MoveInputAmount = RawInputAmount;
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
			if (MoveInputAmount < 0.1f || Owner->GetCharacterMovement()->IsFalling())
			{
				ApplyDesiredMovementTier(MoveInputAmount);
			}
		}
		else if (!Owner->GetCharacterMovement()->IsFalling())
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}

	// ── 闪避方向更新 ──
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner && SekiroOwner->IsDodging() && Input.Size() > 0.1f)
	{
		SekiroOwner->SetDodgeDirection(Normalized.Y, Normalized.X);
	}
}

/**
 * 处理 Enhanced Input 对 MoveAction 发出的 Completed 边沿。
 *
 * @param Value Completed 事件携带的动作值；释放语义由事件类型决定，因此本函数不依赖该值判断。
 * @return 无返回值；Lua 返回 true 时由脚本完成清理，否则执行原生回退逻辑。
 * @thread 仅在游戏线程的 Enhanced Input 分发阶段调用，可安全访问角色、移动组件与 UnLua。
 */
void USKInputManager::HandleMoveCompleted_Implementation()
{
	if (IsExternalInputLocked())
	{
		ClearMoveIntentForScript();
		return;
	}

	MoveIntent = FVector2D::ZeroVector;
	MoveInputAmount = 0.f;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner || Owner->GetCharacterMovement()->IsFalling()) return;

	ApplyDesiredMovementTier(0.f);
}

void USKInputManager::HandleLookInput_Implementation(float InputX, float InputY)
{
	const FVector2D LuaInput(InputX, InputY);

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	FVector2D LookAxis = LuaInput;
	LookIntent = LookAxis;

	USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>();
	if (CameraManager)
	{
		CameraManager->AddLookInput(LookAxis);
		return;
	}

	Owner->AddControllerYawInput(LookAxis.X);
	Owner->AddControllerPitchInput(LookAxis.Y);
}

//////////////////////////////////////////////////////////////////////////
// 跳跃回调

void USKInputManager::HandleJumpStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

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

void USKInputManager::HandleJumpCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->StopJumping();
	}
}

//////////////////////////////////////////////////////////////////////////
// 闪避/冲刺回调

void USKInputManager::HandleDodgeStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	bDodgeHeld = true;
	DodgeHoldTime = 0.f;

	// ── 长按后才进入冲刺；短按在松开时生成闪避输入 ──
	if (Owner->bIsCrouched)
	{
		Owner->UnCrouch();
	}
}

void USKInputManager::HandleDodgeCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	const bool bWasShortPress = DodgeHoldTime < SprintHoldThreshold;
	bDodgeHeld = false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (bWasShortPress)
	{
		QueueDodgePressed();
	}

	// ── 长按结束退出 Sprint；短按保持 Walk/Run 档位并交给 Dodge 动作处理 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ResolveMovementTierFromInput(MoveInputAmount);
	}
	DodgeHoldTime = 0.f;
}

void USKInputManager::HandleWalkModifierStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bWalkHeld = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier != ESKMovementTier::Sprint && !Owner->GetCharacterMovement()->IsFalling())
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}
}

void USKInputManager::HandleWalkModifierCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bWalkHeld = false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier == ESKMovementTier::Walk && !Owner->GetCharacterMovement()->IsFalling())
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 蹲下回调

void USKInputManager::HandleCrouchStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

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

void USKInputManager::HandleAttackStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

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

void USKInputManager::HandleAttackCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bAttackHeld = false;
	AttackHoldTime = 0.f;
}

void USKInputManager::HandleGuardStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bGuardHeld = true;

	// Guard 也入缓冲，但消费方优先使用 IsGuardHeld 持续状态
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Guard");
	Entry.Priority = 5;                          // ESKActionPriority::Guard
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::HandleGuardCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bGuardHeld = false;
}

void USKInputManager::HandleLockOnStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bLockOnPressed = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>())
	{
		CameraManager->ToggleLockTargetInView();
	}
}

void USKInputManager::HandleProstheticStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

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

void USKInputManager::HandleProstheticCompleted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bProstheticHeld = false;
	ProstheticHoldTime = 0.f;
}

void USKInputManager::HandleGrappleStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

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

void USKInputManager::HandleInteractStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bInteractPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Interact");
	Entry.Priority = 10;                         // ESKActionPriority::Deathblow
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::HandleUseItemStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bUseItemPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::HandleHealingGourdStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bHealingGourdPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::HandleCycleItemNextStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bCycleItemNext = true;
}

void USKInputManager::HandleCycleItemPrevStarted_Implementation()
{
	if (IsExternalInputLocked()) return;

	bCycleItemPrev = true;
}

//////////////////////////////////////////////////////////////////////////
// 系统回调

void USKInputManager::HandlePauseStarted_Implementation()
{

	bPausePressed = true;
}

void USKInputManager::HandleMenuStarted_Implementation()
{

	bMenuPressed = true;
}

/**
 * 将 Enhanced Input 战斗边沿转发给 Owner 的战斗组件。
 * 本函数只负责构造不可变事件并同步提交，不调用 Lua、不改变输入组件原有标记；
 * Owner 没有战斗组件时安全忽略，且仅允许在游戏线程输入回调中调用。
 *
 * @param Action Attack 或 Guard 抽象动作类型。
 * @param Phase Started 或 Completed 物理输入边沿。
 * @param InputSerial 同一次按下与释放共享的正整数序列号。
 * @param EventTimeSeconds 输入发生的游戏世界绝对秒数。
 * @param HoldDuration Completed 边沿的按住秒数，Started 应为零。
 */
void USKInputManager::PublishCombatInputEvent(
	ESKCombatInputAction Action,
	ESKCombatInputPhase Phase,
	int32 InputSerial,
	double EventTimeSeconds,
	float HoldDuration)
{
	if (IsExternalInputLocked()) return;

	AActor* Owner = GetOwner();
	USKCombatComponent* CombatComponent = Owner ? Owner->FindComponentByClass<USKCombatComponent>() : nullptr;
	if (!CombatComponent) return;

	FSKCombatInputEvent InputEvent;
	InputEvent.Action = Action;
	InputEvent.Phase = Phase;
	InputEvent.InputSerial = InputSerial;
	InputEvent.EventTimeSeconds = EventTimeSeconds;
	InputEvent.HoldDuration = FMath::Max(0.f, HoldDuration);
	CombatComponent->SubmitCombatInputEvent(InputEvent);
}

ESKMovementTier USKInputManager::ResolveMovementTierFromInput(float InputMagnitude) const
{
	if (IsExternalInputLocked()) return ESKMovementTier::Idle;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return ESKMovementTier::Run;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return ESKMovementTier::Run;

	if (Owner->bIsCrouched)
	{
		return ESKMovementTier::Crouch;
	}

	if (bDodgeHeld
		&& DodgeHoldTime >= SprintHoldThreshold
		&& InputMagnitude > 0.1f
		&& !Owner->GetCharacterMovement()->IsFalling())
	{
		return ESKMovementTier::Sprint;
	}

	if (InputMagnitude <= 0.1f)
	{
		return bWalkHeld ? ESKMovementTier::Walk : ESKMovementTier::Run;
	}

	if (bWalkHeld)
	{
		return ESKMovementTier::Walk;
	}

	if (MoveComp->CurrentMovementTier == ESKMovementTier::Walk)
	{
		return InputMagnitude >= AnalogRunEnterThreshold ? ESKMovementTier::Run : ESKMovementTier::Walk;
	}

	return InputMagnitude <= AnalogWalkEnterThreshold ? ESKMovementTier::Walk : ESKMovementTier::Run;
}

ESKMovementTier USKInputManager::ResolveMovementTierByName(FName TierName) const
{
	const FString NormalizedName = TierName.ToString().ToLower();
	if (NormalizedName == TEXT("idle")) return ESKMovementTier::Idle;
	if (NormalizedName == TEXT("walk")) return ESKMovementTier::Walk;
	if (NormalizedName == TEXT("sprint")) return ESKMovementTier::Sprint;
	if (NormalizedName == TEXT("crouch")) return ESKMovementTier::Crouch;
	return ESKMovementTier::Run;
}

void USKInputManager::ApplyDesiredMovementTier(float InputMagnitude)
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return;

	MoveComp->CurrentMovementTier = ResolveMovementTierFromInput(InputMagnitude);
}

void USKInputManager::QueueDodgePressed()
{
	if (IsExternalInputLocked()) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner)
	{
		const bool bInAir = Owner->GetCharacterMovement()->IsFalling();
		if (bInAir && !SekiroOwner->CanAirDodge())
		{
			return;
		}

		SekiroOwner->SetDodgeDirection(MoveIntent.Y, MoveIntent.X);
		SekiroOwner->SetDodging(true);
	}

	bDodgeActive = true;
	DodgeActiveTimeRemaining = DodgeActiveDuration;
	bDodgePressed = true;

	FSKBufferedInput Entry;
	Entry.Action = TEXT("Dodge");
	Entry.Priority = 7;                          // ESKActionPriority::Dodge
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}
