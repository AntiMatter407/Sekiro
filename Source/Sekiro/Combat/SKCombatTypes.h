// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/SKAttributeTypes.h"
#include "SKCombatTypes.generated.h"

UENUM(BlueprintType)
enum class ESKCombatActionState : uint8
{
    Neutral,
    PendingAttack,
    LightAttack,
    HeavyAttack,
    GuardRaise,
    Guarding,
    GuardLower,
    DeflectReaction,
    Dodging,
    PostureBroken,
    AIReaction
};

UENUM(BlueprintType)
enum class ESKCombatPostureState : uint8
{
    Normal,
    GuardGround,
    GuardAir
};

UENUM(BlueprintType)
enum class ESKAttackSide : uint8
{
    None,
    Left,
    Right
};

UENUM(BlueprintType)
enum class ESKCombatInputAction : uint8
{
    Attack,
    Guard
};

UENUM(BlueprintType)
enum class ESKCombatInputPhase : uint8
{
    Started,
    Completed
};

UENUM(BlueprintType)
enum class ESKIncomingAttackType : uint8
{
    Light,
    Heavy,
    Thrust,
    Special
};

UENUM(BlueprintType)
enum class ESKWeaponContactResult : uint8
{
    Ignored,
    Hit,
    Guarded,
    Deflected
};

UENUM(BlueprintType)
enum class ESKCombatDamageChannel : uint8 { Melee, Projectile };

UENUM(BlueprintType)
enum class ESKCombatHitOutcome : uint8 { Ignored, Hit, Guarded, Deflected, Dodged, Invulnerable };

UENUM(BlueprintType)
enum class ESKCombatHitResultCode : uint8 { Rejected, Committed };

USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHitRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<AActor> SourceActor = nullptr; // 发起角色，不是武器或弹射物
    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<AActor> TargetActor = nullptr; // 实际接触目标
    UPROPERTY(BlueprintReadWrite)
    int32 SourceActionSerial = 0; // 签发来源时锁存的正动作序号
    UPROPERTY(BlueprintReadWrite)
    int64 SourceLifeSerial = 0; // 来源生命轮次，死亡或回生使旧请求失效
    UPROPERTY(BlueprintReadWrite)
    int64 HitSourceSerial = 0; // 来源组件签发的窗口或弹射物身份
    UPROPERTY(BlueprintReadWrite)
    ESKIncomingAttackType AttackType = ESKIncomingAttackType::Light; // 签发时的攻击类型
    UPROPERTY(BlueprintReadWrite)
    ESKCombatDamageChannel DamageChannel = ESKCombatDamageChannel::Melee; // 来源通道
    UPROPERTY(BlueprintReadWrite)
    float HealthDamage = 0.f; // Lua 配置的有限非负基础生命伤害
    UPROPERTY(BlueprintReadWrite)
    float PostureDamage = 0.f; // 在 Survival 公式封顶前纳入的额外躯干伤害
    UPROPERTY(BlueprintReadWrite)
    FVector ImpactPoint = FVector::ZeroVector; // 世界命中点，厘米
    UPROPERTY(BlueprintReadWrite)
    FVector AttackDirection = FVector::ZeroVector; // 从来源指向目标的世界方向
    UPROPERTY(BlueprintReadWrite)
    FName EventTag = NAME_None; // 不由原生解释的中性事件标签
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHitEvaluation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    bool bAccepted = false; // 规则必须显式接受，未绑定 Lua 时失败关闭
    UPROPERTY(BlueprintReadWrite)
    ESKCombatHitOutcome Outcome = ESKCombatHitOutcome::Ignored; // 攻防接触结果，不与死亡或崩溃混合
    UPROPERTY(BlueprintReadWrite)
    float HealthDamage = 0.f; // 规则计算后的生命伤害
    UPROPERTY(BlueprintReadWrite)
    FName TargetPostureReason = NAME_None; // 守方躯干规则语义
    UPROPERTY(BlueprintReadWrite)
    FName SourcePostureReason = NAME_None; // 攻方躯干规则语义
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHitResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    ESKCombatHitResultCode Code = ESKCombatHitResultCode::Rejected; // 是否完成一次权威结算
    UPROPERTY(BlueprintReadOnly)
    ESKCombatHitOutcome Outcome = ESKCombatHitOutcome::Ignored; // 独立保存攻防结果
    UPROPERTY(BlueprintReadOnly)
    FName RejectionReason = NAME_None; // 明确的门禁或提交拒绝原因
    UPROPERTY(BlueprintReadOnly)
    float AppliedHealthDamage = 0.f; // 守方实际生命扣减
    UPROPERTY(BlueprintReadOnly)
    float AppliedPostureDamage = 0.f; // 守方实际躯干增长
    UPROPERTY(BlueprintReadOnly)
    float AppliedSourcePostureDamage = 0.f; // 攻方反馈实际躯干增长
    UPROPERTY(BlueprintReadOnly)
    bool bPostureBroken = false; // 本次首次打崩守方
    UPROPERTY(BlueprintReadOnly)
    bool bSourcePostureBroken = false; // 本次首次打崩攻方
    UPROPERTY(BlueprintReadOnly)
    bool bKilled = false; // 本次守方开始死亡，不代表 Boss 最终击败
    UPROPERTY(BlueprintReadOnly)
    int32 TargetActionSerial = 0; // 裁决前目标动作身份
    UPROPERTY(BlueprintReadOnly)
    FSKNumericResult Numeric; // 守方复合提交的真实 GAS 数值结果
    UPROPERTY(BlueprintReadOnly)
    FSKNumericResult SourceNumeric; // 攻方反馈结果，不掩盖已经提交的守方结果
};

