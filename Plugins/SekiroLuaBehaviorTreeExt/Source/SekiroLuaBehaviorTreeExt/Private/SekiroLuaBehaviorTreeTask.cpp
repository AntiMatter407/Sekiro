#include "SekiroLuaBehaviorTreeTask.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "LuaEnv.h"
#include "LuaValue.h"
#include "Modules/ModuleManager.h"
#include "UnLuaLegacy.h"
#include "UnLuaModule.h"
#include "lua.hpp"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroLuaBehaviorTreeTask, Log, All);

namespace
{
    /**
     * 将 Lua 返回字符串映射为行为树结果；比较不区分大小写。
     *
     * @param Result Lua 返回的结果名称。
     * @return Succeeded、Failed 或 InProgress；未知名称返回 Failed。
     */
    EBTNodeResult::Type ParseResultName(const FString& Result)
    {
        if (Result.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
            return EBTNodeResult::Succeeded;
        if (Result.Equals(TEXT("InProgress"), ESearchCase::IgnoreCase))
            return EBTNodeResult::InProgress;
        return EBTNodeResult::Failed;
    }
}

/**
 * 创建一个必须按实例运行且接收 Tick 的通用 Lua Task 模板。
 * 构造发生在 CDO、资产模板和运行时实例上；只有运行时实例会保存 ActiveOwnerComp。
 */
USekiroLuaBehaviorTreeTask::USekiroLuaBehaviorTreeTask()
{
    NodeName = TEXT("Lua Behavior Tree Task");
    bCreateNodeInstance = true;
    bNotifyTick = true;
    bNotifyTaskFinished = true;
}

/**
 * 由当前 Lua Task 实例显式结束 InProgress 生命周期。
 * 只能在游戏线程由本节点 Lua 回调调用；重复 Finish、Idle 状态、InProgress 参数或失效 OwnerComp 均被拒绝。
 * Executing 状态调用 FinishLatentTask，Aborting 状态调用 FinishLatentAbort，随后清除实例运行态。
 *
 * @param Result Executing 时允许 Succeeded/Failed；Aborting 时该值仅用于签名统一且不能是 InProgress。
 * @return 成功提交一次 latent 完成时返回 true，否则记录稳定警告并返回 false。
 */
bool USekiroLuaBehaviorTreeTask::FinishLuaTask(
    const ESekiroLuaBehaviorTreeTaskResult Result)
{
    if (!IsInGameThread()
        || TaskState == ETaskState::Idle
        || !ActiveOwnerComp.IsValid()
        || !bLatentFinishAllowed)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.InvalidFinish] FinishLuaTask ignored for '%s': no latent task is awaiting completion."),
            *GetPathName());
        return false;
    }
    if (Result == ESekiroLuaBehaviorTreeTaskResult::InProgress)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.InvalidFinishResult] FinishLuaTask ignored for '%s': InProgress cannot finish a task."),
            *GetPathName());
        return false;
    }

    UBehaviorTreeComponent* OwnerComp = ActiveOwnerComp.Get();
    const ETaskState PreviousState = TaskState;
    ResetActiveState();
    if (PreviousState == ETaskState::Aborting)
    {
        FinishLatentAbort(*OwnerComp);
        return true;
    }

    FinishLatentTask(
        *OwnerComp,
        Result == ESekiroLuaBehaviorTreeTaskResult::Succeeded
            ? EBTNodeResult::Succeeded
            : EBTNodeResult::Failed);
    return true;
}

/**
 * 返回当前活动 BehaviorTreeComponent 的 AIController。
 * 只能在游戏线程读取；任务未活动或组件失效时返回 nullptr，不缓存 Controller。
 *
 * @return 当前通用 Task 的 AIController，或 nullptr。
 */
AAIController* USekiroLuaBehaviorTreeTask::GetTaskAIController() const
{
    const UBehaviorTreeComponent* OwnerComp = ActiveOwnerComp.Get();
    return OwnerComp ? OwnerComp->GetAIOwner() : nullptr;
}

