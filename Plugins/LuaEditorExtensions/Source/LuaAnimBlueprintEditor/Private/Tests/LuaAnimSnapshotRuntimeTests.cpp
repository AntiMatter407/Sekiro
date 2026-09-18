#include "LuaAnimDebugRuntime.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimDebugCommandTest,
    "Lua.LuaAnimDebug.Runtime.DebugCommandReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Debug 无参开启、非法参数替换为非激活帮助页、Off 关闭的单一视图语义。
 * 测试在游戏线程调用 Runtime 测试钩子，不启动 PIE，也不创建资产。
 */
bool FLuaAnimDebugCommandTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FLuaAnimDebugRuntime::ResetForTesting();

    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    TestTrue(TEXT("No arguments enables hierarchy"), FLuaAnimDebugRuntime::IsDebugEnabledForTesting());

    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting({ TEXT("Unexpected") });
    TestFalse(TEXT("Invalid arguments replace hierarchy with help"), FLuaAnimDebugRuntime::IsDebugEnabledForTesting());

    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting({ TEXT("Off") });
    TestFalse(TEXT("Off replaces and closes hierarchy"), FLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    FLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimCompleteHierarchyTest,
    "Lua.LuaAnimDebug.Runtime.CompleteActiveHierarchy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证实时层级从根节点递归显示全部活跃分支，并保留 Slot、Sequence 和 Montage 动画信息。
 * 测试只构造公共快照结构并调用真实文本格式化逻辑，不访问世界、文件或动画资产。
 */
bool FLuaAnimCompleteHierarchyTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    const FLuaAnimDebugNode ParsedMontage =
        FLuaAnimDebugRuntime::ParseDebugLineForTesting(
            TEXT("Montage('AttackDynamicMontage') Anim('a00_000100') P(0.42) W(65%)"));
    TestEqual(TEXT("Montage debug item gets an explicit node type"), ParsedMontage.NodeType, FString(TEXT("Montage")));
    TestEqual(TEXT("Montage animation is parsed"), ParsedMontage.NativeAssetName, FString(TEXT("a00_000100")));
    TestEqual(TEXT("Montage position is parsed"), ParsedMontage.Inputs.FindRef(TEXT("Position")), FString(TEXT("0.42")));
    TestEqual(TEXT("Montage weight is normalized"), ParsedMontage.Inputs.FindRef(TEXT("MontageWeight")), FString(TEXT("0.65")));

    FLuaAnimDebugFrame Frame;
    Frame.AnimInstancePath = TEXT("/Game/TestAnimInstance");
    Frame.LuaModuleName = TEXT("Animation.Test.ABP_Test");

    FLuaAnimDebugNode Root;
    Root.NodeType = TEXT("FAnimNode_Root");
    Root.AbsoluteWeight = 1.0f;
    FLuaAnimDebugNode Slot;
    Slot.NodeType = TEXT("FAnimNode_Slot");
    Slot.AbsoluteWeight = 1.0f;
    Slot.Inputs.Add(TEXT("SlotName"), TEXT("CombatFullBodySlot"));
    FLuaAnimDebugNode Source;
    Source.NodeType = TEXT("FAnimNode_SequencePlayer");
    Source.AbsoluteWeight = 0.35f;
    Source.ResolvedAnimationName = TEXT("AnimAssets.Locomotion.Idle");
    FLuaAnimDebugNode Montage;
    Montage.NodeType = TEXT("Montage");
    Montage.AbsoluteWeight = 0.65f;
    Montage.ResolvedAnimationName = TEXT("AnimAssets.Combat.LightAttack_Right");
    Montage.Inputs.Add(TEXT("MontageWeight"), TEXT("0.65"));
    Slot.Children.Add(Source);
    Slot.Children.Add(Montage);
    Root.Children.Add(Slot);
    Frame.Roots.Add(Root);

    const FString Text = FLuaAnimDebugRuntime::BuildRealtimeTextForTesting(Frame);
    TestTrue(TEXT("Root node is visible"), Text.Contains(TEXT("FAnimNode_Root")));
    TestTrue(TEXT("Slot node is visible"), Text.Contains(TEXT("FAnimNode_Slot")));
    TestTrue(TEXT("Source branch remains visible"), Text.Contains(TEXT("AnimAssets.Locomotion.Idle")));
    TestTrue(TEXT("Montage branch remains visible"), Text.Contains(TEXT("AnimAssets.Combat.LightAttack_Right")));
    TestTrue(TEXT("Slot input is visible"), Text.Contains(TEXT("SlotName=CombatFullBodySlot")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimSnapshotSessionTest,
    "Lua.LuaAnimDebug.Runtime.SnapshotSessionReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证有秒参数时启用定时采样、无参数时仅采样状态变化，以及新 Session 替换和 Stop 关闭。
 * 文件仅写入 Saved/Automation/LuaAnimDebug 并在测试结束删除。
 */
bool FLuaAnimSnapshotSessionTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FLuaAnimDebugRuntime::ResetForTesting();
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LuaAnimDebug"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString FirstPath = FPaths::Combine(Directory, TEXT("First.jsonl"));
    const FString SecondPath = FPaths::Combine(Directory, TEXT("Second.jsonl"));

    TestTrue(
        TEXT("Explicit interval session starts"),
        FLuaAnimDebugRuntime::ApplySnapshotArgumentsForTesting(
            { TEXT("0.2") },
            FirstPath));
    TestTrue(TEXT("Snapshot is active"), FLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
    TestEqual(TEXT("First interval is applied"), FLuaAnimDebugRuntime::GetSnapshotIntervalForTesting(), 0.2f);
    TestTrue(
        TEXT("Explicit interval enables timed sampling"),
        FLuaAnimDebugRuntime::IsSnapshotIntervalSamplingEnabledForTesting());
    TestTrue(
        TEXT("No-argument session replaces first"),
        FLuaAnimDebugRuntime::ApplySnapshotArgumentsForTesting(
            TArray<FString>(),
            SecondPath));
    TestFalse(
        TEXT("No arguments disable timed sampling"),
        FLuaAnimDebugRuntime::IsSnapshotIntervalSamplingEnabledForTesting());
    TestEqual(TEXT("Only latest path is current"), FLuaAnimDebugRuntime::GetSnapshotSessionPath(), SecondPath);

    FString FirstJsonLine;
    TestTrue(TEXT("Replacing session flushes first file"), FFileHelper::LoadFileToString(FirstJsonLine, *FirstPath));
    TestTrue(TEXT("First session contains its own Start frame"), FirstJsonLine.Contains(TEXT("\"FrameIndex\":0")));

    FLuaAnimDebugRuntime::StopSnapshotSession();
    TestFalse(TEXT("Stop closes current session"), FLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
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
    FLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimPIEStartResetTest,
    "Lua.LuaAnimDebug.Runtime.PIEStartReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 PIE 开始入口会覆盖编辑器阶段残留的 Debug/Snapshot 开关，并 Flush 已打开文件。
 * 测试直接调用生产重置入口，不启动 PIE；临时 JSONL 只写入 Saved/Automation/LuaAnimDebug。
 */
bool FLuaAnimPIEStartResetTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FLuaAnimDebugRuntime::ResetForTesting();
    const FString Directory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Automation"),
        TEXT("LuaAnimDebug"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString SnapshotPath = FPaths::Combine(Directory, TEXT("PIEStartReset.jsonl"));

    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    TestTrue(
        TEXT("Hierarchy debug starts enabled"),
        FLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    TestTrue(
        TEXT("Snapshot starts enabled"),
        FLuaAnimDebugRuntime::StartSnapshotForTesting(0.15f, SnapshotPath));

    FLuaAnimDebugRuntime::ResetForPIEStart();

    TestFalse(
        TEXT("PIE start disables hierarchy debug"),
        FLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    TestFalse(
        TEXT("PIE start closes snapshot"),
        FLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
    TestFalse(
        TEXT("PIE start disables all sampling"),
        FLuaAnimDebugRuntime::IsSamplingEnabled());

    FString JsonLine;
    TestTrue(
        TEXT("PIE start flushes the snapshot file"),
        FFileHelper::LoadFileToString(JsonLine, *SnapshotPath));
    TestTrue(
        TEXT("Flushed snapshot retains its Start frame"),
        JsonLine.Contains(TEXT("\"CaptureReason\":\"Start\"")));

    IFileManager::Get().Delete(*SnapshotPath, false, true);
    FLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimPIEEndResetTest,
    "Lua.LuaAnimDebug.Runtime.PIEEndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 PIE 结束入口会同时关闭实时 Debug 与 Snapshot、Flush 文件并停止全部采样。
 * 测试直接调用生产重置入口，不启动 PIE；临时 JSONL 只写入 Saved/Automation/LuaAnimDebug。
 */
bool FLuaAnimPIEEndResetTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FLuaAnimDebugRuntime::ResetForTesting();
    const FString Directory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Automation"),
        TEXT("LuaAnimDebug"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString SnapshotPath = FPaths::Combine(Directory, TEXT("PIEEndReset.jsonl"));

    FLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(TArray<FString>());
    TestTrue(
        TEXT("Hierarchy debug starts enabled"),
        FLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    TestTrue(
        TEXT("Snapshot starts enabled"),
        FLuaAnimDebugRuntime::StartSnapshotForTesting(0.15f, SnapshotPath));
    TestTrue(
        TEXT("Sampling is enabled before PIE end"),
        FLuaAnimDebugRuntime::IsSamplingEnabled());

    FLuaAnimDebugRuntime::ResetForPIEEnd();

    TestFalse(
        TEXT("PIE end disables hierarchy debug"),
        FLuaAnimDebugRuntime::IsDebugEnabledForTesting());
    TestFalse(
        TEXT("PIE end closes snapshot"),
        FLuaAnimDebugRuntime::IsSnapshotActiveForTesting());
    TestFalse(
        TEXT("PIE end disables all sampling"),
        FLuaAnimDebugRuntime::IsSamplingEnabled());
    TestEqual(
        TEXT("PIE end preserves the latest snapshot path"),
        FLuaAnimDebugRuntime::GetSnapshotSessionPath(),
        SnapshotPath);

    FString JsonLine;
    TestTrue(
        TEXT("PIE end flushes the snapshot file"),
        FFileHelper::LoadFileToString(JsonLine, *SnapshotPath));
    TestTrue(
        TEXT("Flushed snapshot retains its Start frame"),
        JsonLine.Contains(TEXT("\"CaptureReason\":\"Start\"")));

    IFileManager::Get().Delete(*SnapshotPath, false, true);
    FLuaAnimDebugRuntime::ResetForTesting();
    return true;
}

#endif
