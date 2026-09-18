#include "LuaAnimBlueprintAutoCompileScheduler.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "LuaAnimBlueprintExtension.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimBlueprintSyncStatusTransitionTest,
    "Lua.AnimGraphIR.Sync.StatusTransitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证同步基线、Lua 文件变化以及两侧冲突的纯状态迁移；不创建 Graph、文件或 PIE 世界。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FLuaAnimBlueprintSyncStatusTransitionTest::RunTest(
    const FString& Parameters)
{
    ULuaAnimBlueprintExtension* Extension =
        NewObject<ULuaAnimBlueprintExtension>(GetTransientPackage());
    TestNotNull(TEXT("Transient sync extension is created"), Extension);
    if (Extension == nullptr) return true;

    TestEqual(
        TEXT("New extension has never synchronized"),
        Extension->SyncStatus,
        ELuaAnimBlueprintSyncStatus::NeverSynchronized);
    Extension->MarkSynchronized(TEXT("BlueprintHash"), TEXT("LuaHash"));
    TestEqual(
        TEXT("Successful synchronization records InSync"),
        Extension->SyncStatus,
        ELuaAnimBlueprintSyncStatus::InSync);
    TestEqual(
        TEXT("Blueprint baseline hash is recorded"),
        Extension->LastSynchronizedBlueprintHash,
        FString(TEXT("BlueprintHash")));
    TestEqual(
        TEXT("Lua baseline hash is recorded"),
        Extension->LastSynchronizedLuaHash,
        FString(TEXT("LuaHash")));

    Extension->MarkLuaChanged(TEXT("Watcher observed Lua change."));
    TestEqual(
        TEXT("Watcher marks only Lua changed"),
        Extension->SyncStatus,
        ELuaAnimBlueprintSyncStatus::LuaChanged);
    Extension->SyncStatus = ELuaAnimBlueprintSyncStatus::BlueprintChanged;
    Extension->MarkLuaChanged(TEXT("Watcher observed a second Lua change."));
    TestEqual(
        TEXT("Lua change after Blueprint change becomes BothChanged"),
        Extension->SyncStatus,
        ELuaAnimBlueprintSyncStatus::BothChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLuaAnimBlueprintAutoCompileSchedulerTest,
    "Lua.AnimGraphIR.DirtyCompile.SourceChangeTracker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证 Lua 文件事件过滤、内容指纹去重、事件合并、立即标脏请求消费，以及关闭时清空未处理请求。
 * 跟踪器不接收 PIE 参数且没有编译入口，因此同一行为也证明 PIE 期间文件变化只会形成 Dirty 请求。
 * 测试只在 Saved/Automation 创建并清理一个临时 Lua 文件，不调用 UnLua，也不创建或编译 Blueprint。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FLuaAnimBlueprintAutoCompileSchedulerTest::RunTest(const FString& Parameters)
{
    const FFileChangeData LuaAdded(
        TEXT("F:/Project/Content/Script/Animation/ABP.lua"),
        FFileChangeData::FCA_Added);
    const FFileChangeData LuaModifiedUppercase(
        TEXT("F:/Project/Content/Script/Animation/Layer.LUA"),
        FFileChangeData::FCA_Modified);
    const FFileChangeData LuaRemoved(
        TEXT("F:/Project/Content/Script/Animation/Old.lua"),
        FFileChangeData::FCA_Removed);
    const FFileChangeData TextModified(
        TEXT("F:/Project/Content/Script/Animation/Readme.txt"),
        FFileChangeData::FCA_Modified);
    const FFileChangeData LuaUnknown(
        TEXT("F:/Project/Content/Script/Animation/Unknown.lua"),
        FFileChangeData::FCA_Unknown);
    const FFileChangeData LuaRescan(
        TEXT("F:/Project/Content/Script/Animation/Rescan.lua"),
        FFileChangeData::FCA_RescanRequired);

    TestTrue(
        TEXT("Added Lua file is relevant"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(LuaAdded));
    TestTrue(
        TEXT("Lua extension comparison is case-insensitive"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(
            LuaModifiedUppercase));
    TestTrue(
        TEXT("Removed Lua file is relevant"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(LuaRemoved));
    TestFalse(
        TEXT("Non-Lua file is ignored"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(TextModified));
    TestFalse(
        TEXT("Unknown Lua event is ignored"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(LuaUnknown));
    TestFalse(
        TEXT("Rescan event is ignored"),
        FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(LuaRescan));

    FLuaAnimBlueprintAutoCompileScheduler Scheduler;
    TestFalse(
        TEXT("Irrelevant batch does not queue dirty work"),
        Scheduler.QueueFileChanges({ TextModified, LuaUnknown }));
    TestFalse(
        TEXT("Empty tracker has no dirty request"),
        Scheduler.ConsumeDirtyRequest());

    TestTrue(
        TEXT("Relevant batch requests game-thread dirty dispatch"),
        Scheduler.QueueFileChanges({ TextModified, LuaAdded }));
    TestFalse(
        TEXT("Additional file events merge into the scheduled dispatch"),
        Scheduler.QueueFileChanges({ LuaModifiedUppercase }));
    TestTrue(
        TEXT("Dirty request is immediately consumable without polling or debounce"),
        Scheduler.ConsumeDirtyRequest());
    TestFalse(
        TEXT("Consumed dirty request does not run twice"),
        Scheduler.ConsumeDirtyRequest());

    TestTrue(
        TEXT("A later Lua change schedules a new dirty dispatch"),
        Scheduler.QueueFileChanges({ LuaRemoved }));
    TestTrue(
        TEXT("PIE does not suppress source dirty consumption"),
        Scheduler.ConsumeDirtyRequest());

    Scheduler.QueueFileChanges({ LuaAdded });
    Scheduler.Reset();
    TestFalse(
        TEXT("Reset clears pending request"),
        Scheduler.ConsumeDirtyRequest());

    const FString SnapshotRoot = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Automation/LuaAnimSourceSnapshot"));
    const FString SnapshotFile = FPaths::Combine(SnapshotRoot, TEXT("Tracked.lua"));
    IFileManager::Get().DeleteDirectory(*SnapshotRoot, false, true);
    IFileManager::Get().MakeDirectory(*SnapshotRoot, true);
    TestTrue(
        TEXT("Snapshot test Lua file is created"),
        FFileHelper::SaveStringToFile(TEXT("return { Value = 1 }\n"), *SnapshotFile));

    FLuaAnimBlueprintAutoCompileScheduler SnapshotScheduler;
    SnapshotScheduler.InitializeSourceSnapshot(SnapshotRoot);
    const FFileChangeData SameContentModified(
        SnapshotFile,
        FFileChangeData::FCA_Modified);
    TestFalse(
        TEXT("Timestamp-only Modified event is ignored when content hash is unchanged"),
        SnapshotScheduler.QueueFileChanges({ SameContentModified }));

    TestTrue(
        TEXT("Snapshot test Lua content is updated"),
        FFileHelper::SaveStringToFile(TEXT("return { Value = 2 }\n"), *SnapshotFile));
    TestTrue(
        TEXT("Modified event queues work when Lua content hash changes"),
        SnapshotScheduler.QueueFileChanges({ SameContentModified }));
    TestFalse(
        TEXT("Repeated Modified event for the same content is deduplicated"),
        SnapshotScheduler.QueueFileChanges({ SameContentModified }));
    TestTrue(
        TEXT("Changed content request remains consumable"),
        SnapshotScheduler.ConsumeDirtyRequest());

    TestTrue(
        TEXT("Snapshot test Lua file is removed"),
        IFileManager::Get().Delete(*SnapshotFile, false, true));
    const FFileChangeData TrackedFileRemoved(
        SnapshotFile,
        FFileChangeData::FCA_Removed);
    TestTrue(
        TEXT("Removing a tracked Lua file queues source stale work"),
        SnapshotScheduler.QueueFileChanges({ TrackedFileRemoved }));
    TestTrue(
        TEXT("Removed content request is consumable"),
        SnapshotScheduler.ConsumeDirtyRequest());
    TestFalse(
        TEXT("Repeated removal of an untracked file is ignored"),
        SnapshotScheduler.QueueFileChanges({ TrackedFileRemoved }));
    IFileManager::Get().DeleteDirectory(*SnapshotRoot, false, true);
    return true;
}

#endif
