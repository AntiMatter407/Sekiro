#include "AnimNodes/AnimNode_SekiroLuaOrientationWarping.h"

#include "Animation/AnimInstanceProxy.h"
#include "SekiroLuaAnimInstanceProxy.h"

void FAnimNode_SekiroLuaOrientationWarping::UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context)
{
    RefreshLuaOrientationWarpingPolicy(Context.AnimInstanceProxy);
    FAnimNode_OrientationWarping::UpdateComponentPose_AnyThread(Context);
}

void FAnimNode_SekiroLuaOrientationWarping::UpdateInternal(const FAnimationUpdateContext& Context)
{
    // ExposedInputs 已执行完毕，此处重新写入策略，避免默认 Pin 覆盖 Lua 值。
    RefreshLuaOrientationWarpingPolicy(Context.AnimInstanceProxy);
    ApplyLuaOrientationWarpingPolicy();
    FAnimNode_OrientationWarping::UpdateInternal(Context);
}

bool FAnimNode_SekiroLuaOrientationWarping::IsValidToEvaluate(
    const USkeleton* Skeleton,
    const FBoneContainer& RequiredBones)
{
    // UE5.2 在 UpdateInternal 前检查有效性，因此必须先恢复本帧缓存策略。
    ApplyLuaOrientationWarpingPolicy();
    return FAnimNode_OrientationWarping::IsValidToEvaluate(Skeleton, RequiredBones);
}

void FAnimNode_SekiroLuaOrientationWarping::RefreshLuaOrientationWarpingPolicy(
    const FAnimInstanceProxy* AnimInstanceProxy)
{
    CachedOrientationWarpingPolicy = FSekiroLuaOrientationWarpingPolicy();
    if (!AnimInstanceProxy) return;

    const FSekiroLuaAnimInstanceProxy* LuaAnimProxy = static_cast<const FSekiroLuaAnimInstanceProxy*>(AnimInstanceProxy);
    const FSekiroLuaAnimSnapshot* Snapshot = LuaAnimProxy->FindLayerSnapshot(LayerName);
    if (Snapshot)
    {
        CachedOrientationWarpingPolicy = Snapshot->OrientationWarpingPolicy;
    }
}

void FAnimNode_SekiroLuaOrientationWarping::ApplyLuaOrientationWarpingPolicy()
{
    Mode = EWarpingEvaluationMode::Manual;
    OrientationAngle = 0.0f;
    WarpingAlpha = 0.0f;
    if (!CachedOrientationWarpingPolicy.bEnabled) return;

    OrientationAngle = FRotator::NormalizeAxis(
        FMath::IsFinite(CachedOrientationWarpingPolicy.OrientationAngle)
            ? CachedOrientationWarpingPolicy.OrientationAngle
            : 0.0f);
    WarpingAlpha = FMath::Clamp(
        FMath::IsFinite(CachedOrientationWarpingPolicy.WarpingAlpha)
            ? CachedOrientationWarpingPolicy.WarpingAlpha
            : 0.0f,
        0.0f,
        1.0f);
}
