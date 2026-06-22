// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SKAnimationController.h"   // ensure recompile for CurrentLocoState init fix
#include "SekiroAnimLogicData.h"
#include "SekiroCombatData.h"
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
int32 USKAnimationController::GetCurrentPriority() const { return CurrentPriority; }

// ── 生命周期 ──────────────────────────────────────────────

void USKAnimationController::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<ASKCharacter>(GetOwner());
    if (!OwnerCharacter.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: Owner is not ASKCharacter! Owner=%s Class=%s"),
            *GetNameSafe(GetOwner()),
            *GetNameSafe(GetOwner()),
            *GetNameSafe(GetOwner() ? GetOwner()->GetClass() : nullptr));
        return;
    }

    InputHandler = OwnerCharacter->GetInputHandler();
    Mesh = OwnerCharacter->GetMesh();

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Owner=%s Class=%s InputHandler=%s"),
        *GetNameSafe(OwnerCharacter.Get()),
        *GetNameSafe(OwnerCharacter.Get()),
        *GetNameSafe(OwnerCharacter->GetClass()),
        *GetNameSafe(InputHandler.Get()));

    if (!AnimLogicData)
    {
        AnimLogicData = LoadObject<USKAnimationLogicData>(nullptr,
            TEXT("/Game/Characters/Sekiro/DA_Sekiro_AnimLogic.DA_Sekiro_AnimLogic"));
        if (AnimLogicData)
        {
            UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: AnimLogicData auto-loaded"),
                *GetNameSafe(OwnerCharacter.Get()));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: AnimLogicData failed to load"),
                *GetNameSafe(OwnerCharacter.Get()));
        }
    }

    if (!CombatData)
    {
        CombatData = LoadObject<USKCombatData>(nullptr,
            TEXT("/Game/Characters/Sekiro/DA_Sekiro_Combat.DA_Sekiro_Combat"));
        if (CombatData)
        {
            UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: CombatData auto-loaded"),
                *GetNameSafe(OwnerCharacter.Get()));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Initialized"),
        *GetNameSafe(OwnerCharacter.Get()));
}

void USKAnimationController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateFrameState();

    // Tick 诊断：每 60 帧输出一次当前状态
    static int32 TickCounter = 0;
    if (++TickCounter % 60 == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("AnimTick[%s]: Action=%s AnimID=%d Priority=%d Mesh=%s ActiveMontage=%s"),
            *GetNameSafe(OwnerCharacter.Get()),
            *CurrentAction.ToString(),
            CurrentAnimID,
            CurrentPriority,
            *GetNameSafe(Mesh.Get()),
            *GetNameSafe(Mesh.IsValid() && Mesh->GetAnimInstance() ? Mesh->GetAnimInstance()->GetCurrentActiveMontage() : nullptr));
    }

    ApplyFrameFlags();
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

void USKAnimationController::ApplyFrameFlags()
{
    if (!AnimLogicData || CurrentAnimID <= 0) return;
    if (!Mesh.IsValid()) return;

    int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);
    FSKFrameFlags Flags;
    if (!AnimLogicData->GetFrameFlags(CurrentAnimID, Frame, Flags)) return;

    USKAnimInstance* AnimInst = Cast<USKAnimInstance>(Mesh->GetAnimInstance());
    if (!AnimInst) return;

    AnimInst->bCanDeflect = Flags.bEnableParry && !Flags.bDisableParry;
    AnimInst->bDisableTurning = Flags.bDisableTurning;
    AnimInst->bDisableMovement = Flags.bDisableMovement || Flags.bLimitMoveSpeedWalk || Flags.bLimitMoveSpeedDash;
}

// ── 意图处理 ──────────────────────────────────────────────

