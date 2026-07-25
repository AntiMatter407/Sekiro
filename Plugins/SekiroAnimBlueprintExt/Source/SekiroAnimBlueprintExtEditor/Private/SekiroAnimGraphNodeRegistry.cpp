#include "SekiroAnimGraphNodeRegistry.h"

namespace SekiroAnimGraphNodeRegistryPrivate
{
    /**
     * 构造插件内置 NodeType 的完整权威契约快照。
     * 本函数仅创建值类型和软类路径，不加载 UObject，可在任意线程调用。
     *
     * @return 按稳定注册顺序排列的节点契约数组，调用方取得独立值。
     */
    TArray<FSekiroAnimIRNodeContract> BuildContracts()
    {
        TArray<FSekiroAnimIRNodeContract> Contracts;
        Contracts.Reserve(22);

        FSekiroAnimIRNodeContract& OutputPose = Contracts.AddDefaulted_GetRef();
        OutputPose.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
        OutputPose.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Root"));
        OutputPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        OutputPose.RootRole = ESekiroAnimIRNodeRootRole::GraphRoot;
        FSekiroAnimIRPinContract& OutputResult = OutputPose.Pins.AddDefaulted_GetRef();
        OutputResult.Name = TEXT("Result");
        OutputResult.Direction = ESekiroAnimIRPinDirection::Input;
        OutputResult.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRNodeContract& StateResult = Contracts.AddDefaulted_GetRef();
        StateResult.NodeType = SekiroAnimGraphIRNames::StateResultNode;
        StateResult.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateResult"));
        StateResult.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        StateResult.RootRole = ESekiroAnimIRNodeRootRole::GraphRoot;
        FSekiroAnimIRPinContract& StateResultPin = StateResult.Pins.AddDefaulted_GetRef();
        StateResultPin.Name = TEXT("Result");
        StateResultPin.Direction = ESekiroAnimIRPinDirection::Input;
        StateResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRNodeContract& SequencePlayer = Contracts.AddDefaulted_GetRef();
        SequencePlayer.NodeType = SekiroAnimGraphIRNames::SequencePlayerNode;
        SequencePlayer.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SequencePlayer"));
        SequencePlayer.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        SequencePlayer.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& SequencePose = SequencePlayer.Pins.AddDefaulted_GetRef();
        SequencePose.Name = TEXT("Pose");
        SequencePose.Direction = ESekiroAnimIRPinDirection::Output;
        SequencePose.DataType = SekiroAnimGraphIRNames::PoseData;
        SequencePose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& Sequence = SequencePlayer.Properties.AddDefaulted_GetRef();
        Sequence.Name = TEXT("Sequence");
        Sequence.ValueType = ESekiroAnimIRValueType::SoftObjectPath;
        Sequence.bRequired = true;
        FSekiroAnimIRPropertyContract& LoopAnimation = SequencePlayer.Properties.AddDefaulted_GetRef();
        LoopAnimation.Name = TEXT("bLoopAnimation");
        LoopAnimation.ValueType = ESekiroAnimIRValueType::Bool;
        FSekiroAnimIRPropertyContract& PlayRate = SequencePlayer.Properties.AddDefaulted_GetRef();
        PlayRate.Name = TEXT("PlayRate");
        PlayRate.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& StartPosition = SequencePlayer.Properties.AddDefaulted_GetRef();
        StartPosition.Name = TEXT("StartPosition");
        StartPosition.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& GroupName = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupName.Name = TEXT("GroupName");
        GroupName.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& GroupRole = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupRole.Name = TEXT("GroupRole");
        GroupRole.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& GroupMethod = SequencePlayer.Properties.AddDefaulted_GetRef();
        GroupMethod.Name = TEXT("GroupMethod");
        GroupMethod.ValueType = ESekiroAnimIRValueType::Name;

        FSekiroAnimIRNodeContract& StateMachine = Contracts.AddDefaulted_GetRef();
        StateMachine.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
        StateMachine.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_StateMachine"));
        StateMachine.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        StateMachine.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        StateMachine.OwnedGraphPolicy = ESekiroAnimIROwnedGraphPolicy::Required;
        StateMachine.OwnedGraphType = SekiroAnimGraphIRNames::StateMachineGraph;
        FSekiroAnimIRPinContract& StateMachinePose = StateMachine.Pins.AddDefaulted_GetRef();
        StateMachinePose.Name = TEXT("Pose");
        StateMachinePose.Direction = ESekiroAnimIRPinDirection::Output;
        StateMachinePose.DataType = SekiroAnimGraphIRNames::PoseData;
        StateMachinePose.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& Inertialization = Contracts.AddDefaulted_GetRef();
        Inertialization.NodeType = SekiroAnimGraphIRNames::InertializationNode;
        Inertialization.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Inertialization"));
        Inertialization.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        Inertialization.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& InertialSource = Inertialization.Pins.AddDefaulted_GetRef();
        InertialSource.Name = TEXT("Source");
        InertialSource.Direction = ESekiroAnimIRPinDirection::Input;
        InertialSource.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& InertialPose = Inertialization.Pins.AddDefaulted_GetRef();
        InertialPose.Name = TEXT("Pose");
        InertialPose.Direction = ESekiroAnimIRPinDirection::Output;
        InertialPose.DataType = SekiroAnimGraphIRNames::PoseData;
        InertialPose.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& LocalToComponent = Contracts.AddDefaulted_GetRef();
        LocalToComponent.NodeType = SekiroAnimGraphIRNames::LocalToComponentSpaceNode;
        LocalToComponent.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LocalToComponentSpace"));
        LocalToComponent.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        LocalToComponent.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& LocalPoseInput = LocalToComponent.Pins.AddDefaulted_GetRef();
        LocalPoseInput.Name = TEXT("LocalPose");
        LocalPoseInput.Direction = ESekiroAnimIRPinDirection::Input;
        LocalPoseInput.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& ComponentPoseOutput = LocalToComponent.Pins.AddDefaulted_GetRef();
        ComponentPoseOutput.Name = TEXT("ComponentPose");
        ComponentPoseOutput.Direction = ESekiroAnimIRPinDirection::Output;
        ComponentPoseOutput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        ComponentPoseOutput.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& ComponentToLocal = Contracts.AddDefaulted_GetRef();
        ComponentToLocal.NodeType = SekiroAnimGraphIRNames::ComponentToLocalSpaceNode;
        ComponentToLocal.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_ComponentToLocalSpace"));
        ComponentToLocal.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        ComponentToLocal.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& ComponentPoseInput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        ComponentPoseInput.Name = TEXT("ComponentPose");
        ComponentPoseInput.Direction = ESekiroAnimIRPinDirection::Input;
        ComponentPoseInput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& LocalPoseOutput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        LocalPoseOutput.Name = TEXT("Pose");
        LocalPoseOutput.Direction = ESekiroAnimIRPinDirection::Output;
        LocalPoseOutput.DataType = SekiroAnimGraphIRNames::PoseData;
        LocalPoseOutput.bAllowMultipleConnections = true;

        FSekiroAnimIRNodeContract& OrientationWarping = Contracts.AddDefaulted_GetRef();
        OrientationWarping.NodeType = SekiroAnimGraphIRNames::OrientationWarpingNode;
        OrientationWarping.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_OrientationWarping"));
        OrientationWarping.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        OrientationWarping.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& OrientationComponentPose = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationComponentPose.Name = TEXT("ComponentPose");
        OrientationComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& OrientationAngle = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationAngle.Name = TEXT("OrientationAngle");
        OrientationAngle.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationAngle.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& OrientationAlpha = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationAlpha.Name = TEXT("Alpha");
        OrientationAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& OrientationPose = OrientationWarping.Pins.AddDefaulted_GetRef();
        OrientationPose.Name = TEXT("Pose");
        OrientationPose.Direction = ESekiroAnimIRPinDirection::Output;
        OrientationPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        OrientationPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& OrientationSpineBones =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationSpineBones.Name = TEXT("SpineBones");
        OrientationSpineBones.ValueType = ESekiroAnimIRValueType::String;
        OrientationSpineBones.bRequired = true;
        FSekiroAnimIRPropertyContract& OrientationFootRoot =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationFootRoot.Name = TEXT("IKFootRootBone");
        OrientationFootRoot.ValueType = ESekiroAnimIRValueType::Name;
        OrientationFootRoot.bRequired = true;
        FSekiroAnimIRPropertyContract& OrientationFootBones =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationFootBones.Name = TEXT("IKFootBones");
        OrientationFootBones.ValueType = ESekiroAnimIRValueType::String;
        OrientationFootBones.bRequired = true;
        FSekiroAnimIRPropertyContract& OrientationRotationAxis =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationRotationAxis.Name = TEXT("RotationAxis");
        OrientationRotationAxis.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& OrientationDistribution =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationDistribution.Name = TEXT("DistributedBoneOrientationAlpha");
        OrientationDistribution.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& OrientationInterpSpeed =
            OrientationWarping.Properties.AddDefaulted_GetRef();
        OrientationInterpSpeed.Name = TEXT("RotationInterpSpeed");
        OrientationInterpSpeed.ValueType = ESekiroAnimIRValueType::Float;

        FSekiroAnimIRNodeContract& SpineYawCompensation = Contracts.AddDefaulted_GetRef();
        SpineYawCompensation.NodeType = SekiroAnimGraphIRNames::SpineYawCompensationNode;
        SpineYawCompensation.EditorNodeClassPath = FSoftClassPath(
            TEXT("/Script/SekiroAnimBlueprintExtEditor.AnimGraphNode_SekiroSpineYawCompensation"));
        SpineYawCompensation.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        SpineYawCompensation.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& SpineYawComponentPose =
            SpineYawCompensation.Pins.AddDefaulted_GetRef();
        SpineYawComponentPose.Name = TEXT("ComponentPose");
        SpineYawComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        SpineYawComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& SpineYawAngle = SpineYawCompensation.Pins.AddDefaulted_GetRef();
        SpineYawAngle.Name = TEXT("YawAngle");
        SpineYawAngle.Direction = ESekiroAnimIRPinDirection::Input;
        SpineYawAngle.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& SpineYawAlpha = SpineYawCompensation.Pins.AddDefaulted_GetRef();
        SpineYawAlpha.Name = TEXT("Alpha");
        SpineYawAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        SpineYawAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& SpineYawPose = SpineYawCompensation.Pins.AddDefaulted_GetRef();
        SpineYawPose.Name = TEXT("Pose");
        SpineYawPose.Direction = ESekiroAnimIRPinDirection::Output;
        SpineYawPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        SpineYawPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& SpineYawBones =
            SpineYawCompensation.Properties.AddDefaulted_GetRef();
        SpineYawBones.Name = TEXT("SpineBones");
        SpineYawBones.ValueType = ESekiroAnimIRValueType::String;
        SpineYawBones.bRequired = true;
        FSekiroAnimIRPropertyContract& SpineYawAxis =
            SpineYawCompensation.Properties.AddDefaulted_GetRef();
        SpineYawAxis.Name = TEXT("RotationAxis");
        SpineYawAxis.ValueType = ESekiroAnimIRValueType::Name;

        FSekiroAnimIRNodeContract& FootPlacement = Contracts.AddDefaulted_GetRef();
        FootPlacement.NodeType = SekiroAnimGraphIRNames::FootPlacementNode;
        FootPlacement.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_FootPlacement"));
        FootPlacement.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        FootPlacement.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& FootPlacementComponentPose = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementComponentPose.Name = TEXT("ComponentPose");
        FootPlacementComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        FootPlacementComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& FootPlacementAlpha = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementAlpha.Name = TEXT("Alpha");
        FootPlacementAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        FootPlacementAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& FootPlacementPose = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementPose.Name = TEXT("Pose");
        FootPlacementPose.Direction = ESekiroAnimIRPinDirection::Output;
        FootPlacementPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FootPlacementPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& FootPlacementFootRoot = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementFootRoot.Name = TEXT("IKFootRootBone");
        FootPlacementFootRoot.ValueType = ESekiroAnimIRValueType::Name;
        FootPlacementFootRoot.bRequired = true;
        FSekiroAnimIRPropertyContract& FootPlacementPelvis = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementPelvis.Name = TEXT("PelvisBone");
        FootPlacementPelvis.ValueType = ESekiroAnimIRValueType::Name;
        FootPlacementPelvis.bRequired = true;
        FSekiroAnimIRPropertyContract& FootPlacementLegs = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementLegs.Name = TEXT("LegDefinitions");
        FootPlacementLegs.ValueType = ESekiroAnimIRValueType::String;
        FootPlacementLegs.bRequired = true;
        FSekiroAnimIRPropertyContract& FootPlacementSpeedMode = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementSpeedMode.Name = TEXT("PlantSpeedMode");
        FootPlacementSpeedMode.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& FootPlacementLockType = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementLockType.Name = TEXT("PlantLockType");
        FootPlacementLockType.ValueType = ESekiroAnimIRValueType::Name;
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
            FSekiroAnimIRPropertyContract& Property = FootPlacement.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ESekiroAnimIRValueType::Float;
        }
        FSekiroAnimIRPropertyContract& FootPlacementTraceEnabled = FootPlacement.Properties.AddDefaulted_GetRef();
        FootPlacementTraceEnabled.Name = TEXT("bTraceEnabled");
        FootPlacementTraceEnabled.ValueType = ESekiroAnimIRValueType::Bool;

