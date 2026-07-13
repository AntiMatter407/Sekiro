#include "SekiroLuaAnimInstanceProxy.h"

#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_SekiroLuaStateMachine.h"
#include "SekiroLuaAnimInstance.h"
#include "UObject/UnrealType.h"

FSekiroLuaAnimInstanceProxy::FSekiroLuaAnimInstanceProxy()
    : FAnimInstanceProxy()
{
}

FSekiroLuaAnimInstanceProxy::FSekiroLuaAnimInstanceProxy(UAnimInstance* AnimInstance)
    : FAnimInstanceProxy(AnimInstance)
{
}

void FSekiroLuaAnimInstanceProxy::PublishSnapshots(USekiroLuaAnimInstance& AnimInstance)
{
    AnimInstance.CopyLuaAnimSnapshotsForProxy(PublishedLayerSnapshots, DefaultLayerName);

    IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimInstance.GetClass());
    if (!AnimClassInterface) return;

    const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();
    for (const FStructProperty* AnimNodeProperty : AnimNodeProperties)
    {
        if (!AnimNodeProperty
            || !AnimNodeProperty->Struct
            || !AnimNodeProperty->Struct->IsChildOf(FAnimNode_SekiroLuaStateMachine::StaticStruct()))
        {
            continue;
        }

        FAnimNode_SekiroLuaStateMachine* StateMachineNode =
            AnimNodeProperty->ContainerPtrToValuePtr<FAnimNode_SekiroLuaStateMachine>(&AnimInstance);
        const FSekiroLuaAnimSnapshot* LayerSnapshot = StateMachineNode
            ? FindLayerSnapshot(StateMachineNode->LayerName)
            : nullptr;
        if (StateMachineNode && LayerSnapshot)
        {
            StateMachineNode->PreparePoseGraphTopology(this, LayerSnapshot->PoseGraph);
        }
    }
}

const FSekiroLuaAnimSnapshot* FSekiroLuaAnimInstanceProxy::FindLayerSnapshot(FName LayerName) const
{
    const FName ResolvedLayerName = LayerName.IsNone() ? DefaultLayerName : LayerName;
    for (const FSekiroLuaAnimProxyLayerSnapshot& LayerSnapshot : PublishedLayerSnapshots)
    {
        if (LayerSnapshot.LayerName == ResolvedLayerName) return &LayerSnapshot.Snapshot;
    }
    return nullptr;
}

void FSekiroLuaAnimInstanceProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
    FAnimInstanceProxy::PostUpdate(InAnimInstance);
    if (!InAnimInstance) return;

    CollectRuntimeStates(*InAnimInstance);
    ApplyRootMotionRotationPolicy();

    USekiroLuaAnimInstance* LuaAnimInstance = Cast<USekiroLuaAnimInstance>(InAnimInstance);
    if (LuaAnimInstance)
    {
        LuaAnimInstance->ApplyLuaAnimProxyRuntimeStates(RuntimeLayerStates);
    }
}

void FSekiroLuaAnimInstanceProxy::CollectRuntimeStates(UAnimInstance& InAnimInstance) const
{
    RuntimeLayerStates.Reset(PublishedLayerSnapshots.Num());
    for (const FSekiroLuaAnimProxyLayerSnapshot& LayerSnapshot : PublishedLayerSnapshots)
    {
        FSekiroLuaPoseGraphRuntimeState& RuntimeState = RuntimeLayerStates.AddDefaulted_GetRef();
        RuntimeState.LayerName = LayerSnapshot.LayerName;
        RuntimeState.Generation = LayerSnapshot.Snapshot.PoseGraph.Generation;
    }

    IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(InAnimInstance.GetClass());
    if (!AnimClassInterface) return;

    const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();
    for (const FStructProperty* AnimNodeProperty : AnimNodeProperties)
    {
        if (!AnimNodeProperty
            || !AnimNodeProperty->Struct
            || !AnimNodeProperty->Struct->IsChildOf(FAnimNode_SekiroLuaStateMachine::StaticStruct()))
        {
            continue;
        }

        const FAnimNode_SekiroLuaStateMachine* StateMachineNode =
            AnimNodeProperty->ContainerPtrToValuePtr<FAnimNode_SekiroLuaStateMachine>(&InAnimInstance);
        if (!StateMachineNode) continue;

        FSekiroLuaPoseGraphRuntimeState RuntimeState;
        StateMachineNode->CollectRuntimeState(RuntimeState);
        if (RuntimeState.LayerName.IsNone())
        {
            RuntimeState.LayerName = DefaultLayerName;
        }
        MergeRuntimeState(RuntimeState);
    }

    for (FSekiroLuaPoseGraphRuntimeState& RuntimeState : RuntimeLayerStates)
    {
        RuntimeState.ActiveNodeIds.Sort();
        RuntimeState.SequencePlayers.Sort([](
            const FSekiroLuaSequencePlayerRuntimeState& Left,
            const FSekiroLuaSequencePlayerRuntimeState& Right)
        {
            return Left.PoseLink.NodeId < Right.PoseLink.NodeId;
        });
    }
}

