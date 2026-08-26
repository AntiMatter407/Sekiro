#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroBehaviorTreeIR.h"

#include "SekiroBehaviorTreeIRLibrary.generated.h"

UCLASS()
class SEKIROLUABEHAVIORTREEEXTEDITOR_API USekiroBehaviorTreeIRLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Lua 导入与校验 ────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool CompileLuaModule(
        const FString& LuaModuleName,
        FSekiroBehaviorTreeIR& OutIR,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool CompileLuaModuleFresh(
        const FString& LuaModuleName,
        FSekiroBehaviorTreeIR& OutIR,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool Validate(
        const FSekiroBehaviorTreeIR& IR,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);
};