        FSekiroAnimIRNodeContract& LegIK = Contracts.AddDefaulted_GetRef();
        LegIK.NodeType = SekiroAnimGraphIRNames::LegIKNode;
        LegIK.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LegIK"));
        LegIK.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        LegIK.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& LegIKComponentPose = LegIK.Pins.AddDefaulted_GetRef();
        LegIKComponentPose.Name = TEXT("ComponentPose");
        LegIKComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        LegIKComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& LegIKAlpha = LegIK.Pins.AddDefaulted_GetRef();
        LegIKAlpha.Name = TEXT("Alpha");
        LegIKAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        LegIKAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& LegIKPose = LegIK.Pins.AddDefaulted_GetRef();
        LegIKPose.Name = TEXT("Pose");
        LegIKPose.Direction = ESekiroAnimIRPinDirection::Output;
        LegIKPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        LegIKPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& LegIKLegs = LegIK.Properties.AddDefaulted_GetRef();
        LegIKLegs.Name = TEXT("LegDefinitions");
        LegIKLegs.ValueType = ESekiroAnimIRValueType::String;
        LegIKLegs.bRequired = true;
        FSekiroAnimIRPropertyContract& LegIKReachPrecision = LegIK.Properties.AddDefaulted_GetRef();
        LegIKReachPrecision.Name = TEXT("ReachPrecision");
        LegIKReachPrecision.ValueType = ESekiroAnimIRValueType::Float;
        FSekiroAnimIRPropertyContract& LegIKMaxIterations = LegIK.Properties.AddDefaulted_GetRef();
        LegIKMaxIterations.Name = TEXT("MaxIterations");
        LegIKMaxIterations.ValueType = ESekiroAnimIRValueType::Integer;

