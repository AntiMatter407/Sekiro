#include "Character/SKSurvivalComponent.h"
#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "Character/SKCharacter.h"
#include "Combat/SKCombatComponent.h"
#include "Engine/World.h"

/** 构造阶段只开启游戏线程 Tick，不创建数值默认值、不加载资源。 */
USKSurvivalComponent::USKSurvivalComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

/** 游戏线程返回可编辑 UnLua 模块名，不加载资源或持有脚本对象。 */
FString USKSurvivalComponent::GetModuleName_Implementation() const
{
    return LuaModuleName;
}

/**
 * 游戏线程先验证配置字典中的全部必需标签，再注册唯一策略；支持 ASC 已就绪后补绑定。
 * @param AttributeSystem 同一 Actor 的非空 ASC，不转移拥有权。
 * @return 注册成功或同一绑定幂等返回 true；标签缺失、冲突、跨角色、离场或重入返回 false。
 */
bool USKSurvivalComponent::BindAttributeSystem(USKAbilitySystemComponent* AttributeSystem)
{
    if (!IsInGameThread() || bEndedPlay || bTransitionBusy || !IsValid(AttributeSystem)
        || AttributeSystem->GetOwner() != GetOwner()) return false;
    if (bBound) return ASC == AttributeSystem && ASC->IsResourcePolicyBoundTo(this);
    if (!ResolveRequiredGameplayTags()) return false;
    ASC = AttributeSystem;
    bBound = true;
    if (!ASC->RegisterResourcePolicy(this, this))
    {
        bBound = false;
        ASC = nullptr;
        return false;
    }
    return true;
}

/** 游戏线程查询本组件的同步转换/事件窗口，不读取或修改属性。 */
bool USKSurvivalComponent::IsResourcePolicyBusy() const
{
    return bTransitionBusy;
}

/**
 * 游戏线程校验普通资源语义及当前标签；不写入 GAS 或补发状态事件。
 * @param Operation 生命/躯干的普通请求类型。
 * @return Applied 允许；NotReady 未绑定，PolicyRejected 表示状态或免疫门禁拒绝。
 */
ESKNumericResultCode USKSurvivalComponent::CheckResourceOperation(ESKNumericOperation Operation) const
{
    if (!IsSurvivalReady()) return ESKNumericResultCode::NotReady;
    if (bSerialExhausted || LifeState != ESKLifeState::Alive || ASC->GetAttributeSnapshot().Health <= 0.f)
        return ESKNumericResultCode::PolicyRejected;
    if (Operation == ESKNumericOperation::Damage)
        return ASC->HasMatchingGameplayTag(DamageImmuneTag) ? ESKNumericResultCode::PolicyRejected : ESKNumericResultCode::Applied;
    if (Operation == ESKNumericOperation::Healing) return ESKNumericResultCode::Applied;
    if (Operation == ESKNumericOperation::PostureDamage || Operation == ESKNumericOperation::PostureRecovery
        || Operation == ESKNumericOperation::ResetPosture)
    {
        if (bPostureBroken || (Operation == ESKNumericOperation::PostureDamage && ASC->HasMatchingGameplayTag(PostureImmuneTag)))
            return ESKNumericResultCode::PolicyRejected;
        return ESKNumericResultCode::Applied;
    }
    return ESKNumericResultCode::PolicyRejected;
}

/**
 * 游戏线程解释资源门禁拒绝，不重新执行数值或改变生命状态。
 * @param Operation 被查询的普通资源语义。
 * @return 稳定原因名；允许时 None，未知操作 UnsupportedOperation。
 */
FName USKSurvivalComponent::GetResourceRejectionReason(ESKNumericOperation Operation) const
{
    if (!IsSurvivalReady()) return TEXT("NotReady");
    if (bSerialExhausted) return TEXT("SerialExhausted");
    if (LifeState != ESKLifeState::Alive || ASC->GetAttributeSnapshot().Health <= 0.f) return TEXT("NotAlive");
    if (Operation == ESKNumericOperation::Damage)
        return ASC->HasMatchingGameplayTag(DamageImmuneTag) ? FName(TEXT("HealthImmune")) : NAME_None;
    if (Operation == ESKNumericOperation::Healing) return NAME_None;
    if (Operation == ESKNumericOperation::PostureDamage || Operation == ESKNumericOperation::PostureRecovery
        || Operation == ESKNumericOperation::ResetPosture)
    {
        if (bPostureBroken) return TEXT("PostureBroken");
        if (Operation == ESKNumericOperation::PostureDamage && ASC->HasMatchingGameplayTag(PostureImmuneTag)) return TEXT("PostureImmune");
        return NAME_None;
    }
    return TEXT("UnsupportedOperation");
}

/**
 * 游戏线程校验仅由当前转换方法打开的原生恢复窗口，不向脚本授予旁路权限。
 * @param InTransitionSerial ASC 传入的非零当前授权序号。
 * @return 当前窗口、序号及就绪状态全部满足时返回 true。
 */
bool USKSurvivalComponent::CanRestoreOwnedResources(int64 InTransitionSerial) const
{
    return IsSurvivalReady() && bInternalResourceCommit && InTransitionSerial > 0
        && InTransitionSerial == InternalTransitionSerial;
}

