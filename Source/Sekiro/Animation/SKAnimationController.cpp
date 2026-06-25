// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SKAnimationController.h"
#include "SekiroAnimLogicData.h"
#include "Character/SKCharacter.h"
#include "Input/SKInputHandler.h"
#include "Animation/SKAnimInstance.h"
#include "Weapon/SKWeapon.h"
#include "Weapon/SKWeaponComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

// ============================================================================
// USKAnimationController
// ============================================================================

USKAnimationController::USKAnimationController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
}

FName USKAnimationController::GetCurrentAction() const { return CurrentAction; }
int32 USKAnimationController::GetCurrentAnimID() const { return CurrentAnimID; }

// ── 生命周期 ──────────────────────────────────────────────

void USKAnimationController::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<ASKCharacter>(GetOwner());
    if (!OwnerCharacter.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: Owner is not ASKCharacter!"),
            *GetNameSafe(GetOwner()));
        return;
    }

    InputHandler = OwnerCharacter->GetInputHandler();
    Mesh = OwnerCharacter->GetMesh();

    if (!AnimLogicData)
    {
        AnimLogicData = LoadObject<USKAnimationLogicData>(nullptr,
            TEXT("/Game/Characters/Sekiro/DA_Sekiro_AnimLogic.DA_Sekiro_AnimLogic"));
    }

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Initialized"), *GetNameSafe(OwnerCharacter.Get()));
}

void USKAnimationController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateFrameState();

    static int32 TickCounter = 0;
    if (++TickCounter % 10 == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("AnimTick[%s]: State=%d Action=%s AnimID=%d Priority=%d"),
            *GetNameSafe(OwnerCharacter.Get()),
            (int32)CurrentState, *CurrentAction.ToString(),
            CurrentAnimID, CurrentPriority);
        if (CurrentAnimID > 0)
        {
            TObjectPtr<UAnimSequence>* FoundSeq = MontageCache.Find(CurrentAnimID);
            UE_LOG(LogTemp, Log, TEXT("  CacheHit=%s AnimLogicData=%s"),
                FoundSeq && *FoundSeq ? TEXT("yes") : TEXT("no"),
                AnimLogicData ? TEXT("ok") : TEXT("null"));
        }
    }

    UpdateAttackHitbox();
    UpdateChargeState(DeltaTime);
    UpdateContextFlags();
    ProcessIntents();
    ProcessLocomotion();
}

// ── 帧级更新 ──────────────────────────────────────────────

void USKAnimationController::UpdateFrameState()
{
    if (!Mesh.IsValid()) return;
    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;
    UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
    if (ActiveMontage)
    {
        CurrentAnimTime = AnimInst->Montage_GetPosition(ActiveMontage);
    }
}

// ── 上下文标志更新 ──────────────────────────────────────────

void USKAnimationController::UpdateContextFlags()
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) { bIsInAir = false; return; }

    bool bPrevInAir = bIsInAir;
    bIsInAir = AnimInst->bIsInAir;

    // 落地检测
    if (bPrevInAir && !bIsInAir)
    {
        if (CurrentState == ESKCharacterState::Airborne)
        {
            UE_LOG(LogTemp, Log, TEXT("State: Airborne -> Idle (landed)"));
            TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
        }
        bWasInAir = false;
    }

    // bCounterWindow 超时清除
    if (bCounterWindow && CurrentAction != TEXT("Deflect") && CurrentAction != TEXT("Attack_Counter"))
    {
        bCounterWindow = false;
    }
}

// ── 状态迁移 ──────────────────────────────────────────────

void USKAnimationController::TransitionTo(ESKCharacterState NewState, FName Action, int32 Priority)
{
    if (CurrentAnimID > 0 && Action != CurrentAction && Action != NAME_None)
    {
        float Crossfade;
        USKAnimInstance* AnimInst = GetAnimInstance();
        if (!AnimInst || !AnimInst->CanCancelTo(Action, Crossfade))
            return;
    }

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0) return;

    UE_LOG(LogTemp, Log, TEXT("State: %d -> %d (Action=%s AnimID=%d)"),
        (int32)CurrentState, (int32)NewState, *Action.ToString(), AnimID);

    PlayMontageByID(AnimID, 0.1f);
    CurrentState = NewState;
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
}

