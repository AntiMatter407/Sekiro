// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/SKCombatComponent.h"
#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"
#include "AI/SKAIBattleProjectile.h"
#include "AIController.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Character/SKCharacter.h"
#include "Character/SKSurvivalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "Input/SKInputManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Movement/SKMovementComponent.h"
#include "Weapon/SKWeaponManagerComponent.h"

namespace
{
    constexpr int32 AICombatEventQueueCapacity = 32; // 通用 AI 战斗事件队列固定容量

    /** 游戏线程查询 Actor 或其 Controller 的明确阵营；未配置返回 NoTeam，不推测敌我。 */
    FGenericTeamId GetCombatTeam(const AActor* Actor)
    {
        const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Actor);
        if (TeamAgent) return TeamAgent->GetGenericTeamId();
        const APawn* Pawn = Cast<APawn>(Actor);
        TeamAgent = Pawn ? Cast<IGenericTeamAgentInterface>(Pawn->GetController()) : nullptr;
        return TeamAgent ? TeamAgent->GetGenericTeamId() : FGenericTeamId::NoTeam;
    }

    /** 游戏线程只读判定提交是否成功或仅受资源免疫抑制；其他策略拒绝和 GE 失败均不接受。 */
    bool IsCommittedCombatNumeric(const FSKNumericResult& Numeric)
    {
        if (Numeric.Code == ESKNumericResultCode::Applied || Numeric.Code == ESKNumericResultCode::NoChange) return true;
        if (Numeric.Code != ESKNumericResultCode::PolicyRejected || !Numeric.RejectionReason.IsNone()
            || (Numeric.RequestedHealthDamage == 0.f && Numeric.RequestedPostureDamage == 0.f)) return false;
        const bool bHealthAccepted = Numeric.RequestedHealthDamage == 0.f
            || Numeric.HealthChannelCode == ESKNumericResultCode::Applied
            || Numeric.HealthRejectionReason == FName(TEXT("HealthImmune"));
        const bool bPostureAccepted = Numeric.RequestedPostureDamage == 0.f
            || Numeric.PostureChannelCode == ESKNumericResultCode::Applied
            || Numeric.PostureRejectionReason == FName(TEXT("PostureImmune"));
        return bHealthAccepted && bPostureAccepted;
    }
}

/**
 * 创建可由 Lua 编排的战斗动作宿主并启用 PrePhysics Tick。
 * 构造阶段不访问 Owner、动画实例或世界；组件只能在游戏线程创建和使用。
 */
USKCombatComponent::USKCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

/**
 * 返回 UnLua 加载当前战斗动作脚本时使用的模块名。
 * 本函数不加载脚本且不修改状态，可在游戏线程的 UnLua 绑定流程中调用。
 *
 * @return 配置的 Lua 模块名；空字符串表示不绑定脚本模块。
 */
FString USKCombatComponent::GetModuleName_Implementation() const
{
    return LuaModuleName;
}

/**
 * 提供没有 Lua 覆盖时的空战斗 Tick 回退，避免 C++ 依赖脚本模块名或手写 Lua 调用。
 * 实际战斗状态机由 UnLua 以同名函数覆盖；仅由组件的 PrePhysics Tick 在游戏线程调用。
 *
 * @param DeltaTime 当前帧步长，单位秒；默认实现不消费该值。
 */
void USKCombatComponent::HandleCombatTick_Implementation(float DeltaTime)
{
    (void)DeltaTime;
}

/**
 * 将命中裁决产生的抽象防御结果交给脚本层处理。
 * 默认实现不解释 ResultName、不计算架势增量且不切换动作状态；UnLua 可用同名方法覆盖。
 * 只能在游戏线程调用。
 *
 * @param ResultName 由 Lua 解释的稳定结果名，例如 DeflectSuccess、Guarded 或 DeflectFailed。
 * @param AttackType 本次来袭的抽象攻击强度类型，不携带资产或数值策略。
 */
void USKCombatComponent::HandlePostureImpact_Implementation(
    FName ResultName,
    ESKIncomingAttackType AttackType)
{
    (void)ResultName;
    (void)AttackType;
}

/**
 * 提供脚本未绑定时的攻击类型回退，使普通攻击仍能进入接触裁决。
 * 默认只根据离散动作状态区分轻攻击与重攻击；Lua 可进一步把蓄力突刺映射为 Thrust。
 * 本函数只读当前战斗状态，不加载资源且不修改组件，必须在游戏线程调用。
 *
 * @return HeavyAttack 返回 Heavy，其他状态返回 Light。
 */
ESKIncomingAttackType USKCombatComponent::ResolveOutgoingAttackType_Implementation() const
{
    return CombatActionState == ESKCombatActionState::HeavyAttack
        ? ESKIncomingAttackType::Heavy
        : ESKIncomingAttackType::Light;
}

/**
 * 提供脚本未绑定时的武器接触回退，避免通用碰撞桥接依赖项目战斗规则。
 * 默认把接触视为普通命中；Lua 覆盖只检查防御阶段，不修改双方架势或播放反应。
 * 本函数不应用伤害、不记录去重且不保留攻击者引用，必须在游戏线程调用。
 *
 * @param AttackerCombat 发起攻击的战斗组件，可为空；默认实现不访问该对象。
 * @param AttackType 本次攻击的抽象类型；默认实现不解释该值。
 * @return 脚本未接管时返回纯 Hit 分类；真实伤害必须经过 ResolveCombatHit，不能据此直接扣血。
 */
ESKWeaponContactResult USKCombatComponent::ResolveIncomingWeaponContact_Implementation(
    USKCombatComponent* AttackerCombat,
    ESKIncomingAttackType AttackType)
{
    (void)AttackerCombat;
    (void)AttackType;
    return ESKWeaponContactResult::Hit;
}

/**
 * 游戏线程让 Lua 读取 GAS/攻击配置得到近战基础生命伤害，不提交任何资源。
 * @param AttackType 当前已选择的攻击类型；默认实现不解释类型。
 * @return 未绑定 Lua 返回负值使来源注册失败关闭；正常规则必须返回有限非负数。
 */
float USKCombatComponent::ResolveOutgoingHealthDamage_Implementation(ESKIncomingAttackType AttackType) const
{
    (void)AttackType;
    return -1.f;
}

/**
 * 游戏线程纯裁决攻防结果、最终生命伤害与双方躯干语义；不得写 GAS、切换动作或播放动画。
 * @param Request 已通过原生对象、数值、来源票据和生命门禁的只读请求。
 * @return 未绑定规则返回 bAccepted=false，禁止使用硬编码伤害兜底。
 */
FSKCombatHitEvaluation USKCombatComponent::EvaluateCombatHit_Implementation(const FSKCombatHitRequest& Request) const
{
    (void)Request;
    return FSKCombatHitEvaluation();
}

/**
 * 游戏线程接收已经完成的权威命中事实，供 Lua 演出；不得再提交同笔 Health/Posture。
 * @param Request 原始来源、动作和命中上下文，调用期间只读。
 * @param Result 实际最终结果，死亡/打崩不覆盖攻防 Outcome。
 * @param bAsSource true 为攻方反馈，false 为守方反馈；脚本须检查生命与动作身份再演出。
 */
void USKCombatComponent::HandleCombatHitCommitted_Implementation(
    const FSKCombatHitRequest& Request, const FSKCombatHitResult& Result, bool bAsSource)
{
    (void)Request;
    (void)Result;
    (void)bAsSource;
}

/**
 * 游戏线程为一个攻击窗口或飞行物签发唯一来源票据，并锁存动作、生命与配置伤害。
 * 近战票据只在当前动作有效；飞行物允许动作自然结束后命中，但射手死亡/回生后全部失效。
 * @param Emitter 当前角色拥有的武器或飞行物，必须有效；只保存弱引用。
 * @param Channel Melee 或 Projectile，其他枚举值拒绝。
 * @param HealthDamage 有限非负基础生命伤害；不在原生推导公式。
 * @param PostureDamage 有限非负额外躯干伤害，交 Survival 公式统一封顶。
 * @param OutRequest 输出签发字段，调用方仅补 TargetActor、几何和 EventTag；失败重置为空。
 * @return 生存允许行动且签发成功返回 true；重复载体、无权威、重入或无有效动作返回 false。
 */
