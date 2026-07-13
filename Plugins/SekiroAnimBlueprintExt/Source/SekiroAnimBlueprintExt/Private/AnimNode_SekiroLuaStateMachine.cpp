#include "AnimNodes/AnimNode_SekiroLuaStateMachine.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "SekiroAnimBlueprintExt.h"
#include "SekiroLuaAnimInstanceProxy.h"

namespace
{
    constexpr int32 SekiroLuaMinimumTransitionCapacity = 128;

    /** 作用：生成包含 Lua 别名的调试资产标签。@param AnimationAsset const UAnimationAsset*，动画资产。@param AnimationName FName，Lua 别名。@return FString，调试标签。 */
    FString GetSekiroLuaAnimDebugAssetLabel(const UAnimationAsset* AnimationAsset, FName AnimationName)
    {
        const FString AssetName = AnimationAsset ? AnimationAsset->GetName() : FString(TEXT("None"));
        return AnimationName.IsNone() ? AssetName : FString::Printf(TEXT("%s [%s]"), *AssetName, *AnimationName.ToString());
    }
}

void FSekiroLuaSequencePlayerNode_Standalone::ResetPlayback(float StartTime)
{
    SetStartPosition(StartTime);
    SetAccumulatedTime(StartTime);
    MarkerTickRecord.Reset();
    DeltaTimeRecord = FDeltaTimeRecord();
    BlendWeight = 0.0f;
    bHasBeenFullWeight = false;
    PlayRateScaleBiasClampState.Reinitialize();
}

void FSekiroLuaStateResultNode_Standalone::GatherDebugData(FNodeDebugData& DebugData)
{
    DebugData.AddDebugItem(FString::Printf(
        TEXT("StateResult: %s State: %s InputNodeId: %d InputType: %s"),
        *NodeName.ToString(),
        *StateName.ToString(),
        InputNode.PoseLink.NodeId,
        *StaticEnum<ESekiroLuaPoseNodeType>()->GetNameStringByValue(static_cast<int64>(InputNode.NodeType))));
    Result.GatherDebugData(DebugData);
}

void FAnimNode_SekiroLuaStateMachine::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);

    for (FSekiroLuaSequencePlayerNode_Standalone& PlayerNode : PoseGraphSequencePlayers)
    {
        PlayerNode.Initialize_AnyThread(Context);
    }

    CachedSnapshot = nullptr;
    ResetActiveTransitions();
    DominantRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract;
    DominantRootMotionWeight = 0.0f;
    bTopologyInitialized = true;
    bWarnedMissingInertializationRequester = false;
}

void FAnimNode_SekiroLuaStateMachine::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    FAnimNode_Base::CacheBones_AnyThread(Context);

    for (FSekiroLuaSequencePlayerNode_Standalone& PlayerNode : PoseGraphSequencePlayers)
    {
        PlayerNode.CacheBones_AnyThread(Context);
    }
}

