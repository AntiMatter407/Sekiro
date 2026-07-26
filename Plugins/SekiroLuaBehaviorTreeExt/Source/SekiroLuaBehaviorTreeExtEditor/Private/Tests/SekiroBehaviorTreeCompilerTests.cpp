#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "SekiroBehaviorTreeFactoryLibrary.h"
#include "SekiroBehaviorTreeIRLibrary.h"
#include "SekiroBehaviorTreeReflectionWriter.h"

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

        FSekiroBehaviorTreeIRNode& Root = IR.Nodes.AddDefaulted_GetRef();
        Root.Id = IR.RootNodeId;
        Root.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTComposite_Selector"));
        Root.DisplayName = TEXT("Root");
        Root.DeclarationOrder = 1;

        FSekiroBehaviorTreeIRNode& Wait = IR.Nodes.AddDefaulted_GetRef();
        Wait.Id = TEXT("Node:Wait");
        Wait.ParentId = Root.Id;
        Wait.ClassPath = FSoftClassPath(TEXT("/Script/AIModule.BTTask_Wait"));
        Wait.DisplayName = TEXT("Wait");
        Wait.DeclarationOrder = 2;

        FSekiroBehaviorTreeIRValue& WaitTime = IR.Values.AddDefaulted_GetRef();
        WaitTime.Type = ESekiroBehaviorTreeValueType::Float;
        WaitTime.FloatValue = 0.5;
        FSekiroBehaviorTreeIRProperty& WaitTimeProperty = Wait.Properties.AddDefaulted_GetRef();
        WaitTimeProperty.Name = TEXT("WaitTime");
        WaitTimeProperty.ValueIndex = 0;
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

#endif
