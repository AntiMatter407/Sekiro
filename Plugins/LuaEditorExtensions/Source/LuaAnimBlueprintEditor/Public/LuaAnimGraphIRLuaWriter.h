#pragma once

#include "CoreMinimal.h"
#include "LuaAnimGraphIR.h"

/** 将完整动画蓝图 IR 确定性序列化为可由现有 Importer 回读的纯 Lua 模块。 */
class LUAANIMBLUEPRINTEDITOR_API FLuaAnimGraphIRLuaWriter
{
public:
    /** 规范化并验证输入后生成 UTF-8 兼容 Lua 文本，不访问文件系统。 */
    static bool WriteModule(
        const FLuaAnimBlueprintIR& Blueprint,
        FString& OutLuaText,
        TArray<FLuaAnimIRDiagnostic>& OutDiagnostics);
};
