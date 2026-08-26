#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroBehaviorTreeIR.h"

#include "SekiroBehaviorTreeExporterLibrary.generated.h"

class UBehaviorTree;

UCLASS()
class SEKIROLUABEHAVIORTREEEXTEDITOR_API USekiroBehaviorTreeExporterLibrary
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── BehaviorTree 反向导出 ─────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool ExtractBehaviorTreeIR(
        UBehaviorTree* BehaviorTree,
        const FString& SourceModule,
        FSekiroBehaviorTreeIR& OutIR,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool ExportBehaviorTreeToLua(
        UBehaviorTree* BehaviorTree,
        const FString& LuaModuleName,
        bool bOverwrite,
        FString& OutFilePath,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool ResolveLuaModuleFilePath(
        const FString& LuaModuleName,
        FString& OutFilePath,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);
};