/**
 * 返回当前 AIController 控制的 Pawn。
 * 只能在游戏线程读取；任务未活动、Controller 或 Pawn 不存在时返回 nullptr。
 *
 * @return 当前受控 Pawn，或 nullptr。
 */
APawn* USekiroLuaBehaviorTreeTask::GetTaskPawn() const
{
    const AAIController* Controller = GetTaskAIController();
    return Controller ? Controller->GetPawn() : nullptr;
}

/**
 * 返回当前 BehaviorTreeComponent 使用的 BlackboardComponent。
 * 只能在游戏线程读取；任务未活动或没有 Blackboard 时返回 nullptr。
 *
 * @return 当前 BlackboardComponent，或 nullptr。
 */
UBlackboardComponent* USekiroLuaBehaviorTreeTask::GetTaskBlackboard() const
{
    UBehaviorTreeComponent* OwnerComp = ActiveOwnerComp.Get();
    return OwnerComp ? OwnerComp->GetBlackboardComponent() : nullptr;
}

/**
 * 查询本节点实例是否处于 Executing 或 Aborting 生命周期。
 * 本函数不检查行为树活动栈，只反映由本实例维护且会在结束时清除的状态。
 *
 * @return 具有有效 OwnerComp 且状态非 Idle 时返回 true。
 */
bool USekiroLuaBehaviorTreeTask::IsTaskActive() const
{
    return TaskState != ETaskState::Idle && ActiveOwnerComp.IsValid();
}

/**
 * 开始 Task 并调用 Lua 模块的 Execute(task, controller, pawn, blackboard, configuration)。
 * 只能由行为树组件在游戏线程调用；空模块、require 失败、函数缺失或非法返回值均稳定失败。
 * Lua 返回 InProgress 时保留实例 OwnerComp，供 Tick、Abort 与显式 FinishLuaTask 使用。
 *
 * @param OwnerComp 当前行为树组件，在同步返回或 latent 完成前由行为树拥有。
 * @param NodeMemory 节点实例内存；本实例化节点不在其中保存额外状态。
 * @return Lua 映射后的 Succeeded、Failed 或 InProgress。
 */
EBTNodeResult::Type USekiroLuaBehaviorTreeTask::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    if (TaskState != ETaskState::Idle)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.ReentrantExecute] Execute rejected for already active task '%s'."),
            *GetPathName());
        return EBTNodeResult::Failed;
    }

    ActiveOwnerComp = &OwnerComp;
    TaskState = ETaskState::Executing;
    bMissingTickReported = false;
    bLatentFinishAllowed = false;
    FString Result;
    if (!CallLua("Execute", OwnerComp, 0.0f, false, Result))
    {
        ResetActiveState();
        return EBTNodeResult::Failed;
    }

    const EBTNodeResult::Type TaskResult = ParseExecuteResult(Result, "Execute");
    if (TaskResult != EBTNodeResult::InProgress) ResetActiveState();
    else bLatentFinishAllowed = true;
    return TaskResult;
}

/**
 * 请求中止活动 Task，并调用 Lua 模块的 Abort(task, controller, pawn, blackboard, configuration)。
 * Lua 可返回 InProgress 后异步调用 FinishLuaTask；其他合法返回值统一结束为 Aborted。
 * 函数缺失或调用失败采用安全同步 Aborted，避免行为树永久等待。
 *
 * @param OwnerComp 发起中止的行为树组件。
 * @param NodeMemory 节点实例内存，本实现不读取。
 * @return Lua 返回 InProgress 时返回 InProgress，否则返回 Aborted。
 */
EBTNodeResult::Type USekiroLuaBehaviorTreeTask::AbortTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    ActiveOwnerComp = &OwnerComp;
    TaskState = ETaskState::Aborting;
    bLatentFinishAllowed = false;
    FString Result;
    if (!CallLua("Abort", OwnerComp, 0.0f, false, Result))
    {
        ResetActiveState();
        return EBTNodeResult::Aborted;
    }
    if (Result.Equals(TEXT("InProgress"), ESearchCase::IgnoreCase))
    {
        bLatentFinishAllowed = true;
        return EBTNodeResult::InProgress;
    }

    ResetActiveState();
    return EBTNodeResult::Aborted;
}