void FAnimNode_SekiroLuaStateMachine::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    FAnimNode_Base::Update_AnyThread(Context);

    const FSekiroLuaAnimInstanceProxy* LuaAnimProxy = static_cast<const FSekiroLuaAnimInstanceProxy*>(Context.AnimInstanceProxy);
    const FSekiroLuaAnimSnapshot* NewSnapshot = LuaAnimProxy ? LuaAnimProxy->FindLayerSnapshot(LayerName) : nullptr;
    CachedSnapshot = NewSnapshot;
    if (!NewSnapshot || !NewSnapshot->PoseGraph.bHasOutputPose)
    {
        ResetActiveTransitions();
        DominantRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract;
        DominantRootMotionWeight = 0.0f;
        return;
    }

    if (!DoesPoseGraphTopologyMatch(NewSnapshot->PoseGraph))
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Error,
            TEXT("Lua PoseGraph topology was not prepared before parallel update. Layer=%s Generation=%d TopologySerial=%d"),
            *LayerName.ToString(),
            NewSnapshot->PoseGraph.Generation,
            NewSnapshot->PoseGraph.TopologySerial);
        CachedSnapshot = nullptr;
        return;
    }

    const bool bOutputChanged = NewSnapshot->PoseGraph.OutputSerial != LastOutputSerial;
    if (bOutputChanged && NewSnapshot->bUseInertialization)
    {
        UE::Anim::IInertializationRequester* InertializationRequester = Context.GetMessage<UE::Anim::IInertializationRequester>();
        if (InertializationRequester)
        {
            InertializationRequester->RequestInertialization(NewSnapshot->InertialBlendTime);
            InertializationRequester->AddDebugRecord(*Context.AnimInstanceProxy, Context.GetCurrentNodeId());
            bWarnedMissingInertializationRequester = false;
        }
        else if (!bWarnedMissingInertializationRequester)
        {
            UE_LOG(LogSekiroAnimBlueprintExt, Warning,
                TEXT("Lua animation requested inertialization without a downstream Inertialization node. Layer=%s State=%s"),
                *LayerName.ToString(),
                *NewSnapshot->CurrentStateName.ToString());
            bWarnedMissingInertializationRequester = true;
        }
    }

    for (int32 NodeIndex = 0; NodeIndex < NewSnapshot->PoseGraph.SequencePlayers.Num(); ++NodeIndex)
    {
        SynchronizePoseGraphNode(NodeIndex, NewSnapshot->PoseGraph.SequencePlayers[NodeIndex]);
    }

    UpdateActiveTransitions(*NewSnapshot, Context.GetDeltaTime());
    BuildPoseGraphNodeWeights(*NewSnapshot);

    // 共享叶节点按聚合权重只 Update 一次，Notify、SyncMarker 与 Root Motion 均由原生 TickRecord 处理。
    for (int32 NodeIndex = 0; NodeIndex < PoseGraphSequencePlayers.Num(); ++NodeIndex)
    {
        const float NodeWeight = PoseGraphSequencePlayerWeights.IsValidIndex(NodeIndex)
            ? PoseGraphSequencePlayerWeights[NodeIndex]
            : 0.0f;
        if (NodeWeight <= UE_KINDA_SMALL_NUMBER) continue;

        PoseGraphSequencePlayers[NodeIndex].Update_AnyThread(Context.FractionalWeight(NodeWeight));
    }
}

void FAnimNode_SekiroLuaStateMachine::Evaluate_AnyThread(FPoseContext& Output)
{
    if (!CachedSnapshot || !CachedSnapshot->PoseGraph.bHasOutputPose)
    {
        Output.ResetToRefPose();
        return;
    }

    const int32 CurrentStateResultIndex = FindPoseGraphStateResultIndex(CachedSnapshot->PoseGraph.CurrentPose);
    if (!StatePoseLinks.IsValidIndex(CurrentStateResultIndex))
    {
        Output.ResetToRefPose();
        return;
    }

    if (ActiveTransitionCount <= 0)
    {
        StatePoseLinks[CurrentStateResultIndex].Evaluate(Output);
        return;
    }

    const FSekiroLuaActiveTransition& FirstTransition = ActiveTransitionSlots[0];
    const int32 FirstSourceIndex = FindPoseGraphStateResultIndex(FirstTransition.PreviousPose);
    if (!StatePoseLinks.IsValidIndex(FirstSourceIndex))
    {
        StatePoseLinks[CurrentStateResultIndex].Evaluate(Output);
        return;
    }

    // 两个固定栈上下文覆盖任意长度过渡链，避免按活动状态 MakeUnique。
    FPoseContext AccumulatedPose(Output);
    FPoseContext NextPose(Output);
    StatePoseLinks[FirstSourceIndex].Evaluate(AccumulatedPose);

    for (int32 TransitionIndex = 0; TransitionIndex < ActiveTransitionCount; ++TransitionIndex)
    {
        const FSekiroLuaActiveTransition& Transition = ActiveTransitionSlots[TransitionIndex];
        const int32 NextStateResultIndex = FindPoseGraphStateResultIndex(Transition.NextPose);
        if (!StatePoseLinks.IsValidIndex(NextStateResultIndex)) continue;

        StatePoseLinks[NextStateResultIndex].Evaluate(NextPose);
        const FAnimationPoseData AccumulatedPoseData(AccumulatedPose);
        const FAnimationPoseData NextPoseData(NextPose);
        FAnimationPoseData OutputPoseData(Output);
        FAnimationRuntime::BlendTwoPosesTogether(
            AccumulatedPoseData,
            NextPoseData,
            1.0f - FMath::Clamp(Transition.Alpha, 0.0f, 1.0f),
            OutputPoseData);

        if (TransitionIndex + 1 < ActiveTransitionCount)
        {
            AccumulatedPose = Output;
        }
    }

    Output.Pose.NormalizeRotations();
}

