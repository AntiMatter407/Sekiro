#include "AnimNodes/AnimNode_SekiroLuaStateMachine.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Logging/TokenizedMessage.h"
#include "SekiroLuaAnimInstance.h"

namespace
{
    void EvaluateSekiroLuaStatePose(const FSekiroLuaAnimState& State, float Time, bool bLoop, FPoseContext& Output)
    {
        if (!State.Sequence || !State.Sequence->GetSkeleton())
        {
            Output.ResetToRefPose();
            return;
        }

        const bool bExpectedAdditive = Output.ExpectsAdditivePose();
        const bool bIsAdditive = State.Sequence->IsValidAdditive();
        if (bExpectedAdditive && !bIsAdditive)
        {
            FText Message = FText::Format(
                NSLOCTEXT("AnimNode_SekiroLuaStateMachine", "AdditiveMismatchWarning", "Trying to play a non-additive animation '{0}' into a pose that is expected to be additive."),
                FText::FromString(State.Sequence->GetName()));
            Output.LogMessage(EMessageSeverity::Warning, Message);
        }

        FAnimationPoseData AnimationPoseData(Output);
        State.Sequence->GetAnimationPose(AnimationPoseData, FAnimExtractContext(static_cast<double>(Time), Output.AnimInstanceProxy->ShouldExtractRootMotion(), FDeltaTimeRecord(), bLoop));
    }

    USekiroLuaAnimGraphAsset* ResolveSekiroLuaGraphAsset(FAnimInstanceProxy* AnimInstanceProxy, FName LayerName, USekiroLuaAnimGraphAsset* NodeGraphAsset)
    {
        if (NodeGraphAsset)
        {
            return NodeGraphAsset;
        }

        if (!AnimInstanceProxy) return nullptr;

        UObject* AnimInstanceObject = AnimInstanceProxy->GetAnimInstanceObject();
        const USekiroLuaAnimInstance* LuaAnimInstance = Cast<USekiroLuaAnimInstance>(AnimInstanceObject);
        if (!LuaAnimInstance) return nullptr;

        return LuaAnimInstance->GetLuaAnimLayerGraphAsset(LayerName);
    }

    bool ResolveSekiroLuaAnimInstanceSnapshot(FAnimInstanceProxy* AnimInstanceProxy, FName LayerName, FSekiroLuaAnimSnapshot& OutSnapshot)
    {
        if (!AnimInstanceProxy) return false;

        UObject* AnimInstanceObject = AnimInstanceProxy->GetAnimInstanceObject();
        const USekiroLuaAnimInstance* LuaAnimInstance = Cast<USekiroLuaAnimInstance>(AnimInstanceObject);
        if (!LuaAnimInstance) return false;

        OutSnapshot = LuaAnimInstance->GetLuaAnimLayerSnapshot(LayerName);
        return true;
    }
}

void FAnimNode_SekiroLuaStateMachine::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);

    CachedSnapshot = FSekiroLuaAnimSnapshot();
    CachedGraphAsset = nullptr;
}

void FAnimNode_SekiroLuaStateMachine::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    FAnimNode_Base::Update_AnyThread(Context);

    CachedGraphAsset = ResolveSekiroLuaGraphAsset(Context.AnimInstanceProxy, LayerName, GraphAsset);
    if (!ResolveSekiroLuaAnimInstanceSnapshot(Context.AnimInstanceProxy, LayerName, CachedSnapshot))
    {
        CachedSnapshot = FSekiroLuaAnimSnapshot();
    }
}

void FAnimNode_SekiroLuaStateMachine::Evaluate_AnyThread(FPoseContext& Output)
{
    USekiroLuaAnimGraphAsset* ResolvedGraphAsset = CachedGraphAsset;
    if (!ResolvedGraphAsset)
    {
        ResolvedGraphAsset = ResolveSekiroLuaGraphAsset(Output.AnimInstanceProxy, LayerName, GraphAsset);
    }

    if (!ResolvedGraphAsset || !CachedSnapshot.bHasPose)
    {
        Output.ResetToRefPose();
        return;
    }

    const FSekiroLuaAnimState* CurrentState = ResolvedGraphAsset->FindState(CachedSnapshot.CurrentStateName);
    if (!CurrentState || !CurrentState->Sequence)
    {
        Output.ResetToRefPose();
        return;
    }

    const bool bNeedsBlend = !CachedSnapshot.PreviousStateName.IsNone() && CachedSnapshot.BlendAlpha < 1.0f;
    if (!bNeedsBlend)
    {
        EvaluateSekiroLuaStatePose(*CurrentState, CachedSnapshot.CurrentTime, CachedSnapshot.bCurrentLoop, Output);
        return;
    }

    const FSekiroLuaAnimState* PreviousState = ResolvedGraphAsset->FindState(CachedSnapshot.PreviousStateName);
    if (!PreviousState || !PreviousState->Sequence)
    {
        EvaluateSekiroLuaStatePose(*CurrentState, CachedSnapshot.CurrentTime, CachedSnapshot.bCurrentLoop, Output);
        return;
    }

    FPoseContext PreviousPose(Output);
    FPoseContext CurrentPose(Output);
    EvaluateSekiroLuaStatePose(*PreviousState, CachedSnapshot.PreviousTime, CachedSnapshot.bPreviousLoop, PreviousPose);
    EvaluateSekiroLuaStatePose(*CurrentState, CachedSnapshot.CurrentTime, CachedSnapshot.bCurrentLoop, CurrentPose);

    const FAnimationPoseData PreviousPoseData(PreviousPose);
    const FAnimationPoseData CurrentPoseData(CurrentPose);
    FAnimationPoseData OutputPoseData(Output);
    FAnimationRuntime::BlendTwoPosesTogether(PreviousPoseData, CurrentPoseData, 1.0f - CachedSnapshot.BlendAlpha, OutputPoseData);
}

void FAnimNode_SekiroLuaStateMachine::GatherDebugData(FNodeDebugData& DebugData)
{
    FString DebugLine = DebugData.GetNodeName(this);
    DebugLine += FString::Printf(TEXT("(Layer: %s, State: %s, Blend: %.2f)"),
        *LayerName.ToString(),
        *CachedSnapshot.CurrentStateName.ToString(),
        CachedSnapshot.BlendAlpha);
    DebugData.AddDebugItem(DebugLine, true);
}