bool USKAnimationController::CanTransition(FName Action, int32& OutPriority)
{
    // 从 Action 映射优先级
    static const TMap<FName, int32> PriorityMap = {
        {TEXT("Deathblow"),     10},
        {TEXT("Hit"),            8},
        {TEXT("Dodge"),          7},
        {TEXT("Deflect"),        6},
        {TEXT("Guard"),          5},
        {TEXT("Jump"),           5},
        {TEXT("Prosthetic"),     4},
        {TEXT("Item"),           3},
        {TEXT("Attack"),         2},
        {TEXT("Grapple"),        2},
        {TEXT("CombatArt"),      2},
        {TEXT("Locomotion"),     0},
    };

    const int32* P = PriorityMap.Find(Action);
    OutPriority = P ? *P : 0;

    // 状态机优先级规则：
    //   Deathblow(10) > Hit(8) > Dodge/Deflect(7-6) > Guard/Jump(5)
    //   > Prosthetic(4) > Item(3) > Attack/Grapple(2) > Locomotion(0)
    if (CurrentPriority > OutPriority && CurrentAnimID > 0)
        return false;

    // 相同动作连段不需要 CancelWindow 检查（由 TryPlayAction 内部处理）
    if (CurrentAnimID > 0 && Action != CurrentAction)
    {
        float Crossfade;
        USKAnimInstance* AnimInst = GetAnimInstance();
        if (!AnimInst || !AnimInst->CanCancelTo(Action, Crossfade))
            return false;
    }

    return true;
}

// ── 意图处理（状态机核心）─────────────────────────────────

void USKAnimationController::ProcessIntents()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;

    // ── Idle 状态：接受所有输入 ──
    if (CurrentState == ESKCharacterState::Idle)
    {
        if (HandleDeathblow()) return;

        // 防御（按住持续）
        if (InputHandler->IsGuardHeld())
        {
            int32 Prio;
            if (CanTransition(TEXT("Guard"), Prio)) { HandleGuard(); return; }
        }

        // Dodge / Jump / Attack / Prosthetic / Item（消费缓冲）
        if (InputHandler->ConsumeBufferedInput(TEXT("Dodge")))  { HandleDodge(); return; }
        if (InputHandler->ConsumeBufferedInput(TEXT("Jump")))   { HandleJump(); return; }
        if (InputHandler->ConsumeBufferedInput(TEXT("Prosthetic"))) { HandleProsthetic(); return; }
        if (InputHandler->ConsumeBufferedInput(TEXT("Item")))   { HandleItemUse(); return; }
        if (InputHandler->ConsumeBufferedInput(TEXT("Grapple"))) { HandleGrapple(); return; }

        // 攻击
        HandleAttack();  // 内部消费缓冲
        return;
    }

    // ── Attack 状态：接受 CancelWindow 内的打断 ──
    if (CurrentState == ESKCharacterState::Attack)
    {
        // 忍杀（最高优先级）
        if (HandleDeathblow()) return;

        // 防御/闪避/跳跃 打断（需要 CancelWindow）
        if (InputHandler->IsGuardHeld())
        {
            int32 Prio;
            if (CanTransition(TEXT("Guard"), Prio)) { HandleGuard(); return; }
        }
        if (InputHandler->ConsumeBufferedInput(TEXT("Dodge")))
        {
            int32 Prio;
            if (CanTransition(TEXT("Dodge"), Prio)) { HandleDodge(); return; }
        }
        if (InputHandler->ConsumeBufferedInput(TEXT("Jump")))
        {
            int32 Prio;
            if (CanTransition(TEXT("Jump"), Prio)) { HandleJump(); return; }
        }

        // R1 连段
        HandleAttack();
        return;
    }

    // ── Guard 状态 ──
    if (CurrentState == ESKCharacterState::Guard)
    {
        if (!InputHandler->IsGuardHeld())
        {
            TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
            return;
        }

        // Guard 中闪避
        if (InputHandler->ConsumeBufferedInput(TEXT("Dodge")))
        {
            int32 Prio;
            if (CanTransition(TEXT("Dodge"), Prio)) { HandleDodge(); return; }
        }

        // Guard 动画由 OnActionMontageEnded 回调管理，不重复调用 TryPlayAction
        return;
    }

    // ── Dodge 状态：闪避中不可打断 ──
    if (CurrentState == ESKCharacterState::Dodge)
    {
        // 闪避结束由 OnActionMontageEnded 自动切回 Idle
        return;
    }

    // ── Jump / Airborne 状态 ──
    if (CurrentState == ESKCharacterState::Jump || CurrentState == ESKCharacterState::Airborne)
    {
        // 空中攻击
        if (InputHandler->ConsumeBufferedInput(TEXT("Attack")))
        {
            int32 Prio;
            if (CanTransition(TEXT("Attack"), Prio)) { HandleAttack(); return; }
        }

        // 空中闪避
        if (InputHandler->ConsumeBufferedInput(TEXT("Dodge")))
        {
            int32 Prio;
            if (CanTransition(TEXT("Dodge"), Prio)) { HandleAirDodge(); return; }
        }

        if (CurrentState == ESKCharacterState::Jump)
        {
            // Jump 动画播完后自动进入 Airborne（物理控制）
            // 由 OnActionMontageEnded 处理
        }
        return;
    }

    // ── Hit / Death 状态：不处理输入 ──
    if (CurrentState == ESKCharacterState::Hit || CurrentState == ESKCharacterState::Death)
    {
        return;
    }
}