/**
 * 游戏线程在完整资源提交结束评估机械状态，先死亡后躯干；外部通知顺序不影响门禁。
 * @param Result 可空实际数值记录，来源仅从真实结算提取。
 * @param bInitial 初始快照只分类，不补发历史死亡。
 */
void USKSurvivalComponent::ReconcileResourceState(const FSKNumericResult* Result, bool bInitial)
{
    if (!IsInGameThread() || bEndedPlay || !bBound || !ASC || !ASC->IsAttributesReady()) return;
    TGuardValue<bool> ReconcileGuard(bTransitionBusy, true);
    const FSKAttributeSnapshot Attributes = ASC->GetAttributeSnapshot();
    if (!bReady)
    {
        bReady = true;
        LifeSerial = 1;
        SetLifeState(Attributes.Health > 0.f ? ESKLifeState::Alive : ESKLifeState::Dead);
        bReadyEventPending = true;
    }
    if (bInternalResourceCommit || bSerialExhausted) return;
    if ((LifeState == ESKLifeState::Dying || LifeState == ESKLifeState::Dead) && Attributes.Health > 0.f
        && !bBypassViolationLogged)
    {
        bBypassViolationLogged = true;
        UE_LOG(LogTemp, Warning, TEXT("Survival 检测到非存活角色的旁路正生命写入；不视为合法回生，门禁保持关闭"));
    }
    AActor* SourceActor = Result && (Result->After.Health < Result->Before.Health
        || Result->After.Posture > Result->Before.Posture) ? Result->SourceActor.Get() : nullptr;
    if (LifeState == ESKLifeState::Alive && Attributes.Health <= 0.f)
    {
        const FSKSurvivalTransitionToken CancelledBreak = GetPostureBreakToken();
        const bool bWasBroken = bPostureBroken;
        bPostureBroken = false;
        if (TransitionSerial == MAX_int64)
        {
            bSerialExhausted = true;
            SetLifeState(ESKLifeState::Dying);
            UE_LOG(LogTemp, Error, TEXT("Survival 死亡序号耗尽，保持失能且不重用旧令牌"));
            if (bWasBroken) QueueTransition(TEXT("PostureBreakCancelled"), CancelledBreak, TEXT("SerialExhausted"), SourceActor);
            return;
        }
        ++TransitionSerial;
        SetLifeState(ESKLifeState::Dying);
        if (bWasBroken) QueueTransition(TEXT("PostureBreakCancelled"), CancelledBreak, TEXT("Death"), SourceActor);
        QueueTransition(TEXT("DeathStarted"), GetDeathToken(), TEXT("HealthDepleted"), SourceActor);
    }
    else if (LifeState == ESKLifeState::Alive && !bPostureBroken
        && Attributes.MaxPosture > 0.f && Attributes.Posture >= Attributes.MaxPosture)
    {
        if (BreakSerial == MAX_int64)
        {
            bSerialExhausted = true;
            bPostureBroken = true;
            UE_LOG(LogTemp, Error, TEXT("Survival 崩溃序号耗尽，保持失能且不重用旧令牌"));
            UpdateOwnedTags();
            return;
        }
        bPostureBroken = true;
        ++BreakSerial;
        BreakStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        UpdateOwnedTags();
        QueueTransition(TEXT("PostureBroken"), GetPostureBreakToken(), bInitial ? TEXT("Initial") : TEXT("PostureFull"), SourceActor);
    }
}

/** 游戏线程在 ASC 门禁内发布待处理通知；BeginPlay 前保留队列供 Lua 先订阅。 */
void USKSurvivalComponent::FlushResourceEvents()
{
    if (!bInternalResourceCommit && HasBegunPlay()) PublishEvents();
}

/** 游戏线程只读查询IsSurvivalReady；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::IsSurvivalReady() const
{
    return IsInGameThread() && !bEndedPlay && bBound && bReady && ASC && ASC->IsAttributesReady()
        && ASC->IsResourcePolicyBoundTo(this);
}

/** 游戏线程只读查询GetSurvivalSnapshot；未就绪返回无效/零值，不触发初始化或资源写入。 */
FSKSurvivalSnapshot USKSurvivalComponent::GetSurvivalSnapshot() const
{
    FSKSurvivalSnapshot Snapshot;
    if (!IsInGameThread()) return Snapshot;
    Snapshot.bReady = IsSurvivalReady();
    Snapshot.LifeState = LifeState;
    Snapshot.LifeSerial = LifeSerial;
    Snapshot.bPostureBroken = bPostureBroken;
    if (ASC && !bEndedPlay) Snapshot.Attributes = ASC->GetAttributeSnapshot();
    return Snapshot;
}

/** 游戏线程只读查询GetLifeState；未就绪返回无效/零值，不触发初始化或资源写入。 */
ESKLifeState USKSurvivalComponent::GetLifeState() const
{
    return IsInGameThread() ? LifeState : ESKLifeState::Uninitialized;
}

/** 游戏线程只读查询GetLifeSerial；未就绪返回无效/零值，不触发初始化或资源写入。 */
int64 USKSurvivalComponent::GetLifeSerial() const
{
    return IsInGameThread() ? LifeSerial : 0;
}