        FSekiroAnimIRNodeContract& TwoBoneIK = Contracts.AddDefaulted_GetRef();
        TwoBoneIK.NodeType = SekiroAnimGraphIRNames::TwoBoneIKNode;
        TwoBoneIK.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_TwoBoneIK"));
        TwoBoneIK.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        TwoBoneIK.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& TwoBoneIKComponentPose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKComponentPose.Name = TEXT("ComponentPose");
        TwoBoneIKComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        TwoBoneIKComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPinContract& TwoBoneIKAlpha = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKAlpha.Name = TEXT("Alpha");
        TwoBoneIKAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        TwoBoneIKAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& TwoBoneIKPose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        TwoBoneIKPose.Name = TEXT("Pose");
        TwoBoneIKPose.Direction = ESekiroAnimIRPinDirection::Output;
        TwoBoneIKPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        TwoBoneIKPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& TwoBoneIKBone = TwoBoneIK.Properties.AddDefaulted_GetRef();
        TwoBoneIKBone.Name = TEXT("IKBone");
        TwoBoneIKBone.ValueType = ESekiroAnimIRValueType::Name;
        TwoBoneIKBone.bRequired = true;
        const TCHAR* TwoBoneIKNamePropertyNames[] = {
            TEXT("EffectorLocationSpace"),
            TEXT("EffectorTargetBoneName"),
            TEXT("EffectorTargetSocketName"),
            TEXT("JointTargetLocationSpace"),
            TEXT("JointTargetBoneName"),
            TEXT("JointTargetSocketName"),
            TEXT("AlphaInputType"),
            TEXT("AlphaCurveName"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKNamePropertyNames)
        {
            FSekiroAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ESekiroAnimIRValueType::Name;
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
            FSekiroAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ESekiroAnimIRValueType::Float;
        }
        const TCHAR* TwoBoneIKBoolPropertyNames[] = {
            TEXT("bTakeRotationFromEffectorSpace"),
            TEXT("bAllowStretching"),
        };
        for (const TCHAR* PropertyName : TwoBoneIKBoolPropertyNames)
        {
            FSekiroAnimIRPropertyContract& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = PropertyName;
            Property.ValueType = ESekiroAnimIRValueType::Bool;
        }

        FSekiroAnimIRNodeContract& SaveCachedPose = Contracts.AddDefaulted_GetRef();
        SaveCachedPose.NodeType = SekiroAnimGraphIRNames::SaveCachedPoseNode;
        SaveCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_SaveCachedPose"));
        SaveCachedPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        FSekiroAnimIRPinContract& CachedPoseInput = SaveCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseInput.Name = TEXT("Pose");
        CachedPoseInput.Direction = ESekiroAnimIRPinDirection::Input;
        CachedPoseInput.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPropertyContract& SaveCacheName = SaveCachedPose.Properties.AddDefaulted_GetRef();
        SaveCacheName.Name = TEXT("CacheName");
        SaveCacheName.ValueType = ESekiroAnimIRValueType::String;
        SaveCacheName.bRequired = true;

        FSekiroAnimIRNodeContract& UseCachedPose = Contracts.AddDefaulted_GetRef();
        UseCachedPose.NodeType = SekiroAnimGraphIRNames::UseCachedPoseNode;
        UseCachedPose.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_UseCachedPose"));
        UseCachedPose.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        FSekiroAnimIRPinContract& CachedPoseOutput = UseCachedPose.Pins.AddDefaulted_GetRef();
        CachedPoseOutput.Name = TEXT("Pose");
        CachedPoseOutput.Direction = ESekiroAnimIRPinDirection::Output;
        CachedPoseOutput.DataType = SekiroAnimGraphIRNames::PoseData;
        CachedPoseOutput.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& UseCacheName = UseCachedPose.Properties.AddDefaulted_GetRef();
        UseCacheName.Name = TEXT("CacheName");
        UseCacheName.ValueType = ESekiroAnimIRValueType::String;
        UseCacheName.bRequired = true;

        const FName GetterTypes[] = {
            SekiroAnimGraphIRNames::BoolPropertyGetterNode,
            SekiroAnimGraphIRNames::FloatPropertyGetterNode,
            SekiroAnimGraphIRNames::BytePropertyGetterNode,
            SekiroAnimGraphIRNames::EnumPropertyGetterNode,
        };
        const FName GetterDataTypes[] = {
            SekiroAnimGraphIRNames::BoolData,
            SekiroAnimGraphIRNames::FloatData,
            SekiroAnimGraphIRNames::ByteData,
            SekiroAnimGraphIRNames::EnumData,
        };
        for (int32 GetterIndex = 0; GetterIndex < UE_ARRAY_COUNT(GetterTypes); ++GetterIndex)
        {
            FSekiroAnimIRNodeContract& Getter = Contracts.AddDefaulted_GetRef();
            Getter.NodeType = GetterTypes[GetterIndex];
            Getter.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/BlueprintGraph.K2Node_VariableGet"));
            Getter.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
            Getter.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
            FSekiroAnimIRPinContract& ValuePin = Getter.Pins.AddDefaulted_GetRef();
            ValuePin.Name = TEXT("Value");
            ValuePin.Direction = ESekiroAnimIRPinDirection::Output;
            ValuePin.DataType = GetterDataTypes[GetterIndex];
            ValuePin.bAllowMultipleConnections = true;
            FSekiroAnimIRPropertyContract& PropertyName = Getter.Properties.AddDefaulted_GetRef();
            PropertyName.Name = TEXT("PropertyName");
            PropertyName.ValueType = ESekiroAnimIRValueType::Name;
            PropertyName.bRequired = true;
        }

        FSekiroAnimIRNodeContract& BlendByBool = Contracts.AddDefaulted_GetRef();
        BlendByBool.NodeType = SekiroAnimGraphIRNames::BlendListByBoolNode;
        BlendByBool.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByBool"));
        BlendByBool.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        BlendByBool.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        const TCHAR* BoolPoseNames[] = { TEXT("TruePose"), TEXT("FalsePose") };
        for (const TCHAR* PoseName : BoolPoseNames)
        {
            FSekiroAnimIRPinContract& PosePin = BlendByBool.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ESekiroAnimIRPinDirection::Input;
            PosePin.DataType = SekiroAnimGraphIRNames::PoseData;
        }
        FSekiroAnimIRPinContract& BoolActive = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolActive.Name = TEXT("ActiveValue");
        BoolActive.Direction = ESekiroAnimIRPinDirection::Input;
        BoolActive.DataType = SekiroAnimGraphIRNames::BoolData;
        FSekiroAnimIRPinContract& BoolPose = BlendByBool.Pins.AddDefaulted_GetRef();
        BoolPose.Name = TEXT("Pose");
        BoolPose.Direction = ESekiroAnimIRPinDirection::Output;
        BoolPose.DataType = SekiroAnimGraphIRNames::PoseData;
        BoolPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& BoolBlendTime = BlendByBool.Properties.AddDefaulted_GetRef();
        BoolBlendTime.Name = TEXT("BlendTime");
        BoolBlendTime.ValueType = ESekiroAnimIRValueType::Float;

        FSekiroAnimIRNodeContract& BlendByEnum = Contracts.AddDefaulted_GetRef();
        BlendByEnum.NodeType = SekiroAnimGraphIRNames::BlendListByEnumNode;
        BlendByEnum.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum"));
        BlendByEnum.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        BlendByEnum.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        const TCHAR* EnumPoseNames[] = {
            TEXT("DefaultPose"), TEXT("Pose0"), TEXT("Pose1"), TEXT("Pose2"), TEXT("Pose3"),
            TEXT("Pose4"), TEXT("Pose5"), TEXT("Pose6"), TEXT("Pose7")
        };
        for (const TCHAR* PoseName : EnumPoseNames)
        {
            FSekiroAnimIRPinContract& PosePin = BlendByEnum.Pins.AddDefaulted_GetRef();
            PosePin.Name = PoseName;
            PosePin.Direction = ESekiroAnimIRPinDirection::Input;
            PosePin.DataType = SekiroAnimGraphIRNames::PoseData;
        }
        FSekiroAnimIRPinContract& EnumActive = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumActive.Name = TEXT("ActiveValue");
        EnumActive.Direction = ESekiroAnimIRPinDirection::Input;
        EnumActive.DataType = SekiroAnimGraphIRNames::EnumData;
        FSekiroAnimIRPinContract& EnumPose = BlendByEnum.Pins.AddDefaulted_GetRef();
        EnumPose.Name = TEXT("Pose");
        EnumPose.Direction = ESekiroAnimIRPinDirection::Output;
        EnumPose.DataType = SekiroAnimGraphIRNames::PoseData;
        EnumPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& EnumType = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumType.Name = TEXT("EnumType");
        EnumType.ValueType = ESekiroAnimIRValueType::SoftObjectPath;
        EnumType.bRequired = true;
        FSekiroAnimIRPropertyContract& EnumEntries = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumEntries.Name = TEXT("EnumEntries");
        EnumEntries.ValueType = ESekiroAnimIRValueType::String;
        EnumEntries.bRequired = true;
        FSekiroAnimIRPropertyContract& EnumBlendTime = BlendByEnum.Properties.AddDefaulted_GetRef();
        EnumBlendTime.Name = TEXT("BlendTime");
        EnumBlendTime.ValueType = ESekiroAnimIRValueType::Float;

        FSekiroAnimIRNodeContract& Slot = Contracts.AddDefaulted_GetRef();
        Slot.NodeType = SekiroAnimGraphIRNames::SlotNode;
        Slot.EditorNodeClassPath = FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_Slot"));
        Slot.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        Slot.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& SlotSource = Slot.Pins.AddDefaulted_GetRef();
        SlotSource.Name = TEXT("Source");
        SlotSource.Direction = ESekiroAnimIRPinDirection::Input;
        SlotSource.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& SlotPose = Slot.Pins.AddDefaulted_GetRef();
        SlotPose.Name = TEXT("Pose");
        SlotPose.Direction = ESekiroAnimIRPinDirection::Output;
        SlotPose.DataType = SekiroAnimGraphIRNames::PoseData;
        SlotPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& SlotName = Slot.Properties.AddDefaulted_GetRef();
        SlotName.Name = TEXT("SlotName");
        SlotName.ValueType = ESekiroAnimIRValueType::Name;
        SlotName.bRequired = true;
        FSekiroAnimIRPropertyContract& AlwaysUpdateSourcePose = Slot.Properties.AddDefaulted_GetRef();
        AlwaysUpdateSourcePose.Name = TEXT("bAlwaysUpdateSourcePose");
        AlwaysUpdateSourcePose.ValueType = ESekiroAnimIRValueType::Bool;

        FSekiroAnimIRNodeContract& LayeredBlend = Contracts.AddDefaulted_GetRef();
        LayeredBlend.NodeType = SekiroAnimGraphIRNames::LayeredBlendPerBoneNode;
        LayeredBlend.EditorNodeClassPath =
            FSoftClassPath(TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend"));
        LayeredBlend.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::PoseGraph);
        LayeredBlend.AllowedGraphTypes.Add(SekiroAnimGraphIRNames::StatePoseGraph);
        FSekiroAnimIRPinContract& LayeredBasePose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBasePose.Name = TEXT("BasePose");
        LayeredBasePose.Direction = ESekiroAnimIRPinDirection::Input;
        LayeredBasePose.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& LayeredBlendPose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBlendPose.Name = TEXT("BlendPose");
        LayeredBlendPose.Direction = ESekiroAnimIRPinDirection::Input;
        LayeredBlendPose.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPinContract& LayeredBlendWeight = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredBlendWeight.Name = TEXT("BlendWeight");
        LayeredBlendWeight.Direction = ESekiroAnimIRPinDirection::Input;
        LayeredBlendWeight.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPinContract& LayeredPose = LayeredBlend.Pins.AddDefaulted_GetRef();
        LayeredPose.Name = TEXT("Pose");
        LayeredPose.Direction = ESekiroAnimIRPinDirection::Output;
        LayeredPose.DataType = SekiroAnimGraphIRNames::PoseData;
        LayeredPose.bAllowMultipleConnections = true;
        FSekiroAnimIRPropertyContract& BranchFilters = LayeredBlend.Properties.AddDefaulted_GetRef();
        BranchFilters.Name = TEXT("BranchFilters");
        BranchFilters.ValueType = ESekiroAnimIRValueType::String;
        BranchFilters.bRequired = true;
        FSekiroAnimIRPropertyContract& MeshSpaceRotationBlend = LayeredBlend.Properties.AddDefaulted_GetRef();
        MeshSpaceRotationBlend.Name = TEXT("bMeshSpaceRotationBlend");
        MeshSpaceRotationBlend.ValueType = ESekiroAnimIRValueType::Bool;
        FSekiroAnimIRPropertyContract& MeshSpaceScaleBlend = LayeredBlend.Properties.AddDefaulted_GetRef();
        MeshSpaceScaleBlend.Name = TEXT("bMeshSpaceScaleBlend");
        MeshSpaceScaleBlend.ValueType = ESekiroAnimIRValueType::Bool;
        FSekiroAnimIRPropertyContract& CurveBlendOption = LayeredBlend.Properties.AddDefaulted_GetRef();
        CurveBlendOption.Name = TEXT("CurveBlendOption");
        CurveBlendOption.ValueType = ESekiroAnimIRValueType::Name;
        FSekiroAnimIRPropertyContract& BlendRootMotion = LayeredBlend.Properties.AddDefaulted_GetRef();
        BlendRootMotion.Name = TEXT("bBlendRootMotionBasedOnRootBone");
        BlendRootMotion.ValueType = ESekiroAnimIRValueType::Bool;

        return Contracts;
    }