// ── 攻击 ──────────────────────────────────────────────────

void USKAnimationController::HandleAttack()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;

    bool bHasAttackInput = InputHandler->ConsumeBufferedInput(TEXT("Attack"));
    bool bIsCharging = InputHandler->GetAttackHoldTime() > 0.3f;

    if (!bHasAttackInput && !bIsCharging)
        return;

    // 忍杀判定
    if (bDeathBlowActive)
    {
        if (TryPlayAction(TEXT("Deathblow"), ESKActionPriority::Deathblow))
        {
            bDeathBlowActive = false;
            CurrentState = ESKCharacterState::Idle;  // 忍杀由单独动画控制
            return;
        }
    }

    // 反斩判定（完美格挡后窗口内R1）
    if (bCounterWindow)
    {
        if (TryPlayAction(TEXT("Attack_Counter"), ESKActionPriority::Attack))
        {
            bCounterWindow = false;
            CurrentState = ESKCharacterState::Attack;
            return;
        }
    }

    // 空中攻击
    if (bIsInAir)
    {
        if (TryPlayAction(TEXT("Attack_Jump"), ESKActionPriority::Attack))
        {
            CurrentState = ESKCharacterState::Attack;
            return;
        }
    }

    // 蓄力
    if (bIsCharging)
        return;

    // 方向变体
    FName DirSuffix = GetMoveDirectionSuffix();
    if (DirSuffix != NAME_None)
    {
        FName DirAction = FName(*(TEXT("Attack_") + DirSuffix.ToString()));
        if (TryPlayAction(DirAction, ESKActionPriority::Attack))
        {
            CurrentState = ESKCharacterState::Attack;
            return;
        }
    }

    // 普通攻击连段
    int32 ComboIdx = AttackState.ComboIndex;
    if (!TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack))
    {
        // 首次攻击（当前无动作）
        CurrentAction = NAME_None;
        if (TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack))
        {
            CurrentState = ESKCharacterState::Attack;
            AttackState.ComboIndex = 1;
            AttackState.ComboTimeout = 1.0f;
        }
    }
    else
    {
        CurrentState = ESKCharacterState::Attack;
        AttackState.ComboIndex = FMath::Min(ComboIdx + 1, 4);
        AttackState.ComboTimeout = 1.0f;
    }
}

int32 USKAnimationController::GetComboAnimID(int32 ComboIdx) const
{
    if (CurrentAnimID > 0)
    {
        int32 JudgeId = CurrentAnimID % 1000;
        int32 NextAnimID = (CurrentAnimID / 1000) * 1000 + (JudgeId + 1);
        if (AnimLogicData && AnimLogicData->AnimPrefixMap.Contains(NextAnimID))
            return NextAnimID;
    }
    return -1;
}

FName USKAnimationController::GetMoveDirectionSuffix() const
{
    if (!InputHandler.IsValid()) return NAME_None;
    FVector2D MoveDir = InputHandler->GetMoveIntent();
    if (MoveDir.IsNearlyZero()) return NAME_None;

    float Angle = FMath::RadiansToDegrees(FMath::Atan2(MoveDir.Y, MoveDir.X));
    if (Angle > -22.5f && Angle <= 22.5f)       return TEXT("Fwd");
    if (Angle > 22.5f && Angle <= 67.5f)        return TEXT("Fwd_R");
    if (Angle > 67.5f && Angle <= 112.5f)       return TEXT("R");
    if (Angle > 112.5f && Angle <= 157.5f)      return TEXT("Bwd_R");
    if (Angle > 157.5f || Angle <= -157.5f)     return TEXT("Bwd");
    if (Angle > -157.5f && Angle <= -112.5f)    return TEXT("Bwd_L");
    if (Angle > -112.5f && Angle <= -67.5f)     return TEXT("L");
    return TEXT("Fwd_L");
}