/** 游戏线程只读查询GetDeathToken；未就绪返回无效/零值，不触发初始化或资源写入。 */
FSKSurvivalTransitionToken USKSurvivalComponent::GetDeathToken() const
{
    return IsSurvivalReady() && !bSerialExhausted && (LifeState == ESKLifeState::Dying || LifeState == ESKLifeState::Dead) ? MakeToken(ESKSurvivalTransitionKind::Death) : FSKSurvivalTransitionToken();
}

/** 游戏线程只读查询GetReviveToken；未就绪返回无效/零值，不触发初始化或资源写入。 */
FSKSurvivalTransitionToken USKSurvivalComponent::GetReviveToken() const
{
    return IsSurvivalReady() && !bSerialExhausted && LifeState == ESKLifeState::Reviving ? MakeToken(ESKSurvivalTransitionKind::Revive) : FSKSurvivalTransitionToken();
}

/** 游戏线程只读查询GetPostureBreakToken；未就绪返回无效/零值，不触发初始化或资源写入。 */
FSKSurvivalTransitionToken USKSurvivalComponent::GetPostureBreakToken() const
{
    return IsSurvivalReady() && !bSerialExhausted && bPostureBroken ? MakeToken(ESKSurvivalTransitionKind::PostureBreak) : FSKSurvivalTransitionToken();
}

/** 游戏线程只读查询IsAlive；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::IsAlive() const
{
    return IsSurvivalReady() && LifeState == ESKLifeState::Alive;
}

/** 游戏线程只读查询IsDead；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::IsDead() const
{
    return IsSurvivalReady() && LifeState == ESKLifeState::Dead;
}

/** 游戏线程只读查询IsPostureBroken；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::IsPostureBroken() const
{
    return IsSurvivalReady() && bPostureBroken;
}

/** 游戏线程只读查询CanAct；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::CanAct() const
{
    if (!IsAlive() || bPostureBroken || bSerialExhausted) return false;
    const FSKAttributeSnapshot Attributes = ASC->GetAttributeSnapshot();
    return Attributes.Health > 0.f && Attributes.Posture < Attributes.MaxPosture;
}

/** 游戏线程只读查询CanReceiveDamage；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::CanReceiveDamage() const
{
    return IsSurvivalReady() && CheckResourceOperation(ESKNumericOperation::Damage) == ESKNumericResultCode::Applied;
}

/** 游戏线程只读查询CanBeginRevive；未就绪返回无效/零值，不触发初始化或资源写入。 */
bool USKSurvivalComponent::CanBeginRevive() const
{
    return IsDead() && !bSerialExhausted && !ASC->IsResourceCommitActive() && !ASC->HasMatchingGameplayTag(ReviveBlockedTag) && LifeSerial < MAX_int64 && TransitionSerial < MAX_int64;
}

/** 游戏线程只读查询GetHealth；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetHealth() const
{
    return IsSurvivalReady() ? ASC->GetAttributeSnapshot().Health : 0.f;
}

/** 游戏线程只读查询GetMaxHealth；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetMaxHealth() const
{
    return IsSurvivalReady() ? ASC->GetAttributeSnapshot().MaxHealth : 0.f;
}

/** 游戏线程只读查询GetHealthRatio；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetHealthRatio() const
{
    const float Maximum = GetMaxHealth();
    return Maximum > 0.f ? FMath::Clamp(GetHealth() / Maximum, 0.f, 1.f) : 0.f;
}

/** 游戏线程只读查询GetPosture；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetPosture() const
{
    return IsSurvivalReady() ? ASC->GetAttributeSnapshot().Posture : 0.f;
}

/** 游戏线程只读查询GetMaxPosture；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetMaxPosture() const
{
    return IsSurvivalReady() ? ASC->GetAttributeSnapshot().MaxPosture : 0.f;
}

/** 游戏线程只读查询GetPostureRatio；未就绪返回无效/零值，不触发初始化或资源写入。 */
float USKSurvivalComponent::GetPostureRatio() const
{
    const float Maximum = GetMaxPosture();
    return Maximum > 0.f ? FMath::Clamp(GetPosture() / Maximum, 0.f, 1.f) : 0.f;
}

/** 游戏线程只读查询IsTransitionTokenValid；未就绪返回无效/零值，不触发初始化或资源写入。 Token 为待验证身份，不取得拥有者引用。 */
bool USKSurvivalComponent::IsTransitionTokenValid(const FSKSurvivalTransitionToken& Token) const
{
    if (!IsSurvivalReady() || bSerialExhausted || Token.Owner.Get() != this || Token.LifeSerial != LifeSerial || Token.TransitionSerial <= 0) return false;
    if (Token.Kind == ESKSurvivalTransitionKind::PostureBreak)
        return IsAlive() && bPostureBroken && Token.TransitionSerial == BreakSerial;
    if (Token.TransitionSerial != TransitionSerial) return false;
    return (Token.Kind == ESKSurvivalTransitionKind::Death && LifeState == ESKLifeState::Dying)
        || (Token.Kind == ESKSurvivalTransitionKind::Revive && LifeState == ESKLifeState::Reviving);
}

/**
 * 游戏线程通过 ASC 策略提交ApplyHealthDamage请求，不重新计算攻防或护甲。
 * @param Amount 正有限资源点数。
 * @param SourceActor 可空来源，不转移所有权。
 * @return ASC 实际结算；未绑定时返回 NotReady，回调内返回 Reentrant。
 */