bool USKCombatComponent::RegisterCombatHitSource(AActor* Emitter, ESKCombatDamageChannel Channel,
    float HealthDamage, float PostureDamage, FSKCombatHitRequest& OutRequest)
{
    OutRequest = FSKCombatHitRequest();
    if (!IsInGameThread() || bCombatEndedPlay || bResolvingCombatHit) return false;
    AActor* Owner = GetOwner();
    const USKSurvivalComponent* Survival = IsValid(Owner) ? Owner->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!Survival || !Survival->CanAct() || !Owner->HasAuthority() || !IsValid(Emitter)
        || Emitter->GetOwner() != Owner || ActionSerial <= 0 || LastHitSourceSerial == MAX_int64
        || (Channel != ESKCombatDamageChannel::Melee && Channel != ESKCombatDamageChannel::Projectile)
        || !FMath::IsFinite(HealthDamage) || HealthDamage < 0.f
        || !FMath::IsFinite(PostureDamage) || PostureDamage < 0.f) return false;

    for (TMap<int64, FSKCombatHitSourceState>::TIterator It = CombatHitSources.CreateIterator(); It; ++It)
    {
        const FSKCombatHitSourceState& Existing = It.Value();
        if (!Existing.Emitter.IsValid() || Existing.LifeSerial != Survival->GetLifeSerial()
            || (Existing.Channel == ESKCombatDamageChannel::Melee && Existing.ActionSerial != ActionSerial))
        {
            It.RemoveCurrent();
            continue;
        }
        if (Existing.Emitter.Get() == Emitter) return false;
    }

    FSKCombatHitSourceState State;
    State.Emitter = Emitter;
    State.LifeSerial = Survival->GetLifeSerial();
    State.ActionSerial = ActionSerial;
    State.AttackType = ResolveOutgoingAttackType();
    if (static_cast<uint8>(State.AttackType) > static_cast<uint8>(ESKIncomingAttackType::Special)) return false;
    State.Channel = Channel;
    State.HealthDamage = HealthDamage;
    State.PostureDamage = PostureDamage;
    const int64 Serial = ++LastHitSourceSerial;
    CombatHitSources.Add(Serial, State);
    OutRequest.SourceActor = Owner;
    OutRequest.SourceLifeSerial = State.LifeSerial;
    OutRequest.SourceActionSerial = State.ActionSerial;
    OutRequest.HitSourceSerial = Serial;
    OutRequest.AttackType = State.AttackType;
    OutRequest.DamageChannel = Channel;
    OutRequest.HealthDamage = HealthDamage;
    OutRequest.PostureDamage = PostureDamage;
    return true;
}

/** 游戏线程关闭指定正来源票据；未知/已关闭编号幂等忽略，不影响其他攻击或动作。 */
void USKCombatComponent::ReleaseCombatHitSource(int64 HitSourceSerial)
{
    if (IsInGameThread()) CombatHitSources.Remove(HitSourceSerial);
}

/**
 * 游戏线程在守方执行唯一命中结算：纯 Lua 裁决、纯 Survival 计算、目标双资源一次提交、结果发布。
 * 同时锁定攻守两组件防止同步通知重入；不保证跨两个 ASC 的事务回滚，攻方反馈失败单独保存在 SourceNumeric。
 * @param Request 武器/飞行物签发后补全的接触事实；禁止跨角色、旧生命、近战旧动作、重复目标和同队请求。
 * @return Rejected 带明确原因且不发布命中事件；Committed 带真实数值和独立的攻防/死亡/崩溃标记。
 */
FSKCombatHitResult USKCombatComponent::ResolveCombatHit(const FSKCombatHitRequest& Request)
{
    FSKCombatHitResult Result;
    if (!IsInGameThread()) { Result.RejectionReason = TEXT("WrongThread"); return Result; }
    AActor* Target = GetOwner();
    AActor* Source = Request.SourceActor;
    if (bCombatEndedPlay || !IsValid(Target) || !IsValid(Source) || Request.TargetActor != Target
        || Source->GetWorld() != Target->GetWorld()) { Result.RejectionReason = TEXT("InvalidActor"); return Result; }
    if (!Target->HasAuthority() || !Source->HasAuthority()) { Result.RejectionReason = TEXT("NotAuthority"); return Result; }
    if (Source == Target) { Result.RejectionReason = TEXT("SelfHit"); return Result; }
    if (!FMath::IsFinite(Request.HealthDamage) || Request.HealthDamage < 0.f
        || !FMath::IsFinite(Request.PostureDamage) || Request.PostureDamage < 0.f
        || Request.ImpactPoint.ContainsNaN() || Request.AttackDirection.ContainsNaN())
    { Result.RejectionReason = TEXT("InvalidNumericInput"); return Result; }
    const FGenericTeamId SourceTeam = GetCombatTeam(Source);
    if (SourceTeam != FGenericTeamId::NoTeam && SourceTeam == GetCombatTeam(Target))
    { Result.RejectionReason = TEXT("FriendlyFire"); return Result; }

    USKCombatComponent* SourceCombat = Source->FindComponentByClass<USKCombatComponent>();
    USKSurvivalComponent* TargetSurvival = Target->FindComponentByClass<USKSurvivalComponent>();
    USKSurvivalComponent* SourceSurvival = Source->FindComponentByClass<USKSurvivalComponent>();
    if (!IsValid(SourceCombat) || !IsValid(TargetSurvival) || !IsValid(SourceSurvival)
        || !TargetSurvival->IsSurvivalReady() || !SourceSurvival->IsSurvivalReady())
    { Result.RejectionReason = TEXT("NotReady"); return Result; }
    if (bResolvingCombatHit || SourceCombat->bResolvingCombatHit)
    { Result.RejectionReason = TEXT("Reentrant"); return Result; }
    if (!TargetSurvival->IsAlive()) { Result.RejectionReason = TEXT("TargetNotAlive"); return Result; }
    Result.RejectionReason = SourceCombat->ValidateCombatHitSource(Request);
    if (!Result.RejectionReason.IsNone()) return Result;
    TGuardValue<bool> TargetGuard(bResolvingCombatHit, true);
    TGuardValue<bool> SourceGuard(SourceCombat->bResolvingCombatHit, true);
    Result.TargetActionSerial = ActionSerial;
    const int64 TargetLifeSerial = TargetSurvival->GetLifeSerial();
    const FSKCombatHitEvaluation Evaluation = EvaluateCombatHit(Request);
    if (!Evaluation.bAccepted || Evaluation.Outcome == ESKCombatHitOutcome::Ignored
        || static_cast<uint8>(Evaluation.Outcome) > static_cast<uint8>(ESKCombatHitOutcome::Invulnerable)
        || !FMath::IsFinite(Evaluation.HealthDamage) || Evaluation.HealthDamage < 0.f)
    { Result.RejectionReason = TEXT("InvalidCombatEvaluation"); return Result; }
    if (Evaluation.Outcome != ESKCombatHitOutcome::Hit && Evaluation.HealthDamage != 0.f)
    { Result.RejectionReason = TEXT("UnexpectedHealthDamage"); return Result; }
    Result.Outcome = Evaluation.Outcome;
    const bool bAvoided = Result.Outcome == ESKCombatHitOutcome::Dodged || Result.Outcome == ESKCombatHitOutcome::Invulnerable;
    FSKPostureImpactEvaluation TargetPosture;
    FSKPostureImpactEvaluation SourcePosture;
    TargetPosture.bAccepted = true;
    SourcePosture.bAccepted = true;
    if (!bAvoided && !Evaluation.TargetPostureReason.IsNone())
        TargetPosture = TargetSurvival->EvaluatePostureImpact(Evaluation.TargetPostureReason, Request.AttackType, Request.PostureDamage);
    if (!bAvoided && !Evaluation.SourcePostureReason.IsNone())
        SourcePosture = SourceSurvival->EvaluatePostureImpact(Evaluation.SourcePostureReason, Request.AttackType, 0.f);
    if (!TargetPosture.bAccepted || !SourcePosture.bAccepted
        || !FMath::IsFinite(TargetPosture.PostureDamage) || TargetPosture.PostureDamage < 0.f
        || !FMath::IsFinite(SourcePosture.PostureDamage) || SourcePosture.PostureDamage < 0.f)
    { Result.RejectionReason = TEXT("InvalidPostureEvaluation"); return Result; }
    if (!bAvoided && Evaluation.TargetPostureReason.IsNone() && Request.PostureDamage > 0.f)
    { Result.RejectionReason = TEXT("MissingPostureRule"); return Result; }

    // 脚本入口虽然约定纯计算，提交前仍复验身份，避免脚本错误使迟到请求写入新生命。
    Result.RejectionReason = SourceCombat->ValidateCombatHitSource(Request);
    if (!Result.RejectionReason.IsNone()) return Result;
    if (!IsValid(TargetSurvival) || !IsValid(SourceSurvival) || !TargetSurvival->IsAlive()
        || TargetSurvival->GetLifeSerial() != TargetLifeSerial || ActionSerial != Result.TargetActionSerial || bCombatEndedPlay)
    { Result.RejectionReason = TEXT("TargetChangedDuringEvaluation"); return Result; }
    FSKCombatHitSourceState* State = SourceCombat->CombatHitSources.Find(Request.HitSourceSerial);
    if (!State) { Result.RejectionReason = TEXT("UnknownHitSource"); return Result; }
    State->ResolvedTargets.Add(Target);

    const float HealthDamage = bAvoided ? 0.f : Evaluation.HealthDamage;
    Result.Numeric.Code = ESKNumericResultCode::NoChange;
    Result.Numeric.Operation = ESKNumericOperation::SurvivalImpact;
    Result.Numeric.SourceActor = Source;
    Result.Numeric.TargetActor = Target;
    Result.Numeric.Before = TargetSurvival->GetSurvivalSnapshot().Attributes;
    Result.Numeric.After = Result.Numeric.Before;
    if (HealthDamage > 0.f || TargetPosture.PostureDamage > 0.f)
    {
        const FSKSurvivalImpactResult Impact = TargetSurvival->ApplySurvivalImpact(HealthDamage, TargetPosture.PostureDamage, Source);
        Result.Numeric = Impact.Numeric;
        Result.AppliedHealthDamage = Impact.Numeric.ActualHealthDamage;
        Result.AppliedPostureDamage = Impact.Numeric.ActualPostureDamage;
        Result.bKilled = Impact.bDeathStarted;
        Result.bPostureBroken = Impact.bPostureBroken;
        if (!IsCommittedCombatNumeric(Result.Numeric))
        { Result.RejectionReason = TEXT("ResourceCommitRejected"); return Result; }
    }

    Result.Code = ESKCombatHitResultCode::Committed;
    Result.RejectionReason = NAME_None;
    Result.SourceNumeric.Code = ESKNumericResultCode::NoChange;
    Result.SourceNumeric.Operation = ESKNumericOperation::SurvivalImpact;
    Result.SourceNumeric.SourceActor = Target;
    Result.SourceNumeric.TargetActor = Source;
    const bool bHasSourceFeedback = !bAvoided && !Evaluation.SourcePostureReason.IsNone();
    if (IsValid(SourceSurvival) && SourceSurvival->IsAlive() && SourceSurvival->GetLifeSerial() == Request.SourceLifeSerial)
    {
        Result.SourceNumeric.Before = SourceSurvival->GetSurvivalSnapshot().Attributes;
        Result.SourceNumeric.After = Result.SourceNumeric.Before;
        // 目标提交的委托可能改变攻方属性；反馈必须按提交时的新快照重新计算封顶。
        if (bHasSourceFeedback)
            SourcePosture = SourceSurvival->EvaluatePostureImpact(Evaluation.SourcePostureReason, Request.AttackType, 0.f);
        if (!IsValid(SourceSurvival) || !SourceSurvival->IsAlive() || SourceSurvival->GetLifeSerial() != Request.SourceLifeSerial)
        {
            Result.SourceNumeric.Code = ESKNumericResultCode::PolicyRejected;
            Result.SourceNumeric.RejectionReason = TEXT("SourceChangedDuringCommit");
        }
        else if (!SourcePosture.bAccepted || !FMath::IsFinite(SourcePosture.PostureDamage) || SourcePosture.PostureDamage < 0.f)
        {
            Result.SourceNumeric.Code = ESKNumericResultCode::InvalidInput;
            Result.SourceNumeric.RejectionReason = TEXT("InvalidPostureEvaluation");
        }
        else if (SourcePosture.PostureDamage > 0.f && IsValid(SourceSurvival)
            && SourceSurvival->IsAlive() && SourceSurvival->GetLifeSerial() == Request.SourceLifeSerial)
        {
            const FSKSurvivalImpactResult Feedback = SourceSurvival->ApplySurvivalImpact(0.f, SourcePosture.PostureDamage, Target);
            Result.SourceNumeric = Feedback.Numeric;
            Result.AppliedSourcePostureDamage = Feedback.Numeric.ActualPostureDamage;
            Result.bSourcePostureBroken = Feedback.bPostureBroken;
        }
        if (IsValid(SourceSurvival) && SourceSurvival->GetLifeSerial() == Request.SourceLifeSerial
            && !bAvoided && !Evaluation.SourcePostureReason.IsNone() && IsCommittedCombatNumeric(Result.SourceNumeric))
            SourceSurvival->HandlePostureImpactCommitted(Evaluation.SourcePostureReason);
    }
    else if (bHasSourceFeedback)
    {
        Result.SourceNumeric.Code = ESKNumericResultCode::PolicyRejected;
        Result.SourceNumeric.RejectionReason = TEXT("SourceChangedDuringCommit");
    }
    if (IsValid(TargetSurvival) && TargetSurvival->GetLifeSerial() == TargetLifeSerial
        && !bAvoided && !Evaluation.TargetPostureReason.IsNone())
        TargetSurvival->HandlePostureImpactCommitted(Evaluation.TargetPostureReason);

    PublishCombatHitEvents(Request, Result, SourceCombat);
    if (IsValid(this) && !bCombatEndedPlay && IsValid(TargetSurvival)
        && TargetSurvival->GetLifeSerial() == TargetLifeSerial) HandleCombatHitCommitted(Request, Result, false);
    if (IsValid(SourceCombat) && !SourceCombat->bCombatEndedPlay) SourceCombat->HandleCombatHitCommitted(Request, Result, true);
    return Result;
}