void USKAnimationController::ResetAttackState()
{
    AttackState.ComboIndex = 0;
    AttackState.ComboTimeout = 0.f;
    AttackState.bIsCharging = false;
    AttackState.ChargeTime = 0.f;
}

void USKAnimationController::UpdateChargeState(float DeltaTime)
{
    if (!InputHandler.IsValid()) return;
    bool bCurrentlyHeld = InputHandler->IsAttackHeld();

    if (bCurrentlyHeld && !AttackState.bAttackHeldPrev && CurrentAction == TEXT("Attack"))
    {
        AttackState.bIsCharging = true;
        AttackState.ChargeTime = 0.f;
    }
    if (AttackState.bIsCharging) AttackState.ChargeTime += DeltaTime;

    if (CheckChargeRelease()) return;
    AttackState.bAttackHeldPrev = bCurrentlyHeld;
}

bool USKAnimationController::CheckChargeRelease()
{
    if (!InputHandler.IsValid()) return false;
    bool bCurrentlyHeld = InputHandler->IsAttackHeld();

    if (AttackState.bAttackHeldPrev && !bCurrentlyHeld && AttackState.bIsCharging)
    {
        AttackState.bIsCharging = false;
        AttackState.ChargeTime = 0.f;

        FName ChargeAction;
        USKAnimInstance* AnimInst = GetAnimInstance();
        if (AnimInst && AnimInst->Speed >= 525.f)
            ChargeAction = TEXT("Attack_Charged_Dash");
        else if (CurrentAction == TEXT("Dodge") || CurrentAction == TEXT("Quickstep"))
            ChargeAction = TEXT("Attack_Charged_Step");
        else
        {
            FName DirSuffix = GetMoveDirectionSuffix();
            ChargeAction = (DirSuffix == TEXT("L")) ? TEXT("Attack_Charged_L") : TEXT("Attack_Charged");
        }

        if (TryPlayAction(ChargeAction, ESKActionPriority::Attack))
        {
            ResetAttackState();
            return true;
        }
    }
    return false;
}

// ── 防御 ──────────────────────────────────────────────────

void USKAnimationController::HandleGuard()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;

    // Deflect 判定
    int32 BehaviorJudgeID = 0;
    int32 EnemyAttackStartFrame = 0;
    bool bEnemyAttacking = IsEnemyAttacking(BehaviorJudgeID, EnemyAttackStartFrame);

    if (bEnemyAttacking)
    {
        float EnemyAttackStartTime = EnemyAttackStartFrame / 30.0f;
        float TimeDiff = FMath::Abs(GetCurrentAnimTime() - EnemyAttackStartTime);

        if (TimeDiff <= (6.0f / 30.0f))
        {
            if (TryPlayAction(TEXT("Deflect"), ESKActionPriority::Deflect))
            {
                bCounterWindow = true;
                GuardState.bIsDeflecting = true;
                CurrentState = ESKCharacterState::Guard;
                return;
            }
        }
    }

    // 普通格挡
    GuardState.bIsDeflecting = false;
    TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard);
    CurrentState = ESKCharacterState::Guard;
}

void USKAnimationController::OnGuardHit(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    CurrentState = ESKCharacterState::Hit;
}

void USKAnimationController::OnGuardBreak()
{
    CurrentState = ESKCharacterState::Hit;
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("GuardBreak"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.1f);
    }
}

// ── 完美格挡判定 ────────────────────────────────────────────

ASKCharacter* USKAnimationController::GetLockOnTarget() const
{
    return nullptr;
}

