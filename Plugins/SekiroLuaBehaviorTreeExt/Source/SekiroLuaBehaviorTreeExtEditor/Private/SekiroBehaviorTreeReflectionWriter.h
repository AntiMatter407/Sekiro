#pragma once

#include "CoreMinimal.h"
#include "SekiroBehaviorTreeIR.h"

class FSekiroBehaviorTreeReflectionWriter
{
public:
    // ── 属性写入 ──────────────────────────────────────────────────

    static bool ApplyProperties(
        UObject* Target,
        const TArray<FSekiroBehaviorTreeIRProperty>& Properties,
        const TArray<FSekiroBehaviorTreeIRValue>& Values,
        const FSekiroBehaviorTreeSourceLocation& SourceLocation,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

private:
    // ── 值分派 ────────────────────────────────────────────────────

    static bool WriteValue(
        FProperty* Property,
        void* ValueAddress,
        const FSekiroBehaviorTreeIRValue& Value,
        const TArray<FSekiroBehaviorTreeIRValue>& Values,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& SourceLocation,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    static void AddError(
        const FName Code,
        const FString& Message,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& SourceLocation,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);
};
