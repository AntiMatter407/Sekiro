#include "SekiroAnimBlueprintFactoryLibrary.h"

#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LegIK.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraph/AnimGraphNode_FootPlacement.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LuaEnv.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "SekiroAnimGraphIRLibrary.h"
#include "SekiroAnimGraphNodeRegistry.h"
#include "SekiroLuaAnimBlueprintExtension.h"
#include "SekiroLuaAnimBlueprintEditorBinding.h"
#include "SekiroLuaAnimBlueprintFactory.h"
#include "SekiroLuaTransitionRuntimeLibrary.h"
#include "UnLuaModule.h"
#include "UnLuaFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SekiroAnimGraphIRTests
{
    FSekiroAnimBlueprintIR MakeMinimalIR();
    bool HasDiagnosticCode(const TArray<FSekiroAnimIRDiagnostic>& Diagnostics, FName Code);
}

namespace SekiroAnimGraphIRLuaImporterTests
{
    UnLua::FLuaEnv* GetOrActivateTestEnvironment();
    FString BuildValidModuleChunk(const FString& ModuleName);
}

namespace SekiroAnimBlueprintFactoryTests
{
    /**
     * 在规范测试 IR 中按稳定 ID 查找可变 Graph。
     * 函数只访问调用方独占的值类型数组，可在 Automation 测试线程调用。
     *
     * @param Blueprint 待查询的单 Layer IR。
     * @param GraphId 目标 Graph 稳定 ID。
     * @return 找到时返回数组元素指针，否则返回 nullptr；数组扩容后指针失效。
     */
    FSekiroAnimIRGraph* FindGraph(FSekiroAnimBlueprintIR& Blueprint, const FString& GraphId)
    {
        if (Blueprint.Layers.Num() == 0) return nullptr;
        for (FSekiroAnimIRGraph& Graph : Blueprint.Layers[0].Graphs)
        {
            if (Graph.Id == GraphId) return &Graph;
        }

        return nullptr;
    }

    /**
     * 在原生 Graph 中查找首个指定节点类。
     * 函数只读访问 Graph.Nodes，必须在游戏线程执行以遵守 UObject 访问约束。
     *
     * @param Graph 待查询的原生 Graph，可为空。
     * @return 找到时返回首个类型匹配节点，否则返回 nullptr。
     */
    template <typename NodeType>
    NodeType* FindFirstNode(const UEdGraph* Graph)
    {
        if (Graph == nullptr) return nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            NodeType* TypedNode = Cast<NodeType>(Node);
            if (TypedNode != nullptr) return TypedNode;
        }

