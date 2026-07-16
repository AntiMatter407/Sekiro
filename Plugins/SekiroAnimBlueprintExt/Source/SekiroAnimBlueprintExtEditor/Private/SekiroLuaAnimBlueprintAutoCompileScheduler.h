#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "IDirectoryWatcher.h"

/** 线程安全地过滤并合并 Lua 文件变化为一次事件驱动的标脏请求。 */
class FSekiroLuaAnimBlueprintAutoCompileScheduler
{
public:
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
    bool bDirtyRequestPending = false; // 是否已有标脏任务等待游戏线程消费
};
