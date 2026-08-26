#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "SKCharacterAttributeSet.generated.h"

/** 角色唯一 GAS 属性集，统一生命、战斗、躯干配置和临时执行属性。 */
UCLASS()
class SEKIRO_API USKCharacterAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    // ── GAS 属性 ──────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData Health; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, Health)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Health)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Health)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Health)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData MaxHealth; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, MaxHealth)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(MaxHealth)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(MaxHealth)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(MaxHealth)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData AttackPower; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, AttackPower)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(AttackPower)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(AttackPower)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(AttackPower)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData Armor; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, Armor)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Armor)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Armor)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Armor)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData Posture; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, Posture)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Posture)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Posture)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Posture)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData MaxPosture; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, MaxPosture)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(MaxPosture)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(MaxPosture)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(MaxPosture)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryRate; // GAS 唯一属性存储

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryRate)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryRate)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryRate)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryRate)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryDelay; // 恢复开始前的等待秒数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryDelay)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryDelay)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryDelay)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryDelay)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryRampDuration; // 恢复速度渐进到最大倍率的秒数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryRampDuration)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryRampDuration)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryRampDuration)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryRampDuration)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryMinRateScale; // 恢复初始速度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryMinRateScale)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryMinRateScale)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryMinRateScale)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryMinRateScale)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryMaxRateScale; // 恢复最终速度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryMaxRateScale)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryMaxRateScale)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryMaxRateScale)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryMaxRateScale)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureDeflectSuccessCapRatio; // 弹反成功躯干增长的非崩溃封顶比例

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureDeflectSuccessCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureDeflectSuccessCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureDeflectSuccessCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureDeflectSuccessCapRatio)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureAttackCapRatio; // 攻击方反馈躯干增长的非崩溃封顶比例

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureAttackCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureAttackCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureAttackCapRatio)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureAttackCapRatio)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureMinGainScale; // 满躯干附近仍保留的最低增长倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureMinGainScale)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureMinGainScale)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureMinGainScale)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureMinGainScale)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainFalloffExponent; // 躯干增长衰减曲线的正指数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainFalloffExponent)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainFalloffExponent)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainFalloffExponent)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainFalloffExponent)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainDeflectSuccess; // 弹反成功时基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainDeflectSuccess)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainDeflectSuccess)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainDeflectSuccess)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainDeflectSuccess)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainGuarded; // 格挡成功时基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainGuarded)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainGuarded)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainGuarded)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainGuarded)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainDeflectFailed; // 弹反失败时基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainDeflectFailed)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainDeflectFailed)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainDeflectFailed)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainDeflectFailed)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainAttackSuccess; // 攻击命中时攻击方基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainAttackSuccess)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainAttackSuccess)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainAttackSuccess)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainAttackSuccess)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainAttackGuarded; // 攻击被格挡时攻击方基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainAttackGuarded)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainAttackGuarded)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainAttackGuarded)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainAttackGuarded)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureGainAttackDeflected; // 攻击被弹反时攻击方基础躯干增长点数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureGainAttackDeflected)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureGainAttackDeflected)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureGainAttackDeflected)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureGainAttackDeflected)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureStrengthLight; // 轻攻击的躯干强度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureStrengthLight)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureStrengthLight)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureStrengthLight)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureStrengthLight)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureStrengthHeavy; // 重攻击的躯干强度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureStrengthHeavy)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureStrengthHeavy)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureStrengthHeavy)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureStrengthHeavy)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureStrengthThrust; // 突刺的躯干强度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureStrengthThrust)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureStrengthThrust)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureStrengthThrust)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureStrengthThrust)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureStrengthSpecial; // 特殊攻击的躯干强度倍率

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureStrengthSpecial)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureStrengthSpecial)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureStrengthSpecial)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureStrengthSpecial)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureBreakMinimumDuration; // 躯干崩溃的最短持续秒数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureBreakMinimumDuration)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureBreakMinimumDuration)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureBreakMinimumDuration)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureBreakMinimumDuration)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureBreakBlendInTime; // 躯干崩溃演出混入秒数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureBreakBlendInTime)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureBreakBlendInTime)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureBreakBlendInTime)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureBreakBlendInTime)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureBreakBlendOutTime; // 躯干崩溃演出混出秒数

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureBreakBlendOutTime)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureBreakBlendOutTime)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureBreakBlendOutTime)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureBreakBlendOutTime)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData PostureRecoveryTargetRatio; // 结束躯干崩溃时的目标积累比例

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, PostureRecoveryTargetRatio)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PostureRecoveryTargetRatio)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PostureRecoveryTargetRatio)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PostureRecoveryTargetRatio)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes")
    FGameplayAttributeData RevivePostureRatio; // 回生时的目标躯干积累比例

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, RevivePostureRatio)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(RevivePostureRatio)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(RevivePostureRatio)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(RevivePostureRatio)


    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", Transient)
    FGameplayAttributeData IncomingDamage; // 单次执行后清零的临时数值

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, IncomingDamage)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(IncomingDamage)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(IncomingDamage)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(IncomingDamage)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", Transient)
    FGameplayAttributeData IncomingHealing; // 单次执行后清零的临时数值

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, IncomingHealing)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(IncomingHealing)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(IncomingHealing)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(IncomingHealing)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", Transient)
    FGameplayAttributeData IncomingPostureDamage; // 单次执行后清零的临时数值

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, IncomingPostureDamage)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(IncomingPostureDamage)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(IncomingPostureDamage)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(IncomingPostureDamage)

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", Transient)
    FGameplayAttributeData IncomingPostureRecovery; // 单次执行后清零的临时数值

    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(USKCharacterAttributeSet, IncomingPostureRecovery)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(IncomingPostureRecovery)
    GAMEPLAYATTRIBUTE_VALUE_SETTER(IncomingPostureRecovery)
    GAMEPLAYATTRIBUTE_VALUE_INITTER(IncomingPostureRecovery)

    // ── 数值约束 ──────────────────────────────────────────
    static TArray<FGameplayAttribute> GetConfigAttributes();

    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
    virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
    virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

private:
    // ── 内部约束 ──────────────────────────────────────────
    void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
