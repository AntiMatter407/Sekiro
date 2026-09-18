#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaBehaviorTreeIR.h"

#include "LuaBehaviorTreeExporterLibrary.generated.h"

class UBehaviorTree;

UCLASS()
class LUABEHAVIORTREEEDITOR_API ULuaBehaviorTreeExporterLibrary
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── BehaviorTree 反向导出 ─────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool ExtractBehaviorTreeIR(
        UBehaviorTree* BehaviorTree,
        const FString& SourceModule,
        FLuaBehaviorTreeIR& OutIR,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool ExportBehaviorTreeToLua(
        UBehaviorTree* BehaviorTree,
        const FString& LuaModuleName,
        bool bOverwrite,
        FString& OutFilePath,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool ResolveLuaModuleFilePath(
        const FString& LuaModuleName,
        FString& OutFilePath,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);
};