void USKAnimationController::ProcessIntents()
{
    if (!InputHandler.IsValid())
    {
        static bool bWarned = false;
        if (!bWarned) { UE_LOG(LogTemp, Warning, TEXT("ProcessIntents: InputHandler invalid, Owner=%s"), *GetNameSafe(OwnerCharacter.Get())); bWarned = true; }
        return;
    }
    if (!AnimLogicData)
    {
        static bool bWarned = false;
        if (!bWarned) { UE_LOG(LogTemp, Warning, TEXT("ProcessIntents: AnimLogicData null, Owner=%s"), *GetNameSafe(OwnerCharacter.Get())); bWarned = true; }
        return;
    }

    // ── 构建意图列表，按 Priority 降序 ──
    struct FIntentEntry
    {
        FName DebugName;
        int32 Priority;
        TFunction<bool()> Handler;
    };

    TArray<FIntentEntry> Intents;
    Intents.Reserve(10);

    Intents.Add({TEXT("Deathblow"),  ESKActionPriority::Deathblow,  [this]() -> bool { return HandleDeathblow(); }});
    Intents.Add({TEXT("Dodge"),      ESKActionPriority::Dodge,      [this]() -> bool { if (InputHandler->ConsumeBufferedInput(TEXT("Dodge"))) { HandleDodge(); return true; } return false; }});
    Intents.Add({TEXT("Jump"),       ESKActionPriority::Jump,       [this]() -> bool { if (InputHandler->ConsumeBufferedInput(TEXT("Jump"))) { HandleJump(); return true; } return false; }});
    Intents.Add({TEXT("Guard"),      ESKActionPriority::Guard,      [this]() -> bool { if (InputHandler->IsGuardHeld()) { HandleGuard(); return true; } return false; }});
    Intents.Add({TEXT("Prosthetic"), ESKActionPriority::Prosthetic, [this]() -> bool { if (InputHandler->ConsumeBufferedInput(TEXT("Prosthetic"))) { HandleProsthetic(); return true; } return false; }});
    Intents.Add({TEXT("ItemUse"),    ESKActionPriority::ItemUse,    [this]() -> bool { if (InputHandler->ConsumeBufferedInput(TEXT("Item"))) { HandleItemUse(); return true; } return false; }});
    Intents.Add({TEXT("Grapple"),    ESKActionPriority::Attack,     [this]() -> bool { if (InputHandler->ConsumeBufferedInput(TEXT("Grapple"))) { HandleGrapple(); return true; } return false; }});
    Intents.Add({TEXT("Attack"),     ESKActionPriority::Attack,     [this]() -> bool { HandleAttack(); return true; }});

    // Priority 降序排序（已按声明顺序保证，此处显式排序确保）
    Intents.Sort([](const FIntentEntry& A, const FIntentEntry& B) { return A.Priority > B.Priority; });

    // ── 降序遍历：高优先级意图优先被消费 ──
    for (const FIntentEntry& Intent : Intents)
    {
        // 低优先级不能打断当前动作
        if (Intent.Priority <= CurrentPriority && CurrentAnimID > 0)
            continue;

        if (Intent.Handler())
            return;  // 一次 Tick 只消费一个最高优先级意图
    }
}

// ── 上下文标志更新 ──────────────────────────────────────────

void USKAnimationController::UpdateContextFlags()
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst)
    {
        bIsInAir = false;
        return;
    }

    // 从 AnimInstance 同步空中状态
    bIsInAir = AnimInst->bIsInAir;

    // bCounterWindow 超时清除：如果当前动画不是 Deflect/Attack_Counter，清除标志
    if (bCounterWindow && CurrentAction != TEXT("Deflect") && CurrentAction != TEXT("Attack_Counter"))
    {
        bCounterWindow = false;
    }

    // bDeathBlowActive 由外部（架势槽满事件）设置，此处不自动清除
}

bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
    if (!AnimLogicData) return false;
    if (CurrentPriority > Priority && CurrentAnimID > 0) return false;

    if (CurrentAnimID > 0)
    {
        float Crossfade;
        if (!AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, Action, Crossfade)) return false;
    }

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0) return false;

    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
    return true;
}

// ── 攻击 ──────────────────────────────────────────────────

