#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SekiroLuaAnimSnapshotLoader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimSnapshotLoaderTest,
    "Sekiro.AnimBlueprintExt.Snapshot.Loader.JsonLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证多帧排序、嵌套节点、Transition 字段以及单行损坏容错。
 * 由 Automation Framework 在编辑器测试线程入口调用；Parameters 未使用。
 * 返回所有断言是否通过，不访问文件系统或 UObject。
 */
bool FSekiroLuaAnimSnapshotLoaderTest::RunTest(const FString& Parameters)
{
    const FString JsonLines =
        TEXT("{\"SchemaVersion\":2,\"FrameIndex\":2,\"UtcTimestamp\":\"2026-07-22T01:00:00Z\",\"SessionElapsedSeconds\":0.2,\"CaptureReason\":\"StateChanged\",\"FutureField\":42,\"Variables\":{\"CombatActionState\":\"LightAttack\",\"Speed\":\"320.0\"},\"Curves\":{\"Attribute.CanCancelToGuard\":1.0,\"MorphTarget.Face\":0.25},\"Roots\":[{\"NodeType\":\"StateMachine\",\"PoseAlias\":\"Locomotion\",\"ChainId\":7,\"CurrentState\":\"Run\",\"Inputs\":{\"Speed\":\"320\"},\"StateWeights\":{\"Run\":0.75},\"Children\":[{\"NodeType\":\"SequencePlayer\",\"ResolvedAnimationName\":\"Run_Fwd\"}]}],\"Transitions\":[{\"TransitionId\":\"Idle_Run\",\"ExpressionLabel\":\"HasInput\",\"ParameterName\":\"bHasMovementInput\",\"ParameterType\":\"Bool\",\"ParameterValue\":\"true\",\"ExpectedValue\":\"true\",\"Threshold\":0.5,\"ExpressionResult\":true,\"RuleResult\":true,\"IsFinal\":true,\"EvaluatedUtcTimestamp\":\"2026-07-22T01:00:00Z\"}]}\n")
        TEXT("{broken json}\n")
        TEXT("{\"FrameIndex\":1,\"UtcTimestamp\":\"2026-07-22T00:59:59Z\",\"SessionElapsedSeconds\":0.1,\"CaptureReason\":\"Start\"}\n");

    FSekiroLuaAnimSnapshotDocument Document;
    FSekiroLuaAnimSnapshotLoader::ParseJsonLines(
        JsonLines,
        TEXT("AutomationMemory.jsonl"),
        Document);

    TestEqual(TEXT("坏行不会阻止两帧加载"), Document.Frames.Num(), 2);
    TestEqual(TEXT("坏行生成一条提示"), Document.Warnings.Num(), 1);
    if (Document.Frames.Num() != 2) return false;

    TestEqual(TEXT("帧按相对秒排序"), Document.Frames[0]->FrameIndex, static_cast<int64>(1));
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& DetailedFrame = Document.Frames[1];
    TestEqual(TEXT("解析全部变量"), DetailedFrame->Variables.Num(), 2);
    TestEqual(
        TEXT("解析战斗状态变量"),
        DetailedFrame->Variables.FindRef(TEXT("CombatActionState")),
        FString(TEXT("LightAttack")));
    TestEqual(TEXT("解析曲线值"), DetailedFrame->Curves.Num(), 2);
    TestEqual(
        TEXT("解析节点使用的属性曲线"),
        DetailedFrame->Curves.FindRef(TEXT("Attribute.CanCancelToGuard")),
        1.0);
    TestEqual(TEXT("嵌套帧包含一个根"), DetailedFrame->Roots.Num(), 1);
    if (DetailedFrame->Roots.IsEmpty()) return false;

    const TSharedPtr<FSekiroLuaAnimSnapshotNode>& RootNode = DetailedFrame->Roots[0];
    TestEqual(TEXT("解析状态机当前状态"), RootNode->CurrentState, FString(TEXT("Run")));
    TestEqual(TEXT("数字 ChainId 转为文本"), RootNode->ChainId, FString(TEXT("7")));
    TestEqual(TEXT("解析节点输入"), RootNode->Inputs.FindRef(TEXT("Speed")), FString(TEXT("320")));
    TestEqual(TEXT("解析状态权重"), RootNode->StateWeights.FindRef(TEXT("Run")), 0.75);
    TestEqual(TEXT("解析嵌套子节点"), RootNode->Children.Num(), 1);
    if (!RootNode->Children.IsEmpty())
    {
        TestEqual(
            TEXT("解析 Lua 动画名"),
            RootNode->Children[0]->ResolvedAnimationName,
            FString(TEXT("Run_Fwd")));
    }

    TestEqual(TEXT("解析 Transition"), DetailedFrame->Transitions.Num(), 1);
    if (!DetailedFrame->Transitions.IsEmpty())
    {
        const FSekiroLuaAnimTransitionSample& Sample = DetailedFrame->Transitions[0];
        TestEqual(TEXT("Transition 分组键"), Sample.TransitionId, FString(TEXT("Idle_Run")));
        TestEqual(TEXT("Transition 参数值"), Sample.ParameterValue, FString(TEXT("true")));
        TestEqual(TEXT("Transition 期望值"), Sample.ExpectedValue, FString(TEXT("true")));
        TestEqual(TEXT("Transition 阈值"), Sample.Threshold, FString(TEXT("0.5")));
        TestTrue(TEXT("Transition 表达式结果"), Sample.bExpressionResult);
        TestTrue(TEXT("Transition 最终结果"), Sample.bRuleResult);
        TestTrue(TEXT("Transition 最终采样标记"), Sample.bIsFinal);
    }
    return true;
}

#endif