FSKNumericResult USKSurvivalComponent::ApplyHealthDamage(float Amount, AActor* SourceActor)
{
    if (!IsInGameThread() || !ASC || bEndedPlay) return FSKNumericResult();
    return ASC->ApplyHealthDamage(Amount, SourceActor);
}

/**
 * 游戏线程通过 ASC 策略提交RestoreHealth请求，不重新计算攻防或护甲。
 * @param Amount 正有限资源点数。
 * @param SourceActor 可空来源，不转移所有权。
 * @return ASC 实际结算；未绑定时返回 NotReady，回调内返回 Reentrant。
 */
FSKNumericResult USKSurvivalComponent::RestoreHealth(float Amount, AActor* SourceActor)
{
    if (!IsInGameThread() || !ASC || bEndedPlay) return FSKNumericResult();
    return ASC->RestoreHealth(Amount, SourceActor);
}

/**
 * 游戏线程通过 ASC 策略提交ApplyPostureDamage请求，不重新计算攻防或护甲。
 * @param Amount 正有限资源点数。
 * @param SourceActor 可空来源，不转移所有权。
 * @return ASC 实际结算；未绑定时返回 NotReady，回调内返回 Reentrant。
 */
FSKNumericResult USKSurvivalComponent::ApplyPostureDamage(float Amount, AActor* SourceActor)
{
    if (!IsInGameThread() || !ASC || bEndedPlay) return FSKNumericResult();
    return ASC->ApplyPostureDamage(Amount, SourceActor);
}

/**
 * 游戏线程通过 ASC 策略提交RestorePosture请求，不重新计算攻防或护甲。
 * @param Amount 正有限资源点数。
 * @param SourceActor 可空来源，不转移所有权。
 * @return ASC 实际结算；未绑定时返回 NotReady，回调内返回 Reentrant。
 */
FSKNumericResult USKSurvivalComponent::RestorePosture(float Amount, AActor* SourceActor)
{
    if (!IsInGameThread() || !ASC || bEndedPlay) return FSKNumericResult();
    return ASC->RestorePosture(Amount, SourceActor);
}

/**
 * 游戏线程提交一次双资源最终伤害，只在两通道执行后发布生命/崩溃事实。
 * @param HealthDamage 有限非负最终生命伤害。
 * @param PostureDamage 有限非负最终躯干伤害，至少一通道为正。
 * @param SourceActor 可空来源，不取得所有权。
 * @return 各通道实际结果、最终快照及首次状态转换标记。
 */
FSKSurvivalImpactResult USKSurvivalComponent::ApplySurvivalImpact(
    float HealthDamage, float PostureDamage, AActor* SourceActor)
{
    FSKSurvivalImpactResult Result;
    if (!IsInGameThread() || !ASC || bEndedPlay) return Result;
    const ESKLifeState PreviousLife = LifeState;
    const bool bPreviouslyBroken = bPostureBroken;
    Result.Numeric = ASC->ApplyResourceImpact(HealthDamage, PostureDamage, SourceActor);
    Result.Snapshot = GetSurvivalSnapshot();
    Result.bDeathStarted = PreviousLife == ESKLifeState::Alive && LifeState == ESKLifeState::Dying;
    Result.bPostureBroken = !bPreviouslyBroken && bPostureBroken;
    return Result;
}

