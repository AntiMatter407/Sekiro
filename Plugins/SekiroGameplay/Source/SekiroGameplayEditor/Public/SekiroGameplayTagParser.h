#pragma once

#include "CoreMinimal.h"
#include "SekiroGameplayTagLibrary.h"

/** 无 UObject 副作用的声明式 Lua 解析入口。 */
class SEKIROGAMEPLAYEDITOR_API FSekiroGameplayTagParser
{
public:
    // ── 数据校验 ──
    static bool Parse(const FString& Source, const FString& SourceName, TArray<FSekiroGameplayTagEntry>& OutTags, FString& OutError);
    static bool ValidateAssetPackagePath(const FString& PackagePath, FString& OutError);
};
