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
    Dodging
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
