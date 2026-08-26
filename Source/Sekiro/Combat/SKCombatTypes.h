// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