void USKAnimationController::HandleAttack()
{
    if (!InputHandler.IsValid()) return;

    // ── 从缓冲队列消费 Attack 输入 ──
    // 蓄力状态不需要消费缓冲（蓄力由持续按住触发，不依赖按下事件）
    bool bHasAttackInput = InputHandler->ConsumeBufferedInput(TEXT("Attack"));
    bool bIsCharging = InputHandler->GetAttackHoldTime() > 0.3f;

    if (!bHasAttackInput && !bIsCharging)
        return;

    // 上下文优先级: 忍杀 > 反斩 > 空中攻击 > 蓄力 > 方向变体 > 普通连段

    // 1. 忍杀判定（架势槽满+R1）
    if (bDeathBlowActive)
    {
        if (TryPlayAction(TEXT("Deathblow"), ESKActionPriority::Deathblow))
        {
            bDeathBlowActive = false;
            return;
        }
    }

    // 2. 反斩判定（完美格挡后窗口内R1）
    if (bCounterWindow)
    {
        if (TryPlayAction(TEXT("Attack_Counter"), ESKActionPriority::Attack))
        {
            bCounterWindow = false;
            return;
        }
    }

    // 3. 空中攻击
    if (bIsInAir)
    {
        if (TryPlayAction(TEXT("Attack_Jump"), ESKActionPriority::Attack))
            return;
    }

    // 4. 蓄力判定 — 蓄力由 CheckChargeRelease 触发，此处不处理
    if (bIsCharging)
    {
        return;  // 蓄力由 CheckChargeRelease 触发
    }

    // 5. 方向变体
    int32 ComboIdx = AttackState.ComboIndex;
    FName DirSuffix = GetMoveDirectionSuffix();
    if (DirSuffix != NAME_None)
    {
        FName DirAction = FName(*(TEXT("Attack_") + DirSuffix.ToString()));
        if (TryPlayAction(DirAction, ESKActionPriority::Attack))
        {
            AttackState.ComboIndex = FMath::Min(ComboIdx + 1, 4);
            AttackState.ComboTimeout = 1.0f;
            return;
        }
    }

    // 6. 普通攻击连段
    {
        int32 AnimID = GetComboAnimID(ComboIdx);
        if (AnimID <= 0) { ComboIdx = 0; AnimID = GetComboAnimID(0); }
        if (AnimID <= 0) return;

        if (TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack))
        {
            AttackState.ComboIndex = FMath::Min(ComboIdx + 1, 4);
            AttackState.ComboTimeout = 1.0f;
        }
    }
}

int32 USKAnimationController::GetComboAnimID(int32 ComboIdx) const
{
    if (CombatData && CurrentAnimID > 0)
    {
        // 从 ComboChain 查当前动画在 R1 下的派生
        int32 Derived = CombatData->GetDerivedAnim(CurrentAnimID, TEXT("R1"));
        if (Derived > 0)
            return Derived;
    }
    // 回退到原来的逻辑
    if (!AnimLogicData) return -1;
    FString Category = FString::Printf(TEXT("Attack_R1_Combo%02d"), ComboIdx);
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Category);
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
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

    // 1. 检查是否有锁定目标且敌人在攻击
    int32 BehaviorJudgeID = 0;
    int32 EnemyAttackStartFrame = 0;
    bool bEnemyAttacking = IsEnemyAttacking(BehaviorJudgeID, EnemyAttackStartFrame);

    if (bEnemyAttacking)
    {
        // 2. 计算玩家当前帧与敌人攻击框起始帧的时间差
        float EnemyAttackStartTime = EnemyAttackStartFrame / 30.0f;
        float PlayerAnimTime = GetCurrentAnimTime();
        float TimeDiff = FMath::Abs(PlayerAnimTime - EnemyAttackStartTime);

        // <=6帧 -> Deflect（完美格挡）
        if (TimeDiff <= (6.0f / 30.0f))
        {
            if (TryPlayAction(TEXT("Deflect"), ESKActionPriority::Deflect))
            {
                bCounterWindow = true;            // 开启反斩窗口
                GuardState.Phase = ESKGuardPhase::Idle;
                GuardState.bIsDeflecting = true;
                return;
            }
        }
    }

    // 3. 普通格挡（走 TryPlayAction 统一走 CanCancelTo 窗口判定）
    GuardState.bIsDeflecting = false;
    TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard);
    GuardState.Phase = ESKGuardPhase::Idle;
}

