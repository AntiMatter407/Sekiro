#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequenceBase.h"
#include "SekiroLuaAnimTypes.generated.h"

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimState
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName StateName = NAME_None;          // 状态名称

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimSequenceBase> Sequence = nullptr; // 状态动画

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0"))
    float BlendTime = 0.15f;              // 进入混合时间

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PlayRate = 1.0f;                // 默认播放速率

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bLoop = true;                    // 是否循环播放
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimDecision
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FName StateName = NAME_None;          // 目标状态

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float PlayRate = 1.0f;                // 播放速率倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bResetTime = false;              // 是否重置时间
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName CurrentStateName = NAME_None;   // 当前状态

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName PreviousStateName = NAME_None;  // 上一个状态

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentTime = 0.0f;             // 当前状态时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousTime = 0.0f;            // 上一个状态时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentPlayRate = 1.0f;         // 当前播放速率

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousPlayRate = 1.0f;        // 上一个播放速率

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendAlpha = 1.0f;              // 混合权重

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendTime = 0.0f;               // 混合总时长

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendElapsedTime = 0.0f;        // 混合已用时

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bCurrentLoop = true;             // 当前是否循环

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bPreviousLoop = true;            // 上一个是否循环

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bHasPose = false;                // 是否有有效姿势
};
