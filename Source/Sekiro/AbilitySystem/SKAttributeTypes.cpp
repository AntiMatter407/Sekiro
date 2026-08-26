#include "AbilitySystem/SKAttributeTypes.h"

/**
 * 校验完整初始配置，不修改输入或应用效果；可在任意线程检查独立值。
 * @return 全部数值有限，资源不越界，上限和衰减指数为正，非崩溃比例小于一、
 * 最低增长倍率在闭区间 [0,1] 内，恢复最小倍率不超过最大倍率且其余参数非负时返回 true。
 */
bool FSKAttributeInitialization::IsValid() const
{
    return FMath::IsFinite(MaxHealth) && MaxHealth > 0.f
        && FMath::IsFinite(InitialHealth) && InitialHealth >= 0.f && InitialHealth <= MaxHealth
        && FMath::IsFinite(AttackPower) && AttackPower >= 0.f
        && FMath::IsFinite(Armor) && Armor >= 0.f
        && FMath::IsFinite(MaxPosture) && MaxPosture > 0.f
        && FMath::IsFinite(InitialPosture) && InitialPosture >= 0.f && InitialPosture <= MaxPosture
        && FMath::IsFinite(PostureRecoveryRate) && PostureRecoveryRate >= 0.f
        && FMath::IsFinite(PostureRecoveryDelay) && PostureRecoveryDelay >= 0.f
        && FMath::IsFinite(PostureRecoveryRampDuration) && PostureRecoveryRampDuration >= 0.f
        && FMath::IsFinite(PostureRecoveryMinRateScale) && PostureRecoveryMinRateScale >= 0.f
        && FMath::IsFinite(PostureRecoveryMaxRateScale) && PostureRecoveryMaxRateScale >= 0.f
        && FMath::IsFinite(PostureDeflectSuccessCapRatio) && PostureDeflectSuccessCapRatio >= 0.f && PostureDeflectSuccessCapRatio < 1.f
        && FMath::IsFinite(PostureAttackCapRatio) && PostureAttackCapRatio >= 0.f && PostureAttackCapRatio < 1.f
        && FMath::IsFinite(PostureMinGainScale) && PostureMinGainScale >= 0.f && PostureMinGainScale <= 1.f
        && FMath::IsFinite(PostureGainFalloffExponent) && PostureGainFalloffExponent > 0.f
        && FMath::IsFinite(PostureGainDeflectSuccess) && PostureGainDeflectSuccess >= 0.f
        && FMath::IsFinite(PostureGainGuarded) && PostureGainGuarded >= 0.f
        && FMath::IsFinite(PostureGainDeflectFailed) && PostureGainDeflectFailed >= 0.f
        && FMath::IsFinite(PostureGainAttackSuccess) && PostureGainAttackSuccess >= 0.f
        && FMath::IsFinite(PostureGainAttackGuarded) && PostureGainAttackGuarded >= 0.f
        && FMath::IsFinite(PostureGainAttackDeflected) && PostureGainAttackDeflected >= 0.f
        && FMath::IsFinite(PostureStrengthLight) && PostureStrengthLight >= 0.f
        && FMath::IsFinite(PostureStrengthHeavy) && PostureStrengthHeavy >= 0.f
        && FMath::IsFinite(PostureStrengthThrust) && PostureStrengthThrust >= 0.f
        && FMath::IsFinite(PostureStrengthSpecial) && PostureStrengthSpecial >= 0.f
        && FMath::IsFinite(PostureBreakMinimumDuration) && PostureBreakMinimumDuration >= 0.f
        && FMath::IsFinite(PostureBreakBlendInTime) && PostureBreakBlendInTime >= 0.f
        && FMath::IsFinite(PostureBreakBlendOutTime) && PostureBreakBlendOutTime >= 0.f
        && FMath::IsFinite(PostureRecoveryTargetRatio) && PostureRecoveryTargetRatio >= 0.f && PostureRecoveryTargetRatio < 1.f
        && FMath::IsFinite(RevivePostureRatio) && RevivePostureRatio >= 0.f && RevivePostureRatio < 1.f
        && PostureRecoveryMinRateScale <= PostureRecoveryMaxRateScale;
}
