#include "SekiroAnimBlueprintFactoryLibrary.h"

#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_UseCachedPose.h"
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
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
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
     * 构造包含两个 State、Inertialization 与同源同目标并行 Transition 的完整工厂测试 IR。
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

    const FSekiroAnimBlueprintIR BlueprintIR = MakeFactoryIR(Sequence);
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
    TestNotNull(TEXT("GeneratedClass exists"), FirstBlueprint->GeneratedClass.Get());
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
        TEXT("EventGraph refreshes every Transition exactly once"),
        CountFunctionCalls(
            EventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateAndCacheTransitionRule)),
        2);

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
    TestEqual(TEXT("Two states are created"), CountNodes<UAnimStateNode>(StateMachineGraph), 2);
    TestEqual(TEXT("Parallel transitions are preserved"), CountNodes<UAnimStateTransitionNode>(StateMachineGraph), 2);

    UAnimStateNode* IdleState = FindState(StateMachineGraph, TEXT("Idle"));
    UAnimStateNode* MoveState = FindState(StateMachineGraph, TEXT("Move"));
    TestNotNull(TEXT("Idle state exists"), IdleState);
    TestNotNull(TEXT("Move state exists"), MoveState);
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
                TEXT("Each Transition Graph has one cache Getter"),
                CountFunctionCalls(
                    RuleGraph,
                    GET_FUNCTION_NAME_CHECKED(
                        USekiroLuaTransitionRuntimeLibrary,
                        GetCachedTransitionRule)),
                1);
            TestEqual(
                TEXT("Each Transition Result is connected to its Getter"),
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
 * 验证内存 Lua 模块可一键导入、生成并保存原生 AnimBlueprint，且生成的 Update Event 会发布 true/false Rule 缓存。
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
            TEXT("Saved EventGraph contains two refresh calls"),
            CountFunctionCalls(
                EventGraph,
                GET_FUNCTION_NAME_CHECKED(
                    USekiroLuaTransitionRuntimeLibrary,
                    EvaluateAndCacheTransitionRule)),
            2);

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
                TEXT("BlueprintUpdateAnimation publishes Lua true"),
                USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
                    AnimInstance,
                    ModuleName,
                    TEXT("CanEnter_IdleSelf")));
            TestFalse(
                TEXT("BlueprintUpdateAnimation publishes Lua false"),
                USekiroLuaTransitionRuntimeLibrary::GetCachedTransitionRule(
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
    const int32 FirstRuleCallCount = CountFunctionCalls(
        EventGraph,
        GET_FUNCTION_NAME_CHECKED(
            USekiroLuaTransitionRuntimeLibrary,
            EvaluateAndCacheTransitionRule));
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
        TEXT("EventGraph rule calls do not duplicate"),
        CountFunctionCalls(
            EventGraph,
            GET_FUNCTION_NAME_CHECKED(
                USekiroLuaTransitionRuntimeLibrary,
                EvaluateAndCacheTransitionRule)),
        FirstRuleCallCount);
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
