#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroBehaviorTreeIR.h"

#include "SekiroBehaviorTreeFactoryLibrary.generated.h"

class UBehaviorTree;
class UBlackboardData;

UCLASS()
class SEKIROLUABEHAVIORTREEEXTEDITOR_API USekiroBehaviorTreeFactoryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── 检查与生成 ────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool CheckLuaBehaviorTree(
        const FString& LuaModuleName,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool GenerateFromLua(
        const FString& LuaModuleName,
        const FString& BlackboardPackagePath,
        const FString& BehaviorTreePackagePath,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutBehaviorTree,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool GenerateFromIR(
        const FSekiroBehaviorTreeIR& IR,
        const FString& BlackboardPackagePath,
        const FString& BehaviorTreePackagePath,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutBehaviorTree,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);
};