/**
 * 将行为树 Tick 转发到 Lua Tick(task, controller, pawn, blackboard, configuration, deltaSeconds)。
 * 只在 Executing 状态调用；Tick 函数可返回 Succeeded/Failed 立即结束，InProgress 或空字符串继续。
 * 函数缺失只报告一次并保持活动，以支持由外部事件调用 FinishLuaTask 的任务。
 *
 * @param OwnerComp 当前行为树组件。
 * @param NodeMemory 节点实例内存，本实现不读取。
 * @param DeltaSeconds 自上次 Tick 的秒数。
 */
void USekiroLuaBehaviorTreeTask::TickTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    const float DeltaSeconds)
{
    if (TaskState != ETaskState::Executing || ActiveOwnerComp.Get() != &OwnerComp) return;
    if (bMissingTickReported) return;

    FString Result;
    if (!CallLua("Tick", OwnerComp, DeltaSeconds, true, Result))
    {
        if (!bMissingTickReported)
        {
            UE_LOG(
                LogSekiroLuaBehaviorTreeTask,
                Warning,
                TEXT("[BT.LuaTask.TickUnavailable] Tick unavailable for module '%s'; task remains active until FinishLuaTask or Abort."),
                *LuaModuleName);
            bMissingTickReported = true;
        }
        return;
    }
    if (Result.IsEmpty() || Result.Equals(TEXT("InProgress"), ESearchCase::IgnoreCase)) return;

    const EBTNodeResult::Type TaskResult = ParseExecuteResult(Result, "Tick");
    if (TaskResult == EBTNodeResult::InProgress) return;
    ResetActiveState();
    FinishLatentTask(OwnerComp, TaskResult);
}

/**
 * 在行为树确认节点结束后兜底清除本实例持有的活动组件和 latent 权限。
 * 只能由 BehaviorTreeComponent 在游戏线程调用；本函数不再调用 Lua，也不二次结束节点。
 *
 * @param OwnerComp 完成该节点的行为树组件，本实现不保留其引用。
 * @param NodeMemory 节点实例内存，本实现不读取。
 * @param TaskResult 行为树最终结果，本实现仅接收生命周期通知。
 */
