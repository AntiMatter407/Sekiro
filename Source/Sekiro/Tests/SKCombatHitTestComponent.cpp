#include "Tests/SKCombatHitTestComponent.h"

/** 游戏线程返回空模块，隔离测试不绑定项目 Lua，也不执行项目初始化和演出。 */
FString USKCombatHitTestComponent::GetModuleName_Implementation() const
{
    return FString();
}

/**
 * 游戏线程为显式测试提供确定性的纯生命伤害规则，不写资源或播放演出。
 * @param Request 测试签发的接触事实；Hit 使用其基础伤害，其他结果为零。
 * @return 测试配置的判定；缺失规则分支返回原生的拒绝值。
 */
FSKCombatHitEvaluation USKCombatHitTestComponent::EvaluateCombatHit_Implementation(const FSKCombatHitRequest& Request) const
{
    if (bUseMissingRules) return Super::EvaluateCombatHit_Implementation(Request);
    if (bProbeReentrant)
        ReentrantReason = const_cast<USKCombatHitTestComponent*>(this)->ResolveCombatHit(Request).RejectionReason;
    FSKCombatHitEvaluation Result;
    Result.bAccepted = true;
    Result.Outcome = TestOutcome;
    Result.HealthDamage = TestOutcome == ESKCombatHitOutcome::Hit ? Request.HealthDamage : 0.f;
    if (bApplyPosture) Result.TargetPostureReason = TEXT("ContractTest");
    return Result;
}

/** 游戏线程返回空模块，测试使用原生机械流程，不装载项目 Survival Lua。 */
FString USKCombatHitTestSurvivalComponent::GetModuleName_Implementation() const
{
    return FString();
}

/**
 * 游戏线程纯返回测试注入的额外姿态，不访问具体攻防公式，不提交数值。
 * @param Reason 测试提供的中性语义，故意不解释项目规则。
 * @param AttackType 测试抽象类型，不影响机械提交验证。
 * @param AdditionalDamage 待验证复合提交的姿态点数。
 * @return 明确接受且返回 AdditionalDamage；输入有效性由生产 resolver 校验。
 */
FSKPostureImpactEvaluation USKCombatHitTestSurvivalComponent::EvaluatePostureImpact_Implementation(
    FName Reason, ESKIncomingAttackType AttackType, float AdditionalDamage) const
{
    (void)Reason;
    (void)AttackType;
    FSKPostureImpactEvaluation Result;
    Result.bAccepted = true;
    Result.PostureDamage = AdditionalDamage;
    return Result;
}
