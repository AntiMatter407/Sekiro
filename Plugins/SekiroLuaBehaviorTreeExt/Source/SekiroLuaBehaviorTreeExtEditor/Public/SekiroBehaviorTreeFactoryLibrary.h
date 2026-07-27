#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroBehaviorTreeIR.h"

#include "SekiroBehaviorTreeFactoryLibrary.generated.h"

class UBehaviorTree;
class UBlackboardData;

USTRUCT(BlueprintType)
struct SEKIROLUABEHAVIORTREEEXTEDITOR_API FSekiroLuaBehaviorTreeAssetConfiguration
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString LuaModuleName;                // 资产绑定的 Lua require 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString BlackboardPackagePath;        // 无已绑定 Blackboard 时复用的目标长包路径
};

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

    // ── 资产 Lua 配置与原地生成 ───────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool GetLuaAssetConfiguration(
        UBehaviorTree* BehaviorTree,
        FSekiroLuaBehaviorTreeAssetConfiguration& OutConfiguration);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool SetLuaAssetConfiguration(
        UBehaviorTree* BehaviorTree,
        const FSekiroLuaBehaviorTreeAssetConfiguration& Configuration);

    UFUNCTION(BlueprintPure, Category = "Sekiro|Lua Behavior Tree")
    static FString DeriveBlackboardPackagePath(
        const FString& BehaviorTreePackagePath);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool CheckConfiguredBehaviorTree(
        UBehaviorTree* BehaviorTree,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Behavior Tree")
    static bool GenerateConfiguredBehaviorTree(
        UBehaviorTree* BehaviorTree,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutGeneratedBehaviorTree,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics);
};