bool USKAnimationController::IsEnemyAttacking(int32& OutBehaviorJudgeID, int32& OutStartFrame) const
{
    ASKCharacter* Target = GetLockOnTarget();
    if (!Target) return false;

    USKAnimInstance* TargetAnimInst = Cast<USKAnimInstance>(Target->GetMesh() ? Target->GetMesh()->GetAnimInstance() : nullptr);
    if (!TargetAnimInst) return false;

    float CurveValue = 0.f;
    if (!TargetAnimInst->GetCurveValue(TEXT("AttackHitbox"), CurveValue))
        return false;

    if ((ESKAttackHitboxType)FMath::RoundToInt(CurveValue) != ESKAttackHitboxType::None)
    {
        // 估算攻击框起始帧（从曲线有效的时间点计算）
        OutBehaviorJudgeID = 0;
        OutStartFrame = FMath::RoundToInt(TargetAnimInst->CurrentAnimTime * 30.0f);
        return true;
    }

    return false;
}

// ── 回避 ──────────────────────────────────────────────────

void USKAnimationController::HandleDodge()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    FName DirSuffix = GetMoveDirectionSuffix();
    FName DodgeAction = (DirSuffix != NAME_None) ? FName(*(TEXT("Dodge_") + DirSuffix.ToString())) : TEXT("Dodge");
    if (TryPlayAction(DodgeAction, ESKActionPriority::Dodge))
    {
        CurrentState = ESKCharacterState::Dodge;
    }
    else if (TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge))
    {
        CurrentState = ESKCharacterState::Dodge;
    }
}

// ── 跳跃 ──────────────────────────────────────────────────

void USKAnimationController::HandleJump()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    if (bWasInAir || bIsInAir) return;

    FName DirSuffix = GetMoveDirectionSuffix();
    FName JumpAction = (DirSuffix != NAME_None) ? FName(*(TEXT("Jump_") + DirSuffix.ToString())) : TEXT("Jump");
    if (TryPlayAction(JumpAction, ESKActionPriority::Jump))
    {
        CurrentState = ESKCharacterState::Jump;
        bWasInAir = true;
    }
    else if (TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump))
    {
        CurrentState = ESKCharacterState::Jump;
        bWasInAir = true;
    }
}

bool USKAnimationController::HandleAirAttack()
{
    if (!AnimLogicData || !bIsInAir) return false;
    if (TryPlayAction(TEXT("Attack_Jump"), ESKActionPriority::Attack))
    {
        CurrentState = ESKCharacterState::Attack;
        return true;
    }
    return false;
}

bool USKAnimationController::HandleAirDodge()
{
    if (!AnimLogicData || !bIsInAir) return false;
    if (TryPlayAction(TEXT("Dodge_Air"), ESKActionPriority::Dodge))
    {
        CurrentState = ESKCharacterState::Dodge;
        return true;
    }
    return false;
}

// ── 忍义手 ─────────────────────────────────────────────────

void USKAnimationController::HandleProsthetic()
{
    if (!InputHandler.IsValid()) return;
    if (!TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic)) return;
    CurrentState = ESKCharacterState::Idle;  // 义手播完后回空闲
}

// ── 道具 ──────────────────────────────────────────────────

void USKAnimationController::HandleItemUse()
{
    if (!InputHandler.IsValid()) return;
    if (!TryPlayAction(TEXT("Item"), ESKActionPriority::ItemUse)) return;
    CurrentState = ESKCharacterState::Idle;
}

// ── 钩绳 ──────────────────────────────────────────────────

void USKAnimationController::HandleGrapple()
{
    if (!InputHandler.IsValid()) return;
    if (!TryPlayAction(TEXT("Grapple"), ESKActionPriority::Attack)) return;
    CurrentState = ESKCharacterState::Idle;
}

// ── 战技 ──────────────────────────────────────────────────

void USKAnimationController::HandleCombatArt()
{
    if (!TryPlayAction(TEXT("CombatArt"), ESKActionPriority::Attack)) return;
    CurrentState = ESKCharacterState::Attack;
}

// ── 忍杀 ──────────────────────────────────────────────────

bool USKAnimationController::HandleDeathblow()
{
    if (!bDeathBlowActive || !AnimLogicData || !InputHandler.IsValid()) return false;
    if (!InputHandler->ConsumeBufferedInput(TEXT("Attack"))) return false;

    if (TryPlayAction(TEXT("Deathblow"), ESKActionPriority::Deathblow))
    {
        bDeathBlowActive = false;
        CurrentState = ESKCharacterState::Idle;
        return true;
    }
    return false;
}

// ── 受击 / 死亡 ───────────────────────────────────────────

void USKAnimationController::OnHitReceived(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    CurrentState = ESKCharacterState::Hit;
    CurrentAction = TEXT("Hit");
}

