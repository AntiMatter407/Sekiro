#pragma once

#include "CoreMinimal.h"
#include "LuaAnimSnapshotModel.h"

/** 从 JSONL 文件加载 Lua 动画快照，坏行不会使其余帧失效。 */
class FLuaAnimSnapshotLoader final
{
public:
    static bool LoadFile(const FString& FilePath, FLuaAnimSnapshotDocument& OutDocument);
    static void ParseJsonLines(
        const FString& JsonLines,
        const FString& SourceLabel,
        FLuaAnimSnapshotDocument& OutDocument);
};