void USKAnimationController::OnGuardHit(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    GuardState.Phase = ESKGuardPhase::HitReaction;
}

void USKAnimationController::OnGuardBreak()
{
    GuardState.Phase = ESKGuardPhase::Broken;
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("GuardBreak"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.1f);
    }
}

// ── 完美格挡判定 ────────────────────────────────────────────

ASKCharacter* USKAnimationController::GetLockOnTarget() const
{
    // TODO: Lock-on system not yet implemented
    return nullptr;
}

bool USKAnimationController::IsEnemyAttacking(int32& OutBehaviorJudgeID, int32& OutStartFrame) const
{
    ASKCharacter* Target = GetLockOnTarget();
    if (!Target) return false;

    USKAnimationController* TargetController = Target->GetAnimController();
    if (!TargetController || !AnimLogicData) return false;

    int32 EnemyAnimID = TargetController->GetCurrentAnimID();
    float EnemyAnimTime = TargetController->GetCurrentAnimTime();
    int32 EnemyFrame = FMath::RoundToInt(EnemyAnimTime * 30.0f);

    TArray<FSKAttackHitboxConfig> ActiveHitboxes;
    AnimLogicData->GetActiveHitboxesAtFrame(EnemyAnimID, EnemyFrame, ActiveHitboxes);

    if (ActiveHitboxes.Num() > 0)
    {
        OutBehaviorJudgeID = ActiveHitboxes[0].BehaviorJudgeID;
        OutStartFrame = ActiveHitboxes[0].StartFrame;
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
    if (!TryPlayAction(DodgeAction, ESKActionPriority::Dodge))
        TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge);
}

// ── 跳跃 ──────────────────────────────────────────────────
void USKAnimationController::HandleJump()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    if (bWasInAir) return;
    FName DirSuffix = GetMoveDirectionSuffix();
    FName JumpAction = (DirSuffix != NAME_None) ? FName(*(TEXT("Jump_") + DirSuffix.ToString())) : TEXT("Jump");
    if (!TryPlayAction(JumpAction, ESKActionPriority::Jump))
        TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump);
    bWasInAir = true;
}

// ── 忍义�?────────────────────────────────────────────────

void USKAnimationController::HandleProsthetic()
{
    if (!InputHandler.IsValid()) return;
    TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic);
}

// ── 道具 ──────────────────────────────────────────────────

void USKAnimationController::HandleItemUse()
{
    if (!InputHandler.IsValid()) return;
    TryPlayAction(TEXT("Item"), ESKActionPriority::ItemUse);
}

// ── 钩绳 ──────────────────────────────────────────────────

void USKAnimationController::HandleGrapple()
{
    if (!InputHandler.IsValid()) return;
    TryPlayAction(TEXT("Grapple"), ESKActionPriority::Attack);  // 与 Attack 同级优先级
}

// ── 战技 ──────────────────────────────────────────────────

void USKAnimationController::HandleCombatArt()
{
    TryPlayAction(TEXT("CombatArt"), ESKActionPriority::Attack);
}

// ── 忍杀 ──────────────────────────────────────────────────

bool USKAnimationController::HandleDeathblow()
{
    // 必须满足：忍杀激活 + R1 输入 + 有动画数据
    if (!bDeathBlowActive || !AnimLogicData || !InputHandler.IsValid()) return false;
    if (!InputHandler->ConsumeBufferedInput(TEXT("Attack"))) return false;

    // 断线攻击（Deathblow）由 HP/Break 系统在满架势+处决线时设置 bDeathBlowActive
    // 这里的 TryPlayAction 保证通过 CanCancelTo 确认窗口
    if (TryPlayAction(TEXT("Deathblow"), ESKActionPriority::Deathblow))
    {
        bDeathBlowActive = false;
        return true;
    }
    return false;
}