/**
 * 游戏线程完成当前死亡收尾，不销毁 Actor 或发放奖励。
 * @param Token 当前死亡身份；同一已完成死亡重复调用返回 NoChange。
 * @return 转换结果与实际快照；过期、重入不产生副作用。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::FinishDeath(const FSKSurvivalTransitionToken& Token)
{
    if (!IsSurvivalReady()) return MakeResult(ESKSurvivalResultCode::NotReady, Token);
    if (ASC->IsResourceCommitActive()) return MakeResult(ESKSurvivalResultCode::Reentrant, Token);
    if (LifeState == ESKLifeState::Dead && Token.Owner.Get() == this && Token.Kind == ESKSurvivalTransitionKind::Death
        && Token.LifeSerial == LifeSerial && Token.TransitionSerial == TransitionSerial && TransitionSerial > 0)
        return MakeResult(ESKSurvivalResultCode::NoChange, Token);
    const ESKSurvivalResultCode Validation = ValidateTransition(Token, ESKSurvivalTransitionKind::Death);
    if (Validation != ESKSurvivalResultCode::Applied) return MakeResult(Validation, Token);
    TGuardValue<bool> Guard(bTransitionBusy, true);
    SetLifeState(ESKLifeState::Dead);
    QueueTransition(TEXT("DeathFinished"), Token, TEXT("Completed"), nullptr);
    PublishEvents();
    return MakeResult(ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程开始回生，普通战斗继续关闭，不恢复资源或消费费用。
 * @param ExpectedLifeSerial 调用方持有的死亡生命轮次，必须与当前一致。
 * @return 成功返回新回生令牌；非法状态/标签/重入返回明确原因。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::BeginRevive(int64 ExpectedLifeSerial)
{
    if (!IsSurvivalReady()) return MakeResult(ESKSurvivalResultCode::NotReady, FSKSurvivalTransitionToken());
    if (ASC->IsResourceCommitActive()) return MakeResult(ESKSurvivalResultCode::Reentrant, GetReviveToken());
    if (ExpectedLifeSerial != LifeSerial) return MakeResult(ESKSurvivalResultCode::StaleTransition, GetReviveToken());
    if (LifeState != ESKLifeState::Dead) return MakeResult(ESKSurvivalResultCode::InvalidState, GetReviveToken());
    if (!CanBeginRevive()) return MakeResult(ESKSurvivalResultCode::Blocked, FSKSurvivalTransitionToken());
    TGuardValue<bool> Guard(bTransitionBusy, true);
    ++TransitionSerial;
    SetLifeState(ESKLifeState::Reviving);
    const FSKSurvivalTransitionToken Token = GetReviveToken();
    QueueTransition(TEXT("ReviveStarted"), Token, TEXT("Requested"), nullptr);
    PublishEvents();
    return MakeResult(ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程在回生恢复点写入最新上限对应资源，验证实际值后才进入新生命。
 * @param Token 当前回生令牌；成功/取消后旧令牌失效。
 * @param HealthRatio 有限 (0,1] 生命比例，躯干比例只读 GAS.RevivePostureRatio。
 * @return 失败保持 Reviving 和行动门禁；实际资源不伪造回滚。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::CompleteRevive(
    const FSKSurvivalTransitionToken& Token, float HealthRatio)
{
    const ESKSurvivalResultCode Validation = ValidateTransition(Token, ESKSurvivalTransitionKind::Revive);
    if (Validation != ESKSurvivalResultCode::Applied) return MakeResult(Validation, Token);
    if (!FMath::IsFinite(HealthRatio) || HealthRatio <= 0.f || HealthRatio > 1.f || LifeSerial == MAX_int64)
        return MakeResult(ESKSurvivalResultCode::InvalidInput, Token);
    TGuardValue<bool> Guard(bTransitionBusy, true);
    const FSKAttributeSnapshot Attributes = ASC->GetAttributeSnapshot();
    if (!FMath::IsFinite(Attributes.RevivePostureRatio) || Attributes.RevivePostureRatio < 0.f || Attributes.RevivePostureRatio >= 1.f)
        return MakeResult(ESKSurvivalResultCode::InvalidInput, Token);
    const float TargetHealth = Attributes.MaxHealth * HealthRatio;
    const float TargetPosture = Attributes.MaxPosture * Attributes.RevivePostureRatio;
    const FSKNumericResult Numeric = CommitResources(Token, TargetHealth, TargetPosture);
    if (!WasResourceCommitSuccessful(Numeric, TargetHealth, TargetPosture) || Numeric.After.Health <= 0.f
        || Numeric.After.Posture >= Numeric.After.MaxPosture)
        return MakeResult(ESKSurvivalResultCode::ResourceCommitFailed, Token);
    ++LifeSerial;
    bPostureBroken = false;
    SetLifeState(ESKLifeState::Alive);
    QueueTransition(TEXT("Revived"), Token, TEXT("Completed"), nullptr);
    PublishEvents();
    return MakeResult(ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程取消当前回生，先验证死亡资源清零，再回到 Dead。
 * @param Token 当前回生令牌。
 * @return GE 失败保持 Reviving；成功令牌失效，不替调用方退还费用。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::CancelRevive(const FSKSurvivalTransitionToken& Token)
{
    const ESKSurvivalResultCode Validation = ValidateTransition(Token, ESKSurvivalTransitionKind::Revive);
    if (Validation != ESKSurvivalResultCode::Applied) return MakeResult(Validation, Token);
    TGuardValue<bool> Guard(bTransitionBusy, true);
    const FSKNumericResult Numeric = CommitResources(Token, 0.f, 0.f);
    if (!WasResourceCommitSuccessful(Numeric, 0.f, 0.f))
        return MakeResult(ESKSurvivalResultCode::ResourceCommitFailed, Token);
    SetLifeState(ESKLifeState::Dead);
    QueueTransition(TEXT("ReviveCancelled"), Token, TEXT("Cancelled"), nullptr);
    PublishEvents();
    return MakeResult(ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程持有效崩溃令牌清空躯干，保持 Broken，不结束动画。
 * @param Token 当前崩溃身份。
 * @return 完整提交成功返回 Applied/NoChange；失败继续关闭行动。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::ResetBrokenPosture(const FSKSurvivalTransitionToken& Token)
{
    const ESKSurvivalResultCode Validation = ValidateTransition(Token, ESKSurvivalTransitionKind::PostureBreak);
    if (Validation != ESKSurvivalResultCode::Applied) return MakeResult(Validation, Token);
    TGuardValue<bool> Guard(bTransitionBusy, true);
    const float Health = ASC->GetAttributeSnapshot().Health;
    const FSKNumericResult Numeric = CommitResources(Token, Health, 0.f);
    if (!WasResourceCommitSuccessful(Numeric, Health, 0.f))
        return MakeResult(ESKSurvivalResultCode::ResourceCommitFailed, Token);
    PublishEvents();
    return MakeResult(Numeric.Code == ESKNumericResultCode::NoChange ? ESKSurvivalResultCode::NoChange : ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程验证当前 GAS 最短崩溃时间并提交恢复比例，资源成功后才解除 Broken。
 * @param Token 当前崩溃身份，旧动画令牌拒绝。
 * @return 未到时间 Blocked；提交失败不解锁；成功发布恢复事件。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::CompletePostureRecovery(const FSKSurvivalTransitionToken& Token)
{
    const ESKSurvivalResultCode Validation = ValidateTransition(Token, ESKSurvivalTransitionKind::PostureBreak);
    if (Validation != ESKSurvivalResultCode::Applied) return MakeResult(Validation, Token);
    const FSKAttributeSnapshot Attributes = ASC->GetAttributeSnapshot();
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (Now - BreakStartTime < Attributes.PostureBreakMinimumDuration)
        return MakeResult(ESKSurvivalResultCode::Blocked, Token);
    if (!FMath::IsFinite(Attributes.PostureRecoveryTargetRatio) || Attributes.PostureRecoveryTargetRatio < 0.f
        || Attributes.PostureRecoveryTargetRatio >= 1.f)
        return MakeResult(ESKSurvivalResultCode::InvalidInput, Token);
    TGuardValue<bool> Guard(bTransitionBusy, true);
    const float TargetPosture = Attributes.MaxPosture * Attributes.PostureRecoveryTargetRatio;
    const FSKNumericResult Numeric = CommitResources(Token, Attributes.Health, TargetPosture);
    if (!WasResourceCommitSuccessful(Numeric, Attributes.Health, TargetPosture) || Numeric.After.Posture >= Numeric.After.MaxPosture)
        return MakeResult(ESKSurvivalResultCode::ResourceCommitFailed, Token);
    bPostureBroken = false;
    UpdateOwnedTags();
    QueueTransition(TEXT("PostureRecovered"), Token, TEXT("Completed"), nullptr);
    PublishEvents();
    return MakeResult(ESKSurvivalResultCode::Applied, Token);
}

/**
 * 游戏线程给 Lua 提供躯干增长策略覆盖点；原生不选择攻防倍率。
 * @param Reason 已裁决攻防结果，未知结果由 Lua 拒绝。
 * @param AttackType 通用来袭类型。
 * @param SourceActor 可空来源，不转移所有权。
 * @return 原生无策略时失败关闭；Lua 返回是否接受。
 */
