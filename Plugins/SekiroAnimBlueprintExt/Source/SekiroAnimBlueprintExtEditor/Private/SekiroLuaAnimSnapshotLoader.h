#pragma once

#include "CoreMinimal.h"
#include "SekiroLuaAnimSnapshotModel.h"

/** 从 JSONL 文件加载 Lua 动画快照，坏行不会使其余帧失效。 */
class FSekiroLuaAnimSnapshotLoader final
{
public:
    static bool LoadFile(const FString& FilePath, FSekiroLuaAnimSnapshotDocument& OutDocument);
    static void ParseJsonLines(
        const FString& JsonLines,
        const FString& SourceLabel,
        FSekiroLuaAnimSnapshotDocument& OutDocument);
};
