#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "SekiroLuaAnimTypes.generated.h"

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimState
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName StateName = NAME_None;          // 状态名称

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> AnimationAsset = nullptr; // 状态动画资产

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
    TObjectPtr<UAnimationAsset> AnimationAsset = nullptr; // 目标动画资源

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FString AnimationPath;                // 目标动画资源路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FName AnimationName = NAME_None;      // Lua 动画别名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float BlendTime = 0.15f;              // 进入混合时间

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float PlayRate = 1.0f;                // 播放速率倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bLoop = true;                    // 是否循环播放

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bResetTime = false;              // 是否重置时间

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float StartPosition = 0.0f;           // 重置时间时使用的归一化起播位置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bUseStartPosition = false;       // 是否使用指定起播位置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FVector BlendInput = FVector::ZeroVector; // BlendSpace 输入参数
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
    TObjectPtr<UAnimationAsset> CurrentAnimationAsset = nullptr; // 当前动画资源

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> PreviousAnimationAsset = nullptr; // 上一个动画资源

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName CurrentAnimationName = NAME_None; // 当前 Lua 动画别名

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName PreviousAnimationName = NAME_None; // 上一个 Lua 动画别名

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
    FVector CurrentBlendInput = FVector::ZeroVector; // 当前 BlendSpace 输入

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FVector PreviousBlendInput = FVector::ZeroVector; // 上一个 BlendSpace 输入

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bHasPose = false;                // 是否有有效姿势
};