UENUM(BlueprintType)
enum class ESKAICombatEventType : uint8
{
    None,
    AttackThreat,
    WeaponContact,
    DamageReceived,
    ProjectileImpact,
    TargetAction,
    ReactionRequested,
    ForceReplan,
    SemanticSignal
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKAICombatEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Combat|AI Event")
    int32 EventSerial = 0; // 组件分配的事件顺序号

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    ESKAICombatEventType EventType = ESKAICombatEventType::None; // 通用事件类型

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    TObjectPtr<AActor> SourceActor = nullptr; // 事件发起者

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    TObjectPtr<AActor> TargetActor = nullptr; // 事件直接作用对象

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    int32 RelatedActionSerial = 0; // 关联战斗动作序列号

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    ESKIncomingAttackType AttackType = ESKIncomingAttackType::Light; // 可选来袭攻击类型

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    ESKWeaponContactResult ContactResult = ESKWeaponContactResult::Ignored; // 可选武器接触结果

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    FName EventTag = NAME_None; // 可选中性语义标签

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    float Magnitude = 0.f; // 可选事件强度

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    ESKCombatHitOutcome HitOutcome = ESKCombatHitOutcome::Ignored; // 统一命中结果，兼容保留 ContactResult

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    int64 HitSourceSerial = 0; // 关联唯一攻击窗口或弹射物

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    bool bKilled = false; // 本次接触是否开始死亡

    UPROPERTY(BlueprintReadWrite, Category = "Combat|AI Event")
    bool bPostureBroken = false; // 本次接触是否首次打崩

    UPROPERTY(BlueprintReadOnly, Category = "Combat|AI Event")
    double EventTimeSeconds = 0.0; // 组件记录的游戏世界绝对时间
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatInputEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
    ESKCombatInputAction Action = ESKCombatInputAction::Attack; // 输入动作类型

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
    ESKCombatInputPhase Phase = ESKCombatInputPhase::Started; // 输入边沿类型

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
    int32 InputSerial = 0; // 本次物理按键的唯一序列号

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
    double EventTimeSeconds = 0.0; // 输入发生的游戏世界绝对时间

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Input")
    float HoldDuration = 0.f; // Completed 边沿携带的按住时长
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKIncomingAttackAnimationContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Animation Test")
    ESKIncomingAttackType AttackType = ESKIncomingAttackType::Light; // 模拟来袭类型

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Animation Test")
    int32 ContextSerial = 0; // 来袭上下文唯一序列号

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Animation Test")
    double ActiveStartTimeSeconds = 0.0; // 有效区间起点

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Animation Test")
    double ActiveEndTimeSeconds = 0.0; // 有效区间终点

    UPROPERTY(BlueprintReadOnly, Category = "Combat|Animation Test")
    bool bConsumed = false; // 是否已被 Guard Started 消费
};