/**
 * 提供 Lua 战斗规则未绑定时的 AI 攻击安全回退。
 * 默认不解释请求名、不启动动画且不修改战斗状态；只能在游戏线程由行为树或玩法代码调用。
 *
 * @param AttackRequest 脚本层定义的抽象攻击请求名；默认实现不保存该名称。
 * @return 默认返回 false，表示没有脚本规则接管本次请求。
 */
bool USKCombatComponent::RequestAIAttack_Implementation(FName AttackRequest)
{
    (void)AttackRequest;
    return false;
}

/**
 * 停止 Owner 当前由 AIController 发起的路径跟随请求，防止攻击反应或架势崩坏期间继续导航滑行。
 * 玩家控制器和没有 Pawn Owner 的组件会安全忽略；函数不清理行为树、不改变战斗状态。
 * 只能在游戏线程调用。
 */
void USKCombatComponent::StopOwnerAIMovement()
{
    const APawn* OwnerPawn = Cast<APawn>(GetOwner());
    AAIController* AIController = OwnerPawn
        ? Cast<AAIController>(OwnerPawn->GetController())
        : nullptr;
    if (AIController) AIController->StopMovement();
}

/**
 * 发布一条通用 AI 战斗事实，并以组件本地顺序号和当前世界时间覆盖调用方提供的排序字段。
 * 本函数只传输事件，不写 Blackboard、不选择动作；队列满时淘汰最旧事件并且每个组件只警告一次。
 * 只能在游戏线程调用，Event 会被复制，函数不保留调用方引用。
 *
 * @param Event 待发布的通用事件；EventType 不能为 None，EventSerial 和 EventTimeSeconds 会被忽略。
 * @return 事件有效且获得新的正序列号并入队时返回 true；类型无效或序列号耗尽时返回 false。
 */
bool USKCombatComponent::PublishAICombatEvent(const FSKAICombatEvent& Event)
{
    if (Event.EventType == ESKAICombatEventType::None) return false;

    if (LastAICombatEventSerial == TNumericLimits<int32>::Max())
    {
        if (!bAICombatEventSerialExhaustedLogged)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("SKCombatComponent AI combat event serial exhausted. Owner=%s"),
                *GetNameSafe(GetOwner()));
            bAICombatEventSerialExhaustedLogged = true;
        }
        return false;
    }

    ++LastAICombatEventSerial;
    FSKAICombatEvent StoredEvent = Event;
    StoredEvent.EventSerial = LastAICombatEventSerial;
    StoredEvent.EventTimeSeconds = GetWorldTimeSeconds();

    if (PendingAICombatEvents.Num() >= AICombatEventQueueCapacity)
    {
        PendingAICombatEvents.RemoveAt(0);
        if (!bAICombatEventOverflowLogged)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("SKCombatComponent AI combat event queue overflowed; oldest event discarded. Owner=%s Capacity=%d"),
                *GetNameSafe(GetOwner()),
                AICombatEventQueueCapacity);
            bAICombatEventOverflowLogged = true;
        }
    }

    PendingAICombatEvents.Add(MoveTemp(StoredEvent));
    return true;
}

/**
 * 弹出等待时间最久的通用 AI 战斗事件，供 Owner 的 ReactionRouter 顺序收集当前批次。
 * 只能在游戏线程调用；成功时从队列永久移除该事件，不执行反应优先级判断。
 *
 * @param OutEvent 输出事件副本；队列为空时重置为安全默认值。
 * @return 成功弹出事件时返回 true，队列为空时返回 false。
 */
bool USKCombatComponent::ConsumeAICombatEvent(FSKAICombatEvent& OutEvent)
{
    OutEvent = FSKAICombatEvent();
    if (PendingAICombatEvents.IsEmpty()) return false;

    OutEvent = PendingAICombatEvents[0];
    PendingAICombatEvents.RemoveAt(0);
    return true;
}

/** 清空所有尚未消费的通用 AI 战斗事件；仅允许游戏线程调用，不重置事件序列号。 */
void USKCombatComponent::ClearAICombatEvents()
{
    PendingAICombatEvents.Reset();
}

/**
 * 查询当前等待 ReactionRouter 消费的通用 AI 战斗事件数量。
 * 仅允许游戏线程读取，不消费事件且不修改队列。
 *
 * @return 当前队列元素数量，范围为零到固定容量 32。
 */
int32 USKCombatComponent::GetPendingAICombatEventCount() const
{
    return PendingAICombatEvents.Num();
}

/**
 * 以组件 Owner 为射手生成并初始化一枚通用 AI 战斗弹射物，供 Lua 在语义动作窗口中调用。
 * 本函数只负责验证输入、生成 Actor 和传递当前 ActionSerial；不选择弹种、资产、目标或发射时机。
 * 只能在游戏线程调用；成功实例由 World 管理，OutProjectile 是非持有输出引用。
 *
 * @param ProjectileClass 要生成的通用弹射物类，必须有效；允许由蓝图子类配置外观和碰撞体。
 * @param SpawnLocation 弹射物出生世界位置，单位厘米；必须包含有限分量。
 * @param TargetActor 可选瞄准目标；有效时使用其当前世界位置，且优先于 TargetLocation。
 * @param TargetLocation TargetActor 无效时使用的世界瞄准位置，单位厘米；必须包含有限分量。
 * @param Speed 初始飞行速度，单位厘米每秒；必须为有限正数。
 * @param GravityScale ProjectileMovement 重力倍率；必须为有限数，可为零或负数。
 * @param Damage 发射时锁存的统一命中基础伤害；必须为有限非负数，Guard/Deflect 由目标 Lua 裁决。
 * @param LifeSeconds 弹射物自动销毁时间，单位秒；必须为有限正数。
 * @param EventTag 透传给 ProjectileImpact 的可选中性语义标签，不由 C++ 解释。
 * @param OutProjectile 成功时输出生成实例，失败时重置为空；调用方不获得生命周期所有权。
 * @return 输入、Owner、World 和生成流程全部有效时返回 true，否则返回 false。
 */
