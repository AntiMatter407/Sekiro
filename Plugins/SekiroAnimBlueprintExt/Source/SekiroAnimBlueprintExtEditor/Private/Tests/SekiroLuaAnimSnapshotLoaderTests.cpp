#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SekiroLuaAnimSnapshotLoader.h"
#include "SekiroLuaAnimSnapshotViewer.h"

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
    TestTrue(
        TEXT("首帧描述为开始记录"),
        Document.Frames[0]->ChangeDescription.Contains(TEXT("开始记录")));
    TestFalse(
        TEXT("首帧摘要保持一句话"),
        Document.Frames[0]->ChangeDescription.Contains(TEXT("；")));
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& DetailedFrame = Document.Frames[1];
    TestTrue(
        TEXT("变化描述包含状态机变化"),
        DetailedFrame->ChangeDetails.Contains(TEXT("状态 Locomotion [7]: <无> → Run")));
    TestTrue(
        TEXT("关键变化标题使用具体节点变化"),
        DetailedFrame->ChangeTitle.Contains(TEXT("Locomotion")));
    TestTrue(
        TEXT("变化描述包含 Lua 动画开始"),
        DetailedFrame->ChangeDetails.Contains(TEXT("动画开始 Run_Fwd")));
    TestTrue(
        TEXT("变化描述包含变量变化"),
        DetailedFrame->ChangeDetails.Contains(TEXT("变量 CombatActionState = LightAttack")));
    TestTrue(
        TEXT("变化描述包含曲线变化"),
        DetailedFrame->ChangeDetails.Contains(TEXT("曲线 Attribute.CanCancelToGuard = 1.0000")));
    TestTrue(
        TEXT("变化描述包含 Transition 结果"),
        DetailedFrame->ChangeDetails.Contains(TEXT("Transition Idle_Run: <无> → 通过")));
    TestFalse(
        TEXT("左侧摘要保持一句话"),
        DetailedFrame->ChangeDescription.Contains(TEXT("；")));
    TestTrue(
        TEXT("左侧摘要概括其余变化"),
        DetailedFrame->ChangeDescription.Contains(TEXT("另有")));
    TestTrue(
        TEXT("搜索可命中 Lua 动画名"),
        SSekiroLuaAnimSnapshotViewer::DoesFrameMatchSearchForTesting(
            DetailedFrame,
            TEXT("run_fwd")));
    TestTrue(
        TEXT("搜索可命中变量名"),
        SSekiroLuaAnimSnapshotViewer::DoesFrameMatchSearchForTesting(
            DetailedFrame,
            TEXT("CombatActionState")));
    TestTrue(
        TEXT("搜索可命中 Transition 标识"),
        SSekiroLuaAnimSnapshotViewer::DoesFrameMatchSearchForTesting(
            DetailedFrame,
            TEXT("Idle_Run")));
    TestFalse(
        TEXT("搜索无关文本不命中"),
        SSekiroLuaAnimSnapshotViewer::DoesFrameMatchSearchForTesting(
            DetailedFrame,
            TEXT("NoSuchSnapshotValue")));
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

    const FString RootPriorityJsonLines =
        TEXT("{\"FrameIndex\":0,\"SessionElapsedSeconds\":0.0,\"CaptureReason\":\"Start\",\"Roots\":[{\"NodeType\":\"Root\",\"Depth\":0,\"ChainId\":1,\"Children\":[{\"NodeType\":\"Slot\",\"Depth\":1,\"ChainId\":2,\"Inputs\":{\"SlotName\":\"FullBodySlot\"},\"Children\":[{\"NodeType\":\"StateMachine\",\"Depth\":3,\"ChainId\":3,\"MachineName\":\"Locomotion\",\"CurrentState\":\"Idle\"}]}]}]}\n")
        TEXT("{\"FrameIndex\":1,\"SessionElapsedSeconds\":0.1,\"CaptureReason\":\"StateChanged\",\"Roots\":[{\"NodeType\":\"Root\",\"Depth\":0,\"ChainId\":1,\"Children\":[{\"NodeType\":\"Slot\",\"Depth\":1,\"ChainId\":2,\"Inputs\":{\"SlotName\":\"FullBodySlot\"},\"Children\":[{\"NodeType\":\"StateMachine\",\"Depth\":3,\"ChainId\":3,\"MachineName\":\"Locomotion\",\"CurrentState\":\"Run\"},{\"NodeType\":\"Montage\",\"Depth\":2,\"ChainId\":4,\"ResolvedAnimationName\":\"Attack.Light\"}]}]}]}\n");
    FSekiroLuaAnimSnapshotDocument RootPriorityDocument;
    FSekiroLuaAnimSnapshotLoader::ParseJsonLines(
        RootPriorityJsonLines,
        TEXT("RootPriority.jsonl"),
        RootPriorityDocument);
    TestEqual(
        TEXT("Root 优先级样本包含两帧"),
        RootPriorityDocument.Frames.Num(),
        2);
    if (RootPriorityDocument.Frames.Num() == 2)
    {
        TestEqual(
            TEXT("近 Root 的 Slot Montage 优先于深层 Locomotion 状态变化"),
            RootPriorityDocument.Frames[1]->ChangeTitle,
            FString(TEXT("Slot FullBodySlot：Montage 开始 Attack.Light")));
    }
    return true;
}

#endif
