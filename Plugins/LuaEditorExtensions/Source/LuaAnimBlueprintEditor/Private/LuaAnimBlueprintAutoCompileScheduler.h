#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "IDirectoryWatcher.h"
#include "Misc/SecureHash.h"

/** 线程安全地过滤并合并 Lua 文件变化为一次事件驱动的标脏请求。 */
class FLuaAnimBlueprintAutoCompileScheduler
{
public:
    /** 建立当前 Lua 源文件内容指纹基线。 */
    void InitializeSourceSnapshot(const FString& SourceRoot);

    /** 过滤文件变化，并在首次待处理变化到达时请求一次游戏线程派发。 */
    bool QueueFileChanges(const TArray<FFileChangeData>& FileChanges);

    /** 消费一次待标脏请求。 */
    bool ConsumeDirtyRequest();

    /** 清空关闭模块后不应继续处理的请求。 */
    void Reset();

    /** 判断单条 DirectoryWatcher 变化是否属于受支持 Lua 文件事件。 */
    static bool IsRelevantLuaFileChange(const FFileChangeData& FileChange);

private:
    FCriticalSection Mutex; // 保护 watcher 回调与游戏线程任务共享状态
    TMap<FString, FMD5Hash> SourceFileHashes; // 规范化 Lua 文件路径对应的最近内容指纹
    bool bSourceSnapshotInitialized = false; // 是否已建立启动时内容基线
    bool bDirtyRequestPending = false; // 是否已有标脏任务等待游戏线程消费
};
