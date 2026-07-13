#include "AnimGraphNode_SekiroLuaOrientationWarping.h"

#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_SekiroLuaOrientationWarping"

FText UAnimGraphNode_SekiroLuaOrientationWarping::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return GetControllerDescription();
}

FText UAnimGraphNode_SekiroLuaOrientationWarping::GetTooltipText() const
{
    return LOCTEXT(
        "NodeTooltip",
        "Applies UE Orientation Warping in Manual mode using the policy snapshot of a Lua animation layer.");
}

FLinearColor UAnimGraphNode_SekiroLuaOrientationWarping::GetNodeTitleColor() const
{
    return FLinearColor(0.52f, 0.18f, 0.12f);
}

FString UAnimGraphNode_SekiroLuaOrientationWarping::GetNodeCategory() const
{
    return TEXT("Sekiro|Lua Animation");
}

FText UAnimGraphNode_SekiroLuaOrientationWarping::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "Sekiro|Lua Animation");
}

void UAnimGraphNode_SekiroLuaOrientationWarping::ValidateAnimNodeDuringCompilation(
    USkeleton* ForSkeleton,
    FCompilerResultsLog& MessageLog)
{
    if (Node.SpineBones.IsEmpty())
    {
        MessageLog.Warning(*LOCTEXT("MissingSpineBones", "@@ - Spine bone definitions are required for Lua Orientation Warping.").ToString(), this);
    }
    if (Node.IKFootRootBone.BoneName.IsNone())
    {
        MessageLog.Warning(*LOCTEXT("MissingIKFootRoot", "@@ - IK Foot Root Bone is required for Lua Orientation Warping.").ToString(), this);
    }
    if (Node.IKFootBones.IsEmpty())
    {
        MessageLog.Warning(*LOCTEXT("MissingIKFootBones", "@@ - IK Foot Bones are required for Lua Orientation Warping.").ToString(), this);
    }

    Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

FText UAnimGraphNode_SekiroLuaOrientationWarping::GetControllerDescription() const
{
    return LOCTEXT("ControllerDescription", "Lua Orientation Warping");
}

const FAnimNode_SkeletalControlBase* UAnimGraphNode_SekiroLuaOrientationWarping::GetNode() const
{
    return &Node;
}

#undef LOCTEXT_NAMESPACE