void FAnimNode_SekiroLuaStateMachine::GatherDebugData(FNodeDebugData& DebugData)
{
    const FString DebugLine = CachedSnapshot
        ? FString::Printf(
            TEXT("Sekiro Lua PoseGraph Layer=%s Generation=%d Topology=%d Output=%d ActiveTransitions=%d"),
            *LayerName.ToString(),
            PoseGraphGeneration,
            PoseGraphTopologySerial,
            LastOutputSerial,
            ActiveTransitionCount)
        : FString::Printf(TEXT("Sekiro Lua PoseGraph Layer=%s NoSnapshot"), *LayerName.ToString());
    DebugData.AddDebugItem(DebugLine);

    for (int32 StateResultIndex = 0; StateResultIndex < PoseGraphStateResults.Num(); ++StateResultIndex)
    {
        const float StateWeight = PoseGraphStateResultWeights.IsValidIndex(StateResultIndex)
            ? PoseGraphStateResultWeights[StateResultIndex]
            : 0.0f;
        if (StateWeight <= UE_KINDA_SMALL_NUMBER || !StatePoseLinks.IsValidIndex(StateResultIndex)) continue;

        StatePoseLinks[StateResultIndex].GatherDebugData(DebugData.BranchFlow(StateWeight));
    }

    for (int32 NodeIndex = 0; NodeIndex < PoseGraphSequencePlayers.Num(); ++NodeIndex)
    {
        const float NodeWeight = PoseGraphSequencePlayerWeights.IsValidIndex(NodeIndex)
            ? PoseGraphSequencePlayerWeights[NodeIndex]
            : 0.0f;
        if (NodeWeight <= UE_KINDA_SMALL_NUMBER) continue;

        const UAnimSequenceBase* Sequence = PoseGraphSequencePlayers[NodeIndex].GetSequence();
        const FName AnimationName = CachedSnapshot && CachedSnapshot->PoseGraph.SequencePlayers.IsValidIndex(NodeIndex)
            ? CachedSnapshot->PoseGraph.SequencePlayers[NodeIndex].AnimationName
            : NAME_None;
        DebugData.AddDebugItem(FString::Printf(
            TEXT("SequencePlayer NodeId=%d Asset=%s Time=%.3f Weight=%.1f%%"),
            PoseGraphSequencePlayerNodeIds[NodeIndex],
            *GetSekiroLuaAnimDebugAssetLabel(Sequence, AnimationName),
            PoseGraphSequencePlayers[NodeIndex].GetAccumulatedTime(),
            NodeWeight * 100.0f));
    }
}

