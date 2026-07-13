#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "SekiroLuaAnimTypes.h"
#include "AnimNode_SekiroLuaOrientationWarping.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct SEKIROANIMBLUEPRINTEXT_API FAnimNode_SekiroLuaOrientationWarping : public FAnimNode_OrientationWarping
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    FName LayerName = NAME_None;          // 提供方向扭转策略的 Lua 动画层名称

protected:
    /** 作用：在组件姿势更新前缓存 Lua 方向扭转策略。@param Context const FAnimationUpdateContext&，动画更新上下文。@return void，无返回值。 */
    virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context) override;
    /** 作用：在暴露输入求值后应用 Lua 策略并更新方向扭转节点。@param Context const FAnimationUpdateContext&，动画更新上下文。@return void，无返回值。 */
    virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
    /** 作用：应用缓存策略后验证骨骼配置是否可求值。@param Skeleton const USkeleton*，目标骨架。@param RequiredBones const FBoneContainer&，当前所需骨骼集合。@return bool，节点可安全求值时为 true。 */
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

    /** 作用：从动画实例线程安全地复制指定层策略。@param AnimInstanceProxy const FAnimInstanceProxy*，动画实例代理。@return void，无返回值。 */
    void RefreshLuaOrientationWarpingPolicy(const FAnimInstanceProxy* AnimInstanceProxy);
    /** 作用：校验并写入父节点使用的手动扭转参数。@param 无。@return void，无返回值。 */
    void ApplyLuaOrientationWarpingPolicy();

    FSekiroLuaOrientationWarpingPolicy CachedOrientationWarpingPolicy; // 当前更新周期读取的线程安全策略副本
};