// ── 受击 / 死亡 ───────────────────────────────────────────

void USKAnimationController::OnHitReceived(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    CurrentAction = TEXT("Hit");
}

void USKAnimationController::OnDeath(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Death");
}

void USKAnimationController::OnResurrection()
{
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Resurrection"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.2f);
    }
}

// ── 移动 — 含 Locomotion 动画播放 ──────────────────────────

void USKAnimationController::ProcessLocomotion()
{
    if (!AnimLogicData)
    {
        static bool bWarned = false;
        if (!bWarned) { UE_LOG(LogTemp, Warning, TEXT("ProcessLocomotion: AnimLogicData null")); bWarned = true; }
        return;
    }
    if (!Mesh.IsValid())
    {
        static bool bWarned = false;
        if (!bWarned) { UE_LOG(LogTemp, Warning, TEXT("ProcessLocomotion: Mesh invalid")); bWarned = true; }
        return;
    }
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst)
    {
        static bool bWarned = false;
        if (!bWarned) { UE_LOG(LogTemp, Warning, TEXT("ProcessLocomotion: AnimInstance null, Mesh=%s"), *GetNameSafe(Mesh.Get())); bWarned = true; }
        return;
    }

    float MySpeed = AnimInst->Speed;
    float MyAngle = AnimInst->Angle;

    // 诊断：每 60 帧输出一次
    static int32 LocoCounter = 0;
    if (++LocoCounter % 60 == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("LocoTick: Speed=%.1f Angle=%.1f Tier=%d Dir=%d LocoStateTier=%d AnimLogicData=%s"),
            MySpeed, MyAngle,
            (int32)CurrentLocoState.Tier, (int32)CurrentLocoState.Direction,
            (int32)AnimInst->MovementTier,
            AnimLogicData ? TEXT("ok") : TEXT("null"));
        if (AnimLogicData)
        {
            const FSKAnimIDList* IdleList = AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion_Idle"));
            UE_LOG(LogTemp, Log, TEXT("  CategoryAnimMap 'Locomotion_Idle': %s"),
                (IdleList && IdleList->IDs.Num() > 0) ? *FString::Printf(TEXT("found %d ids"), IdleList->IDs.Num()) : TEXT("NOT FOUND"));
        }
    }

    float Speed = AnimInst->Speed;
    float Angle = AnimInst->Angle;

    // 更新冷却计时
    TurnCooldown = FMath::Max(0.f, TurnCooldown - GetWorld()->GetDeltaSeconds());

    // ── 原地转身判定 ──
    // 仅当速度很低（原地）且角度变化超过阈值时触发
    if (CurrentPriority <= ESKActionPriority::Locomotion && Speed < 50.f && TurnCooldown <= 0.f)
    {
        float AngleDelta = FMath::FindDeltaAngleDegrees(LastAngle, Angle);
        if (FMath::Abs(AngleDelta) > 90.f)
        {
            int32 TurnID = GetTurnAnimID(AngleDelta);
            if (TurnID > 0)
            {
                PlayLocomotionMontage(TurnID, false);
                TurnCooldown = 0.5f;   // 转身冷却
                LastAngle = Angle;     // 缓存角度
                return;                // 转身期间不处理移动
            }
        }
    }

    LastAngle = Angle;

    // ── 原有逻辑：Tier 升降 + Direction 切换 ──
    FSKLocomotionState NewState = EvaluateLocomotionState(Speed, Angle);

    if (CurrentPriority > ESKActionPriority::Locomotion)
    {
        CurrentLocoState = NewState;
        return;
    }

    if (CurrentLocoState.AnimID == 0)
    {
        // 初次Tick：从未播放过任何Locomotion动画 → 强制播放当前状态的动画
        int32 LocoID = ResolveLocomotionAnimID(NewState);
        if (LocoID > 0) PlayLocomotionMontage(LocoID, NewState.Tier != ESKMovementTier::Idle);
    }
    else if (NewState.Tier == ESKMovementTier::Idle && CurrentLocoState.Tier != ESKMovementTier::Idle)
    {
        int32 StopID = GetStopAnimID(CurrentLocoState);
        if (StopID > 0) PlayLocomotionMontage(StopID, false);
    }
    else if (NewState.Tier != CurrentLocoState.Tier || NewState.Direction != CurrentLocoState.Direction)
    {
        if (CurrentLocoState.Tier != ESKMovementTier::Idle && NewState.Tier != ESKMovementTier::Idle)
        {
            int32 TransID = GetTransitionAnimID(CurrentLocoState, NewState);
            if (TransID > 0) { PlayLocomotionMontage(TransID, false); CurrentLocoState = NewState; return; }
        }
        int32 LocoID = ResolveLocomotionAnimID(NewState);
        if (LocoID > 0) PlayLocomotionMontage(LocoID, NewState.Tier != ESKMovementTier::Idle);
    }
    CurrentLocoState = NewState;
}

