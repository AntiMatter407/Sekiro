#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AIGraphNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "LuaBehaviorTreeFactoryLibrary.h"
#include "LuaBehaviorTreeExporterLibrary.h"
#include "LuaBehaviorTreeIRLibrary.h"
#include "LuaBehaviorTreeReflectionWriter.h"
#include "LuaBehaviorTreeTask.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace LuaBehaviorTreeCompilerTests
{
    /**
     * 创建只含 Selector 根与 Wait 叶节点的最小通用 IR。
     * 类路径均来自 AIModule，用于证明编译器没有具体节点注册步骤。
     *
     * @return 可通过结构校验的最小 IR。
     */
    FLuaBehaviorTreeIR MakeMinimalIR()
    {
        FLuaBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.Minimal");
        IR.RootNodeId = TEXT("Node:Root");

        FLuaBehaviorTreeIRNode Root;
        Root.Id = IR.RootNodeId;
        Root.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTComposite_Selector"));
        Root.DisplayName = TEXT("Root");
        Root.DeclarationOrder = 1;
        IR.Nodes.Add(MoveTemp(Root));

        FLuaBehaviorTreeIRNode Wait;
        Wait.Id = TEXT("Node:Wait");
        Wait.ParentId = IR.RootNodeId;
        Wait.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTTask_Wait"));
        Wait.DisplayName = TEXT("Wait");
        Wait.DeclarationOrder = 2;

        FLuaBehaviorTreeIRValue& WaitTime = IR.Values.AddDefaulted_GetRef();
        WaitTime.Type = ELuaBehaviorTreeValueType::Float;
        WaitTime.FloatValue = 0.5;
        FLuaBehaviorTreeIRProperty& WaitTimeProperty = Wait.Properties.AddDefaulted_GetRef();
        WaitTimeProperty.Name = TEXT("WaitTime");
        WaitTimeProperty.ValueIndex = 0;
        IR.Nodes.Add(MoveTemp(Wait));
        return IR;
    }

    /**
     * 创建足以跨越多次 TArray 扩容边界的深层行为树 IR。
     * 每个中间节点只有一个子节点，便于导出后按先序数组直接验证父子链。
     *
     * @param NodeCount 主节点总数；至少为 2，首节点为 Selector，末节点为 Wait。
     * @return 可直接生成内存 BehaviorTree 的深层 IR。
     */
    FLuaBehaviorTreeIR MakeArrayGrowthIR(const int32 NodeCount)
    {
        check(NodeCount >= 2);
        FLuaBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.ArrayGrowth");
        FString ParentId;
        for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
        {
            const FString NodeId = FString::Printf(TEXT("Node:Growth%02d"), NodeIndex);
            FLuaBehaviorTreeIRNode Node;
            Node.Id = NodeId;
            Node.ParentId = ParentId;
            Node.ClassPath = FSoftClassPath(
                NodeIndex == 0
                    ? TEXT("/Script/AIModule.BTComposite_Selector")
                    : NodeIndex + 1 == NodeCount
                        ? TEXT("/Script/AIModule.BTTask_Wait")
                        : TEXT("/Script/AIModule.BTComposite_Sequence"));
            Node.DisplayName = FString::Printf(TEXT("Growth%02d"), NodeIndex);
            Node.DeclarationOrder = NodeIndex + 1;
            if (NodeIndex == 0) IR.RootNodeId = NodeId;
            IR.Nodes.Add(MoveTemp(Node));
            ParentId = NodeId;
        }
        return IR;
    }

    /**
     * 查询诊断数组是否包含指定稳定错误码。
     *
     * @param Diagnostics 待查询诊断。
     * @param Code 目标错误码。
     * @return 至少存在一条匹配诊断时返回 true。
     */
    bool ContainsCode(
        const TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics,
        const FName Code)
    {
        for (const FLuaBehaviorTreeDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Code == Code) return true;
        }
        return false;
    }

    /**
     * 创建包含四层、不均匀兄弟子树、RunBehavior 叶节点和附属节点的布局测试 IR。
     * 类型仅通过通用 ClassPath 声明，不要求工厂注册任何具体节点。
     *
     * @return 可直接交给 GenerateFromIR 的合法布局测试 IR。
     */
    FLuaBehaviorTreeIR MakeUnevenLayoutIR()
    {
        FLuaBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.UnevenLayout");
        IR.RootNodeId = TEXT("Root");

        const auto AddNode = [&IR](
            const TCHAR* Id,
            const TCHAR* ParentId,
            const TCHAR* ClassPath,
            const int32 DeclarationOrder)
        {
            FLuaBehaviorTreeIRNode Node;
            Node.Id = Id;
            Node.ParentId = ParentId;
            Node.ClassPath = FSoftClassPath(ClassPath);
            Node.DeclarationOrder = DeclarationOrder;
            return IR.Nodes.Add(MoveTemp(Node));
        };

        AddNode(
            TEXT("Root"),
            TEXT(""),
            TEXT("/Script/AIModule.BTComposite_Selector"),
            1);
        AddNode(
            TEXT("BranchA"),
            TEXT("Root"),
            TEXT("/Script/AIModule.BTComposite_Sequence"),
            10);
        const int32 LeafAIndex = AddNode(
            TEXT("LeafA"),
            TEXT("BranchA"),
            TEXT("/Script/AIModule.BTTask_Wait"),
            11);
        AddNode(
            TEXT("BranchA2"),
            TEXT("BranchA"),
            TEXT("/Script/AIModule.BTComposite_Sequence"),
            12);
        AddNode(
            TEXT("RunBehavior"),
            TEXT("BranchA2"),
            TEXT("/Script/AIModule.BTTask_RunBehavior"),
            13);
        const int32 MoveToIndex = AddNode(
            TEXT("MoveTo"),
            TEXT("BranchA2"),
            TEXT("/Script/LuaBehaviorTree.LuaBehaviorTreeTask"),
            14);
        AddNode(
            TEXT("BranchB"),
            TEXT("Root"),
            TEXT("/Script/AIModule.BTComposite_Sequence"),
            20);
        AddNode(
            TEXT("LeafB"),
            TEXT("BranchB"),
            TEXT("/Script/AIModule.BTTask_Wait"),
            21);

        FLuaBehaviorTreeIRNode DecoratorA;
        DecoratorA.Id = TEXT("DecoratorA");
        DecoratorA.ParentId = TEXT("LeafA");
        DecoratorA.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTDecorator_Loop"));
        DecoratorA.DeclarationOrder = 100;
        IR.Decorators.Add(MoveTemp(DecoratorA));

        FLuaBehaviorTreeIRNode DecoratorB;
        DecoratorB.Id = TEXT("DecoratorB");
        DecoratorB.ParentId = TEXT("LeafA");
        DecoratorB.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTDecorator_ForceSuccess"));
        DecoratorB.DeclarationOrder = 101;
        IR.Decorators.Add(MoveTemp(DecoratorB));

        FLuaBehaviorTreeIRNode Service;
        Service.Id = TEXT("ServiceRoot");
        Service.ParentId = TEXT("Root");
        Service.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTService_DefaultFocus"));
        Service.DeclarationOrder = 102;
        IR.Services.Add(MoveTemp(Service));

        FLuaBehaviorTreeIRValue& WaitTime = IR.Values.AddDefaulted_GetRef();
        WaitTime.Type = ELuaBehaviorTreeValueType::Float;
        WaitTime.FloatValue = 0.75;
        FLuaBehaviorTreeIRProperty& WaitTimeProperty =
            IR.Nodes[LeafAIndex].Properties.AddDefaulted_GetRef();
        WaitTimeProperty.Name = TEXT("WaitTime");
        WaitTimeProperty.ValueIndex = 0;
        WaitTimeProperty.DeclarationOrder = 1;

        FLuaBehaviorTreeIRValue& LuaModule = IR.Values.AddDefaulted_GetRef();
        LuaModule.Type = ELuaBehaviorTreeValueType::String;
        LuaModule.StringValue = TEXT("AI.Tasks.ExportExample");
        FLuaBehaviorTreeIRProperty& LuaModuleProperty =
            IR.Nodes[MoveToIndex].Properties.AddDefaulted_GetRef();
        LuaModuleProperty.Name = TEXT("LuaModuleName");
        LuaModuleProperty.ValueIndex = 1;
        LuaModuleProperty.DeclarationOrder = 1;

        FLuaBehaviorTreeIRValue& Configuration = IR.Values.AddDefaulted_GetRef();
        Configuration.Type = ELuaBehaviorTreeValueType::String;
        Configuration.StringValue = TEXT("line1\n\"quoted\"\\path");
        FLuaBehaviorTreeIRProperty& ConfigurationProperty =
            IR.Nodes[MoveToIndex].Properties.AddDefaulted_GetRef();
        ConfigurationProperty.Name = TEXT("Configuration");
        ConfigurationProperty.ValueIndex = 2;
        ConfigurationProperty.DeclarationOrder = 2;

        FLuaBlackboardIRKey& Key = IR.BlackboardKeys.AddDefaulted_GetRef();
        Key.Id = TEXT("BlackboardKey:bExportEnabled");
        Key.Name = TEXT("bExportEnabled");
        Key.ClassPath = FSoftClassPath(
            TEXT("/Script/AIModule.BlackboardKeyType_Bool"));
        Key.bInstanceSynced = true;
        Key.DeclarationOrder = 103;
        return IR;
    }

    /**
     * 按 IR 声明顺序把生成 Graph 的主节点坐标映射回稳定 ID，并读取虚拟 Root 坐标。
     * Graph 必须由当前工厂生成；函数只读取内存对象，不修改资产。
     *
     * @param BehaviorTree 已生成且持有 BTGraph 的内存行为树。
     * @param IR 生成该 Graph 时使用的 IR。
     * @param OutPositions 接收主节点 ID 到左上角坐标的映射。
     * @param OutVirtualRoot 接收编辑器虚拟 Root 左上角坐标。
     * @return 主节点数量与 IR 完全一致且找到虚拟 Root 时返回 true。
     */
    bool CaptureLayoutPositions(
        const UBehaviorTree* BehaviorTree,
        const FLuaBehaviorTreeIR& IR,
        TMap<FString, FIntPoint>& OutPositions,
        FIntPoint& OutVirtualRoot)
    {
        OutPositions.Reset();
        OutVirtualRoot = FIntPoint::ZeroValue;
        const UEdGraph* Graph = BehaviorTree ? BehaviorTree->BTGraph : nullptr;
        if (!Graph) return false;

        TArray<const FLuaBehaviorTreeIRNode*> SortedNodes;
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
            SortedNodes.Add(&Node);
        SortedNodes.Sort(
            [](const FLuaBehaviorTreeIRNode& Left,
                const FLuaBehaviorTreeIRNode& Right)
            {
                if (Left.DeclarationOrder != Right.DeclarationOrder)
                    return Left.DeclarationOrder < Right.DeclarationOrder;
                return Left.Id < Right.Id;
            });

        TArray<const UAIGraphNode*> MainGraphNodes;
        bool bFoundVirtualRoot = false;
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const UAIGraphNode* AIGraphNode = Cast<UAIGraphNode>(Node);
            if (!AIGraphNode) continue;
            if (AIGraphNode->GetClass()->GetPathName()
                == TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Root"))
            {
                OutVirtualRoot = FIntPoint(
                    AIGraphNode->NodePosX,
                    AIGraphNode->NodePosY);
                bFoundVirtualRoot = true;
                continue;
            }
            const UBTNode* NodeInstance = Cast<UBTNode>(AIGraphNode->NodeInstance);
            if (NodeInstance
                && (NodeInstance->IsA<UBTCompositeNode>()
                    || NodeInstance->IsA<UBTTaskNode>()))
            {
                MainGraphNodes.Add(AIGraphNode);
            }
        }
        if (!bFoundVirtualRoot || MainGraphNodes.Num() != SortedNodes.Num())
            return false;
        for (int32 NodeIndex = 0; NodeIndex < SortedNodes.Num(); ++NodeIndex)
        {
            OutPositions.Add(
                SortedNodes[NodeIndex]->Id,
                FIntPoint(
                    MainGraphNodes[NodeIndex]->NodePosX,
                    MainGraphNodes[NodeIndex]->NodePosY));
        }
        return true;
    }

    /**
     * 根据测试 IR 使用的通用视觉占用规则计算一个节点的水平中心。
     * 本函数仅供断言读取，不修改 IR 或 Graph。
     *
     * @param IR 布局测试 IR。
     * @param NodeId 目标主节点 ID。
     * @param Position 目标主节点左上角 Graph 坐标。
     * @return 估算视觉轮廓中心 X；节点不存在时返回 Position.X。
     */
    double GetLayoutCenterX(
        const FLuaBehaviorTreeIR& IR,
        const FString& NodeId,
        const FIntPoint& Position)
    {
        int32 AttachedNodeCount = 0;
        for (const FLuaBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            if (Decorator.ParentId == NodeId) ++AttachedNodeCount;
        }
        for (const FLuaBehaviorTreeIRNode& Service : IR.Services)
        {
            if (Service.ParentId == NodeId) ++AttachedNodeCount;
        }
        return Position.X + (240.0 + AttachedNodeCount * 24.0) * 0.5;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeIRValidationTest,
    "Lua.BehaviorTree.IR.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证重复 ID、父链环和非法继承均被稳定诊断拒绝。
 * 测试只加载引擎类，不创建资产、不保存文件，可在编辑器游戏线程运行。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 断言均成立时返回 true。
 */
bool FLuaBehaviorTreeIRValidationTest::RunTest(const FString& Parameters)
{
    using namespace LuaBehaviorTreeCompilerTests;

    FLuaBehaviorTreeIR DuplicateIR = MakeMinimalIR();
    DuplicateIR.Nodes[1].Id = DuplicateIR.Nodes[0].Id;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestFalse(TEXT("Duplicate ID is rejected"), ULuaBehaviorTreeIRLibrary::Validate(DuplicateIR, Diagnostics));
    TestTrue(TEXT("Duplicate ID has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.DuplicateId")));

    FLuaBehaviorTreeIR CycleIR = MakeMinimalIR();
    CycleIR.Nodes[0].ParentId = CycleIR.Nodes[1].Id;
    Diagnostics.Reset();
    TestFalse(TEXT("Parent cycle is rejected"), ULuaBehaviorTreeIRLibrary::Validate(CycleIR, Diagnostics));
    TestTrue(TEXT("Cycle has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.GraphCycle")));

    FLuaBehaviorTreeIR InvalidClassIR = MakeMinimalIR();
    InvalidClassIR.Nodes[1].ClassPath = FSoftClassPath(TEXT("/Script/Engine.Actor"));
    Diagnostics.Reset();
    TestFalse(TEXT("Invalid inheritance is rejected"), ULuaBehaviorTreeIRLibrary::Validate(InvalidClassIR, Diagnostics));
    TestTrue(TEXT("Invalid inheritance has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.InvalidClass")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeReflectionWriterTest,
    "Lua.BehaviorTree.Reflection.Properties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用反射写入器可写 WaitTime，并拒绝错误的 Integer 标签。
 * 测试对象位于 transient package，不生成或保存资产。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 成功写入与错误类型诊断均符合预期时返回 true。
 */
bool FLuaBehaviorTreeReflectionWriterTest::RunTest(const FString& Parameters)
{
    UBTTask_Wait* Task = NewObject<UBTTask_Wait>();
    TArray<FLuaBehaviorTreeIRValue> Values;
    FLuaBehaviorTreeIRValue& FloatValue = Values.AddDefaulted_GetRef();
    FloatValue.Type = ELuaBehaviorTreeValueType::Float;
    FloatValue.FloatValue = 1.25;
    FLuaBehaviorTreeIRProperty Property;
    Property.Name = TEXT("WaitTime");
    Property.ValueIndex = 0;
    TArray<FLuaBehaviorTreeIRProperty> Properties;
    Properties.Add(Property);
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;

    TestTrue(
        TEXT("Editable float property is written without a registry"),
        FLuaBehaviorTreeReflectionWriter::ApplyProperties(
            Task,
            Properties,
            Values,
            FLuaBehaviorTreeSourceLocation(),
            Diagnostics));
    TestEqual(TEXT("WaitTime receives reflected value"), Task->WaitTime, 1.25f);

    Values[0].Type = ELuaBehaviorTreeValueType::Integer;
    Values[0].IntegerValue = 1;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Wrong explicit type is rejected"),
        FLuaBehaviorTreeReflectionWriter::ApplyProperties(
            Task,
            Properties,
            Values,
            FLuaBehaviorTreeSourceLocation(),
            Diagnostics));
    TestTrue(
        TEXT("Wrong type has stable diagnostic"),
        LuaBehaviorTreeCompilerTests::ContainsCode(Diagnostics, TEXT("BT.Reflection.PropertyTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeArbitraryNodeGenerationTest,
    "Lua.BehaviorTree.Factory.ArbitraryNativeNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证未注册的引擎 Wait Task 可仅凭 ClassPath 与反射属性生成原生 BT/BB。
 * 测试只创建内存包且 bSaveAssets=false，不写磁盘、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 工厂生成带 RootNode 的 UBehaviorTree 时返回 true。
 */
bool FLuaBehaviorTreeArbitraryNodeGenerationTest::RunTest(const FString& Parameters)
{
    FLuaBehaviorTreeIR IR = LuaBehaviorTreeCompilerTests::MakeMinimalIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    const bool bGenerated = ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
        IR,
        TEXT("/Game/__LuaBTTests/BB_ArbitraryNode"),
        TEXT("/Game/__LuaBTTests/BT_ArbitraryNode"),
        false,
        Blackboard,
        BehaviorTree,
        Diagnostics);
    TestTrue(TEXT("Arbitrary native node generates without registration"), bGenerated);
    TestNotNull(TEXT("Generated Blackboard exists"), Blackboard);
    TestNotNull(TEXT("Generated BehaviorTree exists"), BehaviorTree);
    if (BehaviorTree) TestNotNull(TEXT("Generated runtime root exists"), BehaviorTree->RootNode.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeFullStructureReplacementTest,
    "Lua.BehaviorTree.Factory.FullStructureReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同一路径再次生成行为树时会全量替换整棵 BTGraph，同时保留资产对象和 Lua 配置元数据。
 * 测试在首次生成的内存 Graph 中手工插入节点；第二次生成必须删除该节点且重新建立运行时 Root。
 * 只操作未保存的测试 Package，不写磁盘、不启动 PIE，必须由 Automation Framework 在游戏线程执行。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FLuaBehaviorTreeFullStructureReplacementTest::RunTest(
    const FString& Parameters)
{
    FLuaBehaviorTreeIR IR = LuaBehaviorTreeCompilerTests::MakeMinimalIR();
    IR.Values.Reset();
    IR.Nodes[1].Properties.Reset();
    const FString BlackboardPath(TEXT("/Game/__LuaBTTests/BB_FullStructureReplacement"));
    const FString BehaviorTreePath(TEXT("/Game/__LuaBTTests/BT_FullStructureReplacement"));
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Initial BehaviorTree generation succeeds"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            BlackboardPath,
            BehaviorTreePath,
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    TestNotNull(TEXT("Initial BehaviorTree has an editor Graph"), BehaviorTree ? BehaviorTree->BTGraph.Get() : nullptr);
    if (BehaviorTree == nullptr || BehaviorTree->BTGraph == nullptr) return true;

    FLuaBehaviorTreeAssetConfiguration Configuration;
    Configuration.LuaModuleName = TEXT("Automation.FullStructureReplacement");
    Configuration.BlackboardPackagePath = BlackboardPath;
    Configuration.SourceMode = ELuaBehaviorTreeSourceMode::Lua;
    TestTrue(
        TEXT("BehaviorTree Lua configuration is written before regeneration"),
        ULuaBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
            BehaviorTree,
            Configuration));

    UEdGraph* const OriginalGraph = BehaviorTree->BTGraph;
    UEdGraphNode* ManualTreeNode = NewObject<UEdGraphNode>(
        OriginalGraph,
        UEdGraphNode::StaticClass(),
        NAME_None,
        RF_Transactional);
    OriginalGraph->AddNode(ManualTreeNode, false, false);
    TestTrue(
        TEXT("Manual tree node is present before regeneration"),
        OriginalGraph->Nodes.Contains(ManualTreeNode));

    UBlackboardData* RegeneratedBlackboard = nullptr;
    UBehaviorTree* RegeneratedBehaviorTree = nullptr;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Second BehaviorTree generation succeeds"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            BlackboardPath,
            BehaviorTreePath,
            false,
            RegeneratedBlackboard,
            RegeneratedBehaviorTree,
            Diagnostics));
    TestEqual(
        TEXT("Full tree replacement preserves the BehaviorTree asset UObject"),
        RegeneratedBehaviorTree,
        BehaviorTree);
    TestEqual(
        TEXT("Full tree replacement preserves the BTGraph shell UObject"),
        RegeneratedBehaviorTree ? RegeneratedBehaviorTree->BTGraph.Get() : nullptr,
        OriginalGraph);
    TestTrue(
        TEXT("Full tree replacement deletes the manually inserted tree node"),
        OriginalGraph != nullptr && !OriginalGraph->Nodes.Contains(ManualTreeNode));
    TestNotNull(
        TEXT("Full tree replacement rebuilds the runtime Root"),
        RegeneratedBehaviorTree ? RegeneratedBehaviorTree->RootNode.Get() : nullptr);

    FLuaBehaviorTreeAssetConfiguration RegeneratedConfiguration;
    TestTrue(
        TEXT("BehaviorTree Lua configuration remains readable after regeneration"),
        ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
            RegeneratedBehaviorTree,
            RegeneratedConfiguration));
    TestEqual(
        TEXT("Full tree replacement preserves Lua module configuration"),
        RegeneratedConfiguration.LuaModuleName,
        Configuration.LuaModuleName);
    TestEqual(
        TEXT("Full tree replacement preserves Blackboard path configuration"),
        RegeneratedConfiguration.BlackboardPackagePath,
        Configuration.BlackboardPackagePath);
    TestEqual(
        TEXT("Full tree replacement preserves source mode configuration"),
        RegeneratedConfiguration.SourceMode,
        Configuration.SourceMode);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeExporterArrayGrowthTest,
    "Lua.BehaviorTree.Factory.ExporterArrayGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证深层树导出期间 Nodes 数组多次扩容不会使父节点 ID 引用失效。
 * 测试只生成内存 BT/BB 并直接调用反向导出，不写文件、不保存资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 连续导出均成功，且每个节点的 ParentId 都指向前一层节点时返回 true。
 */
bool FLuaBehaviorTreeExporterArrayGrowthTest::RunTest(
    const FString& Parameters)
{
    using namespace LuaBehaviorTreeCompilerTests;

    constexpr int32 NodeCount = 48;
    const FLuaBehaviorTreeIR IR = MakeArrayGrowthIR(NodeCount);
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Deep tree generates in memory"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__LuaBTTests/BB_ExporterArrayGrowth"),
            TEXT("/Game/__LuaBTTests/BT_ExporterArrayGrowth"),
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    if (!BehaviorTree) return false;

    for (int32 Iteration = 0; Iteration < 4; ++Iteration)
    {
        FLuaBehaviorTreeIR ExtractedIR;
        Diagnostics.Reset();
        TestTrue(
            *FString::Printf(TEXT("Deep tree export succeeds at iteration %d"), Iteration),
            ULuaBehaviorTreeExporterLibrary::ExtractBehaviorTreeIR(
                BehaviorTree,
                TEXT("Automation.ArrayGrowth.Exported"),
                ExtractedIR,
                Diagnostics));
        TestEqual(
            *FString::Printf(TEXT("Deep tree node count at iteration %d"), Iteration),
            ExtractedIR.Nodes.Num(),
            NodeCount);
        for (int32 NodeIndex = 1; NodeIndex < ExtractedIR.Nodes.Num(); ++NodeIndex)
        {
            TestEqual(
                *FString::Printf(
                    TEXT("Deep tree parent chain at iteration %d node %d"),
                    Iteration,
                    NodeIndex),
                ExtractedIR.Nodes[NodeIndex].ParentId,
                ExtractedIR.Nodes[NodeIndex - 1].Id);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeDeterministicLayoutTest,
    "Lua.BehaviorTree.Factory.DeterministicUnevenLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证多层不均匀 IR 的深度分层、声明顺序、轮廓间距、父节点居中和重复生成稳定性。
 * 测试包含普通 RunBehavior 叶节点，只创建并重建内存 BT/BB，不保存资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 首次与重复生成的全部布局断言均成立时返回 true。
 */
bool FLuaBehaviorTreeDeterministicLayoutTest::RunTest(
    const FString& Parameters)
{
    using namespace LuaBehaviorTreeCompilerTests;

    const FLuaBehaviorTreeIR IR = MakeUnevenLayoutIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Uneven tree generates in memory"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__LuaBTTests/BB_DeterministicLayout"),
            TEXT("/Game/__LuaBTTests/BT_DeterministicLayout"),
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    if (!BehaviorTree) return false;

    TMap<FString, FIntPoint> FirstPositions;
    FIntPoint FirstVirtualRoot;
    TestTrue(
        TEXT("First graph exposes all layout positions"),
        CaptureLayoutPositions(
            BehaviorTree,
            IR,
            FirstPositions,
            FirstVirtualRoot));

    const FIntPoint* Root = FirstPositions.Find(TEXT("Root"));
    const FIntPoint* BranchA = FirstPositions.Find(TEXT("BranchA"));
    const FIntPoint* LeafA = FirstPositions.Find(TEXT("LeafA"));
    const FIntPoint* BranchA2 = FirstPositions.Find(TEXT("BranchA2"));
    const FIntPoint* RunBehavior = FirstPositions.Find(TEXT("RunBehavior"));
    const FIntPoint* MoveTo = FirstPositions.Find(TEXT("MoveTo"));
    const FIntPoint* BranchB = FirstPositions.Find(TEXT("BranchB"));
    const FIntPoint* LeafB = FirstPositions.Find(TEXT("LeafB"));
    if (!Root || !BranchA || !LeafA || !BranchA2
        || !RunBehavior || !MoveTo || !BranchB || !LeafB)
    {
        AddError(TEXT("Generated graph is missing an expected main node position."));
        return false;
    }

    TestTrue(TEXT("Virtual Root is above IR root"), FirstVirtualRoot.Y < Root->Y);
    TestTrue(TEXT("First child layer is below root"), Root->Y < BranchA->Y);
    TestEqual(TEXT("Root children share one depth layer"), BranchA->Y, BranchB->Y);
    TestTrue(TEXT("Second child layer is below first"), BranchA->Y < LeafA->Y);
    TestEqual(TEXT("Uneven siblings share depth Y"), LeafA->Y, BranchA2->Y);
    TestEqual(TEXT("Other branch leaf shares depth Y"), LeafA->Y, LeafB->Y);
    TestTrue(TEXT("Fourth layer is below third"), LeafA->Y < RunBehavior->Y);
    TestEqual(TEXT("Deep leaves share depth Y"), RunBehavior->Y, MoveTo->Y);

    const double RootCenter = GetLayoutCenterX(IR, TEXT("Root"), *Root);
    const double BranchACenter = GetLayoutCenterX(IR, TEXT("BranchA"), *BranchA);
    const double BranchA2Center = GetLayoutCenterX(IR, TEXT("BranchA2"), *BranchA2);
    const double RunBehaviorCenter = GetLayoutCenterX(
        IR,
        TEXT("RunBehavior"),
        *RunBehavior);
    const double MoveToCenter = GetLayoutCenterX(IR, TEXT("MoveTo"), *MoveTo);
    const double LeafACenter = GetLayoutCenterX(IR, TEXT("LeafA"), *LeafA);
    const double LeafBCenter = GetLayoutCenterX(IR, TEXT("LeafB"), *LeafB);

    TestTrue(
        TEXT("Virtual Root center aligns with IR root center"),
        FMath::Abs(FirstVirtualRoot.X + 100.0 - RootCenter) <= 1.0);
    TestTrue(
        TEXT("Deep parent centers over direct child subtree range"),
        FMath::Abs(
            BranchA2Center
            - (RunBehaviorCenter + MoveToCenter) * 0.5) <= 1.0);
    TestTrue(
        TEXT("Uneven branch parent centers over descendant outline"),
        FMath::Abs(
            BranchACenter
            - (LeafA->X + MoveTo->X + 240.0) * 0.5) <= 1.0);
    TestTrue(
        TEXT("IR root centers over the full direct subtree range"),
        FMath::Abs(
            RootCenter
            - (LeafA->X + LeafB->X + 240.0) * 0.5) <= 1.0);

    TestTrue(TEXT("Declaration order places LeafA first"), LeafACenter < RunBehaviorCenter);
    TestTrue(TEXT("RunBehavior participates as an ordinary ordered leaf"), RunBehaviorCenter < MoveToCenter);
    TestTrue(TEXT("Second root subtree remains on the right"), MoveToCenter < LeafBCenter);
    TestTrue(
        TEXT("Attached nodes expand LeafA spacing without overlap"),
        LeafA->X + 288 + 100 <= BranchA2->X);
    TestTrue(
        TEXT("Deep sibling leaves retain horizontal clearance"),
        RunBehavior->X + 240 + 100 <= MoveTo->X);
    TestTrue(
        TEXT("Root sibling subtrees retain horizontal clearance"),
        MoveTo->X + 240 + 100 <= LeafB->X);

    UBlackboardData* RebuiltBlackboard = nullptr;
    UBehaviorTree* RebuiltBehaviorTree = nullptr;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Same IR rebuilds the same in-memory graph"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__LuaBTTests/BB_DeterministicLayout"),
            TEXT("/Game/__LuaBTTests/BT_DeterministicLayout"),
            false,
            RebuiltBlackboard,
            RebuiltBehaviorTree,
            Diagnostics));
    TestTrue(
        TEXT("Rebuild reuses BehaviorTree object"),
        RebuiltBehaviorTree == BehaviorTree);

    TMap<FString, FIntPoint> SecondPositions;
    FIntPoint SecondVirtualRoot;
    TestTrue(
        TEXT("Rebuilt graph exposes all layout positions"),
        CaptureLayoutPositions(
            RebuiltBehaviorTree,
            IR,
            SecondPositions,
            SecondVirtualRoot));
    TestEqual(
        TEXT("Virtual Root coordinate is deterministic"),
        SecondVirtualRoot,
        FirstVirtualRoot);
    for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
    {
        const FIntPoint* FirstPosition = FirstPositions.Find(Node.Id);
        const FIntPoint* SecondPosition = SecondPositions.Find(Node.Id);
        TestTrue(
            *FString::Printf(TEXT("Repeated coordinate exists for %s"), *Node.Id),
            FirstPosition && SecondPosition);
        if (FirstPosition && SecondPosition)
        {
            TestEqual(
                *FString::Printf(TEXT("Repeated coordinate is stable for %s"), *Node.Id),
                *SecondPosition,
                *FirstPosition);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeBidirectionalExportTest,
    "Lua.BehaviorTree.Factory.BidirectionalExportRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证原生多层树、附属节点、Blackboard、LuaTask 属性可显式导出并重新编译为等价 IR。
 * 测试使用唯一 Content/Script 临时模块并在结束时删除，不保存 BT/BB、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 路径、覆盖保护、文本转义、fresh round-trip 与 SourceMode 断言均成立时返回 true。
 */
bool FLuaBehaviorTreeBidirectionalExportTest::RunTest(
    const FString& Parameters)
{
    using namespace LuaBehaviorTreeCompilerTests;

    const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString LuaModuleName = TEXT("__LuaBTTests.T_") + UniqueSuffix;
    FString LuaFilePath;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Unique module resolves inside Content Script"),
        ULuaBehaviorTreeExporterLibrary::ResolveLuaModuleFilePath(
            LuaModuleName,
            LuaFilePath,
            Diagnostics));
    ON_SCOPE_EXIT
    {
        if (!LuaFilePath.IsEmpty())
            IFileManager::Get().Delete(*LuaFilePath, false, true, true);
    };

    FString InvalidPath;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Parent traversal module is rejected"),
        ULuaBehaviorTreeExporterLibrary::ResolveLuaModuleFilePath(
            TEXT("AI...Escape"),
            InvalidPath,
            Diagnostics));

    FLuaBehaviorTreeIR IR = MakeUnevenLayoutIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Native uneven tree generates before export"),
        ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__LuaBTTests/BB_BidirectionalExport"),
            TEXT("/Game/__LuaBTTests/BT_BidirectionalExport"),
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    if (!BehaviorTree) return false;

    FString ExportedPath;
    Diagnostics.Reset();
    TestTrue(
        TEXT("BehaviorTree exports with explicit overwrite authority"),
        ULuaBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
            BehaviorTree,
            LuaModuleName,
            true,
            ExportedPath,
            Diagnostics));
    TestEqual(TEXT("Exporter returns resolved target"), ExportedPath, LuaFilePath);

    FString ExportedSource;
    TestTrue(
        TEXT("Exported Lua can be read"),
        FFileHelper::LoadFileToString(ExportedSource, *LuaFilePath));
    TestTrue(
        TEXT("Lua contains native runtime ClassPath"),
        ExportedSource.Contains(TEXT("/Script/AIModule.BTTask_RunBehavior")));
    TestTrue(
        TEXT("Lua contains generic LuaTask ClassPath"),
        ExportedSource.Contains(TEXT("/Script/LuaBehaviorTree.LuaBehaviorTreeTask")));
    TestTrue(
        TEXT("Lua contains reflected Float constructor"),
        ExportedSource.Contains(TEXT("LuaBehaviorTree.Value.Float(0.75)")));
    TestTrue(
        TEXT("Lua escapes quotes, newline and slash"),
        ExportedSource.Contains(TEXT("line1\\n\\\"quoted\\\"\\\\path")));

    const FString OriginalSource = ExportedSource;
    FString ProtectedPath;
    Diagnostics.Reset();
    TestFalse(
        TEXT("bOverwrite false rejects existing Lua"),
        ULuaBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
            BehaviorTree,
            LuaModuleName,
            false,
            ProtectedPath,
            Diagnostics));
    ExportedSource.Reset();
    FFileHelper::LoadFileToString(ExportedSource, *LuaFilePath);
    TestEqual(TEXT("Overwrite rejection preserves old Lua"), ExportedSource, OriginalSource);

    FLuaBehaviorTreeIR RoundTripIR;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Exported Lua fresh-compiles after package.loaded invalidation"),
        ULuaBehaviorTreeIRLibrary::CompileLuaModuleFresh(
            LuaModuleName,
            RoundTripIR,
            Diagnostics));
    TestEqual(TEXT("Round-trip keeps main node count"), RoundTripIR.Nodes.Num(), IR.Nodes.Num());
    TestEqual(TEXT("Round-trip keeps Decorator count"), RoundTripIR.Decorators.Num(), IR.Decorators.Num());
    TestEqual(TEXT("Round-trip keeps Service count"), RoundTripIR.Services.Num(), IR.Services.Num());
    const int32 NativeBlackboardKeyCount = BehaviorTree->BlackboardAsset
        ? BehaviorTree->BlackboardAsset->Keys.Num()
        : 0;
    TestEqual(
        TEXT("Round-trip keeps native Blackboard key count including persistent keys"),
        RoundTripIR.BlackboardKeys.Num(),
        NativeBlackboardKeyCount);

    FLuaBehaviorTreeAssetConfiguration Configuration;
    ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
        BehaviorTree,
        Configuration);
    TestTrue(
        TEXT("BehaviorTree to Lua marks BehaviorTree as last source"),
        Configuration.SourceMode == ELuaBehaviorTreeSourceMode::BehaviorTree);
    TestEqual(TEXT("Export stores target module metadata"), Configuration.LuaModuleName, LuaModuleName);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeLuaImporterTest,
    "Lua.BehaviorTree.LuaImporter.ReflectionExample",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用 Lua DSL 示例可通过 UnLua 导入严格 IR，且节点与属性保持显式类型。
 * 测试只读取 Content/Script 模块，不创建资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 示例模块导入且结构符合预期时返回 true。
 */