bool USKCombatComponent::SpawnAIBattleProjectile(
    TSubclassOf<ASKAIBattleProjectile> ProjectileClass,
    const FVector& SpawnLocation,
    AActor* TargetActor,
    const FVector& TargetLocation,
    float Speed,
    float GravityScale,
    float Damage,
    float LifeSeconds,
    FName EventTag,
    ASKAIBattleProjectile*& OutProjectile)
{
    OutProjectile = nullptr;

    AActor* ShooterActor = GetOwner();
    UWorld* World = GetWorld();
    const FVector ResolvedTargetLocation = IsValid(TargetActor)
        ? TargetActor->GetActorLocation()
        : TargetLocation;
    if (!ProjectileClass
        || !ShooterActor
        || !World
        || SpawnLocation.ContainsNaN()
        || ResolvedTargetLocation.ContainsNaN()
        || !FMath::IsFinite(Speed)
        || Speed <= UE_SMALL_NUMBER
        || !FMath::IsFinite(GravityScale)
        || !FMath::IsFinite(Damage)
        || Damage < 0.f
        || !FMath::IsFinite(LifeSeconds)
        || LifeSeconds <= 0.f
        || ResolvedTargetLocation.Equals(SpawnLocation, UE_SMALL_NUMBER))
    {
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = ShooterActor;
    SpawnParameters.Instigator = Cast<APawn>(ShooterActor);
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    const FRotator SpawnRotation = (ResolvedTargetLocation - SpawnLocation).Rotation();
    ASKAIBattleProjectile* Projectile = World->SpawnActor<ASKAIBattleProjectile>(
        ProjectileClass,
        SpawnLocation,
        SpawnRotation,
        SpawnParameters);
    if (!Projectile) return false;

    if (!Projectile->InitializeProjectile(
        ShooterActor,
        TargetActor,
        ResolvedTargetLocation,
        ActionSerial,
        EventTag,
        Damage,
        Speed,
        GravityScale,
        LifeSeconds))
    {
        Projectile->Destroy();
        return false;
    }

    OutProjectile = Projectile;
    return true;
}

/**
 * 查询当前战斗动作状态，不推进状态机。
 * 仅允许游戏线程读取，返回值由 Lua 或测试接口最近一次写入。
 *
 * @return 当前动作状态枚举。
 */
ESKCombatActionState USKCombatComponent::GetCombatActionState() const
{
    return CombatActionState;
}

/**
 * 写入供 AnimBlueprint 和 Lua 共享的动作状态，不播放或停止动画。
 * 仅允许游戏线程调用。
 *
 * @param NewState 要立即发布的新动作状态。
 */
void USKCombatComponent::SetCombatActionState(ESKCombatActionState NewState)
{
    CombatActionState = NewState;
}

/**
 * 查询 AnimGraph 应持续输出的基础战斗姿态，不推进 Lua 战斗状态机。
 * 该姿态独立于当前离散动作，因此攻击 Montage 覆盖期间仍可保留防御底层 Pose。
 * 仅允许游戏线程读取。
 *
 * @return Lua 最近发布的基础战斗姿态枚举。
 */
ESKCombatPostureState USKCombatComponent::GetCombatPostureState() const
{
    return CombatPostureState;
}

/**
 * 写入供 AnimBlueprint 消费的基础战斗姿态，不修改动作状态，也不播放或停止动画。
 * 仅允许游戏线程由 Lua 战斗状态机调用。
 *
 * @param NewState 要立即发布的新基础战斗姿态。
 */
void USKCombatComponent::SetCombatPostureState(ESKCombatPostureState NewState)
{
    CombatPostureState = NewState;
}

/** 查询当前离散动作已提交的攻击侧；仅允许游戏线程读取。 */
ESKAttackSide USKCombatComponent::GetCommittedAttackSide() const
{
    return CommittedAttackSide;
}

/** 写入当前离散动作稳定攻击侧；仅允许游戏线程调用，不自动修改下一侧。 */
void USKCombatComponent::SetCommittedAttackSide(ESKAttackSide NewSide)
{
    CommittedAttackSide = NewSide;
}

/** 查询后续动作候选攻击侧；仅允许游戏线程读取。 */
ESKAttackSide USKCombatComponent::GetNextAttackSide() const
{
    return NextAttackSide;
}

/** 写入后续动作候选攻击侧；仅允许游戏线程调用，不污染当前动作侧。 */
void USKCombatComponent::SetNextAttackSide(ESKAttackSide NewSide)
{
    NextAttackSide = NewSide;
}

/** 查询输入层最后发布的防御键按住状态；仅允许游戏线程读取。 */
bool USKCombatComponent::IsGuardHeld() const
{
    return bGuardHeld;
}

/**
 * 查询组件所属 Character 当前是否由 CharacterMovement 判定为 Falling。
 * 本函数只做游戏线程只读查询，不修改移动模式，也不推断落地事件。
 *
 * @return 所属角色及移动组件有效且当前处于 Falling 移动模式时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerFalling() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const UCharacterMovementComponent* MovementComponent =
        Character ? Character->GetCharacterMovement() : nullptr;
    return MovementComponent && MovementComponent->IsFalling();
}

/**
 * 查询所属角色移动组件当前发布的速度档位是否为 Sprint。
 * 本函数只提供瞬时事实，不推断战斗状态或决定架势能否恢复；只能在游戏线程读取。
 *
 * @return Owner 使用 USKMovementComponent 且当前档位为 Sprint 时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerSprinting() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const USKMovementComponent* MovementComponent = Character
        ? Cast<USKMovementComponent>(Character->GetCharacterMovement())
        : nullptr;
    return MovementComponent && MovementComponent->IsMovementTierSprint();
}

/**
 * 查询所属角色是否仍处于 Dodge/Step 活动窗口。
 * 同时读取输入组件活动窗口、角色原生 Dodge 快照和战斗动作状态，避免组件间一帧更新差导致误判；
 * 本函数不结束 Dodge、不推进状态机，只能在游戏线程读取。
 *
 * @return 任一现有 Dodge/Step 事实为活动状态时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerDodgingOrStepActive() const
{
    const ASKCharacter* Character = Cast<ASKCharacter>(GetOwner());
    const USKInputManager* InputManager = Character
        ? Character->FindComponentByClass<USKInputManager>()
        : nullptr;
    return CombatActionState == ESKCombatActionState::Dodging
        || (InputManager && InputManager->IsDodgeActive())
        || (Character && Character->IsDodging());
}

/** 查询当前动作序列号；仅允许游戏线程读取。 */
int32 USKCombatComponent::GetActionSerial() const
{
    return ActionSerial;
}

/**
 * 开始一个新的逻辑动作，递增动作序列号并发布状态。
 * 本函数不选择或播放资产；仅允许游戏线程由 Lua 状态机调用。
 *
 * @param NewState 新动作对 AnimBlueprint 可见的状态。
 * @return 新动作的正整数序列号；数值未就绪返回零，后续输入和结束回调必须用序列号校验。
 */
int32 USKCombatComponent::BeginCombatAction(ESKCombatActionState NewState)
{
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!IsInGameThread() || !IsCombatAttributesReady() || !Survival || !Survival->CanAct() || ActionSerial == MAX_int32) return 0;
    PresentationToken = FSKSurvivalTransitionToken();
    ++ActionSerial;
    CombatActionState = NewState;
    return ActionSerial;
}

/**
 * 游戏线程持有效生存过程令牌开启特殊演出，不能用于生命伤害或攻击碰撞。
 * @param Token 当前死亡/回生/崩溃身份，必须属于本 Owner 的 Survival。
 * @param NewState 崩溃只允许 PostureBroken；死亡/回生使用 AIReaction 通用受控演出状态。
 * @return 正动作序号表示授权成功；过期、跨角色或错误动作类型返回零。
 */
int32 USKCombatComponent::BeginSurvivalPresentation(
    const FSKSurvivalTransitionToken& Token, ESKCombatActionState NewState)
{
    if (!IsInGameThread()) return 0;
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!Survival || !Survival->IsTransitionTokenValid(Token) || ActionSerial == MAX_int32) return 0;
    const bool bBreak = Token.Kind == ESKSurvivalTransitionKind::PostureBreak;
    if ((bBreak && NewState != ESKCombatActionState::PostureBroken)
        || (!bBreak && NewState != ESKCombatActionState::AIReaction)) return 0;
    PresentationToken = Token;
    CombatActionState = NewState;
    return ++ActionSerial;
}

/**
 * 使指定当前动作立即失效并清除其输入候选，防止迟到回调推进状态。
 * ExpectedActionSerial 非零且与当前值不匹配时不做任何修改；本函数不停止 Montage。
 * 仅允许游戏线程调用。
 *
 * @param ExpectedActionSerial 调用方认为仍活动的序列号；零表示无条件失效当前动作。
 */
void USKCombatComponent::InvalidateCombatAction(int32 ExpectedActionSerial)
{
    if (ExpectedActionSerial != 0 && ExpectedActionSerial != ActionSerial) return;

    if (ActionSerial < MAX_int32) ++ActionSerial;
    PresentationToken = FSKSurvivalTransitionToken();
    PendingInputEvents.Reset();
    DeflectGuardInputSerial = 0;
    DeflectContextSerial = 0;
}