    /**
     * 取得进程内唯一的不可变内置节点契约数组。
     * C++11 静态局部初始化保证首次并发查询安全；函数不加载 UObject。
     *
     * @return 注册表内部数组的常量引用，其生命周期持续到进程结束。
     */
    const TArray<FSekiroAnimIRNodeContract>& GetRegisteredContractsWithFootIK()
    {
        static const TArray<FSekiroAnimIRNodeContract> Contracts = BuildContracts();
        return Contracts;
    }
}

/**
 * 返回全部内置 NodeType 契约的只读连续视图。
 * 本函数可在任意线程调用，不加载 UObject；返回视图不可修改注册表且在进程生命周期内有效。
 *
 * @return 按稳定注册顺序排列的常量契约视图。
 */
TConstArrayView<FSekiroAnimIRNodeContract> FSekiroAnimGraphNodeRegistry::GetContracts()
{
    return SekiroAnimGraphNodeRegistryPrivate::GetRegisteredContractsWithFootIK();
}

/**
 * 按稳定注册名查询单个 NodeType 的权威契约。
 * 本函数执行大小写不敏感的 FName 比较，不加载 UObject，可在任意线程调用。
 *
 * @param NodeType Lua IR 声明的节点类型注册名；None 不匹配任何契约。
 * @return 找到时返回注册表内部常量指针，否则返回 nullptr；指针在进程生命周期内有效。
 */
const FSekiroAnimIRNodeContract* FSekiroAnimGraphNodeRegistry::Find(const FName NodeType)
{
    for (const FSekiroAnimIRNodeContract& Contract :
        SekiroAnimGraphNodeRegistryPrivate::GetRegisteredContractsWithFootIK())
    {
        if (Contract.NodeType == NodeType) return &Contract;
    }

    return nullptr;
}
