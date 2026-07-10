#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "SekiroLuaAnimTypes.h"
#include "AnimNode_SekiroLuaStateMachine.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct SEKIROANIMBLUEPRINTEXT_API FAnimNode_SekiroLuaStateMachine : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    FName LayerName = NAME_None;          // Lua 动画层名称，留空时读取 Lua AnimBlueprint 默认输出

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:
    FSekiroLuaAnimSnapshot CachedSnapshot; // 线程评估快照

    FSekiroLuaAnimSnapshot PreviousRootMotionSnapshot; // 上一帧根运动快照

    bool bHasPreviousRootMotionSnapshot = false; // 是否已有上一帧根运动快照
};
