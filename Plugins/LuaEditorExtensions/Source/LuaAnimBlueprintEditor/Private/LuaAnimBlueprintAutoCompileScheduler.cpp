#include "LuaAnimBlueprintAutoCompileScheduler.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

/**
 * 扫描 Animation Lua 根目录并保存规范化路径与内容 MD5，作为后续 Modified 事件去重基线。
 * 只能在注册 DirectoryWatcher 前的游戏线程调用；函数只读取 Lua 文件，不访问 UObject 或触发编译。
 *
 * @param SourceRoot 要递归扫描的 Animation Lua 目录绝对路径；目录不存在时建立空基线。
 */
void FLuaAnimBlueprintAutoCompileScheduler::InitializeSourceSnapshot(
    const FString& SourceRoot)
{
    TArray<FString> LuaFiles;
    IFileManager::Get().FindFilesRecursive(
        LuaFiles,
        *SourceRoot,
        TEXT("*.lua"),
        true,
        false,
        false);

    TMap<FString, FMD5Hash> NewSourceFileHashes;
    for (FString LuaFile : LuaFiles)
    {
        FPaths::NormalizeFilename(LuaFile);
        const FMD5Hash FileHash = FMD5Hash::HashFile(*LuaFile);
        if (FileHash.IsValid()) NewSourceFileHashes.Add(LuaFile, FileHash);
    }

    FScopeLock Lock(&Mutex);
    SourceFileHashes = MoveTemp(NewSourceFileHashes);
    bSourceSnapshotInitialized = true;
}

/**
 * 过滤一批 DirectoryWatcher 事件，对 Added/Modified 文件比较内容指纹，并把真实变化合并为单次标脏请求。
 * 可从 watcher 回调线程或游戏线程调用；函数读取变化文件但不访问 UObject，也不触发编译。
 * 返回 true 仅表示调用方需要派发新的游戏线程任务，已有任务尚未消费时继续合并并返回 false。
 *
 * @param FileChanges DirectoryWatcher 提供的不可变变化数组。
 * @return 首次记录有效变化并需要派发游戏线程标脏任务时返回 true，否则返回 false。
 */
bool FLuaAnimBlueprintAutoCompileScheduler::QueueFileChanges(
    const TArray<FFileChangeData>& FileChanges)
{
    bool bContainsRelevantChange = false;
    for (const FFileChangeData& FileChange : FileChanges)
    {
        if (!IsRelevantLuaFileChange(FileChange)) continue;

        FString Filename = FileChange.Filename;
        FPaths::NormalizeFilename(Filename);
        if (FileChange.Action == FFileChangeData::FCA_Removed)
        {
            FScopeLock Lock(&Mutex);
            const bool bWasTracked = SourceFileHashes.Remove(Filename) > 0;
            bContainsRelevantChange |= bWasTracked || !bSourceSnapshotInitialized;
            continue;
        }

        const FMD5Hash CurrentHash = FMD5Hash::HashFile(*Filename);
        if (!CurrentHash.IsValid())
        {
            bContainsRelevantChange = true;
            continue;
        }

        FScopeLock Lock(&Mutex);
        const FMD5Hash* PreviousHash = SourceFileHashes.Find(Filename);
        if (PreviousHash != nullptr && *PreviousHash == CurrentHash) continue;
        SourceFileHashes.Add(Filename, CurrentHash);
        bContainsRelevantChange = true;
    }
    if (!bContainsRelevantChange) return false;

    FScopeLock Lock(&Mutex);
    if (bDirtyRequestPending) return false;
    bDirtyRequestPending = true;
    return true;
}

/**
 * 原子消费一次待标脏请求；模块在游戏线程任务中调用，PIE 不影响消费，因为本函数从不编译 Graph。
 * 可从任意线程调用；没有待处理变化时返回 false。
 *
 * @return 本次消费了至少一批 Lua 文件变化时返回 true。
 */
bool FLuaAnimBlueprintAutoCompileScheduler::ConsumeDirtyRequest()
{
    FScopeLock Lock(&Mutex);
    if (!bDirtyRequestPending) return false;
    bDirtyRequestPending = false;
    return true;
}

/**
 * 清空尚未由游戏线程消费的标脏请求，供模块 Shutdown 使用。
 * 可从任意线程调用，不取消已经开始的外部编译。
 */
void FLuaAnimBlueprintAutoCompileScheduler::Reset()
{
    FScopeLock Lock(&Mutex);
    bDirtyRequestPending = false;
}

/**
 * 仅接受扩展名不区分大小写的 .lua 文件，以及 Added、Modified、Removed 三种明确文件事件。
 * 函数只检查值类型数据，可从任意线程调用；目录变化、未知事件和 RescanRequired 均返回 false。
 *
 * @param FileChange 待判断的单条 DirectoryWatcher 事件。
 * @return 事件应触发 Lua 动画蓝图标脏时返回 true。
 */
bool FLuaAnimBlueprintAutoCompileScheduler::IsRelevantLuaFileChange(
    const FFileChangeData& FileChange)
{
    const bool bSupportedAction = FileChange.Action == FFileChangeData::FCA_Added
        || FileChange.Action == FFileChangeData::FCA_Modified
        || FileChange.Action == FFileChangeData::FCA_Removed;
    return bSupportedAction
        && FPaths::GetExtension(FileChange.Filename, true).Equals(TEXT(".lua"), ESearchCase::IgnoreCase);
}