bool USKSurvivalComponent::ApplyPostureImpact_Implementation(
    FName Reason, ESKIncomingAttackType AttackType, AActor* SourceActor)
{
    return false;
}

/**
 * 游戏线程提供自然恢复/延迟流程覆盖点，原生不自动恢复生命或躯干。
 * @param DeltaSeconds 有限正秒数；Tick 已过滤非法值。
 */
void USKSurvivalComponent::HandleSurvivalTick_Implementation(float DeltaSeconds)
{
}

/** 游戏线程在 Lua 订阅之后发布初始化事件，未绑定时尝试同 Actor ASC。 */
void USKSurvivalComponent::BeginPlay()
{
    if (!bBound) BindAttributeSystem(GetOwner() ? GetOwner()->FindComponentByClass<USKAbilitySystemComponent>() : nullptr);
    const bool bEngineDispatchesReceiveBeginPlay = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);
    Super::BeginPlay();
    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
    PublishEvents();
}

/**
 * 游戏线程先关闭门禁并注销策略，再撤销自己拥有的标签贡献，不发布死亡事件。
 * @param EndPlayReason 引擎生命周期原因，透传父类。
 */
void USKSurvivalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndedPlay = true;
    bReady = false;
    bBound = false;
    PendingEvents.Reset();
    PendingEventNames.Reset();
    if (ASC)
    {
        ASC->UnregisterResourcePolicy(this);
        for (const FGameplayTag& Tag : OwnedStateTags) ASC->RemoveLooseGameplayTag(Tag);
    }
    OwnedStateTags.Reset();
    ASC = nullptr;
    const bool bEngineDispatchesReceiveEndPlay = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);
    if (!bEngineDispatchesReceiveEndPlay && !HasAnyFlags(RF_BeginDestroyed) && !IsUnreachable())
        ReceiveEndPlay(EndPlayReason);
    Super::EndPlay(EndPlayReason);
}

/**
 * 游戏线程先收敛引擎旁路变化，再调用 Lua 工作流。
 * @param DeltaTime 当前正有限秒数，非法值忽略。
 * @param TickType 引擎 Tick 类型，透传父类。
 * @param ThisTickFunction 当前 Tick 描述，不持有引用。
 */
void USKSurvivalComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bEndedPlay || !IsInGameThread() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
    if (ASC && !ASC->IsResourceCommitActive())
    {
        ReconcileResourceState(nullptr, false);
        PublishEvents();
    }
    HandleSurvivalTick(DeltaTime);
}

/**
 * 游戏线程在首次策略绑定前解析配置字典中的完整标签集，不注册、重建或导入标签。
 * 所有名称均解析成功才更新组件缓存；任何缺失都记录明确诊断并保持未绑定，
 * 避免把无效免疫标签解释为没有免疫。只允许在正常初始化/绑定阶段调用，
 * 不在构造函数或模块静态初始化中访问标签管理器。
 * @return 全部七个标签已存在于引擎字典时返回 true；非游戏线程或任一缺失返回 false。
 */