/**
 * 校验回调或候选是否仍属于当前动作。
 * 仅允许游戏线程读取，不修改序列号。
 *
 * @param ExpectedActionSerial 待验证的正整数动作序列号。
 * @return 与当前序列号完全相等且非零时返回 true。
 */
bool USKCombatComponent::IsActionSerialValid(int32 ExpectedActionSerial) const
{
    return ExpectedActionSerial > 0 && ExpectedActionSerial == ActionSerial;
}

/** 游戏线程查询 Owner 数值是否就绪；缺少 ASC 时返回 false，不触发初始化。 */
bool USKCombatComponent::IsCombatAttributesReady() const
{
    const USKAbilitySystemComponent* ASC = ResolveAttributeSystem();
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    return ASC && ASC->IsAttributesReady() && Survival && Survival->IsSurvivalReady();
}

/** 查询 GAS 当前架势值；仅允许游戏线程读取，缺少 ASC 时返回零，不推进恢复或打崩流程。 */
float USKCombatComponent::GetCurrentPosture() const
{
    const USKAbilitySystemComponent* ASC = ResolveAttributeSystem();
    return ASC ? ASC->GetAttributeSnapshot().Posture : 0.f;
}

/** 查询当前架势上限；仅允许游戏线程读取。 */
float USKCombatComponent::GetMaxPosture() const
{
    const USKAbilitySystemComponent* ASC = ResolveAttributeSystem();
    return ASC ? ASC->GetAttributeSnapshot().MaxPosture : 0.f;
}

/**
 * 查询当前架势比例，供 Lua 公式、动画和 UI 使用。
 * 当上限为零时稳定返回零，函数不修改任何状态；仅允许游戏线程读取。
 *
 * @return 限制在 [0, 1] 的架势比例，零上限时返回 0。
 */
float USKCombatComponent::GetPostureNormalized() const
{
    const float Maximum = GetMaxPosture();
    return Maximum > 0.f
        ? FMath::Clamp(GetCurrentPosture() / Maximum, 0.f, 1.f)
        : 0.f;
}

/** 查询当前是否处于架势打崩流程；仅允许游戏线程读取。 */
bool USKCombatComponent::IsPostureBroken() const
{
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    return Survival && Survival->IsPostureBroken();
}

/** 游戏线程返回 GAS 配置的每秒架势恢复点数；缺少 ASC 时返回零，不自行执行恢复。 */
float USKCombatComponent::GetPostureRecoveryRate() const
{
    const USKAbilitySystemComponent* ASC = ResolveAttributeSystem();
    return ASC ? ASC->GetAttributeSnapshot().PostureRecoveryRate : 0.f;
}

/**
 * 将一个稳定原因名的输入锁请求转发给同 Owner 的输入组件。
 * 本函数不解释原因、不持有锁副本；Reason 为 None 或输入组件缺失时安全忽略。
 * 只能在游戏线程调用。
 *
 * @param Reason 调用系统拥有的稳定锁原因，必须非 None。
 * @param bLocked true 添加该原因，false 仅移除该原因。
 */
void USKCombatComponent::SetOwnerExternalInputLock(FName Reason, bool bLocked)
{
    AActor* Owner = GetOwner();
    USKInputManager* InputManager = Owner ? Owner->FindComponentByClass<USKInputManager>() : nullptr;
    if (InputManager) InputManager->SetExternalInputLock(Reason, bLocked);
}

/**
 * 清除 Owner 输入组件中所有玩法意图，并同步丢弃本组件尚未裁决的战斗输入与 Guard Held 快照。
 * Look、Pause 和 Menu 由输入组件保留；本函数不增删外部锁原因，只能在游戏线程调用。
 */
void USKCombatComponent::ClearOwnerGameplayInputForScript()
{
    AActor* Owner = GetOwner();
    USKInputManager* InputManager = Owner ? Owner->FindComponentByClass<USKInputManager>() : nullptr;
    if (InputManager) InputManager->ClearAllGameplayInputForScript();

    PendingInputEvents.Reset();
    bGuardHeld = false;
}

/**
 * 可选清空 Owner 当前武器的单次攻击去重集合，再开启其 QueryOnly 攻击碰撞。
 * 本函数只桥接组件查找与物理碰撞接口，不判断动作状态或动画窗口；这些业务边界由 Lua 编排。
 * 只能在游戏线程调用，不缓存 WeaponManager 或武器引用。
 *
 * @param bResetHitActors true 表示开启前清空本轮已命中目标，false 保留现有去重集合。
 * @return Owner、WeaponManager 和当前武器均有效，且碰撞开启请求已提交时返回 true；否则返回 false。
 */
bool USKCombatComponent::ActivateOwnerWeaponHitbox(bool bResetHitActors)
{
    if (!IsInGameThread() || !IsCombatAttributesReady()) return false;
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!Survival || !Survival->CanAct()) return false;
    AActor* Owner = GetOwner();
    USKWeaponManagerComponent* WeaponManager =
        Owner ? Owner->FindComponentByClass<USKWeaponManagerComponent>() : nullptr;
    if (!WeaponManager) return false;
    if (bResetHitActors && !WeaponManager->ClearWeaponHitActors()) return false;
    return WeaponManager->ActivateWeaponHitbox();
}

/**
 * 关闭 Owner 当前武器的攻击碰撞，供 Lua 在窗口结束、动作切换或异常中断时统一收敛。
 * 本函数不清空已命中集合、不改变武器挂载或展示状态；只能在游戏线程调用且不缓存引用。
 *
 * @return Owner、WeaponManager 和当前武器均有效，且关闭请求已提交时返回 true；否则返回 false。
 */
bool USKCombatComponent::DeactivateOwnerWeaponHitbox()
{
    AActor* Owner = GetOwner();
    USKWeaponManagerComponent* WeaponManager =
        Owner ? Owner->FindComponentByClass<USKWeaponManagerComponent>() : nullptr;
    return WeaponManager && WeaponManager->DeactivateWeaponHitbox();
}

/**
 * 接收输入组件生成的不可变战斗输入事件，更新 Guard Held 快照并按发生顺序入队。
 * 队列最多保留 32 条，溢出时丢弃最旧事件；同时同步广播通知，但不执行动作裁决。
 * 仅允许游戏线程调用，InputEvent 在函数内复制，不保留调用方引用。
 *
 * @param InputEvent 已包含成对 InputSerial、绝对时间和释放时长的事件。
 */
void USKCombatComponent::SubmitCombatInputEvent(const FSKCombatInputEvent& InputEvent)
{
    if (!IsInGameThread()) return;
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!Survival || !Survival->CanAct()) return;
    if (InputEvent.Action == ESKCombatInputAction::Guard)
    {
        bGuardHeld = InputEvent.Phase == ESKCombatInputPhase::Started;
    }

    if (PendingInputEvents.Num() >= 32) PendingInputEvents.RemoveAt(0);
    PendingInputEvents.Add(InputEvent);
    OnCombatInputEvent.Broadcast(InputEvent);
}

/**
 * 弹出等待时间最久的输入事件，供 Lua 在战斗组件 Tick 中顺序裁决。
 * 仅允许游戏线程调用；成功时从队列永久移除该事件。
 *
 * @param OutInputEvent 输出事件副本；队列为空时重置为默认值。
 * @return 成功弹出事件时返回 true，队列为空时返回 false。
 */
bool USKCombatComponent::ConsumeCombatInputEvent(FSKCombatInputEvent& OutInputEvent)
{
    OutInputEvent = FSKCombatInputEvent();
    if (PendingInputEvents.IsEmpty()) return false;

    OutInputEvent = PendingInputEvents[0];
    PendingInputEvents.RemoveAt(0);
    return true;
}

/** 清空所有尚未消费的战斗输入事件；仅允许游戏线程调用，不改变 Held 状态。 */
void USKCombatComponent::ClearCombatInputEvents()
{
    PendingInputEvents.Reset();
}

/**
 * 在 CombatFullBodySlot 播放给定 Sequence，并由本组件独占所创建的动态 Montage。
 * 播放前必须取得 FullBody RootMotion Owner Token；竞争失败时不停止已有 Montage。
 * 开始前会停止本组件旧 Montage，但不会停止其他组件拥有的动画；不选择资产或编排连段。
 * 仅允许游戏线程调用，Animation 在播放期间由组件强引用持有。
 *
 * @param Animation 要播放的非空动画序列。
 * @param BlendInTime 淡入秒数，负值限制为零。
 * @param BlendOutTime 淡出秒数，负值限制为零。
 * @param PlayRate 播放倍率，绝对值过小时按 1 处理。
 * @param LoopCount 循环次数，小于一时限制为一。
 * @return 动画实例有效且动态 Montage 成功启动时返回 true，否则返回 false。
 */
