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
#include "AnimGraph/AnimGraphNode_FootPlacement.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
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
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LuaEnv.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "SekiroAnimGraphIRLibrary.h"
#include "SekiroAnimGraphNodeRegistry.h"
#include "SekiroLuaAnimBlueprintExtension.h"
#include "SekiroLuaAnimBlueprintEditorBinding.h"
#include "SekiroLuaAnimBlueprintFactory.h"
#include "SekiroLuaTransitionRuntimeLibrary.h"
#include "UnLuaModule.h"

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
            StateMachineGraph->Layout.Style = ESekiroAnimIRLayoutStyle::HierarchicalBlocks;

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
    TestEqual(TEXT("Explicit StateMachine Grid column is applied"),
        StateMachineNode != nullptr ? StateMachineNode->NodePosX : INDEX_NONE, 0);
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
        TEXT("OrientationWarping uses Manual mode"),
        OrientationWarping != nullptr ? OrientationWarping->Node.Mode : EWarpingEvaluationMode::Graph,
        EWarpingEvaluationMode::Manual);
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
    FSekiroAnimIRTransition& NativeTransition = StateMachineIR->StateMachine.Transitions[0];
    NativeTransition.RuleFunctionName = NAME_None;
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
 * 验证 Validator 可接受但 NodeFactory 尚未支持的第二 Layer 会被显式拒绝，不被静默忽略。
 * 测试只操作内存 IR，不创建 UObject；由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimBlueprintFactoryMultipleLayersTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR BlueprintIR = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRLayer& SecondLayer = BlueprintIR.Layers.AddDefaulted_GetRef();
    SecondLayer.Id = TEXT("Layer.Second");
    SecondLayer.Name = TEXT("Second");
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
    TestNull(TEXT("Multiple Layers create no blueprint"), AnimBlueprint);
    TestTrue(
        TEXT("Multiple Layers emit stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("Factory.UnsupportedLayerCount")));
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
        TestTrue(TEXT("New Lua asset starts source dirty"), Extension->bSourceDirty);
        TestEqual(TEXT("New Lua asset has initial source revision"), Extension->SourceRevision, 1);
    }

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
    TArray<UEdGraph*> FirstAllGraphs;
    AnimBlueprint->GetAllGraphs(FirstAllGraphs);

    bool bRecursiveCallbackObserved = false;
    bool bRecursiveCompileSucceeded = true;
    TArray<FSekiroAnimIRDiagnostic> RecursiveDiagnostics;
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
    TestEqual(TEXT("Variables do not duplicate"), AnimBlueprint->NewVariables.Num(), FirstVariableCount);
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
        TEXT("Generate From Lua restores cleared nodes"),
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
        TEXT("Generate From Lua recreates missing Graph shells"),
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
            AnimBlueprint,
            TestCompileCommand);
    const TSharedRef<FExtender> FirstToolbarExtender =
        CommandBinding->GetToolbarExtender();
    const TSharedRef<FExtender> RebuiltToolbarExtender =
        CommandBinding->GetToolbarExtender();
    TestTrue(
        TEXT("Toolbar rebuild reuses one Lua extender"),
        &FirstToolbarExtender.Get() == &RebuiltToolbarExtender.Get());
    FToolBarBuilder TestToolbarBuilder(
        TestCommandList,
        FMultiBoxCustomization::None);
    TestToolbarBuilder.AddToolBarButton(
        FUIAction(),
        NAME_None,
        FText::FromString(TEXT("Existing native control")));
    CommandBinding->FillToolbar(TestToolbarBuilder);
    TestEqual(
        TEXT("Parent toolbar does not receive Lua controls"),
        TestToolbarBuilder.GetMultiBox()->GetBlocks().Num(),
        1);
    FToolBarBuilder DisplayedToolbarBuilder(
        TestCommandList,
        FMultiBoxCustomization::AllowCustomization(
            TEXT("AssetEditorToolbar.CommonActions")));
    CommandBinding->FillToolbar(DisplayedToolbarBuilder);
    const int32 DisplayedToolbarBlockCount =
        DisplayedToolbarBuilder.GetMultiBox()->GetBlocks().Num();
    TestTrue(
        TEXT("Displayed toolbar receives Lua controls"),
        DisplayedToolbarBlockCount > 0);
    CommandBinding->FillToolbar(DisplayedToolbarBuilder);
    TestEqual(
        TEXT("Repeated displayed toolbar fill does not duplicate Lua controls"),
        DisplayedToolbarBuilder.GetMultiBox()->GetBlocks().Num(),
        DisplayedToolbarBlockCount);
    Extension = USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    const int32 CheckedRevisionBeforeNativeCommand =
        Extension != nullptr ? Extension->LastCheckedSourceRevision : INDEX_NONE;
    if (Extension != nullptr)
    {
        Extension->SourceMode = ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint;
    }
    TestTrue(
        TEXT("Native mode command list executes mapped Compile action"),
        TestCommandList->ExecuteAction(TestCompileCommand.ToSharedRef()));
    TestEqual(TEXT("Native mode delegates exactly once"), NativeCompileCallCount, 1);
    if (Extension != nullptr)
    {
        TestEqual(
            TEXT("Native mode does not run Check Lua"),
            Extension->LastCheckedSourceRevision,
            CheckedRevisionBeforeNativeCommand);
        Extension->SourceMode = ESekiroLuaAnimBlueprintSourceMode::Lua;
        Extension->MarkSourceDirty(TEXT("Command binding Lua mode test."));
    }
    const int32 SuccessfulRevisionBeforeLuaCommand =
        Extension != nullptr ? Extension->SuccessfulCompileRevision : 0;
    TestTrue(
        TEXT("Lua mode command list executes mapped Compile action"),
        TestCommandList->ExecuteAction(TestCompileCommand.ToSharedRef()));
    TestEqual(
        TEXT("Lua mode delegates to native Compile exactly once"),
        NativeCompileCallCount,
        2);
    if (Extension != nullptr)
    {
        TestEqual(
            TEXT("Lua mode records one successful native compile"),
            Extension->SuccessfulCompileRevision,
            SuccessfulRevisionBeforeLuaCommand + 1);
        TestFalse(TEXT("Lua mode compile clears source dirty"), Extension->bSourceDirty);
    }
    const int32 SuccessfulRevisionAfterCommand =
        Extension != nullptr ? Extension->SuccessfulCompileRevision : 0;
    CommandBinding->RestoreOriginalCompileActions();

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
    const TArray<UEdGraphNode*> NodesBeforeFailedGenerate = MainGraph->Nodes;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Preflight failure rejects Generate From Lua"),
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
        FirstVariableCount);
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

#endif