void FAnimNode_SekiroLuaStateMachine::PreparePoseGraphTopology(
    FAnimInstanceProxy* AnimInstanceProxy,
    const FSekiroLuaPoseGraphSnapshot& PoseGraph)
{
    if (DoesPoseGraphTopologyMatch(PoseGraph)) return;

    const bool bGenerationChanged = PoseGraphGeneration != PoseGraph.Generation;
    TArray<FSekiroLuaSequencePlayerNode_Standalone> PreviousPlayers = MoveTemp(PoseGraphSequencePlayers);
    TArray<int32> PreviousPlayerNodeIds = MoveTemp(PoseGraphSequencePlayerNodeIds);
    TArray<int32> PreviousResetSerials = MoveTemp(PoseGraphResetSerials);

    PoseGraphSequencePlayers.Reset(PoseGraph.SequencePlayers.Num());
    PoseGraphSequencePlayerNodeIds.Reset(PoseGraph.SequencePlayers.Num());
    PoseGraphResetSerials.Reset(PoseGraph.SequencePlayers.Num());
    TArray<bool> NewPlayerFlags;
    NewPlayerFlags.Init(false, PoseGraph.SequencePlayers.Num());

    for (int32 NodeIndex = 0; NodeIndex < PoseGraph.SequencePlayers.Num(); ++NodeIndex)
    {
        const FSekiroLuaSequencePlayerSnapshot& NodeSnapshot = PoseGraph.SequencePlayers[NodeIndex];
        const int32 PreviousNodeIndex = bGenerationChanged
            ? INDEX_NONE
            : PreviousPlayerNodeIds.Find(NodeSnapshot.PoseLink.NodeId);
        if (PreviousPlayers.IsValidIndex(PreviousNodeIndex))
        {
            PoseGraphSequencePlayers.Add(MoveTemp(PreviousPlayers[PreviousNodeIndex]));
            PoseGraphResetSerials.Add(PreviousResetSerials.IsValidIndex(PreviousNodeIndex)
                ? PreviousResetSerials[PreviousNodeIndex]
                : INDEX_NONE);
        }
        else
        {
            FSekiroLuaSequencePlayerNode_Standalone& NewPlayer = PoseGraphSequencePlayers.AddDefaulted_GetRef();
            NewPlayer.SetSequence(NodeSnapshot.Sequence.Get());
            NewPlayer.SetLoopAnimation(NodeSnapshot.bLoop);
            NewPlayer.SetStartPosition(NodeSnapshot.PlaybackTargetTime);
            NewPlayer.SetPlayRate(NodeSnapshot.PlayRate);
            PoseGraphResetSerials.Add(NodeSnapshot.PlaybackResetSerial);
            NewPlayerFlags[NodeIndex] = true;
        }
        PoseGraphSequencePlayerNodeIds.Add(NodeSnapshot.PoseLink.NodeId);
    }

    PoseGraphStateResults.Reset(PoseGraph.StateResults.Num());
    StatePoseLinks.Reset(PoseGraph.StateResults.Num());
    PoseGraphStateResultNodeIds.Reset(PoseGraph.StateResults.Num());
    PoseGraphStateResultInputs.Reset(PoseGraph.StateResults.Num());
    for (const FSekiroLuaStateResultSnapshot& StateResultSnapshot : PoseGraph.StateResults)
    {
        FSekiroLuaStateResultNode_Standalone& StateResultNode = PoseGraphStateResults.AddDefaulted_GetRef();
        StateResultNode.NodeName = StateResultSnapshot.NodeName;
        StateResultNode.StateName = StateResultSnapshot.StateName;
        StateResultNode.InputNode = StateResultSnapshot.InputNode;
        StatePoseLinks.AddDefaulted();
        PoseGraphStateResultNodeIds.Add(StateResultSnapshot.PoseLink.NodeId);
        PoseGraphStateResultInputs.Add(StateResultSnapshot.InputNode);
    }

    PoseGraphGeneration = PoseGraph.Generation;
    PoseGraphTopologySerial = PoseGraph.TopologySerial;
    RebuildPoseGraphLinks();

    const int32 StateResultCount = PoseGraphStateResults.Num();
    const int32 SequencePlayerCount = PoseGraphSequencePlayers.Num();
    const int32 TransitionCapacity = FMath::Max(SekiroLuaMinimumTransitionCapacity, StateResultCount * 4);
    if (ActiveTransitionSlots.Num() < TransitionCapacity)
    {
        ActiveTransitionSlots.SetNum(TransitionCapacity);
    }
    PoseGraphStateResultWeights.SetNumZeroed(StateResultCount);
    PoseGraphStateResultRootMotionRotationModes.SetNum(StateResultCount);
    PoseGraphSequencePlayerWeights.SetNumZeroed(SequencePlayerCount);
    PoseGraphSequencePlayerRootMotionRotationModes.SetNum(SequencePlayerCount);
    DominantStateResultWeights.SetNumZeroed(SequencePlayerCount);

    if (bGenerationChanged)
    {
        ResetActiveTransitions();
        LastOutputSerial = PoseGraph.OutputSerial;
        LastPublishedCurrentPose = PoseGraph.CurrentPose;
    }

    if (bTopologyInitialized && AnimInstanceProxy)
    {
        FAnimationInitializeContext InitializeContext(AnimInstanceProxy);
        FAnimationCacheBonesContext CacheBonesContext(AnimInstanceProxy);
        const bool bCacheBones = AnimInstanceProxy->GetCachedBonesCounter().HasEverBeenUpdated();
        for (int32 NodeIndex = 0; NodeIndex < PoseGraphSequencePlayers.Num(); ++NodeIndex)
        {
            if (!NewPlayerFlags[NodeIndex]) continue;

            PoseGraphSequencePlayers[NodeIndex].Initialize_AnyThread(InitializeContext);
            if (bCacheBones)
            {
                PoseGraphSequencePlayers[NodeIndex].CacheBones_AnyThread(CacheBonesContext);
            }
        }
    }
}