bool FLuaBehaviorTreeLuaImporterTest::RunTest(const FString& Parameters)
{
    FLuaBehaviorTreeIR IR;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    const bool bCompiled = ULuaBehaviorTreeIRLibrary::CompileLuaModule(
        TEXT("AI.Examples.BT_ReflectionExample"),
        IR,
        Diagnostics);
    TestTrue(TEXT("Reflection example imports through UnLua"), bCompiled);
    TestEqual(TEXT("Example has two main nodes"), IR.Nodes.Num(), 2);
    TestEqual(TEXT("Example has one Blackboard Key"), IR.BlackboardKeys.Num(), 1);
    TestTrue(TEXT("Example property uses explicit Float value"), IR.Values.Num() > 0
        && IR.Values[0].Type == ELuaBehaviorTreeValueType::Float);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeAssetConfigurationTest,
    "Lua.BehaviorTree.Factory.AssetConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证每资产 Lua 元数据可往返读取，且配置式 API 会原地生成并复用目标 UBehaviorTree。
 * 测试使用内存包且 bSaveAssets=false，不写磁盘、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 元数据、路径推导与原地生成断言均成立时返回 true。
 */
bool FLuaBehaviorTreeAssetConfigurationTest::RunTest(const FString& Parameters)
{
    UPackage* BehaviorTreePackage =
        CreatePackage(TEXT("/Game/__LuaBTTests/BT_Configured"));
    UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(
        BehaviorTreePackage,
        TEXT("BT_Configured"),
        RF_Public | RF_Standalone);
    FLuaBehaviorTreeAssetConfiguration Configuration;
    Configuration.LuaModuleName = TEXT("AI.Examples.BT_ReflectionExample");
    Configuration.SourceMode = ELuaBehaviorTreeSourceMode::Lua;

    TestTrue(
        TEXT("Configuration writes to package metadata"),
        ULuaBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
            BehaviorTree,
            Configuration));
    FLuaBehaviorTreeAssetConfiguration ReadConfiguration;
    TestTrue(
        TEXT("Configuration reads from package metadata"),
        ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Lua module metadata round-trips"),
        ReadConfiguration.LuaModuleName,
        Configuration.LuaModuleName);
    TestTrue(
        TEXT("Source Mode metadata round-trips"),
        ReadConfiguration.SourceMode == ELuaBehaviorTreeSourceMode::Lua);
    TestEqual(
        TEXT("BT prefix derives BB path"),
        ULuaBehaviorTreeFactoryLibrary::DeriveBlackboardPackagePath(
            TEXT("/Game/AI/BT_Guard")),
        FString(TEXT("/Game/AI/BB_Guard")));

    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* GeneratedBehaviorTree = nullptr;
    TArray<FLuaBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Configured generation succeeds in place"),
        ULuaBehaviorTreeFactoryLibrary::GenerateConfiguredBehaviorTree(
            BehaviorTree,
            false,
            Blackboard,
            GeneratedBehaviorTree,
            Diagnostics));
    TestTrue(
        TEXT("Configured generation keeps the same BehaviorTree object"),
        GeneratedBehaviorTree == BehaviorTree);
    TestNotNull(TEXT("Configured generation creates Blackboard"), Blackboard);

    ReadConfiguration = FLuaBehaviorTreeAssetConfiguration();
    TestTrue(
        TEXT("Generated configuration reads back before Source Mode update"),
        ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Generated Blackboard path is available to later metadata updates"),
        ReadConfiguration.BlackboardPackagePath,
        FString(TEXT("/Game/__LuaBTTests/BB_Configured")));

    const int32 GraphNodeCount = BehaviorTree->BTGraph
        ? BehaviorTree->BTGraph->Nodes.Num()
        : 0;
    ReadConfiguration.SourceMode = ELuaBehaviorTreeSourceMode::BehaviorTree;
    TestTrue(
        TEXT("Source Mode switch only writes metadata"),
        ULuaBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Source Mode switch does not change Graph"),
        BehaviorTree->BTGraph ? BehaviorTree->BTGraph->Nodes.Num() : 0,
        GraphNodeCount);
    Diagnostics.Reset();
    TestTrue(
        TEXT("Check Lua succeeds without rebuilding Graph"),
        ULuaBehaviorTreeFactoryLibrary::CheckConfiguredBehaviorTree(
            BehaviorTree,
            Diagnostics));
    TestEqual(
        TEXT("Check Lua does not change Graph"),
        BehaviorTree->BTGraph ? BehaviorTree->BTGraph->Nodes.Num() : 0,
        GraphNodeCount);

    ReadConfiguration = FLuaBehaviorTreeAssetConfiguration();
    ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
        BehaviorTree,
        ReadConfiguration);
    TestEqual(
        TEXT("Actual Blackboard path is written back"),
        ReadConfiguration.BlackboardPackagePath,
        FString(TEXT("/Game/__LuaBTTests/BB_Configured")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaBehaviorTreeTaskBoundaryTest,
    "Lua.BehaviorTree.Runtime.GenericTaskBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用 Lua Task 强制节点实例化、暴露必要配置，且空闲状态拒绝显式 Finish。
 * 测试不创建 BehaviorTreeComponent、不调用 Lua、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 实例化和生命周期边界断言均成立时返回 true。
 */
bool FLuaBehaviorTreeTaskBoundaryTest::RunTest(const FString& Parameters)
{
    ULuaBehaviorTreeTask* Task = NewObject<ULuaBehaviorTreeTask>();
    TestTrue(TEXT("Generic Lua task creates a node instance"), Task->HasInstance());
    TestNotNull(
        TEXT("LuaModuleName is available to generic reflection generation"),
        FindFProperty<FProperty>(
            ULuaBehaviorTreeTask::StaticClass(),
            GET_MEMBER_NAME_CHECKED(
                ULuaBehaviorTreeTask,
                LuaModuleName)));
    TestNotNull(
        TEXT("Configuration is available to generic reflection generation"),
        FindFProperty<FProperty>(
            ULuaBehaviorTreeTask::StaticClass(),
            GET_MEMBER_NAME_CHECKED(
                ULuaBehaviorTreeTask,
                Configuration)));
    TestFalse(
        TEXT("Idle task rejects latent Finish"),
        Task->FinishLuaTask(ELuaBehaviorTreeTaskResult::Succeeded));
    TestFalse(TEXT("Idle task is not active"), Task->IsTaskActive());
    TestNull(TEXT("Idle task has no AIController"), Task->GetTaskAIController());
    TestNull(TEXT("Idle task has no Pawn"), Task->GetTaskPawn());
    TestNull(TEXT("Idle task has no Blackboard"), Task->GetTaskBlackboard());
    return true;
}

#endif
