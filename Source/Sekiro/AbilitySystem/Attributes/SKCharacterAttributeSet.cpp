#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"
#include "GameplayEffectExtension.h"
#include <cmath>

/**
 * 游戏线程返回允许持续 GE 修改和通知观察的全部非资源角色属性。
 * 返回值不包含 Health、Posture 或临时提交属性；数组独立拥有属性标识，不保存组件引用。
 * @return 生命上限、战斗统计和躯干配置属性集合，顺序稳定且无重复。
 */
TArray<FGameplayAttribute> USKCharacterAttributeSet::GetConfigAttributes()
{
    return {
        GetMaxHealthAttribute(),
        GetAttackPowerAttribute(),
        GetArmorAttribute(),
        GetMaxPostureAttribute(),
        GetPostureRecoveryRateAttribute(),
        GetPostureRecoveryDelayAttribute(),
        GetPostureRecoveryRampDurationAttribute(),
        GetPostureRecoveryMinRateScaleAttribute(),
        GetPostureRecoveryMaxRateScaleAttribute(),
        GetPostureDeflectSuccessCapRatioAttribute(),
        GetPostureAttackCapRatioAttribute(),
        GetPostureMinGainScaleAttribute(),
        GetPostureGainFalloffExponentAttribute(),
        GetPostureGainDeflectSuccessAttribute(),
        GetPostureGainGuardedAttribute(),
        GetPostureGainDeflectFailedAttribute(),
        GetPostureGainAttackSuccessAttribute(),
        GetPostureGainAttackGuardedAttribute(),
        GetPostureGainAttackDeflectedAttribute(),
        GetPostureStrengthLightAttribute(),
        GetPostureStrengthHeavyAttribute(),
        GetPostureStrengthThrustAttribute(),
        GetPostureStrengthSpecialAttribute(),
        GetPostureBreakMinimumDurationAttribute(),
        GetPostureBreakBlendInTimeAttribute(),
        GetPostureBreakBlendOutTimeAttribute(),
        GetPostureRecoveryTargetRatioAttribute(),
        GetRevivePostureRatioAttribute()
    };
}

/**
 * 在游戏线程约束聚合后的属性，非法值保留现值；不解释伤害来源。
 * @param Attribute 即将更新的属性。
 * @param NewValue 输入候选值，输出满足范围的值。
 */
void USKCharacterAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);
    ClampAttribute(Attribute, NewValue);
}

/**
 * 在游戏线程约束基础值，确保即时/周期效果不能留下越界 BaseValue。
 * @param Attribute 即将更新的属性。
 * @param NewValue 输入候选基础值，输出经过限制的基础值。
 */
void USKCharacterAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
    Super::PreAttributeBaseChange(Attribute, NewValue);
    ClampAttribute(Attribute, NewValue);
}

/**
 * 上限下降时在游戏线程实际截断资源基础值，避免移除增益后隐性恢复。
 * @param Attribute 已更新属性。
 * @param OldValue 更新前的聚合值。
 * @param NewValue 更新后的聚合值。
 */
void USKCharacterAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
    Super::PostAttributeChange(Attribute, OldValue, NewValue);
    if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue)
    {
        SetHealth(FMath::Max(0.f, NewValue));
    }
    else if (Attribute == GetMaxPostureAttribute() && GetPosture() > NewValue)
    {
        SetPosture(FMath::Max(0.f, NewValue));
    }
}

/**
 * 在游戏线程拒绝非有限执行量，以及负数临时伤害/恢复量。
 * @param Data GAS 本次修改数据；不持有外部引用。
 * @return true 允许提交；false 拒绝本条修改器。
 */
bool USKCharacterAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
    if (!Super::PreGameplayEffectExecute(Data) || !FMath::IsFinite(Data.EvaluatedData.Magnitude)) return false;
    const FGameplayAttribute& Attribute = Data.EvaluatedData.Attribute;
    if (Attribute == GetIncomingDamageAttribute() || Attribute == GetIncomingHealingAttribute()
        || Attribute == GetIncomingPostureDamageAttribute() || Attribute == GetIncomingPostureRecoveryAttribute())
    {
        return Data.EvaluatedData.Magnitude >= 0.f;
    }
    return true;
}

/**
 * 游戏线程消费临时数值并立即清零；只提交最终资源变化，不计算护甲、不发布死亡事件。
 * @param Data 已执行的修改数据；来源上下文仍由 GAS Spec 持有。
 */
void USKCharacterAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);
    if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
    {
        const float Amount = GetIncomingDamage();
        SetIncomingDamage(0.f);
        const double Result = static_cast<double>(GetHealth()) - Amount;
        SetHealth(static_cast<float>(FMath::Clamp(Result, 0.0, static_cast<double>(GetMaxHealth()))));
    }
    else if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
    {
        const float Amount = GetIncomingHealing();
        SetIncomingHealing(0.f);
        const double Result = static_cast<double>(GetHealth()) + Amount;
        SetHealth(static_cast<float>(FMath::Clamp(Result, 0.0, static_cast<double>(GetMaxHealth()))));
    }
    else if (Data.EvaluatedData.Attribute == GetIncomingPostureDamageAttribute())
    {
        const float Amount = GetIncomingPostureDamage();
        SetIncomingPostureDamage(0.f);
        const double Result = static_cast<double>(GetPosture()) + Amount;
        SetPosture(static_cast<float>(FMath::Clamp(Result, 0.0, static_cast<double>(GetMaxPosture()))));
    }
    else if (Data.EvaluatedData.Attribute == GetIncomingPostureRecoveryAttribute())
    {
        const float Amount = GetIncomingPostureRecovery();
        SetIncomingPostureRecovery(0.f);
        const double Result = static_cast<double>(GetPosture()) - Amount;
        SetPosture(static_cast<float>(FMath::Clamp(Result, 0.0, static_cast<double>(GetMaxPosture()))));
    }
}

/**
 * 游戏线程统一限制基础值和聚合值；非有限输入保留属性现值。
 * @param Attribute 要限制的属性。
 * @param NewValue 输入候选数值，输出合法数值；正上限和衰减指数最小为 UE_SMALL_NUMBER。
 * 恢复倍率之间不互相改写，临时倒置由读取者按最新快照处理。
 */
void USKCharacterAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
    if (!FMath::IsFinite(NewValue))
    {
        NewValue = Attribute.GetNumericValue(this);
        return;
    }
    if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxPostureAttribute()
        || Attribute == GetPostureGainFalloffExponentAttribute())
    {
        NewValue = FMath::Max(UE_SMALL_NUMBER, NewValue);
    }
    else if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, GetMaxHealth()));
    }
    else if (Attribute == GetPostureAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, GetMaxPosture()));
    }
    else if (Attribute == GetPostureDeflectSuccessCapRatioAttribute()
        || Attribute == GetPostureAttackCapRatioAttribute()
        || Attribute == GetPostureRecoveryTargetRatioAttribute()
        || Attribute == GetRevivePostureRatioAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, std::nextafter(1.f, 0.f));
    }
    else if (Attribute == GetPostureMinGainScaleAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
    }
    else
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
}
