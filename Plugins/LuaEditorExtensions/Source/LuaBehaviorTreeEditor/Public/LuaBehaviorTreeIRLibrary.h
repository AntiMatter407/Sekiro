#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaBehaviorTreeIR.h"

#include "LuaBehaviorTreeIRLibrary.generated.h"

UCLASS()
class LUABEHAVIORTREEEDITOR_API ULuaBehaviorTreeIRLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Lua 导入与校验 ────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool CompileLuaModule(
        const FString& LuaModuleName,
        FLuaBehaviorTreeIR& OutIR,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool CompileLuaModuleFresh(
        const FString& LuaModuleName,
        FLuaBehaviorTreeIR& OutIR,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool Validate(
        const FLuaBehaviorTreeIR& IR,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);
};
