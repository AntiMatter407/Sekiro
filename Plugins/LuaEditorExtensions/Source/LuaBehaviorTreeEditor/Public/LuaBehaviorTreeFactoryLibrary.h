#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LuaBehaviorTreeIR.h"

#include "LuaBehaviorTreeFactoryLibrary.generated.h"

class UBehaviorTree;
class UBlackboardData;

UENUM(BlueprintType)
enum class ELuaBehaviorTreeSourceMode : uint8
{
    BehaviorTree,
    Lua,
};

USTRUCT(BlueprintType)
struct LUABEHAVIORTREEEDITOR_API FLuaBehaviorTreeAssetConfiguration
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString LuaModuleName;                // 资产绑定的 Lua require 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    FString BlackboardPackagePath;        // 无已绑定 Blackboard 时复用的目标长包路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Behavior Tree")
    ELuaBehaviorTreeSourceMode SourceMode = ELuaBehaviorTreeSourceMode::BehaviorTree; // 最近选择或同步来源，不驱动自动行为
};

UCLASS()
class LUABEHAVIORTREEEDITOR_API ULuaBehaviorTreeFactoryLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── 检查与生成 ────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool CheckLuaBehaviorTree(
        const FString& LuaModuleName,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool GenerateFromLua(
        const FString& LuaModuleName,
        const FString& BlackboardPackagePath,
        const FString& BehaviorTreePackagePath,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutBehaviorTree,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool GenerateFromIR(
        const FLuaBehaviorTreeIR& IR,
        const FString& BlackboardPackagePath,
        const FString& BehaviorTreePackagePath,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutBehaviorTree,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    // ── 资产 Lua 配置与原地生成 ───────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool GetLuaAssetConfiguration(
        UBehaviorTree* BehaviorTree,
        FLuaBehaviorTreeAssetConfiguration& OutConfiguration);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool SetLuaAssetConfiguration(
        UBehaviorTree* BehaviorTree,
        const FLuaBehaviorTreeAssetConfiguration& Configuration);

    UFUNCTION(BlueprintPure, Category = "Lua|Behavior Tree")
    static FString DeriveBlackboardPackagePath(
        const FString& BehaviorTreePackagePath);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool CheckConfiguredBehaviorTree(
        UBehaviorTree* BehaviorTree,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    static bool GenerateConfiguredBehaviorTree(
        UBehaviorTree* BehaviorTree,
        bool bSaveAssets,
        UBlackboardData*& OutBlackboard,
        UBehaviorTree*& OutGeneratedBehaviorTree,
        TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics);
};
