#include "LuaAnimGraphNodeRegistry.h"

namespace LuaAnimGraphNodeRegistryPrivate
{
    /**
     * 构造插件内置 NodeType 的完整权威契约快照。
     * 本函数仅创建值类型和软类路径，不加载 UObject，可在任意线程调用。
     *
     * @return 按稳定注册顺序排列的节点契约数组，调用方取得独立值。
     */
    TArray<FLuaAnimIRNodeContract> BuildContracts()
    {
        TArray<FLuaAnimIRNodeContract> Contracts;
        Contracts.Reserve(25);

        FLuaAnimIRNodeContract& OutputPose = Contracts.AddDefaulted_GetRef();
        OutputPose.NodeType = LuaAnimGraphIRNames::OutputPoseNode;
        OutputPose.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Root"));
        OutputPose.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        OutputPose.RootRole = ELuaAnimIRNodeRootRole::GraphRoot;
        FLuaAnimIRPinContract& OutputResult = OutputPose.Pins.AddDefaulted_GetRef();
        OutputResult.Name = TEXT("Result");
        OutputResult.Direction = ELuaAnimIRPinDirection::Input;
        OutputResult.DataType = LuaAnimGraphIRNames::PoseData;

        FLuaAnimIRNodeContract& StateResult = Contracts.AddDefaulted_GetRef();
        StateResult.NodeType = LuaAnimGraphIRNames::StateResultNode;
        StateResult.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateResult"));
        StateResult.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        StateResult.RootRole = ELuaAnimIRNodeRootRole::GraphRoot;
        FLuaAnimIRPinContract& StateResultPin = StateResult.Pins.AddDefaulted_GetRef();
        StateResultPin.Name = TEXT("Result");
        StateResultPin.Direction = ELuaAnimIRPinDirection::Input;
        StateResultPin.DataType = LuaAnimGraphIRNames::PoseData;

        FLuaAnimIRNodeContract& SequencePlayer = Contracts.AddDefaulted_GetRef();
        SequencePlayer.NodeType = LuaAnimGraphIRNames::SequencePlayerNode;
        SequencePlayer.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
        SequencePlayer.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        SequencePlayer.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& SequencePose = SequencePlayer.Pins.AddDefaulted_GetRef();
        SequencePose.Name = TEXT("Pose");
        SequencePose.Direction = ELuaAnimIRPinDirection::Output;
        SequencePose.DataType = LuaAnimGraphIRNames::PoseData;
        SequencePose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& Sequence = SequencePlayer.Properties.AddDefaulted_GetRef();
        Sequence.Name = TEXT("Sequence");
        Sequence.ValueType = ELuaAnimIRValueType::SoftObjectPath;
        Sequence.bRequired = true;
        FLuaAnimIRPropertyContract& LoopAnimation = SequencePlayer.Properties.AddDefaulted_GetRef();
        LoopAnimation.Name = TEXT("bLoopAnimation");
        LoopAnimation.ValueType = ELuaAnimIRValueType::Bool;
        FLuaAnimIRPropertyContract& PlayRate = SequencePlayer.Properties.AddDefaulted_GetRef();
        PlayRate.Name = TEXT("PlayRate");
        PlayRate.ValueType = ELuaAnimIRValueType::Float;
        FLuaAnimIRPropertyContract& StartPosition = SequencePlayer.Properties.AddDefaulted_GetRef();
        StartPosition.Name = TEXT("StartPosition");
        StartPosition.ValueType = ELuaAnimIRValueType::Float;
        FLuaAnimIRPropertyContract& GroupName = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupName.Name = TEXT("GroupName");
        GroupName.ValueType = ELuaAnimIRValueType::Name;
        FLuaAnimIRPropertyContract& GroupRole = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupRole.Name = TEXT("GroupRole");
        GroupRole.ValueType = ELuaAnimIRValueType::Enum;
        FLuaAnimIRPropertyContract& GroupMethod = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupMethod.Name = TEXT("GroupMethod");
        GroupMethod.ValueType = ELuaAnimIRValueType::Enum;

        FLuaAnimIRNodeContract& StateMachine = Contracts.AddDefaulted_GetRef();
        StateMachine.NodeType = LuaAnimGraphIRNames::StateMachineNode;
        StateMachine.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateMachine"));
        StateMachine.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        StateMachine.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        StateMachine.OwnedGraphPolicy = ELuaAnimIROwnedGraphPolicy::Required;
        StateMachine.OwnedGraphType = LuaAnimGraphIRNames::StateMachineGraph;
        FLuaAnimIRPinContract& StateMachinePose = StateMachine.Pins.AddDefaulted_GetRef();
        StateMachinePose.Name = TEXT("Pose");
        StateMachinePose.Direction = ELuaAnimIRPinDirection::Output;
        StateMachinePose.DataType = LuaAnimGraphIRNames::PoseData;
        StateMachinePose.bAllowMultipleConnections = true;

        FLuaAnimIRNodeContract& Inertialization = Contracts.AddDefaulted_GetRef();
        Inertialization.NodeType = LuaAnimGraphIRNames::InertializationNode;
        Inertialization.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Inertialization"));
        Inertialization.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        Inertialization.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& InertialSource = Inertialization.Pins.AddDefaulted_GetRef();
        InertialSource.Name = TEXT("Source");
        InertialSource.Direction = ELuaAnimIRPinDirection::Input;
        InertialSource.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPinContract& InertialPose = Inertialization.Pins.AddDefaulted_GetRef();
        InertialPose.Name = TEXT("Pose");
        InertialPose.Direction = ELuaAnimIRPinDirection::Output;
        InertialPose.DataType = LuaAnimGraphIRNames::PoseData;
        InertialPose.bAllowMultipleConnections = true;

        FLuaAnimIRNodeContract& LocalToComponent = Contracts.AddDefaulted_GetRef();
        LocalToComponent.NodeType = LuaAnimGraphIRNames::LocalToComponentSpaceNode;
        LocalToComponent.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LocalToComponentSpace"));
        LocalToComponent.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LocalToComponent.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& LocalPoseInput = LocalToComponent.Pins.AddDefaulted_GetRef();
        LocalPoseInput.Name = TEXT("LocalPose");
        LocalPoseInput.Direction = ELuaAnimIRPinDirection::Input;
        LocalPoseInput.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPinContract& ComponentPoseOutput = LocalToComponent.Pins.AddDefaulted_GetRef();
        ComponentPoseOutput.Name = TEXT("ComponentPose");
        ComponentPoseOutput.Direction = ELuaAnimIRPinDirection::Output;
        ComponentPoseOutput.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        ComponentPoseOutput.bAllowMultipleConnections = true;

        FLuaAnimIRNodeContract& ComponentToLocal = Contracts.AddDefaulted_GetRef();
        ComponentToLocal.NodeType = LuaAnimGraphIRNames::ComponentToLocalSpaceNode;
        ComponentToLocal.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_ComponentToLocalSpace"));
        ComponentToLocal.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        ComponentToLocal.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& ComponentPoseInput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        ComponentPoseInput.Name = TEXT("ComponentPose");
        ComponentPoseInput.Direction = ELuaAnimIRPinDirection::Input;
        ComponentPoseInput.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FLuaAnimIRPinContract& LocalPoseOutput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        LocalPoseOutput.Name = TEXT("Pose");
        LocalPoseOutput.Direction = ELuaAnimIRPinDirection::Output;
        LocalPoseOutput.DataType = LuaAnimGraphIRNames::PoseData;
        LocalPoseOutput.bAllowMultipleConnections = true;

        FLuaAnimIRNodeContract& OrientationWarping = Contracts.AddDefaulted_GetRef();
        OrientationWarping.NodeType = LuaAnimGraphIRNames::OrientationWarpingNode;
        OrientationWarping.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_OrientationWarping"));
        OrientationWarping.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        OrientationWarping.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& OrientationComponentPose = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationComponentPose.Name = TEXT("ComponentPose");
        OrientationComponentPose.Direction = ELuaAnimIRPinDirection::Input;
        OrientationComponentPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FLuaAnimIRPinContract& OrientationAngle = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationAngle.Name = TEXT("OrientationAngle");
        OrientationAngle.Direction = ELuaAnimIRPinDirection::Input;
        OrientationAngle.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& OrientationLocomotionAngle =
            OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationLocomotionAngle.Name = TEXT("LocomotionAngle");
        OrientationLocomotionAngle.Direction = ELuaAnimIRPinDirection::Input;
        OrientationLocomotionAngle.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& OrientationAlpha = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationAlpha.Name = TEXT("Alpha");
        OrientationAlpha.Direction = ELuaAnimIRPinDirection::Input;
        OrientationAlpha.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& OrientationPose = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationPose.Name = TEXT("Pose");
        OrientationPose.Direction = ELuaAnimIRPinDirection::Output;
        OrientationPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        OrientationPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& OrientationSpineBones =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationSpineBones.Name = TEXT("SpineBones");
        OrientationSpineBones.ValueType = ELuaAnimIRValueType::String;
        OrientationSpineBones.bRequired = true;
        FLuaAnimIRPropertyContract& OrientationFootRoot =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationFootRoot.Name = TEXT("IKFootRootBone");
        OrientationFootRoot.ValueType = ELuaAnimIRValueType::Name;
        OrientationFootRoot.bRequired = true;
        FLuaAnimIRPropertyContract& OrientationFootBones =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationFootBones.Name = TEXT("IKFootBones");
        OrientationFootBones.ValueType = ELuaAnimIRValueType::String;
        OrientationFootBones.bRequired = true;
        FLuaAnimIRPropertyContract& OrientationRotationAxis =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationRotationAxis.Name = TEXT("RotationAxis");
        OrientationRotationAxis.ValueType = ELuaAnimIRValueType::Enum;
        FLuaAnimIRPropertyContract& OrientationDistribution =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationDistribution.Name = TEXT("DistributedBoneOrientationAlpha");
        OrientationDistribution.ValueType = ELuaAnimIRValueType::Float;
        FLuaAnimIRPropertyContract& OrientationInterpSpeed =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationInterpSpeed.Name = TEXT("RotationInterpSpeed");
        OrientationInterpSpeed.ValueType = ELuaAnimIRValueType::Float;
        FLuaAnimIRPropertyContract& OrientationMode =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationMode.Name = TEXT("Mode");
        OrientationMode.ValueType = ELuaAnimIRValueType::Enum;
        const TCHAR* OrientationFloatPropertyNames[] = {
            TEXT("MinRootMotionSpeedThreshold"),
            TEXT("LocomotionAngleDeltaThreshold"),
            TEXT("WarpingAlpha"),
            TEXT("OffsetAlpha"),
            TEXT("MaxOffsetAngle"),
        };
        for (const TCHAR* PropertyName : OrientationFloatPropertyNames)
        {
            FLuaAnimIRPropertyContract& Property =
                OrientationWarping.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Float;
        }

        FLuaAnimIRNodeContract& FootPlacement = Contracts.AddDefaulted_GetRef();
        FootPlacement.NodeType = LuaAnimGraphIRNames::FootPlacementNode;
        FootPlacement.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_FootPlacement"));
        FootPlacement.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        FootPlacement.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& FootPlacementComponentPose = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementComponentPose.Name = TEXT("ComponentPose");
        FootPlacementComponentPose.Direction = ELuaAnimIRPinDirection::Input;
        FootPlacementComponentPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FLuaAnimIRPinContract& FootPlacementAlpha = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementAlpha.Name = TEXT("Alpha");
        FootPlacementAlpha.Direction = ELuaAnimIRPinDirection::Input;
        FootPlacementAlpha.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& FootPlacementPose = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementPose.Name = TEXT("Pose");
        FootPlacementPose.Direction = ELuaAnimIRPinDirection::Output;
        FootPlacementPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FootPlacementPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& FootPlacementFootRoot = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementFootRoot.Name = TEXT("IKFootRootBone");
        FootPlacementFootRoot.ValueType = ELuaAnimIRValueType::Name;
        FootPlacementFootRoot.bRequired = true;
        FLuaAnimIRPropertyContract& FootPlacementPelvis = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementPelvis.Name = TEXT("PelvisBone");
        FootPlacementPelvis.ValueType = ELuaAnimIRValueType::Name;
        FootPlacementPelvis.bRequired = true;
        FLuaAnimIRPropertyContract& FootPlacementLegs = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementLegs.Name = TEXT("LegDefinitions");
        FootPlacementLegs.ValueType = ELuaAnimIRValueType::String;
        FootPlacementLegs.bRequired = true;
        FLuaAnimIRPropertyContract& FootPlacementSpeedMode = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementSpeedMode.Name = TEXT("PlantSpeedMode");
        FootPlacementSpeedMode.ValueType = ELuaAnimIRValueType::Enum;
        FLuaAnimIRPropertyContract& FootPlacementLockType = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementLockType.Name = TEXT("PlantLockType");
        FootPlacementLockType.ValueType = ELuaAnimIRValueType::Enum;
        const TCHAR* FootPlacementFloatPropertyNames[] = {
            TEXT("PelvisMaxOffset"),
            TEXT("PelvisHorizontalRebalancingWeight"),
            TEXT("PlantSpeedThreshold"),
            TEXT("PlantDistanceToGround"),
            TEXT("TraceStartOffset"),
            TEXT("TraceEndOffset"),
            TEXT("TraceSweepRadius"),
            TEXT("TraceMaxGroundPenetration"),
        };
        for (const TCHAR* PropertyName : FootPlacementFloatPropertyNames)
        {
            FLuaAnimIRPropertyContract& Property = FootPlacement.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Float;
        }
        FLuaAnimIRPropertyContract& FootPlacementTraceEnabled = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementTraceEnabled.Name = TEXT("bTraceEnabled");
        FootPlacementTraceEnabled.ValueType = ELuaAnimIRValueType::Bool;

        FLuaAnimIRNodeContract& LegIK = Contracts.AddDefaulted_GetRef();
        LegIK.NodeType = LuaAnimGraphIRNames::LegIKNode;
        LegIK.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LegIK"));
        LegIK.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LegIK.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& LegIKComponentPose = LegIK.Pins.AddDefaulted_GetRef();
        LegIKComponentPose.Name = TEXT("ComponentPose");
        LegIKComponentPose.Direction = ELuaAnimIRPinDirection::Input;
        LegIKComponentPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FLuaAnimIRPinContract& LegIKAlpha = LegIK.Pins.AddDefaulted_GetRef();
        LegIKAlpha.Name = TEXT("Alpha");
        LegIKAlpha.Direction = ELuaAnimIRPinDirection::Input;
        LegIKAlpha.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& LegIKPose = LegIK.Pins.AddDefaulted_GetRef();
        LegIKPose.Name = TEXT("Pose");
        LegIKPose.Direction = ELuaAnimIRPinDirection::Output;
        LegIKPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        LegIKPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& LegIKLegs = LegIK.Properties.AddDefaulted_GetRef();
        LegIKLegs.Name = TEXT("LegDefinitions");
        LegIKLegs.ValueType = ELuaAnimIRValueType::String;
        LegIKLegs.bRequired = true;
        FLuaAnimIRPropertyContract& LegIKReachPrecision = LegIK.Properties.AddDefaulted_GetRef();
        LegIKReachPrecision.Name = TEXT("ReachPrecision");
        LegIKReachPrecision.ValueType = ELuaAnimIRValueType::Float;
        FLuaAnimIRPropertyContract& LegIKMaxIterations = LegIK.Properties.AddDefaulted_GetRef();
        LegIKMaxIterations.Name = TEXT("MaxIterations");
        LegIKMaxIterations.ValueType = ELuaAnimIRValueType::Integer;

        FLuaAnimIRNodeContract& TwoBoneIK = Contracts.AddDefaulted_GetRef();
        TwoBoneIK.NodeType = LuaAnimGraphIRNames::TwoBoneIKNode;
        TwoBoneIK.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_TwoBoneIK"));
        TwoBoneIK.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        TwoBoneIK.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& TwoBoneIKComponentPose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKComponentPose.Name = TEXT("ComponentPose");
        TwoBoneIKComponentPose.Direction = ELuaAnimIRPinDirection::Input;
        TwoBoneIKComponentPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        FLuaAnimIRPinContract& TwoBoneIKAlpha = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKAlpha.Name = TEXT("Alpha");
        TwoBoneIKAlpha.Direction = ELuaAnimIRPinDirection::Input;
        TwoBoneIKAlpha.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& TwoBoneIKPose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKPose.Name = TEXT("Pose");
        TwoBoneIKPose.Direction = ELuaAnimIRPinDirection::Output;
        TwoBoneIKPose.DataType = LuaAnimGraphIRNames::ComponentPoseData;
        TwoBoneIKPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& TwoBoneIKBone = TwoBoneIK.Properties.AddDefaulted_GetRef();
        TwoBoneIKBone.Name = TEXT("IKBone");
        TwoBoneIKBone.ValueType = ELuaAnimIRValueType::Name;
        TwoBoneIKBone.bRequired = true;
        const TCHAR* TwoBoneIKEnumPropertyNames[] = {
            TEXT("EffectorLocationSpace"),
            TEXT("JointTargetLocationSpace"),
            TEXT("AlphaInputType"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKEnumPropertyNames)
        {
            FLuaAnimIRPropertyContract& Property =
                TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Enum;
        }
        const TCHAR* TwoBoneIKNamePropertyNames[] = {
            TEXT("EffectorTargetBoneName"),
            TEXT("EffectorTargetSocketName"),
            TEXT("JointTargetBoneName"),
            TEXT("JointTargetSocketName"),
            TEXT("AlphaCurveName"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKNamePropertyNames)
        {
            FLuaAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Name;
        }
        const TCHAR* TwoBoneIKFloatPropertyNames[] = {
            TEXT("EffectorLocationX"),
            TEXT("EffectorLocationY"),
            TEXT("EffectorLocationZ"),
            TEXT("JointTargetLocationX"),
            TEXT("JointTargetLocationY"),
            TEXT("JointTargetLocationZ"),
            TEXT("StartStretchRatio"),
            TEXT("MaxStretchScale"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKFloatPropertyNames)
        {
            FLuaAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Float;
        }
        const TCHAR* TwoBoneIKBoolPropertyNames[] = {
            TEXT("bTakeRotationFromEffectorSpace"),
            TEXT("bAllowStretching"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKBoolPropertyNames)
        {
            FLuaAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ELuaAnimIRValueType::Bool;
        }

        FLuaAnimIRNodeContract& SaveCachedPose = Contracts.AddDefaulted_GetRef();
        SaveCachedPose.NodeType = LuaAnimGraphIRNames::SaveCachedPoseNode;
        SaveCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SaveCachedPose"));
        SaveCachedPose.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        FLuaAnimIRPinContract& CachedPoseInput = SaveCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseInput.Name = TEXT("Pose");
        CachedPoseInput.Direction = ELuaAnimIRPinDirection::Input;
        CachedPoseInput.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPropertyContract& SaveCacheName = SaveCachedPose.Properties.AddDefaulted_GetRef();
        SaveCacheName.Name = TEXT("CacheName");
        SaveCacheName.ValueType = ELuaAnimIRValueType::String;
        SaveCacheName.bRequired = true;

        FLuaAnimIRNodeContract& UseCachedPose = Contracts.AddDefaulted_GetRef();
        UseCachedPose.NodeType = LuaAnimGraphIRNames::UseCachedPoseNode;
        UseCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_UseCachedPose"));
        UseCachedPose.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        FLuaAnimIRPinContract& CachedPoseOutput = UseCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseOutput.Name = TEXT("Pose");
        CachedPoseOutput.Direction = ELuaAnimIRPinDirection::Output;
        CachedPoseOutput.DataType = LuaAnimGraphIRNames::PoseData;
        CachedPoseOutput.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& UseCacheName = UseCachedPose.Properties.AddDefaulted_GetRef();
        UseCacheName.Name = TEXT("CacheName");
        UseCacheName.ValueType = ELuaAnimIRValueType::String;
        UseCacheName.bRequired = true;

        const FName GetterTypes[] = {
            LuaAnimGraphIRNames::BoolPropertyGetterNode,
            LuaAnimGraphIRNames::FloatPropertyGetterNode,
            LuaAnimGraphIRNames::BytePropertyGetterNode,
            LuaAnimGraphIRNames::EnumPropertyGetterNode,
        };
        const FName GetterDataTypes[] = {
            LuaAnimGraphIRNames::BoolData,
            LuaAnimGraphIRNames::FloatData,
            LuaAnimGraphIRNames::ByteData,
            LuaAnimGraphIRNames::EnumData,
        };
        for (int32 GetterIndex = 0; GetterIndex < UE_ARRAY_COUNT(GetterTypes); ++GetterIndex)
        {
            FLuaAnimIRNodeContract& Getter = Contracts.AddDefaulted_GetRef();
            Getter.NodeType = GetterTypes[GetterIndex];
            Getter.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/BlueprintGraph.K2Node_VariableGet"));
            Getter.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
            Getter.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
            FLuaAnimIRPinContract& ValuePin = Getter.Pins.AddDefaulted_GetRef();
            ValuePin.Name = TEXT("Value");
            ValuePin.Direction = ELuaAnimIRPinDirection::Output;
            ValuePin.DataType = GetterDataTypes[GetterIndex];
            ValuePin.bAllowMultipleConnections = true;
            FLuaAnimIRPropertyContract& PropertyName = Getter.Properties.AddDefaulted_GetRef();
            PropertyName.Name = TEXT("PropertyName");
            PropertyName.ValueType = ELuaAnimIRValueType::Name;
            PropertyName.bRequired = true;
        }

        FLuaAnimIRNodeContract& BlendByBool = Contracts.AddDefaulted_GetRef();
        BlendByBool.NodeType = LuaAnimGraphIRNames::BlendListByBoolNode;
        BlendByBool.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"));
        BlendByBool.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        BlendByBool.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        const TCHAR* BoolPoseNames[] = { TEXT("TruePose"), TEXT("FalsePose") };
        for (const TCHAR* PoseName : BoolPoseNames)
        {
            FLuaAnimIRPinContract& PosePin = BlendByBool.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ELuaAnimIRPinDirection::Input;
            PosePin.DataType = LuaAnimGraphIRNames::PoseData;
        }
        FLuaAnimIRPinContract& BoolActive = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolActive.Name = TEXT("ActiveValue");
        BoolActive.Direction = ELuaAnimIRPinDirection::Input;
        BoolActive.DataType = LuaAnimGraphIRNames::BoolData;
        FLuaAnimIRPinContract& BoolPose = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolPose.Name = TEXT("Pose");
        BoolPose.Direction = ELuaAnimIRPinDirection::Output;
        BoolPose.DataType = LuaAnimGraphIRNames::PoseData;
        BoolPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& BoolBlendTime = BlendByBool.Properties.AddDefaulted_GetRef();
        BoolBlendTime.Name = TEXT("BlendTime");
        BoolBlendTime.ValueType = ELuaAnimIRValueType::Float;

        FLuaAnimIRNodeContract& BlendByEnum = Contracts.AddDefaulted_GetRef();
        BlendByEnum.NodeType = LuaAnimGraphIRNames::BlendListByEnumNode;
        BlendByEnum.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum"));
        BlendByEnum.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        BlendByEnum.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        const TCHAR* EnumPoseNames[] = {
            TEXT("DefaultPose"), TEXT("Pose0"), TEXT("Pose1"), TEXT("Pose2"), TEXT("Pose3"),
            TEXT("Pose4"), TEXT("Pose5"), TEXT("Pose6"), TEXT("Pose7")
        };
        for (const TCHAR* PoseName : EnumPoseNames)
        {
            FLuaAnimIRPinContract& PosePin = BlendByEnum.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ELuaAnimIRPinDirection::Input;
            PosePin.DataType = LuaAnimGraphIRNames::PoseData;
        }
        FLuaAnimIRPinContract& EnumActive = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumActive.Name = TEXT("ActiveValue");
        EnumActive.Direction = ELuaAnimIRPinDirection::Input;
        EnumActive.DataType = LuaAnimGraphIRNames::EnumData;
        FLuaAnimIRPinContract& EnumPose = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumPose.Name = TEXT("Pose");
        EnumPose.Direction = ELuaAnimIRPinDirection::Output;
        EnumPose.DataType = LuaAnimGraphIRNames::PoseData;
        EnumPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& EnumType = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumType.Name = TEXT("EnumType");
        EnumType.ValueType = ELuaAnimIRValueType::SoftObjectPath;
        EnumType.bRequired = true;
        FLuaAnimIRPropertyContract& EnumEntries = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumEntries.Name = TEXT("EnumEntries");
        EnumEntries.ValueType = ELuaAnimIRValueType::String;
        EnumEntries.bRequired = true;
        FLuaAnimIRPropertyContract& EnumBlendTime = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumBlendTime.Name = TEXT("BlendTime");
        EnumBlendTime.ValueType = ELuaAnimIRValueType::Float;

        FLuaAnimIRNodeContract& Slot = Contracts.AddDefaulted_GetRef();
        Slot.NodeType = LuaAnimGraphIRNames::SlotNode;
        Slot.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Slot"));
        Slot.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        Slot.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& SlotSource = Slot.Pins.AddDefaulted_GetRef();
        SlotSource.Name = TEXT("Source");
        SlotSource.Direction = ELuaAnimIRPinDirection::Input;
        SlotSource.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPinContract& SlotPose = Slot.Pins.AddDefaulted_GetRef();
        SlotPose.Name = TEXT("Pose");
        SlotPose.Direction = ELuaAnimIRPinDirection::Output;
        SlotPose.DataType = LuaAnimGraphIRNames::PoseData;
        SlotPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& SlotName = Slot.Properties.AddDefaulted_GetRef();
        SlotName.Name = TEXT("SlotName");
        SlotName.ValueType = ELuaAnimIRValueType::Name;
        SlotName.bRequired = true;
        FLuaAnimIRPropertyContract& AlwaysUpdateSourcePose = Slot.Properties.AddDefaulted_GetRef();
        AlwaysUpdateSourcePose.Name = TEXT("bAlwaysUpdateSourcePose");
        AlwaysUpdateSourcePose.ValueType = ELuaAnimIRValueType::Bool;

        FLuaAnimIRNodeContract& LayeredBlend = Contracts.AddDefaulted_GetRef();
        LayeredBlend.NodeType = LuaAnimGraphIRNames::LayeredBlendPerBoneNode;
        LayeredBlend.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend"));
        LayeredBlend.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LayeredBlend.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        FLuaAnimIRPinContract& LayeredBasePose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBasePose.Name = TEXT("BasePose");
        LayeredBasePose.Direction = ELuaAnimIRPinDirection::Input;
        LayeredBasePose.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPinContract& LayeredBlendPose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBlendPose.Name = TEXT("BlendPose");
        LayeredBlendPose.Direction = ELuaAnimIRPinDirection::Input;
        LayeredBlendPose.DataType = LuaAnimGraphIRNames::PoseData;
        FLuaAnimIRPinContract& LayeredBlendWeight = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBlendWeight.Name = TEXT("BlendWeight");
        LayeredBlendWeight.Direction = ELuaAnimIRPinDirection::Input;
        LayeredBlendWeight.DataType = LuaAnimGraphIRNames::FloatData;
        FLuaAnimIRPinContract& LayeredPose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredPose.Name = TEXT("Pose");
        LayeredPose.Direction = ELuaAnimIRPinDirection::Output;
        LayeredPose.DataType = LuaAnimGraphIRNames::PoseData;
        LayeredPose.bAllowMultipleConnections = true;
        FLuaAnimIRPropertyContract& BranchFilters = LayeredBlend.Properties.AddDefaulted_GetRef();
        BranchFilters.Name = TEXT("BranchFilters");
        BranchFilters.ValueType = ELuaAnimIRValueType::String;
        BranchFilters.bRequired = true;
        FLuaAnimIRPropertyContract& MeshSpaceRotationBlend = LayeredBlend.Properties.AddDefaulted_GetRef();
        MeshSpaceRotationBlend.Name = TEXT("bMeshSpaceRotationBlend");
        MeshSpaceRotationBlend.ValueType = ELuaAnimIRValueType::Bool;
        FLuaAnimIRPropertyContract& MeshSpaceScaleBlend = LayeredBlend.Properties.AddDefaulted_GetRef();
        MeshSpaceScaleBlend.Name = TEXT("bMeshSpaceScaleBlend");
        MeshSpaceScaleBlend.ValueType = ELuaAnimIRValueType::Bool;
        FLuaAnimIRPropertyContract& CurveBlendOption = LayeredBlend.Properties.AddDefaulted_GetRef();
        CurveBlendOption.Name = TEXT("CurveBlendOption");
        CurveBlendOption.ValueType = ELuaAnimIRValueType::Enum;
        FLuaAnimIRPropertyContract& BlendRootMotion = LayeredBlend.Properties.AddDefaulted_GetRef();
        BlendRootMotion.Name = TEXT("bBlendRootMotionBasedOnRootBone");
        BlendRootMotion.ValueType = ELuaAnimIRValueType::Bool;

        FLuaAnimIRNodeContract& LinkedLayer = Contracts.AddDefaulted_GetRef();
        LinkedLayer.NodeType = LuaAnimGraphIRNames::LinkedAnimLayerNode;
        LinkedLayer.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LinkedAnimLayer"));
        LinkedLayer.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LinkedLayer.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        LinkedLayer.bDynamicPins = true;
        FLuaAnimIRPropertyContract& LinkedLayerName = LinkedLayer.Properties.AddDefaulted_GetRef();
        LinkedLayerName.Name = TEXT("LayerName");
        LinkedLayerName.ValueType = ELuaAnimIRValueType::Name;
        LinkedLayerName.bRequired = true;
        FLuaAnimIRPropertyContract& LinkedLayerInstanceClass =
            LinkedLayer.Properties.AddDefaulted_GetRef();
        LinkedLayerInstanceClass.Name = TEXT("InstanceClass");
        LinkedLayerInstanceClass.ValueType = ELuaAnimIRValueType::SoftClassPath;
        FLuaAnimIRPropertyContract& LinkedLayerInterfaceClass =
            LinkedLayer.Properties.AddDefaulted_GetRef();
        LinkedLayerInterfaceClass.Name = TEXT("InterfaceClass");
        LinkedLayerInterfaceClass.ValueType = ELuaAnimIRValueType::SoftClassPath;

        FLuaAnimIRNodeContract& LinkedGraph = Contracts.AddDefaulted_GetRef();
        LinkedGraph.NodeType = LuaAnimGraphIRNames::LinkedAnimGraphNode;
        LinkedGraph.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LinkedAnimGraph"));
        LinkedGraph.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LinkedGraph.AllowedGraphTypes.Add(LuaAnimGraphIRNames::StatePoseGraph);
        LinkedGraph.bDynamicPins = true;
        FLuaAnimIRPropertyContract& LinkedGraphInstanceClass =
            LinkedGraph.Properties.AddDefaulted_GetRef();
        LinkedGraphInstanceClass.Name = TEXT("InstanceClass");
        LinkedGraphInstanceClass.ValueType = ELuaAnimIRValueType::SoftClassPath;
        LinkedGraphInstanceClass.bRequired = true;
        FLuaAnimIRPropertyContract& LinkedGraphName = LinkedGraph.Properties.AddDefaulted_GetRef();
        LinkedGraphName.Name = TEXT("GraphName");
        LinkedGraphName.ValueType = ELuaAnimIRValueType::Name;

        FLuaAnimIRNodeContract& LinkedInput = Contracts.AddDefaulted_GetRef();
        LinkedInput.NodeType = LuaAnimGraphIRNames::LinkedInputPoseNode;
        LinkedInput.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LinkedInputPose"));
        LinkedInput.AllowedGraphTypes.Add(LuaAnimGraphIRNames::PoseGraph);
        LinkedInput.bDynamicPins = true;
        FLuaAnimIRPropertyContract& LinkedInputName = LinkedInput.Properties.AddDefaulted_GetRef();
        LinkedInputName.Name = TEXT("PoseName");
        LinkedInputName.ValueType = ELuaAnimIRValueType::Name;
        LinkedInputName.bRequired = true;

        return Contracts;
    }

    /**
     * 取得进程内唯一的不可变内置节点契约数组。
     * C++11 静态局部初始化保证首次并发查询安全；函数不加载 UObject。
     *
     * @return 注册表内部数组的常量引用，其生命周期持续到进程结束。
     */
    const TArray<FLuaAnimIRNodeContract>& GetRegisteredContractsWithFootIK()
    {
        static const TArray<FLuaAnimIRNodeContract> Contracts = BuildContracts();
        return Contracts;
    }
}

/**
 * 返回全部内置 NodeType 契约的只读连续视图。
 * 本函数可在任意线程调用，不加载 UObject；返回视图不可修改注册表且在进程生命周期内有效。
 *
 * @return 按稳定注册顺序排列的常量契约视图。
 */
TConstArrayView<FLuaAnimIRNodeContract> FLuaAnimGraphNodeRegistry::GetContracts()
{
    return LuaAnimGraphNodeRegistryPrivate::GetRegisteredContractsWithFootIK();
}

/**
 * 按稳定注册名查询单个 NodeType 的权威契约。
 * 本函数执行大小写不敏感的 FName 比较，不加载 UObject，可在任意线程调用。
 *
 * @param NodeType Lua IR 声明的节点类型注册名；None 不匹配任何契约。
 * @return 找到时返回注册表内部常量指针，否则返回 nullptr；指针在进程生命周期内有效。
 */
const FLuaAnimIRNodeContract* FLuaAnimGraphNodeRegistry::Find(const FName NodeType)
{
    for (const FLuaAnimIRNodeContract& Contract :
        LuaAnimGraphNodeRegistryPrivate::GetRegisteredContractsWithFootIK())
    {
        if (Contract.NodeType == NodeType) return &Contract;
    }

    return nullptr;
}