bool USKSurvivalComponent::ResolveRequiredGameplayTags()
{
    if (!IsInGameThread()) return false;
    const FName RequiredNames[] = {
        TEXT("State.Life.Dying"),
        TEXT("State.Life.Dead"),
        TEXT("State.Life.Reviving"),
        TEXT("State.Posture.Broken"),
        TEXT("State.Damage.Immune"),
        TEXT("State.Posture.Immune"),
        TEXT("State.Revive.Blocked")
    };
    FGameplayTag ResolvedTags[UE_ARRAY_COUNT(RequiredNames)];
    TArray<FString> MissingNames;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(RequiredNames); ++Index)
    {
        ResolvedTags[Index] = FGameplayTag::RequestGameplayTag(RequiredNames[Index], false);
        if (!ResolvedTags[Index].IsValid()) MissingNames.Add(RequiredNames[Index].ToString());
    }
    if (!MissingNames.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Survival 必需 GameplayTag 缺失：%s。拒绝绑定资源策略；请检查已注册的 GameplayTag 数据表或配置，刷新字典或重启进程。Owner=%s"),
            *FString::Join(MissingNames, TEXT(", ")), *GetNameSafe(GetOwner()));
        return false;
    }
    LifeDyingTag = ResolvedTags[0];
    LifeDeadTag = ResolvedTags[1];
    LifeRevivingTag = ResolvedTags[2];
    PostureBrokenTag = ResolvedTags[3];
    DamageImmuneTag = ResolvedTags[4];
    PostureImmuneTag = ResolvedTags[5];
    ReviveBlockedTag = ResolvedTags[6];
    return true;
}

/**
 * 游戏线程生成当前流程令牌；只复制弱身份和单调序号，不改变状态。
 * @param Kind Death/Revive 使用过程序号，PostureBreak 使用崩溃序号。
 * @return 当前身份，未开始序号仍为零且不能提交。
 */
FSKSurvivalTransitionToken USKSurvivalComponent::MakeToken(ESKSurvivalTransitionKind Kind) const
{
    FSKSurvivalTransitionToken Token;
    Token.Owner = const_cast<USKSurvivalComponent*>(this);
    Token.LifeSerial = LifeSerial;
    Token.TransitionSerial = Kind == ESKSurvivalTransitionKind::PostureBreak ? BreakSerial : TransitionSerial;
    Token.Kind = Kind;
    return Token;
}

/**
 * 游戏线程生成转换实际结果，不触发资源修改。
 * @param Code 本次结果码。
 * @param Token 当前或已完成流程的身份。
 * @return 当前真实快照，失败不伪造成功状态。
 */
FSKSurvivalTransitionResult USKSurvivalComponent::MakeResult(
    ESKSurvivalResultCode Code, const FSKSurvivalTransitionToken& Token) const
{
    FSKSurvivalTransitionResult Result;
    Result.Code = Code;
    Result.Token = Token;
    Result.Snapshot = GetSurvivalSnapshot();
    return Result;
}

/**
 * 游戏线程校验公开转换门禁及身份，不打开资源旁路。
 * @param Token 外部持有流程令牌。
 * @param Kind 当前接口所要求的过程类型。
 * @return Applied 表示可继续；其他结果无状态副作用。
 */
ESKSurvivalResultCode USKSurvivalComponent::ValidateTransition(
    const FSKSurvivalTransitionToken& Token, ESKSurvivalTransitionKind Kind) const
{
    if (!IsSurvivalReady()) return ESKSurvivalResultCode::NotReady;
    if (ASC->IsResourceCommitActive()) return ESKSurvivalResultCode::Reentrant;
    if (Token.Kind != Kind || !IsTransitionTokenValid(Token)) return ESKSurvivalResultCode::StaleTransition;
    return ESKSurvivalResultCode::Applied;
}

/**
 * 游戏线程更新唯一生命状态并同步标签，只排队事件不执行 Lua。
 * @param NewState 新机械状态，不直接修改生命或躯干。
 */
void USKSurvivalComponent::SetLifeState(ESKLifeState NewState)
{
    if (LifeState == NewState) return;
    LifeState = NewState;
    bLifeStateEventPending = true;
    UpdateOwnedTags();
}

/** 游戏线程差量维护本组件 loose tag 贡献，不重置外部相同标签计数。 */
void USKSurvivalComponent::UpdateOwnedTags()
{
    if (!ASC || bEndedPlay) return;
    FGameplayTagContainer Desired;
    if (LifeState == ESKLifeState::Dying) Desired.AddTag(LifeDyingTag);
    if (LifeState == ESKLifeState::Dead) Desired.AddTag(LifeDeadTag);
    if (LifeState == ESKLifeState::Reviving) Desired.AddTag(LifeRevivingTag);
    if (bPostureBroken) Desired.AddTag(PostureBrokenTag);
    for (const FGameplayTag& Tag : OwnedStateTags)
    {
        if (!Desired.HasTagExact(Tag)) ASC->RemoveLooseGameplayTag(Tag);
    }
    for (const FGameplayTag& Tag : Desired)
    {
        if (!OwnedStateTags.HasTagExact(Tag)) ASC->AddLooseGameplayTag(Tag);
    }
    OwnedStateTags = Desired;
}