void USekiroLuaBehaviorTreeTask::OnTaskFinished(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    const EBTNodeResult::Type TaskResult)
{
    ResetActiveState();
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

/**
 * 每次调用都从当前 UnLua Env require 模块并执行指定函数，不缓存裸 Lua 栈索引或跨 Env table。
 * 函数把当前 Task UObject 显式作为首参，再传 Controller、Pawn、Blackboard、配置和可选 DeltaSeconds。
 * 只能在游戏线程调用；返回值必须是 string，调用失败或函数缺失返回 false。
 *
 * @param FunctionName 模块函数名，目前为 Execute、Tick 或 Abort。
 * @param OwnerComp 当前行为树组件。
 * @param DeltaSeconds Tick 间隔秒数，其他生命周期传零。
 * @param bIncludeDeltaSeconds 是否在参数末尾传递 DeltaSeconds。
 * @param OutResult 成功时接收 Lua 返回字符串；无返回值时为空。
 * @return require、函数解析和 Lua 调用均成功时返回 true。
 */
bool USekiroLuaBehaviorTreeTask::CallLua(
    const char* FunctionName,
    UBehaviorTreeComponent& OwnerComp,
    const float DeltaSeconds,
    const bool bIncludeDeltaSeconds,
    FString& OutResult)
{
    OutResult.Reset();
    if (!IsInGameThread() || LuaModuleName.IsEmpty())
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.EmptyModule] Lua task '%s' has no module or is running off the game thread."),
            *GetPathName());
        return false;
    }

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (!UnLuaModule)
    {
        UE_LOG(LogSekiroLuaBehaviorTreeTask, Error, TEXT("[BT.LuaTask.ModuleUnavailable] UnLua is unavailable."));
        return false;
    }
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(this);
    if (!Environment) Environment = UnLuaModule->GetEnv();
    if (!Environment)
    {
        UE_LOG(LogSekiroLuaBehaviorTreeTask, Error, TEXT("[BT.LuaTask.EnvironmentUnavailable] UnLua environment is unavailable."));
        return false;
    }

    lua_State* LuaState = Environment->GetMainState();
    UnLua::FLuaRetValues RequiredValues =
        UnLua::Call(LuaState, "require", TCHAR_TO_UTF8(*LuaModuleName));
    if (!RequiredValues.IsValid()
        || RequiredValues.Num() < 1
        || RequiredValues[0].GetType() != LUA_TTABLE)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.RequireFailed] require('%s') failed or did not return a table."),
            *LuaModuleName);
        return false;
    }

    UnLua::FLuaTable ModuleTable(Environment, RequiredValues[0]);
    const int32 InitialTop = lua_gettop(LuaState);
    const int32 FunctionType = lua_getfield(LuaState, ModuleTable.GetIndex(), FunctionName);
    lua_settop(LuaState, InitialTop);
    if (FunctionType != LUA_TFUNCTION)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.FunctionMissing] Module '%s' has no function '%s'."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName));
        return false;
    }

    AAIController* Controller = OwnerComp.GetAIOwner();
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    UnLua::FLuaRetValues ReturnValues = bIncludeDeltaSeconds
        ? ModuleTable.Call(
            FunctionName,
            this,
            Controller,
            Pawn,
            Blackboard,
            Configuration,
            DeltaSeconds)
        : ModuleTable.Call(
            FunctionName,
            this,
            Controller,
            Pawn,
            Blackboard,
            Configuration);
    if (!ReturnValues.IsValid())
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.CallFailed] %s.%s failed."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName));
        return false;
    }
    if (ReturnValues.Num() == 0) return true;
    if (ReturnValues[0].GetType() != LUA_TSTRING)
    {
        UE_LOG(
            LogSekiroLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.InvalidReturnType] %s.%s must return a result string."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName));
        return false;
    }
    OutResult = ReturnValues[0].Value<FString>();
    return true;
}

/**
 * 校验 Execute/Tick 返回名称并映射为 UE 结果。
 * 未知字符串记录稳定错误并映射 Failed；Abort 使用独立规则，不调用本函数。
 *
 * @param Result Lua 返回字符串。
 * @param FunctionName 产生结果的函数名，仅用于日志。
 * @return 合法字符串对应结果，未知字符串返回 Failed。
 */
EBTNodeResult::Type USekiroLuaBehaviorTreeTask::ParseExecuteResult(
    const FString& Result,
    const char* FunctionName) const
{
    if (Result.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase)
        || Result.Equals(TEXT("Failed"), ESearchCase::IgnoreCase)
        || Result.Equals(TEXT("InProgress"), ESearchCase::IgnoreCase))
    {
        return ParseResultName(Result);
    }

    UE_LOG(
        LogSekiroLuaBehaviorTreeTask,
        Error,
        TEXT("[BT.LuaTask.InvalidResult] %s.%s returned unsupported result '%s'."),
        *LuaModuleName,
        UTF8_TO_TCHAR(FunctionName),
        *Result);
    return EBTNodeResult::Failed;
}

/**
 * 清除当前节点实例持有的运行时弱引用和一次性日志状态。
 * 只能在游戏线程的生命周期结束路径调用；不通知 BehaviorTreeComponent。
 */
void USekiroLuaBehaviorTreeTask::ResetActiveState()
{
    ActiveOwnerComp.Reset();
    TaskState = ETaskState::Idle;
    bMissingTickReported = false;
    bLatentFinishAllowed = false;
}