void FAnimNode_SekiroLuaStateMachine::CollectRuntimeState(FSekiroLuaPoseGraphRuntimeState& OutRuntimeState) const
{
    OutRuntimeState = FSekiroLuaPoseGraphRuntimeState();
    OutRuntimeState.LayerName = LayerName;
    OutRuntimeState.Generation = PoseGraphGeneration;
    OutRuntimeState.DominantRootMotionRotationMode = DominantRootMotionRotationMode;
    OutRuntimeState.DominantRootMotionWeight = DominantRootMotionWeight;

    OutRuntimeState.SequencePlayers.Reserve(PoseGraphSequencePlayers.Num());
    for (int32 NodeIndex = 0; NodeIndex < PoseGraphSequencePlayers.Num(); ++NodeIndex)
    {
        FSekiroLuaSequencePlayerRuntimeState& PlayerState = OutRuntimeState.SequencePlayers.AddDefaulted_GetRef();
        PlayerState.PoseLink.NodeId = PoseGraphSequencePlayerNodeIds[NodeIndex];
        PlayerState.PoseLink.Generation = PoseGraphGeneration;
        PlayerState.CurrentTime = PoseGraphSequencePlayers[NodeIndex].GetAccumulatedTime();

        const float NodeWeight = PoseGraphSequencePlayerWeights.IsValidIndex(NodeIndex)
            ? PoseGraphSequencePlayerWeights[NodeIndex]
            : 0.0f;
        if (NodeWeight > UE_KINDA_SMALL_NUMBER)
        {
            OutRuntimeState.ActiveNodeIds.AddUnique(PlayerState.PoseLink.NodeId);
        }
    }

    for (int32 StateResultIndex = 0; StateResultIndex < PoseGraphStateResults.Num(); ++StateResultIndex)
    {
        const float StateWeight = PoseGraphStateResultWeights.IsValidIndex(StateResultIndex)
            ? PoseGraphStateResultWeights[StateResultIndex]
            : 0.0f;
        if (StateWeight > UE_KINDA_SMALL_NUMBER)
        {
            OutRuntimeState.ActiveNodeIds.AddUnique(PoseGraphStateResultNodeIds[StateResultIndex]);
        }
    }

    if (ActiveTransitionCount > 0)
    {
        const FSekiroLuaActiveTransition& LatestTransition = ActiveTransitionSlots[ActiveTransitionCount - 1];
        OutRuntimeState.PreviousPose = LatestTransition.PreviousPose;
        OutRuntimeState.TransitionTime = LatestTransition.CrossfadeDuration;
        OutRuntimeState.TransitionElapsedTime = LatestTransition.ElapsedTime;
        OutRuntimeState.TransitionAlpha = LatestTransition.Alpha;
    }
}

bool FAnimNode_SekiroLuaStateMachine::DoesPoseGraphTopologyMatch(const FSekiroLuaPoseGraphSnapshot& PoseGraph) const
{
    if (PoseGraphGeneration != PoseGraph.Generation
        || PoseGraphTopologySerial != PoseGraph.TopologySerial
        || PoseGraphSequencePlayers.Num() != PoseGraph.SequencePlayers.Num()
        || PoseGraphStateResults.Num() != PoseGraph.StateResults.Num())
    {
        return false;
    }

    for (int32 NodeIndex = 0; NodeIndex < PoseGraph.SequencePlayers.Num(); ++NodeIndex)
    {
        if (!PoseGraphSequencePlayerNodeIds.IsValidIndex(NodeIndex)
            || PoseGraphSequencePlayerNodeIds[NodeIndex] != PoseGraph.SequencePlayers[NodeIndex].PoseLink.NodeId)
        {
            return false;
        }
    }

    for (int32 StateResultIndex = 0; StateResultIndex < PoseGraph.StateResults.Num(); ++StateResultIndex)
    {
        const FSekiroLuaStateResultSnapshot& StateResultSnapshot = PoseGraph.StateResults[StateResultIndex];
        if (!PoseGraphStateResultNodeIds.IsValidIndex(StateResultIndex)
            || !PoseGraphStateResultInputs.IsValidIndex(StateResultIndex)
            || PoseGraphStateResultNodeIds[StateResultIndex] != StateResultSnapshot.PoseLink.NodeId
            || PoseGraphStateResultInputs[StateResultIndex].PoseLink != StateResultSnapshot.InputNode.PoseLink
            || PoseGraphStateResultInputs[StateResultIndex].NodeType != StateResultSnapshot.InputNode.NodeType
            || PoseGraphStateResults[StateResultIndex].NodeName != StateResultSnapshot.NodeName
            || PoseGraphStateResults[StateResultIndex].StateName != StateResultSnapshot.StateName)
        {
            return false;
        }
    }

    return true;
}