void USKAnimationController::OnDeath(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentState = ESKCharacterState::Death;
    CurrentAction = TEXT("Death");
}

void USKAnimationController::OnResurrection()
{
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Resurrection"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.2f);
    }
    CurrentState = ESKCharacterState::Idle;
}

// ── Locomotion — 只更新 AnimInstance 参数，不播放 Montage ──

void USKAnimationController::ProcessLocomotion()
{
    if (!Mesh.IsValid()) return;
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) return;

    // 非 Idle 状态时 Locomotion 被战斗/动作动画覆盖，不更新 Locomotion 参数
    if (CurrentState != ESKCharacterState::Idle)
        return;

    // Speed 已在 USKAnimInstance::NativeUpdateAnimation 中计算
    // 此处更新 MovementTier（基于速度）和 Direction（8方向）
    ESKMovementTier Tier = ESKMovementTier::Idle;
    float Spd = AnimInst->Speed;
    if (Spd >= 550.f)
        Tier = ESKMovementTier::Sprint;
    else if (Spd >= 430.f)
        Tier = ESKMovementTier::Run;
    else if (Spd >= 200.f)
        Tier = ESKMovementTier::Jog;
    else if (Spd >= 10.f)
        Tier = ESKMovementTier::Walk;

    AnimInst->MovementTier = Tier;

    // 8方向计算 — Angle 已经由 NativeUpdateAnimation 算出
    float Ang = AnimInst->Angle;
    if (Ang > -22.5f && Ang <= 22.5f)
        AnimInst->Direction = ESKLocomotionDirection::Fwd;
    else if (Ang > 22.5f && Ang <= 67.5f)
        AnimInst->Direction = ESKLocomotionDirection::Fwd_R;
    else if (Ang > 67.5f && Ang <= 112.5f)
        AnimInst->Direction = ESKLocomotionDirection::R;
    else if (Ang > 112.5f && Ang <= 157.5f)
        AnimInst->Direction = ESKLocomotionDirection::Bwd_R;
    else if (Ang > 157.5f || Ang <= -157.5f)
        AnimInst->Direction = ESKLocomotionDirection::Bwd;
    else if (Ang > -157.5f && Ang <= -112.5f)
        AnimInst->Direction = ESKLocomotionDirection::Bwd_L;
    else if (Ang > -112.5f && Ang <= -67.5f)
        AnimInst->Direction = ESKLocomotionDirection::L;
    else
        AnimInst->Direction = ESKLocomotionDirection::Fwd_L;
}

// ── Montage 结束统一回调 ──────────────────────────────────

void USKAnimationController::OnActionMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    UE_LOG(LogTemp, Log, TEXT("OnActionMontageEnded: State=%d Action=%s AnimID=%d bInterrupted=%d"),
        (int32)CurrentState, *CurrentAction.ToString(), CurrentAnimID, bInterrupted);

    // 根据当前状态决定迁移
    switch (CurrentState)
    {
    case ESKCharacterState::Attack:
        // 攻击动画播完 → 回 Idle
        CurrentState = ESKCharacterState::Idle;
        break;

    case ESKCharacterState::Guard:
        // Guard 动画结束，不改变状态（由下一帧 ProcessIntents 检测 !IsGuardHeld 回退）
        // 不清空 CurrentAction/CurrentPriority，保持 Guard 状态
        CurrentAnimID = 0;
        return;  // 不执行后面的清理

    case ESKCharacterState::Dodge:
        // 闪避结束 → 回 Idle
        CurrentState = ESKCharacterState::Idle;
        break;

    case ESKCharacterState::Jump:
        // Jump 动画结束 → 进入 Airborne（物理控制上升/下落）
        CurrentState = ESKCharacterState::Airborne;
        break;

    case ESKCharacterState::Hit:
        // 受击结束 → 回 Idle
        CurrentState = ESKCharacterState::Idle;
        break;

    case ESKCharacterState::Death:
        // 死亡不退出
        break;

    default:
        CurrentState = ESKCharacterState::Idle;
        break;
    }

    CurrentPriority = ESKActionPriority::Locomotion;
    CurrentAnimID = 0;
    CurrentAction = NAME_None;
}

// ── 动画播放 ──────────────────────────────────────────────

