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
#include "SekiroBehaviorTreeFactoryLibrary.h"
#include "SekiroBehaviorTreeExporterLibrary.h"
#include "SekiroBehaviorTreeIRLibrary.h"
#include "SekiroBehaviorTreeReflectionWriter.h"
#include "SekiroLuaBehaviorTreeTask.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace SekiroBehaviorTreeCompilerTests
{
    /**
     * 创建只含 Selector 根与 Wait 叶节点的最小通用 IR。
     * 类路径均来自 AIModule，用于证明编译器没有具体节点注册步骤。
     *
     * @return 可通过结构校验的最小 IR。
     */
    FSekiroBehaviorTreeIR MakeMinimalIR()
    {
        FSekiroBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.Minimal");
        IR.RootNodeId = TEXT("Node:Root");

        FSekiroBehaviorTreeIRNode Root;
        Root.Id = IR.RootNodeId;
        Root.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTComposite_Selector"));
        Root.DisplayName = TEXT("Root");
        Root.DeclarationOrder = 1;
        IR.Nodes.Add(MoveTemp(Root));

        FSekiroBehaviorTreeIRNode Wait;
        Wait.Id = TEXT("Node:Wait");
        Wait.ParentId = IR.RootNodeId;
        Wait.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTTask_Wait"));
        Wait.DisplayName = TEXT("Wait");
        Wait.DeclarationOrder = 2;

        FSekiroBehaviorTreeIRValue& WaitTime = IR.Values.AddDefaulted_GetRef();
        WaitTime.Type = ESekiroBehaviorTreeValueType::Float;
        WaitTime.FloatValue = 0.5;
        FSekiroBehaviorTreeIRProperty& WaitTimeProperty = Wait.Properties.AddDefaulted_GetRef();
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
    FSekiroBehaviorTreeIR MakeArrayGrowthIR(const int32 NodeCount)
    {
        check(NodeCount >= 2);
        FSekiroBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.ArrayGrowth");
        FString ParentId;
        for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
        {
            const FString NodeId = FString::Printf(TEXT("Node:Growth%02d"), NodeIndex);
            FSekiroBehaviorTreeIRNode Node;
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
        const TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics,
        const FName Code)
    {
        for (const FSekiroBehaviorTreeDiagnostic& Diagnostic : Diagnostics)
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
    FSekiroBehaviorTreeIR MakeUnevenLayoutIR()
    {
        FSekiroBehaviorTreeIR IR;
        IR.SourceModule = TEXT("Automation.UnevenLayout");
        IR.RootNodeId = TEXT("Root");

        const auto AddNode = [&IR](
            const TCHAR* Id,
            const TCHAR* ParentId,
            const TCHAR* ClassPath,
            const int32 DeclarationOrder)
        {
            FSekiroBehaviorTreeIRNode Node;
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
            TEXT("/Script/SekiroLuaBehaviorTreeExt.SekiroLuaBehaviorTreeTask"),
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

        FSekiroBehaviorTreeIRNode DecoratorA;
        DecoratorA.Id = TEXT("DecoratorA");
        DecoratorA.ParentId = TEXT("LeafA");
        DecoratorA.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTDecorator_Loop"));
        DecoratorA.DeclarationOrder = 100;
        IR.Decorators.Add(MoveTemp(DecoratorA));

        FSekiroBehaviorTreeIRNode DecoratorB;
        DecoratorB.Id = TEXT("DecoratorB");
        DecoratorB.ParentId = TEXT("LeafA");
        DecoratorB.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTDecorator_ForceSuccess"));
        DecoratorB.DeclarationOrder = 101;
        IR.Decorators.Add(MoveTemp(DecoratorB));

        FSekiroBehaviorTreeIRNode Service;
        Service.Id = TEXT("ServiceRoot");
        Service.ParentId = TEXT("Root");
        Service.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTService_DefaultFocus"));
        Service.DeclarationOrder = 102;
        IR.Services.Add(MoveTemp(Service));

        FSekiroBehaviorTreeIRValue& WaitTime = IR.Values.AddDefaulted_GetRef();
        WaitTime.Type = ESekiroBehaviorTreeValueType::Float;
        WaitTime.FloatValue = 0.75;
        FSekiroBehaviorTreeIRProperty& WaitTimeProperty =
            IR.Nodes[LeafAIndex].Properties.AddDefaulted_GetRef();
        WaitTimeProperty.Name = TEXT("WaitTime");
        WaitTimeProperty.ValueIndex = 0;
        WaitTimeProperty.DeclarationOrder = 1;

        FSekiroBehaviorTreeIRValue& LuaModule = IR.Values.AddDefaulted_GetRef();
        LuaModule.Type = ESekiroBehaviorTreeValueType::String;
        LuaModule.StringValue = TEXT("AI.Tasks.ExportExample");
        FSekiroBehaviorTreeIRProperty& LuaModuleProperty =
            IR.Nodes[MoveToIndex].Properties.AddDefaulted_GetRef();
        LuaModuleProperty.Name = TEXT("LuaModuleName");
        LuaModuleProperty.ValueIndex = 1;
        LuaModuleProperty.DeclarationOrder = 1;

        FSekiroBehaviorTreeIRValue& Configuration = IR.Values.AddDefaulted_GetRef();
        Configuration.Type = ESekiroBehaviorTreeValueType::String;
        Configuration.StringValue = TEXT("line1\n\"quoted\"\\path");
        FSekiroBehaviorTreeIRProperty& ConfigurationProperty =
            IR.Nodes[MoveToIndex].Properties.AddDefaulted_GetRef();
        ConfigurationProperty.Name = TEXT("Configuration");
        ConfigurationProperty.ValueIndex = 2;
        ConfigurationProperty.DeclarationOrder = 2;

        FSekiroBlackboardIRKey& Key = IR.BlackboardKeys.AddDefaulted_GetRef();
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
        const FSekiroBehaviorTreeIR& IR,
        TMap<FString, FIntPoint>& OutPositions,
        FIntPoint& OutVirtualRoot)
    {
        OutPositions.Reset();
        OutVirtualRoot = FIntPoint::ZeroValue;
        const UEdGraph* Graph = BehaviorTree ? BehaviorTree->BTGraph : nullptr;
        if (!Graph) return false;

        TArray<const FSekiroBehaviorTreeIRNode*> SortedNodes;
        for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
            SortedNodes.Add(&Node);
        SortedNodes.Sort(
            [](const FSekiroBehaviorTreeIRNode& Left,
                const FSekiroBehaviorTreeIRNode& Right)
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
        const FSekiroBehaviorTreeIR& IR,
        const FString& NodeId,
        const FIntPoint& Position)
    {
        int32 AttachedNodeCount = 0;
        for (const FSekiroBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            if (Decorator.ParentId == NodeId) ++AttachedNodeCount;
        }
        for (const FSekiroBehaviorTreeIRNode& Service : IR.Services)
        {
            if (Service.ParentId == NodeId) ++AttachedNodeCount;
        }
        return Position.X + (240.0 + AttachedNodeCount * 24.0) * 0.5;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroBehaviorTreeIRValidationTest,
    "Sekiro.LuaBehaviorTree.IR.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证重复 ID、父链环和非法继承均被稳定诊断拒绝。
 * 测试只加载引擎类，不创建资产、不保存文件，可在编辑器游戏线程运行。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 断言均成立时返回 true。
 */
bool FSekiroBehaviorTreeIRValidationTest::RunTest(const FString& Parameters)
{
    using namespace SekiroBehaviorTreeCompilerTests;

    FSekiroBehaviorTreeIR DuplicateIR = MakeMinimalIR();
    DuplicateIR.Nodes[1].Id = DuplicateIR.Nodes[0].Id;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    TestFalse(TEXT("Duplicate ID is rejected"), USekiroBehaviorTreeIRLibrary::Validate(DuplicateIR, Diagnostics));
    TestTrue(TEXT("Duplicate ID has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.DuplicateId")));

    FSekiroBehaviorTreeIR CycleIR = MakeMinimalIR();
    CycleIR.Nodes[0].ParentId = CycleIR.Nodes[1].Id;
    Diagnostics.Reset();
    TestFalse(TEXT("Parent cycle is rejected"), USekiroBehaviorTreeIRLibrary::Validate(CycleIR, Diagnostics));
    TestTrue(TEXT("Cycle has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.GraphCycle")));

    FSekiroBehaviorTreeIR InvalidClassIR = MakeMinimalIR();
    InvalidClassIR.Nodes[1].ClassPath = FSoftClassPath(TEXT("/Script/Engine.Actor"));
    Diagnostics.Reset();
    TestFalse(TEXT("Invalid inheritance is rejected"), USekiroBehaviorTreeIRLibrary::Validate(InvalidClassIR, Diagnostics));
    TestTrue(TEXT("Invalid inheritance has stable diagnostic"), ContainsCode(Diagnostics, TEXT("BT.IR.InvalidClass")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroBehaviorTreeReflectionWriterTest,
    "Sekiro.LuaBehaviorTree.Reflection.Properties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用反射写入器可写 WaitTime，并拒绝错误的 Integer 标签。
 * 测试对象位于 transient package，不生成或保存资产。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 成功写入与错误类型诊断均符合预期时返回 true。
 */
bool FSekiroBehaviorTreeReflectionWriterTest::RunTest(const FString& Parameters)
{
    UBTTask_Wait* Task = NewObject<UBTTask_Wait>();
    TArray<FSekiroBehaviorTreeIRValue> Values;
    FSekiroBehaviorTreeIRValue& FloatValue = Values.AddDefaulted_GetRef();
    FloatValue.Type = ESekiroBehaviorTreeValueType::Float;
    FloatValue.FloatValue = 1.25;
    FSekiroBehaviorTreeIRProperty Property;
    Property.Name = TEXT("WaitTime");
    Property.ValueIndex = 0;
    TArray<FSekiroBehaviorTreeIRProperty> Properties;
    Properties.Add(Property);
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;

    TestTrue(
        TEXT("Editable float property is written without a registry"),
        FSekiroBehaviorTreeReflectionWriter::ApplyProperties(
            Task,
            Properties,
            Values,
            FSekiroBehaviorTreeSourceLocation(),
            Diagnostics));
    TestEqual(TEXT("WaitTime receives reflected value"), Task->WaitTime, 1.25f);

    Values[0].Type = ESekiroBehaviorTreeValueType::Integer;
    Values[0].IntegerValue = 1;
    Diagnostics.Reset();
    TestFalse(
        TEXT("Wrong explicit type is rejected"),
        FSekiroBehaviorTreeReflectionWriter::ApplyProperties(
            Task,
            Properties,
            Values,
            FSekiroBehaviorTreeSourceLocation(),
            Diagnostics));
    TestTrue(
        TEXT("Wrong type has stable diagnostic"),
        SekiroBehaviorTreeCompilerTests::ContainsCode(Diagnostics, TEXT("BT.Reflection.PropertyTypeMismatch")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroBehaviorTreeArbitraryNodeGenerationTest,
    "Sekiro.LuaBehaviorTree.Factory.ArbitraryNativeNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证未注册的引擎 Wait Task 可仅凭 ClassPath 与反射属性生成原生 BT/BB。
 * 测试只创建内存包且 bSaveAssets=false，不写磁盘、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 工厂生成带 RootNode 的 UBehaviorTree 时返回 true。
 */
bool FSekiroBehaviorTreeArbitraryNodeGenerationTest::RunTest(const FString& Parameters)
{
    FSekiroBehaviorTreeIR IR = SekiroBehaviorTreeCompilerTests::MakeMinimalIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    const bool bGenerated = USekiroBehaviorTreeFactoryLibrary::GenerateFromIR(
        IR,
        TEXT("/Game/__SekiroLuaBTTests/BB_ArbitraryNode"),
        TEXT("/Game/__SekiroLuaBTTests/BT_ArbitraryNode"),
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
    FSekiroBehaviorTreeExporterArrayGrowthTest,
    "Sekiro.LuaBehaviorTree.Factory.ExporterArrayGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证深层树导出期间 Nodes 数组多次扩容不会使父节点 ID 引用失效。
 * 测试只生成内存 BT/BB 并直接调用反向导出，不写文件、不保存资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 连续导出均成功，且每个节点的 ParentId 都指向前一层节点时返回 true。
 */
bool FSekiroBehaviorTreeExporterArrayGrowthTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroBehaviorTreeCompilerTests;

    constexpr int32 NodeCount = 48;
    const FSekiroBehaviorTreeIR IR = MakeArrayGrowthIR(NodeCount);
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Deep tree generates in memory"),
        USekiroBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__SekiroLuaBTTests/BB_ExporterArrayGrowth"),
            TEXT("/Game/__SekiroLuaBTTests/BT_ExporterArrayGrowth"),
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    if (!BehaviorTree) return false;

    for (int32 Iteration = 0; Iteration < 4; ++Iteration)
    {
        FSekiroBehaviorTreeIR ExtractedIR;
        Diagnostics.Reset();
        TestTrue(
            *FString::Printf(TEXT("Deep tree export succeeds at iteration %d"), Iteration),
            USekiroBehaviorTreeExporterLibrary::ExtractBehaviorTreeIR(
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
    FSekiroBehaviorTreeDeterministicLayoutTest,
    "Sekiro.LuaBehaviorTree.Factory.DeterministicUnevenLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证多层不均匀 IR 的深度分层、声明顺序、轮廓间距、父节点居中和重复生成稳定性。
 * 测试包含普通 RunBehavior 叶节点，只创建并重建内存 BT/BB，不保存资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 首次与重复生成的全部布局断言均成立时返回 true。
 */
bool FSekiroBehaviorTreeDeterministicLayoutTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroBehaviorTreeCompilerTests;

    const FSekiroBehaviorTreeIR IR = MakeUnevenLayoutIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Uneven tree generates in memory"),
        USekiroBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__SekiroLuaBTTests/BB_DeterministicLayout"),
            TEXT("/Game/__SekiroLuaBTTests/BT_DeterministicLayout"),
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
        USekiroBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__SekiroLuaBTTests/BB_DeterministicLayout"),
            TEXT("/Game/__SekiroLuaBTTests/BT_DeterministicLayout"),
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
    for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
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
    FSekiroBehaviorTreeBidirectionalExportTest,
    "Sekiro.LuaBehaviorTree.Factory.BidirectionalExportRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证原生多层树、附属节点、Blackboard、LuaTask 属性可显式导出并重新编译为等价 IR。
 * 测试使用唯一 Content/Script 临时模块并在结束时删除，不保存 BT/BB、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 路径、覆盖保护、文本转义、fresh round-trip 与 SourceMode 断言均成立时返回 true。
 */
bool FSekiroBehaviorTreeBidirectionalExportTest::RunTest(
    const FString& Parameters)
{
    using namespace SekiroBehaviorTreeCompilerTests;

    const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString LuaModuleName = TEXT("__SekiroLuaBTTests.T_") + UniqueSuffix;
    FString LuaFilePath;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Unique module resolves inside Content Script"),
        USekiroBehaviorTreeExporterLibrary::ResolveLuaModuleFilePath(
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
        USekiroBehaviorTreeExporterLibrary::ResolveLuaModuleFilePath(
            TEXT("AI...Escape"),
            InvalidPath,
            Diagnostics));

    FSekiroBehaviorTreeIR IR = MakeUnevenLayoutIR();
    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* BehaviorTree = nullptr;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Native uneven tree generates before export"),
        USekiroBehaviorTreeFactoryLibrary::GenerateFromIR(
            IR,
            TEXT("/Game/__SekiroLuaBTTests/BB_BidirectionalExport"),
            TEXT("/Game/__SekiroLuaBTTests/BT_BidirectionalExport"),
            false,
            Blackboard,
            BehaviorTree,
            Diagnostics));
    if (!BehaviorTree) return false;

    FString ExportedPath;
    Diagnostics.Reset();
    TestTrue(
        TEXT("BehaviorTree exports with explicit overwrite authority"),
        USekiroBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
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
        ExportedSource.Contains(TEXT("/Script/SekiroLuaBehaviorTreeExt.SekiroLuaBehaviorTreeTask")));
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
        USekiroBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
            BehaviorTree,
            LuaModuleName,
            false,
            ProtectedPath,
            Diagnostics));
    ExportedSource.Reset();
    FFileHelper::LoadFileToString(ExportedSource, *LuaFilePath);
    TestEqual(TEXT("Overwrite rejection preserves old Lua"), ExportedSource, OriginalSource);

    FSekiroBehaviorTreeIR RoundTripIR;
    Diagnostics.Reset();
    TestTrue(
        TEXT("Exported Lua fresh-compiles after package.loaded invalidation"),
        USekiroBehaviorTreeIRLibrary::CompileLuaModuleFresh(
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

    FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
    USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
        BehaviorTree,
        Configuration);
    TestTrue(
        TEXT("BehaviorTree to Lua marks BehaviorTree as last source"),
        Configuration.SourceMode == ESekiroLuaBehaviorTreeSourceMode::BehaviorTree);
    TestEqual(TEXT("Export stores target module metadata"), Configuration.LuaModuleName, LuaModuleName);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroBehaviorTreeLuaImporterTest,
    "Sekiro.LuaBehaviorTree.LuaImporter.ReflectionExample",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用 Lua DSL 示例可通过 UnLua 导入严格 IR，且节点与属性保持显式类型。
 * 测试只读取 Content/Script 模块，不创建资产、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 示例模块导入且结构符合预期时返回 true。
 */
bool FSekiroBehaviorTreeLuaImporterTest::RunTest(const FString& Parameters)
{
    FSekiroBehaviorTreeIR IR;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    const bool bCompiled = USekiroBehaviorTreeIRLibrary::CompileLuaModule(
        TEXT("AI.Examples.BT_ReflectionExample"),
        IR,
        Diagnostics);
    TestTrue(TEXT("Reflection example imports through UnLua"), bCompiled);
    TestEqual(TEXT("Example has two main nodes"), IR.Nodes.Num(), 2);
    TestEqual(TEXT("Example has one Blackboard Key"), IR.BlackboardKeys.Num(), 1);
    TestTrue(TEXT("Example property uses explicit Float value"), IR.Values.Num() > 0
        && IR.Values[0].Type == ESekiroBehaviorTreeValueType::Float);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroBehaviorTreeAssetConfigurationTest,
    "Sekiro.LuaBehaviorTree.Factory.AssetConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证每资产 Lua 元数据可往返读取，且配置式 API 会原地生成并复用目标 UBehaviorTree。
 * 测试使用内存包且 bSaveAssets=false，不写磁盘、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 元数据、路径推导与原地生成断言均成立时返回 true。
 */
bool FSekiroBehaviorTreeAssetConfigurationTest::RunTest(const FString& Parameters)
{
    UPackage* BehaviorTreePackage =
        CreatePackage(TEXT("/Game/__SekiroLuaBTTests/BT_Configured"));
    UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(
        BehaviorTreePackage,
        TEXT("BT_Configured"),
        RF_Public | RF_Standalone);
    FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
    Configuration.LuaModuleName = TEXT("AI.Examples.BT_ReflectionExample");
    Configuration.SourceMode = ESekiroLuaBehaviorTreeSourceMode::Lua;

    TestTrue(
        TEXT("Configuration writes to package metadata"),
        USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
            BehaviorTree,
            Configuration));
    FSekiroLuaBehaviorTreeAssetConfiguration ReadConfiguration;
    TestTrue(
        TEXT("Configuration reads from package metadata"),
        USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Lua module metadata round-trips"),
        ReadConfiguration.LuaModuleName,
        Configuration.LuaModuleName);
    TestTrue(
        TEXT("Source Mode metadata round-trips"),
        ReadConfiguration.SourceMode == ESekiroLuaBehaviorTreeSourceMode::Lua);
    TestEqual(
        TEXT("BT prefix derives BB path"),
        USekiroBehaviorTreeFactoryLibrary::DeriveBlackboardPackagePath(
            TEXT("/Game/AI/BT_Guard")),
        FString(TEXT("/Game/AI/BB_Guard")));

    UBlackboardData* Blackboard = nullptr;
    UBehaviorTree* GeneratedBehaviorTree = nullptr;
    TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
    TestTrue(
        TEXT("Configured generation succeeds in place"),
        USekiroBehaviorTreeFactoryLibrary::GenerateConfiguredBehaviorTree(
            BehaviorTree,
            false,
            Blackboard,
            GeneratedBehaviorTree,
            Diagnostics));
    TestTrue(
        TEXT("Configured generation keeps the same BehaviorTree object"),
        GeneratedBehaviorTree == BehaviorTree);
    TestNotNull(TEXT("Configured generation creates Blackboard"), Blackboard);

    ReadConfiguration = FSekiroLuaBehaviorTreeAssetConfiguration();
    TestTrue(
        TEXT("Generated configuration reads back before Source Mode update"),
        USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Generated Blackboard path is available to later metadata updates"),
        ReadConfiguration.BlackboardPackagePath,
        FString(TEXT("/Game/__SekiroLuaBTTests/BB_Configured")));

    const int32 GraphNodeCount = BehaviorTree->BTGraph
        ? BehaviorTree->BTGraph->Nodes.Num()
        : 0;
    ReadConfiguration.SourceMode = ESekiroLuaBehaviorTreeSourceMode::BehaviorTree;
    TestTrue(
        TEXT("Source Mode switch only writes metadata"),
        USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
            BehaviorTree,
            ReadConfiguration));
    TestEqual(
        TEXT("Source Mode switch does not change Graph"),
        BehaviorTree->BTGraph ? BehaviorTree->BTGraph->Nodes.Num() : 0,
        GraphNodeCount);
    Diagnostics.Reset();
    TestTrue(
        TEXT("Check Lua succeeds without rebuilding Graph"),
        USekiroBehaviorTreeFactoryLibrary::CheckConfiguredBehaviorTree(
            BehaviorTree,
            Diagnostics));
    TestEqual(
        TEXT("Check Lua does not change Graph"),
        BehaviorTree->BTGraph ? BehaviorTree->BTGraph->Nodes.Num() : 0,
        GraphNodeCount);

    ReadConfiguration = FSekiroLuaBehaviorTreeAssetConfiguration();
    USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
        BehaviorTree,
        ReadConfiguration);
    TestEqual(
        TEXT("Actual Blackboard path is written back"),
        ReadConfiguration.BlackboardPackagePath,
        FString(TEXT("/Game/__SekiroLuaBTTests/BB_Configured")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaBehaviorTreeTaskBoundaryTest,
    "Sekiro.LuaBehaviorTree.Runtime.GenericTaskBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证通用 Lua Task 强制节点实例化、暴露必要配置，且空闲状态拒绝显式 Finish。
 * 测试不创建 BehaviorTreeComponent、不调用 Lua、不启动 PIE。
 *
 * @param Parameters Automation 框架保留参数，本测试不使用。
 * @return 实例化和生命周期边界断言均成立时返回 true。
 */
bool FSekiroLuaBehaviorTreeTaskBoundaryTest::RunTest(const FString& Parameters)
{
    USekiroLuaBehaviorTreeTask* Task = NewObject<USekiroLuaBehaviorTreeTask>();
    TestTrue(TEXT("Generic Lua task creates a node instance"), Task->HasInstance());
    TestNotNull(
        TEXT("LuaModuleName is available to generic reflection generation"),
        FindFProperty<FProperty>(
            USekiroLuaBehaviorTreeTask::StaticClass(),
            GET_MEMBER_NAME_CHECKED(
                USekiroLuaBehaviorTreeTask,
                LuaModuleName)));
    TestNotNull(
        TEXT("Configuration is available to generic reflection generation"),
        FindFProperty<FProperty>(
            USekiroLuaBehaviorTreeTask::StaticClass(),
            GET_MEMBER_NAME_CHECKED(
                USekiroLuaBehaviorTreeTask,
                Configuration)));
    TestFalse(
        TEXT("Idle task rejects latent Finish"),
        Task->FinishLuaTask(ESekiroLuaBehaviorTreeTaskResult::Succeeded));
    TestFalse(TEXT("Idle task is not active"), Task->IsTaskActive());
    TestNull(TEXT("Idle task has no AIController"), Task->GetTaskAIController());
    TestNull(TEXT("Idle task has no Pawn"), Task->GetTaskPawn());
    TestNull(TEXT("Idle task has no Blackboard"), Task->GetTaskBlackboard());
    return true;
}

#endif