void FAnimNode_SekiroLuaStateMachine::RebuildPoseGraphLinks()
{
    for (int32 StateResultIndex = 0; StateResultIndex < PoseGraphStateResults.Num(); ++StateResultIndex)
    {
        FSekiroLuaStateResultNode_Standalone& StateResultNode = PoseGraphStateResults[StateResultIndex];
        const FSekiroLuaPoseNodeReference& InputNode = PoseGraphStateResultInputs[StateResultIndex];
        if (InputNode.NodeType == ESekiroLuaPoseNodeType::SequencePlayer)
        {
            const int32 SequencePlayerIndex = FindPoseGraphSequencePlayerIndex(InputNode.PoseLink);
            if (PoseGraphSequencePlayers.IsValidIndex(SequencePlayerIndex))
            {
                StateResultNode.Result.SetLinkNode(&PoseGraphSequencePlayers[SequencePlayerIndex]);
            }
        }
        StatePoseLinks[StateResultIndex].SetLinkNode(&StateResultNode);
    }
}

void FAnimNode_SekiroLuaStateMachine::SynchronizePoseGraphNode(
    int32 NodeIndex,
    const FSekiroLuaSequencePlayerSnapshot& Snapshot)
{
    if (!PoseGraphSequencePlayers.IsValidIndex(NodeIndex)
        || !PoseGraphResetSerials.IsValidIndex(NodeIndex))
    {
        return;
    }

    FSekiroLuaSequencePlayerNode_Standalone& PlayerNode = PoseGraphSequencePlayers[NodeIndex];
    UAnimSequenceBase* SequenceAsset = Snapshot.Sequence.Get();
    const bool bAssetChanged = PlayerNode.GetSequence() != SequenceAsset;
    const bool bPlaybackReset = PoseGraphResetSerials[NodeIndex] != Snapshot.PlaybackResetSerial;
    PlayerNode.SetSequence(SequenceAsset);
    PlayerNode.SetLoopAnimation(Snapshot.bLoop);
    PlayerNode.SetPlayRate(Snapshot.PlayRate);
    if (bAssetChanged || bPlaybackReset)
    {
        PlayerNode.ResetPlayback(Snapshot.PlaybackTargetTime);
        PoseGraphResetSerials[NodeIndex] = Snapshot.PlaybackResetSerial;
    }
}