bool USKCombatComponent::PlayCombatAnimation(
    UAnimSequence* Animation,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount)
{
    if (!IsInGameThread()) return false;
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!Survival || (!Survival->CanAct() && !Survival->IsTransitionTokenValid(PresentationToken))) return false;
    UAnimInstance* AnimInstance = ResolveAnimInstance();
    if (!Animation || !AnimInstance || CombatSlotName.IsNone()) return false;
    if (!AcquireCombatRootMotionOwnership()) return false;

    UAnimMontage* PreviousMontage = ActiveMontage;
    ClearOwnedAnimationState();
    if (PreviousMontage) AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), PreviousMontage);

    ActivePlayRate = FMath::Abs(PlayRate) <= UE_SMALL_NUMBER ? 1.f : PlayRate;
    UAnimMontage* DynamicMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
        Animation,
        CombatSlotName,
        FMath::Max(0.f, BlendInTime),
        FMath::Max(0.f, BlendOutTime),
        ActivePlayRate,
        FMath::Max(1, LoopCount));
    if (!DynamicMontage)
    {
        ReleaseCombatRootMotionOwnership();
        return false;
    }

    ActiveSequence = Animation;
    ActiveMontage = DynamicMontage;
    AnimationStartTimeSeconds = GetWorldTimeSeconds();

    FOnMontageEnded EndDelegate;
    EndDelegate.BindUObject(this, &USKCombatComponent::HandleCombatMontageEnded, ActionSerial);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, DynamicMontage);
    return true;
}

/**
 * 从 Lua 配置提供的软对象路径同步加载 UAnimSequence，并交给通用动态 Montage 播放接口。
 * 本函数不缓存路径、不硬编码资产名；加载和播放均只能在游戏线程执行。
 *
 * @param AnimationPath UAnimSequence 的完整对象路径，空字符串或类型不匹配时失败。
 * @param BlendInTime 淡入秒数，负值由通用播放接口限制为零。
 * @param BlendOutTime 淡出秒数，负值由通用播放接口限制为零。
 * @param PlayRate 播放倍率，绝对值过小时由通用播放接口按 1 处理。
 * @param LoopCount 循环次数，小于一时由通用播放接口限制为一。
 * @return 资产成功加载且动态 Montage 成功启动时返回 true，否则返回 false。
 */
bool USKCombatComponent::PlayCombatAnimationByPath(
    const FString& AnimationPath,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount)
{
    if (AnimationPath.IsEmpty()) return false;

    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *AnimationPath);
    return PlayCombatAnimation(Animation, BlendInTime, BlendOutTime, PlayRate, LoopCount);
}

/**
 * 淡出并停止本组件拥有的动态 Montage，并立即清除活动资产引用与 FullBody 令牌。
 * 本函数不停止其他系统的 Montage，旧结束回调因 Montage 身份校验而成为幂等空操作。
 * 仅允许游戏线程调用。
 *
 * @param BlendOutTime 淡出秒数，负值限制为零。
 */
void USKCombatComponent::StopCombatAnimation(float BlendOutTime)
{
    UAnimMontage* Montage = ActiveMontage;
    UAnimInstance* AnimInstance = ResolveAnimInstance();
    ClearOwnedAnimationState();
    ReleaseCombatRootMotionOwnership();
    if (Montage && AnimInstance) AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), Montage);
}

/** 查询本组件动态 Montage 是否仍在播放或混合；仅允许游戏线程读取。 */
bool USKCombatComponent::IsCombatAnimationPlaying() const
{
    const UAnimInstance* AnimInstance = ResolveAnimInstance();
    return ActiveMontage && AnimInstance && AnimInstance->Montage_IsPlaying(ActiveMontage);
}

/** 查询活动动态 Montage 的 Track 位置；无有效播放时返回零，仅允许游戏线程读取。 */
float USKCombatComponent::GetCombatAnimationPosition() const
{
    const UAnimInstance* AnimInstance = ResolveAnimInstance();
    return ActiveMontage && AnimInstance ? AnimInstance->Montage_GetPosition(ActiveMontage) : 0.f;
}

/** 查询活动 Montage Track 位置映射后的源 Sequence 本地时间；映射失败时返回零。 */
float USKCombatComponent::GetActiveSequencePosition() const
{
    float SequencePosition = 0.f;
    ResolveSequencePosition(GetCombatAnimationPosition(), SequencePosition);
    return SequencePosition;
}

/**
 * 在当前源 Sequence 本地时间直接采样指定曲线，避免读取最终混合 Pose 的残留曲线。
 * 曲线或活动 Sequence 不存在时返回零；仅允许游戏线程读取。
 *
 * @param CurveName Skeleton 中登记的曲线名，None 返回零。
 * @return 指定本地时间的曲线浮点值，查询失败时为零。
 */
float USKCombatComponent::SampleActiveSequenceCurve(FName CurveName) const
{
    return EvaluateSequenceCurve(CurveName, GetActiveSequencePosition());
}

/**
 * 按输入事件的世界绝对时间估算对应 Montage Track 位置，再映射到源 Sequence 采样曲线。
 * 本实现以当前 Montage 位置和世界时间差回溯，受游戏暂停和 Montage 非匀速改变影响时由 Serial 校验兜底；
 * 不推进动画且仅允许游戏线程读取。
 *
 * @param CurveName Skeleton 中登记的曲线名。
 * @param EventTimeSeconds 输入事件发生的游戏世界绝对秒数，可早于当前帧。
 * @return 事件时刻对应的曲线值；无活动动画或无法映射时返回零。
 */
float USKCombatComponent::SampleActiveSequenceCurveAtTime(FName CurveName, double EventTimeSeconds) const
{
    if (!ActiveMontage || !ActiveSequence) return 0.f;

    const double TimeOffset = GetWorldTimeSeconds() - EventTimeSeconds;
    const float EventMontagePosition = GetCombatAnimationPosition() - static_cast<float>(TimeOffset) * ActivePlayRate;
    float SequencePosition = 0.f;
    if (!ResolveSequencePosition(EventMontagePosition, SequencePosition)) return 0.f;
    return EvaluateSequenceCurve(CurveName, SequencePosition);
}

/** 查询战斗全身 Montage 是否处于活动播放或混合状态；仅允许游戏线程读取。 */
bool USKCombatComponent::IsCombatFullBodyActionActive() const
{
    return IsCombatAnimationPlaying();
}

/**
 * 创建或覆盖一个模拟来袭有效区间，用于无 AI、碰撞和伤害条件下测试弹反动画。
 * 新上下文递增 ContextSerial，ActiveDuration 负值限制为零；仅允许游戏线程调用。
 *
 * @param AttackType 由 Lua 映射到 Deflect Type 的抽象攻击类型。
 * @param ActiveDuration 从当前世界时间起算的有效秒数。
 * @return 新上下文的正整数序列号。
 */
int32 USKCombatComponent::BeginIncomingAttackAnimationTest(ESKIncomingAttackType AttackType, float ActiveDuration)
{
    ++ContextSerial;
    if (ContextSerial <= 0) ContextSerial = 1;

    const double Now = GetWorldTimeSeconds();
    IncomingAttackContext.AttackType = AttackType;
    IncomingAttackContext.ContextSerial = ContextSerial;
    IncomingAttackContext.ActiveStartTimeSeconds = Now;
    IncomingAttackContext.ActiveEndTimeSeconds = Now + FMath::Max(0.f, ActiveDuration);
    IncomingAttackContext.bConsumed = false;
    return ContextSerial;
}

/**
 * 清除当前模拟来袭；非零 ExpectedContextSerial 不匹配时忽略，防止旧测试回调清除新上下文。
 * 仅允许游戏线程调用，不中断当前弹反动画。
 *
 * @param ExpectedContextSerial 期望清除的上下文序列号；零表示无条件清除。
 */
void USKCombatComponent::ClearIncomingAttackAnimationTest(int32 ExpectedContextSerial)
{
    if (ExpectedContextSerial != 0 && ExpectedContextSerial != IncomingAttackContext.ContextSerial) return;
    IncomingAttackContext = FSKIncomingAttackAnimationContext();
}

/**
 * 判断给定绝对事件时间是否落在当前未消费来袭的闭区间内。
 * 仅允许游戏线程读取，不自动消费上下文。
 *
 * @param EventTimeSeconds Guard Started 事件的世界绝对秒数。
 * @return 上下文有效、未消费且事件时间位于起止边界内时返回 true。
 */
bool USKCombatComponent::IsIncomingAttackAnimationActiveAt(double EventTimeSeconds) const
{
    return IncomingAttackContext.ContextSerial > 0
        && !IncomingAttackContext.bConsumed
        && EventTimeSeconds >= IncomingAttackContext.ActiveStartTimeSeconds
        && EventTimeSeconds <= IncomingAttackContext.ActiveEndTimeSeconds;
}

/**
 * 复制当前模拟来袭上下文供 Lua 调试或裁决。
 * 仅允许游戏线程读取，不消费或延长上下文。
 *
 * @param OutContext 接收上下文副本；不存在时重置为默认值。
 * @return 存在非零 ContextSerial 时返回 true，否则返回 false。
 */
bool USKCombatComponent::GetIncomingAttackAnimationContext(FSKIncomingAttackAnimationContext& OutContext) const
{
    OutContext = IncomingAttackContext;
    return OutContext.ContextSerial > 0;
}

/**
 * 使用新的 Guard Started 输入一次性消费模拟来袭，并锁存弹反关联序列号。
 * 本函数只验证时序和序列号，不选择 Deflect 资产、不推进 Stage，也不播放动画。
 * 仅允许游戏线程调用。
 *
 * @param ExpectedContextSerial Lua 已读取并准备消费的上下文序列号。
 * @param GuardInputSerial 触发裁决的新 Guard Started 输入序列号，必须为正数。
 * @param EventTimeSeconds Guard Started 发生的世界绝对秒数。
 * @param OutAttackType 成功时输出抽象攻击类型；失败时重置为 Light。
 * @return 所有校验通过且上下文首次被消费时返回 true，否则返回 false。
 */