/**
 * 游戏线程复制过程通知数据，保留发生时快照，不调用外部逻辑。
 * @param EventName 内部语义事件名。
 * @param Token 当前或取消流程身份。
 * @param Reason 可诊断原因名称，不代表 Boss 奖励规则。
 * @param SourceActor 可空来源，在待发布事件中持有 UObject 引用。
 */
void USKSurvivalComponent::QueueTransition(
    FName EventName, const FSKSurvivalTransitionToken& Token, FName Reason, AActor* SourceActor)
{
    FSKSurvivalTransitionEvent Event;
    Event.Token = Token;
    Event.Reason = Reason;
    Event.SourceActor = SourceActor;
    Event.Snapshot = GetSurvivalSnapshot();
    PendingEvents.Add(Event);
    PendingEventNames.Add(EventName);
}

/** 游戏线程发布合并资源快照和过程事件；整个同步通知期间拒绝公开转换和数值重入。 */
void USKSurvivalComponent::PublishEvents()
{
    if (bEndedPlay || !bReady || !HasBegunPlay()) return;
    TGuardValue<bool> Guard(bTransitionBusy, true);
    const FSKAttributeSnapshot Attributes = ASC->GetAttributeSnapshot();
    if (!bHasPublishedResources || Attributes.Health != LastPublishedAttributes.Health || Attributes.MaxHealth != LastPublishedAttributes.MaxHealth)
        OnHealthChanged.Broadcast(Attributes.Health, Attributes.MaxHealth, Attributes.MaxHealth > 0.f ? Attributes.Health / Attributes.MaxHealth : 0.f);
    if (bEndedPlay) return;
    if (!bHasPublishedResources || Attributes.Posture != LastPublishedAttributes.Posture || Attributes.MaxPosture != LastPublishedAttributes.MaxPosture)
        OnPostureChanged.Broadcast(Attributes.Posture, Attributes.MaxPosture, Attributes.MaxPosture > 0.f ? Attributes.Posture / Attributes.MaxPosture : 0.f);
    if (bEndedPlay) return;
    LastPublishedAttributes = Attributes;
    bHasPublishedResources = true;
    if (bReadyEventPending)
    {
        bReadyEventPending = false;
        OnSurvivalReady.Broadcast();
    }
    if (bEndedPlay) return;
    if (bLifeStateEventPending)
    {
        bLifeStateEventPending = false;
        OnLifeStateChanged.Broadcast(LifeState);
    }
    if (bEndedPlay) return;
    TArray<FSKSurvivalTransitionEvent> Events = MoveTemp(PendingEvents);
    TArray<FName> Names = MoveTemp(PendingEventNames);
    PendingEvents.Reset();
    PendingEventNames.Reset();
    for (int32 Index = 0; Index < Events.Num() && !bEndedPlay; ++Index)
    {
        const FName Name = Names[Index];
        const FSKSurvivalTransitionEvent& Event = Events[Index];
        if (Name == TEXT("DeathStarted")) OnDeathStarted.Broadcast(Event);
        else if (Name == TEXT("DeathFinished")) OnDeathFinished.Broadcast(Event);
        else if (Name == TEXT("ReviveStarted")) OnReviveStarted.Broadcast(Event);
        else if (Name == TEXT("Revived")) OnRevived.Broadcast(Event);
        else if (Name == TEXT("ReviveCancelled")) OnReviveCancelled.Broadcast(Event);
        else if (Name == TEXT("PostureBroken")) OnPostureBroken.Broadcast(Event);
        else if (Name == TEXT("PostureRecovered")) OnPostureRecovered.Broadcast(Event);
        else if (Name == TEXT("PostureBreakCancelled")) OnPostureBreakCancelled.Broadcast(Event);
    }
}

/**
 * 游戏线程短暂打开绑定拥有者的原生恢复窗口，提交完毕立即关闭。
 * @param Token 已在公开入口验证的令牌。
 * @param Health 有限目标生命点数。
 * @param Posture 有限目标躯干点数。
 * @return GAS 实际双资源结果；不得以预期值代替返回快照。
 */
FSKNumericResult USKSurvivalComponent::CommitResources(
    const FSKSurvivalTransitionToken& Token, float Health, float Posture)
{
    TGuardValue<bool> CommitGuard(bInternalResourceCommit, true);
    TGuardValue<int64> SerialGuard(InternalTransitionSerial, Token.TransitionSerial);
    return ASC->RestoreOwnedResources(this, Token.TransitionSerial, Health, Posture);
}

/**
 * 游戏线程复核受控恢复真实值，拒绝未执行 GE、部分提交或离场。
 * @param Result ASC 执行结果。
 * @param Health 期望生命绝对值。
 * @param Posture 期望躯干绝对值。
 * @return 已执行且实际目标一致为 true，不产生写入。
 */
bool USKSurvivalComponent::WasResourceCommitSuccessful(
    const FSKNumericResult& Result, float Health, float Posture) const
{
    return IsSurvivalReady() && (Result.Code == ESKNumericResultCode::Applied || Result.Code == ESKNumericResultCode::NoChange)
        && FMath::IsNearlyEqual(Result.After.Health, Health) && FMath::IsNearlyEqual(Result.After.Posture, Posture);
}
