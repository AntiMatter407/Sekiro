#include "SekiroAnimGraphIRLibrary.h"

#include "Misc/AutomationTest.h"
#include "SekiroAnimGraphNodeRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SekiroAnimGraphIRTests
{
    /**
     * 构造 Main Pose Graph、其 StateMachine Node、内部 StateMachine Graph 与状态专属 StatePose Graph 的合法最小 IR。
     * 函数不访问 UObject 或磁盘，可在任意线程调用。
     *
     * @return 值语义返回完整测试 IR，调用方拥有并可自由修改。
     */
    FSekiroAnimBlueprintIR MakeMinimalIR()
    {
        FSekiroAnimBlueprintIR Blueprint;
        Blueprint.SchemaVersion = 2;
        Blueprint.SourceModule = TEXT("tests.anim_graph");
        Blueprint.ParentAnimInstanceClass = FSoftClassPath(TEXT("/Script/Engine.AnimInstance"));
        Blueprint.TargetSkeleton = FSoftObjectPath(TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton"));
        Blueprint.SourceLocation.LuaModule = Blueprint.SourceModule;
        Blueprint.SourceLocation.Line = 1;

        FSekiroAnimIRLayer& Layer = Blueprint.Layers.AddDefaulted_GetRef();
        Layer.Id = TEXT("Layer.Main");
        Layer.Name = TEXT("Main");
        Layer.RootGraphId = TEXT("Graph.Main");
        Layer.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRGraph& Graph = Layer.Graphs.AddDefaulted_GetRef();
        Graph.Id = TEXT("Graph.Main");
        Graph.Name = TEXT("MainGraph");
        Graph.GraphType = SekiroAnimGraphIRNames::PoseGraph;
        Graph.RootNodeId = TEXT("Node.Output");
        Graph.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRNode& RootNode = Graph.Nodes.AddDefaulted_GetRef();
        RootNode.Id = TEXT("Node.Output");
        RootNode.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
        RootNode.DisplayName = TEXT("Output Pose");
        RootNode.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRPin& ResultPin = RootNode.Pins.AddDefaulted_GetRef();
        ResultPin.Name = TEXT("Result");
        ResultPin.Direction = ESekiroAnimIRPinDirection::Input;
        ResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRNode& StateMachineNode = Graph.Nodes.AddDefaulted_GetRef();
        StateMachineNode.Id = TEXT("Node.StateMachine");
        StateMachineNode.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
        StateMachineNode.DisplayName = TEXT("Locomotion");
        StateMachineNode.OwnedGraphId = TEXT("Graph.StateMachine");
        StateMachineNode.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRPin& StateMachinePosePin = StateMachineNode.Pins.AddDefaulted_GetRef();
        StateMachinePosePin.Name = TEXT("Pose");
        StateMachinePosePin.Direction = ESekiroAnimIRPinDirection::Output;
        StateMachinePosePin.DataType = SekiroAnimGraphIRNames::PoseData;
        StateMachinePosePin.bAllowMultipleConnections = true;

        FSekiroAnimIRLink& StateMachineLink = Graph.Links.AddDefaulted_GetRef();
        StateMachineLink.Id = TEXT("Link.StateMachineToOutput");
        StateMachineLink.Source.NodeId = TEXT("Node.StateMachine");
        StateMachineLink.Source.PinName = TEXT("Pose");
        StateMachineLink.Target.NodeId = TEXT("Node.Output");
        StateMachineLink.Target.PinName = TEXT("Result");
        StateMachineLink.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRGraph& StateMachineGraph = Layer.Graphs.AddDefaulted_GetRef();
        StateMachineGraph.Id = TEXT("Graph.StateMachine");
        StateMachineGraph.Name = TEXT("Locomotion");
        StateMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;
        StateMachineGraph.StateMachine.EntryStateId = TEXT("State.Idle");
        StateMachineGraph.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRState& IdleState = StateMachineGraph.StateMachine.States.AddDefaulted_GetRef();
        IdleState.Id = StateMachineGraph.StateMachine.EntryStateId;
        IdleState.Name = TEXT("Idle");
        IdleState.GraphId = TEXT("Graph.Idle");
        IdleState.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRGraph& IdleGraph = Layer.Graphs.AddDefaulted_GetRef();
        IdleGraph.Id = TEXT("Graph.Idle");
        IdleGraph.Name = TEXT("Idle");
        IdleGraph.GraphType = SekiroAnimGraphIRNames::StatePoseGraph;
        IdleGraph.RootNodeId = TEXT("Node.Idle.Output");
        IdleGraph.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRNode& IdleRootNode = IdleGraph.Nodes.AddDefaulted_GetRef();
        IdleRootNode.Id = IdleGraph.RootNodeId;
        IdleRootNode.NodeType = SekiroAnimGraphIRNames::StateResultNode;
        IdleRootNode.DisplayName = TEXT("State Result");
        IdleRootNode.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRPin& IdleResultPin = IdleRootNode.Pins.AddDefaulted_GetRef();
        IdleResultPin.Name = TEXT("Result");
        IdleResultPin.Direction = ESekiroAnimIRPinDirection::Input;
        IdleResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

        return Blueprint;
    }

    /**
     * 向最小 IR 的首个 Layer 追加一个带 StateResult 根节点的 StatePose Graph。
     * 函数只修改 Blueprint 内数组，不保留其他数组元素引用；调用方必须独占 Blueprint。
     *
     * @param Blueprint 由 MakeMinimalIR 创建或具有首层 Layer 的可变 IR。
     * @param GraphId 新 StatePose Graph 的全局稳定 ID。
     * @param RootNodeId 新 StateResult 节点的全局稳定 ID。
     * @return 新增 Graph 的数组内引用；Layer.Graphs 再次扩容后该引用可能失效。
     */
    FSekiroAnimIRGraph& AddStatePoseGraph(
        FSekiroAnimBlueprintIR& Blueprint,
        const FString& GraphId,
        const FString& RootNodeId)
    {
        FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs.AddDefaulted_GetRef();
        Graph.Id = GraphId;
        Graph.Name = GraphId;
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
     * 向最小 IR 的主 Graph 追加一个满足权威契约的 SequencePlayer 节点。
     * 函数只修改 Blueprint 内数组，不保留引用；调用方必须独占 Blueprint。
     *
     * @param Blueprint 由 MakeMinimalIR 创建或具有相同首层结构的可变 IR。
     * @param NodeId 新节点的全局稳定 ID。
     */
    void AddSequencePlayerNode(FSekiroAnimBlueprintIR& Blueprint, const FString& NodeId)
    {
        FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs[0];
        FSekiroAnimIRNode& Node = Graph.Nodes.AddDefaulted_GetRef();
        Node.Id = NodeId;
        Node.NodeType = SekiroAnimGraphIRNames::SequencePlayerNode;
        Node.DisplayName = NodeId;
        Node.SourceLocation = Blueprint.SourceLocation;

        FSekiroAnimIRPin& OutputPin = Node.Pins.AddDefaulted_GetRef();
        OutputPin.Name = TEXT("Pose");
        OutputPin.Direction = ESekiroAnimIRPinDirection::Output;
        OutputPin.DataType = SekiroAnimGraphIRNames::PoseData;
        OutputPin.bAllowMultipleConnections = true;

        FSekiroAnimIRProperty& SequenceProperty = Node.Properties.AddDefaulted_GetRef();
        SequenceProperty.Name = TEXT("Sequence");
        SequenceProperty.Value.Type = ESekiroAnimIRValueType::SoftObjectPath;
        SequenceProperty.Value.SoftObjectPathValue =
            FSoftObjectPath(TEXT("/Engine/EngineAnimations/DefaultAnim.DefaultAnim"));
    }

    /**
     * 查询验证结果中是否包含指定稳定诊断代码。
     * 函数只读遍历数组，可在任意线程调用。
     *
     * @param Diagnostics Validator 产生的诊断数组。
     * @param Code 目标稳定诊断代码。
     * @return 至少存在一条匹配诊断时返回 true。
     */
    bool HasDiagnosticCode(const TArray<FSekiroAnimIRDiagnostic>& Diagnostics, const FName Code)
    {
        for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Code == Code) return true;
        }

        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRValidMinimalTest,
    "Sekiro.AnimGraphIR.ValidMinimal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证合法最小 Pose Graph 不产生诊断。
 * 测试仅操作内存 IR，可由 Automation Framework 在编辑器测试线程调用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRValidMinimalTest::RunTest(const FString& Parameters)
{
    const FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bIsValid = USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics);

    TestTrue(TEXT("Minimal IR is valid"), bIsValid);
    TestEqual(TEXT("Minimal IR has no diagnostics"), Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRMissingTargetSkeletonTest,
    "Sekiro.AnimGraphIR.MissingTargetSkeleton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证具体 AnimBlueprint IR 必须显式声明 TargetSkeleton，不能从父类或动画资源推导。
 * 测试仅清空内存软路径，不加载 UObject 或查询 Asset Registry。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRMissingTargetSkeletonTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.TargetSkeleton.Reset();

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing TargetSkeleton is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing TargetSkeleton emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MissingTargetSkeleton")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRInvalidTargetSkeletonPathTest,
    "Sekiro.AnimGraphIR.InvalidTargetSkeletonPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 TargetSkeleton 拒绝子对象路径与磁盘式非法包路径，只接受顶层资产对象路径。
 * 测试仅执行字符串和软路径检查，不加载 Skeleton 资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRInvalidTargetSkeletonPathTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.TargetSkeleton = FSoftObjectPath(
        TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton:Preview"));

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("TargetSkeleton subobject path is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("TargetSkeleton subobject emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTargetSkeletonPath")));

    Blueprint.TargetSkeleton = FSoftObjectPath(TEXT("/F:/Project/Skeleton.Skeleton"));
    Diagnostics.Reset();
    TestFalse(TEXT("TargetSkeleton disk-style path is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("TargetSkeleton disk-style path emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTargetSkeletonPath")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRRootGraphTypeTest,
    "Sekiro.AnimGraphIR.RootGraphType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Layer 根只能指向本层 Pose Graph，不能直接以内部 StateMachine Graph 为根。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRRootGraphTypeTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].RootGraphId = TEXT("Graph.StateMachine");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("StateMachine Layer root is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Root Graph type emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.LayerRootGraphTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStateMachineNodeContainerTest,
    "Sekiro.AnimGraphIR.StateMachineNodeContainer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StateMachine Node 只能位于 Pose 或 StatePose Graph，不能放入其内部 StateMachine Graph。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStateMachineNodeContainerTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRNode MisplacedNode = Blueprint.Layers[0].Graphs[0].Nodes[1];
    Blueprint.Layers[0].Graphs[0].Nodes.RemoveAt(1);
    Blueprint.Layers[0].Graphs[0].Links.Reset();
    Blueprint.Layers[0].Graphs[1].Nodes.Add(MisplacedNode);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("StateMachine Node outside Pose Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Invalid StateMachine Node container emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateMachineNodeOutsidePoseGraph")));
    TestTrue(
        TEXT("Registry rejects StateMachine Node in a StateMachine Graph"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.NodeGraphTypeNotAllowed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRNestedStateMachineNodeTest,
    "Sekiro.AnimGraphIR.NestedStateMachineNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StatePose Graph 可以包含 StateMachine Node，并独占其内部 StateMachine Graph。
 * 测试构造两层嵌套状态机且仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRNestedStateMachineNodeTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& IdleGraph = Blueprint.Layers[0].Graphs[2];
    const FString NestedMachineGraphId(TEXT("Graph.NestedMachine"));

    FSekiroAnimIRNode& NestedMachineNode = IdleGraph.Nodes.AddDefaulted_GetRef();
    NestedMachineNode.Id = TEXT("Node.Idle.NestedMachine");
    NestedMachineNode.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
    NestedMachineNode.DisplayName = TEXT("Nested Machine");
    NestedMachineNode.OwnedGraphId = NestedMachineGraphId;
    NestedMachineNode.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRPin& NestedPosePin = NestedMachineNode.Pins.AddDefaulted_GetRef();
    NestedPosePin.Name = TEXT("Pose");
    NestedPosePin.Direction = ESekiroAnimIRPinDirection::Output;
    NestedPosePin.DataType = SekiroAnimGraphIRNames::PoseData;
    NestedPosePin.bAllowMultipleConnections = true;

    FSekiroAnimIRLink& NestedOutputLink = IdleGraph.Links.AddDefaulted_GetRef();
    NestedOutputLink.Id = TEXT("Link.NestedMachineToIdleResult");
    NestedOutputLink.Source.NodeId = NestedMachineNode.Id;
    NestedOutputLink.Source.PinName = TEXT("Pose");
    NestedOutputLink.Target.NodeId = IdleGraph.RootNodeId;
    NestedOutputLink.Target.PinName = TEXT("Result");

    FSekiroAnimIRGraph& NestedMachineGraph = Blueprint.Layers[0].Graphs.AddDefaulted_GetRef();
    NestedMachineGraph.Id = NestedMachineGraphId;
    NestedMachineGraph.Name = TEXT("NestedMachine");
    NestedMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;
    NestedMachineGraph.StateMachine.EntryStateId = TEXT("State.NestedIdle");
    NestedMachineGraph.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRState& NestedIdleState = NestedMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    NestedIdleState.Id = NestedMachineGraph.StateMachine.EntryStateId;
    NestedIdleState.Name = TEXT("NestedIdle");
    NestedIdleState.GraphId = TEXT("Graph.NestedIdle");
    NestedIdleState.SourceLocation = Blueprint.SourceLocation;

    SekiroAnimGraphIRTests::AddStatePoseGraph(
        Blueprint,
        TEXT("Graph.NestedIdle"),
        TEXT("Node.NestedIdle.Result"));

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Nested StateMachine Node in StatePose Graph is valid"),
        USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestEqual(TEXT("Nested StateMachine topology has no diagnostics"), Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStateGraphMissingOwnerTest,
    "Sekiro.AnimGraphIR.StateGraphMissingOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证每个 StatePose Graph 必须由同层一个 State 拥有，不能作为游离 Graph 存在。
 * 测试仅追加一个未引用的内存 Graph，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStateGraphMissingOwnerTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddStatePoseGraph(
        Blueprint,
        TEXT("Graph.OrphanState"),
        TEXT("Node.OrphanState.Result"));

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Unowned StatePose Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Unowned StatePose Graph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateGraphMissingOwner")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStateGraphMultipleOwnersTest,
    "Sekiro.AnimGraphIR.StateGraphMultipleOwners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证一个 StatePose Graph 不能被同层多个 State 复用。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStateGraphMultipleOwnersTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRState& SecondState = Blueprint.Layers[0].Graphs[1].StateMachine.States.AddDefaulted_GetRef();
    SecondState.Id = TEXT("State.SecondIdle");
    SecondState.Name = TEXT("SecondIdle");
    SecondState.GraphId = TEXT("Graph.Idle");
    SecondState.SourceLocation = Blueprint.SourceLocation;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Shared StatePose Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Shared StatePose Graph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateGraphMultipleOwners")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStateGraphIsLayerRootTest,
    "Sekiro.AnimGraphIR.StateGraphIsLayerRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 State 不得复用 Layer 的主 Pose 根 Graph。
 * 测试仅重定向一个内存 State 引用，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStateGraphIsLayerRootTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[1].StateMachine.States[0].GraphId = Blueprint.Layers[0].RootGraphId;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Layer root reused as State Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Layer root reuse emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateGraphIsLayerRoot")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStateGraphTypeTest,
    "Sekiro.AnimGraphIR.StateGraphType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 State.GraphId 只能指向 StatePose Graph，普通 Pose Graph 不满足状态所有权契约。
 * 测试仅修改内存 Graph 类型，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStateGraphTypeTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[2].GraphType = SekiroAnimGraphIRNames::PoseGraph;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Ordinary Pose Graph used by State is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("State Graph type mismatch emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateGraphTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRStatePoseValidationTest,
    "Sekiro.AnimGraphIR.StatePoseValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StatePose Graph 与主 Pose Graph 使用相同的 RootNode、Link 和 Pose DAG 校验。
 * 测试在一个 StatePose Graph 中同时构造缺失根节点、坏 Link 与有向环，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRStatePoseValidationTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs[2];
    Graph.Nodes.Reset();
    Graph.Links.Reset();
    Graph.RootNodeId = TEXT("Node.MissingRoot");

    FSekiroAnimIRNode& ResultNode = Graph.Nodes.AddDefaulted_GetRef();
    ResultNode.Id = TEXT("Node.StatePose.Result");
    ResultNode.NodeType = SekiroAnimGraphIRNames::StateResultNode;
    FSekiroAnimIRPin& ResultPin = ResultNode.Pins.AddDefaulted_GetRef();
    ResultPin.Name = TEXT("Result");
    ResultPin.Direction = ESekiroAnimIRPinDirection::Input;
    ResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

    for (int32 NodeIndex = 0; NodeIndex < 2; ++NodeIndex)
    {
        FSekiroAnimIRNode& Node = Graph.Nodes.AddDefaulted_GetRef();
        Node.Id = NodeIndex == 0 ? TEXT("Node.StatePose.A") : TEXT("Node.StatePose.B");
        Node.NodeType = SekiroAnimGraphIRNames::InertializationNode;

        FSekiroAnimIRPin& InputPin = Node.Pins.AddDefaulted_GetRef();
        InputPin.Name = TEXT("Source");
        InputPin.Direction = ESekiroAnimIRPinDirection::Input;
        InputPin.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRPin& OutputPin = Node.Pins.AddDefaulted_GetRef();
        OutputPin.Name = TEXT("Pose");
        OutputPin.Direction = ESekiroAnimIRPinDirection::Output;
        OutputPin.DataType = SekiroAnimGraphIRNames::PoseData;
        OutputPin.bAllowMultipleConnections = true;
    }

    FSekiroAnimIRLink& LinkAB = Graph.Links.AddDefaulted_GetRef();
    LinkAB.Id = TEXT("Link.StatePose.AB");
    LinkAB.Source.NodeId = TEXT("Node.StatePose.A");
    LinkAB.Source.PinName = TEXT("Pose");
    LinkAB.Target.NodeId = TEXT("Node.StatePose.B");
    LinkAB.Target.PinName = TEXT("Source");

    FSekiroAnimIRLink& LinkBA = Graph.Links.AddDefaulted_GetRef();
    LinkBA.Id = TEXT("Link.StatePose.BA");
    LinkBA.Source.NodeId = TEXT("Node.StatePose.B");
    LinkBA.Source.PinName = TEXT("Pose");
    LinkBA.Target.NodeId = TEXT("Node.StatePose.A");
    LinkBA.Target.PinName = TEXT("Source");

    FSekiroAnimIRLink& BadLink = Graph.Links.AddDefaulted_GetRef();
    BadLink.Id = TEXT("Link.StatePose.Bad");
    BadLink.Source.NodeId = TEXT("Node.StatePose.A");
    BadLink.Source.PinName = TEXT("Pose");
    BadLink.Target.NodeId = TEXT("Node.StatePose.Missing");
    BadLink.Target.PinName = TEXT("Source");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid StatePose topology is rejected"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("StatePose root is validated"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.GraphRootNodeNotFound")));
    TestTrue(
        TEXT("StatePose Link is validated"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.LinkNodeNotFound")));
    TestTrue(
        TEXT("StatePose Pose cycle is validated"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.PoseGraphCycle")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRGraphOwnershipCycleTest,
    "Sekiro.AnimGraphIR.GraphOwnershipCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证任意 Node.OwnedGraphId 建立的 Graph 所有权回边会被拒绝。
 * 测试在合法 StatePose Graph 内构造自所有权边，不访问 UObject 或磁盘。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRGraphOwnershipCycleTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[2].Nodes[0].OwnedGraphId = TEXT("Graph.Idle");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Graph ownership cycle is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Graph ownership cycle emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.GraphOwnershipCycle")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRMissingOwnedGraphIdTest,
    "Sekiro.AnimGraphIR.MissingOwnedGraphId",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StateMachine Node 必须显式声明 OwnedGraphId，且内部 Graph 会报告缺少 owner。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRMissingOwnedGraphIdTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].Nodes[1].OwnedGraphId.Reset();

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing OwnedGraphId is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing OwnedGraphId emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateMachineNodeMissingOwnedGraph")));
    TestTrue(
        TEXT("Unowned StateMachine Graph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateMachineGraphMissingOwner")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRMissingOwnedGraphTest,
    "Sekiro.AnimGraphIR.MissingOwnedGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StateMachine Node 的 OwnedGraphId 必须解析到本层实际 Graph。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRMissingOwnedGraphTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].Nodes[1].OwnedGraphId = TEXT("Graph.Missing");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing owned Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing owned Graph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.OwnedGraphNotFound")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRMultipleOwnersTest,
    "Sekiro.AnimGraphIR.MultipleStateMachineOwners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证一个内部 StateMachine Graph 不能被同层多个 StateMachine Node 共同拥有。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRMultipleOwnersTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRNode DuplicateOwner = Blueprint.Layers[0].Graphs[0].Nodes[1];
    DuplicateOwner.Id = TEXT("Node.StateMachine.Second");
    Blueprint.Layers[0].Graphs[0].Nodes.Add(DuplicateOwner);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Multiple StateMachine owners are invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Multiple StateMachine owners emit stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateMachineGraphMultipleOwners")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRCrossLayerOwnedGraphTest,
    "Sekiro.AnimGraphIR.CrossLayerOwnedGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 StateMachine Node 不得把其他动画层中的 Graph 声明为 OwnedGraph。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRCrossLayerOwnedGraphTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].Nodes[1].OwnedGraphId = TEXT("Graph.OtherMachine");

    FSekiroAnimIRLayer& OtherLayer = Blueprint.Layers.AddDefaulted_GetRef();
    OtherLayer.Id = TEXT("Layer.OtherOwner");
    OtherLayer.RootGraphId = TEXT("Graph.OtherMachine");
    FSekiroAnimIRGraph& OtherMachineGraph = OtherLayer.Graphs.AddDefaulted_GetRef();
    OtherMachineGraph.Id = OtherLayer.RootGraphId;
    OtherMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Cross-layer OwnedGraph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Cross-layer OwnedGraph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.OwnedGraphOutsideLayer")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRDuplicateIdTest,
    "Sekiro.AnimGraphIR.DuplicateId",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同类型实体的重复稳定 ID 被拒绝。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRDuplicateIdTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    const FSekiroAnimIRNode DuplicateNode = Blueprint.Layers[0].Graphs[0].Nodes[0];
    Blueprint.Layers[0].Graphs[0].Nodes.Add(DuplicateNode);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Duplicate Node ID is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Duplicate Node ID emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.DuplicateNodeId")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRBadLinkTest,
    "Sekiro.AnimGraphIR.BadLink",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Link 端点引用不存在的节点时产生稳定诊断。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRBadLinkTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.Source"));

    FSekiroAnimIRLink& Link = Blueprint.Layers[0].Graphs[0].Links.AddDefaulted_GetRef();
    Link.Id = TEXT("Link.Bad");
    Link.Source.NodeId = TEXT("Node.Source");
    Link.Source.PinName = TEXT("Pose");
    Link.Target.NodeId = TEXT("Node.Missing");
    Link.Target.PinName = TEXT("Result");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing Link endpoint is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing Link endpoint emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.LinkNodeNotFound")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRPinContractMismatchTest,
    "Sekiro.AnimGraphIR.PinContractMismatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua IR 不能通过改写 Pin 数据类型覆盖权威节点契约。
 * 测试保留合法 Link，由注册表解析端点，因此只针对声明契约错误。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRPinContractMismatchTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.Source"));
    Blueprint.Layers[0].Graphs[0].Nodes.Last().Pins[0].DataType = TEXT("Float");

    FSekiroAnimIRLink& Link = Blueprint.Layers[0].Graphs[0].Links.AddDefaulted_GetRef();
    Link.Id = TEXT("Link.PinContractMismatch");
    Link.Source.NodeId = TEXT("Node.Source");
    Link.Source.PinName = TEXT("Pose");
    Link.Target.NodeId = TEXT("Node.Output");
    Link.Target.PinName = TEXT("Result");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Pin contract mismatch is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Pin contract mismatch emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.PinContractMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRPoseCycleTest,
    "Sekiro.AnimGraphIR.PoseCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Pose Graph 中的有向依赖环被拒绝。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRPoseCycleTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs[0];

    for (int32 NodeIndex = 0; NodeIndex < 2; ++NodeIndex)
    {
        FSekiroAnimIRNode& Node = Graph.Nodes.AddDefaulted_GetRef();
        Node.Id = NodeIndex == 0 ? TEXT("Node.Inertial.A") : TEXT("Node.Inertial.B");
        Node.NodeType = SekiroAnimGraphIRNames::InertializationNode;

        FSekiroAnimIRPin& InputPin = Node.Pins.AddDefaulted_GetRef();
        InputPin.Name = TEXT("Source");
        InputPin.Direction = ESekiroAnimIRPinDirection::Input;
        InputPin.DataType = SekiroAnimGraphIRNames::PoseData;

        FSekiroAnimIRPin& OutputPin = Node.Pins.AddDefaulted_GetRef();
        OutputPin.Name = TEXT("Pose");
        OutputPin.Direction = ESekiroAnimIRPinDirection::Output;
        OutputPin.DataType = SekiroAnimGraphIRNames::PoseData;
        OutputPin.bAllowMultipleConnections = true;
    }

    FSekiroAnimIRLink& LinkAB = Graph.Links.AddDefaulted_GetRef();
    LinkAB.Id = TEXT("Link.AB");
    LinkAB.Source.NodeId = TEXT("Node.Inertial.A");
    LinkAB.Source.PinName = TEXT("Pose");
    LinkAB.Target.NodeId = TEXT("Node.Inertial.B");
    LinkAB.Target.PinName = TEXT("Source");

    FSekiroAnimIRLink& LinkBA = Graph.Links.AddDefaulted_GetRef();
    LinkBA.Id = TEXT("Link.BA");
    LinkBA.Source.NodeId = TEXT("Node.Inertial.B");
    LinkBA.Source.PinName = TEXT("Pose");
    LinkBA.Target.NodeId = TEXT("Node.Inertial.A");
    LinkBA.Target.PinName = TEXT("Source");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Pose cycle is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Pose cycle emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.PoseGraphCycle")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRBadEntryTest,
    "Sekiro.AnimGraphIR.BadEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证状态机 Entry、Transition 状态引用和规则函数名错误均被报告。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRBadEntryTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& StateMachineGraph = Blueprint.Layers[0].Graphs[1];
    StateMachineGraph.StateMachine.EntryStateId = TEXT("State.Missing");

    FSekiroAnimIRTransition& Transition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    Transition.Id = TEXT("Transition.Bad");
    Transition.Key = TEXT("BadTransition");
    Transition.SourceStateId = TEXT("State.Idle");
    Transition.TargetStateId = TEXT("State.Missing");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Broken StateMachine is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Bad Entry emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EntryStateNotFound")));
    TestTrue(
        TEXT("Bad Transition state emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.TransitionStateNotFound")));
    TestTrue(
        TEXT("Empty Transition rule emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyTransitionRule")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRCanonicalizeTest,
    "Sekiro.AnimGraphIR.Canonicalize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证身份集合按稳定 ID、Pin 与属性按声明顺序生成确定性排列。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRCanonicalizeTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint;
    const FString ExpectedTargetSkeleton(TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton"));
    Blueprint.TargetSkeleton = FSoftObjectPath(ExpectedTargetSkeleton);
    FSekiroAnimIRLayer& LayerZ = Blueprint.Layers.AddDefaulted_GetRef();
    LayerZ.Id = TEXT("Layer.Z");
    FSekiroAnimIRLayer& LayerA = Blueprint.Layers.AddDefaulted_GetRef();
    LayerA.Id = TEXT("Layer.A");

    FSekiroAnimIRGraph& GraphZ = LayerA.Graphs.AddDefaulted_GetRef();
    GraphZ.Id = TEXT("Graph.Z");
    FSekiroAnimIRGraph& GraphA = LayerA.Graphs.AddDefaulted_GetRef();
    GraphA.Id = TEXT("Graph.A");

    FSekiroAnimIRNode& NodeZ = GraphZ.Nodes.AddDefaulted_GetRef();
    NodeZ.Id = TEXT("Node.Z");
    FSekiroAnimIRNode& NodeA = GraphZ.Nodes.AddDefaulted_GetRef();
    NodeA.Id = TEXT("Node.A");

    FSekiroAnimIRPin& PinLate = NodeA.Pins.AddDefaulted_GetRef();
    PinLate.Name = TEXT("Late");
    PinLate.DeclarationOrder = 20;
    FSekiroAnimIRPin& PinEarly = NodeA.Pins.AddDefaulted_GetRef();
    PinEarly.Name = TEXT("Early");
    PinEarly.DeclarationOrder = 10;

    FSekiroAnimIRProperty& PropertyLate = NodeA.Properties.AddDefaulted_GetRef();
    PropertyLate.Name = TEXT("Late");
    PropertyLate.DeclarationOrder = 20;
    FSekiroAnimIRProperty& PropertyEarly = NodeA.Properties.AddDefaulted_GetRef();
    PropertyEarly.Name = TEXT("Early");
    PropertyEarly.DeclarationOrder = 10;

    FSekiroAnimIRLink& LinkZ = GraphZ.Links.AddDefaulted_GetRef();
    LinkZ.Id = TEXT("Link.Z");
    FSekiroAnimIRLink& LinkA = GraphZ.Links.AddDefaulted_GetRef();
    LinkA.Id = TEXT("Link.A");

    FSekiroAnimIRState& StateZ = GraphZ.StateMachine.States.AddDefaulted_GetRef();
    StateZ.Id = TEXT("State.Z");
    FSekiroAnimIRState& StateA = GraphZ.StateMachine.States.AddDefaulted_GetRef();
    StateA.Id = TEXT("State.A");

    FSekiroAnimIRTransition& TransitionZ = GraphZ.StateMachine.Transitions.AddDefaulted_GetRef();
    TransitionZ.Id = TEXT("Transition.Z");
    TransitionZ.Key = TEXT("TransitionZ");
    TransitionZ.Settings.PriorityOrder = 0;
    TransitionZ.DeclarationOrder = 20;
    FSekiroAnimIRTransition& TransitionA = GraphZ.StateMachine.Transitions.AddDefaulted_GetRef();
    TransitionA.Id = TEXT("Transition.A");
    TransitionA.Key = TEXT("TransitionA");
    TransitionA.Settings.PriorityOrder = 1;
    TransitionA.DeclarationOrder = 10;

    USekiroAnimGraphIRLibrary::Canonicalize(Blueprint);
    USekiroAnimGraphIRLibrary::Canonicalize(Blueprint);

    TestEqual(TEXT("Layers sort by stable ID"), Blueprint.Layers[0].Id, FString(TEXT("Layer.A")));
    TestEqual(TEXT("Graphs sort by stable ID"), Blueprint.Layers[0].Graphs[0].Id, FString(TEXT("Graph.A")));
    const FSekiroAnimIRGraph& CanonicalGraphZ = Blueprint.Layers[0].Graphs[1];
    TestEqual(TEXT("Nodes sort by stable ID"), CanonicalGraphZ.Nodes[0].Id, FString(TEXT("Node.A")));
    TestEqual(TEXT("Pins sort by declaration order"), CanonicalGraphZ.Nodes[0].Pins[0].Name, FString(TEXT("Early")));
    TestEqual(
        TEXT("Properties sort by declaration order"),
        CanonicalGraphZ.Nodes[0].Properties[0].Name,
        FName(TEXT("Early")));
    TestEqual(TEXT("Links sort by stable ID"), CanonicalGraphZ.Links[0].Id, FString(TEXT("Link.A")));
    TestEqual(TEXT("States sort by stable ID"), CanonicalGraphZ.StateMachine.States[0].Id, FString(TEXT("State.A")));
    TestEqual(
        TEXT("Transitions preserve explicit priority"),
        CanonicalGraphZ.StateMachine.Transitions[0].Id,
        FString(TEXT("Transition.Z")));
    TestEqual(
        TEXT("Repeated Canonicalize preserves TargetSkeleton"),
        Blueprint.TargetSkeleton.ToString(),
        ExpectedTargetSkeleton);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRCrossLayerStateGraphTest,
    "Sekiro.AnimGraphIR.CrossLayerStateGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证状态只能引用所属动画层内的 Pose Graph，防止层之间产生隐藏拓扑依赖。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRCrossLayerStateGraphTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRLayer& OtherLayer = Blueprint.Layers.AddDefaulted_GetRef();
    OtherLayer.Id = TEXT("Layer.Other");
    OtherLayer.Name = TEXT("Other");
    OtherLayer.RootGraphId = TEXT("Graph.OtherMain");

    FSekiroAnimIRGraph& OtherMainGraph = OtherLayer.Graphs.AddDefaulted_GetRef();
    OtherMainGraph.Id = OtherLayer.RootGraphId;
    OtherMainGraph.Name = TEXT("OtherMain");
    OtherMainGraph.GraphType = SekiroAnimGraphIRNames::PoseGraph;
    OtherMainGraph.RootNodeId = TEXT("Node.OtherOutput");

    FSekiroAnimIRNode& OtherOutputNode = OtherMainGraph.Nodes.AddDefaulted_GetRef();
    OtherOutputNode.Id = OtherMainGraph.RootNodeId;
    OtherOutputNode.NodeType = SekiroAnimGraphIRNames::OutputPoseNode;
    FSekiroAnimIRPin& OtherResultPin = OtherOutputNode.Pins.AddDefaulted_GetRef();
    OtherResultPin.Name = TEXT("Result");
    OtherResultPin.Direction = ESekiroAnimIRPinDirection::Input;
    OtherResultPin.DataType = SekiroAnimGraphIRNames::PoseData;

    FSekiroAnimIRNode& OtherMachineNode = OtherMainGraph.Nodes.AddDefaulted_GetRef();
    OtherMachineNode.Id = TEXT("Node.OtherStateMachine");
    OtherMachineNode.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
    OtherMachineNode.OwnedGraphId = TEXT("Graph.OtherMachine");
    FSekiroAnimIRPin& OtherPosePin = OtherMachineNode.Pins.AddDefaulted_GetRef();
    OtherPosePin.Name = TEXT("Pose");
    OtherPosePin.Direction = ESekiroAnimIRPinDirection::Output;
    OtherPosePin.DataType = SekiroAnimGraphIRNames::PoseData;
    OtherPosePin.bAllowMultipleConnections = true;

    FSekiroAnimIRLink& OtherLink = OtherMainGraph.Links.AddDefaulted_GetRef();
    OtherLink.Id = TEXT("Link.OtherMachineToOutput");
    OtherLink.Source.NodeId = TEXT("Node.OtherStateMachine");
    OtherLink.Source.PinName = TEXT("Pose");
    OtherLink.Target.NodeId = TEXT("Node.OtherOutput");
    OtherLink.Target.PinName = TEXT("Result");

    FSekiroAnimIRGraph& StateMachineGraph = OtherLayer.Graphs.AddDefaulted_GetRef();
    StateMachineGraph.Id = TEXT("Graph.OtherMachine");
    StateMachineGraph.Name = TEXT("OtherMachine");
    StateMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;
    StateMachineGraph.StateMachine.EntryStateId = TEXT("State.OtherIdle");

    FSekiroAnimIRState& State = StateMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    State.Id = StateMachineGraph.StateMachine.EntryStateId;
    State.Name = TEXT("OtherIdle");
    State.GraphId = TEXT("Graph.Main");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Cross-layer State Graph is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Cross-layer State Graph emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.StateGraphOutsideLayer")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRMissingRootTest,
    "Sekiro.AnimGraphIR.MissingRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Pose Graph 缺失 Root Node 时被拒绝。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRMissingRootTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].RootNodeId.Reset();

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing Pose root is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Missing Pose root emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MissingGraphRootNode")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRLinkCardinalityTest,
    "Sekiro.AnimGraphIR.LinkCardinality",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证方向错误与单输入多连接均产生诊断。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRLinkCardinalityTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.SourceA"));
    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.SourceB"));
    FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs[0];

    FSekiroAnimIRLink& FirstLink = Graph.Links.AddDefaulted_GetRef();
    FirstLink.Id = TEXT("Link.A");
    FirstLink.Source.NodeId = TEXT("Node.SourceA");
    FirstLink.Source.PinName = TEXT("Pose");
    FirstLink.Target.NodeId = TEXT("Node.Output");
    FirstLink.Target.PinName = TEXT("Result");

    FSekiroAnimIRLink& SecondLink = Graph.Links.AddDefaulted_GetRef();
    SecondLink.Id = TEXT("Link.B");
    SecondLink.Source.NodeId = TEXT("Node.SourceB");
    SecondLink.Source.PinName = TEXT("Pose");
    SecondLink.Target.NodeId = TEXT("Node.Output");
    SecondLink.Target.PinName = TEXT("Result");

    FSekiroAnimIRLink& ReversedLink = Graph.Links.AddDefaulted_GetRef();
    ReversedLink.Id = TEXT("Link.Reversed");
    ReversedLink.Source.NodeId = TEXT("Node.Output");
    ReversedLink.Source.PinName = TEXT("Result");
    ReversedLink.Target.NodeId = TEXT("Node.SourceA");
    ReversedLink.Target.PinName = TEXT("Pose");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Bad Link cardinality is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Multiple input Links emit stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MultipleInputLinks")));
    TestTrue(
        TEXT("Reversed Link emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.LinkDirection")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRContractFieldsTest,
    "Sekiro.AnimGraphIR.ContractFields",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Blueprint、Graph、Node、Pin 与 Property 的必填契约字段会被统一拒绝。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRContractFieldsTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.SchemaVersion = 3;
    Blueprint.SourceModule.Reset();
    Blueprint.ParentAnimInstanceClass.Reset();

    FSekiroAnimIRGraph& Graph = Blueprint.Layers[0].Graphs[0];
    Graph.GraphType = NAME_None;
    FSekiroAnimIRNode& Node = Graph.Nodes[0];
    Node.NodeType = NAME_None;
    Node.Pins[0].DataType = NAME_None;
    Node.Properties.AddDefaulted();
    FSekiroAnimIRProperty& FirstNamedProperty = Node.Properties.AddDefaulted_GetRef();
    FirstNamedProperty.Name = TEXT("Asset");
    FSekiroAnimIRProperty& DuplicateNamedProperty = Node.Properties.AddDefaulted_GetRef();
    DuplicateNamedProperty.Name = TEXT("Asset");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Missing contract fields are invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(TEXT("Schema version is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.UnsupportedSchemaVersion")));
    TestTrue(TEXT("Source module is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptySourceModule")));
    TestTrue(TEXT("Parent class is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MissingParentAnimInstanceClass")));
    TestTrue(TEXT("Graph type is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyGraphType")));
    TestTrue(TEXT("Node type is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyNodeType")));
    TestTrue(TEXT("Pin data type is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyPinDataType")));
    TestTrue(TEXT("Empty Property name is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyPropertyName")));
    TestTrue(TEXT("Duplicate Property name is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.DuplicatePropertyName")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRParallelTransitionTest,
    "Sekiro.AnimGraphIR.ParallelTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证相同 Source/Target 状态对允许声明多个具有不同 Key、规则名和优先级的 Transition。
 * 测试仅操作内存 IR，不访问 UObject 或磁盘。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRParallelTransitionTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddStatePoseGraph(
        Blueprint,
        TEXT("Graph.Move"),
        TEXT("Node.Move.Result"));

    FSekiroAnimIRGraph& StateMachineGraph = Blueprint.Layers[0].Graphs[1];
    FSekiroAnimIRState& MoveState = StateMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    MoveState.Id = TEXT("State.Move");
    MoveState.Name = TEXT("Move");
    MoveState.GraphId = TEXT("Graph.Move");
    MoveState.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRTransition& FastTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    FastTransition.Id = TEXT("Transition.Graph.StateMachine.FastMove");
    FastTransition.Key = TEXT("FastMove");
    FastTransition.SourceStateId = TEXT("State.Idle");
    FastTransition.TargetStateId = TEXT("State.Move");
    FastTransition.RuleFunctionName = TEXT("CanEnter_FastMove");
    FastTransition.Settings.PriorityOrder = 0;

    FSekiroAnimIRTransition& NormalTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    NormalTransition.Id = TEXT("Transition.Graph.StateMachine.NormalMove");
    NormalTransition.Key = TEXT("NormalMove");
    NormalTransition.SourceStateId = TEXT("State.Idle");
    NormalTransition.TargetStateId = TEXT("State.Move");
    NormalTransition.RuleFunctionName = TEXT("CanEnter_NormalMove");
    NormalTransition.Settings.PriorityOrder = 1;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Parallel Transitions with distinct Keys are valid"),
        USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestEqual(TEXT("Valid parallel Transitions have no diagnostics"), Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRDuplicateTransitionKeyTest,
    "Sekiro.AnimGraphIR.DuplicateTransitionKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同一状态机内的 Transition Key 必须唯一，即使 Transition ID 与规则函数名不同。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRDuplicateTransitionKeyTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& StateMachineGraph = Blueprint.Layers[0].Graphs[1];

    FSekiroAnimIRTransition& FirstTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    FirstTransition.Id = TEXT("Transition.DuplicateKey.First");
    FirstTransition.Key = TEXT("IdleSelf");
    FirstTransition.SourceStateId = TEXT("State.Idle");
    FirstTransition.TargetStateId = TEXT("State.Idle");
    FirstTransition.RuleFunctionName = TEXT("CanEnter_IdleSelf_First");
    FirstTransition.Settings.PriorityOrder = 0;

    FSekiroAnimIRTransition& SecondTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    SecondTransition.Id = TEXT("Transition.DuplicateKey.Second");
    SecondTransition.Key = TEXT("IdleSelf");
    SecondTransition.SourceStateId = TEXT("State.Idle");
    SecondTransition.TargetStateId = TEXT("State.Idle");
    SecondTransition.RuleFunctionName = TEXT("CanEnter_IdleSelf_Second");
    SecondTransition.Settings.PriorityOrder = 1;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Duplicate Transition Key is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Duplicate Transition Key emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.DuplicateTransitionKey")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRTransitionIdentifierTest,
    "Sekiro.AnimGraphIR.TransitionIdentifier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Transition Key 与 RuleFunctionName 必须使用非空、非保留字的 ASCII Lua 标识符。
 * 测试同时覆盖空 Key、非法 Key 和非法规则函数名，不访问 Lua VM。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRTransitionIdentifierTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& StateMachineGraph = Blueprint.Layers[0].Graphs[1];

    FSekiroAnimIRTransition& EmptyKeyTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    EmptyKeyTransition.Id = TEXT("Transition.EmptyKey");
    EmptyKeyTransition.SourceStateId = TEXT("State.Idle");
    EmptyKeyTransition.TargetStateId = TEXT("State.Idle");
    EmptyKeyTransition.RuleFunctionName = TEXT("CanEnter_EmptyKey");
    EmptyKeyTransition.Settings.PriorityOrder = 0;

    FSekiroAnimIRTransition& InvalidIdentifierTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    InvalidIdentifierTransition.Id = TEXT("Transition.InvalidIdentifier");
    InvalidIdentifierTransition.Key = TEXT("bad-key");
    InvalidIdentifierTransition.SourceStateId = TEXT("State.Idle");
    InvalidIdentifierTransition.TargetStateId = TEXT("State.Idle");
    InvalidIdentifierTransition.RuleFunctionName = TEXT("CanEnter-Bad");
    InvalidIdentifierTransition.Settings.PriorityOrder = 1;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid Transition identifiers are rejected"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Empty Transition Key emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyTransitionKey")));
    TestTrue(
        TEXT("Invalid Transition Key emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTransitionKey")));
    TestTrue(
        TEXT("Invalid RuleFunctionName emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTransitionRuleFunctionName")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRDuplicateTransitionRuleFunctionTest,
    "Sekiro.AnimGraphIR.DuplicateTransitionRuleFunction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证不同 StateMachine Graph 也不能复用同一个 Transition RuleFunctionName。
 * 测试构造合法嵌套状态机，仅以重复规则函数名破坏 Blueprint 模块级身份约束。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRDuplicateTransitionRuleFunctionTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRGraph& OuterMachineGraph = Blueprint.Layers[0].Graphs[1];
    FSekiroAnimIRTransition& OuterTransition = OuterMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    OuterTransition.Id = TEXT("Transition.Outer.SharedRule");
    OuterTransition.Key = TEXT("OuterSharedRule");
    OuterTransition.SourceStateId = TEXT("State.Idle");
    OuterTransition.TargetStateId = TEXT("State.Idle");
    OuterTransition.RuleFunctionName = TEXT("CanEnter_SharedRule");

    FSekiroAnimIRGraph& IdleGraph = Blueprint.Layers[0].Graphs[2];
    const FString NestedMachineGraphId(TEXT("Graph.RuleNestedMachine"));
    FSekiroAnimIRNode& NestedMachineNode = IdleGraph.Nodes.AddDefaulted_GetRef();
    NestedMachineNode.Id = TEXT("Node.Idle.RuleNestedMachine");
    NestedMachineNode.NodeType = SekiroAnimGraphIRNames::StateMachineNode;
    NestedMachineNode.DisplayName = TEXT("Rule Nested Machine");
    NestedMachineNode.OwnedGraphId = NestedMachineGraphId;
    NestedMachineNode.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRPin& NestedPosePin = NestedMachineNode.Pins.AddDefaulted_GetRef();
    NestedPosePin.Name = TEXT("Pose");
    NestedPosePin.Direction = ESekiroAnimIRPinDirection::Output;
    NestedPosePin.DataType = SekiroAnimGraphIRNames::PoseData;
    NestedPosePin.bAllowMultipleConnections = true;

    FSekiroAnimIRLink& NestedOutputLink = IdleGraph.Links.AddDefaulted_GetRef();
    NestedOutputLink.Id = TEXT("Link.RuleNestedMachineToIdleResult");
    NestedOutputLink.Source.NodeId = NestedMachineNode.Id;
    NestedOutputLink.Source.PinName = TEXT("Pose");
    NestedOutputLink.Target.NodeId = IdleGraph.RootNodeId;
    NestedOutputLink.Target.PinName = TEXT("Result");

    FSekiroAnimIRGraph& NestedMachineGraph = Blueprint.Layers[0].Graphs.AddDefaulted_GetRef();
    NestedMachineGraph.Id = NestedMachineGraphId;
    NestedMachineGraph.Name = TEXT("RuleNestedMachine");
    NestedMachineGraph.GraphType = SekiroAnimGraphIRNames::StateMachineGraph;
    NestedMachineGraph.StateMachine.EntryStateId = TEXT("State.RuleNestedIdle");
    NestedMachineGraph.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRState& NestedState = NestedMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    NestedState.Id = NestedMachineGraph.StateMachine.EntryStateId;
    NestedState.Name = TEXT("RuleNestedIdle");
    NestedState.GraphId = TEXT("Graph.RuleNestedIdle");
    NestedState.SourceLocation = Blueprint.SourceLocation;

    FSekiroAnimIRTransition& NestedTransition = NestedMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    NestedTransition.Id = TEXT("Transition.Nested.SharedRule");
    NestedTransition.Key = TEXT("NestedSharedRule");
    NestedTransition.SourceStateId = NestedState.Id;
    NestedTransition.TargetStateId = NestedState.Id;
    NestedTransition.RuleFunctionName = TEXT("CanEnter_SharedRule");

    SekiroAnimGraphIRTests::AddStatePoseGraph(
        Blueprint,
        TEXT("Graph.RuleNestedIdle"),
        TEXT("Node.RuleNestedIdle.Result"));

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Duplicate module-level RuleFunctionName is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Duplicate module-level RuleFunctionName emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.DuplicateTransitionRuleFunctionName")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRTransitionSettingsTest,
    "Sekiro.AnimGraphIR.TransitionSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同源 Transition 的显式优先级不可冲突，混合时长不可为负数。
 * 测试仅操作内存 IR，无外部副作用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRTransitionSettingsTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddStatePoseGraph(
        Blueprint,
        TEXT("Graph.Move"),
        TEXT("Node.Move.Result"));
    FSekiroAnimIRGraph& StateMachineGraph = Blueprint.Layers[0].Graphs[1];
    FSekiroAnimIRState& MoveState = StateMachineGraph.StateMachine.States.AddDefaulted_GetRef();
    MoveState.Id = TEXT("State.Move");
    MoveState.GraphId = TEXT("Graph.Move");

    FSekiroAnimIRTransition& FirstTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    FirstTransition.Id = TEXT("Transition.IdleToMoveA");
    FirstTransition.Key = TEXT("IdleToMoveA");
    FirstTransition.SourceStateId = TEXT("State.Idle");
    FirstTransition.TargetStateId = TEXT("State.Move");
    FirstTransition.RuleFunctionName = TEXT("CanEnter_Idle_Move_A");
    FirstTransition.Settings.PriorityOrder = 0;
    FirstTransition.Settings.BlendDuration = -0.1f;

    FSekiroAnimIRTransition& SecondTransition = StateMachineGraph.StateMachine.Transitions.AddDefaulted_GetRef();
    SecondTransition.Id = TEXT("Transition.IdleToMoveB");
    SecondTransition.Key = TEXT("IdleToMoveB");
    SecondTransition.SourceStateId = TEXT("State.Idle");
    SecondTransition.TargetStateId = TEXT("State.Move");
    SecondTransition.RuleFunctionName = TEXT("CanEnter_Idle_Move_B");
    SecondTransition.Settings.PriorityOrder = 0;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid Transition settings are rejected"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(TEXT("Negative blend duration is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTransitionBlendDuration")));
    TestTrue(TEXT("Duplicate transition priority is validated"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.DuplicateTransitionPriority")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRNodeRegistryTest,
    "Sekiro.AnimGraphIR.NodeRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证内置 NodeType 注册表公开稳定只读契约，并拒绝未知查询。
 * 测试不加载软类路径指向的 UObject，可由 Automation Framework 在编辑器测试线程调用。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRNodeRegistryTest::RunTest(const FString& Parameters)
{
    const TConstArrayView<FSekiroAnimIRNodeContract> Contracts =
        FSekiroAnimGraphNodeRegistry::GetContracts();
    TestEqual(TEXT("Registry exposes twenty-one built-in NodeTypes"), Contracts.Num(), 21);

    const FSekiroAnimIRNodeContract* OutputPose =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::OutputPoseNode);
    TestNotNull(TEXT("OutputPose contract is registered"), OutputPose);
    if (OutputPose)
    {
        TestEqual(
            TEXT("OutputPose keeps the native editor node class path"),
            OutputPose->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_Root")));
        TestEqual(TEXT("OutputPose is root-only"), OutputPose->RootRole, ESekiroAnimIRNodeRootRole::GraphRoot);
        TestEqual(TEXT("OutputPose has one registered Pin"), OutputPose->Pins.Num(), 1);
        if (!OutputPose->Pins.IsEmpty())
        {
            TestFalse(TEXT("OutputPose Result input is single-connect"), OutputPose->Pins[0].bAllowMultipleConnections);
        }
    }

    const FSekiroAnimIRNodeContract* StateMachine =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::StateMachineNode);
    TestNotNull(TEXT("StateMachine contract is registered"), StateMachine);
    if (StateMachine && !StateMachine->Pins.IsEmpty())
    {
        TestTrue(TEXT("StateMachine Pose output supports fan-out"), StateMachine->Pins[0].bAllowMultipleConnections);
        TestEqual(
            TEXT("StateMachine requires an owned Graph"),
            StateMachine->OwnedGraphPolicy,
            ESekiroAnimIROwnedGraphPolicy::Required);
    }

    const FSekiroAnimIRNodeContract* SequencePlayer =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::SequencePlayerNode);
    TestNotNull(TEXT("SequencePlayer contract is registered"), SequencePlayer);
    bool bFoundRequiredSequence = false;
    if (SequencePlayer)
    {
        for (const FSekiroAnimIRPropertyContract& Property : SequencePlayer->Properties)
        {
            if (Property.Name == TEXT("Sequence")
                && Property.ValueType == ESekiroAnimIRValueType::SoftObjectPath
                && Property.bRequired)
            {
                bFoundRequiredSequence = true;
            }
        }
    }
    TestTrue(TEXT("SequencePlayer requires a SoftObjectPath Sequence property"), bFoundRequiredSequence);

    const FSekiroAnimIRNodeContract* LocalToComponent =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::LocalToComponentSpaceNode);
    TestNotNull(TEXT("LocalToComponentSpace contract is registered"), LocalToComponent);
    if (LocalToComponent)
    {
        TestEqual(TEXT("LocalToComponentSpace has two Pins"), LocalToComponent->Pins.Num(), 2);
        if (LocalToComponent->Pins.Num() == 2)
        {
            TestEqual(
                TEXT("LocalToComponentSpace consumes local Pose"),
                LocalToComponent->Pins[0].DataType,
                SekiroAnimGraphIRNames::PoseData);
            TestEqual(
                TEXT("LocalToComponentSpace produces component Pose"),
                LocalToComponent->Pins[1].DataType,
                SekiroAnimGraphIRNames::ComponentPoseData);
        }
    }

    const FSekiroAnimIRNodeContract* ComponentToLocal =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::ComponentToLocalSpaceNode);
    TestNotNull(TEXT("ComponentToLocalSpace contract is registered"), ComponentToLocal);
    if (ComponentToLocal && ComponentToLocal->Pins.Num() == 2)
    {
        TestEqual(
            TEXT("ComponentToLocalSpace consumes component Pose"),
            ComponentToLocal->Pins[0].DataType,
            SekiroAnimGraphIRNames::ComponentPoseData);
        TestEqual(
            TEXT("ComponentToLocalSpace produces local Pose"),
            ComponentToLocal->Pins[1].DataType,
            SekiroAnimGraphIRNames::PoseData);
    }

    const FSekiroAnimIRNodeContract* OrientationWarping =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::OrientationWarpingNode);
    TestNotNull(TEXT("OrientationWarping contract is registered"), OrientationWarping);
    if (OrientationWarping)
    {
        TestEqual(
            TEXT("OrientationWarping keeps the AnimationWarping editor class path"),
            OrientationWarping->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_OrientationWarping")));
        TestEqual(TEXT("OrientationWarping has five Pins"), OrientationWarping->Pins.Num(), 5);
        TestEqual(TEXT("OrientationWarping has twelve properties"), OrientationWarping->Properties.Num(), 12);
        if (OrientationWarping->Pins.Num() == 5)
        {
            TestEqual(
                TEXT("OrientationWarping input uses component Pose type"),
                OrientationWarping->Pins[0].DataType,
                SekiroAnimGraphIRNames::ComponentPoseData);
            TestEqual(
                TEXT("OrientationWarping exposes Graph locomotion angle"),
                OrientationWarping->Pins[2].Name,
                FString(TEXT("LocomotionAngle")));
            TestEqual(
                TEXT("OrientationWarping locomotion angle uses Float type"),
                OrientationWarping->Pins[2].DataType,
                SekiroAnimGraphIRNames::FloatData);
            TestEqual(
                TEXT("OrientationWarping output uses component Pose type"),
                OrientationWarping->Pins[4].DataType,
                SekiroAnimGraphIRNames::ComponentPoseData);
        }
        TestEqual(
            TEXT("OrientationWarping exposes evaluation Mode"),
            OrientationWarping->Properties[6].Name,
            FName(TEXT("Mode")));
        TestEqual(
            TEXT("OrientationWarping exposes Graph minimum root motion speed"),
            OrientationWarping->Properties[7].Name,
            FName(TEXT("MinRootMotionSpeedThreshold")));
        TestEqual(
            TEXT("OrientationWarping exposes Graph warping alpha"),
            OrientationWarping->Properties[9].Name,
            FName(TEXT("WarpingAlpha")));
    }

    const FSekiroAnimIRNodeContract* FootPlacement =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::FootPlacementNode);
    TestNotNull(TEXT("FootPlacement contract is registered"), FootPlacement);
    if (FootPlacement)
    {
        TestEqual(
            TEXT("FootPlacement keeps the AnimationWarping editor class path"),
            FootPlacement->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimationWarpingEditor.AnimGraphNode_FootPlacement")));
        TestEqual(TEXT("FootPlacement has three Pins"), FootPlacement->Pins.Num(), 3);
        TestEqual(TEXT("FootPlacement has fourteen properties"), FootPlacement->Properties.Num(), 14);
        TestEqual(
            TEXT("FootPlacement consumes component Pose"),
            FootPlacement->Pins[0].DataType,
            SekiroAnimGraphIRNames::ComponentPoseData);
        TestEqual(
            TEXT("FootPlacement exposes Float Alpha"),
            FootPlacement->Pins[1].DataType,
            SekiroAnimGraphIRNames::FloatData);
        TestEqual(
            TEXT("FootPlacement produces component Pose"),
            FootPlacement->Pins[2].DataType,
            SekiroAnimGraphIRNames::ComponentPoseData);
        TestTrue(TEXT("FootPlacement IK root is required"), FootPlacement->Properties[0].bRequired);
        TestTrue(TEXT("FootPlacement pelvis is required"), FootPlacement->Properties[1].bRequired);
        TestTrue(TEXT("FootPlacement legs are required"), FootPlacement->Properties[2].bRequired);
        TestEqual(
            TEXT("FootPlacement exposes optional Name PlantLockType"),
            FootPlacement->Properties[4].Name,
            FName(TEXT("PlantLockType")));
        TestEqual(
            TEXT("FootPlacement PlantLockType uses Name storage"),
            FootPlacement->Properties[4].ValueType,
            ESekiroAnimIRValueType::Name);
        TestFalse(
            TEXT("FootPlacement PlantLockType is optional"),
            FootPlacement->Properties[4].bRequired);
    }

    const FSekiroAnimIRNodeContract* LegIK =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::LegIKNode);
    TestNotNull(TEXT("LegIK contract is registered"), LegIK);
    if (LegIK)
    {
        TestEqual(
            TEXT("LegIK keeps the AnimGraph editor class path"),
            LegIK->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_LegIK")));
        TestEqual(TEXT("LegIK has three Pins"), LegIK->Pins.Num(), 3);
        TestEqual(TEXT("LegIK has three properties"), LegIK->Properties.Num(), 3);
        TestEqual(
            TEXT("LegIK MaxIterations uses integer storage"),
            LegIK->Properties[2].ValueType,
            ESekiroAnimIRValueType::Integer);
        TestTrue(TEXT("LegIK legs are required"), LegIK->Properties[0].bRequired);
    }

    const FSekiroAnimIRNodeContract* TwoBoneIK =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::TwoBoneIKNode);
    TestNotNull(TEXT("TwoBoneIK contract is registered"), TwoBoneIK);
    if (TwoBoneIK)
    {
        TestEqual(
            TEXT("TwoBoneIK keeps the AnimGraph editor class path"),
            TwoBoneIK->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_TwoBoneIK")));
        TestEqual(TEXT("TwoBoneIK has three Pins"), TwoBoneIK->Pins.Num(), 3);
        TestEqual(TEXT("TwoBoneIK has nineteen properties"), TwoBoneIK->Properties.Num(), 19);
        TestEqual(
            TEXT("TwoBoneIK consumes component Pose"),
            TwoBoneIK->Pins[0].DataType,
            SekiroAnimGraphIRNames::ComponentPoseData);
        TestEqual(
            TEXT("TwoBoneIK exposes Float Alpha"),
            TwoBoneIK->Pins[1].DataType,
            SekiroAnimGraphIRNames::FloatData);
        TestEqual(
            TEXT("TwoBoneIK produces component Pose"),
            TwoBoneIK->Pins[2].DataType,
            SekiroAnimGraphIRNames::ComponentPoseData);
        TestTrue(TEXT("TwoBoneIK IKBone is required"), TwoBoneIK->Properties[0].bRequired);
        TestEqual(
            TEXT("TwoBoneIK exposes Effector Socket targets"),
            TwoBoneIK->Properties[3].Name,
            FName(TEXT("EffectorTargetSocketName")));
        TestEqual(
            TEXT("TwoBoneIK exposes AlphaCurveName"),
            TwoBoneIK->Properties[8].Name,
            FName(TEXT("AlphaCurveName")));
    }

    const FSekiroAnimIRNodeContract* SaveCachedPose =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::SaveCachedPoseNode);
    TestNotNull(TEXT("SaveCachedPose contract is registered"), SaveCachedPose);
    if (SaveCachedPose)
    {
        TestEqual(
            TEXT("SaveCachedPose keeps the UE5.2 editor node class path"),
            SaveCachedPose->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_SaveCachedPose")));
        TestEqual(TEXT("SaveCachedPose has one input Pin"), SaveCachedPose->Pins.Num(), 1);
        if (!SaveCachedPose->Pins.IsEmpty())
        {
            TestEqual(TEXT("SaveCachedPose Pin is Pose"), SaveCachedPose->Pins[0].Name, FString(TEXT("Pose")));
            TestEqual(
                TEXT("SaveCachedPose Pose Pin is input"),
                SaveCachedPose->Pins[0].Direction,
                ESekiroAnimIRPinDirection::Input);
        }
        TestEqual(TEXT("SaveCachedPose has one property"), SaveCachedPose->Properties.Num(), 1);
        if (!SaveCachedPose->Properties.IsEmpty())
        {
            TestEqual(TEXT("SaveCachedPose property is CacheName"), SaveCachedPose->Properties[0].Name, FName(TEXT("CacheName")));
            TestEqual(
                TEXT("SaveCachedPose CacheName is String"),
                SaveCachedPose->Properties[0].ValueType,
                ESekiroAnimIRValueType::String);
            TestTrue(TEXT("SaveCachedPose CacheName is required"), SaveCachedPose->Properties[0].bRequired);
        }
    }

    const FSekiroAnimIRNodeContract* UseCachedPose =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::UseCachedPoseNode);
    TestNotNull(TEXT("UseCachedPose contract is registered"), UseCachedPose);
    if (UseCachedPose)
    {
        TestEqual(
            TEXT("UseCachedPose keeps the UE5.2 editor node class path"),
            UseCachedPose->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_UseCachedPose")));
        TestEqual(TEXT("UseCachedPose has one output Pin"), UseCachedPose->Pins.Num(), 1);
        if (!UseCachedPose->Pins.IsEmpty())
        {
            TestEqual(TEXT("UseCachedPose Pin is Pose"), UseCachedPose->Pins[0].Name, FString(TEXT("Pose")));
            TestEqual(
                TEXT("UseCachedPose Pose Pin is output"),
                UseCachedPose->Pins[0].Direction,
                ESekiroAnimIRPinDirection::Output);
            TestTrue(TEXT("UseCachedPose Pose output supports fan-out"), UseCachedPose->Pins[0].bAllowMultipleConnections);
        }
        TestEqual(TEXT("UseCachedPose has one property"), UseCachedPose->Properties.Num(), 1);
        if (!UseCachedPose->Properties.IsEmpty())
        {
            TestEqual(TEXT("UseCachedPose property is CacheName"), UseCachedPose->Properties[0].Name, FName(TEXT("CacheName")));
            TestEqual(
                TEXT("UseCachedPose CacheName is String"),
                UseCachedPose->Properties[0].ValueType,
                ESekiroAnimIRValueType::String);
            TestTrue(TEXT("UseCachedPose CacheName is required"), UseCachedPose->Properties[0].bRequired);
        }
    }

    const FSekiroAnimIRNodeContract* Slot =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::SlotNode);
    TestNotNull(TEXT("Slot contract is registered"), Slot);
    if (Slot)
    {
        TestEqual(
            TEXT("Slot keeps the UE5.2 editor node class path"),
            Slot->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_Slot")));
        TestEqual(TEXT("Slot has Source and Pose Pins"), Slot->Pins.Num(), 2);
        TestEqual(TEXT("Slot has two properties"), Slot->Properties.Num(), 2);
        if (!Slot->Properties.IsEmpty())
        {
            TestEqual(TEXT("Slot property is SlotName"), Slot->Properties[0].Name, FName(TEXT("SlotName")));
            TestTrue(TEXT("SlotName is required"), Slot->Properties[0].bRequired);
        }
    }

    const FSekiroAnimIRNodeContract* LayeredBlend =
        FSekiroAnimGraphNodeRegistry::Find(SekiroAnimGraphIRNames::LayeredBlendPerBoneNode);
    TestNotNull(TEXT("LayeredBlendPerBone contract is registered"), LayeredBlend);
    if (LayeredBlend)
    {
        TestEqual(
            TEXT("LayeredBlendPerBone keeps the UE5.2 editor node class path"),
            LayeredBlend->EditorNodeClassPath.ToString(),
            FString(TEXT("/Script/AnimGraph.AnimGraphNode_LayeredBoneBlend")));
        TestEqual(TEXT("LayeredBlendPerBone has four Pins"), LayeredBlend->Pins.Num(), 4);
        TestEqual(TEXT("LayeredBlendPerBone has five properties"), LayeredBlend->Properties.Num(), 5);
        if (LayeredBlend->Pins.Num() == 4 && !LayeredBlend->Properties.IsEmpty())
        {
            TestEqual(
                TEXT("LayeredBlendPerBone exposes Float BlendWeight"),
                LayeredBlend->Pins[2].DataType,
                SekiroAnimGraphIRNames::FloatData);
            TestEqual(
                TEXT("LayeredBlendPerBone property is BranchFilters"),
                LayeredBlend->Properties[0].Name,
                FName(TEXT("BranchFilters")));
            TestTrue(TEXT("BranchFilters is required"), LayeredBlend->Properties[0].bRequired);
        }
    }

    TestNull(TEXT("Unknown NodeType is not synthesized"), FSekiroAnimGraphNodeRegistry::Find(TEXT("Unknown")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRUnknownNodeTypeTest,
    "Sekiro.AnimGraphIR.UnknownNodeType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua IR 不能声明注册表之外的 NodeType。
 * 测试仅操作内存 IR，不加载节点类或动画资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRUnknownNodeTypeTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].Nodes[1].NodeType = TEXT("UnknownNode");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Unknown NodeType is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Unknown NodeType emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.UnknownNodeType")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRPinRegistryContractTest,
    "Sekiro.AnimGraphIR.PinRegistryContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua Pin 声明必须完整匹配权威注册表，不能省略、增加或改写契约。
 * 测试同时保留原有 Link，以确认 Link 端点不依赖被篡改的 IR Pin 声明。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRPinRegistryContractTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRNode& OutputNode = Blueprint.Layers[0].Graphs[0].Nodes[0];
    OutputNode.Pins.Reset();

    FSekiroAnimIRNode& StateMachineNode = Blueprint.Layers[0].Graphs[0].Nodes[1];
    StateMachineNode.Pins[0].bAllowMultipleConnections = false;
    FSekiroAnimIRPin& Unexpected = StateMachineNode.Pins.AddDefaulted_GetRef();
    Unexpected.Name = TEXT("Unexpected");
    Unexpected.Direction = ESekiroAnimIRPinDirection::Output;
    Unexpected.DataType = SekiroAnimGraphIRNames::PoseData;
    Unexpected.bAllowMultipleConnections = true;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid Pin declarations are rejected"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(TEXT("Missing registered Pin is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MissingRegisteredPin")));
    TestTrue(TEXT("Unexpected Pin is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.UnexpectedPin")));
    TestTrue(TEXT("Pin contract mismatch is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.PinContractMismatch")));
    TestFalse(TEXT("Link still resolves through registry Pins"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.LinkPinNotFound")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRPropertyRegistryContractTest,
    "Sekiro.AnimGraphIR.PropertyRegistryContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua Property 声明必须来自注册表，并满足必填项和显式值类型约束。
 * 测试仅操作内存 IR，不解析或加载 Sequence 软路径。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRPropertyRegistryContractTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.Sequence.Missing"));
    Blueprint.Layers[0].Graphs[0].Nodes.Last().Properties.Reset();

    SekiroAnimGraphIRTests::AddSequencePlayerNode(Blueprint, TEXT("Node.Sequence.Invalid"));
    FSekiroAnimIRNode& InvalidNode = Blueprint.Layers[0].Graphs[0].Nodes.Last();
    InvalidNode.Properties[0].Value.Type = ESekiroAnimIRValueType::Bool;
    FSekiroAnimIRProperty& UnknownProperty = InvalidNode.Properties.AddDefaulted_GetRef();
    UnknownProperty.Name = TEXT("UnknownProperty");
    UnknownProperty.Value.Type = ESekiroAnimIRValueType::String;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Invalid Property declarations are rejected"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(TEXT("Missing required Property is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.MissingRequiredNodeProperty")));
    TestTrue(TEXT("Unknown Property is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.UnknownNodeProperty")));
    TestTrue(TEXT("Property type mismatch is diagnosed"), SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.NodePropertyTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRRootNodeContractTest,
    "Sekiro.AnimGraphIR.RootNodeContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Pose Graph 根节点必须使用注册表指定的 OutputPose，且 root-only 节点不能降为普通节点。
 * 测试仅修改内存 RootNodeId，不加载编辑器节点类。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRRootNodeContractTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].RootNodeId = TEXT("Node.StateMachine");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Wrong root NodeType is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Wrong root NodeType emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.RootNodeTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRUnexpectedOwnedGraphTest,
    "Sekiro.AnimGraphIR.UnexpectedOwnedGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证禁止拥有内部 Graph 的内置节点不能通过 Lua IR 声明 OwnedGraphId。
 * 测试仅修改内存 IR；StateMachine 缺少 OwnedGraph 的既有测试继续覆盖 Required 策略。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRUnexpectedOwnedGraphTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    Blueprint.Layers[0].Graphs[0].Nodes[0].OwnedGraphId = TEXT("Graph.StateMachine");

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestFalse(TEXT("Unexpected OwnedGraphId is invalid"), USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestTrue(
        TEXT("Unexpected OwnedGraphId emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.UnexpectedOwnedGraph")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroAnimGraphIRNativeBoolTransitionRuleTest,
    "Sekiro.AnimGraphIR.NativeBoolTransitionRule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 BoolProperty 可独立构成无 Lua 函数的完整 Transition Rule，并拒绝无规则、空属性名与无来源 LuaBool。
 * 测试仅修改内存 IR，不加载父类、生成蓝图或访问 Lua VM。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成断言收集。
 */
bool FSekiroAnimGraphIRNativeBoolTransitionRuleTest::RunTest(const FString& Parameters)
{
    FSekiroAnimBlueprintIR Blueprint = SekiroAnimGraphIRTests::MakeMinimalIR();
    FSekiroAnimIRTransition& Transition =
        Blueprint.Layers[0].Graphs[1].StateMachine.Transitions.AddDefaulted_GetRef();
    Transition.Id = TEXT("Transition.NativeBool");
    Transition.Key = TEXT("NativeBool");
    Transition.SourceStateId = TEXT("State.Idle");
    Transition.TargetStateId = TEXT("State.Idle");
    Transition.SourceLocation = Blueprint.SourceLocation;
    FSekiroAnimIRTransitionGateNode& BoolProperty = Transition.Gate.Nodes.AddDefaulted_GetRef();
    BoolProperty.Type = TEXT("BoolProperty");
    BoolProperty.Name = TEXT("bCanEnter");
    BoolProperty.bExpectedBool = false;
    Transition.Gate.RootIndex = 0;

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Pure native BoolProperty Transition is valid"),
        USekiroAnimGraphIRLibrary::Validate(Blueprint, Diagnostics));
    TestEqual(TEXT("Pure native BoolProperty has no diagnostics"), Diagnostics.Num(), 0);

    FSekiroAnimBlueprintIR MissingRuleBlueprint = Blueprint;
    MissingRuleBlueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Gate.Nodes.Reset();
    MissingRuleBlueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Gate.RootIndex = INDEX_NONE;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Transition without Lua or native Rule is invalid"),
        USekiroAnimGraphIRLibrary::Validate(MissingRuleBlueprint, Diagnostics));
    TestTrue(
        TEXT("Missing complete Transition Rule emits stable code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.EmptyTransitionRule")));

    FSekiroAnimBlueprintIR EmptyPropertyBlueprint = Blueprint;
    EmptyPropertyBlueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Gate.Nodes[0].Name = NAME_None;
    Diagnostics.Reset();
    TestFalse(
        TEXT("BoolProperty without property name is invalid"),
        USekiroAnimGraphIRLibrary::Validate(EmptyPropertyBlueprint, Diagnostics));
    TestTrue(
        TEXT("Empty BoolProperty name emits Gate code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTransitionGate")));

    FSekiroAnimBlueprintIR NativeLuaBoolBlueprint = Blueprint;
    NativeLuaBoolBlueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Gate.Nodes[0].Type = TEXT("LuaBool");
    NativeLuaBoolBlueprint.Layers[0].Graphs[1].StateMachine.Transitions[0].Gate.Nodes[0].Name = NAME_None;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Pure native Rule cannot contain LuaBool"),
        USekiroAnimGraphIRLibrary::Validate(NativeLuaBoolBlueprint, Diagnostics));
    TestTrue(
        TEXT("LuaBool without RuleFunctionName emits Gate code"),
        SekiroAnimGraphIRTests::HasDiagnosticCode(Diagnostics, TEXT("IR.InvalidTransitionGate")));
    return true;
}

#endif
