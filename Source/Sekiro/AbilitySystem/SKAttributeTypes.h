#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "SKAttributeTypes.generated.h"

UENUM(BlueprintType)
enum class ESKNumericOperation : uint8
{
    Initialize,
    InitializeResources,
    Damage,
    Healing,
    PostureDamage,
    PostureRecovery,
    ResetPosture,
    SurvivalImpact,
    RestoreResources
};

UENUM(BlueprintType)
enum class ESKNumericResultCode : uint8
{
    Applied,
    NoChange,
    NotReady,
    InvalidInput,
    Reentrant,
    EffectRejected,
    PolicyRejected,
    ResourceCommitFailed
};

/** 完整初始配置；零默认值刻意无效，调用方必须显式配置正上限。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKAttributeInitialization
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxHealth = 0.f; // 生命上限

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InitialHealth = 0.f; // 初始生命绝对值

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float AttackPower = 0.f; // 基础攻击力

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Armor = 0.f; // 护甲曲线输入

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxPosture = 0.f; // 架势积累上限

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InitialPosture = 0.f; // 初始架势积累

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryRate = 0.f; // 每秒基础架势恢复点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryDelay = 0.f; // 恢复开始前的等待秒数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryRampDuration = 0.f; // 恢复速度渐进到最大倍率的秒数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryMinRateScale = 0.f; // 恢复初始速度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryMaxRateScale = 0.f; // 恢复最终速度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureDeflectSuccessCapRatio = 0.f; // 弹反成功躯干增长的非崩溃封顶比例

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureAttackCapRatio = 0.f; // 攻击方反馈躯干增长的非崩溃封顶比例

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureMinGainScale = 0.f; // 满躯干附近仍保留的最低增长倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainFalloffExponent = 0.f; // 躯干增长衰减曲线的正指数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainDeflectSuccess = 0.f; // 弹反成功时基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainGuarded = 0.f; // 格挡成功时基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainDeflectFailed = 0.f; // 弹反失败时基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainAttackSuccess = 0.f; // 攻击命中时攻击方基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainAttackGuarded = 0.f; // 攻击被格挡时攻击方基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureGainAttackDeflected = 0.f; // 攻击被弹反时攻击方基础躯干增长点数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureStrengthLight = 0.f; // 轻攻击的躯干强度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureStrengthHeavy = 0.f; // 重攻击的躯干强度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureStrengthThrust = 0.f; // 突刺的躯干强度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureStrengthSpecial = 0.f; // 特殊攻击的躯干强度倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureBreakMinimumDuration = 0.f; // 躯干崩溃的最短持续秒数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureBreakBlendInTime = 0.f; // 躯干崩溃演出混入秒数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureBreakBlendOutTime = 0.f; // 躯干崩溃演出混出秒数

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PostureRecoveryTargetRatio = 0.f; // 结束躯干崩溃时的目标积累比例

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RevivePostureRatio = 0.f; // 回生时的目标躯干积累比例


    bool IsValid() const;
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKAttributeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float Health = 0.f; // 当前生命

    UPROPERTY(BlueprintReadOnly)
    float MaxHealth = 0.f; // 当前生命上限

    UPROPERTY(BlueprintReadOnly)
    float AttackPower = 0.f; // 当前攻击力

    UPROPERTY(BlueprintReadOnly)
    float Armor = 0.f; // 当前护甲

    UPROPERTY(BlueprintReadOnly)
    float Posture = 0.f; // 当前架势积累

    UPROPERTY(BlueprintReadOnly)
    float MaxPosture = 0.f; // 当前架势上限

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryRate = 0.f; // 每秒架势恢复点数

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryDelay = 0.f; // 恢复开始前的等待秒数

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryRampDuration = 0.f; // 恢复速度渐进到最大倍率的秒数

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryMinRateScale = 0.f; // 恢复初始速度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryMaxRateScale = 0.f; // 恢复最终速度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureDeflectSuccessCapRatio = 0.f; // 弹反成功躯干增长的非崩溃封顶比例

    UPROPERTY(BlueprintReadOnly)
    float PostureAttackCapRatio = 0.f; // 攻击方反馈躯干增长的非崩溃封顶比例

    UPROPERTY(BlueprintReadOnly)
    float PostureMinGainScale = 0.f; // 满躯干附近仍保留的最低增长倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureGainFalloffExponent = 0.f; // 躯干增长衰减曲线的正指数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainDeflectSuccess = 0.f; // 弹反成功时基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainGuarded = 0.f; // 格挡成功时基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainDeflectFailed = 0.f; // 弹反失败时基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainAttackSuccess = 0.f; // 攻击命中时攻击方基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainAttackGuarded = 0.f; // 攻击被格挡时攻击方基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureGainAttackDeflected = 0.f; // 攻击被弹反时攻击方基础躯干增长点数

    UPROPERTY(BlueprintReadOnly)
    float PostureStrengthLight = 0.f; // 轻攻击的躯干强度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureStrengthHeavy = 0.f; // 重攻击的躯干强度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureStrengthThrust = 0.f; // 突刺的躯干强度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureStrengthSpecial = 0.f; // 特殊攻击的躯干强度倍率

    UPROPERTY(BlueprintReadOnly)
    float PostureBreakMinimumDuration = 0.f; // 躯干崩溃的最短持续秒数

    UPROPERTY(BlueprintReadOnly)
    float PostureBreakBlendInTime = 0.f; // 躯干崩溃演出混入秒数

    UPROPERTY(BlueprintReadOnly)
    float PostureBreakBlendOutTime = 0.f; // 躯干崩溃演出混出秒数

    UPROPERTY(BlueprintReadOnly)
    float PostureRecoveryTargetRatio = 0.f; // 结束躯干崩溃时的目标积累比例

    UPROPERTY(BlueprintReadOnly)
    float RevivePostureRatio = 0.f; // 回生时的目标躯干积累比例

};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKNumericResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    ESKNumericResultCode Code = ESKNumericResultCode::NotReady; // 执行结果

    UPROPERTY(BlueprintReadOnly)
    FName RejectionReason = NAME_None; // 单资源拒绝原因，不将死亡和免疫混淆

    UPROPERTY(BlueprintReadOnly)
    FName HealthRejectionReason = NAME_None; // 复合生命通道拒绝原因

    UPROPERTY(BlueprintReadOnly)
    FName PostureRejectionReason = NAME_None; // 复合躯干通道拒绝原因

    UPROPERTY(BlueprintReadOnly)
    ESKNumericOperation Operation = ESKNumericOperation::Damage; // 请求语义

    UPROPERTY(BlueprintReadOnly)
    int64 RequestId = 0; // ASC 内单调递增的请求编号

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> SourceActor = nullptr; // 可空伤害或恢复来源

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> TargetActor = nullptr; // 数值所属角色

    UPROPERTY(BlueprintReadOnly)
    float RequestedAmount = 0.f; // 请求量，尚未经过资源截断

    UPROPERTY(BlueprintReadOnly)
    float ActualAmount = 0.f; // 实际资源变化的绝对值

    UPROPERTY(BlueprintReadOnly)
    float RequestedHealthDamage = 0.f; // 复合请求原始生命伤害量

    UPROPERTY(BlueprintReadOnly)
    float RequestedPostureDamage = 0.f; // 复合请求原始躯干伤害量

    UPROPERTY(BlueprintReadOnly)
    float ActualHealthDamage = 0.f; // 复合请求实际生命扣减量

    UPROPERTY(BlueprintReadOnly)
    float ActualPostureDamage = 0.f; // 复合请求实际躯干增加量

    UPROPERTY(BlueprintReadOnly)
    ESKNumericResultCode HealthChannelCode = ESKNumericResultCode::NoChange; // 生命通道独立结果

    UPROPERTY(BlueprintReadOnly)
    ESKNumericResultCode PostureChannelCode = ESKNumericResultCode::NoChange; // 躯干通道独立结果

    UPROPERTY(BlueprintReadOnly)
    FSKAttributeSnapshot Before; // 提交前快照

    UPROPERTY(BlueprintReadOnly)
    FSKAttributeSnapshot After; // 提交后快照
};
