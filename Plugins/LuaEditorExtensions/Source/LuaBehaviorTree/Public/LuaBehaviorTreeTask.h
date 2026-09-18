#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"

#include "LuaBehaviorTreeTask.generated.h"

class AAIController;
class APawn;
class UBlackboardComponent;
class UBehaviorTreeComponent;

UENUM(BlueprintType)
enum class ELuaBehaviorTreeTaskResult : uint8
{
    Succeeded,
    Failed,
    InProgress,
    Aborted,
};

UCLASS(BlueprintType, EditInlineNew)
class LUABEHAVIORTREE_API ULuaBehaviorTreeTask : public UBTTaskNode
{
    GENERATED_BODY()

public:
    ULuaBehaviorTreeTask();

    // ── 配置 ──────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Task")
    FString LuaModuleName;                // require 使用的 Lua 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lua Task", meta = (MultiLine = "true"))
    FString Configuration;                // 原样交给 Lua 的可选配置字符串

    // ── Lua 控制接口 ──────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Lua|Behavior Tree")
    bool FinishLuaTask(ELuaBehaviorTreeTaskResult Result);

    UFUNCTION(BlueprintPure, Category = "Lua|Behavior Tree")
    AAIController* GetTaskAIController() const;

    UFUNCTION(BlueprintPure, Category = "Lua|Behavior Tree")
    APawn* GetTaskPawn() const;

    UFUNCTION(BlueprintPure, Category = "Lua|Behavior Tree")
    UBlackboardComponent* GetTaskBlackboard() const;

    UFUNCTION(BlueprintPure, Category = "Lua|Behavior Tree")
    bool IsTaskActive() const;

protected:
    // ── UBTTaskNode ───────────────────────────────────────────────

    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;

    virtual EBTNodeResult::Type AbortTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;

    virtual void TickTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        float DeltaSeconds) override;

    virtual void OnTaskFinished(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        EBTNodeResult::Type TaskResult) override;

private:
    enum class ETaskState : uint8
    {
        Idle,
        Executing,
        Aborting,
    };

    // ── Lua 分派 ──────────────────────────────────────────────────

    bool CallLua(
        const char* FunctionName,
        UBehaviorTreeComponent& OwnerComp,
        float DeltaSeconds,
        bool bIncludeDeltaSeconds,
        TOptional<ELuaBehaviorTreeTaskResult>& OutResult);

    EBTNodeResult::Type ConvertExecuteResult(
        ELuaBehaviorTreeTaskResult Result,
        const char* FunctionName) const;

    void ResetActiveState();

    TWeakObjectPtr<UBehaviorTreeComponent> ActiveOwnerComp; // 当前实例正在执行或中止的组件
    ETaskState TaskState = ETaskState::Idle; // 当前实例生命周期状态
    bool bMissingTickReported = false; // 防止可选 Tick 缺失日志刷屏
    bool bLatentFinishAllowed = false; // Lua 返回 InProgress 后才允许显式完成
};
