#include "AbilitySystem/SKNumericGameplayEffect.h"
#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"

/** 在 UObject 构造线程创建无业务数值的即时执行模板，不加载资产。 */
USKNumericGameplayEffect::USKNumericGameplayEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayEffectExecutionDefinition Execution;
    Execution.CalculationClass = USKNumericExecution::StaticClass();
    Executions.Add(Execution);
}

/**
 * 游戏线程将 ASC 已验证的 SetByCaller 请求转换为 GAS 属性修改器。
 * 不读取护甲或玩法状态；伤害输入必须已由调用方完成裁决。
 * @param Parameters 本次效果执行上下文及 SetByCaller 参数，不保留引用。
 * @param Output 输出有序属性修改器；缺少本系统 ASC 或授权执行窗口时不输出。
 */
void USKNumericExecution::Execute_Implementation(
    const FGameplayEffectCustomExecutionParameters& Parameters,
    FGameplayEffectCustomExecutionOutput& Output) const
{
    USKAbilitySystemComponent* ASC = Cast<USKAbilitySystemComponent>(Parameters.GetTargetAbilitySystemComponent());
    if (!ASC || !ASC->bApplyingNumericEffect) return;

    const FGameplayEffectSpec& Spec = Parameters.GetOwningSpec();
    const float OperationValue = Spec.GetSetByCallerMagnitude(FName(TEXT("Operation")), false, -1.f);
    const float Amount = Spec.GetSetByCallerMagnitude(FName(TEXT("Amount")), false, 0.f);
    if (!FMath::IsFinite(OperationValue) || OperationValue < 0.f
        || OperationValue > static_cast<float>(ESKNumericOperation::RestoreResources)
        || FMath::FloorToFloat(OperationValue) != OperationValue || !FMath::IsFinite(Amount) || Amount < 0.f) return;
    const ESKNumericOperation Operation = static_cast<ESKNumericOperation>(static_cast<uint8>(OperationValue));
    if (Operation == ESKNumericOperation::Initialize)
    {
        for (const FGameplayAttribute& Attribute : USKCharacterAttributeSet::GetConfigAttributes())
        {
            Output.AddOutputModifier(FGameplayModifierEvaluatedData(
                Attribute, EGameplayModOp::Override,
                Spec.GetSetByCallerMagnitude(FName(*Attribute.GetName()), false, 0.f)));
        }
    }
    else if (Operation == ESKNumericOperation::InitializeResources)
    {
        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetHealthAttribute(),
            EGameplayModOp::Override, Spec.GetSetByCallerMagnitude(FName(TEXT("InitialHealth")), false, 0.f)));
        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetPostureAttribute(),
            EGameplayModOp::Override, Spec.GetSetByCallerMagnitude(FName(TEXT("InitialPosture")), false, 0.f)));
    }
    else if (Operation == ESKNumericOperation::SurvivalImpact)
    {
        const float HealthAmount = Spec.GetSetByCallerMagnitude(FName(TEXT("HealthAmount")), false, -1.f);
        const float PostureAmount = Spec.GetSetByCallerMagnitude(FName(TEXT("PostureAmount")), false, -1.f);
        if (!FMath::IsFinite(HealthAmount) || HealthAmount < 0.f
            || !FMath::IsFinite(PostureAmount) || PostureAmount < 0.f) return;

        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetIncomingDamageAttribute(),
            EGameplayModOp::Additive, HealthAmount));
        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetIncomingPostureDamageAttribute(),
            EGameplayModOp::Additive, PostureAmount));
    }
    else if (Operation == ESKNumericOperation::RestoreResources)
    {
        const float TargetHealth = Spec.GetSetByCallerMagnitude(FName(TEXT("TargetHealth")), false, -1.f);
        const float TargetPosture = Spec.GetSetByCallerMagnitude(FName(TEXT("TargetPosture")), false, -1.f);
        if (!FMath::IsFinite(TargetHealth) || TargetHealth < 0.f
            || !FMath::IsFinite(TargetPosture) || TargetPosture < 0.f) return;

        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetHealthAttribute(),
            EGameplayModOp::Override, TargetHealth));
        Output.AddOutputModifier(FGameplayModifierEvaluatedData(USKCharacterAttributeSet::GetPostureAttribute(),
            EGameplayModOp::Override, TargetPosture));
    }
    else
    {
        FGameplayAttribute Attribute;
        switch (Operation)
        {
        case ESKNumericOperation::Damage:
            Attribute = USKCharacterAttributeSet::GetIncomingDamageAttribute();
            break;
        case ESKNumericOperation::Healing:
            Attribute = USKCharacterAttributeSet::GetIncomingHealingAttribute();
            break;
        case ESKNumericOperation::PostureDamage:
            Attribute = USKCharacterAttributeSet::GetIncomingPostureDamageAttribute();
            break;
        case ESKNumericOperation::PostureRecovery:
        case ESKNumericOperation::ResetPosture:
            Attribute = USKCharacterAttributeSet::GetIncomingPostureRecoveryAttribute();
            break;
        default:
            return;
        }
        Output.AddOutputModifier(FGameplayModifierEvaluatedData(Attribute, EGameplayModOp::Additive, Amount));
    }
    ASC->bNumericExecutionObserved = true;
}