int32 USKAnimationController::ResolveAnimID(FName Action)
{
    if (!AnimLogicData) return -1;

    if (CurrentAnimID > 0 && Action == CurrentAction)
    {
        int32 NextAnimID = DeriveNextAnim(CurrentAnimID, Action);
        if (NextAnimID > 0)
            return NextAnimID;
    }

    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
    if (!List || List->IDs.Num() == 0) return -1;

    // Attack 类别：跳过 200000-200999 过渡段，从 201000 开始取
    if (Action == TEXT("Attack"))
    {
        for (int32 Id : List->IDs)
        {
            if (Id >= 201000)
                return Id;
        }
    }

    return List->IDs[0];
}

int32 USKAnimationController::ResolveAnimID(FName Action, int32 FromAnimID)
{
    if (!AnimLogicData) return -1;

    if (FromAnimID > 0 && Action == CurrentAction)
    {
        int32 NextAnimID = DeriveNextAnim(FromAnimID, Action);
        if (NextAnimID > 0)
            return NextAnimID;
    }

    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::DeriveNextAnim(int32 InCurrentAnimID, FName Action) const
{
    if (!AnimLogicData || InCurrentAnimID <= 0) return -1;

    int32 JudgeId = InCurrentAnimID % 1000;
    int32 NextAnimID = (InCurrentAnimID / 1000) * 1000 + (JudgeId + 1);

    if (AnimLogicData->AnimPrefixMap.Contains(NextAnimID))
        return NextAnimID;

    return -1;
}

bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
    if (!AnimLogicData) return false;

    // 优先级检查：高优先级不能被打断
    if (CurrentPriority > Priority && CurrentAnimID > 0)
        return false;

    // CancelWindow 检查（同动作连段不需要）
    if (CurrentAnimID > 0 && Action != CurrentAction)
    {
        float Crossfade;
        USKAnimInstance* AnimInst = GetAnimInstance();
        if (!AnimInst || !AnimInst->CanCancelTo(Action, Crossfade))
            return false;
    }

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0) return false;

    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
    return true;
}

void USKAnimationController::PlayMontageByID(int32 AnimID, float Crossfade)
{
    if (!Mesh.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: Mesh invalid")); return; }
    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) { UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: AnimInstance null")); return; }

    EnsureMontageLoaded(AnimID);
    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found)
    {
        UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: AnimID=%d not in cache"), AnimID);
        return;
    }

    UAnimSequence* Seq = *Found;
    UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, Crossfade, 0.0f);
    if (DynMontage)
    {
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &USKAnimationController::OnActionMontageEnded);
        AnimInst->Montage_SetEndDelegate(EndDelegate);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("PlayMontageByID: AnimID=%d -> FAILED"), AnimID);
    }
}

void USKAnimationController::EnsureMontageLoaded(int32 AnimID)
{
    if (!AnimLogicData) return;
    if (MontageCache.Contains(AnimID)) return;

    FString AssetPath = AnimLogicData->BuildAnimAssetPath(AnimID);
    UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (Seq) MontageCache.Add(AnimID, Seq);
}

// ── 攻击碰撞箱 ─────────────────────────────────────────────

void USKAnimationController::UpdateAttackHitbox()
{
    ASKWeapon* Weapon = GetWeapon();
    if (!Weapon) { if (Weapon) Weapon->DeactivateHitbox(); return; }

    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst)
    {
        Weapon->DeactivateHitbox();
        return;
    }

    // 从 "AttackHitbox" 曲线读取当前帧是否有活跃攻击框
    float CurveValue = 0.f;
    if (AnimInst->GetCurveValue(TEXT("AttackHitbox"), CurveValue))
    {
        ESKAttackHitboxType HitType = (ESKAttackHitboxType)FMath::RoundToInt(CurveValue);
        if (HitType != ESKAttackHitboxType::None)
        {
            Weapon->ActivateHitbox();
            return;
        }
    }

    Weapon->DeactivateHitbox();
    Weapon->ClearHitActors();
}

ASKWeapon* USKAnimationController::GetWeapon() const
{
    if (!OwnerCharacter.IsValid()) return nullptr;
    USKWeaponComponent* WComp = OwnerCharacter->GetWeaponComponent();
    return WComp ? WComp->CurrentWeapon : nullptr;
}

USKAnimInstance* USKAnimationController::GetAnimInstance() const
{
    if (!Mesh.IsValid()) return nullptr;
    return Cast<USKAnimInstance>(Mesh->GetAnimInstance());
}
