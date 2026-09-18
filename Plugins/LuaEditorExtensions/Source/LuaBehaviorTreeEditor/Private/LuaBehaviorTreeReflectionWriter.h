#pragma once

#include "CoreMinimal.h"
#include "LuaBehaviorTreeIR.h"

class FLuaBehaviorTreeReflectionWriter
{
public:
    // ── 属性写入 ──────────────────────────────────────────────────

    static bool ApplyProperties(
        UObject* Target,
        const TArray<FLuaBehaviorTreeIRProperty>& Properties,
        const TArray<FLuaBehaviorTreeIRValue>& Values,
        const FLuaBehaviorTreeSourceLocation& SourceLocation,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

private:
    // ── 值分派 ────────────────────────────────────────────────────

    static bool WriteValue(
        FProperty* Property,
        void* ValueAddress,
        const FLuaBehaviorTreeIRValue& Value,
        const TArray<FLuaBehaviorTreeIRValue>& Values,
        const FString& Path,
        const FLuaBehaviorTreeSourceLocation& SourceLocation,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    static void AddError(
        const FName Code,
        const FString& Message,
        const FString& Path,
        const FLuaBehaviorTreeSourceLocation& SourceLocation,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);
};