void FAnimNode_SekiroLuaStateMachine::UpdateActiveTransitions(
    const FSekiroLuaAnimSnapshot& NewSnapshot,
    float DeltaSeconds)
{
    for (int32 TransitionIndex = 0; TransitionIndex < ActiveTransitionCount; ++TransitionIndex)
    {
        ActiveTransitionSlots[TransitionIndex].Advance(DeltaSeconds);
    }
    RemoveCompletedTransitions();

    const FSekiroLuaPoseGraphSnapshot& PoseGraph = NewSnapshot.PoseGraph;
    if (PoseGraph.OutputSerial == LastOutputSerial) return;

    if (NewSnapshot.bUseInertialization
        || !PoseGraph.PreviousPose.IsValid()
        || PoseGraph.TransitionTime <= UE_KINDA_SMALL_NUMBER)
    {
        ResetActiveTransitions();
    }
    else if (ActiveTransitionCount < ActiveTransitionSlots.Num())
    {
        FSekiroLuaActiveTransition& NewTransition = ActiveTransitionSlots[ActiveTransitionCount++];
        NewTransition = FSekiroLuaActiveTransition();
        NewTransition.PreviousPose = PoseGraph.PreviousPose;
        NewTransition.NextPose = PoseGraph.CurrentPose;
        NewTransition.CrossfadeDuration = FMath::Max(0.0f, PoseGraph.TransitionTime);
        NewTransition.PreviousRootMotionRotationMode = NewSnapshot.PreviousRootMotionRotationMode;
        NewTransition.NextRootMotionRotationMode = NewSnapshot.CurrentRootMotionRotationMode;
        NewTransition.Alpha = 0.0f;
    }
    else
    {
        UE_LOG(LogSekiroAnimBlueprintExt, Error,
            TEXT("Lua PoseGraph active transition capacity exceeded. Layer=%s Capacity=%d"),
            *LayerName.ToString(),
            ActiveTransitionSlots.Num());
    }

    LastOutputSerial = PoseGraph.OutputSerial;
    LastPublishedCurrentPose = PoseGraph.CurrentPose;
}

void FAnimNode_SekiroLuaStateMachine::BuildPoseGraphNodeWeights(const FSekiroLuaAnimSnapshot& NewSnapshot)
{
    for (float& StateWeight : PoseGraphStateResultWeights)
    {
        StateWeight = 0.0f;
    }
    for (ESekiroLuaRootMotionRotationMode& RotationMode : PoseGraphStateResultRootMotionRotationModes)
    {
        RotationMode = ESekiroLuaRootMotionRotationMode::Extract;
    }
    for (float& PlayerWeight : PoseGraphSequencePlayerWeights)
    {
        PlayerWeight = 0.0f;
    }
    for (ESekiroLuaRootMotionRotationMode& RotationMode : PoseGraphSequencePlayerRootMotionRotationModes)
    {
        RotationMode = ESekiroLuaRootMotionRotationMode::Extract;
    }
    for (float& DominantWeight : DominantStateResultWeights)
    {
        DominantWeight = 0.0f;
    }

    if (ActiveTransitionCount == 0)
    {
        const int32 CurrentStateResultIndex = FindPoseGraphStateResultIndex(NewSnapshot.PoseGraph.CurrentPose);
        if (PoseGraphStateResultWeights.IsValidIndex(CurrentStateResultIndex))
        {
            PoseGraphStateResultWeights[CurrentStateResultIndex] = 1.0f;
            PoseGraphStateResultRootMotionRotationModes[CurrentStateResultIndex] = NewSnapshot.CurrentRootMotionRotationMode;
        }
    }
    else
    {
        const FSekiroLuaActiveTransition& FirstTransition = ActiveTransitionSlots[0];
        const int32 FirstSourceIndex = FindPoseGraphStateResultIndex(FirstTransition.PreviousPose);
        if (PoseGraphStateResultWeights.IsValidIndex(FirstSourceIndex))
        {
            PoseGraphStateResultWeights[FirstSourceIndex] = 1.0f;
            PoseGraphStateResultRootMotionRotationModes[FirstSourceIndex] = FirstTransition.PreviousRootMotionRotationMode;
        }

        for (int32 TransitionIndex = 0; TransitionIndex < ActiveTransitionCount; ++TransitionIndex)
        {
            const FSekiroLuaActiveTransition& Transition = ActiveTransitionSlots[TransitionIndex];
            const float SourceWeight = 1.0f - FMath::Clamp(Transition.Alpha, 0.0f, 1.0f);
            for (float& StateWeight : PoseGraphStateResultWeights)
            {
                StateWeight *= SourceWeight;
            }

            const int32 NextStateResultIndex = FindPoseGraphStateResultIndex(Transition.NextPose);
            if (PoseGraphStateResultWeights.IsValidIndex(NextStateResultIndex))
            {
                PoseGraphStateResultWeights[NextStateResultIndex] += Transition.Alpha;
                PoseGraphStateResultRootMotionRotationModes[NextStateResultIndex] = Transition.NextRootMotionRotationMode;
            }
        }
    }

    DominantRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract;
    DominantRootMotionWeight = 0.0f;
    for (int32 StateResultIndex = 0; StateResultIndex < PoseGraphStateResults.Num(); ++StateResultIndex)
    {
        const float StateResultWeight = PoseGraphStateResultWeights[StateResultIndex];
        if (StateResultWeight <= UE_KINDA_SMALL_NUMBER) continue;

        FSekiroLuaPoseLink StateResultPoseLink;
        StateResultPoseLink.NodeId = PoseGraphStateResultNodeIds[StateResultIndex];
        StateResultPoseLink.Generation = PoseGraphGeneration;
        const int32 SequencePlayerIndex = FindPoseGraphStateResultInputSequencePlayerIndex(StateResultPoseLink);
        if (!PoseGraphSequencePlayerWeights.IsValidIndex(SequencePlayerIndex)) continue;

        PoseGraphSequencePlayerWeights[SequencePlayerIndex] += StateResultWeight;
        if (StateResultWeight > DominantStateResultWeights[SequencePlayerIndex])
        {
            DominantStateResultWeights[SequencePlayerIndex] = StateResultWeight;
            PoseGraphSequencePlayerRootMotionRotationModes[SequencePlayerIndex] =
                PoseGraphStateResultRootMotionRotationModes[StateResultIndex];
        }
    }

    for (int32 NodeIndex = 0; NodeIndex < PoseGraphSequencePlayerWeights.Num(); ++NodeIndex)
    {
        if (PoseGraphSequencePlayerWeights[NodeIndex] <= DominantRootMotionWeight) continue;

        DominantRootMotionWeight = PoseGraphSequencePlayerWeights[NodeIndex];
        DominantRootMotionRotationMode = PoseGraphSequencePlayerRootMotionRotationModes[NodeIndex];
    }
}