        return nullptr;
    }

    /**
     * 统计原生 Graph 中指定节点类的实例数量。
     * 函数只读访问 Graph.Nodes，必须在游戏线程执行。
     *
     * @param Graph 待查询的原生 Graph，可为空。
     * @return 类型匹配的节点数量；Graph 为空时返回 0。
     */
    template <typename NodeType>
    int32 CountNodes(const UEdGraph* Graph)
    {
        if (Graph == nullptr) return 0;
        int32 Count = 0;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Cast<NodeType>(Node) != nullptr) ++Count;
        }
        return Count;
    }

    /**
     * 统计 Graph 中绑定指定原生 UFunction 的 K2 调用节点。
     * 函数只读访问编辑器节点，只能在游戏线程调用；FunctionName 使用反射函数名精确比较。
     *
     * @param Graph 待查询的 K2 Graph，可为空。
     * @param FunctionName 目标原生 UFunction 名称。
     * @return 匹配调用节点数量；Graph 为空时返回 0。
     */
    int32 CountFunctionCalls(const UEdGraph* Graph, const FName FunctionName)
    {
        if (Graph == nullptr) return 0;
        int32 Count = 0;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            UFunction* Function = CallNode != nullptr ? CallNode->GetTargetFunction() : nullptr;
            if (Function != nullptr && Function->GetFName() == FunctionName) ++Count;
        }
        return Count;
    }

    /**
     * 基于 Importer 合法内存模块增加第二条 false Transition Rule，供一键保存与生成 EventGraph 端到端测试使用。
     * 函数只处理 Lua 源码字符串，不访问文件系统、Lua VM 或 UObject。
     *
     * @param ModuleName 测试使用的唯一 require 模块名。
     * @param SequencePath 工厂能够解析的测试 UAnimSequence 软路径。
     * @return 含 true/false 两条 Transition 且引用给定 Sequence 的完整 package.preload chunk。
     */
    FString BuildEndToEndModuleChunk(
        const FString& ModuleName,
        const FString& SequencePath)
    {
        FString Chunk = SekiroAnimGraphIRLuaImporterTests::BuildValidModuleChunk(ModuleName);
        Chunk.ReplaceInline(
            TEXT("/Game/Test/Fake.Fake"),
            *SequencePath,
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT(R"LUA(ImplementedInterfaces = {
                "/Script/Engine.AnimInstance",
                "/Script/Engine.AnimSingleNodeInstance",
            },)LUA"),
            TEXT("ImplementedInterfaces = {},"),
            ESearchCase::CaseSensitive);
        Chunk.ReplaceInline(
            TEXT("            Layers = {"),
            TEXT(R"LUA(            Variables = {
                {
                    Name = "GeneratedBool",
                    DataType = "Bool",
                    TypeObjectPath = "",
                    DefaultValue = { Type = "Bool", BoolValue = false },
                    bTransient = true,
                    DeclarationOrder = 0,
                    SourceLocation = Location,
                },
                {
                    Name = "GeneratedFloat",
                    DataType = "Float",
                    TypeObjectPath = "",
                    DefaultValue = { Type = "Float", FloatValue = 0.0 },
                    bTransient = true,
                    DeclarationOrder = 1,
                    SourceLocation = Location,
                },
                {
                    Name = "GeneratedEnum",
                    DataType = "Enum",
                    TypeObjectPath = "/Script/Engine.EComponentMobility",
                    DefaultValue = { Type = "Integer", IntegerValue = 0 },
                    bTransient = true,
                    DeclarationOrder = 2,
                    SourceLocation = Location,
                },
            },
            Layers = {)LUA"),
            ESearchCase::CaseSensitive);
        const FString ExtendedReturn = TEXT(R"LUA(
    function Module.CanEnter_IdleSelfFalse(self)
        return false
    end

    function Module.BlueprintUpdateAnimation(Inst, DeltaSeconds)
        assert(Inst:GetOwningComponent() ~= nil, "Inst must preserve native UFunction calls")
        assert(type(Inst.RootMotionMode) == "number", "Inst must preserve native property reads")
        Inst.GeneratedBool = true
        Inst.GeneratedFloat = 42.5 + DeltaSeconds
        Inst.GeneratedEnum = 2
    end

    local OriginalCompileIR = Module.CompileIR
    function Module.CompileIR()
        local IR = OriginalCompileIR()
        for _, Graph in ipairs(IR.Layers[1].Graphs) do
            if Graph.GraphType == "StateMachine" then
                table.insert(Graph.StateMachine.Transitions, {
                    Id = "Transition.IdleSelf.False",
                    Key = "IdleSelfFalse",
                    SourceStateId = "State.Idle",
                    TargetStateId = "State.Idle",
                    RuleFunctionName = "CanEnter_IdleSelfFalse",
                    Settings = {
                        BlendDuration = 0.1,
                        PriorityOrder = 1,
                        BlendMode = "Linear",
                    },
                    DeclarationOrder = 2,
                    SourceLocation = IR.SourceLocation,
                })
            end
        end
        return IR
    end

    return Module
)LUA");
        Chunk.ReplaceInline(
            TEXT("    return Module"),
            *ExtendedReturn,
            ESearchCase::CaseSensitive);
        return Chunk;
    }

    /**
     * 加载 Validator 使用的引擎测试 Skeleton。
     * 函数会解析 UObject 软路径，只能在游戏线程调用。
     *
     * @return 成功时返回引擎 SkeletalCube Skeleton，否则返回 nullptr。
     */
    USkeleton* LoadTestSkeleton()
    {
        return LoadObject<USkeleton>(
            nullptr,
            TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton"));
    }

    /**
     * 创建与测试 Skeleton 兼容的 transient UAnimSequence，供工厂预检真实解析 SoftObjectPath。
     * 函数创建 UObject，只能在游戏线程调用；返回对象由 transient package 持有。
     *
     * @param Skeleton 新序列绑定的 Skeleton，必须非空。
     * @return 新建 transient 序列；Skeleton 为空时返回 nullptr。
     */
    UAnimSequence* CreateTestSequence(USkeleton* Skeleton)
    {
        if (Skeleton == nullptr) return nullptr;
        UAnimSequence* Sequence = NewObject<UAnimSequence>(
            GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), TEXT("SekiroFactoryTestSequence")),
            RF_Transient);
        Sequence->SetSkeleton(Skeleton);
        return Sequence;
    }

    /**
     * 向 StatePose Graph 添加完整合法的 SequencePlayer，并可选经 Inertialization 连接 StateResult。
     * 函数仅修改调用方独占 IR，不访问 UObject，可在 Automation 测试线程调用。
     *
     * @param Graph 目标 StatePose Graph，必须已有 RootNodeId 对应的 StateResult。
     * @param NodePrefix 新节点和 Link 使用的全局稳定 ID 前缀。
     * @param SequencePath 工厂可解析的 UAnimSequenceBase 软路径。
     * @param bUseInertialization 为 true 时插入原生 Inertialization 节点。
     * @param SourceLocation 复制到新增实体的 Lua 源位置。
     * @return 无返回值。
     */
    void AddSequencePose(
        FSekiroAnimIRGraph& Graph,
        const FString& NodePrefix,
        const FSoftObjectPath& SequencePath,
        const bool bUseInertialization,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRNode& SequenceNode = Graph.Nodes.AddDefaulted_GetRef();
        SequenceNode.Id = NodePrefix + TEXT(".Sequence");
        SequenceNode.NodeType = SekiroAnimGraphIRNames::SequencePlayerNode;
        SequenceNode.DisplayName = TEXT("Test Sequence");
        SequenceNode.SourceLocation = SourceLocation;

        FSekiroAnimIRPin& SequencePose = SequenceNode.Pins.AddDefaulted_GetRef();
        SequencePose.Name = TEXT("Pose");
        SequencePose.Direction = ESekiroAnimIRPinDirection::Output;
        SequencePose.DataType = SekiroAnimGraphIRNames::PoseData;
        SequencePose.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& SequenceProperty = SequenceNode.Properties.AddDefaulted_GetRef();
        SequenceProperty.Name = TEXT("Sequence");
        SequenceProperty.Value.Type = ESekiroAnimIRValueType::SoftObjectPath;
        SequenceProperty.Value.SoftObjectPathValue = SequencePath;

        FSekiroAnimIRProperty& LoopProperty = SequenceNode.Properties.AddDefaulted_GetRef();
        LoopProperty.Name = TEXT("bLoopAnimation");
        LoopProperty.Value.Type = ESekiroAnimIRValueType::Bool;
        LoopProperty.Value.BoolValue = false;

        FSekiroAnimIRProperty& PlayRateProperty = SequenceNode.Properties.AddDefaulted_GetRef();
        PlayRateProperty.Name = TEXT("PlayRate");
        PlayRateProperty.Value.Type = ESekiroAnimIRValueType::Float;
        PlayRateProperty.Value.FloatValue = 1.25;

        FSekiroAnimIRProperty& StartPositionProperty = SequenceNode.Properties.AddDefaulted_GetRef();
        StartPositionProperty.Name = TEXT("StartPosition");
        StartPositionProperty.Value.Type = ESekiroAnimIRValueType::Float;
        StartPositionProperty.Value.FloatValue = 0.0;

        FString SourceNodeId = SequenceNode.Id;
        FString SourcePinName = TEXT("Pose");
        if (bUseInertialization)
        {
            FSekiroAnimIRNode& InertialNode = Graph.Nodes.AddDefaulted_GetRef();
            InertialNode.Id = NodePrefix + TEXT(".Inertialization");
            InertialNode.NodeType = SekiroAnimGraphIRNames::InertializationNode;
            InertialNode.DisplayName = TEXT("Inertialization");
            InertialNode.SourceLocation = SourceLocation;

            FSekiroAnimIRPin& SourcePin = InertialNode.Pins.AddDefaulted_GetRef();
            SourcePin.Name = TEXT("Source");
            SourcePin.Direction = ESekiroAnimIRPinDirection::Input;
            SourcePin.DataType = SekiroAnimGraphIRNames::PoseData;

            FSekiroAnimIRPin& OutputPin = InertialNode.Pins.AddDefaulted_GetRef();
            OutputPin.Name = TEXT("Pose");
            OutputPin.Direction = ESekiroAnimIRPinDirection::Output;
            OutputPin.DataType = SekiroAnimGraphIRNames::PoseData;
            OutputPin.bAllowMultipleConnections = true;

            FSekiroAnimIRLink& SequenceLink = Graph.Links.AddDefaulted_GetRef();
            SequenceLink.Id = NodePrefix + TEXT(".Link.SequenceToInertialization");
            SequenceLink.Source.NodeId = SequenceNode.Id;
            SequenceLink.Source.PinName = TEXT("Pose");
            SequenceLink.Target.NodeId = InertialNode.Id;
            SequenceLink.Target.PinName = TEXT("Source");
            SequenceLink.SourceLocation = SourceLocation;

            SourceNodeId = InertialNode.Id;
        }

        FSekiroAnimIRLink& ResultLink = Graph.Links.AddDefaulted_GetRef();
        ResultLink.Id = NodePrefix + TEXT(".Link.ToResult");
        ResultLink.Source.NodeId = SourceNodeId;
        ResultLink.Source.PinName = SourcePinName;
        ResultLink.Target.NodeId = Graph.RootNodeId;
        ResultLink.Target.PinName = TEXT("Result");
        ResultLink.SourceLocation = SourceLocation;
    }

    /**
     * 在既有本地空间 Pose 输出与 Graph Result 之间插入显式空间转换和 Orientation Warping 链。
     * 函数只修改调用方独占的 IR，不访问 UObject；BoneName 必须由测试 Skeleton 提供且非 None。
     *
     * @param Graph 目标 Pose Graph，必须恰有一条连接 Root Result 的现有 Link。
     * @param BoneName 同时用于测试 Spine、IK Foot Root 和 IK Foot 的有效 Skeleton 骨骼名。
     * @param SourceLocation 复制到新增节点与连接的 Lua 源位置。
     * @return 找到原有 Root 输入并成功改写链时返回 true，否则不创建节点并返回 false。
     */
    bool AddOrientationWarpingChain(
        FSekiroAnimIRGraph& Graph,
        const FName BoneName,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRLink* ResultLink = nullptr;
        for (FSekiroAnimIRLink& Link : Graph.Links)
        {
            if (Link.Target.NodeId == Graph.RootNodeId && Link.Target.PinName == TEXT("Result"))
            {
                ResultLink = &Link;
                break;
            }
        }
        if (ResultLink == nullptr || BoneName.IsNone()) return false;

        const FSekiroAnimIRPinEndpoint OriginalSource = ResultLink->Source;
        const FString LocalToComponentId(TEXT("Node.Move.LocalToComponent"));
        const FString OrientationId(TEXT("Node.Move.OrientationWarping"));
        const FString LocomotionGetterId(TEXT("Node.Move.LocomotionAngle"));
        const FString ComponentToLocalId(TEXT("Node.Move.ComponentToLocal"));
        ResultLink->Source.NodeId = ComponentToLocalId;
        ResultLink->Source.PinName = TEXT("Pose");

        FSekiroAnimIRNode& LocalToComponent = Graph.Nodes.AddDefaulted_GetRef();
        LocalToComponent.Id = LocalToComponentId;
        LocalToComponent.NodeType = SekiroAnimGraphIRNames::LocalToComponentSpaceNode;
        LocalToComponent.DisplayName = TEXT("Local To Component");
        LocalToComponent.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& LocalPose = LocalToComponent.Pins.AddDefaulted_GetRef();
        LocalPose.Name = TEXT("LocalPose");
        LocalPose.Direction = ESekiroAnimIRPinDirection::Input;
        LocalPose.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPin& ComponentPose = LocalToComponent.Pins.AddDefaulted_GetRef();
        ComponentPose.Name = TEXT("ComponentPose");
        ComponentPose.Direction = ESekiroAnimIRPinDirection::Output;
        ComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        ComponentPose.bAllowMultipleConnections = true;

        FSekiroAnimIRNode& Orientation = Graph.Nodes.AddDefaulted_GetRef();
        Orientation.Id = OrientationId;
        Orientation.NodeType = SekiroAnimGraphIRNames::OrientationWarpingNode;
        Orientation.DisplayName = TEXT("Orientation Warping");
        Orientation.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& OrientationComponentPose = Orientation.Pins.AddDefaulted_GetRef();
        OrientationComponentPose.Name = TEXT("ComponentPose");
        OrientationComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPin& OrientationAngle = Orientation.Pins.AddDefaulted_GetRef();
        OrientationAngle.Name = TEXT("OrientationAngle");
        OrientationAngle.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationAngle.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& LocomotionAngle = Orientation.Pins.AddDefaulted_GetRef();
        LocomotionAngle.Name = TEXT("LocomotionAngle");
        LocomotionAngle.Direction = ESekiroAnimIRPinDirection::Input;
        LocomotionAngle.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& OrientationAlpha = Orientation.Pins.AddDefaulted_GetRef();
        OrientationAlpha.Name = TEXT("Alpha");
        OrientationAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        OrientationAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& OrientationPose = Orientation.Pins.AddDefaulted_GetRef();
        OrientationPose.Name = TEXT("Pose");
        OrientationPose.Direction = ESekiroAnimIRPinDirection::Output;
        OrientationPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        OrientationPose.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& SpineBones = Orientation.Properties.AddDefaulted_GetRef();
        SpineBones.Name = TEXT("SpineBones");
        SpineBones.Value.Type = ESekiroAnimIRValueType::String;
        SpineBones.Value.StringValue = BoneName.ToString();
        FSekiroAnimIRProperty& FootRoot = Orientation.Properties.AddDefaulted_GetRef();
        FootRoot.Name = TEXT("IKFootRootBone");
        FootRoot.Value.Type = ESekiroAnimIRValueType::Name;
        FootRoot.Value.NameValue = BoneName;
        FSekiroAnimIRProperty& FootBones = Orientation.Properties.AddDefaulted_GetRef();
        FootBones.Name = TEXT("IKFootBones");
        FootBones.Value.Type = ESekiroAnimIRValueType::String;
        FootBones.Value.StringValue = BoneName.ToString();
        FSekiroAnimIRProperty& RotationAxis = Orientation.Properties.AddDefaulted_GetRef();
        RotationAxis.Name = TEXT("RotationAxis");
        RotationAxis.Value.Type = ESekiroAnimIRValueType::Name;
        RotationAxis.Value.NameValue = TEXT("Z");
        FSekiroAnimIRProperty& Distribution = Orientation.Properties.AddDefaulted_GetRef();
        Distribution.Name = TEXT("DistributedBoneOrientationAlpha");
        Distribution.Value.Type = ESekiroAnimIRValueType::Float;
        Distribution.Value.FloatValue = 1.0;
        FSekiroAnimIRProperty& InterpSpeed = Orientation.Properties.AddDefaulted_GetRef();
        InterpSpeed.Name = TEXT("RotationInterpSpeed");
        InterpSpeed.Value.Type = ESekiroAnimIRValueType::Float;
        InterpSpeed.Value.FloatValue = 8.0;
        FSekiroAnimIRProperty& Mode = Orientation.Properties.AddDefaulted_GetRef();
        Mode.Name = TEXT("Mode");
        Mode.Value.Type = ESekiroAnimIRValueType::Name;
        Mode.Value.NameValue = TEXT("Graph");
        FSekiroAnimIRProperty& MinRootMotionSpeed = Orientation.Properties.AddDefaulted_GetRef();
        MinRootMotionSpeed.Name = TEXT("MinRootMotionSpeedThreshold");
        MinRootMotionSpeed.Value.Type = ESekiroAnimIRValueType::Float;
        MinRootMotionSpeed.Value.FloatValue = 12.0;
        FSekiroAnimIRProperty& LocomotionDelta = Orientation.Properties.AddDefaulted_GetRef();
        LocomotionDelta.Name = TEXT("LocomotionAngleDeltaThreshold");
        LocomotionDelta.Value.Type = ESekiroAnimIRValueType::Float;
        LocomotionDelta.Value.FloatValue = 75.0;
        FSekiroAnimIRProperty& WarpingAlpha = Orientation.Properties.AddDefaulted_GetRef();
        WarpingAlpha.Name = TEXT("WarpingAlpha");
        WarpingAlpha.Value.Type = ESekiroAnimIRValueType::Float;
        WarpingAlpha.Value.FloatValue = 0.8;
        FSekiroAnimIRProperty& OffsetAlpha = Orientation.Properties.AddDefaulted_GetRef();
        OffsetAlpha.Name = TEXT("OffsetAlpha");
        OffsetAlpha.Value.Type = ESekiroAnimIRValueType::Float;
        OffsetAlpha.Value.FloatValue = 0.25;
        FSekiroAnimIRProperty& MaxOffsetAngle = Orientation.Properties.AddDefaulted_GetRef();
        MaxOffsetAngle.Name = TEXT("MaxOffsetAngle");
        MaxOffsetAngle.Value.Type = ESekiroAnimIRValueType::Float;
        MaxOffsetAngle.Value.FloatValue = 55.0;

        FSekiroAnimIRNode& LocomotionGetter = Graph.Nodes.AddDefaulted_GetRef();
        LocomotionGetter.Id = LocomotionGetterId;
        LocomotionGetter.NodeType = SekiroAnimGraphIRNames::FloatPropertyGetterNode;
        LocomotionGetter.DisplayName = TEXT("Graph Locomotion Angle");
        LocomotionGetter.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& LocomotionValue = LocomotionGetter.Pins.AddDefaulted_GetRef();
        LocomotionValue.Name = TEXT("Value");
        LocomotionValue.Direction = ESekiroAnimIRPinDirection::Output;
        LocomotionValue.DataType = SekiroAnimGraphIRNames::FloatData;
        LocomotionValue.bAllowMultipleConnections = true;
        FSekiroAnimIRProperty& LocomotionProperty =
            LocomotionGetter.Properties.AddDefaulted_GetRef();
        LocomotionProperty.Name = TEXT("PropertyName");
        LocomotionProperty.Value.Type = ESekiroAnimIRValueType::Name;
        LocomotionProperty.Value.NameValue = TEXT("GraphLocomotionAngle");

        FSekiroAnimIRNode& ComponentToLocal = Graph.Nodes.AddDefaulted_GetRef();
        ComponentToLocal.Id = ComponentToLocalId;
        ComponentToLocal.NodeType = SekiroAnimGraphIRNames::ComponentToLocalSpaceNode;
        ComponentToLocal.DisplayName = TEXT("Component To Local");
        ComponentToLocal.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& ComponentInput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        ComponentInput.Name = TEXT("ComponentPose");
        ComponentInput.Direction = ESekiroAnimIRPinDirection::Input;
        ComponentInput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPin& LocalOutput = ComponentToLocal.Pins.AddDefaulted_GetRef();
        LocalOutput.Name = TEXT("Pose");
        LocalOutput.Direction = ESekiroAnimIRPinDirection::Output;
        LocalOutput.DataType = SekiroAnimGraphIRNames::PoseData;
        LocalOutput.bAllowMultipleConnections = true;

        FSekiroAnimIRLink& ToComponentLink = Graph.Links.AddDefaulted_GetRef();
        ToComponentLink.Id = TEXT("Link.Move.ToLocalToComponent");
        ToComponentLink.Source = OriginalSource;
        ToComponentLink.Target.NodeId = LocalToComponentId;
        ToComponentLink.Target.PinName = TEXT("LocalPose");
        ToComponentLink.SourceLocation = SourceLocation;
        FSekiroAnimIRLink& ToOrientationLink = Graph.Links.AddDefaulted_GetRef();
        ToOrientationLink.Id = TEXT("Link.Move.ToOrientationWarping");
        ToOrientationLink.Source.NodeId = LocalToComponentId;
        ToOrientationLink.Source.PinName = TEXT("ComponentPose");
        ToOrientationLink.Target.NodeId = OrientationId;
        ToOrientationLink.Target.PinName = TEXT("ComponentPose");
        ToOrientationLink.SourceLocation = SourceLocation;
        FSekiroAnimIRLink& ToLocomotionAngle = Graph.Links.AddDefaulted_GetRef();
        ToLocomotionAngle.Id = TEXT("Link.Move.LocomotionAngleToOrientationWarping");
        ToLocomotionAngle.Source.NodeId = LocomotionGetterId;
        ToLocomotionAngle.Source.PinName = TEXT("Value");
        ToLocomotionAngle.Target.NodeId = OrientationId;
        ToLocomotionAngle.Target.PinName = TEXT("LocomotionAngle");
        ToLocomotionAngle.SourceLocation = SourceLocation;
        FSekiroAnimIRLink& ToLocalLink = Graph.Links.AddDefaulted_GetRef();
        ToLocalLink.Id = TEXT("Link.Move.ToComponentToLocal");
        ToLocalLink.Source.NodeId = OrientationId;
        ToLocalLink.Source.PinName = TEXT("Pose");
        ToLocalLink.Target.NodeId = ComponentToLocalId;
        ToLocalLink.Target.PinName = TEXT("ComponentPose");
        ToLocalLink.SourceLocation = SourceLocation;

        return true;
    }

    /**
     * 在 OrientationWarping 与 ComponentToLocalSpace 之间插入 FootPlacement 和 LegIK。
     * 函数仅修改调用方独占的 IR，不访问 UObject；测试骨骼名由目标 Skeleton 提供。
     *
     * @param Graph 已包含 OrientationWarping 组件空间链的 StatePose Graph。
     * @param BoneName 用于构造双节点腿定义的有效测试骨骼名。
     * @param SourceLocation 复制到新增节点与连接的 Lua 源位置。
     * @return 找到现有 OrientationWarping 输出连接并成功改写时返回 true，否则返回 false。
     */
    bool AddFootIKChain(
        FSekiroAnimIRGraph& Graph,
        const FName BoneName,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRLink* ToLocalLink = nullptr;
        for (FSekiroAnimIRLink& Link : Graph.Links)
        {
            if (Link.Id == TEXT("Link.Move.ToComponentToLocal"))
            {
                ToLocalLink = &Link;
                break;
            }
        }
        if (ToLocalLink == nullptr || BoneName.IsNone()) return false;

        const FSekiroAnimIRPinEndpoint OrientationSource = ToLocalLink->Source;
        const FString FootPlacementId(TEXT("Node.Move.FootPlacement"));
        const FString LegIKId(TEXT("Node.Move.LegIK"));
        ToLocalLink->Source.NodeId = LegIKId;
        ToLocalLink->Source.PinName = TEXT("Pose");

        FSekiroAnimIRNode& FootPlacement = Graph.Nodes.AddDefaulted_GetRef();
        FootPlacement.Id = FootPlacementId;
        FootPlacement.NodeType = SekiroAnimGraphIRNames::FootPlacementNode;
        FootPlacement.DisplayName = TEXT("Foot Placement");
        FootPlacement.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& FootPlacementInput = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementInput.Name = TEXT("ComponentPose");
        FootPlacementInput.Direction = ESekiroAnimIRPinDirection::Input;
        FootPlacementInput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPin& FootPlacementAlpha = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementAlpha.Name = TEXT("Alpha");
        FootPlacementAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        FootPlacementAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& FootPlacementOutput = FootPlacement.Pins.AddDefaulted_GetRef();
        FootPlacementOutput.Name = TEXT("Pose");
        FootPlacementOutput.Direction = ESekiroAnimIRPinDirection::Output;
        FootPlacementOutput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FootPlacementOutput.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& FootRoot = FootPlacement.Properties.AddDefaulted_GetRef();
        FootRoot.Name = TEXT("IKFootRootBone");
        FootRoot.Value.Type = ESekiroAnimIRValueType::Name;
        FootRoot.Value.NameValue = BoneName;
        FSekiroAnimIRProperty& Pelvis = FootPlacement.Properties.AddDefaulted_GetRef();
        Pelvis.Name = TEXT("PelvisBone");
        Pelvis.Value.Type = ESekiroAnimIRValueType::Name;
        Pelvis.Value.NameValue = BoneName;
        FSekiroAnimIRProperty& FootLegs = FootPlacement.Properties.AddDefaulted_GetRef();
        FootLegs.Name = TEXT("LegDefinitions");
        FootLegs.Value.Type = ESekiroAnimIRValueType::String;
        FootLegs.Value.StringValue = FString::Printf(
            TEXT(" %s , %s , %s , 1 | %s,%s,%s,1 "),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString());
        FSekiroAnimIRProperty& PlantLockType = FootPlacement.Properties.AddDefaulted_GetRef();
        PlantLockType.Name = TEXT("PlantLockType");
        PlantLockType.Value.Type = ESekiroAnimIRValueType::Name;
        PlantLockType.Value.NameValue = TEXT("PivotAroundAnkle");
        FSekiroAnimIRProperty& PelvisMaxOffset = FootPlacement.Properties.AddDefaulted_GetRef();
        PelvisMaxOffset.Name = TEXT("PelvisMaxOffset");
        PelvisMaxOffset.Value.Type = ESekiroAnimIRValueType::Float;
        PelvisMaxOffset.Value.FloatValue = 37.0;
        FSekiroAnimIRProperty& PelvisRebalancing = FootPlacement.Properties.AddDefaulted_GetRef();
        PelvisRebalancing.Name = TEXT("PelvisHorizontalRebalancingWeight");
        PelvisRebalancing.Value.Type = ESekiroAnimIRValueType::Float;
        PelvisRebalancing.Value.FloatValue = 0.4;
        FSekiroAnimIRProperty& PlantSpeedThreshold = FootPlacement.Properties.AddDefaulted_GetRef();
        PlantSpeedThreshold.Name = TEXT("PlantSpeedThreshold");
        PlantSpeedThreshold.Value.Type = ESekiroAnimIRValueType::Float;
        PlantSpeedThreshold.Value.FloatValue = 45.0;
        FSekiroAnimIRProperty& PlantDistance = FootPlacement.Properties.AddDefaulted_GetRef();
        PlantDistance.Name = TEXT("PlantDistanceToGround");
        PlantDistance.Value.Type = ESekiroAnimIRValueType::Float;
        PlantDistance.Value.FloatValue = 8.0;
        FSekiroAnimIRProperty& TraceStart = FootPlacement.Properties.AddDefaulted_GetRef();
        TraceStart.Name = TEXT("TraceStartOffset");
        TraceStart.Value.Type = ESekiroAnimIRValueType::Float;
        TraceStart.Value.FloatValue = -55.0;
        FSekiroAnimIRProperty& TraceEnd = FootPlacement.Properties.AddDefaulted_GetRef();
        TraceEnd.Name = TEXT("TraceEndOffset");
        TraceEnd.Value.Type = ESekiroAnimIRValueType::Float;
        TraceEnd.Value.FloatValue = 90.0;
        FSekiroAnimIRProperty& TraceRadius = FootPlacement.Properties.AddDefaulted_GetRef();
        TraceRadius.Name = TEXT("TraceSweepRadius");
        TraceRadius.Value.Type = ESekiroAnimIRValueType::Float;
        TraceRadius.Value.FloatValue = 6.0;
        FSekiroAnimIRProperty& TracePenetration = FootPlacement.Properties.AddDefaulted_GetRef();
        TracePenetration.Name = TEXT("TraceMaxGroundPenetration");
        TracePenetration.Value.Type = ESekiroAnimIRValueType::Float;
        TracePenetration.Value.FloatValue = 7.0;
        FSekiroAnimIRProperty& TraceEnabled = FootPlacement.Properties.AddDefaulted_GetRef();
        TraceEnabled.Name = TEXT("bTraceEnabled");
        TraceEnabled.Value.Type = ESekiroAnimIRValueType::Bool;
        TraceEnabled.Value.BoolValue = true;

        FSekiroAnimIRNode& LegIK = Graph.Nodes.AddDefaulted_GetRef();
        LegIK.Id = LegIKId;
        LegIK.NodeType = SekiroAnimGraphIRNames::LegIKNode;
        LegIK.DisplayName = TEXT("Leg IK");
        LegIK.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& LegIKInput = LegIK.Pins.AddDefaulted_GetRef();
        LegIKInput.Name = TEXT("ComponentPose");
        LegIKInput.Direction = ESekiroAnimIRPinDirection::Input;
        LegIKInput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPin& LegIKAlpha = LegIK.Pins.AddDefaulted_GetRef();
        LegIKAlpha.Name = TEXT("Alpha");
        LegIKAlpha.Direction = ESekiroAnimIRPinDirection::Input;
        LegIKAlpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& LegIKOutput = LegIK.Pins.AddDefaulted_GetRef();
        LegIKOutput.Name = TEXT("Pose");
        LegIKOutput.Direction = ESekiroAnimIRPinDirection::Output;
        LegIKOutput.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        LegIKOutput.bAllowMultipleConnections = true;
        FSekiroAnimIRProperty& LegIKLegs = LegIK.Properties.AddDefaulted_GetRef();
        LegIKLegs.Name = TEXT("LegDefinitions");
        LegIKLegs.Value.Type = ESekiroAnimIRValueType::String;
        LegIKLegs.Value.StringValue = FString::Printf(
            TEXT(" %s , %s , 1 | %s,%s,1 "),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString(),
            *BoneName.ToString());
        FSekiroAnimIRProperty& ReachPrecision = LegIK.Properties.AddDefaulted_GetRef();
        ReachPrecision.Name = TEXT("ReachPrecision");
        ReachPrecision.Value.Type = ESekiroAnimIRValueType::Float;
        ReachPrecision.Value.FloatValue = 0.05;
        FSekiroAnimIRProperty& MaxIterations = LegIK.Properties.AddDefaulted_GetRef();
        MaxIterations.Name = TEXT("MaxIterations");
        MaxIterations.Value.Type = ESekiroAnimIRValueType::Integer;
        MaxIterations.Value.IntegerValue = 7;

        FSekiroAnimIRLink& ToFootPlacement = Graph.Links.AddDefaulted_GetRef();
        ToFootPlacement.Id = TEXT("Link.Move.ToFootPlacement");
        ToFootPlacement.Source = OrientationSource;
        ToFootPlacement.Target.NodeId = FootPlacementId;
        ToFootPlacement.Target.PinName = TEXT("ComponentPose");
        ToFootPlacement.SourceLocation = SourceLocation;
        FSekiroAnimIRLink& ToLegIK = Graph.Links.AddDefaulted_GetRef();
        ToLegIK.Id = TEXT("Link.Move.ToLegIK");
        ToLegIK.Source.NodeId = FootPlacementId;
        ToLegIK.Source.PinName = TEXT("Pose");
        ToLegIK.Target.NodeId = LegIKId;
        ToLegIK.Target.PinName = TEXT("ComponentPose");
        ToLegIK.SourceLocation = SourceLocation;
        return true;
    }

    /**
     * 在 OrientationWarping 与 FootPlacement 之间插入 TwoBoneIK，覆盖骨骼、Socket、空间、位置、拉伸和曲线 Alpha 配置。
     * 函数仅修改调用方独占的 IR，不访问 UObject；测试目标名均由参数或本函数的测试常量提供。
     *
     * @param Graph 已包含 Link.Move.ToFootPlacement 的 StatePose Graph。
     * @param BoneName IKBone 与 JointTarget 使用的有效测试骨骼名。
     * @param SourceLocation 复制到新增节点与连接的 Lua 源位置。
     * @return 找到现有 FootPlacement 输入连接且 BoneName 有效时返回 true，否则返回 false。
     */
    bool AddTwoBoneIKNode(
        FSekiroAnimIRGraph& Graph,
        const FName BoneName,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRLink* ToFootPlacement = nullptr;
        for (FSekiroAnimIRLink& Link : Graph.Links)
        {
            if (Link.Id == TEXT("Link.Move.ToFootPlacement"))
            {
                ToFootPlacement = &Link;
                break;
            }
        }
        if (ToFootPlacement == nullptr || BoneName.IsNone()) return false;

        const FSekiroAnimIRPinEndpoint OriginalSource = ToFootPlacement->Source;
        const FString TwoBoneIKId(TEXT("Node.Move.TwoBoneIK"));
        ToFootPlacement->Source.NodeId = TwoBoneIKId;
        ToFootPlacement->Source.PinName = TEXT("Pose");

        FSekiroAnimIRNode& TwoBoneIK = Graph.Nodes.AddDefaulted_GetRef();
        TwoBoneIK.Id = TwoBoneIKId;
        TwoBoneIK.NodeType = SekiroAnimGraphIRNames::TwoBoneIKNode;
        TwoBoneIK.DisplayName = TEXT("Two Bone IK");
        TwoBoneIK.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& ComponentPose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        ComponentPose.Name = TEXT("ComponentPose");
        ComponentPose.Direction = ESekiroAnimIRPinDirection::Input;
        ComponentPose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        FSekiroAnimIRPin& Alpha = TwoBoneIK.Pins.AddDefaulted_GetRef();
        Alpha.Name = TEXT("Alpha");
        Alpha.Direction = ESekiroAnimIRPinDirection::Input;
        Alpha.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& Pose = TwoBoneIK.Pins.AddDefaulted_GetRef();
        Pose.Name = TEXT("Pose");
        Pose.Direction = ESekiroAnimIRPinDirection::Output;
        Pose.DataType = SekiroAnimGraphIRNames::ComponentPoseData;
        Pose.bAllowMultipleConnections = true;

        const auto AddNameProperty = [&TwoBoneIK](const TCHAR* Name, const FName Value)
        {
            FSekiroAnimIRProperty& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = Name;
            Property.Value.Type = ESekiroAnimIRValueType::Name;
            Property.Value.NameValue = Value;
        };
        const auto AddFloatProperty = [&TwoBoneIK](const TCHAR* Name, const double Value)
        {
            FSekiroAnimIRProperty& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = Name;
            Property.Value.Type = ESekiroAnimIRValueType::Float;
            Property.Value.FloatValue = Value;
        };
        const auto AddBoolProperty = [&TwoBoneIK](const TCHAR* Name, const bool bValue)
        {
            FSekiroAnimIRProperty& Property = TwoBoneIK.Properties.AddDefaulted_GetRef();
            Property.Name = Name;
            Property.Value.Type = ESekiroAnimIRValueType::Bool;
            Property.Value.BoolValue = bValue;
        };
        AddNameProperty(TEXT("IKBone"), BoneName);
        AddNameProperty(TEXT("EffectorLocationSpace"), TEXT("BoneSpace"));
        AddNameProperty(TEXT("EffectorTargetSocketName"), TEXT("FactoryTestEffectorSocket"));
        AddFloatProperty(TEXT("EffectorLocationX"), 11.0);
        AddFloatProperty(TEXT("EffectorLocationY"), 12.0);
        AddFloatProperty(TEXT("EffectorLocationZ"), 13.0);
        AddNameProperty(TEXT("JointTargetLocationSpace"), TEXT("BoneSpace"));
        AddNameProperty(TEXT("JointTargetBoneName"), BoneName);
        AddFloatProperty(TEXT("JointTargetLocationX"), 21.0);
        AddFloatProperty(TEXT("JointTargetLocationY"), 22.0);
        AddFloatProperty(TEXT("JointTargetLocationZ"), 23.0);
        AddBoolProperty(TEXT("bTakeRotationFromEffectorSpace"), true);
        AddBoolProperty(TEXT("bAllowStretching"), true);
        AddFloatProperty(TEXT("StartStretchRatio"), 0.8);
        AddFloatProperty(TEXT("MaxStretchScale"), 1.4);
        AddNameProperty(TEXT("AlphaInputType"), TEXT("Curve"));
        AddNameProperty(TEXT("AlphaCurveName"), TEXT("FactoryTestIKAlpha"));

        FSekiroAnimIRLink& ToTwoBoneIK = Graph.Links.AddDefaulted_GetRef();
        ToTwoBoneIK.Id = TEXT("Link.Move.ToTwoBoneIK");
        ToTwoBoneIK.Source = OriginalSource;
        ToTwoBoneIK.Target.NodeId = TwoBoneIKId;
        ToTwoBoneIK.Target.PinName = TEXT("ComponentPose");
        ToTwoBoneIK.SourceLocation = SourceLocation;
        return true;
    }

    /**
     * 在 StatePose 根输出前插入 Slot 与单层 Layered Blend Per Bone，模拟上半身 Montage 合成链。
     * 函数仅修改调用方独占的值类型 IR；基础姿势同时作为 Slot Source 和 Layered BasePose。
     *
     * @param Graph 已有唯一根输出 Link 的 StatePose Graph。
     * @param BoneName BranchFilters 使用的目标 Skeleton 有效骨骼名。
     * @param SourceLocation 复制到新增节点、属性和连接的 Lua 源位置。
     * @return 找到根输出且骨骼名有效时返回 true，否则不修改 Graph 并返回 false。
     */
    bool AddUpperBodyBlendChain(
        FSekiroAnimIRGraph& Graph,
        const FName BoneName,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRLink* RootLink = nullptr;
        for (FSekiroAnimIRLink& Link : Graph.Links)
        {
            if (Link.Target.NodeId == Graph.RootNodeId && Link.Target.PinName == TEXT("Result"))
            {
                RootLink = &Link;
                break;
            }
        }
        if (RootLink == nullptr || BoneName.IsNone()) return false;

        const FSekiroAnimIRPinEndpoint OriginalSource = RootLink->Source;
        const FString SlotId(TEXT("Node.Move.UpperBodySlot"));
        const FString LayeredId(TEXT("Node.Move.UpperBodyLayeredBlend"));
        RootLink->Source.NodeId = LayeredId;
        RootLink->Source.PinName = TEXT("Pose");

        FSekiroAnimIRNode& Slot = Graph.Nodes.AddDefaulted_GetRef();
        Slot.Id = SlotId;
        Slot.NodeType = SekiroAnimGraphIRNames::SlotNode;
        Slot.DisplayName = TEXT("Upper Body Slot");
        Slot.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& SlotSource = Slot.Pins.AddDefaulted_GetRef();
        SlotSource.Name = TEXT("Source");
        SlotSource.Direction = ESekiroAnimIRPinDirection::Input;
        SlotSource.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPin& SlotPose = Slot.Pins.AddDefaulted_GetRef();
        SlotPose.Name = TEXT("Pose");
        SlotPose.Direction = ESekiroAnimIRPinDirection::Output;
        SlotPose.DataType = SekiroAnimGraphIRNames::PoseData;
        SlotPose.bAllowMultipleConnections = true;
        FSekiroAnimIRProperty& SlotName = Slot.Properties.AddDefaulted_GetRef();
        SlotName.Name = TEXT("SlotName");
        SlotName.Value.Type = ESekiroAnimIRValueType::Name;
        SlotName.Value.NameValue = TEXT("DefaultSlot");
        FSekiroAnimIRProperty& AlwaysUpdate = Slot.Properties.AddDefaulted_GetRef();
        AlwaysUpdate.Name = TEXT("bAlwaysUpdateSourcePose");
        AlwaysUpdate.Value.Type = ESekiroAnimIRValueType::Bool;
        AlwaysUpdate.Value.BoolValue = true;

        FSekiroAnimIRNode& Layered = Graph.Nodes.AddDefaulted_GetRef();
        Layered.Id = LayeredId;
        Layered.NodeType = SekiroAnimGraphIRNames::LayeredBlendPerBoneNode;
        Layered.DisplayName = TEXT("Upper Body Layered Blend");
        Layered.SourceLocation = SourceLocation;
        const TCHAR* LayeredPoseInputs[] = { TEXT("BasePose"), TEXT("BlendPose") };
        for (const TCHAR* PinName : LayeredPoseInputs)
        {
            FSekiroAnimIRPin& Pin = Layered.Pins.AddDefaulted_GetRef();
            Pin.Name = PinName;
            Pin.Direction = ESekiroAnimIRPinDirection::Input;
            Pin.DataType = SekiroAnimGraphIRNames::PoseData;
        }
        FSekiroAnimIRPin& BlendWeight = Layered.Pins.AddDefaulted_GetRef();
        BlendWeight.Name = TEXT("BlendWeight");
        BlendWeight.Direction = ESekiroAnimIRPinDirection::Input;
        BlendWeight.DataType = SekiroAnimGraphIRNames::FloatData;
        FSekiroAnimIRPin& LayeredPose = Layered.Pins.AddDefaulted_GetRef();
        LayeredPose.Name = TEXT("Pose");
        LayeredPose.Direction = ESekiroAnimIRPinDirection::Output;
        LayeredPose.DataType = SekiroAnimGraphIRNames::PoseData;
        LayeredPose.bAllowMultipleConnections = true;
        FSekiroAnimIRProperty& BranchFilters = Layered.Properties.AddDefaulted_GetRef();
        BranchFilters.Name = TEXT("BranchFilters");
        BranchFilters.Value.Type = ESekiroAnimIRValueType::String;
        BranchFilters.Value.StringValue = FString::Printf(TEXT(" %s , 3 "), *BoneName.ToString());
        FSekiroAnimIRProperty& MeshRotation = Layered.Properties.AddDefaulted_GetRef();
        MeshRotation.Name = TEXT("bMeshSpaceRotationBlend");
        MeshRotation.Value.Type = ESekiroAnimIRValueType::Bool;
        MeshRotation.Value.BoolValue = true;
        FSekiroAnimIRProperty& CurveOption = Layered.Properties.AddDefaulted_GetRef();
        CurveOption.Name = TEXT("CurveBlendOption");
        CurveOption.Value.Type = ESekiroAnimIRValueType::Name;
        CurveOption.Value.NameValue = TEXT("UseBasePose");

        FSekiroAnimIRLink& ToSlot = Graph.Links.AddDefaulted_GetRef();
        ToSlot.Id = TEXT("Link.Move.ToUpperBodySlot");
        ToSlot.Source = OriginalSource;
        ToSlot.Target.NodeId = SlotId;
        ToSlot.Target.PinName = TEXT("Source");
        ToSlot.SourceLocation = SourceLocation;
        FSekiroAnimIRLink& SlotToLayered = Graph.Links.AddDefaulted_GetRef();
        SlotToLayered.Id = TEXT("Link.Move.UpperBodySlotToLayered");
        SlotToLayered.Source.NodeId = SlotId;
        SlotToLayered.Source.PinName = TEXT("Pose");
        SlotToLayered.Target.NodeId = LayeredId;
        SlotToLayered.Target.PinName = TEXT("BlendPose");
        SlotToLayered.SourceLocation = SourceLocation;
        return true;
    }

    /**
     * 将主 Pose Graph 的既有输出链改为 Save/Use Cached Pose 对，并让 Save 缓存指定源节点的 Pose。
     * 函数仅修改调用方独占的值类型 IR，不访问 UObject；调用方应保证 Graph 当前只有一条待替换的根输出 Link。
     *
     * @param Graph 目标 Pose Graph，必须已有 RootNodeId 对应的 OutputPose。
     * @param SourceNodeId 要送入 Save Cached Pose 的同图源节点稳定 ID。
     * @param CacheName Save 与 Use 共用的非空缓存名。
     * @param SourceLocation 新节点与 Link 使用的 Lua 源位置。
     * @return 无返回值。
     */
    void AddCachedPosePair(
        FSekiroAnimIRGraph& Graph,
        const FString& SourceNodeId,
        const FString& CacheName,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        Graph.Links.Reset();
        const FString SaveNodeId(TEXT("Node.Main.SaveCachedPose"));
        const FString UseNodeId(TEXT("Node.Main.UseCachedPose"));

        FSekiroAnimIRNode& SaveNode = Graph.Nodes.AddDefaulted_GetRef();
        SaveNode.Id = SaveNodeId;
        SaveNode.NodeType = SekiroAnimGraphIRNames::SaveCachedPoseNode;
        SaveNode.DisplayName = TEXT("Save Main Pose");
        SaveNode.SourceLocation = SourceLocation;

        FSekiroAnimIRPin& SavePose = SaveNode.Pins.AddDefaulted_GetRef();
        SavePose.Name = TEXT("Pose");
        SavePose.Direction = ESekiroAnimIRPinDirection::Input;
        SavePose.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRProperty& SaveCacheName = SaveNode.Properties.AddDefaulted_GetRef();
        SaveCacheName.Name = TEXT("CacheName");
        SaveCacheName.Value.Type = ESekiroAnimIRValueType::String;
        SaveCacheName.Value.StringValue = CacheName;

        FSekiroAnimIRNode& UseNode = Graph.Nodes.AddDefaulted_GetRef();
        UseNode.Id = UseNodeId;
        UseNode.NodeType = SekiroAnimGraphIRNames::UseCachedPoseNode;
        UseNode.DisplayName = TEXT("Use Main Pose");
        UseNode.SourceLocation = SourceLocation;

        FSekiroAnimIRPin& UsePose = UseNode.Pins.AddDefaulted_GetRef();
        UsePose.Name = TEXT("Pose");
        UsePose.Direction = ESekiroAnimIRPinDirection::Output;
        UsePose.DataType = SekiroAnimGraphIRNames::PoseData;
        UsePose.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& UseCacheName = UseNode.Properties.AddDefaulted_GetRef();
        UseCacheName.Name = TEXT("CacheName");
        UseCacheName.Value.Type = ESekiroAnimIRValueType::String;
        UseCacheName.Value.StringValue = CacheName;

        FSekiroAnimIRLink& SaveLink = Graph.Links.AddDefaulted_GetRef();
        SaveLink.Id = TEXT("Link.Main.SourceToSaveCachedPose");
        SaveLink.Source.NodeId = SourceNodeId;
        SaveLink.Source.PinName = TEXT("Pose");
        SaveLink.Target.NodeId = SaveNodeId;
        SaveLink.Target.PinName = TEXT("Pose");
        SaveLink.SourceLocation = SourceLocation;

        FSekiroAnimIRLink& UseLink = Graph.Links.AddDefaulted_GetRef();
        UseLink.Id = TEXT("Link.Main.UseCachedPoseToOutput");
        UseLink.Source.NodeId = UseNodeId;
        UseLink.Source.PinName = TEXT("Pose");
        UseLink.Target.NodeId = Graph.RootNodeId;
        UseLink.Target.PinName = TEXT("Result");
        UseLink.SourceLocation = SourceLocation;
    }

    /**
     * 向 IR 首层追加带 StateResult 根节点的 StatePose Graph。
     * 函数仅修改值类型数组，不访问 UObject；返回引用在 Layer.Graphs 再次扩容后失效。
     *
     * @param Blueprint 具有至少一个 Layer 的可变测试 IR。
     * @param GraphId 新 Graph 稳定 ID。
     * @param GraphName 编辑器显示名。
     * @param RootNodeId 新 StateResult 节点稳定 ID。
     * @return 新增 Graph 的数组元素引用。
     */
    FSekiroAnimIRGraph& AddStatePoseGraph(
        FSekiroAnimBlueprintIR& Blueprint,
        const FString& GraphId,
        const FString& GraphName,
        const FString& RootNodeId)
    {
        FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs.AddDefaulted_GetRef();
        Graph.Id = GraphId;
        Graph.Name = GraphName;
        Graph.GraphType = SekiroAnimGraphIRNames::StatePoseGraph;
        Graph.RootNodeId = RootNodeId;
        Graph.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRNode& RootNode = Graph.Nodes.AddDefaulted_GetRef();
        RootNode.Id = RootNodeId;
        RootNode.NodeType = SekiroAnimGraphIRNames::StateResultNode;
        RootNode.DisplayName = TEXT("State Result");
        RootNode.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRPin& ResultPin = RootNode.Pins.AddDefaulted_GetRef();
        ResultPin.Name = TEXT("Result");
        ResultPin.Direction = ESekiroAnimIRPinDirection::Input;
        ResultPin.DataType = SekiroAnimGraphIRNames::PoseData;
        return Graph;
    }

    /**
     * 构造包含八个有效 State、一个孤立 State、Inertialization 与并行 Transition 的完整工厂测试 IR。
     * 函数修改值类型 IR，并将调用方提供的 transient Sequence 写为软路径；只能在测试线程独占调用。
     *
     * @param Sequence 工厂应加载并写入 SequencePlayer 的动画序列。
     * @return 可通过 Validator 且覆盖首批原生 NodeFactory 能力的 IR。
     */
    FSekiroAnimBlueprintIR MakeFactoryIR(UAnimSequenceBase* Sequence)
    {
        FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
        FSekiroAnimIRVariable& GraphLocomotionAngle =
            Blueprint.Variables.AddDefaulted_GetRef();
        GraphLocomotionAngle.Name = TEXT("GraphLocomotionAngle");
        GraphLocomotionAngle.DataType = TEXT("Float");
        GraphLocomotionAngle.DefaultValue.Type = ESekiroAnimIRValueType::Float;
        GraphLocomotionAngle.DefaultValue.FloatValue = 0.0;
        GraphLocomotionAngle.bTransient = true;
        GraphLocomotionAngle.SourceLocation = Blueprint.SourceLocation;
        FSekiroAnimIRGraph* MainGraph = FindGraph(Blueprint, TEXT("Graph.Main"));
        if (MainGraph != nullptr)
        {
            AddCachedPosePair(
                *MainGraph,
                TEXT("Node.StateMachine"),
                TEXT("MainPoseCache"),
                Blueprint.SourceLocation);
            MainGraph->Layout.Style = ESekiroAnimIRLayoutStyle::LeftToRight;
            FSekiroAnimIRLayoutGrid& MainGrid = MainGraph->Layout.Grids.AddDefaulted_GetRef();
            MainGrid.Name = TEXT("MainFlow");
            FSekiroAnimIRLayoutItem& MachineItem = MainGrid.Items.AddDefaulted_GetRef();
            MachineItem.ElementId = TEXT("Node.StateMachine");
            FSekiroAnimIRLayoutItem& RootItem = MainGrid.Items.AddDefaulted_GetRef();
            RootItem.ElementId = TEXT("Node.Output");
            RootItem.Column = 2;
            RootItem.DeclarationOrder = 1;
            FSekiroAnimIRLayoutPosition& MachinePosition =
                MainGraph->Layout.Positions.AddDefaulted_GetRef();
            MachinePosition.ElementId = TEXT("Node.StateMachine");
            MachinePosition.X = 137;
            MachinePosition.Y = 419;
        }

        FSekiroAnimIRGraph* IdleGraph = FindGraph(Blueprint, TEXT("Graph.Idle"));
        if (IdleGraph != nullptr)
        {
            AddSequencePose(
                *IdleGraph,
                TEXT("Node.Idle"),
                FSoftObjectPath(Sequence),
                false,
                Blueprint.SourceLocation);
        }

        FSekiroAnimIRGraph* StateMachineGraph = FindGraph(Blueprint, TEXT("Graph.StateMachine"));
        if (StateMachineGraph != nullptr)
        {
            FSekiroAnimIRState& MoveState = StateMachineGraph->StateMachine.States.AddDefaulted_GetRef();
            MoveState.Id = TEXT("State.Move");
            MoveState.Name = TEXT("Move");
            MoveState.GraphId = TEXT("Graph.Move");
            MoveState.bAlwaysResetOnEntry = true;
            MoveState.DeclarationOrder = 1;
            MoveState.SourceLocation = Blueprint.SourceLocation;

            FSekiroAnimIRTransition& FirstTransition =
                StateMachineGraph->StateMachine.Transitions.AddDefaulted_GetRef();
            FirstTransition.Id = TEXT("Transition.IdleToMove.Primary");
            FirstTransition.Key = TEXT("IdleToMovePrimary");
            FirstTransition.SourceStateId = TEXT("State.Idle");
            FirstTransition.TargetStateId = TEXT("State.Move");
            FirstTransition.RuleFunctionName = TEXT("CanEnter_Idle_Move_Primary");
            FirstTransition.Settings.BlendDuration = 0.35f;
            FirstTransition.Settings.PriorityOrder = 2;
            FirstTransition.Settings.BlendMode = TEXT("Linear");
            FirstTransition.SourceLocation = Blueprint.SourceLocation;

            FSekiroAnimIRTransitionGateNode& StopCurveGate =
                FirstTransition.Gate.Nodes.AddDefaulted_GetRef();
            StopCurveGate.Type = TEXT("CurveGreaterEqual");
            StopCurveGate.Name = TEXT("CanEnterStop");
            StopCurveGate.Threshold = 0.5f;

            FSekiroAnimIRTransitionGateNode& StopTimeGate =
                FirstTransition.Gate.Nodes.AddDefaulted_GetRef();
            StopTimeGate.Type = TEXT("TimeRemainingLessEqual");
            StopTimeGate.Threshold = 0.12f;

            FSekiroAnimIRTransitionGateNode& StopAnyGate =
                FirstTransition.Gate.Nodes.AddDefaulted_GetRef();
            StopAnyGate.Type = TEXT("Any");
            StopAnyGate.Children = { 0, 1 };
            FirstTransition.Gate.RootIndex = 2;

            FSekiroAnimIRTransition& ParallelTransition =
                StateMachineGraph->StateMachine.Transitions.AddDefaulted_GetRef();
            ParallelTransition.Id = TEXT("Transition.IdleToMove.Alternate");
            ParallelTransition.Key = TEXT("IdleToMoveAlternate");
            ParallelTransition.SourceStateId = TEXT("State.Idle");
            ParallelTransition.TargetStateId = TEXT("State.Move");
            ParallelTransition.RuleFunctionName = TEXT("CanEnter_Idle_Move_Alternate");
            ParallelTransition.Settings.BlendDuration = 0.1f;
            ParallelTransition.Settings.PriorityOrder = 3;
            ParallelTransition.Settings.BlendMode = TEXT("Linear");
            ParallelTransition.SourceLocation = Blueprint.SourceLocation;

            FSekiroAnimIRTransitionGateNode& RemainingTimeGate =
                ParallelTransition.Gate.Nodes.AddDefaulted_GetRef();
            RemainingTimeGate.Type = TEXT("TimeRemainingLessEqual");
            RemainingTimeGate.Threshold = 0.08f;
            ParallelTransition.Gate.RootIndex = 0;

            FString PreviousStateId = MoveState.Id;
            for (int32 StateIndex = 2; StateIndex <= 7; ++StateIndex)
            {
                const FString StateSuffix = FString::FromInt(StateIndex);
                FSekiroAnimIRState& AdditionalState =
                    StateMachineGraph->StateMachine.States.AddDefaulted_GetRef();
                AdditionalState.Id = TEXT("State.Grid") + StateSuffix;
                AdditionalState.Name = TEXT("Grid") + StateSuffix;
                AdditionalState.GraphId = TEXT("Graph.Grid") + StateSuffix;
                AdditionalState.DeclarationOrder = StateIndex;
                AdditionalState.SourceLocation = Blueprint.SourceLocation;

                FSekiroAnimIRTransition& GridTransition =
                    StateMachineGraph->StateMachine.Transitions.AddDefaulted_GetRef();
                GridTransition.Id = TEXT("Transition.Grid") + StateSuffix;
                GridTransition.Key = TEXT("Grid") + StateSuffix;
                GridTransition.SourceStateId = PreviousStateId;
                GridTransition.TargetStateId = AdditionalState.Id;
                GridTransition.RuleFunctionName = FName(*(TEXT("CanEnter_Grid") + StateSuffix));
                GridTransition.Settings.BlendDuration = 0.1f;
                GridTransition.Settings.PriorityOrder = StateIndex;
                GridTransition.Settings.BlendMode = TEXT("Linear");
                GridTransition.SourceLocation = Blueprint.SourceLocation;
                FSekiroAnimIRTransitionGateNode& GridGate =
                    GridTransition.Gate.Nodes.AddDefaulted_GetRef();
                GridGate.Type = TEXT("TimeRemainingLessEqual");
                GridGate.Threshold = 0.1f;
                GridTransition.Gate.RootIndex = 0;
                PreviousStateId = AdditionalState.Id;
            }

            FSekiroAnimIRState& IsolatedState =
                StateMachineGraph->StateMachine.States.AddDefaulted_GetRef();
            IsolatedState.Id = TEXT("State.Isolated");
            IsolatedState.Name = TEXT("Isolated");
            IsolatedState.GraphId = TEXT("Graph.Isolated");
            IsolatedState.DeclarationOrder = 8;
            IsolatedState.SourceLocation = Blueprint.SourceLocation;

            FSekiroAnimIRLayoutGrid& IsolatedGrid =
                StateMachineGraph->Layout.Grids.AddDefaulted_GetRef();
            IsolatedGrid.Name = TEXT("IsolatedGrid");
            FSekiroAnimIRLayoutItem& IsolatedGridItem =
                IsolatedGrid.Items.AddDefaulted_GetRef();
            IsolatedGridItem.ElementId = IsolatedState.Id;
            FSekiroAnimIRLayoutPosition& IsolatedPosition =
                StateMachineGraph->Layout.Positions.AddDefaulted_GetRef();
            IsolatedPosition.ElementId = IsolatedState.Id;
            IsolatedPosition.X = -900;
            IsolatedPosition.Y = 900;
        }

        FSekiroAnimIRGraph& MoveGraph = AddStatePoseGraph(
            Blueprint,
            TEXT("Graph.Move"),
            TEXT("Move"),
            TEXT("Node.Move.Output"));
        AddSequencePose(
            MoveGraph,
            TEXT("Node.Move"),
            FSoftObjectPath(Sequence),
            true,
            Blueprint.SourceLocation);
        for (int32 StateIndex = 2; StateIndex <= 7; ++StateIndex)
        {
            const FString StateSuffix = FString::FromInt(StateIndex);
            FSekiroAnimIRGraph& GridGraph = AddStatePoseGraph(
                Blueprint,
                TEXT("Graph.Grid") + StateSuffix,
                TEXT("Grid") + StateSuffix,
                TEXT("Node.Grid") + StateSuffix + TEXT(".Output"));
            AddSequencePose(
                GridGraph,
                TEXT("Node.Grid") + StateSuffix,
                FSoftObjectPath(Sequence),
                false,
                Blueprint.SourceLocation);
        }
        FSekiroAnimIRGraph& IsolatedGraph = AddStatePoseGraph(
            Blueprint,
            TEXT("Graph.Isolated"),
            TEXT("Isolated"),
            TEXT("Node.Isolated.Output"));
        AddSequencePose(
            IsolatedGraph,
            TEXT("Node.Isolated"),
            FSoftObjectPath(Sequence),
            false,
            Blueprint.SourceLocation);
        return Blueprint;
    }

    /**
     * 在 AnimBlueprint FunctionGraphs 中定位主 UAnimationGraph。
     * 函数只读访问 Blueprint UObject，只能在游戏线程调用。
     *
     * @param Blueprint 工厂生成的 AnimBlueprint，可为空。
     * @return 找到的非 StateGraph 主 AnimGraph，否则返回 nullptr。
     */
    UAnimationGraph* FindMainGraph(const UAnimBlueprint* Blueprint)
    {
        if (Blueprint == nullptr) return nullptr;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph);
            if (AnimationGraph != nullptr && !AnimationGraph->IsA<UAnimationStateGraph>())
            {
                return AnimationGraph;
            }
        }

        return nullptr;
    }

    /**
     * 按 BoundGraph 名称查找状态机中的原生 State。
     * 函数只读访问 Graph.Nodes，只能在游戏线程调用。
     *
     * @param Graph 原生 StateMachine Graph，可为空。
     * @param StateName 期望状态名。
     * @return 找到时返回状态节点，否则返回 nullptr。
     */
    UAnimStateNode* FindState(const UAnimationStateMachineGraph* Graph, const FString& StateName)
    {
        if (Graph == nullptr) return nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
            if (StateNode != nullptr && StateNode->GetStateName() == StateName) return StateNode;
        }

        return nullptr;
    }

    /**
     * 构造一个只声明 SourcePose 输入和 Result 输出的最小 Animation Layer Interface Layer。
     * 函数只创建值类型测试 IR，不访问 UObject；返回值可直接加入接口 Blueprint 的 Layers 数组。
     *
     * @param LayerName 同时作为稳定 ID 后缀、函数名和 Graph 名的非空名称。
     * @param SourceLocation 测试 IR 的 Lua 位置，按值复制到全部声明。
     * @param DeclarationOrder 接口函数的稳定声明顺序。
     * @return 可通过 Validator 的独立 Pose Layer，每次调用使用互不冲突的稳定 ID。
     */
    FSekiroAnimIRLayer MakePoseInterfaceLayer(
        const FString& LayerName,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        const int32 DeclarationOrder)
    {
        FSekiroAnimIRLayer Layer;
        Layer.Id = TEXT("Layer.") + LayerName;
        Layer.Name = LayerName;
        Layer.FunctionName = FName(*LayerName);
        Layer.RootGraphId = TEXT("Graph.") + LayerName;
        Layer.DeclarationOrder = DeclarationOrder;
        Layer.SourceLocation = SourceLocation;

        FSekiroAnimIRFunctionParameter& SourcePose =
            Layer.Parameters.AddDefaulted_GetRef();
        SourcePose.Name = TEXT("SourcePose");
        SourcePose.DataType = SekiroAnimGraphIRNames::PoseData;
        SourcePose.bIsPose = true;
        SourcePose.SourceLocation = SourceLocation;

        FSekiroAnimIRGraph& Graph = Layer.Graphs.AddDefaulted_GetRef();
        Graph.Id = Layer.RootGraphId;
        Graph.Name = LayerName;
        Graph.GraphType = SekiroAnimGraphIRNames::PoseGraph;
        Graph.RootNodeId = TEXT("Node.") + LayerName + TEXT(".Output");
        Graph.SourceLocation = SourceLocation;

        FSekiroAnimIRNode& Output = Graph.Nodes.AddDefaulted_GetRef();
        Output.Id = Graph.RootNodeId;
        Output.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
        Output.SourceLocation = SourceLocation;
        FSekiroAnimIRPin& Result = Output.Pins.AddDefaulted_GetRef();
        Result.Name = TEXT("Result");
        Result.Direction = ESekiroAnimIRPinDirection::Input;
        Result.DataType = SekiroAnimGraphIRNames::PoseData;
        return Layer;
    }

    /**
     * 向 Pose Graph 追加一个引用指定接口函数的自层 Linked Anim Layer IR 节点。
     * 节点故意不声明 InstanceClass，用于覆盖宿主实现接口但 SkeletonGeneratedClass 尚未重编译的生成时序。
     * 函数只修改调用方独占的值类型 Graph，不加载接口类。
     *
     * @param Graph 接收节点的 Pose Graph。
     * @param NodeId 节点稳定 ID。
     * @param LayerName 接口函数名。
     * @param InterfaceClassPath 已生成 Animation Layer Interface 类软路径。
     * @param SourceLocation 测试 IR 的 Lua 位置。
     * @return 新增节点引用；Graph.Nodes 再次扩容后引用失效。
     */
    FSekiroAnimIRNode& AddSelfLinkedAnimLayerNode(
        FSekiroAnimIRGraph& Graph,
        const FString& NodeId,
        const FName LayerName,
        const FSoftClassPath& InterfaceClassPath,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRNode& Node = Graph.Nodes.AddDefaulted_GetRef();
        Node.Id = NodeId;
        Node.NodeType = SekiroAnimGraphIRNames::LinkedAnimLayerNode;
        Node.SourceLocation = SourceLocation;

        FSekiroAnimIRPin& SourcePose = Node.Pins.AddDefaulted_GetRef();
        SourcePose.Name = TEXT("SourcePose");
        SourcePose.Direction = ESekiroAnimIRPinDirection::Input;
        SourcePose.DataType = SekiroAnimGraphIRNames::PoseData;
        FSekiroAnimIRPin& Pose = Node.Pins.AddDefaulted_GetRef();
        Pose.Name = TEXT("Pose");
        Pose.Direction = ESekiroAnimIRPinDirection::Output;
        Pose.DataType = SekiroAnimGraphIRNames::PoseData;
        Pose.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& LayerNameProperty =
            Node.Properties.AddDefaulted_GetRef();
        LayerNameProperty.Name = TEXT("LayerName");
        LayerNameProperty.Value.Type = ESekiroAnimIRValueType::Name;
        LayerNameProperty.Value.NameValue = LayerName;
        FSekiroAnimIRProperty& InterfaceClassProperty =
            Node.Properties.AddDefaulted_GetRef();
        InterfaceClassProperty.Name = TEXT("InterfaceClass");
        InterfaceClassProperty.Value.Type =
            ESekiroAnimIRValueType::SoftClassPath;
        InterfaceClassProperty.Value.SoftClassPathValue = InterfaceClassPath;
        return Node;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryNativeTopologyTest,
    "Sekiro.AnimGraphIR.Factory.NativeTopology",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证工厂创建原生 AnimBlueprint、默认根、状态机拓扑、并行 Transition、节点属性和最终编译结果。
 * 测试创建 transient UObject，不写入磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryNativeTopologyTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    TestNotNull(TEXT("Test Skeleton loads"), Skeleton);
    TestNotNull(TEXT("Transient test Sequence is created"), Sequence);
    if (Sequence == nullptr) return true;

    const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
    const FName OrientationTestBone = ReferenceSkeleton.GetNum() > 0
        ? ReferenceSkeleton.GetBoneName(0)
        : NAME_None;
    TestFalse(TEXT("Test Skeleton exposes a bone for Orientation Warping"), OrientationTestBone.IsNone());
    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRGraph* OrientationGraph = FindGraph(BlueprintIR, TEXT("Graph.Move"));
    TestTrue(
        TEXT("Test IR adds an explicit component-space Orientation Warping chain"),
        OrientationGraph != nullptr
            && AddOrientationWarpingChain(
                *OrientationGraph,
                OrientationTestBone,
                BlueprintIR.SourceLocation));
    TestTrue(
        TEXT("Test IR adds explicit FootPlacement and LegIK nodes"),
        OrientationGraph != nullptr
            && AddFootIKChain(
                *OrientationGraph,
                OrientationTestBone,
                BlueprintIR.SourceLocation));
    TestTrue(
        TEXT("Test IR adds a configurable TwoBoneIK node"),
        OrientationGraph != nullptr
            && AddTwoBoneIKNode(
                *OrientationGraph,
                OrientationTestBone,
                BlueprintIR.SourceLocation));
    TestTrue(
        TEXT("Test IR adds Slot and Layered Blend Per Bone nodes"),
        OrientationGraph != nullptr
            && AddUpperBodyBlendChain(
                *OrientationGraph,
                OrientationTestBone,
                BlueprintIR.SourceLocation));
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* FirstBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
    {
        AddInfo(FString::Printf(
            TEXT("Factory diagnostic %s: %s"),
            *Diagnostic.Code.ToString(),
            *Diagnostic.Message));
    }
    TestNotNull(TEXT("Factory creates native AnimBlueprint"), FirstBlueprint);
    TestEqual(TEXT("Successful build has no diagnostics"), Diagnostics.Num(), 0);
    if (FirstBlueprint == nullptr) return true;

    TestEqual(TEXT("TargetSkeleton is assigned"), FirstBlueprint->TargetSkeleton.Get(), Skeleton);
    TestFalse(TEXT("Lua AnimBlueprint disables threaded animation update"),
        FirstBlueprint->bUseMultiThreadedAnimationUpdate);
    TestNotNull(TEXT("GeneratedClass exists"), FirstBlueprint->GeneratedClass.Get());
    const UAnimInstance* GeneratedDefaultInstance = FirstBlueprint->GeneratedClass != nullptr
        ? Cast<UAnimInstance>(FirstBlueprint->GeneratedClass->GetDefaultObject())
        : nullptr;
    TestNotNull(TEXT("Generated AnimInstance default object exists"), GeneratedDefaultInstance);
    TestFalse(TEXT("Generated AnimInstance also disables threaded animation update"),
        GeneratedDefaultInstance != nullptr && GeneratedDefaultInstance->bUseMultiThreadedAnimationUpdate);
    TestTrue(TEXT("Blueprint compiles without error"), FirstBlueprint->Status != BS_Error);

    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(FirstBlueprint);
    int32 UpdateEventCount = 0;
    if (EventGraph != nullptr)
    {
        for (UEdGraphNode* Node : EventGraph->Nodes)
        {
            UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            if (EventNode != nullptr
                && EventNode->bOverrideFunction
                && EventNode->GetFunctionName()
                    == GET_FUNCTION_NAME_CHECKED(UAnimInstance, BlueprintUpdateAnimation))
            {
                ++UpdateEventCount;
            }
        }
    }
    TestNotNull(TEXT("Generated EventGraph exists"), EventGraph);
    TestEqual(TEXT("BlueprintUpdateAnimation is a real override Event"), UpdateEventCount, 1);
    TestEqual(
        TEXT("EventGraph contains one Lua animation update bridge"),
        CountFunctionCalls(
            EventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateBlueprintUpdateAnimation)),
        1);
    TestEqual(
        TEXT("EventGraph does not pre-evaluate Transition rules"),
        CountFunctionCalls(
            EventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateLuaTransitionRule)),
        0);

    UAnimationGraph* MainGraph = FindMainGraph(FirstBlueprint);
    UAnimGraphNode_Root* RootNode = FindFirstNode<UAnimGraphNode_Root>(MainGraph);
    UAnimGraphNode_StateMachine* StateMachineNode = FindFirstNode<UAnimGraphNode_StateMachine>(MainGraph);
    UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode =
        FindFirstNode<UAnimGraphNode_SaveCachedPose>(MainGraph);
    UAnimGraphNode_UseCachedPose* UseCachedPoseNode =
        FindFirstNode<UAnimGraphNode_UseCachedPose>(MainGraph);
    TestNotNull(TEXT("Main native AnimGraph exists"), MainGraph);
    TestNotNull(TEXT("Existing OutputPose root is reused"), RootNode);
    TestEqual(TEXT("OutputPose root is not duplicated"), CountNodes<UAnimGraphNode_Root>(MainGraph), 1);
    TestNotNull(TEXT("Native StateMachine node exists"), StateMachineNode);
    TestNotNull(TEXT("Native Save Cached Pose node exists"), SaveCachedPoseNode);
    TestNotNull(TEXT("Native Use Cached Pose node exists"), UseCachedPoseNode);
    TestEqual(TEXT("Exact StateMachine X overrides its Grid column"),
        StateMachineNode != nullptr ? StateMachineNode->NodePosX : INDEX_NONE, 137);
    TestEqual(TEXT("Exact StateMachine Y overrides automatic layout"),
        StateMachineNode != nullptr ? StateMachineNode->NodePosY : INDEX_NONE, 419);
    TestEqual(TEXT("Explicit OutputPose Grid column is applied"),
        RootNode != nullptr ? RootNode->NodePosX : INDEX_NONE, 720);
    TestEqual(
        TEXT("Save Cached Pose receives CacheName"),
        SaveCachedPoseNode != nullptr ? SaveCachedPoseNode->CacheName : FString(),
        FString(TEXT("MainPoseCache")));
    TestEqual(
        TEXT("Use Cached Pose binds the same-Graph Save node"),
        UseCachedPoseNode != nullptr ? UseCachedPoseNode->SaveCachedPoseNode.Get() : nullptr,
        SaveCachedPoseNode);

    UEdGraphPin* RootResultPin = RootNode != nullptr ? RootNode->FindPin(TEXT("Result"), EGPD_Input) : nullptr;
    UEdGraphPin* SavePosePin = SaveCachedPoseNode != nullptr
        ? SaveCachedPoseNode->FindPin(TEXT("Pose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* UsePosePin = UseCachedPoseNode != nullptr
        ? UseCachedPoseNode->FindPin(TEXT("Pose"), EGPD_Output)
        : nullptr;
    TestNotNull(TEXT("OutputPose Result pin exists"), RootResultPin);
    TestNotNull(TEXT("Save Cached Pose input pin is named Pose"), SavePosePin);
    TestNotNull(TEXT("Use Cached Pose output pin is named Pose"), UsePosePin);
    TestEqual(
        TEXT("Use Cached Pose is connected to OutputPose"),
        RootResultPin != nullptr ? RootResultPin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("StateMachine pose is connected to Save Cached Pose"),
        SavePosePin != nullptr ? SavePosePin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("Compiled Save CachePoseName matches IR"),
        SaveCachedPoseNode != nullptr ? SaveCachedPoseNode->Node.CachePoseName : NAME_None,
        FName(TEXT("MainPoseCache")));
    TestEqual(
        TEXT("Compiled Use CachePoseName matches IR"),
        UseCachedPoseNode != nullptr ? UseCachedPoseNode->Node.CachePoseName : NAME_None,
        FName(TEXT("MainPoseCache")));

    UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode != nullptr
        ? StateMachineNode->EditorStateMachineGraph
        : nullptr;
    TestNotNull(TEXT("StateMachine owns native graph"), StateMachineGraph);
    TestNotNull(TEXT("StateMachine default Entry exists"), StateMachineGraph != nullptr ? StateMachineGraph->EntryNode.Get() : nullptr);
    TestEqual(TEXT("Eight connected states and one isolated state are created"),
        CountNodes<UAnimStateNode>(StateMachineGraph), 9);
    TestEqual(TEXT("Parallel and grid transitions are preserved"),
        CountNodes<UAnimStateTransitionNode>(StateMachineGraph), 8);

    UAnimStateNode* IdleState = FindState(StateMachineGraph, TEXT("Idle"));
    UAnimStateNode* MoveState = FindState(StateMachineGraph, TEXT("Move"));
    TestNotNull(TEXT("Idle state exists"), IdleState);
    TestNotNull(TEXT("Move state exists"), MoveState);
    TestEqual(TEXT("First row starts with entry State"),
        IdleState != nullptr ? IdleState->NodePosX : INDEX_NONE, 0);
    TestEqual(TEXT("Eight effective states use three-column square-root grid"),
        MoveState != nullptr ? MoveState->NodePosX : INDEX_NONE, 300);
    UAnimStateNode* Grid2State = FindState(StateMachineGraph, TEXT("Grid2"));
    UAnimStateNode* Grid3State = FindState(StateMachineGraph, TEXT("Grid3"));
    UAnimStateNode* Grid7State = FindState(StateMachineGraph, TEXT("Grid7"));
    UAnimStateNode* IsolatedState = FindState(StateMachineGraph, TEXT("Isolated"));
    TestEqual(TEXT("Third effective State completes first grid row"),
        Grid2State != nullptr ? Grid2State->NodePosX : INDEX_NONE, 600);
    TestEqual(TEXT("Fourth effective State starts second grid row"),
        Grid3State != nullptr ? Grid3State->NodePosY : INDEX_NONE, 170);
    TestEqual(TEXT("Eighth effective State occupies third grid row"),
        Grid7State != nullptr ? Grid7State->NodePosY : INDEX_NONE, 340);
    TestTrue(TEXT("Disconnected State is excluded from effective grid and placed below it"),
        IsolatedState != nullptr && IsolatedState->NodePosY > 340);
    TestEqual(TEXT("Exact disconnected State X overrides its Grid"),
        IsolatedState != nullptr ? IsolatedState->NodePosX : INDEX_NONE, -900);
    TestEqual(TEXT("Exact disconnected State Y overrides its Grid"),
        IsolatedState != nullptr ? IsolatedState->NodePosY : INDEX_NONE, 900);
    TestTrue(TEXT("Move AlwaysResetOnEntry is applied"), MoveState != nullptr && MoveState->bAlwaysResetOnEntry);
    TestEqual(
        TEXT("Entry is connected to Idle"),
        StateMachineGraph != nullptr && StateMachineGraph->EntryNode != nullptr
            ? StateMachineGraph->EntryNode->GetOutputNode()
            : nullptr,
        static_cast<UEdGraphNode*>(IdleState));

    UAnimationStateGraph* IdleGraph = IdleState != nullptr ? Cast<UAnimationStateGraph>(IdleState->BoundGraph) : nullptr;
    UAnimationStateGraph* MoveGraph = MoveState != nullptr ? Cast<UAnimationStateGraph>(MoveState->BoundGraph) : nullptr;
    TestNotNull(TEXT("Idle owns UAnimationStateGraph"), IdleGraph);
    TestNotNull(TEXT("Move owns UAnimationStateGraph"), MoveGraph);
    TestNotNull(TEXT("Idle reuses default StateResult"), IdleGraph != nullptr ? IdleGraph->GetResultNode() : nullptr);
    TestNotNull(TEXT("Move reuses default StateResult"), MoveGraph != nullptr ? MoveGraph->GetResultNode() : nullptr);

    UAnimGraphNode_SequencePlayer* IdleSequence = FindFirstNode<UAnimGraphNode_SequencePlayer>(IdleGraph);
    UAnimGraphNode_Inertialization* MoveInertialization = FindFirstNode<UAnimGraphNode_Inertialization>(MoveGraph);
    UAnimGraphNode_LocalToComponentSpace* LocalToComponent =
        FindFirstNode<UAnimGraphNode_LocalToComponentSpace>(MoveGraph);
    UAnimGraphNode_OrientationWarping* OrientationWarping =
        FindFirstNode<UAnimGraphNode_OrientationWarping>(MoveGraph);
    UAnimGraphNode_FootPlacement* FootPlacement =
        FindFirstNode<UAnimGraphNode_FootPlacement>(MoveGraph);
    UAnimGraphNode_LegIK* LegIK = FindFirstNode<UAnimGraphNode_LegIK>(MoveGraph);
    UAnimGraphNode_TwoBoneIK* TwoBoneIK = FindFirstNode<UAnimGraphNode_TwoBoneIK>(MoveGraph);
    UAnimGraphNode_Slot* Slot = FindFirstNode<UAnimGraphNode_Slot>(MoveGraph);
    UAnimGraphNode_LayeredBoneBlend* LayeredBlend =
        FindFirstNode<UAnimGraphNode_LayeredBoneBlend>(MoveGraph);
    UAnimGraphNode_ComponentToLocalSpace* ComponentToLocal =
        FindFirstNode<UAnimGraphNode_ComponentToLocalSpace>(MoveGraph);
    TestNotNull(TEXT("SequencePlayer is created"), IdleSequence);
    TestEqual(
        TEXT("Sequence property is applied"),
        IdleSequence != nullptr ? IdleSequence->Node.GetSequence() : nullptr,
        static_cast<UAnimSequenceBase*>(Sequence));
    TestFalse(TEXT("Loop property is applied"), IdleSequence != nullptr && IdleSequence->Node.GetLoopAnimation());
    TestEqual(
        TEXT("PlayRate property is applied"),
        IdleSequence != nullptr ? IdleSequence->Node.GetPlayRate() : 0.0f,
        1.25f);
    TestNotNull(TEXT("Inertialization node is created"), MoveInertialization);
    UEdGraphPin* InertialSource = MoveInertialization != nullptr
        ? MoveInertialization->FindPin(TEXT("Source"), EGPD_Input)
        : nullptr;
    TestEqual(
        TEXT("Inertialization Source is connected through native Schema"),
        InertialSource != nullptr ? InertialSource->LinkedTo.Num() : 0,
        1);
    TestNotNull(TEXT("LocalToComponentSpace node is explicitly created"), LocalToComponent);
    TestNotNull(TEXT("OrientationWarping node is created"), OrientationWarping);
    TestNotNull(TEXT("FootPlacement node is created"), FootPlacement);
    TestNotNull(TEXT("LegIK node is created"), LegIK);
    TestNotNull(TEXT("TwoBoneIK node is created"), TwoBoneIK);
    TestNotNull(TEXT("Slot node is created"), Slot);
    TestNotNull(TEXT("Layered Blend Per Bone node is created"), LayeredBlend);
    TestNotNull(TEXT("ComponentToLocalSpace node is explicitly created"), ComponentToLocal);
    TestEqual(
        TEXT("Slot receives SlotName"),
        Slot != nullptr ? Slot->Node.SlotName : NAME_None,
        FName(TEXT("DefaultSlot")));
    TestTrue(
        TEXT("Slot receives bAlwaysUpdateSourcePose"),
        Slot != nullptr && Slot->Node.bAlwaysUpdateSourcePose);
    TestEqual(
        TEXT("Layered Blend keeps one overlay pose"),
        LayeredBlend != nullptr ? LayeredBlend->Node.BlendPoses.Num() : 0,
        1);
    TestEqual(
        TEXT("Layered Blend receives one branch filter"),
        LayeredBlend != nullptr && !LayeredBlend->Node.LayerSetup.IsEmpty()
            ? LayeredBlend->Node.LayerSetup[0].BranchFilters.Num()
            : 0,
        1);
    TestEqual(
        TEXT("Layered Blend receives BranchFilter bone"),
        LayeredBlend != nullptr
            && !LayeredBlend->Node.LayerSetup.IsEmpty()
            && !LayeredBlend->Node.LayerSetup[0].BranchFilters.IsEmpty()
            ? LayeredBlend->Node.LayerSetup[0].BranchFilters[0].BoneName
            : NAME_None,
        OrientationTestBone);
    TestTrue(
        TEXT("Layered Blend enables mesh-space rotation"),
        LayeredBlend != nullptr && LayeredBlend->Node.bMeshSpaceRotationBlend);
    TestEqual(
        TEXT("Layered Blend receives curve strategy"),
        LayeredBlend != nullptr
            ? LayeredBlend->Node.CurveBlendOption.GetValue()
            : ECurveBlendOption::Override,
        ECurveBlendOption::UseBasePose);
    UEdGraphPin* LayeredBasePose = LayeredBlend != nullptr
        ? LayeredBlend->FindPin(TEXT("BasePose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* LayeredBlendPose = LayeredBlend != nullptr
        ? LayeredBlend->FindPin(TEXT("BlendPoses_0"), EGPD_Input)
        : nullptr;
    UEdGraphPin* LayeredBlendWeight = LayeredBlend != nullptr
        ? LayeredBlend->FindPin(TEXT("BlendWeights_0"), EGPD_Input)
        : nullptr;
    TestNotNull(TEXT("Layered Blend exposes BasePose Pin"), LayeredBasePose);
    TestEqual(
        TEXT("Layered Blend BlendPose is connected"),
        LayeredBlendPose != nullptr ? LayeredBlendPose->LinkedTo.Num() : 0,
        1);
    TestNotNull(TEXT("Layered Blend exposes BlendWeight Pin"), LayeredBlendWeight);
    TestEqual(
        TEXT("No implicit duplicate LocalToComponentSpace node is generated"),
        CountNodes<UAnimGraphNode_LocalToComponentSpace>(MoveGraph),
        1);
    TestEqual(
        TEXT("No implicit duplicate ComponentToLocalSpace node is generated"),
        CountNodes<UAnimGraphNode_ComponentToLocalSpace>(MoveGraph),
        1);
    TestEqual(
        TEXT("OrientationWarping uses Graph mode"),
        OrientationWarping != nullptr ? OrientationWarping->Node.Mode : EWarpingEvaluationMode::Graph,
        EWarpingEvaluationMode::Graph);
    TestEqual(
        TEXT("OrientationWarping receives SpineBones"),
        OrientationWarping != nullptr && !OrientationWarping->Node.SpineBones.IsEmpty()
            ? OrientationWarping->Node.SpineBones[0].BoneName
            : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("OrientationWarping receives IKFootRootBone"),
        OrientationWarping != nullptr ? OrientationWarping->Node.IKFootRootBone.BoneName : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("OrientationWarping receives IKFootBones"),
        OrientationWarping != nullptr && !OrientationWarping->Node.IKFootBones.IsEmpty()
            ? OrientationWarping->Node.IKFootBones[0].BoneName
            : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("OrientationWarping receives rotation axis"),
        OrientationWarping != nullptr ? OrientationWarping->Node.RotationAxis.GetValue() : EAxis::None,
        EAxis::Z);
    TestEqual(
        TEXT("OrientationWarping receives distribution alpha"),
        OrientationWarping != nullptr
            ? OrientationWarping->Node.DistributedBoneOrientationAlpha
            : 0.0f,
        1.0f);
    TestEqual(
        TEXT("OrientationWarping receives interpolation speed"),
        OrientationWarping != nullptr ? OrientationWarping->Node.RotationInterpSpeed : 0.0f,
        8.0f);
    TestEqual(
        TEXT("OrientationWarping receives minimum root motion speed"),
        OrientationWarping != nullptr
            ? OrientationWarping->Node.MinRootMotionSpeedThreshold
            : 0.0f,
        12.0f);
    TestEqual(
        TEXT("OrientationWarping receives locomotion delta threshold"),
        OrientationWarping != nullptr
            ? OrientationWarping->Node.LocomotionAngleDeltaThreshold
            : 0.0f,
        75.0f);
    TestEqual(
        TEXT("OrientationWarping receives graph warping alpha"),
        OrientationWarping != nullptr ? OrientationWarping->Node.WarpingAlpha : 0.0f,
        0.8f);
    TestEqual(
        TEXT("OrientationWarping receives graph offset alpha"),
        OrientationWarping != nullptr ? OrientationWarping->Node.OffsetAlpha : 0.0f,
        0.25f);
    TestEqual(
        TEXT("OrientationWarping receives graph max offset angle"),
        OrientationWarping != nullptr ? OrientationWarping->Node.MaxOffsetAngle : 0.0f,
        55.0f);
    UEdGraphPin* OrientationLocomotionAngle = OrientationWarping != nullptr
        ? OrientationWarping->FindPin(TEXT("LocomotionAngle"), EGPD_Input)
        : nullptr;
    TestNotNull(
        TEXT("Graph OrientationWarping exposes LocomotionAngle Pin"),
        OrientationLocomotionAngle);
    TestEqual(
        TEXT("Graph OrientationWarping LocomotionAngle is connected"),
        OrientationLocomotionAngle != nullptr
            ? OrientationLocomotionAngle->LinkedTo.Num()
            : 0,
        1);
    const UK2Node_VariableGet* LocomotionGetter =
        OrientationLocomotionAngle != nullptr && !OrientationLocomotionAngle->LinkedTo.IsEmpty()
            ? Cast<UK2Node_VariableGet>(
                OrientationLocomotionAngle->LinkedTo[0]->GetOwningNode())
            : nullptr;
    TestEqual(
        TEXT("Graph OrientationWarping reads the declared locomotion angle property"),
        LocomotionGetter != nullptr ? LocomotionGetter->GetVarName() : NAME_None,
        FName(TEXT("GraphLocomotionAngle")));
    TestEqual(
        TEXT("FootPlacement defaults PlantSpeedMode to Graph"),
        FootPlacement != nullptr ? FootPlacement->Node.PlantSpeedMode : EWarpingEvaluationMode::Manual,
        EWarpingEvaluationMode::Graph);
    TestEqual(
        TEXT("FootPlacement applies PlantLockType"),
        FootPlacement != nullptr
            ? FootPlacement->Node.PlantSettings.LockType
            : EFootPlacementLockType::PivotAroundBall,
        EFootPlacementLockType::PivotAroundAnkle);
    TestEqual(
        TEXT("FootPlacement uses Float Alpha"),
        FootPlacement != nullptr ? FootPlacement->Node.AlphaInputType : EAnimAlphaInputType::Bool,
        EAnimAlphaInputType::Float);
    TestEqual(
        TEXT("FootPlacement receives IKFootRootBone"),
        FootPlacement != nullptr ? FootPlacement->Node.IKFootRootBone.BoneName : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("FootPlacement receives PelvisBone"),
        FootPlacement != nullptr ? FootPlacement->Node.PelvisBone.BoneName : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("FootPlacement parses two trimmed legs"),
        FootPlacement != nullptr ? FootPlacement->Node.LegDefinitions.Num() : 0,
        2);
    TestEqual(
        TEXT("FootPlacement parses NumBonesInLimb"),
        FootPlacement != nullptr && !FootPlacement->Node.LegDefinitions.IsEmpty()
            ? FootPlacement->Node.LegDefinitions[0].NumBonesInLimb
            : 0,
        1);
    TestEqual(
        TEXT("FootPlacement applies pelvis max offset"),
        FootPlacement != nullptr ? FootPlacement->Node.PelvisSettings.MaxOffset : 0.0f,
        37.0f);
    TestEqual(
        TEXT("FootPlacement applies pelvis horizontal rebalancing"),
        FootPlacement != nullptr
            ? FootPlacement->Node.PelvisSettings.HorizontalRebalancingWeight
            : 0.0f,
        0.4f);
    TestEqual(
        TEXT("FootPlacement applies plant speed threshold"),
        FootPlacement != nullptr ? FootPlacement->Node.PlantSettings.SpeedThreshold : 0.0f,
        45.0f);
    TestEqual(
        TEXT("FootPlacement applies plant ground distance"),
        FootPlacement != nullptr ? FootPlacement->Node.PlantSettings.DistanceToGround : 0.0f,
        8.0f);
    TestEqual(
        TEXT("FootPlacement applies trace start"),
        FootPlacement != nullptr ? FootPlacement->Node.TraceSettings.StartOffset : 0.0f,
        -55.0f);
    TestEqual(
        TEXT("FootPlacement applies trace end"),
        FootPlacement != nullptr ? FootPlacement->Node.TraceSettings.EndOffset : 0.0f,
        90.0f);
    TestEqual(
        TEXT("FootPlacement applies trace radius"),
        FootPlacement != nullptr ? FootPlacement->Node.TraceSettings.SweepRadius : 0.0f,
        6.0f);
    TestEqual(
        TEXT("FootPlacement applies trace penetration"),
        FootPlacement != nullptr ? FootPlacement->Node.TraceSettings.MaxGroundPenetration : 0.0f,
        7.0f);
    TestTrue(
        TEXT("FootPlacement keeps tracing enabled"),
        FootPlacement != nullptr && FootPlacement->Node.TraceSettings.bEnabled);
    TestEqual(
        TEXT("LegIK uses Float Alpha"),
        LegIK != nullptr ? LegIK->Node.AlphaInputType : EAnimAlphaInputType::Bool,
        EAnimAlphaInputType::Float);
    TestEqual(
        TEXT("LegIK parses two trimmed legs"),
        LegIK != nullptr ? LegIK->Node.LegsDefinition.Num() : 0,
        2);
    TestEqual(
        TEXT("LegIK applies reach precision"),
        LegIK != nullptr ? LegIK->Node.ReachPrecision : 0.0f,
        0.05f);
    TestEqual(
        TEXT("LegIK writes byte MaxIterations into int32"),
        LegIK != nullptr ? LegIK->Node.MaxIterations : 0,
        7);
    TestEqual(
        TEXT("TwoBoneIK receives IKBone"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.IKBone.BoneName : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("TwoBoneIK uses Effector BoneSpace"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.EffectorLocationSpace.GetValue() : BCS_MAX,
        BCS_BoneSpace);
    TestTrue(
        TEXT("TwoBoneIK uses an Effector Socket target"),
        TwoBoneIK != nullptr && TwoBoneIK->Node.EffectorTarget.bUseSocket);
    TestEqual(
        TEXT("TwoBoneIK receives Effector Socket name"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.EffectorTarget.SocketReference.SocketName : NAME_None,
        FName(TEXT("FactoryTestEffectorSocket")));
    TestEqual(
        TEXT("TwoBoneIK receives Effector location"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.EffectorLocation : FVector::ZeroVector,
        FVector(11.0, 12.0, 13.0));
    TestEqual(
        TEXT("TwoBoneIK uses JointTarget BoneSpace"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.JointTargetLocationSpace.GetValue() : BCS_MAX,
        BCS_BoneSpace);
    TestFalse(
        TEXT("TwoBoneIK uses a JointTarget bone"),
        TwoBoneIK != nullptr && TwoBoneIK->Node.JointTarget.bUseSocket);
    TestEqual(
        TEXT("TwoBoneIK receives JointTarget bone name"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.JointTarget.BoneReference.BoneName : NAME_None,
        OrientationTestBone);
    TestEqual(
        TEXT("TwoBoneIK receives JointTarget location"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.JointTargetLocation : FVector::ZeroVector,
        FVector(21.0, 22.0, 23.0));
    TestTrue(
        TEXT("TwoBoneIK takes Effector rotation"),
        TwoBoneIK != nullptr && TwoBoneIK->Node.bTakeRotationFromEffectorSpace);
    TestTrue(
        TEXT("TwoBoneIK allows stretching"),
        TwoBoneIK != nullptr && TwoBoneIK->Node.bAllowStretching);
    TestEqual(
        TEXT("TwoBoneIK receives start stretch ratio"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.StartStretchRatio : 0.0,
        0.8);
    TestEqual(
        TEXT("TwoBoneIK receives max stretch scale"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.MaxStretchScale : 0.0,
        1.4);
    TestEqual(
        TEXT("TwoBoneIK uses Curve Alpha"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.AlphaInputType : EAnimAlphaInputType::Float,
        EAnimAlphaInputType::Curve);
    TestEqual(
        TEXT("TwoBoneIK receives Alpha curve name"),
        TwoBoneIK != nullptr ? TwoBoneIK->Node.AlphaCurveName : NAME_None,
        FName(TEXT("FactoryTestIKAlpha")));

    UEdGraphPin* LocalPosePin = LocalToComponent != nullptr
        ? LocalToComponent->FindPin(TEXT("LocalPose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* OrientationComponentPin = OrientationWarping != nullptr
        ? OrientationWarping->FindPin(TEXT("ComponentPose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* FootPlacementComponentPin = FootPlacement != nullptr
        ? FootPlacement->FindPin(TEXT("ComponentPose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* TwoBoneIKComponentPin = TwoBoneIK != nullptr
        ? TwoBoneIK->FindPin(TEXT("ComponentPose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* LegIKComponentPin = LegIK != nullptr
        ? LegIK->FindPin(TEXT("ComponentPose"), EGPD_Input)
        : nullptr;
    UEdGraphPin* ComponentToLocalInput = ComponentToLocal != nullptr
        ? ComponentToLocal->FindPin(TEXT("ComponentPose"), EGPD_Input)
        : nullptr;
    TestEqual(
        TEXT("LocalToComponentSpace receives the local Pose source"),
        LocalPosePin != nullptr ? LocalPosePin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("OrientationWarping receives explicit component Pose"),
        OrientationComponentPin != nullptr ? OrientationComponentPin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("TwoBoneIK receives OrientationWarping component Pose"),
        TwoBoneIKComponentPin != nullptr ? TwoBoneIKComponentPin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("FootPlacement receives TwoBoneIK component Pose"),
        FootPlacementComponentPin != nullptr ? FootPlacementComponentPin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("LegIK receives FootPlacement component Pose"),
        LegIKComponentPin != nullptr ? LegIKComponentPin->LinkedTo.Num() : 0,
        1);
    TestEqual(
        TEXT("ComponentToLocalSpace receives warped component Pose"),
        ComponentToLocalInput != nullptr ? ComponentToLocalInput->LinkedTo.Num() : 0,
        1);

    UAnimStateTransitionNode* TransitionNode = FindFirstNode<UAnimStateTransitionNode>(StateMachineGraph);
    TestNotNull(TEXT("Transition node exists"), TransitionNode);
    TestNotNull(
        TEXT("Transition owns native rule graph"),
        TransitionNode != nullptr ? Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph) : nullptr);
    TestNotNull(
        TEXT("Transition rule graph keeps native bool Result"),
        TransitionNode != nullptr && Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph) != nullptr
            ? Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph)->GetResultNode()
            : nullptr);
    TestTrue(
        TEXT("Linear BlendMode is applied"),
        TransitionNode != nullptr && TransitionNode->BlendMode == EAlphaBlendOption::Linear);

    if (StateMachineGraph != nullptr)
    {
        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* NativeTransition = Cast<UAnimStateTransitionNode>(Node);
            if (NativeTransition == nullptr) continue;
            UAnimationTransitionGraph* RuleGraph = Cast<UAnimationTransitionGraph>(NativeTransition->BoundGraph);
            UAnimGraphNode_TransitionResult* ResultNode = RuleGraph != nullptr
                ? RuleGraph->GetResultNode()
                : nullptr;
            UEdGraphPin* ResultPin = ResultNode != nullptr
                ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
                : nullptr;
            TestEqual(
                TEXT("Each Transition Graph directly evaluates its Lua rule"),
                CountFunctionCalls(
                    RuleGraph,
                    GET_FUNCTION_NAME_CHECKED(
                        USekiroLuaTransitionRuntimeLibrary,
                        EvaluateLuaTransitionRule)),
                1);
            TestTrue(
                TEXT("Each native Gate remains combined with the Lua rule"),
                CountFunctionCalls(
                    RuleGraph,
                    GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanAND)) >= 1);
            TestEqual(
                TEXT("Each Transition Result is connected to its final rule expression"),
                ResultPin != nullptr ? ResultPin->LinkedTo.Num() : 0,
                1);
        }
    }

    TArray<FSekiroAnimIRDiagnostic> SecondDiagnostics;
    UAnimBlueprint* SecondBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        SecondDiagnostics);
    UAnimationGraph* SecondMainGraph = FindMainGraph(SecondBlueprint);
    UAnimGraphNode_StateMachine* SecondStateMachine = FindFirstNode<UAnimGraphNode_StateMachine>(SecondMainGraph);
    TestNotNull(TEXT("Same IR can be rebuilt independently"), SecondBlueprint);
    TestEqual(
        TEXT("Main GraphGuid is deterministic"),
        MainGraph != nullptr ? MainGraph->GraphGuid : FGuid(),
        SecondMainGraph != nullptr ? SecondMainGraph->GraphGuid : FGuid());
    TestEqual(
        TEXT("NodeGuid is deterministic"),
        StateMachineNode != nullptr ? StateMachineNode->NodeGuid : FGuid(),
        SecondStateMachine != nullptr ? SecondStateMachine->NodeGuid : FGuid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryNativeBoolTransitionRuleTest,
    "Sekiro.AnimGraphIR.Factory.NativeBoolTransitionRule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证纯 BoolProperty AST 直接生成原生 Property Getter 与 Bool 比较，不创建 EvaluateLuaTransitionRule 调用。
 * 测试创建并编译 transient AnimBlueprint，不写磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryNativeBoolTransitionRuleTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    UAnimSequence* Sequence = CreateTestSequence(LoadTestSkeleton());
    TestNotNull(TEXT("Transient test Sequence is created"), Sequence);
    if (Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRVariable& NativeRuleVariable = BlueprintIR.Variables.AddDefaulted_GetRef();
    NativeRuleVariable.Name = TEXT("bNativeCanEnter");
    NativeRuleVariable.DataType = TEXT("Bool");
    NativeRuleVariable.DefaultValue.Type = ESekiroAnimIRValueType::Bool;
    NativeRuleVariable.DefaultValue.BoolValue = false;
    NativeRuleVariable.bTransient = true;
    NativeRuleVariable.SourceLocation = BlueprintIR.SourceLocation;

    FSekiroAnimIRGraph* StateMachineIR = FindGraph(BlueprintIR, TEXT("Graph.StateMachine"));
    TestNotNull(TEXT("Factory IR StateMachine exists"), StateMachineIR);
    if (StateMachineIR == nullptr || StateMachineIR->StateMachine.Transitions.IsEmpty()) return true;
    for (FSekiroAnimIRTransition& Transition : StateMachineIR->StateMachine.Transitions)
    {
        Transition.RuleFunctionName = NAME_None;
    }
    FSekiroAnimIRTransition& NativeTransition = StateMachineIR->StateMachine.Transitions[0];
    NativeTransition.Gate.Nodes.Reset();
    FSekiroAnimIRTransitionGateNode& BoolProperty = NativeTransition.Gate.Nodes.AddDefaulted_GetRef();
    BoolProperty.Type = TEXT("BoolProperty");
    BoolProperty.Name = NativeRuleVariable.Name;
    BoolProperty.bExpectedBool = false;
    NativeTransition.Gate.RootIndex = 0;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
    {
        AddInfo(FString::Printf(
            TEXT("Factory diagnostic %s: %s"),
            *Diagnostic.Code.ToString(),
            *Diagnostic.Message));
    }
    TestNotNull(TEXT("Factory creates native Bool Rule AnimBlueprint"), AnimBlueprint);
    TestEqual(TEXT("Native Bool Rule build has no diagnostics"), Diagnostics.Num(), 0);
    if (AnimBlueprint == nullptr) return true;
    TestTrue(TEXT("Native Bool Rule Blueprint compiles without error"), AnimBlueprint->Status != BS_Error);
    TestTrue(
        TEXT("All-native Transition Blueprint enables threaded animation update"),
        AnimBlueprint->bUseMultiThreadedAnimationUpdate);
    const UAnimInstance* GeneratedDefaultInstance = AnimBlueprint->GeneratedClass != nullptr
        ? Cast<UAnimInstance>(AnimBlueprint->GeneratedClass->GetDefaultObject())
        : nullptr;
    TestTrue(
        TEXT("All-native generated AnimInstance enables threaded animation update"),
        GeneratedDefaultInstance != nullptr
            && GeneratedDefaultInstance->bUseMultiThreadedAnimationUpdate);

    UAnimGraphNode_StateMachine* StateMachineNode =
        FindFirstNode<UAnimGraphNode_StateMachine>(FindMainGraph(AnimBlueprint));
    UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode != nullptr
        ? StateMachineNode->EditorStateMachineGraph
        : nullptr;
    UAnimationTransitionGraph* NativeRuleGraph = nullptr;
    if (StateMachineGraph != nullptr)
    {
        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
            if (TransitionNode == nullptr || TransitionNode->BoundGraph == nullptr) continue;
            if (TransitionNode->BoundGraph->GetName() != NativeTransition.Key) continue;
            NativeRuleGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
            break;
        }
    }
    TestNotNull(TEXT("Pure native Transition owns a Rule Graph"), NativeRuleGraph);
    TestEqual(
        TEXT("Pure native Rule has no Lua runtime call"),
        CountFunctionCalls(
            NativeRuleGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateLuaTransitionRule)),
        0);
    TestEqual(
        TEXT("Pure native Rule contains one Bool property Getter"),
        CountNodes<UK2Node_VariableGet>(NativeRuleGraph),
        1);
    TestEqual(
        TEXT("Pure native Rule contains one explicit Bool comparison"),
        CountFunctionCalls(
            NativeRuleGraph,
            GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_BoolBool)),
        1);
    const FName RecordBoolFunctionName = GET_FUNCTION_NAME_CHECKED(
        USekiroLuaTransitionRuntimeLibrary,
        RecordBoolTransitionDebugValue);
    const FName RecordExpressionFunctionName = GET_FUNCTION_NAME_CHECKED(
        USekiroLuaTransitionRuntimeLibrary,
        RecordTransitionExpressionDebugValue);
    UFunction* RecordBoolFunction =
        USekiroLuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(RecordBoolFunctionName);
    UFunction* RecordExpressionFunction =
        USekiroLuaTransitionRuntimeLibrary::StaticClass()->FindFunctionByName(RecordExpressionFunctionName);
    TestTrue(
        TEXT("Bool Transition trace is marked BlueprintThreadSafe"),
        RecordBoolFunction != nullptr
            && RecordBoolFunction->HasMetaData(TEXT("BlueprintThreadSafe")));
    TestTrue(
        TEXT("Expression Transition trace is marked BlueprintThreadSafe"),
        RecordExpressionFunction != nullptr
            && RecordExpressionFunction->HasMetaData(TEXT("BlueprintThreadSafe")));
    TestEqual(
        TEXT("Pure native Rule contains one Bool leaf trace"),
        CountFunctionCalls(NativeRuleGraph, RecordBoolFunctionName),
        1);
    TestEqual(
        TEXT("Pure native Rule contains one final trace"),
        CountFunctionCalls(NativeRuleGraph, RecordExpressionFunctionName),
        1);

    UAnimGraphNode_TransitionResult* ResultNode = NativeRuleGraph != nullptr
        ? NativeRuleGraph->GetResultNode()
        : nullptr;
    UEdGraphPin* ResultPin = ResultNode != nullptr
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    TestEqual(
        TEXT("Pure native Rule connects through final trace to Transition Result"),
        ResultPin != nullptr ? ResultPin->LinkedTo.Num() : 0,
        1);

    bool bExpectedFalseFound = false;
    UK2Node_CallFunction* BoolTraceNode = nullptr;
    UK2Node_CallFunction* FinalTraceNode = nullptr;
    if (NativeRuleGraph != nullptr)
    {
        for (UEdGraphNode* Node : NativeRuleGraph->Nodes)
        {
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            UFunction* Function = CallNode != nullptr ? CallNode->GetTargetFunction() : nullptr;
            if (Function != nullptr && Function->GetFName() == RecordBoolFunctionName)
            {
                BoolTraceNode = CallNode;
            }
            if (Function != nullptr && Function->GetFName() == RecordExpressionFunctionName)
            {
                UEdGraphPin* IsFinalPin = CallNode->FindPin(TEXT("bIsFinal"), EGPD_Input);
                if (IsFinalPin != nullptr && IsFinalPin->DefaultValue == TEXT("true"))
                {
                    FinalTraceNode = CallNode;
                }
            }
            if (Function == nullptr
                || Function->GetFName()
                    != GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_BoolBool))
            {
                continue;
            }
            UEdGraphPin* ExpectedPin = CallNode->FindPin(TEXT("B"), EGPD_Input);
            bExpectedFalseFound = ExpectedPin != nullptr && ExpectedPin->DefaultValue == TEXT("false");
        }
    }
    TestTrue(TEXT("Bool comparison preserves ExpectedBool=false"), bExpectedFalseFound);
    TestNotNull(TEXT("Bool leaf trace node exists"), BoolTraceNode);
    TestNotNull(TEXT("Final trace node exists"), FinalTraceNode);
    UEdGraphPin* LeafActualPin = BoolTraceNode != nullptr
        ? BoolTraceNode->FindPin(TEXT("ActualValue"), EGPD_Input)
        : nullptr;
    UEdGraphPin* LeafExpectedPin = BoolTraceNode != nullptr
        ? BoolTraceNode->FindPin(TEXT("ExpectedValue"), EGPD_Input)
        : nullptr;
    UEdGraphPin* LeafResultPin = BoolTraceNode != nullptr
        ? BoolTraceNode->FindPin(TEXT("Result"), EGPD_Input)
        : nullptr;
    UEdGraphPin* FinalInputPin = FinalTraceNode != nullptr
        ? FinalTraceNode->FindPin(TEXT("Result"), EGPD_Input)
        : nullptr;
    TestEqual(TEXT("Bool leaf trace ActualValue is connected once"),
        LeafActualPin != nullptr ? LeafActualPin->LinkedTo.Num() : 0, 1);
    TestEqual(TEXT("Bool leaf trace comparison Result is connected once"),
        LeafResultPin != nullptr ? LeafResultPin->LinkedTo.Num() : 0, 1);
    TestEqual(TEXT("Bool leaf trace preserves ExpectedValue=false"),
        LeafExpectedPin != nullptr ? LeafExpectedPin->DefaultValue : FString(), FString(TEXT("false")));
    TestEqual(TEXT("Final trace consumes the leaf pass-through once"),
        FinalInputPin != nullptr ? FinalInputPin->LinkedTo.Num() : 0, 1);
    TestEqual(
        TEXT("Final trace input comes from the Bool leaf trace"),
        FinalInputPin != nullptr && !FinalInputPin->LinkedTo.IsEmpty()
            ? FinalInputPin->LinkedTo[0]->GetOwningNode()
            : nullptr,
        static_cast<UEdGraphNode*>(BoolTraceNode));
    TestTrue(
        TEXT("Bool leaf ActualValue comes from the existing variable Getter"),
        LeafActualPin != nullptr
            && !LeafActualPin->LinkedTo.IsEmpty()
            && Cast<UK2Node_VariableGet>(LeafActualPin->LinkedTo[0]->GetOwningNode()) != nullptr);
    UK2Node_CallFunction* LeafCompareNode = LeafResultPin != nullptr
        && !LeafResultPin->LinkedTo.IsEmpty()
        ? Cast<UK2Node_CallFunction>(LeafResultPin->LinkedTo[0]->GetOwningNode())
        : nullptr;
    TestEqual(
        TEXT("Bool leaf Result comes from EqualEqual_BoolBool"),
        LeafCompareNode != nullptr && LeafCompareNode->GetTargetFunction() != nullptr
            ? LeafCompareNode->GetTargetFunction()->GetFName()
            : NAME_None,
        GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, EqualEqual_BoolBool));
    TestEqual(
        TEXT("Transition Result consumes the final trace output"),
        ResultPin != nullptr && !ResultPin->LinkedTo.IsEmpty()
            ? ResultPin->LinkedTo[0]->GetOwningNode()
            : nullptr,
        static_cast<UEdGraphNode*>(FinalTraceNode));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryBlueprintParentTest,
    "Sekiro.AnimGraphIR.Factory.BlueprintParent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证生成器能以已有 AnimBlueprint GeneratedClass 为父类创建子资产，并在子 IR 不重复声明变量时，
 * 让 Transition Rule 与 Graph Property Getter 解析父类公开属性。子 AnimGraph 保留 Lua 生成拓扑供编辑，
 * 运行时 AnimNodeData 遵循 UE Derived AnimBlueprint 语义继承根父图；子 EventGraph 仍可更新继承属性。
 * 测试仅创建 transient UObject，必须由 Automation Framework 在游戏线程执行，不写入磁盘。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryBlueprintParentTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    TestNotNull(TEXT("Blueprint parent test Skeleton loads"), Skeleton);
    TestNotNull(TEXT("Blueprint parent test Sequence is created"), Sequence);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR ParentIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRVariable& ParentTransitionVariable = ParentIR.Variables.AddDefaulted_GetRef();
    ParentTransitionVariable.Name = TEXT("bParentCanEnter");
    ParentTransitionVariable.DataType = TEXT("Bool");
    ParentTransitionVariable.DefaultValue.Type = ESekiroAnimIRValueType::Bool;
    ParentTransitionVariable.DefaultValue.BoolValue = false;
    ParentTransitionVariable.bTransient = true;
    ParentTransitionVariable.SourceLocation = ParentIR.SourceLocation;

    TArray<FSekiroAnimIRDiagnostic> ParentDiagnostics;
    UAnimBlueprint* ParentBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            ParentIR,
            ParentDiagnostics);
    TestNotNull(TEXT("Blueprint parent compiles"), ParentBlueprint);
    TestEqual(TEXT("Blueprint parent build has no diagnostics"), ParentDiagnostics.Num(), 0);
    if (ParentBlueprint == nullptr || ParentBlueprint->GeneratedClass == nullptr) return true;

    FSekiroAnimBlueprintIR ChildIR = MakeFactoryIR(Sequence);
    ChildIR.ParentAnimInstanceClass =
        FSoftClassPath(ParentBlueprint->GeneratedClass->GetPathName());
    ChildIR.Variables.Add(ParentTransitionVariable);

    FSekiroAnimBlueprintIR MismatchedChildIR = ChildIR;
    FSekiroAnimIRVariable* MismatchedVariable =
        MismatchedChildIR.Variables.FindByPredicate(
            [&ParentTransitionVariable](const FSekiroAnimIRVariable& Variable)
            {
                return Variable.Name == ParentTransitionVariable.Name;
            });
    if (MismatchedVariable != nullptr)
    {
        MismatchedVariable->DataType = TEXT("Float");
        MismatchedVariable->DefaultValue.Type = ESekiroAnimIRValueType::Float;
    }
    TArray<FSekiroAnimIRDiagnostic> MismatchDiagnostics;
    UAnimBlueprint* MismatchedChildBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            MismatchedChildIR,
            MismatchDiagnostics);
    TestNull(
        TEXT("Blueprint parent rejects an inherited variable type mismatch"),
        MismatchedChildBlueprint);
    TestTrue(
        TEXT("Inherited variable mismatch emits a stable diagnostic"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            MismatchDiagnostics,
            TEXT("Factory.InheritedVariableTypeMismatch")));

    const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
    const FName GraphTestBone = ReferenceSkeleton.GetNum() > 0
        ? ReferenceSkeleton.GetBoneName(0)
        : NAME_None;
    FSekiroAnimIRGraph* ChildMoveGraph = FindGraph(ChildIR, TEXT("Graph.Move"));
    TestTrue(
        TEXT("Child IR adds a Graph getter backed only by the parent class"),
        ChildMoveGraph != nullptr
            && AddOrientationWarpingChain(
                *ChildMoveGraph,
                GraphTestBone,
                ChildIR.SourceLocation));

    FSekiroAnimIRGraph* ChildStateMachineIR =
        FindGraph(ChildIR, TEXT("Graph.StateMachine"));
    TestNotNull(TEXT("Child StateMachine IR exists"), ChildStateMachineIR);
    if (ChildStateMachineIR == nullptr
        || ChildStateMachineIR->StateMachine.Transitions.IsEmpty())
    {
        return true;
    }
    FSekiroAnimIRTransition& ChildTransition =
        ChildStateMachineIR->StateMachine.Transitions[0];
    ChildTransition.Gate.Nodes.Reset();
    FSekiroAnimIRTransitionGateNode& ParentBoolGate =
        ChildTransition.Gate.Nodes.AddDefaulted_GetRef();
    ParentBoolGate.Type = TEXT("BoolProperty");
    ParentBoolGate.Name = ParentTransitionVariable.Name;
    ParentBoolGate.bExpectedBool = true;
    ChildTransition.Gate.RootIndex = 0;

    TArray<FSekiroAnimIRDiagnostic> ChildDiagnostics;
    UAnimBlueprint* ChildBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            ChildIR,
            ChildDiagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : ChildDiagnostics)
    {
        AddInfo(FString::Printf(
            TEXT("Blueprint parent diagnostic %s: %s"),
            *Diagnostic.Code.ToString(),
            *Diagnostic.Message));
    }
    TestNotNull(TEXT("Factory creates a Blueprint-parent AnimBlueprint"), ChildBlueprint);
    TestEqual(TEXT("Blueprint-parent build has no diagnostics"), ChildDiagnostics.Num(), 0);
    if (ChildBlueprint == nullptr || ChildBlueprint->GeneratedClass == nullptr) return true;

    TestEqual(
        TEXT("Child AnimBlueprint preserves the requested GeneratedClass parent"),
        ChildBlueprint->ParentClass.Get(),
        ParentBlueprint->GeneratedClass.Get());
    TestEqual(
        TEXT("Child AnimBlueprint preserves the parent Skeleton"),
        ChildBlueprint->TargetSkeleton.Get(),
        Skeleton);
    TestEqual(
        TEXT("Child IR does not duplicate parent member variables"),
        ChildBlueprint->NewVariables.Num(),
        0);
    TestTrue(
        TEXT("Child AnimBlueprint compiles without error"),
        ChildBlueprint->Status != BS_Error);

    FBoolProperty* InheritedBool = FindFProperty<FBoolProperty>(
        ChildBlueprint->GeneratedClass,
        ParentTransitionVariable.Name);
    FFloatProperty* InheritedFloat = FindFProperty<FFloatProperty>(
        ChildBlueprint->GeneratedClass,
        TEXT("GraphLocomotionAngle"));
    TestNotNull(TEXT("Child GeneratedClass reflects inherited Transition Bool"), InheritedBool);
    TestNotNull(TEXT("Child GeneratedClass reflects inherited Graph Float"), InheritedFloat);
    TestEqual(
        TEXT("Inherited Transition Bool remains owned by parent GeneratedClass"),
        InheritedBool != nullptr ? InheritedBool->GetOwnerClass() : nullptr,
        ParentBlueprint->GeneratedClass.Get());
    TestEqual(
        TEXT("Inherited Graph Float remains owned by parent GeneratedClass"),
        InheritedFloat != nullptr ? InheritedFloat->GetOwnerClass() : nullptr,
        ParentBlueprint->GeneratedClass.Get());

    USkeletalMeshComponent* TestMeshComponent =
        NewObject<USkeletalMeshComponent>(
            GetTransientPackage(),
            NAME_None,
            RF_Transient);
    UAnimInstance* ChildInstance = NewObject<UAnimInstance>(
        TestMeshComponent,
        ChildBlueprint->GeneratedClass,
        NAME_None,
        RF_Transient);
    TestNotNull(TEXT("Child GeneratedClass creates an AnimInstance"), ChildInstance);
    if (ChildInstance != nullptr && InheritedBool != nullptr && InheritedFloat != nullptr)
    {
        InheritedBool->SetPropertyValue_InContainer(ChildInstance, true);
        InheritedFloat->SetPropertyValue_InContainer(ChildInstance, 37.5f);
        TestTrue(
            TEXT("Runtime child instance can update inherited Transition Bool"),
            InheritedBool->GetPropertyValue_InContainer(ChildInstance));
        TestEqual(
            TEXT("Runtime child instance can update inherited Graph Float"),
            InheritedFloat->GetPropertyValue_InContainer(ChildInstance),
            37.5f);
    }

    UAnimationGraph* ChildMainGraph = FindMainGraph(ChildBlueprint);
    TestNotNull(TEXT("Child keeps its Lua-generated AnimGraph topology"), ChildMainGraph);
    UAnimGraphNode_StateMachine* ChildStateMachineNode =
        FindFirstNode<UAnimGraphNode_StateMachine>(ChildMainGraph);
    UAnimationStateMachineGraph* ChildStateMachineGraph =
        ChildStateMachineNode != nullptr
            ? ChildStateMachineNode->EditorStateMachineGraph
            : nullptr;
    UAnimStateNode* ChildMoveState = FindState(ChildStateMachineGraph, TEXT("Move"));
    UEdGraph* ChildMoveNativeGraph =
        ChildMoveState != nullptr ? ChildMoveState->BoundGraph : nullptr;
    bool bFoundInheritedGraphGetter = false;
    if (ChildMoveNativeGraph != nullptr)
    {
        for (UEdGraphNode* Node : ChildMoveNativeGraph->Nodes)
        {
            UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node);
            if (Getter != nullptr
                && Getter->VariableReference.GetMemberName() == TEXT("GraphLocomotionAngle"))
            {
                bFoundInheritedGraphGetter = true;
                break;
            }
        }
    }
    TestTrue(
        TEXT("Child Graph Property Getter targets the inherited Float"),
        bFoundInheritedGraphGetter);

    bool bFoundInheritedTransitionGetter = false;
    if (ChildStateMachineGraph != nullptr)
    {
        for (UEdGraphNode* Node : ChildStateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* TransitionNode =
                Cast<UAnimStateTransitionNode>(Node);
            UEdGraph* RuleGraph = TransitionNode != nullptr
                ? TransitionNode->BoundGraph
                : nullptr;
            if (RuleGraph == nullptr || RuleGraph->GetName() != ChildTransition.Key) continue;
            for (UEdGraphNode* RuleNode : RuleGraph->Nodes)
            {
                UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(RuleNode);
                if (Getter != nullptr
                    && Getter->VariableReference.GetMemberName()
                        == ParentTransitionVariable.Name)
                {
                    bFoundInheritedTransitionGetter = true;
                    break;
                }
            }
            break;
        }
    }
    TestTrue(
        TEXT("Child Transition Rule Getter targets the inherited Bool"),
        bFoundInheritedTransitionGetter);

    UEdGraph* ChildEventGraph = FBlueprintEditorUtils::FindEventGraph(ChildBlueprint);
    TestEqual(
        TEXT("Child EventGraph keeps one Lua animation update bridge"),
        CountFunctionCalls(
            ChildEventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateBlueprintUpdateAnimation)),
        1);

    const FString ParentLuaModule(TEXT("SekiroAnimGraphIRTests.InheritedParent"));
    TestTrue(
        TEXT("Parent AnimBlueprint accepts generic Lua source metadata"),
        USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
            ParentBlueprint,
            ParentLuaModule));
    TestNull(
        TEXT("Standard child initially owns no Lua extension"),
        USekiroLuaAnimBlueprintExtension::Find(ChildBlueprint));

    bool bInheritedLuaSource = false;
    const USekiroLuaAnimBlueprintExtension* EffectiveChildExtension =
        USekiroLuaAnimBlueprintExtension::FindEffective(
            ChildBlueprint,
            &bInheritedLuaSource);
    TestTrue(TEXT("Child resolves Lua source through UE class ancestry"), bInheritedLuaSource);
    TestNotNull(TEXT("Child resolves the nearest parent Lua extension"), EffectiveChildExtension);
    if (EffectiveChildExtension != nullptr)
    {
        TestEqual(
            TEXT("Child inherits the parent Lua module without path inference"),
            EffectiveChildExtension->LuaModuleName,
            ParentLuaModule);
    }

    TSharedRef<FUICommandList> ChildCommandList = MakeShared<FUICommandList>();
    const TSharedPtr<const FUICommandInfo> ChildCompileCommand =
        FGenericCommands::Get().Delete;
    int32 ChildNativeCompileCallCount = 0;
    ChildCommandList->MapAction(
        ChildCompileCommand,
        FUIAction(FExecuteAction::CreateLambda([&ChildNativeCompileCallCount]()
        {
            ++ChildNativeCompileCallCount;
        })));
    TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> ChildBinding =
        FSekiroLuaAnimBlueprintEditorBinding::CreateForTest(
            ChildCommandList,
            ChildBlueprint);
    ChildBinding->GetToolbarExtender();

    FToolBarBuilder ChildToolbarBuilder(
        ChildCommandList,
        FMultiBoxCustomization::None);
    ChildBinding->FillToolbar(ChildToolbarBuilder);

    const TSharedRef<FMultiBox> ChildToolbar = ChildToolbarBuilder.GetMultiBox();
    const TArray<TSharedRef<const FMultiBlock>>& ChildToolbarBlocks =
        ChildToolbar->GetBlocks();
    TestEqual(
        TEXT("Inherited child toolbar contains all Lua controls"),
        ChildToolbarBlocks.Num(),
        6);
    TestNotNull(
        TEXT("Inherited source remains resolvable after toolbar construction"),
        USekiroLuaAnimBlueprintExtension::FindEffective(ChildBlueprint));
    TestTrue(
        TEXT("Inherited child enables Check Lua and Lua → AnimBlueprint"),
        ChildBinding->CanExecuteLuaActionForTest());
    TestTrue(
        TEXT("Inherited child native Compile command remains mapped"),
        ChildCommandList->ExecuteAction(ChildCompileCommand.ToSharedRef()));
    TestEqual(
        TEXT("Inherited child native Compile command remains untouched"),
        ChildNativeCompileCallCount,
        1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryTransitionRuleDebugSamplingTest,
    "Sekiro.AnimGraphIR.Factory.TransitionRuleDebugSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Curve/Time/Any Gate 采样连接真实参数与组合结果，并确认 Lua-only Rule 不生成原生 AST 采样节点。
 * 测试创建并编译 transient AnimBlueprint，不写磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryTransitionRuleDebugSamplingTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    UAnimSequence* Sequence = CreateTestSequence(LoadTestSkeleton());
    TestNotNull(TEXT("Transient debug sampling Sequence is created"), Sequence);
    if (Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRGraph* StateMachineIR = FindGraph(BlueprintIR, TEXT("Graph.StateMachine"));
    TestNotNull(TEXT("Debug sampling StateMachine IR exists"), StateMachineIR);
    if (StateMachineIR == nullptr || StateMachineIR->StateMachine.Transitions.Num() < 2) return true;

    const FString SampledRuleKey = StateMachineIR->StateMachine.Transitions[0].Key;
    FSekiroAnimIRTransition& LuaOnlyTransition = StateMachineIR->StateMachine.Transitions[1];
    const FString LuaOnlyRuleKey = LuaOnlyTransition.Key;
    LuaOnlyTransition.Gate.RootIndex = INDEX_NONE;
    LuaOnlyTransition.Gate.Nodes.Reset();

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
    {
        AddInfo(FString::Printf(
            TEXT("Factory diagnostic %s: %s"),
            *Diagnostic.Code.ToString(),
            *Diagnostic.Message));
    }
    TestNotNull(TEXT("Factory creates sampled Transition AnimBlueprint"), AnimBlueprint);
    TestEqual(TEXT("Sampled Transition build has no diagnostics"), Diagnostics.Num(), 0);
    if (AnimBlueprint == nullptr) return true;

    UAnimGraphNode_StateMachine* StateMachineNode =
        FindFirstNode<UAnimGraphNode_StateMachine>(FindMainGraph(AnimBlueprint));
    UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode != nullptr
        ? StateMachineNode->EditorStateMachineGraph
        : nullptr;
    UAnimationTransitionGraph* SampledRuleGraph = nullptr;
    UAnimationTransitionGraph* LuaOnlyRuleGraph = nullptr;
    if (StateMachineGraph != nullptr)
    {
        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
            UAnimationTransitionGraph* RuleGraph = TransitionNode != nullptr
                ? Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph)
                : nullptr;
            if (RuleGraph == nullptr) continue;
            if (RuleGraph->GetName() == SampledRuleKey) SampledRuleGraph = RuleGraph;
            if (RuleGraph->GetName() == LuaOnlyRuleKey) LuaOnlyRuleGraph = RuleGraph;
        }
    }
    TestNotNull(TEXT("Curve/Time sampled Rule Graph exists"), SampledRuleGraph);
    TestNotNull(TEXT("Lua-only Rule Graph exists"), LuaOnlyRuleGraph);

    const FName RecordBoolFunctionName = GET_FUNCTION_NAME_CHECKED(
        USekiroLuaTransitionRuntimeLibrary,
        RecordBoolTransitionDebugValue);
    const FName RecordFloatFunctionName = GET_FUNCTION_NAME_CHECKED(
        USekiroLuaTransitionRuntimeLibrary,
        RecordFloatTransitionDebugValue);
    const FName RecordExpressionFunctionName = GET_FUNCTION_NAME_CHECKED(
        USekiroLuaTransitionRuntimeLibrary,
        RecordTransitionExpressionDebugValue);
    TestEqual(TEXT("Curve and Time each create one Float trace"),
        CountFunctionCalls(SampledRuleGraph, RecordFloatFunctionName), 2);
    TestEqual(TEXT("Any and final result each create one expression trace"),
        CountFunctionCalls(SampledRuleGraph, RecordExpressionFunctionName), 2);
    TestEqual(TEXT("Curve/Time Rule has no Bool property trace"),
        CountFunctionCalls(SampledRuleGraph, RecordBoolFunctionName), 0);

    bool bCurveParametersConnected = false;
    bool bTimeParametersConnected = false;
    bool bAnyExpressionConnected = false;
    bool bFinalExpressionConnected = false;
    UK2Node_CallFunction* FinalTraceNode = nullptr;
    if (SampledRuleGraph != nullptr)
    {
        for (UEdGraphNode* Node : SampledRuleGraph->Nodes)
        {
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            UFunction* Function = CallNode != nullptr ? CallNode->GetTargetFunction() : nullptr;
            if (Function == nullptr) continue;
            if (Function->GetFName() == RecordFloatFunctionName)
            {
                UEdGraphPin* ParameterNamePin = CallNode->FindPin(TEXT("ParameterName"), EGPD_Input);
                UEdGraphPin* ActualValuePin = CallNode->FindPin(TEXT("ActualValue"), EGPD_Input);
                UEdGraphPin* ThresholdPin = CallNode->FindPin(TEXT("Threshold"), EGPD_Input);
                UEdGraphPin* SampleResultPin = CallNode->FindPin(TEXT("Result"), EGPD_Input);
                const bool bValueAndResultConnected = ActualValuePin != nullptr
                    && ActualValuePin->LinkedTo.Num() == 1
                    && SampleResultPin != nullptr
                    && SampleResultPin->LinkedTo.Num() == 1;
                UEdGraphNode* ActualValueNode = bValueAndResultConnected
                    ? ActualValuePin->LinkedTo[0]->GetOwningNode()
                    : nullptr;
                UK2Node_CallFunction* ComparisonNode = bValueAndResultConnected
                    ? Cast<UK2Node_CallFunction>(SampleResultPin->LinkedTo[0]->GetOwningNode())
                    : nullptr;
                const FName ComparisonFunctionName = ComparisonNode != nullptr
                    && ComparisonNode->GetTargetFunction() != nullptr
                    ? ComparisonNode->GetTargetFunction()->GetFName()
                    : NAME_None;
                if (ParameterNamePin != nullptr && ParameterNamePin->DefaultValue == TEXT("CanEnterStop"))
                {
                    UK2Node_CallFunction* CurveGetterNode = Cast<UK2Node_CallFunction>(ActualValueNode);
                    bCurveParametersConnected = bValueAndResultConnected
                        && ThresholdPin != nullptr
                        && ThresholdPin->DefaultValue == FString::SanitizeFloat(0.5f)
                        && CurveGetterNode != nullptr
                        && CurveGetterNode->GetTargetFunction() != nullptr
                        && CurveGetterNode->GetTargetFunction()->GetFName()
                            == FName(TEXT("GetCurveValue"))
                        && ComparisonFunctionName
                            == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, GreaterEqual_DoubleDouble);
                }
                if (ParameterNamePin != nullptr
                    && ParameterNamePin->DefaultValue == TEXT("RelevantTimeRemaining"))
                {
                    bTimeParametersConnected = bValueAndResultConnected
                        && ThresholdPin != nullptr
                        && ThresholdPin->DefaultValue == FString::SanitizeFloat(0.12f)
                        && Cast<UK2Node_AnimGetter>(ActualValueNode) != nullptr
                        && ComparisonFunctionName
                            == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, LessEqual_DoubleDouble);
                }
            }
            if (Function->GetFName() == RecordExpressionFunctionName)
            {
                UEdGraphPin* LabelPin = CallNode->FindPin(TEXT("ExpressionLabel"), EGPD_Input);
                UEdGraphPin* IsFinalPin = CallNode->FindPin(TEXT("bIsFinal"), EGPD_Input);
                UEdGraphPin* SampleResultPin = CallNode->FindPin(TEXT("Result"), EGPD_Input);
                const bool bResultConnected = SampleResultPin != nullptr
                    && SampleResultPin->LinkedTo.Num() == 1;
                UK2Node_CallFunction* SourceExpressionNode = bResultConnected
                    ? Cast<UK2Node_CallFunction>(SampleResultPin->LinkedTo[0]->GetOwningNode())
                    : nullptr;
                const FName SourceExpressionFunctionName = SourceExpressionNode != nullptr
                    && SourceExpressionNode->GetTargetFunction() != nullptr
                    ? SourceExpressionNode->GetTargetFunction()->GetFName()
                    : NAME_None;
                if (LabelPin != nullptr && LabelPin->DefaultValue == TEXT("2:Any"))
                {
                    bAnyExpressionConnected = bResultConnected
                        && IsFinalPin != nullptr
                        && IsFinalPin->DefaultValue == TEXT("false")
                        && SourceExpressionFunctionName
                            == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanOR);
                }
                if (LabelPin != nullptr && LabelPin->DefaultValue == TEXT("RuleResult"))
                {
                    FinalTraceNode = CallNode;
                    bFinalExpressionConnected = bResultConnected
                        && IsFinalPin != nullptr
                        && IsFinalPin->DefaultValue == TEXT("true")
                        && SourceExpressionFunctionName
                            == GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BooleanAND);
                }
            }
        }
    }
    TestTrue(TEXT("Curve trace connects actual value, threshold and comparison"), bCurveParametersConnected);
    TestTrue(TEXT("Time trace connects actual value, threshold and comparison"), bTimeParametersConnected);
    TestTrue(TEXT("Any trace connects the combined expression result"), bAnyExpressionConnected);
    TestTrue(TEXT("Final trace connects the authoritative RuleResult"), bFinalExpressionConnected);
    UAnimGraphNode_TransitionResult* SampledResultNode = SampledRuleGraph != nullptr
        ? SampledRuleGraph->GetResultNode()
        : nullptr;
    UEdGraphPin* SampledResultPin = SampledResultNode != nullptr
        ? SampledResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    TestEqual(
        TEXT("Sampled Transition Result consumes the final trace output"),
        SampledResultPin != nullptr && !SampledResultPin->LinkedTo.IsEmpty()
            ? SampledResultPin->LinkedTo[0]->GetOwningNode()
            : nullptr,
        static_cast<UEdGraphNode*>(FinalTraceNode));

    TestEqual(TEXT("Lua-only Rule still has one Lua evaluation"),
        CountFunctionCalls(
            LuaOnlyRuleGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateLuaTransitionRule)),
        1);
    TestEqual(TEXT("Lua-only Rule adds no Bool leaf trace"),
        CountFunctionCalls(LuaOnlyRuleGraph, RecordBoolFunctionName), 0);
    TestEqual(TEXT("Lua-only Rule adds no Float leaf trace"),
        CountFunctionCalls(LuaOnlyRuleGraph, RecordFloatFunctionName), 0);
    TestEqual(TEXT("Lua-only Rule adds no native expression trace"),
        CountFunctionCalls(LuaOnlyRuleGraph, RecordExpressionFunctionName), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryInvalidFootPlacementLockTypeTest,
    "Sekiro.AnimGraphIR.Factory.InvalidFootPlacementLockType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 FootPlacement 的未知 PlantLockType 在创建 UObject 前产生稳定预检诊断。
 * 测试仅创建 transient 测试资产，不写入磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryInvalidFootPlacementLockTypeTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Sequence == nullptr || Skeleton == nullptr) return true;

    const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
    const FName TestBone = ReferenceSkeleton.GetNum() > 0
        ? ReferenceSkeleton.GetBoneName(0)
        : NAME_None;
    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRGraph* MoveGraph = FindGraph(BlueprintIR, TEXT("Graph.Move"));
    const bool bChainAdded = MoveGraph != nullptr
        && AddOrientationWarpingChain(*MoveGraph, TestBone, BlueprintIR.SourceLocation)
        && AddFootIKChain(*MoveGraph, TestBone, BlueprintIR.SourceLocation);
    TestTrue(TEXT("Invalid lock test creates the FootPlacement chain"), bChainAdded);
    if (!bChainAdded) return true;

    bool bFoundLockProperty = false;
    for (FSekiroAnimIRNode& Node : MoveGraph->Nodes)
    {
        if (Node.NodeType != SekiroAnimGraphIRNames::FootPlacementNode) continue;
        for (FSekiroAnimIRProperty& Property : Node.Properties)
        {
            if (Property.Name != TEXT("PlantLockType")) continue;
            Property.Value.NameValue = TEXT("UnsupportedLock");
            bFoundLockProperty = true;
            break;
        }
    }
    TestTrue(TEXT("Invalid lock test finds PlantLockType"), bFoundLockProperty);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* Blueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    TestNull(TEXT("Invalid PlantLockType prevents Blueprint creation"), Blueprint);
    TestTrue(
        TEXT("Invalid PlantLockType emits stable diagnostic"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            Diagnostics,
            TEXT("Factory.InvalidFootPlacementPlantLockType")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryNestedStateMachineTest,
    "Sekiro.AnimGraphIR.Factory.NestedStateMachine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StatePose Graph 内可继续创建 StateMachine Node，并递归生成其 State 与专属 StatePose。
 * 测试只创建 transient UObject，不写入磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryNestedStateMachineTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR BlueprintIR = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph* IdleGraph = FindGraph(BlueprintIR, TEXT("Graph.Idle"));
    if (IdleGraph == nullptr) return true;

    FSekiroAnimIRNode& NestedNode = IdleGraph->Nodes.AddDefaulted_GetRef();
    NestedNode.Id = TEXT("Node.Idle.NestedStateMachine");
    NestedNode.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
    NestedNode.DisplayName = TEXT("Nested");
    NestedNode.OwnedGraphId = TEXT("Graph.NestedStateMachine");
    NestedNode.SourceLocation = BlueprintIR.SourceLocation;
    FSekiroAnimIRPin& NestedPose = NestedNode.Pins.AddDefaulted_GetRef();
    NestedPose.Name = TEXT("Pose");
    NestedPose.Direction = ESekiroAnimIRPinDirection::Output;
    NestedPose.DataType = SekiroAnimGraphIRNames::PoseData;
    NestedPose.bAllowMultipleConnections = true;

    FSekiroAnimIRLink& NestedLink = IdleGraph->Links.AddDefaulted_GetRef();
    NestedLink.Id = TEXT("Link.Idle.NestedToResult");
    NestedLink.Source.NodeId = NestedNode.Id;
    NestedLink.Source.PinName = TEXT("Pose");
    NestedLink.Target.NodeId = IdleGraph->RootNodeId;
    NestedLink.Target.PinName = TEXT("Result");
    NestedLink.SourceLocation = BlueprintIR.SourceLocation;

    FSekiroAnimIRGraph& NestedMachineGraph = BlueprintIR.Layers[0].Graphs.AddDefaulted_GetRef();
    NestedMachineGraph.Id = TEXT("Graph.NestedStateMachine");
    NestedMachineGraph.Name = TEXT("NestedMachine");
    NestedMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;
    NestedMachineGraph.StateMachine.EntryStateId = TEXT("State.NestedIdle");
    NestedMachineGraph.SourceLocation = BlueprintIR.SourceLocation;
    FSekiroAnimIRState& NestedState = NestedMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    NestedState.Id = TEXT("State.NestedIdle");
    NestedState.Name = TEXT("NestedIdle");
    NestedState.GraphId = TEXT("Graph.NestedIdle");
    NestedState.SourceLocation = BlueprintIR.SourceLocation;

    FSekiroAnimIRGraph& NestedStateGraph = AddStatePoseGraph(
        BlueprintIR,
        TEXT("Graph.NestedIdle"),
        TEXT("NestedIdle"),
        TEXT("Node.NestedIdle.Output"));
    AddSequencePose(
        NestedStateGraph,
        TEXT("Node.NestedIdle"),
        FSoftObjectPath(Sequence),
        false,
        BlueprintIR.SourceLocation);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    TestNotNull(TEXT("Nested StateMachine IR builds"), AnimBlueprint);
    TestEqual(TEXT("Nested build has no diagnostics"), Diagnostics.Num(), 0);
    if (AnimBlueprint == nullptr) return true;

    UAnimGraphNode_StateMachine* OuterMachine = FindFirstNode<UAnimGraphNode_StateMachine>(FindMainGraph(AnimBlueprint));
    UAnimStateNode* IdleState = OuterMachine != nullptr
        ? FindState(OuterMachine->EditorStateMachineGraph, TEXT("Idle"))
        : nullptr;
    UAnimationStateGraph* NativeIdleGraph = IdleState != nullptr
        ? Cast<UAnimationStateGraph>(IdleState->BoundGraph)
        : nullptr;
    UAnimGraphNode_StateMachine* NativeNestedMachine =
        FindFirstNode<UAnimGraphNode_StateMachine>(NativeIdleGraph);
    TestNotNull(TEXT("StatePose contains native nested StateMachine"), NativeNestedMachine);
    TestNotNull(
        TEXT("Nested StateMachine owns native graph"),
        NativeNestedMachine != nullptr ? NativeNestedMachine->EditorStateMachineGraph.Get() : nullptr);
    TestEqual(
        TEXT("Nested graph contains one state"),
        NativeNestedMachine != nullptr
            ? CountNodes<UAnimStateNode>(NativeNestedMachine->EditorStateMachineGraph)
            : 0,
        1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryUnsupportedBlendModeTest,
    "Sekiro.AnimGraphIR.Factory.UnsupportedBlendMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证非 Linear Transition 在创建 UObject 前被稳定诊断拒绝。
 * 测试只使用 transient Sequence，不写磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryUnsupportedBlendModeTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    UAnimSequence* Sequence = CreateTestSequence(LoadTestSkeleton());
    if (Sequence == nullptr) return true;
    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    FSekiroAnimIRGraph* StateMachineGraph = FindGraph(BlueprintIR, TEXT("Graph.StateMachine"));
    if (StateMachineGraph != nullptr)
    {
        StateMachineGraph->StateMachine.Transitions[0].Settings.BlendMode = TEXT("Cubic");
    }

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    TestNull(TEXT("Unsupported blend mode creates no blueprint"), AnimBlueprint);
    TestTrue(
        TEXT("Unsupported blend mode emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            Diagnostics,
            TEXT("Factory.UnsupportedTransitionBlendMode")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryMissingSequenceTest,
    "Sekiro.AnimGraphIR.Factory.MissingSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证不存在的 Sequence 软路径在蓝图创建前被稳定诊断拒绝。
 * 测试不创建资产、不写磁盘；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryMissingSequenceTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    FSekiroAnimBlueprintIR BlueprintIR = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph* IdleGraph = FindGraph(BlueprintIR, TEXT("Graph.Idle"));
    if (IdleGraph != nullptr)
    {
        AddSequencePose(
            *IdleGraph,
            TEXT("Node.Idle"),
            FSoftObjectPath(TEXT("/Engine/DoesNotExist.DoesNotExist")),
            false,
            BlueprintIR.SourceLocation);
    }

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    TestNull(TEXT("Missing Sequence creates no blueprint"), AnimBlueprint);
    TestTrue(
        TEXT("Missing Sequence emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("Factory.AnimationAssetLoadFailed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryMultipleLayersTest,
    "Sekiro.AnimGraphIR.Factory.MultipleLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证第二个具名 Animation Layer 会被物化为原生 FunctionGraph，而不是被拒绝或静默忽略。
 * 测试只创建 transient UObject；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryMultipleLayersTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;
    FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
    BlueprintIR.SchemaVersion = 3;
    FSekiroAnimIRLayer& SecondLayer = BlueprintIR.Layers.AddDefaulted_GetRef();
    SecondLayer.Id = TEXT("Layer.Second");
    SecondLayer.Name = TEXT("Second");
    SecondLayer.FunctionName = TEXT("Second");
    SecondLayer.RootGraphId = TEXT("Graph.Second");
    SecondLayer.SourceLocation = BlueprintIR.SourceLocation;

    FSekiroAnimIRGraph& SecondGraph = SecondLayer.Graphs.AddDefaulted_GetRef();
    SecondGraph.Id = SecondLayer.RootGraphId;
    SecondGraph.Name = TEXT("SecondGraph");
    SecondGraph.GraphType = SekiroAnimGraphIRNames::PoseGraph;
    SecondGraph.RootNodeId = TEXT("Node.Second.Output");
    SecondGraph.SourceLocation = BlueprintIR.SourceLocation;

    FSekiroAnimIRNode& SecondRoot = SecondGraph.Nodes.AddDefaulted_GetRef();
    SecondRoot.Id = SecondGraph.RootNodeId;
    SecondRoot.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
    SecondRoot.SourceLocation = BlueprintIR.SourceLocation;
    FSekiroAnimIRPin& ResultPin = SecondRoot.Pins.AddDefaulted_GetRef();
    ResultPin.Name = TEXT("Result");
    ResultPin.Direction = ESekiroAnimIRPinDirection::Input;
    ResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        BlueprintIR,
        Diagnostics);
    TestNotNull(TEXT("Multiple Layers create an AnimBlueprint"), AnimBlueprint);
    TestEqual(TEXT("Multiple Layers have no diagnostics"), Diagnostics.Num(), 0);
    bool bFoundSecondGraph = false;
    if (AnimBlueprint != nullptr)
    {
        TArray<UEdGraph*> Graphs;
        AnimBlueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph != nullptr && Graph->GetFName() == TEXT("Second"))
            {
                bFoundSecondGraph = true;
                break;
            }
        }
    }
    TestTrue(TEXT("Second Layer FunctionGraph exists"), bFoundSecondGraph);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryCompileSaveEndToEndTest,
    "Sekiro.AnimGraphIR.Factory.CompileSaveEndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证内存 Lua 模块可一键导入、生成并保存原生 AnimBlueprint，且 Rule 可绕过 Update Event 直接求值。
 * 测试使用唯一 /Game 测试包，断言完成后注销资产并删除生成文件，不在 Content 中保留测试资产。
 * 必须由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集与清理。
 */
bool FSekiroAnimBlueprintFactoryCompileSaveEndToEndTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("End-to-end test Sequence is created"), Sequence);
    TestNotNull(TEXT("End-to-end UnLua environment is available"), Environment);
    if (Sequence == nullptr || Environment == nullptr) return true;

    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.FactoryCompileSave"));
    const FString ModuleChunk = BuildEndToEndModuleChunk(
        ModuleName,
        FSoftObjectPath(Sequence).ToString());
    TestTrue(
        TEXT("End-to-end memory module is injected"),
        Environment->DoString(ModuleChunk, TEXT("SekiroAnimGraphIRTests.FactoryCompileSave.Inject")));

    const FString PackagePath(TEXT("/Game/__SekiroAnimGraphIRTests__"));
    const FString AssetName = TEXT("ABP_CompileSave_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* AnimBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CompileLuaModuleToAnimBlueprintAsset(
            ModuleName,
            PackagePath,
            AssetName,
            Diagnostics);
    TestNotNull(TEXT("One-click API creates and saves AnimBlueprint"), AnimBlueprint);
    TestEqual(TEXT("One-click API reports no diagnostics"), Diagnostics.Num(), 0);

    const FString LongPackageName = PackagePath + TEXT("/") + AssetName;
    const FString Filename = FPackageName::LongPackageNameToFilename(
        LongPackageName,
        FPackageName::GetAssetPackageExtension());
    TestTrue(TEXT("Generated package file exists"), IFileManager::Get().FileExists(*Filename));

    if (AnimBlueprint != nullptr && AnimBlueprint->GeneratedClass != nullptr)
    {
        UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
        TestEqual(
            TEXT("Saved EventGraph contains one Lua update bridge"),
            CountFunctionCalls(
                EventGraph,
                GET_FUNCTION_NAME_CHECKED(
                    USekiroLuaTransitionRuntimeLibrary,
                    EvaluateBlueprintUpdateAnimation)),
            1);
        TestEqual(
            TEXT("Saved EventGraph contains no direct Transition rule calls"),
            CountFunctionCalls(
                EventGraph,
                GET_FUNCTION_NAME_CHECKED(
                    USekiroLuaTransitionRuntimeLibrary,
                    EvaluateLuaTransitionRule)),
            0);
        TestFalse(TEXT("Saved Lua AnimBlueprint disables threaded animation update"),
            AnimBlueprint->bUseMultiThreadedAnimationUpdate);

        USkeletalMeshComponent* SkeletalMeshComponent =
            NewObject<USkeletalMeshComponent>(GetTransientPackage());
        UAnimInstance* AnimInstance = SkeletalMeshComponent != nullptr
            ? NewObject<UAnimInstance>(SkeletalMeshComponent, AnimBlueprint->GeneratedClass)
            : nullptr;
        TestNotNull(TEXT("Generated AnimInstance is created"), AnimInstance);
        if (AnimInstance != nullptr)
        {
            AnimInstance->BlueprintUpdateAnimation(1.0f / 60.0f);

            FProperty* BoolProperty = AnimBlueprint->GeneratedClass->FindPropertyByName(TEXT("GeneratedBool"));
            FProperty* FloatProperty = AnimBlueprint->GeneratedClass->FindPropertyByName(TEXT("GeneratedFloat"));
            FProperty* EnumProperty = AnimBlueprint->GeneratedClass->FindPropertyByName(TEXT("GeneratedEnum"));
            TestNotNull(TEXT("Generated bool property exists"), BoolProperty);
            TestNotNull(TEXT("Generated float property exists"), FloatProperty);
            TestNotNull(TEXT("Generated enum property exists"), EnumProperty);

            for (FProperty* Property : { BoolProperty, FloatProperty, EnumProperty })
            {
                if (Property == nullptr) continue;
                TestTrue(
                    *FString::Printf(TEXT("%s is BlueprintVisible"), *Property->GetName()),
                    Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
                TestFalse(
                    *FString::Printf(TEXT("%s is not BlueprintReadOnly"), *Property->GetName()),
                    Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
                TestFalse(
                    *FString::Printf(TEXT("%s is writable on instances"), *Property->GetName()),
                    Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
            }

            if (FBoolProperty* TypedBool = CastField<FBoolProperty>(BoolProperty))
            {
                TestTrue(TEXT("Lua writes generated bool directly"),
                    TypedBool->GetPropertyValue_InContainer(AnimInstance));
            }
            if (FFloatProperty* TypedFloat = CastField<FFloatProperty>(FloatProperty))
            {
                TestEqual(TEXT("Lua writes generated float directly"),
                    TypedFloat->GetPropertyValue_InContainer(AnimInstance),
                    42.5f + 1.0f / 60.0f);
            }
            if (FByteProperty* TypedEnum = CastField<FByteProperty>(EnumProperty))
            {
                TestEqual(TEXT("Lua writes generated enum directly"),
                    TypedEnum->GetPropertyValue_InContainer(AnimInstance),
                    static_cast<uint8>(2));
            }
            TestTrue(
                TEXT("Transition rule evaluates Lua true directly"),
                USekiroLuaTransitionRuntimeLibrary::EvaluateLuaTransitionRule(
                    AnimInstance,
                    ModuleName,
                    TEXT("CanEnter_IdleSelf")));
            TestFalse(
                TEXT("Transition rule evaluates Lua false directly"),
                USekiroLuaTransitionRuntimeLibrary::EvaluateLuaTransitionRule(
                    AnimInstance,
                    ModuleName,
                    TEXT("CanEnter_IdleSelfFalse")));
        }
    }

    if (AnimBlueprint != nullptr)
    {
        UPackage* Package = AnimBlueprint->GetOutermost();
        FAssetRegistryModule::AssetDeleted(AnimBlueprint);
        AnimBlueprint->ClearFlags(RF_Public | RF_Standalone);
        AnimBlueprint->SetFlags(RF_Transient);
        AnimBlueprint->MarkAsGarbage();
        if (Package != nullptr) Package->SetDirtyFlag(false);
    }
    IFileManager::Get().Delete(*Filename, false, true);
    TestFalse(TEXT("Generated package file is cleaned"), IFileManager::Get().FileExists(*Filename));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryUpsertSkeletalMeshSocketTest,
    "Sekiro.AnimGraphIR.Factory.UpsertSkeletalMeshSocket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Mesh Socket 工具可创建、保存并原地更新同名 Socket，同时拒绝空名称和无效骨骼。
 * 测试复制引擎 SkeletalCube 到唯一 /Game 测试包，完成后注销对象并删除生成文件，不保留 Content 资产。
 * 必须由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集与测试资产清理。
 */
bool FSekiroAnimBlueprintFactoryUpsertSkeletalMeshSocketTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* SourceMesh = LoadObject<USkeletalMesh>(
        nullptr,
        TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
    TestNotNull(TEXT("Socket test source SkeletalMesh loads"), SourceMesh);
    if (SourceMesh == nullptr) return true;

    const FString AssetName = TEXT("SKM_SocketUpsert_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString LongPackageName = TEXT("/Game/__SekiroAnimGraphIRTests__/") + AssetName;
    UPackage* Package = CreatePackage(*LongPackageName);
    USkeletalMesh* TestMesh = DuplicateObject<USkeletalMesh>(
        SourceMesh,
        Package,
        FName(*AssetName));
    TestNotNull(TEXT("Socket test duplicates a SkeletalMesh asset"), TestMesh);
    if (TestMesh == nullptr) return true;

    TestMesh->SetFlags(RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(TestMesh);
    const FReferenceSkeleton& ReferenceSkeleton = TestMesh->GetRefSkeleton();
    const FName BoneName = ReferenceSkeleton.GetNum() > 0
        ? ReferenceSkeleton.GetBoneName(0)
        : NAME_None;
    TestFalse(TEXT("Socket test mesh has a reference bone"), BoneName.IsNone());

    const FName SocketName(TEXT("FactoryTestMeshSocket"));
    const FTransform FirstTransform(
        FRotator(10.0, 20.0, 30.0),
        FVector(11.0, 12.0, 13.0),
        FVector(1.1, 1.2, 1.3));
    FString ErrorMessage;
    TestTrue(
        TEXT("Upsert creates and saves a Mesh Socket"),
        USekiroAnimBlueprintFactoryLibrary::UpsertSkeletalMeshSocket(
            TestMesh,
            SocketName,
            BoneName,
            FirstTransform,
            ErrorMessage));
    TestTrue(TEXT("Successful Socket creation clears error"), ErrorMessage.IsEmpty());

    const FString Filename = FPackageName::LongPackageNameToFilename(
        LongPackageName,
        FPackageName::GetAssetPackageExtension());
    TestTrue(TEXT("Socket upsert saves the SkeletalMesh package"), IFileManager::Get().FileExists(*Filename));

    USkeletalMeshSocket* CreatedSocket = nullptr;
    int32 MatchingSocketCount = 0;
    for (USkeletalMeshSocket* Socket : TestMesh->GetMeshOnlySocketList())
    {
        if (Socket == nullptr || Socket->SocketName != SocketName) continue;
        CreatedSocket = Socket;
        ++MatchingSocketCount;
    }
    TestEqual(TEXT("Socket upsert creates one Mesh Socket"), MatchingSocketCount, 1);
    TestNotNull(TEXT("Created Mesh Socket is discoverable"), CreatedSocket);
    if (CreatedSocket != nullptr)
    {
        TestEqual(TEXT("Created Socket uses requested bone"), CreatedSocket->BoneName, BoneName);
        TestEqual(
            TEXT("Created Socket receives relative location"),
            CreatedSocket->RelativeLocation,
            FirstTransform.GetLocation());
        TestTrue(
            TEXT("Created Socket receives relative rotation"),
            CreatedSocket->RelativeRotation.Equals(FirstTransform.Rotator()));
        TestEqual(
            TEXT("Created Socket receives relative scale"),
            CreatedSocket->RelativeScale,
            FirstTransform.GetScale3D());
    }

    const FTransform UpdatedTransform(
        FRotator(-15.0, 25.0, 35.0),
        FVector(21.0, 22.0, 23.0),
        FVector(0.9, 0.8, 0.7));
    TestTrue(
        TEXT("Second upsert updates and saves the existing Mesh Socket"),
        USekiroAnimBlueprintFactoryLibrary::UpsertSkeletalMeshSocket(
            TestMesh,
            SocketName,
            BoneName,
            UpdatedTransform,
            ErrorMessage));
    MatchingSocketCount = 0;
    USkeletalMeshSocket* UpdatedSocket = nullptr;
    for (USkeletalMeshSocket* Socket : TestMesh->GetMeshOnlySocketList())
    {
        if (Socket == nullptr || Socket->SocketName != SocketName) continue;
        UpdatedSocket = Socket;
        ++MatchingSocketCount;
    }
    TestEqual(TEXT("Second upsert does not duplicate the Mesh Socket"), MatchingSocketCount, 1);
    TestEqual(TEXT("Second upsert preserves Socket object identity"), UpdatedSocket, CreatedSocket);
    if (UpdatedSocket != nullptr)
    {
        TestEqual(
            TEXT("Second upsert replaces relative location"),
            UpdatedSocket->RelativeLocation,
            UpdatedTransform.GetLocation());
        TestTrue(
            TEXT("Second upsert replaces relative rotation"),
            UpdatedSocket->RelativeRotation.Equals(UpdatedTransform.Rotator()));
        TestEqual(
            TEXT("Second upsert replaces relative scale"),
            UpdatedSocket->RelativeScale,
            UpdatedTransform.GetScale3D());
    }

    TestFalse(
        TEXT("Upsert rejects a missing reference bone"),
        USekiroAnimBlueprintFactoryLibrary::UpsertSkeletalMeshSocket(
            TestMesh,
            TEXT("InvalidBoneSocket"),
            TEXT("MissingFactoryTestBone"),
            FTransform::Identity,
            ErrorMessage));
    TestFalse(TEXT("Invalid bone reports an error"), ErrorMessage.IsEmpty());
    TestFalse(
        TEXT("Upsert rejects an empty Socket name"),
        USekiroAnimBlueprintFactoryLibrary::UpsertSkeletalMeshSocket(
            TestMesh,
            NAME_None,
            BoneName,
            FTransform::Identity,
            ErrorMessage));
    TestFalse(TEXT("Empty Socket name reports an error"), ErrorMessage.IsEmpty());

    IFileManager::Get().Delete(*Filename, false, true);
    TestMesh->ClearFlags(RF_Public | RF_Standalone);
    TestMesh->SetFlags(RF_Transient);
    TestMesh->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    TestFalse(TEXT("Socket test package file is cleaned"), IFileManager::Get().FileExists(*Filename));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimBlueprintSourceStalePackageTest,
    "Sekiro.AnimGraphIR.DirtyCompile.SourceStaleDoesNotDirtyPackage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua 文件监听产生的源码过期状态只留在内存，不创建事务或把 AnimBlueprint package 标记为待保存。
 * 测试创建唯一临时 package 和标准 UAnimBlueprint，不读取 Lua、不生成 Graph，也不写入磁盘。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集和临时对象清理。
 */
bool FSekiroLuaAnimBlueprintSourceStalePackageTest::RunTest(
    const FString& Parameters)
{
    const FString PackageName = FString::Printf(
        TEXT("/Temp/SekiroLuaSourceStale_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UPackage* Package = CreatePackage(*PackageName);
    UAnimBlueprint* AnimBlueprint = NewObject<UAnimBlueprint>(
        Package,
        TEXT("ABP_SourceStale"),
        RF_Public | RF_Standalone | RF_Transactional);
    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Request(AnimBlueprint);
    TestNotNull(TEXT("Source stale test package is created"), Package);
    TestNotNull(TEXT("Source stale test AnimBlueprint is created"), AnimBlueprint);
    TestNotNull(TEXT("Source stale test extension is attached"), Extension);
    if (Package == nullptr || AnimBlueprint == nullptr || Extension == nullptr) return true;

    Extension->LuaModuleName = TEXT("Animation.Tests.SourceStale");
    Extension->bSourceDirty = false;
    const int32 SourceRevisionBefore = Extension->SourceRevision;
    Package->SetDirtyFlag(false);

    const int32 StaleSourceCount =
        USekiroAnimBlueprintFactoryLibrary::MarkLoadedLuaAnimBlueprintsDirty(
            TEXT("Automation source change."));
    TestTrue(TEXT("Loaded Lua source is marked stale"), StaleSourceCount > 0);
    TestTrue(TEXT("Source stale state is updated in memory"), Extension->bSourceDirty);
    TestEqual(
        TEXT("Source stale state increments the observed revision"),
        Extension->SourceRevision,
        SourceRevisionBefore + 1);
    TestFalse(
        TEXT("Source stale state does not dirty the AnimBlueprint package"),
        Package->IsDirty());

    AnimBlueprint->ClearFlags(RF_Public | RF_Standalone);
    AnimBlueprint->SetFlags(RF_Transient);
    AnimBlueprint->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimBlueprintInPlaceCompileTest,
    "Sekiro.AnimGraphIR.Factory.LuaAssetInPlaceCompile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证专用 Factory 创建的是带 Lua 元数据的标准 UAnimBlueprint，并可在同一 UObject 与对象路径上幂等重编译。
 * 测试连续编译两次同一内存 Lua 模块，检查变量、主状态机、State、Transition 和 EventGraph 调用均不重复。
 * 必须由 Automation Framework 在游戏线程执行；资产位于 transient package，不写入 Content。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集和 transient 对象清理。
 */
bool FSekiroLuaAnimBlueprintInPlaceCompileTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    UnLua::FLuaEnv* Environment = SekiroAnimGraphIRLuaImporterTests::GetOrActivateTestEnvironment();
    TestNotNull(TEXT("In-place test Skeleton is loaded"), Skeleton);
    TestNotNull(TEXT("In-place test Sequence is created"), Sequence);
    TestNotNull(TEXT("In-place test UnLua environment is available"), Environment);
    if (Skeleton == nullptr || Sequence == nullptr || Environment == nullptr) return true;

    const FString ModuleName(TEXT("SekiroAnimGraphIRTests.FactoryInPlace"));
    const FString ModuleChunk = BuildEndToEndModuleChunk(
        ModuleName,
        FSoftObjectPath(Sequence).ToString());
    TestTrue(
        TEXT("In-place memory module is injected"),
        Environment->DoString(ModuleChunk, TEXT("SekiroAnimGraphIRTests.FactoryInPlace.Inject")));

    USekiroLuaAnimBlueprintFactory* LuaFactory =
        NewObject<USekiroLuaAnimBlueprintFactory>(GetTransientPackage());
    LuaFactory->ParentClass = UAnimInstance::StaticClass();
    LuaFactory->TargetSkeleton = Skeleton;
    LuaFactory->LuaModuleName = ModuleName;
    UFactory* FactoryInterface = LuaFactory;
    const FName AssetName = MakeUniqueObjectName(
        GetTransientPackage(),
        UAnimBlueprint::StaticClass(),
        TEXT("ABP_LuaInPlace"));
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FactoryInterface->FactoryCreateNew(
        UAnimBlueprint::StaticClass(),
        GetTransientPackage(),
        AssetName,
        RF_Transient | RF_Transactional,
        nullptr,
        GWarn,
        TEXT("SekiroLuaAnimBlueprintInPlaceTest")));
    TestNotNull(TEXT("Lua Factory creates an AnimBlueprint"), AnimBlueprint);
    if (AnimBlueprint == nullptr) return true;

    TestFalse(TEXT("Lua Factory disables threaded animation update immediately"),
        AnimBlueprint->bUseMultiThreadedAnimationUpdate);
    TestEqual(
        TEXT("Lua asset remains exact standard UAnimBlueprint class"),
        AnimBlueprint->GetClass(),
        UAnimBlueprint::StaticClass());
    USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    TestNotNull(TEXT("Lua source extension is attached"), Extension);
    if (Extension != nullptr)
    {
        TestEqual(TEXT("Lua module metadata is preserved"), Extension->LuaModuleName, ModuleName);
        TestEqual(
            TEXT("Lua Factory does not repurpose deprecated SourceMode"),
            Extension->SourceMode,
            ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint);
        TestTrue(TEXT("New Lua asset starts source dirty"), Extension->bSourceDirty);
        TestEqual(TEXT("New Lua asset has initial source revision"), Extension->SourceRevision, 1);
    }
    const FProperty* DeprecatedSourceModeProperty =
        FindFProperty<FProperty>(
            USekiroLuaAnimBlueprintExtension::StaticClass(),
            GET_MEMBER_NAME_CHECKED(USekiroLuaAnimBlueprintExtension, SourceMode));
    TestTrue(
        TEXT("Legacy SourceMode property remains serialized but deprecated"),
        DeprecatedSourceModeProperty != nullptr
            && DeprecatedSourceModeProperty->HasAnyPropertyFlags(CPF_Deprecated));

    UAnimBlueprint* const OriginalObject = AnimBlueprint;
    const FString OriginalObjectPath = AnimBlueprint->GetPathName();
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("First in-place compilation succeeds"),
        USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
            AnimBlueprint,
            false,
            Diagnostics));
    TestEqual(TEXT("First in-place compilation has no diagnostics"), Diagnostics.Num(), 0);
    TestTrue(
        TEXT("First in-place compilation creates GeneratedClass"),
        AnimBlueprint->GeneratedClass != nullptr);

    UAnimationGraph* MainGraph = nullptr;
    for (UEdGraph* FunctionGraph : AnimBlueprint->FunctionGraphs)
    {
        UAnimationGraph* Candidate = Cast<UAnimationGraph>(FunctionGraph);
        if (Candidate != nullptr && !Candidate->IsA<UAnimationStateGraph>())
        {
            MainGraph = Candidate;
            break;
        }
    }
    UAnimGraphNode_StateMachine* StateMachineNode =
        FindFirstNode<UAnimGraphNode_StateMachine>(MainGraph);
    UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode != nullptr
        ? StateMachineNode->EditorStateMachineGraph
        : nullptr;
    UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
    const int32 FirstVariableCount = AnimBlueprint->NewVariables.Num();
    const int32 FirstMachineCount = CountNodes<UAnimGraphNode_StateMachine>(MainGraph);
    const int32 FirstStateCount = CountNodes<UAnimStateNode>(StateMachineGraph);
    const int32 FirstTransitionCount = CountNodes<UAnimStateTransitionNode>(StateMachineGraph);
    const int32 FirstUpdateCallCount = CountFunctionCalls(
        EventGraph,
        GET_FUNCTION_NAME_CHECKED(
            USekiroLuaTransitionRuntimeLibrary,
            EvaluateBlueprintUpdateAnimation));
    UAnimGraphNode_StateMachine* const OriginalStateMachineNode = StateMachineNode;
    UAnimStateNode* const OriginalStateNode =
        FindFirstNode<UAnimStateNode>(StateMachineGraph);
    UAnimStateTransitionNode* const OriginalTransitionNode =
        FindFirstNode<UAnimStateTransitionNode>(StateMachineGraph);
    if (OriginalStateMachineNode != nullptr)
    {
        OriginalStateMachineNode->NodePosX = 137;
        OriginalStateMachineNode->NodePosY = 419;
    }
    if (OriginalStateNode != nullptr)
    {
        OriginalStateNode->NodePosX = 811;
        OriginalStateNode->NodePosY = 233;
    }
    TArray<UEdGraph*> FirstAllGraphs;
    AnimBlueprint->GetAllGraphs(FirstAllGraphs);

    FEdGraphPinType ManualVariableType;
    ManualVariableType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    const FName ManualVariableName(TEXT("EditorOwnedFlag"));
    TestTrue(
        TEXT("Editor-owned variable is added before Lua regeneration"),
        FBlueprintEditorUtils::AddMemberVariable(
            AnimBlueprint,
            ManualVariableName,
            ManualVariableType,
            TEXT("false")));
    UK2Node_Self* EditorOwnedEventNode = nullptr;
    UK2Node_Self* EditorOwnedAnimGraphNode = nullptr;
    if (EventGraph != nullptr)
    {
        FGraphNodeCreator<UK2Node_Self> NodeCreator(*EventGraph);
        EditorOwnedEventNode = NodeCreator.CreateNode(false);
        NodeCreator.Finalize();
        EditorOwnedEventNode->NodePosX = -500;
        EditorOwnedEventNode->NodePosY = 400;
    }
    if (MainGraph != nullptr)
    {
        FGraphNodeCreator<UK2Node_Self> NodeCreator(*MainGraph);
        EditorOwnedAnimGraphNode = NodeCreator.CreateNode(false);
        NodeCreator.Finalize();
        EditorOwnedAnimGraphNode->NodePosX = -750;
        EditorOwnedAnimGraphNode->NodePosY = 620;
    }

    bool bRecursiveCallbackObserved = false;
    bool bRecursiveCompileSucceeded = true;
    TArray<FSekiroAnimIRDiagnostic> RecursiveDiagnostics;
    AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
    const FDelegateHandle PreCompileHandle = GEditor->OnBlueprintPreCompile().AddLambda(
        [&](UBlueprint* BlueprintToCompile)
        {
            if (BlueprintToCompile != AnimBlueprint || bRecursiveCallbackObserved) return;
            bRecursiveCallbackObserved = true;
            bRecursiveCompileSucceeded =
                USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
                    AnimBlueprint,
                    false,
                    RecursiveDiagnostics);
        });
    Diagnostics.Reset();
    TestTrue(
        TEXT("Second in-place compilation succeeds"),
        USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
            AnimBlueprint,
            false,
            Diagnostics));
    GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
    TestEqual(TEXT("Second in-place compilation has no diagnostics"), Diagnostics.Num(), 0);
    TestTrue(TEXT("Native precompile callback attempted recursive compile"), bRecursiveCallbackObserved);
    TestFalse(TEXT("Recursive in-place compile is rejected"), bRecursiveCompileSucceeded);
    TestTrue(
        TEXT("Recursive compile emits stable reentry diagnostic"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            RecursiveDiagnostics,
            TEXT("Factory.CompileAssetReentry")));
    TestEqual(TEXT("In-place compilation preserves UObject identity"), AnimBlueprint, OriginalObject);
    TestEqual(TEXT("In-place compilation preserves object path"), AnimBlueprint->GetPathName(), OriginalObjectPath);
    TestTrue(
        TEXT("Second in-place compilation keeps GeneratedClass valid"),
        AnimBlueprint->GeneratedClass != nullptr);
    TestFalse(
        TEXT("In-place Graph regeneration preserves disabled threaded animation update"),
        AnimBlueprint->bUseMultiThreadedAnimationUpdate);
    const UAnimInstance* InPlaceGeneratedDefaultInstance =
        AnimBlueprint->GeneratedClass != nullptr
            ? Cast<UAnimInstance>(AnimBlueprint->GeneratedClass->GetDefaultObject())
            : nullptr;
    TestTrue(
        TEXT("In-place generated default instance preserves disabled threaded animation update"),
        InPlaceGeneratedDefaultInstance != nullptr
            && !InPlaceGeneratedDefaultInstance->bUseMultiThreadedAnimationUpdate);

    MainGraph = nullptr;
    for (UEdGraph* FunctionGraph : AnimBlueprint->FunctionGraphs)
    {
        UAnimationGraph* Candidate = Cast<UAnimationGraph>(FunctionGraph);
        if (Candidate != nullptr && !Candidate->IsA<UAnimationStateGraph>())
        {
            MainGraph = Candidate;
            break;
        }
    }
    StateMachineNode = FindFirstNode<UAnimGraphNode_StateMachine>(MainGraph);
    StateMachineGraph = StateMachineNode != nullptr
        ? StateMachineNode->EditorStateMachineGraph
        : nullptr;
    EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
    TestEqual(
        TEXT("Lua variables do not duplicate and editor variable remains"),
        AnimBlueprint->NewVariables.Num(),
        FirstVariableCount + 1);
    TestTrue(
        TEXT("Editor-owned variable survives Lua regeneration"),
        AnimBlueprint->NewVariables.ContainsByPredicate(
            [ManualVariableName](const FBPVariableDescription& Variable)
            {
                return Variable.VarName == ManualVariableName;
            }));
    TestTrue(
        TEXT("Editor-owned EventGraph node survives Lua regeneration"),
        EventGraph != nullptr
            && EditorOwnedEventNode != nullptr
            && EventGraph->Nodes.Contains(EditorOwnedEventNode));
    TestTrue(
        TEXT("Editor-owned AnimGraph node survives Lua regeneration"),
        MainGraph != nullptr
            && EditorOwnedAnimGraphNode != nullptr
            && MainGraph->Nodes.Contains(EditorOwnedAnimGraphNode));
    TestEqual(
        TEXT("Incremental generation preserves state machine UObject"),
        StateMachineNode,
        OriginalStateMachineNode);
    TestEqual(
        TEXT("Incremental generation preserves state UObject"),
        FindFirstNode<UAnimStateNode>(StateMachineGraph),
        OriginalStateNode);
    TestEqual(
        TEXT("Incremental generation preserves transition UObject"),
        FindFirstNode<UAnimStateTransitionNode>(StateMachineGraph),
        OriginalTransitionNode);
    if (StateMachineNode != nullptr)
    {
        TestEqual(
            TEXT("Incremental generation preserves state machine X position"),
            StateMachineNode->NodePosX,
            137);
        TestEqual(
            TEXT("Incremental generation preserves state machine Y position"),
            StateMachineNode->NodePosY,
            419);
    }
    UAnimStateNode* CurrentStateNode = FindFirstNode<UAnimStateNode>(StateMachineGraph);
    if (CurrentStateNode != nullptr)
    {
        TestEqual(
            TEXT("Incremental generation preserves state X position"),
            CurrentStateNode->NodePosX,
            811);
        TestEqual(
            TEXT("Incremental generation preserves state Y position"),
            CurrentStateNode->NodePosY,
            233);
    }
    TestEqual(
        TEXT("Root state machine does not duplicate"),
        CountNodes<UAnimGraphNode_StateMachine>(MainGraph),
        FirstMachineCount);
    TestEqual(TEXT("States do not duplicate"), CountNodes<UAnimStateNode>(StateMachineGraph), FirstStateCount);
    TestEqual(
        TEXT("Transitions do not duplicate"),
        CountNodes<UAnimStateTransitionNode>(StateMachineGraph),
        FirstTransitionCount);
    TestEqual(
        TEXT("EventGraph Lua update bridge does not duplicate"),
        CountFunctionCalls(
            EventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateBlueprintUpdateAnimation)),
        FirstUpdateCallCount);
    TArray<UEdGraph*> SecondAllGraphs;
    AnimBlueprint->GetAllGraphs(SecondAllGraphs);
    TestEqual(TEXT("Owned Graphs do not duplicate"), SecondAllGraphs.Num(), FirstAllGraphs.Num());

    Extension = USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    TestNotNull(TEXT("Lua extension survives native AnimBlueprint compilation"), Extension);
    if (Extension != nullptr)
    {
        TestEqual(
            TEXT("Successful compile records current generator version"),
            Extension->CompilerVersion,
            USekiroLuaAnimBlueprintExtension::CurrentCompilerVersion);
        TestEqual(TEXT("Successful revision increments twice"), Extension->SuccessfulCompileRevision, 2);
        TestFalse(TEXT("Successful compile clears source dirty"), Extension->bSourceDirty);
        TestEqual(
            TEXT("Successful source revision matches observed source"),
            Extension->SuccessfulSourceRevision,
            Extension->SourceRevision);
        TestEqual(
            TEXT("Compile status is up to date"),
            Extension->CompileStatus,
            ESekiroLuaAnimBlueprintCompileStatus::UpToDate);
        TestTrue(
            TEXT("Successful generation persists incremental ownership baseline"),
            Extension->bHasLastGeneratedIR);
    }
    const TArray<UEdGraphNode*> NodesBeforeCheck = MainGraph->Nodes;
    const int32 VariablesBeforeCheck = AnimBlueprint->NewVariables.Num();
    UClass* const GeneratedClassBeforeCheck = AnimBlueprint->GeneratedClass;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Check Lua succeeds without Graph generation"),
        USekiroAnimBlueprintFactoryLibrary::CheckLuaAnimBlueprint(
            AnimBlueprint,
            Diagnostics));
    TestEqual(TEXT("Check Lua has no diagnostics"), Diagnostics.Num(), 0);
    TestEqual(
        TEXT("Check Lua does not change main Graph node count"),
        MainGraph->Nodes.Num(),
        NodesBeforeCheck.Num());
    bool bCheckPreservedNodeIdentity = MainGraph->Nodes.Num() == NodesBeforeCheck.Num();
    for (int32 NodeIndex = 0;
         bCheckPreservedNodeIdentity && NodeIndex < NodesBeforeCheck.Num();
         ++NodeIndex)
    {
        bCheckPreservedNodeIdentity =
            MainGraph->Nodes[NodeIndex].Get() == NodesBeforeCheck[NodeIndex];
    }
    TestTrue(TEXT("Check Lua preserves main Graph node identity"), bCheckPreservedNodeIdentity);
    TestEqual(
        TEXT("Check Lua does not change variables"),
        AnimBlueprint->NewVariables.Num(),
        VariablesBeforeCheck);
    TestEqual(
        TEXT("Check Lua does not call native compilation"),
        AnimBlueprint->GeneratedClass.Get(),
        GeneratedClassBeforeCheck);

    const TArray<UEdGraphNode*> NodesBeforeExplicitRecovery = MainGraph->Nodes;
    for (UEdGraphNode* Node : NodesBeforeExplicitRecovery)
    {
        if (Node == nullptr || Node->IsA<UAnimGraphNode_Root>()) continue;
        FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, true);
    }
    Diagnostics.Reset();
    TestTrue(
        TEXT("Lua → AnimBlueprint restores cleared nodes"),
        USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
            AnimBlueprint,
            Diagnostics));
    MainGraph = FindMainGraph(AnimBlueprint);
    TestEqual(
        TEXT("Explicit Generate restores Lua state machine after node clearing"),
        CountNodes<UAnimGraphNode_StateMachine>(MainGraph),
        FirstMachineCount);

    EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
    if (MainGraph != nullptr)
    {
        FBlueprintEditorUtils::RemoveGraph(
            AnimBlueprint,
            MainGraph,
            EGraphRemoveFlags::MarkTransient);
    }
    if (EventGraph != nullptr)
    {
        FBlueprintEditorUtils::RemoveGraph(
            AnimBlueprint,
            EventGraph,
            EGraphRemoveFlags::MarkTransient);
    }
    Diagnostics.Reset();
    TestTrue(
        TEXT("Lua → AnimBlueprint recreates missing Graph shells"),
        USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
            AnimBlueprint,
            Diagnostics));
    MainGraph = FindMainGraph(AnimBlueprint);
    EventGraph = FBlueprintEditorUtils::FindEventGraph(AnimBlueprint);
    TestNotNull(TEXT("Explicit Generate recreates main AnimGraph"), MainGraph);
    TestNotNull(TEXT("Explicit Generate recreates EventGraph"), EventGraph);
    TestNotNull(
        TEXT("Explicit Generate recreates Root"),
        FindFirstNode<UAnimGraphNode_Root>(MainGraph));
    TestEqual(
        TEXT("Recreated AnimGraph contains Lua state machine"),
        CountNodes<UAnimGraphNode_StateMachine>(MainGraph),
        FirstMachineCount);

    int32 NativeCompileCallCount = 0;
    TSharedRef<FUICommandList> TestCommandList = MakeShared<FUICommandList>();
    const TSharedPtr<const FUICommandInfo> TestCompileCommand =
        FGenericCommands::Get().Delete;
    TestCommandList->MapAction(
        TestCompileCommand,
        FUIAction(FExecuteAction::CreateLambda([&]()
        {
            ++NativeCompileCallCount;
            FKismetEditorUtilities::CompileBlueprint(
                AnimBlueprint,
                EBlueprintCompileOptions::SkipGarbageCollection);
        })));
    TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> CommandBinding =
        FSekiroLuaAnimBlueprintEditorBinding::CreateForTest(
            TestCommandList,
            AnimBlueprint);
    const TSharedRef<FExtender> FirstToolbarExtender =
        CommandBinding->GetToolbarExtender();
    const TSharedRef<FExtender> RebuiltToolbarExtender =
        CommandBinding->GetToolbarExtender();
    TestTrue(
        TEXT("Toolbar rebuild reuses one Lua extender"),
        &FirstToolbarExtender.Get() == &RebuiltToolbarExtender.Get());
    FToolBarBuilder DisplayedToolbarBuilder(
        TestCommandList,
        FMultiBoxCustomization::AllowCustomization(
            TEXT("AssetEditorToolbar.CommonActions")));
    CommandBinding->FillToolbar(DisplayedToolbarBuilder);
    const int32 DisplayedToolbarBlockCount =
        DisplayedToolbarBuilder.GetMultiBox()->GetBlocks().Num();
    TestTrue(
        TEXT("First toolbar build immediately receives Lua controls"),
        DisplayedToolbarBlockCount == 6);
    CommandBinding->FillToolbar(DisplayedToolbarBuilder);
    TestEqual(
        TEXT("Repeated displayed toolbar fill does not duplicate Lua controls"),
        DisplayedToolbarBuilder.GetMultiBox()->GetBlocks().Num(),
        DisplayedToolbarBlockCount);
    Extension = USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    const int32 CheckedRevisionBeforeNativeCommand =
        Extension != nullptr ? Extension->LastCheckedSourceRevision : INDEX_NONE;
    const int32 SuccessfulRevisionBeforeNativeCommand =
        Extension != nullptr ? Extension->SuccessfulCompileRevision : 0;
    const TArray<UEdGraphNode*> NodesBeforeNativeCommand =
        MainGraph != nullptr ? MainGraph->Nodes : TArray<UEdGraphNode*>();
    if (Extension != nullptr)
    {
        Extension->MarkSourceDirty(TEXT("Native Compile independence test."));
    }
    TestTrue(
        TEXT("Native Compile command list executes mapped action"),
        TestCommandList->ExecuteAction(TestCompileCommand.ToSharedRef()));
    TestEqual(TEXT("Native Compile action executes exactly once"), NativeCompileCallCount, 1);
    if (Extension != nullptr)
    {
        TestEqual(
            TEXT("Native Compile does not run Check Lua"),
            Extension->LastCheckedSourceRevision,
            CheckedRevisionBeforeNativeCommand);
        TestEqual(
            TEXT("Native Compile does not update Lua successful revision"),
            Extension->SuccessfulCompileRevision,
            SuccessfulRevisionBeforeNativeCommand);
        TestTrue(TEXT("Native Compile preserves Lua source dirty"), Extension->bSourceDirty);
    }
    MainGraph = FindMainGraph(AnimBlueprint);
    bool bNativeCompilePreservedNodeIdentity =
        MainGraph != nullptr
        && MainGraph->Nodes.Num() == NodesBeforeNativeCommand.Num();
    for (int32 NodeIndex = 0;
         bNativeCompilePreservedNodeIdentity
            && NodeIndex < NodesBeforeNativeCommand.Num();
         ++NodeIndex)
    {
        bNativeCompilePreservedNodeIdentity =
            MainGraph->Nodes[NodeIndex].Get() == NodesBeforeNativeCommand[NodeIndex];
    }
    TestTrue(
        TEXT("Native Compile skips Lua Graph regeneration"),
        bNativeCompilePreservedNodeIdentity);
    const int32 SuccessfulRevisionAfterCommand =
        Extension != nullptr ? Extension->SuccessfulCompileRevision : 0;

    UClass* const LastSuccessfulGeneratedClass = AnimBlueprint->GeneratedClass;
    const FString InvalidModuleName(TEXT("SekiroAnimGraphIRTests.FactoryInPlaceInvalid"));
    const FString InvalidModuleChunk = BuildEndToEndModuleChunk(
        InvalidModuleName,
        TEXT("/Game/__SekiroAnimGraphIRTests__/MissingSequence.MissingSequence"));
    TestTrue(
        TEXT("Invalid in-place memory module is injected"),
        Environment->DoString(
            InvalidModuleChunk,
            TEXT("SekiroAnimGraphIRTests.FactoryInPlaceInvalid.Inject")));
    if (Extension != nullptr)
    {
        Extension->LuaModuleName = InvalidModuleName;
        Extension->MarkSourceDirty(TEXT("Test invalid Lua source revision."));
    }
    const int32 VariablesBeforeFailedGenerate = AnimBlueprint->NewVariables.Num();
    const TArray<UEdGraphNode*> NodesBeforeFailedGenerate = MainGraph->Nodes;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Preflight failure rejects Lua → AnimBlueprint"),
        USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
            AnimBlueprint,
            Diagnostics));
    TestTrue(
        TEXT("Preflight failure emits missing sequence diagnostic"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            Diagnostics,
            TEXT("Factory.AnimationAssetLoadFailed")));
    TestEqual(
        TEXT("Preflight failure preserves GeneratedClass"),
        AnimBlueprint->GeneratedClass.Get(),
        LastSuccessfulGeneratedClass);
    TestEqual(
        TEXT("Preflight failure preserves variables"),
        AnimBlueprint->NewVariables.Num(),
        VariablesBeforeFailedGenerate);
    TestEqual(
        TEXT("Preflight failure preserves root state machine"),
        CountNodes<UAnimGraphNode_StateMachine>(MainGraph),
        FirstMachineCount);
    TestEqual(
        TEXT("Failed Generate leaves native Graph node count untouched"),
        MainGraph->Nodes.Num(),
        NodesBeforeFailedGenerate.Num());
    bool bFailedGeneratePreservedNodeIdentity =
        MainGraph->Nodes.Num() == NodesBeforeFailedGenerate.Num();
    for (int32 NodeIndex = 0;
         bFailedGeneratePreservedNodeIdentity
            && NodeIndex < NodesBeforeFailedGenerate.Num();
         ++NodeIndex)
    {
        bFailedGeneratePreservedNodeIdentity =
            MainGraph->Nodes[NodeIndex].Get() == NodesBeforeFailedGenerate[NodeIndex];
    }
    TestTrue(
        TEXT("Failed Generate leaves native Graph node identity untouched"),
        bFailedGeneratePreservedNodeIdentity);
    if (Extension != nullptr)
    {
        TestEqual(
            TEXT("Preflight failure does not increment successful revision"),
            Extension->SuccessfulCompileRevision,
            SuccessfulRevisionAfterCommand);
        TestEqual(
            TEXT("Preflight failure updates compile status"),
            Extension->CompileStatus,
            ESekiroLuaAnimBlueprintCompileStatus::Error);
        TestTrue(TEXT("Preflight failure preserves source dirty"), Extension->bSourceDirty);
        TestTrue(
            TEXT("Failed source revision remains newer than successful source revision"),
            Extension->SourceRevision > Extension->SuccessfulSourceRevision);
    }

    AnimBlueprint->ClearFlags(RF_Public | RF_Standalone);
    AnimBlueprint->SetFlags(RF_Transient);
    AnimBlueprint->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryAnimationLayerInterfaceTest,
    "Sekiro.AnimGraphIR.Factory.AnimationLayerInterface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 IR v3 可生成真实 Animation Layer Interface，并由普通 AnimBlueprint 实现同签名 Layer Graph。
 * 测试只创建 transient UObject，必须在 Automation Framework 游戏线程执行，不保存资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryAnimationLayerInterfaceTest::RunTest(const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR InterfaceIR = MakeFactoryIR(Sequence);
    InterfaceIR.SchemaVersion = 3;
    InterfaceIR.BlueprintKind = ESekiroAnimIRBlueprintKind::AnimationLayerInterface;
    InterfaceIR.ParentAnimInstanceClass.Reset();
    InterfaceIR.TargetSkeleton.Reset();
    InterfaceIR.Variables.Reset();
    InterfaceIR.Layers.SetNum(1);
    FSekiroAnimIRLayer& InterfaceLayer = InterfaceIR.Layers[0];
    InterfaceLayer.Name = TEXT("Locomotion");
    InterfaceLayer.FunctionName = TEXT("Locomotion");
    InterfaceLayer.bOverride = false;
    InterfaceLayer.InterfaceClass.Reset();
    FSekiroAnimIRFunctionParameter& PoseParameter =
        InterfaceLayer.Parameters.AddDefaulted_GetRef();
    PoseParameter.Name = TEXT("SourcePose");
    PoseParameter.DataType = SekiroAnimGraphIRNames::PoseData;
    PoseParameter.bIsPose = true;
    PoseParameter.SourceLocation = InterfaceIR.SourceLocation;

    TArray<FSekiroAnimIRDiagnostic> InterfaceDiagnostics;
    UAnimBlueprint* InterfaceBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            InterfaceIR,
            InterfaceDiagnostics);
    TestNotNull(TEXT("Animation Layer Interface is generated"), InterfaceBlueprint);
    TestEqual(TEXT("Interface generation has no diagnostics"), InterfaceDiagnostics.Num(), 0);
    if (InterfaceBlueprint == nullptr || InterfaceBlueprint->GeneratedClass == nullptr) return true;
    TestEqual(
        TEXT("Generated asset uses the native interface Blueprint type"),
        InterfaceBlueprint->BlueprintType,
        BPTYPE_Interface);
    TestNotNull(
        TEXT("Generated interface exposes the declared Layer UFunction"),
        InterfaceBlueprint->GeneratedClass->FindFunctionByName(TEXT("Locomotion")));

    FSekiroAnimBlueprintIR ImplementationIR = MakeFactoryIR(Sequence);
    ImplementationIR.SchemaVersion = 3;
    const FSoftClassPath InterfaceClassPath(
        InterfaceBlueprint->GeneratedClass->GetPathName());
    ImplementationIR.ImplementedInterfaces.Add(InterfaceClassPath);
    FSekiroAnimIRLayer OverrideLayer = InterfaceLayer;
    OverrideLayer.Id = TEXT("Layer.Locomotion");
    OverrideLayer.RootGraphId = TEXT("Graph.Locomotion");
    OverrideLayer.InterfaceClass = InterfaceClassPath;
    OverrideLayer.bOverride = true;
    OverrideLayer.Graphs.SetNum(1);
    OverrideLayer.Graphs[0].Id = OverrideLayer.RootGraphId;
    OverrideLayer.Graphs[0].Name = TEXT("Locomotion");
    OverrideLayer.Graphs[0].GraphType = SekiroAnimGraphIRNames::PoseGraph;
    OverrideLayer.Graphs[0].Nodes.SetNum(1);
    OverrideLayer.Graphs[0].Nodes[0].Id = TEXT("Node.Locomotion.Output");
    OverrideLayer.Graphs[0].Nodes[0].NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
    OverrideLayer.Graphs[0].Nodes[0].Pins =
        InterfaceLayer.Graphs[0].Nodes[0].Pins;
    OverrideLayer.Graphs[0].RootNodeId = OverrideLayer.Graphs[0].Nodes[0].Id;
    OverrideLayer.Graphs[0].Links.Reset();
    OverrideLayer.Graphs[0].StateMachine = FSekiroAnimIRStateMachine();
    OverrideLayer.Graphs[0].Layout = FSekiroAnimIRGraphLayout();
    ImplementationIR.Layers.Add(OverrideLayer);
    FSekiroAnimIRLayer PreservedLayer = OverrideLayer;
    PreservedLayer.Id = TEXT("Layer.Preserved");
    PreservedLayer.Name = TEXT("PreservedLayer");
    PreservedLayer.FunctionName = TEXT("PreservedLayer");
    PreservedLayer.InterfaceClass.Reset();
    PreservedLayer.bOverride = false;
    PreservedLayer.RootGraphId = TEXT("Graph.Preserved");
    PreservedLayer.Graphs[0].Id = PreservedLayer.RootGraphId;
    PreservedLayer.Graphs[0].Name = TEXT("PreservedLayer");
    PreservedLayer.Graphs[0].RootNodeId = TEXT("Node.Preserved.Output");
    PreservedLayer.Graphs[0].Nodes[0].Id = PreservedLayer.Graphs[0].RootNodeId;
    ImplementationIR.Layers.Add(PreservedLayer);

    TArray<FSekiroAnimIRDiagnostic> ImplementationDiagnostics;
    UAnimBlueprint* ImplementationBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            ImplementationIR,
            ImplementationDiagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : ImplementationDiagnostics)
    {
        AddInfo(
            FString::Printf(
                TEXT("%s: %s"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message));
    }
    TestNotNull(TEXT("AnimBlueprint implements generated interface"), ImplementationBlueprint);
    TestEqual(
        TEXT("Interface implementation has no diagnostics"),
        ImplementationDiagnostics.Num(),
        0);
    if (ImplementationBlueprint != nullptr)
    {
        UFunction* OverrideFunction =
            ImplementationBlueprint->GeneratedClass != nullptr
                ? ImplementationBlueprint->GeneratedClass->FindFunctionByName(
                    TEXT("Locomotion"))
                : nullptr;
        TestNotNull(
            TEXT("Interface Layer override exposes its native UFunction"),
            OverrideFunction);
        TestNotNull(
            TEXT("Interface Layer override keeps reflected SourcePose parameter"),
            OverrideFunction != nullptr
                ? FindFProperty<FProperty>(OverrideFunction, TEXT("SourcePose"))
                : nullptr);
    }

    if (ImplementationBlueprint == nullptr
        || ImplementationBlueprint->GeneratedClass == nullptr)
    {
        return true;
    }

    FSekiroAnimBlueprintIR ChildIR = MakeFactoryIR(Sequence);
    ChildIR.SchemaVersion = 3;
    ChildIR.ParentAnimInstanceClass =
        FSoftClassPath(ImplementationBlueprint->GeneratedClass->GetPathName());
    FSekiroAnimIRLayer ChildOverride = OverrideLayer;
    ChildOverride.Id = TEXT("Layer.Child.Locomotion");
    ChildOverride.RootGraphId = TEXT("Graph.Child.Locomotion");
    ChildOverride.Graphs[0].Id = ChildOverride.RootGraphId;
    ChildOverride.Graphs[0].RootNodeId = TEXT("Node.Child.Locomotion.Output");
    ChildOverride.Graphs[0].Nodes[0].Id = ChildOverride.Graphs[0].RootNodeId;
    ChildIR.Layers.Add(ChildOverride);

    TArray<FSekiroAnimIRDiagnostic> ChildDiagnostics;
    UAnimBlueprint* ChildBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            ChildIR,
            ChildDiagnostics);
    TestNotNull(TEXT("Child AnimBlueprint overrides parent Layer"), ChildBlueprint);
    TestEqual(TEXT("Child Layer override has no diagnostics"), ChildDiagnostics.Num(), 0);
    if (ChildBlueprint != nullptr && ChildBlueprint->GeneratedClass != nullptr)
    {
        bool bOwnsLocomotionOverride = false;
        bool bCopiedPreservedLayer = false;
        TArray<UEdGraph*> ChildGraphs;
        ChildBlueprint->GetAllGraphs(ChildGraphs);
        for (UEdGraph* Graph : ChildGraphs)
        {
            if (Graph == nullptr) continue;
            bOwnsLocomotionOverride |= Graph->GetFName() == TEXT("Locomotion");
            bCopiedPreservedLayer |= Graph->GetFName() == TEXT("PreservedLayer");
        }
        TestTrue(TEXT("Child owns the declared Locomotion override graph"), bOwnsLocomotionOverride);
        TestFalse(TEXT("Child does not copy an undeclared parent Layer graph"), bCopiedPreservedLayer);
        TestNotNull(
            TEXT("Child GeneratedClass inherits the undeclared parent Layer function"),
            ChildBlueprint->GeneratedClass->FindFunctionByName(TEXT("PreservedLayer")));
    }

    FSekiroAnimBlueprintIR LinkedHostIR = MakeFactoryIR(Sequence);
    LinkedHostIR.SchemaVersion = 3;
    LinkedHostIR.ImplementedInterfaces.Add(InterfaceClassPath);
    FSekiroAnimIRGraph* HostRootGraph =
        FindGraph(LinkedHostIR, LinkedHostIR.Layers[0].RootGraphId);
    TestNotNull(TEXT("Linked node host root graph exists"), HostRootGraph);
    if (HostRootGraph == nullptr) return true;

    FSekiroAnimIRNode& LinkedGraphIR = HostRootGraph->Nodes.AddDefaulted_GetRef();
    LinkedGraphIR.Id = TEXT("Node.LinkedGraph");
    LinkedGraphIR.NodeType = SekiroAnimGraphIRNames::LinkedAnimGraphNode;
    LinkedGraphIR.SourceLocation = LinkedHostIR.SourceLocation;
    FSekiroAnimIRPin& LinkedGraphPose = LinkedGraphIR.Pins.AddDefaulted_GetRef();
    LinkedGraphPose.Name = TEXT("Pose");
    LinkedGraphPose.Direction = ESekiroAnimIRPinDirection::Output;
    LinkedGraphPose.DataType = SekiroAnimGraphIRNames::PoseData;
    LinkedGraphPose.bAllowMultipleConnections = true;
    FSekiroAnimIRProperty& LinkedGraphClass =
        LinkedGraphIR.Properties.AddDefaulted_GetRef();
    LinkedGraphClass.Name = TEXT("InstanceClass");
    LinkedGraphClass.Value.Type = ESekiroAnimIRValueType::SoftClassPath;
    LinkedGraphClass.Value.SoftClassPathValue =
        FSoftClassPath(ImplementationBlueprint->GeneratedClass->GetPathName());

    FSekiroAnimIRNode& LinkedLayerIR = HostRootGraph->Nodes.AddDefaulted_GetRef();
    LinkedLayerIR.Id = TEXT("Node.LinkedLayer");
    LinkedLayerIR.NodeType = SekiroAnimGraphIRNames::LinkedAnimLayerNode;
    LinkedLayerIR.SourceLocation = LinkedHostIR.SourceLocation;
    FSekiroAnimIRPin& LinkedLayerInput = LinkedLayerIR.Pins.AddDefaulted_GetRef();
    LinkedLayerInput.Name = TEXT("SourcePose");
    LinkedLayerInput.Direction = ESekiroAnimIRPinDirection::Input;
    LinkedLayerInput.DataType = SekiroAnimGraphIRNames::PoseData;
    FSekiroAnimIRPin& LinkedLayerPose = LinkedLayerIR.Pins.AddDefaulted_GetRef();
    LinkedLayerPose.Name = TEXT("Pose");
    LinkedLayerPose.Direction = ESekiroAnimIRPinDirection::Output;
    LinkedLayerPose.DataType = SekiroAnimGraphIRNames::PoseData;
    LinkedLayerPose.bAllowMultipleConnections = true;
    FSekiroAnimIRProperty& LinkedLayerName =
        LinkedLayerIR.Properties.AddDefaulted_GetRef();
    LinkedLayerName.Name = TEXT("LayerName");
    LinkedLayerName.Value.Type = ESekiroAnimIRValueType::Name;
    LinkedLayerName.Value.NameValue = TEXT("Locomotion");
    FSekiroAnimIRProperty& LinkedLayerInterface =
        LinkedLayerIR.Properties.AddDefaulted_GetRef();
    LinkedLayerInterface.Name = TEXT("InterfaceClass");
    LinkedLayerInterface.Value.Type = ESekiroAnimIRValueType::SoftClassPath;
    LinkedLayerInterface.Value.SoftClassPathValue = InterfaceClassPath;
    FSekiroAnimIRProperty& LinkedLayerClass =
        LinkedLayerIR.Properties.AddDefaulted_GetRef();
    LinkedLayerClass.Name = TEXT("InstanceClass");
    LinkedLayerClass.Value.Type = ESekiroAnimIRValueType::SoftClassPath;
    LinkedLayerClass.Value.SoftClassPathValue =
        FSoftClassPath(ImplementationBlueprint->GeneratedClass->GetPathName());

    TArray<FSekiroAnimIRDiagnostic> LinkedHostDiagnostics;
    UAnimBlueprint* LinkedHost =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            LinkedHostIR,
            LinkedHostDiagnostics);
    TestNotNull(TEXT("Linked node host AnimBlueprint compiles"), LinkedHost);
    TestEqual(TEXT("Linked node host has no diagnostics"), LinkedHostDiagnostics.Num(), 0);
    if (LinkedHost != nullptr)
    {
        UAnimationGraph* NativeHostGraph = FindMainGraph(LinkedHost);
        UAnimGraphNode_LinkedAnimGraph* LinkedGraphNode =
            FindFirstNode<UAnimGraphNode_LinkedAnimGraph>(NativeHostGraph);
        UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode =
            FindFirstNode<UAnimGraphNode_LinkedAnimLayer>(NativeHostGraph);
        TestNotNull(TEXT("Native Linked Anim Graph node exists"), LinkedGraphNode);
        TestNotNull(TEXT("Native Linked Anim Layer node exists"), LinkedLayerNode);
        TestNotNull(
            TEXT("Linked Anim Graph exposes reflected Pose output"),
            LinkedGraphNode != nullptr
                ? LinkedGraphNode->FindPin(TEXT("Pose"), EGPD_Output)
                : nullptr);
        TestNotNull(
            TEXT("Linked Anim Layer exposes reflected SourcePose input"),
            LinkedLayerNode != nullptr
                ? LinkedLayerNode->FindPin(TEXT("SourcePose"), EGPD_Input)
                : nullptr);
        TestNotNull(
            TEXT("Linked Anim Layer exposes reflected Pose output"),
            LinkedLayerNode != nullptr
                ? LinkedLayerNode->FindPin(TEXT("Pose"), EGPD_Output)
                : nullptr);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryLinkedAnimLayerChainTest,
    "Sekiro.AnimGraphIR.Factory.LinkedAnimLayerChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同一 AnimBlueprint 实现双层 ALI 时，主 AnimGraph 可串联两个无 InstanceClass 的 Linked Anim Layer。
 * 测试覆盖接口生成、宿主 ImplementNewInterface、节点重建、Pose Pin 映射、Schema 连接和最终原生编译，
 * 只创建 transient UObject，不保存资产，也不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryLinkedAnimLayerChainTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR InterfaceIR =
        SekiroAnimGraphIRTests::MakeMinimalIR();
    InterfaceIR.SchemaVersion = 3;
    InterfaceIR.SourceModule = TEXT("Tests.LinkedAnimLayerChain.Interface");
    InterfaceIR.BlueprintKind =
        ESekiroAnimIRBlueprintKind::AnimationLayerInterface;
    InterfaceIR.ParentAnimInstanceClass.Reset();
    InterfaceIR.TargetSkeleton.Reset();
    InterfaceIR.Variables.Reset();
    InterfaceIR.Layers.Reset();
    InterfaceIR.Layers.Add(
        MakePoseInterfaceLayer(
            TEXT("BaseLayer"),
            InterfaceIR.SourceLocation,
            0));
    InterfaceIR.Layers.Add(
        MakePoseInterfaceLayer(
            TEXT("OverlayLayer"),
            InterfaceIR.SourceLocation,
            1));

    TArray<FSekiroAnimIRDiagnostic> InterfaceDiagnostics;
    UAnimBlueprint* InterfaceBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            InterfaceIR,
            InterfaceDiagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : InterfaceDiagnostics)
    {
        AddInfo(
            FString::Printf(
                TEXT("%s: %s"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message));
    }
    TestNotNull(TEXT("Chained Layer interface is generated"), InterfaceBlueprint);
    TestEqual(
        TEXT("Chained Layer interface has no diagnostics"),
        InterfaceDiagnostics.Num(),
        0);
    if (InterfaceBlueprint == nullptr
        || InterfaceBlueprint->GeneratedClass == nullptr)
    {
        return true;
    }
    for (const FName LayerName : { FName(TEXT("BaseLayer")), FName(TEXT("OverlayLayer")) })
    {
        UFunction* LayerFunction =
            InterfaceBlueprint->GeneratedClass->FindFunctionByName(LayerName);
        TestNotNull(
            *FString::Printf(
                TEXT("Interface exposes %s"),
                *LayerName.ToString()),
            LayerFunction);
        TestNotNull(
            *FString::Printf(
                TEXT("%s preserves its SourcePose interface parameter"),
                *LayerName.ToString()),
            LayerFunction != nullptr
                ? FindFProperty<FProperty>(
                    LayerFunction,
                    TEXT("SourcePose"))
                : nullptr);
    }

    const FSoftClassPath InterfaceClassPath(
        InterfaceBlueprint->GeneratedClass->GetPathName());
    FSekiroAnimBlueprintIR HostIR = MakeFactoryIR(Sequence);
    HostIR.SchemaVersion = 3;
    HostIR.SourceModule = TEXT("Tests.LinkedAnimLayerChain.Host");
    HostIR.ImplementedInterfaces.Add(InterfaceClassPath);
    FSekiroAnimIRGraph* HostGraph =
        FindGraph(HostIR, HostIR.Layers[0].RootGraphId);
    TestNotNull(TEXT("Chained Layer host graph exists"), HostGraph);
    if (HostGraph == nullptr) return true;

    AddSelfLinkedAnimLayerNode(
        *HostGraph,
        TEXT("Node.BaseLayer"),
        TEXT("BaseLayer"),
        InterfaceClassPath,
        HostIR.SourceLocation);
    AddSelfLinkedAnimLayerNode(
        *HostGraph,
        TEXT("Node.OverlayLayer"),
        TEXT("OverlayLayer"),
        InterfaceClassPath,
        HostIR.SourceLocation);
    FSekiroAnimIRLink& ChainLink = HostGraph->Links.AddDefaulted_GetRef();
    ChainLink.Id = TEXT("Link.BaseLayerToOverlayLayer");
    ChainLink.Source.NodeId = TEXT("Node.BaseLayer");
    ChainLink.Source.PinName = TEXT("Pose");
    ChainLink.Target.NodeId = TEXT("Node.OverlayLayer");
    ChainLink.Target.PinName = TEXT("SourcePose");
    ChainLink.DeclarationOrder = HostGraph->Links.Num();
    ChainLink.SourceLocation = HostIR.SourceLocation;

    TArray<FSekiroAnimIRDiagnostic> HostDiagnostics;
    UAnimBlueprint* HostBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            HostIR,
            HostDiagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : HostDiagnostics)
    {
        AddInfo(
            FString::Printf(
                TEXT("%s: %s"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message));
    }
    TestNotNull(
        TEXT("Host with chained self Linked Anim Layers compiles"),
        HostBlueprint);
    TestEqual(
        TEXT("Chained self Linked Anim Layers have no diagnostics"),
        HostDiagnostics.Num(),
        0);
    if (HostBlueprint == nullptr) return true;

    UAnimationGraph* NativeGraph = FindMainGraph(HostBlueprint);
    TestEqual(
        TEXT("Host contains both Linked Anim Layer nodes"),
        CountNodes<UAnimGraphNode_LinkedAnimLayer>(NativeGraph),
        2);
    TArray<UAnimGraphNode_LinkedAnimLayer*> LinkedLayerNodes;
    if (NativeGraph != nullptr)
    {
        NativeGraph->GetNodesOfClass(LinkedLayerNodes);
    }
    bool bFoundConnectedSourcePose = false;
    for (UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode : LinkedLayerNodes)
    {
        if (LinkedLayerNode == nullptr
            || LinkedLayerNode->Node.Layer != TEXT("OverlayLayer"))
        {
            continue;
        }
        UEdGraphPin* SourcePosePin =
            LinkedLayerNode->FindPin(TEXT("SourcePose"), EGPD_Input);
        bFoundConnectedSourcePose =
            SourcePosePin != nullptr
            && SourcePosePin->LinkedTo.Num() == 1;
        break;
    }
    TestTrue(
        TEXT("OverlayLayer SourcePose is present and connected"),
        bFoundConnectedSourcePose);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryRealLuaAnimationLayerChainTest,
    "Sekiro.AnimGraphIR.Factory.RealLuaAnimationLayerChain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证真实 Lua ALI 与 ABP 模块经过 Importer 后，可使用本轮原生编译的接口 GeneratedClass 完成 Linked Anim Layer 生成。
 * 测试将 ABP IR 中指向磁盘接口资产的引用改为 transient ALI，隔离旧资产缓存并完整覆盖同一轮导入、生成和链接；
 * 只读取项目 Lua，不保存或修改 Content，也不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryRealLuaAnimationLayerChainTest::RunTest(
    const FString& Parameters)
{
    FSekiroAnimBlueprintIR InterfaceIR;
    TArray<FSekiroAnimIRDiagnostic> InterfaceImportDiagnostics;
    const bool bInterfaceImported = USekiroAnimGraphIRLibrary::CompileLuaModule(
        TEXT("Animation.Sekiro.ALI_Sekiro"),
        InterfaceIR,
        InterfaceImportDiagnostics);
    TestTrue(TEXT("Real Lua ALI imports"), bInterfaceImported);
    TestEqual(
        TEXT("Real Lua ALI import has no diagnostics"),
        InterfaceImportDiagnostics.Num(),
        0);
    if (!bInterfaceImported) return true;

    TArray<FSekiroAnimIRDiagnostic> TransientInterfaceDiagnostics;
    UAnimBlueprint* TransientInterface =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            InterfaceIR,
            TransientInterfaceDiagnostics);
    TestNotNull(
        TEXT("Real Lua ALI IR creates a transient native interface"),
        TransientInterface);
    TestEqual(
        TEXT("Real Lua transient ALI has no diagnostics"),
        TransientInterfaceDiagnostics.Num(),
        0);
    if (TransientInterface == nullptr || TransientInterface->GeneratedClass == nullptr)
    {
        return true;
    }

    for (const FSekiroAnimIRLayer& Layer : InterfaceIR.Layers)
    {
        if (Layer.Parameters.IsEmpty()) continue;
        const FName FunctionName =
            Layer.FunctionName.IsNone() ? FName(*Layer.Name) : Layer.FunctionName;
        UFunction* InterfaceFunction =
            TransientInterface->GeneratedClass->FindFunctionByName(FunctionName);
        TestNotNull(
            *FString::Printf(
                TEXT("Transient ALI exposes %s"),
                *FunctionName.ToString()),
            InterfaceFunction);
        for (const FSekiroAnimIRFunctionParameter& Parameter : Layer.Parameters)
        {
            if (!Parameter.bIsPose || InterfaceFunction == nullptr) continue;
            TestNotNull(
                *FString::Printf(
                    TEXT("Transient %s exposes Pose parameter %s"),
                    *FunctionName.ToString(),
                    *Parameter.Name.ToString()),
                FindFProperty<FProperty>(
                    InterfaceFunction,
                    Parameter.Name));
        }
    }

    FSekiroAnimBlueprintIR HostIR;
    TArray<FSekiroAnimIRDiagnostic> HostImportDiagnostics;
    const bool bHostImported = USekiroAnimGraphIRLibrary::CompileLuaModule(
        TEXT("Animation.Sekiro.ABP_Sekiro"),
        HostIR,
        HostImportDiagnostics);
    TestTrue(TEXT("Real Lua ABP imports"), bHostImported);
    TestEqual(
        TEXT("Real Lua ABP import has no diagnostics"),
        HostImportDiagnostics.Num(),
        0);
    if (!bHostImported || HostIR.ImplementedInterfaces.IsEmpty()) return true;

    const FSoftClassPath PersistedInterfacePath =
        HostIR.ImplementedInterfaces[0];
    const FSoftClassPath TransientInterfacePath(
        TransientInterface->GeneratedClass->GetPathName());
    for (FSoftClassPath& InterfacePath : HostIR.ImplementedInterfaces)
    {
        if (InterfacePath == PersistedInterfacePath)
        {
            InterfacePath = TransientInterfacePath;
        }
    }
    for (FSekiroAnimIRLayer& Layer : HostIR.Layers)
    {
        if (Layer.InterfaceClass == PersistedInterfacePath)
        {
            Layer.InterfaceClass = TransientInterfacePath;
        }
        for (FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            for (FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                for (FSekiroAnimIRProperty& Property : Node.Properties)
                {
                    if (Property.Name == TEXT("InterfaceClass")
                        && Property.Value.SoftClassPathValue
                            == PersistedInterfacePath)
                    {
                        Property.Value.SoftClassPathValue =
                            TransientInterfacePath;
                    }
                }
            }
        }
    }

    TArray<FSekiroAnimIRDiagnostic> HostDiagnostics;
    UAnimBlueprint* HostBlueprint =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            HostIR,
            HostDiagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : HostDiagnostics)
    {
        AddInfo(
            FString::Printf(
                TEXT("%s: %s"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message));
    }
    TestNotNull(
        TEXT("Real Lua ABP compiles against freshly generated ALI"),
        HostBlueprint);
    TestEqual(
        TEXT("Real Lua ABP Factory has no diagnostics"),
        HostDiagnostics.Num(),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryPersistedFreshLayerHostTest,
    "Sekiro.AnimGraphIR.Factory.PersistedFreshLayerHost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证全新生成的 ALI 与 Host 保存后，Host package 经卸载和重载仍保留有效的 Linked Anim Layer Pose 输入。
 * 测试仅在 /Game/Developers/SekiroAnimBlueprintExtTests 下创建唯一临时 package，断言结束前删除对应磁盘文件；
 * 不修改项目正式资产、不启动 PIE，且只在游戏线程执行同步保存、卸载、重载和原生编译。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryPersistedFreshLayerHostTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    TStrongObjectPtr<UAnimSequence> SequenceGuard(
        LoadObject<UAnimSequence>(
            nullptr,
            TEXT("/Game/Characters/Sekiro/Animations/Anim_Sekiro_a000_000000.Anim_Sekiro_a000_000000")));
    UAnimSequence* Sequence = SequenceGuard.Get();
    USkeleton* Skeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
    TestNotNull(TEXT("Fresh persisted test loads a disk Sequence"), Sequence);
    TestNotNull(TEXT("Fresh persisted disk Sequence has a Skeleton"), Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    const FString UniqueSuffix =
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString InterfacePackageName = FString::Printf(
        TEXT("/Game/Developers/SekiroAnimBlueprintExtTests/ALI_%s"),
        *UniqueSuffix);
    const FString HostPackageName = FString::Printf(
        TEXT("/Game/Developers/SekiroAnimBlueprintExtTests/ABP_%s"),
        *UniqueSuffix);
    const FString InterfaceAssetName =
        FPackageName::GetLongPackageAssetName(InterfacePackageName);
    const FString HostAssetName =
        FPackageName::GetLongPackageAssetName(HostPackageName);
    const FString InterfaceFilename =
        FPackageName::LongPackageNameToFilename(
            InterfacePackageName,
            FPackageName::GetAssetPackageExtension());
    const FString HostFilename =
        FPackageName::LongPackageNameToFilename(
            HostPackageName,
            FPackageName::GetAssetPackageExtension());

    FSekiroAnimBlueprintIR InterfaceIR =
        SekiroAnimGraphIRTests::MakeMinimalIR();
    InterfaceIR.SchemaVersion = 3;
    InterfaceIR.SourceModule = TEXT("Tests.PersistedFreshLayerHost.Interface");
    InterfaceIR.BlueprintKind =
        ESekiroAnimIRBlueprintKind::AnimationLayerInterface;
    InterfaceIR.ParentAnimInstanceClass.Reset();
    InterfaceIR.TargetSkeleton.Reset();
    InterfaceIR.Variables.Reset();
    InterfaceIR.Layers.Reset();
    InterfaceIR.Layers.Add(
        MakePoseInterfaceLayer(
            TEXT("BaseLayer"),
            InterfaceIR.SourceLocation,
            0));
    InterfaceIR.Layers.Add(
        MakePoseInterfaceLayer(
            TEXT("OverlayLayer"),
            InterfaceIR.SourceLocation,
            1));

    TArray<FSekiroAnimIRDiagnostic> InterfaceDiagnostics;
    UAnimBlueprint* TransientInterface =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            InterfaceIR,
            InterfaceDiagnostics);
    TestNotNull(TEXT("Fresh persisted test creates transient ALI"), TransientInterface);
    TestEqual(
        TEXT("Fresh persisted transient ALI has no diagnostics"),
        InterfaceDiagnostics.Num(),
        0);
    if (TransientInterface == nullptr) return true;
    TStrongObjectPtr<UAnimBlueprint> TransientInterfaceGuard(
        TransientInterface);

    UPackage* InterfacePackage = CreatePackage(*InterfacePackageName);
    UAnimBlueprint* PersistedInterface = Cast<UAnimBlueprint>(
        StaticDuplicateObject(
            TransientInterface,
            InterfacePackage,
            FName(*InterfaceAssetName),
            RF_Public | RF_Standalone | RF_Transactional));
    TestNotNull(TEXT("Fresh ALI duplicates into a saved package"), PersistedInterface);
    if (PersistedInterface == nullptr) return true;
    TestEqual(
        TEXT("Fresh packaged ALI duplicates UpToDate"),
        PersistedInterface->Status,
        BS_UpToDate);
    UFunction* PersistedSkeletonFunction =
        PersistedInterface->SkeletonGeneratedClass != nullptr
        ? PersistedInterface->SkeletonGeneratedClass->FindFunctionByName(
            TEXT("OverlayLayer"))
        : nullptr;
    TestNotNull(
        TEXT("Fresh packaged ALI Skeleton exposes OverlayLayer"),
        PersistedSkeletonFunction);
    TestNotNull(
        TEXT("Fresh packaged ALI Skeleton exposes OverlayLayer SourcePose"),
        PersistedSkeletonFunction != nullptr
            ? FindFProperty<FProperty>(
                PersistedSkeletonFunction,
                TEXT("SourcePose"))
            : nullptr);

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bInterfaceSaved = UPackage::SavePackage(
        InterfacePackage,
        PersistedInterface,
        *InterfaceFilename,
        SaveArgs);
    TestTrue(TEXT("Fresh ALI package saves"), bInterfaceSaved);
    if (!bInterfaceSaved
        || PersistedInterface->GeneratedClass == nullptr)
    {
        IFileManager::Get().Delete(*InterfaceFilename, false, true);
        return true;
    }

    const FSoftClassPath InterfaceClassPath(
        PersistedInterface->GeneratedClass->GetPathName());
    FSekiroAnimBlueprintIR HostIR = MakeFactoryIR(Sequence);
    HostIR.SchemaVersion = 3;
    HostIR.SourceModule = TEXT("Tests.PersistedFreshLayerHost.Host");
    HostIR.TargetSkeleton = FSoftObjectPath(Skeleton);
    HostIR.ImplementedInterfaces.Add(InterfaceClassPath);
    FSekiroAnimIRGraph* HostGraph =
        FindGraph(HostIR, HostIR.Layers[0].RootGraphId);
    TestNotNull(TEXT("Fresh persisted Host graph exists"), HostGraph);
    if (HostGraph == nullptr)
    {
        IFileManager::Get().Delete(*InterfaceFilename, false, true);
        return true;
    }

    AddSelfLinkedAnimLayerNode(
        *HostGraph,
        TEXT("Node.BaseLayer"),
        TEXT("BaseLayer"),
        InterfaceClassPath,
        HostIR.SourceLocation);
    AddSelfLinkedAnimLayerNode(
        *HostGraph,
        TEXT("Node.OverlayLayer"),
        TEXT("OverlayLayer"),
        InterfaceClassPath,
        HostIR.SourceLocation);
    FSekiroAnimIRLink& ChainLink = HostGraph->Links.AddDefaulted_GetRef();
    ChainLink.Id = TEXT("Link.BaseLayerToOverlayLayer");
    ChainLink.Source.NodeId = TEXT("Node.BaseLayer");
    ChainLink.Source.PinName = TEXT("Pose");
    ChainLink.Target.NodeId = TEXT("Node.OverlayLayer");
    ChainLink.Target.PinName = TEXT("SourcePose");
    ChainLink.DeclarationOrder = HostGraph->Links.Num();
    ChainLink.SourceLocation = HostIR.SourceLocation;

    TArray<FSekiroAnimIRDiagnostic> HostDiagnostics;
    UAnimBlueprint* TransientHost =
        USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
            HostIR,
            HostDiagnostics);
    TestNotNull(TEXT("Fresh persisted test creates transient Host"), TransientHost);
    TestEqual(
        TEXT("Fresh persisted transient Host has no diagnostics"),
        HostDiagnostics.Num(),
        0);
    if (TransientHost == nullptr)
    {
        IFileManager::Get().Delete(*InterfaceFilename, false, true);
        return true;
    }

    UPackage* HostPackage = CreatePackage(*HostPackageName);
    UAnimBlueprint* PersistedHost = Cast<UAnimBlueprint>(
        StaticDuplicateObject(
            TransientHost,
            HostPackage,
            FName(*HostAssetName),
            RF_Public | RF_Standalone | RF_Transactional));
    TestNotNull(TEXT("Fresh Host duplicates into a saved package"), PersistedHost);
    if (PersistedHost == nullptr)
    {
        IFileManager::Get().Delete(*InterfaceFilename, false, true);
        return true;
    }
    FKismetEditorUtilities::CompileBlueprint(
        PersistedHost,
        EBlueprintCompileOptions::SkipGarbageCollection);
    TestEqual(
        TEXT("Fresh packaged Host compiles before save"),
        PersistedHost->Status,
        BS_UpToDate);
    const bool bHostSaved = UPackage::SavePackage(
        HostPackage,
        PersistedHost,
        *HostFilename,
        SaveArgs);
    TestTrue(TEXT("Fresh Host package saves"), bHostSaved);

    TransientHost->MarkAsGarbage();
    PersistedHost = nullptr;
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TArray<UPackage*> PackagesToUnload;
    PackagesToUnload.Add(HostPackage);
    const bool bHostUnloaded = UPackageTools::UnloadPackages(PackagesToUnload);
    TestTrue(TEXT("Fresh Host package unloads"), bHostUnloaded);

    UAnimBlueprint* ReloadedHost = bHostSaved && bHostUnloaded
        ? LoadObject<UAnimBlueprint>(
            nullptr,
            *FString::Printf(
                TEXT("%s.%s"),
                *HostPackageName,
                *HostAssetName))
        : nullptr;
    TestNotNull(TEXT("Fresh Host reloads from disk"), ReloadedHost);
    bool bReloadedPinsValid = ReloadedHost != nullptr;
    if (ReloadedHost != nullptr)
    {
        UAnimationGraph* ReloadedGraph = FindMainGraph(ReloadedHost);
        TArray<UAnimGraphNode_LinkedAnimLayer*> LinkedLayerNodes;
        if (ReloadedGraph != nullptr)
        {
            ReloadedGraph->GetNodesOfClass(LinkedLayerNodes);
        }
        TestEqual(
            TEXT("Reloaded Host retains both Linked Anim Layer nodes"),
            LinkedLayerNodes.Num(),
            2);
        for (UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode : LinkedLayerNodes)
        {
            UEdGraphPin* SourcePosePin = LinkedLayerNode != nullptr
                ? LinkedLayerNode->FindPin(TEXT("SourcePose"), EGPD_Input)
                : nullptr;
            const bool bPinValid =
                SourcePosePin != nullptr && !SourcePosePin->bOrphanedPin;
            TestTrue(
                *FString::Printf(
                    TEXT("Reloaded %s SourcePose remains non-orphaned"),
                    LinkedLayerNode != nullptr
                        ? *LinkedLayerNode->Node.Layer.ToString()
                        : TEXT("<null>")),
                bPinValid);
            bReloadedPinsValid &= bPinValid;
        }
        if (bReloadedPinsValid)
        {
            FKismetEditorUtilities::CompileBlueprint(
                ReloadedHost,
                EBlueprintCompileOptions::SkipGarbageCollection);
            TestEqual(
                TEXT("Reloaded fresh Host compiles UpToDate"),
                ReloadedHost->Status,
                BS_UpToDate);
        }
    }

    ReloadedHost = nullptr;
    if (UPackage* ReloadedHostPackage =
        FindPackage(nullptr, *HostPackageName))
    {
        TArray<UPackage*> HostPackagesToUnload;
        HostPackagesToUnload.Add(ReloadedHostPackage);
        UPackageTools::UnloadPackages(HostPackagesToUnload);
    }
    PersistedInterface = nullptr;
    TransientInterface = nullptr;
    TransientInterfaceGuard.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (UPackage* LoadedInterfacePackage =
        FindPackage(nullptr, *InterfacePackageName))
    {
        TArray<UPackage*> InterfacePackagesToUnload;
        InterfacePackagesToUnload.Add(LoadedInterfacePackage);
        UPackageTools::UnloadPackages(InterfacePackagesToUnload);
    }
    IFileManager::Get().Delete(*HostFilename, false, true);
    IFileManager::Get().Delete(*InterfaceFilename, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryPersistedAnimationLayerSignatureTest,
    "Sekiro.AnimGraphIR.Factory.PersistedAnimationLayerSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证磁盘 ALI_Sekiro 的 GeneratedClass 与当前 Lua IR 保持完全一致的 Layer Pose 参数签名。
 * 测试只读取 Lua 与已保存资产，不生成、修改或保存 Content，也不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryPersistedAnimationLayerSignatureTest::RunTest(
    const FString& Parameters)
{
    FSekiroAnimBlueprintIR InterfaceIR;
    TArray<FSekiroAnimIRDiagnostic> ImportDiagnostics;
    const bool bImported = USekiroAnimGraphIRLibrary::CompileLuaModule(
        TEXT("Animation.Sekiro.ALI_Sekiro"),
        InterfaceIR,
        ImportDiagnostics);
    TestTrue(TEXT("Persisted signature Lua ALI imports"), bImported);
    TestEqual(
        TEXT("Persisted signature Lua ALI has no diagnostics"),
        ImportDiagnostics.Num(),
        0);
    if (!bImported) return true;

    UClass* InterfaceClass = FSoftClassPath(
        TEXT("/Game/Characters/Sekiro/ALI_Sekiro.ALI_Sekiro_C"))
        .TryLoadClass<UAnimLayerInterface>();
    TestNotNull(TEXT("Persisted ALI_Sekiro class loads"), InterfaceClass);
    if (InterfaceClass == nullptr) return true;

    UBlueprint* InterfaceBlueprint =
        UBlueprint::GetBlueprintFromClass(InterfaceClass);
    UClass* InterfaceSkeletonClass = InterfaceBlueprint != nullptr
        ? InterfaceBlueprint->SkeletonGeneratedClass
        : nullptr;
    TestNotNull(
        TEXT("Persisted ALI_Sekiro SkeletonGeneratedClass loads"),
        InterfaceSkeletonClass);
    IAnimClassInterface* NativeAnimClass =
        IAnimClassInterface::GetFromClass(InterfaceClass);
    TestNotNull(
        TEXT("Persisted ALI_Sekiro exposes native AnimClass metadata"),
        NativeAnimClass);
    for (const FSekiroAnimIRLayer& Layer : InterfaceIR.Layers)
    {
        const FName FunctionName =
            Layer.FunctionName.IsNone() ? FName(*Layer.Name) : Layer.FunctionName;
        UFunction* Function = InterfaceClass->FindFunctionByName(FunctionName);
        UFunction* SkeletonFunction = InterfaceSkeletonClass != nullptr
            ? InterfaceSkeletonClass->FindFunctionByName(FunctionName)
            : nullptr;
        TestNotNull(
            *FString::Printf(
                TEXT("Persisted ALI exposes %s"),
                *FunctionName.ToString()),
            Function);
        TestNotNull(
            *FString::Printf(
                TEXT("Persisted ALI SkeletonClass exposes %s"),
                *FunctionName.ToString()),
            SkeletonFunction);
        const FAnimBlueprintFunction* AnimFunction =
            NativeAnimClass != nullptr
                ? IAnimClassInterface::FindAnimBlueprintFunction(
                    NativeAnimClass,
                    FunctionName)
                : nullptr;
        TestNotNull(
            *FString::Printf(
                TEXT("Persisted AnimClass metadata exposes %s"),
                *FunctionName.ToString()),
            AnimFunction);
        for (const FSekiroAnimIRFunctionParameter& Parameter : Layer.Parameters)
        {
            if (Function == nullptr) continue;
            FProperty* NativeParameter = FindFProperty<FProperty>(
                Function,
                Parameter.Name);
            FProperty* SkeletonParameter = SkeletonFunction != nullptr
                ? FindFProperty<FProperty>(
                    SkeletonFunction,
                    Parameter.Name)
                : nullptr;
            TestNotNull(
                *FString::Printf(
                    TEXT("Persisted %s exposes parameter %s"),
                    *FunctionName.ToString(),
                    *Parameter.Name.ToString()),
                NativeParameter);
            TestNotNull(
                *FString::Printf(
                    TEXT("Persisted SkeletonClass %s exposes parameter %s"),
                    *FunctionName.ToString(),
                    *Parameter.Name.ToString()),
                SkeletonParameter);
            if (NativeParameter != nullptr)
            {
                AddInfo(
                    FString::Printf(
                        TEXT("%s.%s type=%s flags=0x%llx"),
                        *FunctionName.ToString(),
                        *Parameter.Name.ToString(),
                        *NativeParameter->GetCPPType(),
                        NativeParameter->GetPropertyFlags()));
            }
            if (Parameter.bIsPose && AnimFunction != nullptr)
            {
                TestTrue(
                    *FString::Printf(
                        TEXT("Persisted AnimClass %s records input pose %s"),
                        *FunctionName.ToString(),
                        *Parameter.Name.ToString()),
                    AnimFunction->InputPoseNames.Contains(Parameter.Name));
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintFactoryRealAssetLayerMigrationTest,
    "Sekiro.AnimGraphIR.Factory.RealAssetLayerMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证旧主动画蓝图的完整 UObject 副本可原地迁移到当前 Lua Animation Layer 架构。
 * 测试只读取项目资产，将目标及其 Graph 复制到 transient package 后执行无保存编译；
 * 不修改、替换或保存 /Game 下的原资产，也不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryRealAssetLayerMigrationTest::RunTest(
    const FString& Parameters)
{
    UAnimBlueprint* SourceBlueprint = LoadObject<UAnimBlueprint>(
        nullptr,
        TEXT("/Game/Characters/Sekiro/ABP_Sekiro.ABP_Sekiro"));
    TestNotNull(TEXT("Legacy ABP_Sekiro source asset loads"), SourceBlueprint);
    if (SourceBlueprint == nullptr) return true;

    const FName DuplicateName = MakeUniqueObjectName(
        GetTransientPackage(),
        UAnimBlueprint::StaticClass(),
        TEXT("ABP_Sekiro_LayerMigration"));
    UAnimBlueprint* MigrationBlueprint = Cast<UAnimBlueprint>(
        StaticDuplicateObject(
            SourceBlueprint,
            GetTransientPackage(),
            DuplicateName,
            RF_Transient | RF_Transactional));
    TestNotNull(
        TEXT("Legacy ABP_Sekiro is duplicated into transient package"),
        MigrationBlueprint);
    if (MigrationBlueprint == nullptr) return true;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bMigrated =
        USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
            MigrationBlueprint,
            false,
            Diagnostics);
    for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
    {
        AddInfo(
            FString::Printf(
                TEXT("%s: %s"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message));
    }
    TestTrue(
        TEXT("Legacy ABP_Sekiro duplicate migrates to current Layer architecture"),
        bMigrated);
    TestEqual(
        TEXT("Legacy ABP_Sekiro migration has no diagnostics"),
        Diagnostics.Num(),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintIRReaderRoundTripTest,
    "Sekiro.AnimGraphIR.Reader.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证标准 AnimBlueprint 的主 Pose Graph、状态机、Transition、实际位置和重复读取确定性，
 * 同时确认只读 API 不改变 package Dirty 标记或 Graph 节点集合。本测试只创建 transient 资产，不保存文件或启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintIRReaderRoundTripTest::RunTest(const FString& Parameters)
{
    USkeleton* Skeleton = SekiroAnimBlueprintFactoryTests::LoadTestSkeleton();
    UAnimSequence* Sequence = SekiroAnimBlueprintFactoryTests::CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr)
    {
        AddError(TEXT("Reader test assets are unavailable."));
        return true;
    }

    FSekiroAnimBlueprintIR SourceIR = SekiroAnimBlueprintFactoryTests::MakeFactoryIR(Sequence);
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* Blueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        SourceIR,
        Diagnostics);
    TestNotNull(TEXT("Reader test Blueprint is created"), Blueprint);
    if (Blueprint == nullptr) return true;

    UAnimationGraph* MainGraph = SekiroAnimBlueprintFactoryTests::FindMainGraph(Blueprint);
    const int32 NodeCountBefore = MainGraph != nullptr ? MainGraph->Nodes.Num() : INDEX_NONE;
    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    FSekiroAnimBlueprintIR FirstRead;
    Diagnostics.Reset();
    TestTrue(
        TEXT("First strict read succeeds"),
        USekiroAnimBlueprintFactoryLibrary::ReadAnimBlueprintToIR(
            Blueprint,
            FirstRead,
            Diagnostics));
    TestEqual(TEXT("First read has no diagnostics"), Diagnostics.Num(), 0);
    TestEqual(TEXT("Read preserves package Dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(
        TEXT("Read preserves main Graph node count"),
        MainGraph != nullptr ? MainGraph->Nodes.Num() : INDEX_NONE,
        NodeCountBefore);

    FSekiroAnimBlueprintIR SecondRead;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Second strict read succeeds"),
        USekiroAnimBlueprintFactoryLibrary::ReadAnimBlueprintToIR(
            Blueprint,
            SecondRead,
            Diagnostics));
    TestTrue(
        TEXT("Repeated reads are structurally deterministic"),
        FSekiroAnimBlueprintIR::StaticStruct()->CompareScriptStruct(
            &FirstRead,
            &SecondRead,
            0));

    FString FirstHash;
    FString SecondHash;
    Diagnostics.Reset();
    TestTrue(
        TEXT("First Canonical IR hash succeeds"),
        USekiroAnimBlueprintFactoryLibrary::ComputeCanonicalIRHash(
            FirstRead,
            FirstHash,
            Diagnostics));
    Diagnostics.Reset();
    TestTrue(
        TEXT("Repeated Canonical IR hash succeeds"),
        USekiroAnimBlueprintFactoryLibrary::ComputeCanonicalIRHash(
            SecondRead,
            SecondHash,
            Diagnostics));
    TestEqual(TEXT("Canonical IR hash is deterministic"), FirstHash, SecondHash);

    FSekiroAnimBlueprintIR MovedIR = FirstRead;
    if (!MovedIR.Layers.IsEmpty()
        && !MovedIR.Layers[0].Graphs.IsEmpty()
        && !MovedIR.Layers[0].Graphs[0].Layout.Positions.IsEmpty())
    {
        MovedIR.Layers[0].Graphs[0].Layout.Positions[0].X += 1;
        FString MovedHash;
        Diagnostics.Reset();
        TestTrue(
            TEXT("Moved layout Canonical IR hash succeeds"),
            USekiroAnimBlueprintFactoryLibrary::ComputeCanonicalIRHash(
                MovedIR,
                MovedHash,
                Diagnostics));
        TestNotEqual(TEXT("Layout position participates in the hash"), MovedHash, FirstHash);
    }

    const FSekiroAnimIRGraph* ReadMainGraph = SekiroAnimBlueprintFactoryTests::FindGraph(
        FirstRead,
        TEXT("Graph.Main"));
    const FSekiroAnimIRGraph* ReadStateMachine = SekiroAnimBlueprintFactoryTests::FindGraph(
        FirstRead,
        TEXT("Graph.StateMachine"));
    TestNotNull(TEXT("Reader returns main Pose Graph"), ReadMainGraph);
    TestNotNull(TEXT("Reader returns StateMachine Graph"), ReadStateMachine);
    if (ReadMainGraph != nullptr)
    {
        const FSekiroAnimIRLayoutPosition* MachinePosition =
            ReadMainGraph->Layout.Positions.FindByPredicate(
                [](const FSekiroAnimIRLayoutPosition& Position)
                {
                    return Position.ElementId == TEXT("Node.StateMachine");
                });
        TestNotNull(TEXT("Reader returns actual state-machine node position"), MachinePosition);
    }
    if (ReadStateMachine != nullptr)
    {
        TestTrue(
            TEXT("Reader returns Transition topology"),
            !ReadStateMachine->StateMachine.Transitions.IsEmpty());
        TestEqual(
            TEXT("Reader returns one position per State"),
            ReadStateMachine->Layout.Positions.Num(),
            ReadStateMachine->StateMachine.States.Num());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintIRReaderUnsupportedNodeTest,
    "Sekiro.AnimGraphIR.Reader.UnsupportedNodeFailsAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证主 Pose Graph 出现未注册 K2 节点时 Reader 返回结构化错误且不泄漏部分 IR。
 * 测试仅修改 transient Graph，不保存文件、不编译资产，也不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintIRReaderUnsupportedNodeTest::RunTest(const FString& Parameters)
{
    USkeleton* Skeleton = SekiroAnimBlueprintFactoryTests::LoadTestSkeleton();
    UAnimSequence* Sequence = SekiroAnimBlueprintFactoryTests::CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR SourceIR = SekiroAnimBlueprintFactoryTests::MakeFactoryIR(Sequence);
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* Blueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        SourceIR,
        Diagnostics);
    UAnimationGraph* MainGraph = SekiroAnimBlueprintFactoryTests::FindMainGraph(Blueprint);
    TestNotNull(TEXT("Unsupported-node test Blueprint is created"), Blueprint);
    TestNotNull(TEXT("Unsupported-node test main Graph exists"), MainGraph);
    if (Blueprint == nullptr || MainGraph == nullptr) return true;

    UK2Node_CallFunction* UnsupportedNode = NewObject<UK2Node_CallFunction>(MainGraph);
    UnsupportedNode->CreateNewGuid();
    MainGraph->AddNode(UnsupportedNode, false, false);
    FSekiroAnimBlueprintIR ReadIR;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Unsupported native node fails the whole read"),
        USekiroAnimBlueprintFactoryLibrary::ReadAnimBlueprintToIR(
            Blueprint,
            ReadIR,
            Diagnostics));
    TestTrue(TEXT("Failed read returns structured diagnostics"), !Diagnostics.IsEmpty());
    TestTrue(TEXT("Failed read exposes no partial layers"), ReadIR.Layers.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimBlueprintToLuaFailureProtectionTest,
    "Sekiro.AnimGraphIR.Writer.FailureProtection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 AnimBlueprint → Lua 在 Reader 失败时不覆盖已有 generated 文件或手写运行时模块，
 * 并在任何文件访问前拒绝包含路径分隔符和上级跳转的模块名。测试只在 ScriptRoot 的
 * `__SekiroAnimGraphIRTests__` 目录写入唯一临时文件，所有断言完成后立即删除，不启动 PIE。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集与清理。
 */
bool FSekiroAnimBlueprintToLuaFailureProtectionTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroAnimBlueprintFactoryTests;

    USkeleton* Skeleton = LoadTestSkeleton();
    UAnimSequence* Sequence = CreateTestSequence(Skeleton);
    if (Skeleton == nullptr || Sequence == nullptr) return true;

    FSekiroAnimBlueprintIR SourceIR = MakeFactoryIR(Sequence);
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    UAnimBlueprint* Blueprint = USekiroAnimBlueprintFactoryLibrary::CreateTransientAnimBlueprint(
        SourceIR,
        Diagnostics);
    TestNotNull(TEXT("Writer protection Blueprint is created"), Blueprint);
    if (Blueprint == nullptr) return true;

    const FString UniqueLeaf = TEXT("WriterProtection_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString RuntimeModule = TEXT("__SekiroAnimGraphIRTests__.") + UniqueLeaf;
    const FString ScriptRoot = FPaths::ConvertRelativePathToFull(
        UUnLuaFunctionLibrary::GetScriptRootPath());
    const FString TestDirectory = FPaths::Combine(
        ScriptRoot,
        TEXT("__SekiroAnimGraphIRTests__"));
    const FString RuntimePath = FPaths::Combine(TestDirectory, UniqueLeaf + TEXT(".lua"));
    const FString GeneratedPath = FPaths::Combine(
        TestDirectory,
        UniqueLeaf + TEXT(".generated.lua"));
    const FString RuntimeSentinel(TEXT("-- handwritten runtime sentinel\nreturn {}\n"));
    const FString GeneratedSentinel(TEXT("-- previous generated sentinel\nreturn {}\n"));
    IFileManager& FileManager = IFileManager::Get();
    TestTrue(TEXT("Writer test directory is created"), FileManager.MakeDirectory(*TestDirectory, true));
    TestTrue(
        TEXT("Handwritten runtime sentinel is written"),
        FFileHelper::SaveStringToFile(
            RuntimeSentinel,
            *RuntimePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(
        TEXT("Existing generated sentinel is written"),
        FFileHelper::SaveStringToFile(
            GeneratedSentinel,
            *GeneratedPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

    TestTrue(
        TEXT("Writer protection source is configured"),
        USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
            Blueprint,
            RuntimeModule));
    UAnimationGraph* MainGraph = FindMainGraph(Blueprint);
    if (MainGraph != nullptr)
    {
        UK2Node_CallFunction* UnsupportedNode = NewObject<UK2Node_CallFunction>(MainGraph);
        UnsupportedNode->CreateNewGuid();
        MainGraph->AddNode(UnsupportedNode, false, false);
    }

    FString GeneratedModule;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Reader failure rejects generated Lua replacement"),
        USekiroAnimBlueprintFactoryLibrary::AnimBlueprintToLua(
            Blueprint,
            GeneratedModule,
            Diagnostics,
            true));
    FString RuntimeAfterFailure;
    FString GeneratedAfterFailure;
    TestTrue(
        TEXT("Handwritten runtime module remains readable"),
        FFileHelper::LoadFileToString(RuntimeAfterFailure, *RuntimePath));
    TestTrue(
        TEXT("Existing generated module remains readable"),
        FFileHelper::LoadFileToString(GeneratedAfterFailure, *GeneratedPath));
    TestEqual(
        TEXT("Failure never changes handwritten runtime module"),
        RuntimeAfterFailure,
        RuntimeSentinel);
    TestEqual(
        TEXT("Failure never changes previous generated module"),
        GeneratedAfterFailure,
        GeneratedSentinel);

    TestTrue(
        TEXT("Unsafe source can be stored only for rejection testing"),
        USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
            Blueprint,
            TEXT("../Escape")));
    Diagnostics.Reset();
    TestFalse(
        TEXT("Traversal module is rejected before writing"),
        USekiroAnimBlueprintFactoryLibrary::AnimBlueprintToLua(
            Blueprint,
            GeneratedModule,
            Diagnostics,
            true));
    TestTrue(
        TEXT("Traversal rejection emits stable diagnostic"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(
            Diagnostics,
            TEXT("Writer.UnsafeModulePath")));

    FileManager.Delete(*RuntimePath, false, true);
    FileManager.Delete(*GeneratedPath, false, true);
    FileManager.Delete(*(GeneratedPath + TEXT(".bak")), false, true);
    FileManager.DeleteDirectory(*TestDirectory, false, false);
    return true;
}

#endif