FSKLocomotionState USKAnimationController::EvaluateLocomotionState(float Speed, float Angle) const
{
    FSKLocomotionState State;
    if (Speed < 10.f)           { State.Tier = ESKMovementTier::Idle; State.bIsMoving = false; }
    else if (Speed < 200.f)     { State.Tier = ESKMovementTier::Walk; State.bIsMoving = true; }
    else if (Speed < 430.f)     { State.Tier = ESKMovementTier::Jog; State.bIsMoving = true; }
    else if (Speed < 550.f)     { State.Tier = ESKMovementTier::Run; State.bIsMoving = true; }
    else                         { State.Tier = ESKMovementTier::Sprint; State.bIsMoving = true; }

    if (Angle > -22.5f && Angle <= 22.5f)           State.Direction = ESKLocomotionDirection::Fwd;
    else if (Angle > 22.5f && Angle <= 67.5f)       State.Direction = ESKLocomotionDirection::Fwd_R;
    else if (Angle > 67.5f && Angle <= 112.5f)      State.Direction = ESKLocomotionDirection::R;
    else if (Angle > 112.5f && Angle <= 157.5f)     State.Direction = ESKLocomotionDirection::Bwd_R;
    else if (Angle > 157.5f || Angle <= -157.5f)    State.Direction = ESKLocomotionDirection::Bwd;
    else if (Angle > -157.5f && Angle <= -112.5f)   State.Direction = ESKLocomotionDirection::Bwd_L;
    else if (Angle > -112.5f && Angle <= -67.5f)    State.Direction = ESKLocomotionDirection::L;
    else                                             State.Direction = ESKLocomotionDirection::Fwd_L;
    return State;
}

int32 USKAnimationController::ResolveLocomotionAnimID(const FSKLocomotionState& State) const
{
    if (!AnimLogicData) return -1;
    FString Category;
    switch (State.Tier)
    {
    case ESKMovementTier::Idle:   Category = TEXT("Locomotion_Idle"); break;
    case ESKMovementTier::Walk:   Category = TEXT("Locomotion_Walk"); break;
    case ESKMovementTier::Run:    Category = TEXT("Locomotion_Run"); break;
    case ESKMovementTier::Jog:    Category = TEXT("Locomotion_Jog"); break;
    case ESKMovementTier::Sprint: Category = TEXT("Locomotion_Sprint"); break;
    default: return -1;
    }
    FString DirStr;
    switch (State.Direction)
    {
    case ESKLocomotionDirection::Fwd: DirStr = TEXT("_Fwd"); break;
    case ESKLocomotionDirection::Bwd: DirStr = TEXT("_Bwd"); break;
    case ESKLocomotionDirection::L:   DirStr = TEXT("_L"); break;
    case ESKLocomotionDirection::R:   DirStr = TEXT("_R"); break;
    default: DirStr = TEXT("_Fwd"); break;
    }
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Category + DirStr);
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    List = AnimLogicData->CategoryAnimMap.Find(Category);
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To) const
{
    FString Category = FString::Printf(TEXT("Locomotion_Transition_%d_to_%d"), (int32)From.Tier, (int32)To.Tier);
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetStopAnimID(const FSKLocomotionState& State) const
{
    FString Category = FString::Printf(TEXT("Locomotion_Stop_%d"), (int32)State.Tier);
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetTurnAnimID(float AngleDelta) const
{
    if (FMath::Abs(AngleDelta) < 15.f) return -1;
    FString Category = (AngleDelta > 0) ? TEXT("Locomotion_Turn_R") : TEXT("Locomotion_Turn_L");
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

void USKAnimationController::PlayLocomotionMontage(int32 AnimID, bool bLooping)
{
    if (AnimID <= 0) return;
    EnsureMontageLoaded(AnimID);

    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found || !Mesh.IsValid()) return;

    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;

    UAnimSequence* Seq = *Found;
    UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, 0.15f, 0.0f);
    if (DynMontage)
    {
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &USKAnimationController::OnLocoTransitionEnded);
        AnimInst->Montage_SetEndDelegate(EndDelegate);
    }
    CurrentAnimID = AnimID;
    CurrentPriority = ESKActionPriority::Locomotion;
}

