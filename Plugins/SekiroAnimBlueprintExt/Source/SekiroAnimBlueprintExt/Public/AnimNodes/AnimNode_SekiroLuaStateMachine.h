#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "SekiroLuaAnimGraphAsset.h"
#include "AnimNode_SekiroLuaStateMachine.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct SEKIROANIMBLUEPRINTEXT_API FAnimNode_SekiroLuaStateMachine : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    FName LayerName = NAME_None;          // Lua 动画层名称

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    TObjectPtr<USekiroLuaAnimGraphAsset> GraphAsset = nullptr; // Lua 动画图覆盖

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:
    FSekiroLuaAnimSnapshot CachedSnapshot; // 线程评估快照
    USekiroLuaAnimGraphAsset* CachedGraphAsset = nullptr; // 线程评估资产
};
