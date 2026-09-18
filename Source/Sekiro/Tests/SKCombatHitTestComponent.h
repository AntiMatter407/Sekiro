#pragma once

#include "Combat/SKCombatComponent.h"
#include "Character/SKSurvivalComponent.h"
#include "SKCombatHitTestComponent.generated.h"

/** 仅供显式命中契约自动化测试注入确定性的纯规则，不被项目角色使用。 */
UCLASS(Transient, NotBlueprintable)
class USKCombatHitTestComponent : public USKCombatComponent
{
    GENERATED_BODY()

public:
    virtual FString GetModuleName_Implementation() const override;
    virtual FSKCombatHitEvaluation EvaluateCombatHit_Implementation(const FSKCombatHitRequest& Request) const override;

    bool bUseMissingRules = false; // 验证未绑定 Lua 的失败关闭
    bool bProbeReentrant = false; // 验证裁决期间同步重入拒绝
    bool bApplyPosture = false; // 验证目标 Health/Posture 使用同一复合提交
    ESKCombatHitOutcome TestOutcome = ESKCombatHitOutcome::Hit; // 测试注入的接触结果
    mutable FName ReentrantReason = NAME_None; // 测试只读裁决探测结果
};

/** 仅验证数值提交协议，真实躯干公式由 Lua 独立覆盖。 */
UCLASS(Transient, NotBlueprintable)
class USKCombatHitTestSurvivalComponent : public USKSurvivalComponent
{
    GENERATED_BODY()

public:
    virtual FString GetModuleName_Implementation() const override;
    virtual FSKPostureImpactEvaluation EvaluatePostureImpact_Implementation(
        FName Reason, ESKIncomingAttackType AttackType, float AdditionalDamage) const override;
};