void USKAnimationController::OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (!bInterrupted) ProcessLocomotion();
}

// ── 动画播放 ──────────────────────────────────────────────

int32 USKAnimationController::ResolveAnimID(FName Action)
{
    // 先从 ComboChain 查派生动画
    if (CurrentAnimID > 0 && CombatData)
    {
        int32 ComboNext = CombatData->GetDerivedAnim(CurrentAnimID, Action);
        if (ComboNext > 0)
            return ComboNext;
    }
    // 回退到 CategoryAnimMap
    if (!AnimLogicData) return -1;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

// 新增重载：从指定AnimID派生
int32 USKAnimationController::ResolveAnimID(FName Action, int32 FromAnimID)
{
    if (FromAnimID > 0 && CombatData)
    {
        int32 ComboNext = CombatData->GetDerivedAnim(FromAnimID, Action);
        if (ComboNext > 0)
            return ComboNext;
    }
    return ResolveAnimID(Action);
}

int32 USKAnimationController::GetComboNextAnim(int32 InCurrentAnimID, FName Action) const
{
    if (!CombatData) return -1;
    return CombatData->GetDerivedAnim(InCurrentAnimID, Action);
}

void USKAnimationController::PlayMontageByID(int32 AnimID, float Crossfade)
{
    EnsureMontageLoaded(AnimID);
    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found || !Mesh.IsValid()) return;

    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;

    UAnimSequence* Seq = *Found;
    AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, Crossfade, 0.0f);
}

void USKAnimationController::EnsureMontageLoaded(int32 AnimID)
{
    if (!AnimLogicData) return;
    if (MontageCache.Contains(AnimID)) return;

    FString AssetPath = AnimLogicData->BuildAnimAssetPath(AnimID);

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Load AnimID=%d (%s)"),
        *GetOwner()->GetName(), AnimID, *AssetPath);

    UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (Seq) MontageCache.Add(AnimID, Seq);
}

// ── 攻击碰撞�?────────────────────────────────────────────

void USKAnimationController::UpdateAttackHitbox()
{
    ASKWeapon* Weapon = GetWeapon();
    if (!AnimLogicData || CurrentAnimID <= 0)
    {
        if (Weapon) Weapon->DeactivateHitbox();
        return;
    }

    int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);
    if (!Weapon) return;

    bool bHasActiveHitbox = false;
    const FSKAttackHitboxList* List = AnimLogicData->AttackHitboxConfigs.Find(CurrentAnimID);
    if (List)
    {
        for (const FSKAttackHitboxConfig& Cfg : List->Hitboxes)
        {
            if (Frame >= Cfg.StartFrame && Frame <= Cfg.EndFrame) { bHasActiveHitbox = true; break; }
        }
    }

    if (bHasActiveHitbox) Weapon->ActivateHitbox();
    else { Weapon->DeactivateHitbox(); Weapon->ClearHitActors(); }
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