bool USKCombatComponent::TryConsumeIncomingAttackAnimation(
    int32 ExpectedContextSerial,
    int32 GuardInputSerial,
    double EventTimeSeconds,
    ESKIncomingAttackType& OutAttackType)
{
    OutAttackType = ESKIncomingAttackType::Light;
    if (GuardInputSerial <= 0
        || ExpectedContextSerial <= 0
        || ExpectedContextSerial != IncomingAttackContext.ContextSerial
        || !IsIncomingAttackAnimationActiveAt(EventTimeSeconds))
    {
        return false;
    }

    IncomingAttackContext.bConsumed = true;
    DeflectGuardInputSerial = GuardInputSerial;
    DeflectContextSerial = ExpectedContextSerial;
    OutAttackType = IncomingAttackContext.AttackType;
    return true;
}

/**
 * 监听 GAS 属性快照，建立组件 Tick 先后关系并为纯原生 UnLua 组件补发 ReceiveBeginPlay。
 * InputManager 作为前置 Tick，角色 Mesh 以本组件为前置，确保输入发布、战斗状态和动画采集有确定顺序。
 * 仅由 UE 在游戏线程生命周期调用。
 */
void USKCombatComponent::BeginPlay()
{
    if (USKAbilitySystemComponent* ASC = ResolveAttributeSystem())
    {
        ASC->OnAttributeChanged.AddUniqueDynamic(this, &USKCombatComponent::HandleGASAttributeChanged);
        ASC->OnAttributesReady.AddUniqueDynamic(this, &USKCombatComponent::HandleGASAttributesReady);
        if (ASC->IsAttributesReady()) BroadcastPostureChanged();
    }
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();

    AActor* Owner = GetOwner();

    ACharacter* Character = Cast<ACharacter>(Owner);
    if (Character)
    {
        if (UActorComponent* InputComponent = Character->FindComponentByClass<USKInputManager>())
        {
            AddTickPrerequisiteComponent(InputComponent);
        }
        if (USkeletalMeshComponent* Mesh = Character->GetMesh())
        {
            Mesh->AddTickPrerequisiteComponent(this);
        }
    }

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

/**
 * 在 Owner 离场前解除属性监听、使 Serial 和命中票据失效、停止自有 Montage 并清空事件状态。
 * 清理可重复调用，旧 Montage 回调因身份和 Serial 校验不会广播新动作结束事件。
 * 仅由 UE 在游戏线程生命周期调用。
 *
 * @param EndPlayReason UE 提供的离场原因，仅透传给父类。
 */
void USKCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (USKAbilitySystemComponent* ASC = ResolveAttributeSystem())
    {
        ASC->OnAttributeChanged.RemoveDynamic(this, &USKCombatComponent::HandleGASAttributeChanged);
        ASC->OnAttributesReady.RemoveDynamic(this, &USKCombatComponent::HandleGASAttributesReady);
    }
    bHasBroadcastPosture = false;
    bCombatEndedPlay = true;
    CombatHitSources.Reset();

    InvalidateCombatAction(0);
    StopCombatAnimation(0.f);
    ClearAICombatEvents();
    PendingInputEvents.Reset();
    IncomingAttackContext = FSKIncomingAttackAnimationContext();
    bGuardHeld = false;
    CombatActionState = ESKCombatActionState::Neutral;
    CombatPostureState = ESKCombatPostureState::Normal;
    Super::EndPlay(EndPlayReason);
}

/**
 * 显式调用 Lua 模块导出的 Tick，使状态机先消费输入，再收敛失去活动 Montage 的 FullBody 所有权，
 * 最后清理已过期且未消费的模拟来袭。本函数不裁决输入或选择动画；合法播放与 Blend Out 期间保留令牌。
 * 仅由 UE 在游戏线程 PrePhysics 阶段调用。
 *
 * @param DeltaTime 本帧组件步长，当前实现不参与区间计算。
 * @param TickType UE Tick 类型，仅透传父类。
 * @param ThisTickFunction 当前 Tick 函数，仅透传父类，可为空。
 */
void USKCombatComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    HandleCombatTick(DeltaTime);

    // FullBody 所有权只能跟随本组件仍处于 Active 状态的动态 Montage；结束回调遗漏时主动收敛，
    // 避免仍存活的 CombatComponent 让 Locomotion 永久停在 ActionOwned。Blend Out 仍属于 Active，
    // 因此不会在动作姿势尚未退出时提前把 Root Motion 归还给 Motion Matching。
    UAnimInstance* AnimInstance = ResolveAnimInstance();
    const bool bOwnedMontageActive = ActiveMontage
        && AnimInstance
        && AnimInstance->Montage_IsActive(ActiveMontage);
    if (ActiveRootMotionOwnerToken.Serial > 0 && !bOwnedMontageActive)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("SKCombatComponent released stale FullBody RootMotion ownership. Owner=%s"),
            *GetNameSafe(GetOwner()));
        ReleaseCombatRootMotionOwnership();
    }

    if (IncomingAttackContext.ContextSerial > 0
        && !IncomingAttackContext.bConsumed
        && GetWorldTimeSeconds() > IncomingAttackContext.ActiveEndTimeSeconds)
    {
        IncomingAttackContext = FSKIncomingAttackAnimationContext();
    }
}

/** 游戏线程查询同 Owner 的数值 ASC；不存在时返回空，不缓存所有权或创建组件。 */
USKAbilitySystemComponent* USKCombatComponent::ResolveAttributeSystem() const
{
    AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<USKAbilitySystemComponent>() : nullptr;
}

/**
 * 游戏线程将架势相关 GAS 属性变化桥接到旧通知，统一快照去重。
 * @param Attribute 已更新的 GAS 属性，非架势属性忽略。
 * @param OldValue 修改前值，仅供 GAS 通知签名使用，不反推资源。
 * @param NewValue 修改后值，仅供 GAS 通知签名使用，快照从 ASC 统一查询。
 */
void USKCombatComponent::HandleGASAttributeChanged(FGameplayAttribute Attribute, float OldValue, float NewValue)
{
    if (Attribute == USKCharacterAttributeSet::GetPostureAttribute()
        || Attribute == USKCharacterAttributeSet::GetMaxPostureAttribute()) BroadcastPostureChanged();
}

/** 游戏线程在初始属性完整提交后发布首个架势快照，不修改任何属性。 */
void USKCombatComponent::HandleGASAttributesReady()
{
    BroadcastPostureChanged();
}

/**
 * 游戏线程只读验证源组件签发记录、生命/动作和逐目标去重，不消费请求。
 * @param Request 待核对来源身份、锁存数值和目标；所有字段必须与签发记录一致。
 * @return None 表示仍可提交；其他名称明确指出失效、伪造或重复原因。
 */
FName USKCombatComponent::ValidateCombatHitSource(const FSKCombatHitRequest& Request) const
{
    if (bCombatEndedPlay || Request.SourceActor != GetOwner()) return TEXT("InvalidSource");
    const USKSurvivalComponent* Survival = GetOwner() ? GetOwner()->FindComponentByClass<USKSurvivalComponent>() : nullptr;
    if (!IsValid(Survival) || !Survival->IsAlive()) return TEXT("SourceNotAlive");
    if (Request.SourceLifeSerial <= 0 || Survival->GetLifeSerial() != Request.SourceLifeSerial) return TEXT("StaleSourceLife");
    const FSKCombatHitSourceState* State = CombatHitSources.Find(Request.HitSourceSerial);
    if (!State || !State->Emitter.IsValid()) return TEXT("UnknownHitSource");
    if (State->Emitter->GetOwner() != GetOwner() || State->LifeSerial != Request.SourceLifeSerial
        || State->ActionSerial != Request.SourceActionSerial || State->Channel != Request.DamageChannel
        || State->AttackType != Request.AttackType || State->HealthDamage != Request.HealthDamage
        || State->PostureDamage != Request.PostureDamage) return TEXT("HitSourceMismatch");
    if (Request.SourceActionSerial <= 0 || (State->Channel == ESKCombatDamageChannel::Melee
        && !IsActionSerialValid(Request.SourceActionSerial))) return TEXT("StaleSourceAction");
    if (State->Channel == ESKCombatDamageChannel::Melee && !Survival->CanAct()) return TEXT("SourceCannotAct");
    if (State->ResolvedTargets.Contains(Request.TargetActor)) return TEXT("DuplicateHit");
    return NAME_None;
}

/**
 * 游戏线程仅在权威提交完成后向攻守队列发布一次接触及真实伤害/崩溃事实，不写数值或 Blackboard。
 * @param Request 已完成结算的只读来源身份和几何上下文。
 * @param Result Committed 结果；真实量为零时不发布 DamageReceived。
 * @param SourceCombat 可在同步死亡通知中失效的攻方组件，仅有效且未离场时发布。
 */