void FSekiroLuaAnimInstanceProxy::MergeRuntimeState(const FSekiroLuaPoseGraphRuntimeState& RuntimeState) const
{
    FSekiroLuaPoseGraphRuntimeState* TargetState = nullptr;
    for (FSekiroLuaPoseGraphRuntimeState& ExistingState : RuntimeLayerStates)
    {
        if (ExistingState.LayerName == RuntimeState.LayerName)
        {
            TargetState = &ExistingState;
            break;
        }
    }
    if (!TargetState)
    {
        TargetState = &RuntimeLayerStates.AddDefaulted_GetRef();
        TargetState->LayerName = RuntimeState.LayerName;
        TargetState->Generation = RuntimeState.Generation;
    }

    if (TargetState->Generation != RuntimeState.Generation) return;

    for (int32 NodeId : RuntimeState.ActiveNodeIds)
    {
        TargetState->ActiveNodeIds.AddUnique(NodeId);
    }
    for (const FSekiroLuaSequencePlayerRuntimeState& PlayerState : RuntimeState.SequencePlayers)
    {
        bool bUpdatedExistingPlayer = false;
        for (FSekiroLuaSequencePlayerRuntimeState& ExistingPlayerState : TargetState->SequencePlayers)
        {
            if (ExistingPlayerState.PoseLink == PlayerState.PoseLink)
            {
                ExistingPlayerState.CurrentTime = PlayerState.CurrentTime;
                bUpdatedExistingPlayer = true;
                break;
            }
        }
        if (!bUpdatedExistingPlayer)
        {
            TargetState->SequencePlayers.Add(PlayerState);
        }
    }

    if (RuntimeState.TransitionAlpha < 1.0f
        || TargetState->TransitionAlpha >= 1.0f)
    {
        TargetState->PreviousPose = RuntimeState.PreviousPose;
        TargetState->TransitionTime = RuntimeState.TransitionTime;
        TargetState->TransitionElapsedTime = RuntimeState.TransitionElapsedTime;
        TargetState->TransitionAlpha = RuntimeState.TransitionAlpha;
    }
    if (RuntimeState.DominantRootMotionWeight > TargetState->DominantRootMotionWeight)
    {
        TargetState->DominantRootMotionWeight = RuntimeState.DominantRootMotionWeight;
        TargetState->DominantRootMotionRotationMode = RuntimeState.DominantRootMotionRotationMode;
    }
}

void FSekiroLuaAnimInstanceProxy::ApplyRootMotionRotationPolicy() const
{
    ESekiroLuaRootMotionRotationMode DominantMode = ESekiroLuaRootMotionRotationMode::Extract;
    float DominantWeight = 0.0f;
    for (const FSekiroLuaPoseGraphRuntimeState& RuntimeState : RuntimeLayerStates)
    {
        if (RuntimeState.DominantRootMotionWeight > DominantWeight)
        {
            DominantWeight = RuntimeState.DominantRootMotionWeight;
            DominantMode = RuntimeState.DominantRootMotionRotationMode;
        }
    }
    if (DominantMode == ESekiroLuaRootMotionRotationMode::Extract) return;

    FSekiroLuaAnimInstanceProxy* MutableProxy = const_cast<FSekiroLuaAnimInstanceProxy*>(this);
    FRootMotionMovementParams& RootMotionParams = MutableProxy->GetExtractedRootMotion();
    if (!RootMotionParams.bHasRootMotion) return;

    FTransform RootMotionTransform = RootMotionParams.GetRootMotionTransform();
    RootMotionTransform.SetRotation(FQuat::Identity);
    RootMotionParams.Set(RootMotionTransform);
}
