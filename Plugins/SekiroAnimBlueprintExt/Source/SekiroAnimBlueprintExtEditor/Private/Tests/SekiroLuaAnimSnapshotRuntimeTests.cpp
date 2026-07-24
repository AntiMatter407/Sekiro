#include "SekiroLuaAnimDebugRuntime.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimDebugCommandTest,
    "Sekiro.LuaAnimDebug.Runtime.DebugCommandReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Debug 无参开启、非法参数替换为非激活帮助页、Off 关闭的单一视图语义。
 * 测试在游戏线程调用 Runtime 测试钩子，不启动 PIE，也不创建资产。
 */
bool FSekiroLuaAnimDebugCommandTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSekiroLuaAnimDebugRuntime::ResetForTesting();

    FSekiroLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    TestTrue(TEXT("No arguments enables hierarchy"), FSekiroLuaAnimDebugRuntime::IsDebugEnabledForTesting());

    FSekiroLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting({ TEXT("Unexpected") });
    TestFalse(TEXT("Invalid arguments replace hierarchy with help"), FSekiroLuaAnimDebugRuntime::IsDebugEnabledForTesting());

    FSekiroLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    FSekiroLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting({ TEXT("Off") });
    TestFalse(TEXT("Off replaces and closes hierarchy"), FSekiroLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    FSekiroLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimCompleteHierarchyTest,
    "Sekiro.LuaAnimDebug.Runtime.CompleteActiveHierarchy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证实时层级从根节点递归显示全部活跃分支，并保留 Slot、Sequence 和 Montage 动画信息。
 * 测试只构造公共快照结构并调用真实文本格式化逻辑，不访问世界、文件或动画资产。
 */
bool FSekiroLuaAnimCompleteHierarchyTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    const FSekiroLuaAnimDebugNode ParsedMontage =
        FSekiroLuaAnimDebugRuntime::ParseDebugLineForTesting(
            TEXT("Montage('AttackDynamicMontage') Anim('a00_000100') P(0.42) W(65%)"));
    TestEqual(TEXT("Montage debug item gets an explicit node type"), ParsedMontage.NodeType, FString(TEXT("Montage")));
    TestEqual(TEXT("Montage animation is parsed"), ParsedMontage.NativeAssetName, FString(TEXT("a00_000100")));
    TestEqual(TEXT("Montage position is parsed"), ParsedMontage.Inputs.FindRef(TEXT("Position")), FString(TEXT("0.42")));
    TestEqual(TEXT("Montage weight is normalized"), ParsedMontage.Inputs.FindRef(TEXT("MontageWeight")), FString(TEXT("0.65")));

    FSekiroLuaAnimDebugFrame Frame;
    Frame.AnimInstancePath = TEXT("/Game/TestAnimInstance");
    Frame.LuaModuleName = TEXT("Animation.Test.ABP_Test");

    FSekiroLuaAnimDebugNode Root;
    Root.NodeType = TEXT("FAnimNode_Root");
    Root.AbsoluteWeight = 1.0f;
    FSekiroLuaAnimDebugNode Slot;
    Slot.NodeType = TEXT("FAnimNode_Slot");
    Slot.AbsoluteWeight = 1.0f;
    Slot.Inputs.Add(TEXT("SlotName"), TEXT("CombatFullBodySlot"));
    FSekiroLuaAnimDebugNode Source;
    Source.NodeType = TEXT("FAnimNode_SequencePlayer");
    Source.AbsoluteWeight = 0.35f;
    Source.ResolvedAnimationName = TEXT("AnimAssets.Locomotion.Idle");
    FSekiroLuaAnimDebugNode Montage;
    Montage.NodeType = TEXT("Montage");
    Montage.AbsoluteWeight = 0.65f;
    Montage.ResolvedAnimationName = TEXT("AnimAssets.Combat.LightAttack_Right");
    Montage.Inputs.Add(TEXT("MontageWeight"), TEXT("0.65"));
    Slot.Children.Add(Source);
    Slot.Children.Add(Montage);
    Root.Children.Add(Slot);
    Frame.Roots.Add(Root);

    const FString Text = FSekiroLuaAnimDebugRuntime::BuildRealtimeTextForTesting(Frame);
    TestTrue(TEXT("Root node is visible"), Text.Contains(TEXT("FAnimNode_Root")));
    TestTrue(TEXT("Slot node is visible"), Text.Contains(TEXT("FAnimNode_Slot")));
    TestTrue(TEXT("Source branch remains visible"), Text.Contains(TEXT("AnimAssets.Locomotion.Idle")));
    TestTrue(TEXT("Montage branch remains visible"), Text.Contains(TEXT("AnimAssets.Combat.LightAttack_Right")));
    TestTrue(TEXT("Slot input is visible"), Text.Contains(TEXT("SlotName=CombatFullBodySlot")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSekiroLuaAnimSnapshotSessionTest,
    "Sekiro.LuaAnimDebug.Runtime.SnapshotSessionReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证有秒参数时启用定时采样、无参数时仅采样状态变化，以及新 Session 替换和 Stop 关闭。
 * 文件仅写入 Saved/Automation/LuaAnimDebug 并在测试结束删除。
 */
bool FSekiroLuaAnimSnapshotSessionTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FSekiroLuaAnimDebugRuntime::ResetForTesting();
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LuaAnimDebug"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString FirstPath = FPaths::Combine(Directory, TEXT("First.jsonl"));
    const FString SecondPath = FPaths::Combine(Directory, TEXT("Second.jsonl"));

    TestTrue(
        TEXT("Explicit interval session starts"),
        FSekiroLuaAnimDebugRuntime::ApplySnapshotArgumentsForTesting(
            { TEXT("0.2") },
            FirstPath));
    TestTrue(TEXT("Snapshot is active"), FSekiroLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
    TestEqual(TEXT("First interval is applied"), FSekiroLuaAnimDebugRuntime::GetSnapshotIntervalForTesting(), 0.2f);
    TestTrue(
        TEXT("Explicit interval enables timed sampling"),
        FSekiroLuaAnimDebugRuntime::IsSnapshotIntervalSamplingEnabledForTesting());
    TestTrue(
        TEXT("No-argument session replaces first"),
        FSekiroLuaAnimDebugRuntime::ApplySnapshotArgumentsForTesting(
            TArray<FString>(),
            SecondPath));
    TestFalse(
        TEXT("No arguments disable timed sampling"),
        FSekiroLuaAnimDebugRuntime::IsSnapshotIntervalSamplingEnabledForTesting());
    TestEqual(TEXT("Only latest path is current"), FSekiroLuaAnimDebugRuntime::GetSnapshotSessionPath(), SecondPath);

    FString FirstJsonLine;
    TestTrue(TEXT("Replacing session flushes first file"), FFileHelper::LoadFileToString(FirstJsonLine, *FirstPath));
    TestTrue(TEXT("First session contains its own Start frame"), FirstJsonLine.Contains(TEXT("\"FrameIndex\":0")));

    FSekiroLuaAnimDebugRuntime::StopSnapshotSession();
    TestFalse(TEXT("Stop closes current session"), FSekiroLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
    FString JsonLine;
    TestTrue(TEXT("Start capture was flushed"), FFileHelper::LoadFileToString(JsonLine, *SecondPath));
    TestTrue(TEXT("JSONL contains Start capture reason"), JsonLine.Contains(TEXT("\"CaptureReason\":\"Start\"")));
    TestTrue(TEXT("JSONL contains schema version"), JsonLine.Contains(TEXT("\"SchemaVersion\":2")));
    TestTrue(TEXT("JSONL contains variable object"), JsonLine.Contains(TEXT("\"Variables\":{}")));
    TestTrue(TEXT("JSONL contains curve object"), JsonLine.Contains(TEXT("\"Curves\":{}")));
    TArray<FString> JsonLines;
    JsonLine.ParseIntoArrayLines(JsonLines, true);
    TestEqual(TEXT("Replacement session starts with exactly one immediate frame"), JsonLines.Num(), 1);

    IFileManager::Get().Delete(*FirstPath, false, true);
    IFileManager::Get().Delete(*SecondPath, false, true);
    FSekiroLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

#endif