void USKCombatComponent::PublishCombatHitEvents(const FSKCombatHitRequest& Request,
    const FSKCombatHitResult& Result, USKCombatComponent* SourceCombat)
{
    if (Result.Code != ESKCombatHitResultCode::Committed) return;
    FSKAICombatEvent Event;
    Event.EventType = Request.DamageChannel == ESKCombatDamageChannel::Projectile
        ? ESKAICombatEventType::ProjectileImpact : ESKAICombatEventType::WeaponContact;
    Event.SourceActor = Request.SourceActor;
    Event.TargetActor = Request.TargetActor;
    Event.RelatedActionSerial = Request.SourceActionSerial;
    Event.AttackType = Request.AttackType;
    Event.EventTag = Request.EventTag;
    Event.HitSourceSerial = Request.HitSourceSerial;
    Event.HitOutcome = Result.Outcome;
    Event.bKilled = Result.bKilled;
    Event.bPostureBroken = Result.bPostureBroken;
    Event.Magnitude = Result.AppliedHealthDamage;
    if (Result.Outcome == ESKCombatHitOutcome::Hit) Event.ContactResult = ESKWeaponContactResult::Hit;
    else if (Result.Outcome == ESKCombatHitOutcome::Guarded) Event.ContactResult = ESKWeaponContactResult::Guarded;
    else if (Result.Outcome == ESKCombatHitOutcome::Deflected) Event.ContactResult = ESKWeaponContactResult::Deflected;
    if (Request.DamageChannel == ESKCombatDamageChannel::Melee && IsValid(SourceCombat)
        && !SourceCombat->bCombatEndedPlay) SourceCombat->PublishAICombatEvent(Event);
    if (!bCombatEndedPlay) PublishAICombatEvent(Event);
    if (Result.AppliedHealthDamage > 0.f && !bCombatEndedPlay)
    {
        Event.EventType = ESKAICombatEventType::DamageReceived;
        PublishAICombatEvent(Event);
    }
    if (Result.bPostureBroken && !bCombatEndedPlay)
    {
        Event.EventType = ESKAICombatEventType::ReactionRequested;
        Event.Magnitude = Result.AppliedPostureDamage;
        PublishAICombatEvent(Event);
    }
    if (Result.bSourcePostureBroken && IsValid(SourceCombat) && !SourceCombat->bCombatEndedPlay)
    {
        Event.EventType = ESKAICombatEventType::ReactionRequested;
        Event.SourceActor = Request.TargetActor;
        Event.TargetActor = Request.SourceActor;
        Event.bKilled = false;
        Event.bPostureBroken = true;
        Event.Magnitude = Result.AppliedSourcePostureDamage;
        SourceCombat->PublishAICombatEvent(Event);
    }
}

/**
 * 解析 Owner 角色 Mesh 当前动画实例，不创建或切换 AnimBlueprint。
 * 仅允许游戏线程读取 UObject 状态。
 *
 * @return 有效角色 Mesh 的 AnimInstance；任一环节缺失时返回 nullptr。
 */
UAnimInstance* USKCombatComponent::ResolveAnimInstance() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
    return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

/**
 * 为 CombatFullBodySlot 在当前角色 Movement 权威上申请排他 FullBody 令牌。
 * 仅允许游戏线程在 Montage 启动前调用；重复启动会校验已保存令牌，失效时先尝试释放。
 * 非 USKMovementComponent 角色没有该协调边界，为保留 Classic 和组件测试兼容性视为无需令牌。
 *
 * @return 令牌已有效、新申请成功或无需令牌时返回 true；排他通道忙或申请失败时返回 false。
 */
bool USKCombatComponent::AcquireCombatRootMotionOwnership()
{
    if (!IsInGameThread()) return false;

    USKMovementComponent* SavedAuthority = ActiveRootMotionOwnerToken.Authority.Get();
    if (SavedAuthority && SavedAuthority->IsRootMotionOwnerTokenValid(ActiveRootMotionOwnerToken))
        return true;
    ReleaseCombatRootMotionOwnership();

    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    USKMovementComponent* MovementComponent = Character
        ? Cast<USKMovementComponent>(Character->GetCharacterMovement())
        : nullptr;
    if (!MovementComponent) return true;

    const FSKRootMotionOwnershipResult Result = MovementComponent->AcquireRootMotionOwnership(
        this,
        ESKRootMotionOwnerType::FullBody);
    if (Result.Code != ESKRootMotionOwnershipResultCode::Acquired
        && Result.Code != ESKRootMotionOwnershipResultCode::AlreadyOwned) return false;

    ActiveRootMotionOwnerToken = Result.Token;
    return true;
}

/**
 * 释放本组件保存的 FullBody 令牌并无条件清空本地副本。
 * 仅允许游戏线程在播放失败、显式停止、Montage 结束或 EndPlay 路径调用；
 * Authority 已销毁或令牌过期时只清理本地状态，不释放任何其他申请者的令牌。
 */
void USKCombatComponent::ReleaseCombatRootMotionOwnership()
{
    USKMovementComponent* MovementComponent = ActiveRootMotionOwnerToken.Authority.Get();
    if (MovementComponent)
        MovementComponent->ReleaseRootMotionOwnership(ActiveRootMotionOwnerToken);
    ActiveRootMotionOwnerToken = FSKRootMotionOwnerToken();
}

/**
 * 使用动态 Montage 的活动 FAnimSegment 将 Track 时间转换为源 Sequence 本地时间。
 * 只接受当前组件持有的 Sequence，超出所有 Segment 范围时失败；仅允许游戏线程读取。
 *
 * @param MontagePosition 动态 Montage Track 秒数。
 * @param OutSequencePosition 成功时输出源 Sequence 本地秒数，失败时为零。
 * @return 找到匹配 Segment 且其动画引用等于 ActiveSequence 时返回 true。
 */
bool USKCombatComponent::ResolveSequencePosition(float MontagePosition, float& OutSequencePosition) const
{
    OutSequencePosition = 0.f;
    if (!ActiveMontage || !ActiveSequence) return false;

    for (const FSlotAnimationTrack& SlotTrack : ActiveMontage->SlotAnimTracks)
    {
        for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
        {
            if (Segment.GetAnimReference() == ActiveSequence && Segment.IsInRange(MontagePosition))
            {
                OutSequencePosition = Segment.ConvertTrackPosToAnimPos(MontagePosition);
                return true;
            }
        }
    }
    return false;
}

/**
 * 通过 ActiveSequence Skeleton 的 SmartName UID 在指定本地时间直接求曲线值。
 * 本函数不读取最终混合 Pose；仅允许游戏线程读取已加载动画资产。
 *
 * @param CurveName 要查询的曲线稳定名称，None 视为失败。
 * @param SequencePosition 源 Sequence 本地秒数，函数会限制到资产播放范围。
 * @return 曲线存在时的浮点值，否则为零。
 */
float USKCombatComponent::EvaluateSequenceCurve(FName CurveName, float SequencePosition) const
{
    if (!ActiveSequence || CurveName.IsNone()) return 0.f;

#if UE_VERSION_NEWER_THAN(5, 7, 0)
    const float ClampedPosition = FMath::Clamp(SequencePosition, 0.f, ActiveSequence->GetPlayLength());
    return ActiveSequence->EvaluateCurveData(CurveName, FAnimExtractContext(static_cast<double>(ClampedPosition)));
#else
    const USkeleton* Skeleton = ActiveSequence->GetSkeleton();
    if (!Skeleton) return 0.f;
    FSmartName SmartCurveName;
    if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, SmartCurveName)) return 0.f;

    const float ClampedPosition = FMath::Clamp(SequencePosition, 0.f, ActiveSequence->GetPlayLength());
    return ActiveSequence->EvaluateCurveData(SmartCurveName.UID, ClampedPosition);
#endif
}

/**
 * 接收动态 Montage 结束通知。Montage 身份仍属于本组件时始终清理资产与 FullBody 令牌；
 * 只有启动时 ActionSerial 仍匹配才广播，防止已失效动作的迟到回调推进新状态。
 * 清理在广播前完成，允许监听方安全开始下一 Montage；仅由 UE 在游戏线程调用。
 *
 * @param Montage 已结束的动态 Montage，可为空。
 * @param bInterrupted 是否由停止或抢占导致中断。
 * @param EndedActionSerial 绑定委托时锁存的动作序列号。
 */
void USKCombatComponent::HandleCombatMontageEnded(
    UAnimMontage* Montage,
    bool bInterrupted,
    int32 EndedActionSerial)
{
    if (!Montage || Montage != ActiveMontage) return;

    const bool bCurrentAction = EndedActionSerial == ActionSerial;
    ClearOwnedAnimationState();
    ReleaseCombatRootMotionOwnership();
    if (bCurrentAction) OnCombatAnimationEnded.Broadcast(EndedActionSerial, bInterrupted);
}

/** 清除本组件持有的活动动画引用和播放时间快照；仅允许游戏线程调用，不停止 Montage。 */
void USKCombatComponent::ClearOwnedAnimationState()
{
    ActiveSequence = nullptr;
    ActiveMontage = nullptr;
    AnimationStartTimeSeconds = 0.0;
    ActivePlayRate = 1.f;
}

/** 查询当前游戏世界绝对秒数；组件尚无 World 时返回零，仅允许游戏线程读取。 */
double USKCombatComponent::GetWorldTimeSeconds() const
{
    const UWorld* World = GetWorld();
    return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

/**
 * 广播当前架势值、上限和安全归一化比例的同帧一致快照。
 * 仅由已确认状态发生变化的写接口在游戏线程调用，不进行额外去重或状态修改。
 */
void USKCombatComponent::BroadcastPostureChanged()
{
    const float Current = GetCurrentPosture();
    const float Maximum = GetMaxPosture();
    if (bHasBroadcastPosture && Current == LastBroadcastPosture && Maximum == LastBroadcastMaxPosture) return;
    bHasBroadcastPosture = true;
    LastBroadcastPosture = Current;
    LastBroadcastMaxPosture = Maximum;
    OnPostureChanged.Broadcast(Current, Maximum, GetPostureNormalized());
}