void FAnimNode_SekiroLuaStateMachine::ResetActiveTransitions()
{
    ActiveTransitionCount = 0;
    for (float& StateWeight : PoseGraphStateResultWeights)
    {
        StateWeight = 0.0f;
    }
    for (float& PlayerWeight : PoseGraphSequencePlayerWeights)
    {
        PlayerWeight = 0.0f;
    }
}

void FAnimNode_SekiroLuaStateMachine::RemoveCompletedTransitions()
{
    int32 CompletedPrefixCount = 0;
    while (CompletedPrefixCount < ActiveTransitionCount
        && ActiveTransitionSlots[CompletedPrefixCount].IsComplete())
    {
        ++CompletedPrefixCount;
    }
    if (CompletedPrefixCount <= 0) return;

    const int32 RemainingCount = ActiveTransitionCount - CompletedPrefixCount;
    for (int32 TransitionIndex = 0; TransitionIndex < RemainingCount; ++TransitionIndex)
    {
        ActiveTransitionSlots[TransitionIndex] = ActiveTransitionSlots[TransitionIndex + CompletedPrefixCount];
    }
    ActiveTransitionCount = RemainingCount;
}

int32 FAnimNode_SekiroLuaStateMachine::FindPoseGraphSequencePlayerIndex(const FSekiroLuaPoseLink& PoseLink) const
{
    if (!PoseLink.IsValid() || PoseLink.Generation != PoseGraphGeneration) return INDEX_NONE;
    return PoseGraphSequencePlayerNodeIds.Find(PoseLink.NodeId);
}

int32 FAnimNode_SekiroLuaStateMachine::FindPoseGraphStateResultIndex(const FSekiroLuaPoseLink& PoseLink) const
{
    if (!PoseLink.IsValid() || PoseLink.Generation != PoseGraphGeneration) return INDEX_NONE;
    return PoseGraphStateResultNodeIds.Find(PoseLink.NodeId);
}

int32 FAnimNode_SekiroLuaStateMachine::FindPoseGraphStateResultInputSequencePlayerIndex(
    const FSekiroLuaPoseLink& PoseLink) const
{
    const int32 StateResultIndex = FindPoseGraphStateResultIndex(PoseLink);
    if (!PoseGraphStateResultInputs.IsValidIndex(StateResultIndex)) return INDEX_NONE;

    const FSekiroLuaPoseNodeReference& InputNode = PoseGraphStateResultInputs[StateResultIndex];
    if (InputNode.NodeType != ESekiroLuaPoseNodeType::SequencePlayer) return INDEX_NONE;
    return FindPoseGraphSequencePlayerIndex(InputNode.PoseLink);
}
