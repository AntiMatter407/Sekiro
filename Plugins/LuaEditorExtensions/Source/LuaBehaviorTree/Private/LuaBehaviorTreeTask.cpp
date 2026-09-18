#include "LuaBehaviorTreeTask.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogLuaBehaviorTreeTask, Log, All);

/**
 * 创建一个必须按实例运行且接收 Tick 的通用 Lua Task 模板。
 * 构造发生在 CDO、资产模板和运行时实例上；只有运行时实例会保存 ActiveOwnerComp。
 */
ULuaBehaviorTreeTask::ULuaBehaviorTreeTask()
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
 * @param Result Executing 时只允许 Succeeded/Failed；Aborting 时只允许 Aborted。
 * @return 成功提交一次 latent 完成时返回 true，否则记录稳定警告并返回 false。
 */
bool ULuaBehaviorTreeTask::FinishLuaTask(
    const ELuaBehaviorTreeTaskResult Result)
{
    if (!IsInGameThread()
        || TaskState == ETaskState::Idle
        || !ActiveOwnerComp.IsValid()
        || !bLatentFinishAllowed)
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.InvalidFinish] FinishLuaTask ignored for '%s': no latent task is awaiting completion."),
            *GetPathName());
        return false;
    }
    if (Result == ELuaBehaviorTreeTaskResult::InProgress)
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.InvalidFinishResult] FinishLuaTask ignored for '%s': InProgress cannot finish a task."),
            *GetPathName());
        return false;
    }
    if ((TaskState == ETaskState::Executing
            && Result == ELuaBehaviorTreeTaskResult::Aborted)
        || (TaskState == ETaskState::Aborting
            && Result != ELuaBehaviorTreeTaskResult::Aborted))
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Warning,
            TEXT("[BT.LuaTask.InvalidFinishResult] FinishLuaTask received a result incompatible with the active lifecycle for '%s'."),
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
        Result == ELuaBehaviorTreeTaskResult::Succeeded
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
AAIController* ULuaBehaviorTreeTask::GetTaskAIController() const
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
APawn* ULuaBehaviorTreeTask::GetTaskPawn() const
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
UBlackboardComponent* ULuaBehaviorTreeTask::GetTaskBlackboard() const
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
bool ULuaBehaviorTreeTask::IsTaskActive() const
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
EBTNodeResult::Type ULuaBehaviorTreeTask::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    if (TaskState != ETaskState::Idle)
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.ReentrantExecute] Execute rejected for already active task '%s'."),
            *GetPathName());
        return EBTNodeResult::Failed;
    }

    ActiveOwnerComp = &OwnerComp;
    TaskState = ETaskState::Executing;
    bMissingTickReported = false;
    bLatentFinishAllowed = false;
    TOptional<ELuaBehaviorTreeTaskResult> Result;
    if (!CallLua("Execute", OwnerComp, 0.0f, false, Result))
    {
        ResetActiveState();
        return EBTNodeResult::Failed;
    }

    if (!Result.IsSet())
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.MissingResult] %s.Execute must return ELuaBehaviorTreeTaskResult."),
            *LuaModuleName);
        ResetActiveState();
        return EBTNodeResult::Failed;
    }

    const EBTNodeResult::Type TaskResult = ConvertExecuteResult(Result.GetValue(), "Execute");
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
EBTNodeResult::Type ULuaBehaviorTreeTask::AbortTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    ActiveOwnerComp = &OwnerComp;
    TaskState = ETaskState::Aborting;
    bLatentFinishAllowed = false;
    TOptional<ELuaBehaviorTreeTaskResult> Result;
    if (!CallLua("Abort", OwnerComp, 0.0f, false, Result))
    {
        ResetActiveState();
        return EBTNodeResult::Aborted;
    }
    if (Result.IsSet() && Result.GetValue() == ELuaBehaviorTreeTaskResult::InProgress)
    {
        bLatentFinishAllowed = true;
        return EBTNodeResult::InProgress;
    }

    ResetActiveState();
    return EBTNodeResult::Aborted;
}

/**
 * 将行为树 Tick 转发到 Lua Tick(task, controller, pawn, blackboard, configuration, deltaSeconds)。
 * 只在 Executing 状态调用；Tick 函数可返回 Succeeded/Failed 立即结束，InProgress 或无返回值继续。
 * 函数缺失只报告一次并保持活动，以支持由外部事件调用 FinishLuaTask 的任务。
 *
 * @param OwnerComp 当前行为树组件。
 * @param NodeMemory 节点实例内存，本实现不读取。
 * @param DeltaSeconds 自上次 Tick 的秒数。
 */
void ULuaBehaviorTreeTask::TickTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    const float DeltaSeconds)
{
    if (TaskState != ETaskState::Executing || ActiveOwnerComp.Get() != &OwnerComp) return;
    if (bMissingTickReported) return;

    TOptional<ELuaBehaviorTreeTaskResult> Result;
    if (!CallLua("Tick", OwnerComp, DeltaSeconds, true, Result))
    {
        if (!bMissingTickReported)
        {
            UE_LOG(
                LogLuaBehaviorTreeTask,
                Warning,
                TEXT("[BT.LuaTask.TickUnavailable] Tick unavailable for module '%s'; task remains active until FinishLuaTask or Abort."),
                *LuaModuleName);
            bMissingTickReported = true;
        }
        return;
    }
    if (!Result.IsSet() || Result.GetValue() == ELuaBehaviorTreeTaskResult::InProgress) return;

    const EBTNodeResult::Type TaskResult = ConvertExecuteResult(Result.GetValue(), "Tick");
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
void ULuaBehaviorTreeTask::OnTaskFinished(
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
 * 只能在游戏线程调用；非空返回值必须是 ELuaBehaviorTreeTaskResult 的 Lua integer。
 *
 * @param FunctionName 模块函数名，目前为 Execute、Tick 或 Abort。
 * @param OwnerComp 当前行为树组件。
 * @param DeltaSeconds Tick 间隔秒数，其他生命周期传零。
 * @param bIncludeDeltaSeconds 是否在参数末尾传递 DeltaSeconds。
 * @param OutResult 成功时接收原生枚举；Lua 无返回值时保持 unset。
 * @return require、函数解析和 Lua 调用均成功时返回 true。
 */
bool ULuaBehaviorTreeTask::CallLua(
    const char* FunctionName,
    UBehaviorTreeComponent& OwnerComp,
    const float DeltaSeconds,
    const bool bIncludeDeltaSeconds,
    TOptional<ELuaBehaviorTreeTaskResult>& OutResult)
{
    OutResult.Reset();
    if (!IsInGameThread() || LuaModuleName.IsEmpty())
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.EmptyModule] Lua task '%s' has no module or is running off the game thread."),
            *GetPathName());
        return false;
    }

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (!UnLuaModule)
    {
        UE_LOG(LogLuaBehaviorTreeTask, Error, TEXT("[BT.LuaTask.ModuleUnavailable] UnLua is unavailable."));
        return false;
    }
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(this);
    if (!Environment) Environment = UnLuaModule->GetEnv();
    if (!Environment)
    {
        UE_LOG(LogLuaBehaviorTreeTask, Error, TEXT("[BT.LuaTask.EnvironmentUnavailable] UnLua environment is unavailable."));
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
            LogLuaBehaviorTreeTask,
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
            LogLuaBehaviorTreeTask,
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
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.CallFailed] %s.%s failed."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName));
        return false;
    }
    if (ReturnValues.Num() == 0) return true;
    if (ReturnValues[0].GetType() != LUA_TNUMBER
        || !lua_isinteger(LuaState, ReturnValues[0].GetIndex()))
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.InvalidReturnType] %s.%s must return ELuaBehaviorTreeTaskResult."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName));
        return false;
    }
    const int64 RawResult = static_cast<int64>(
        lua_tointeger(LuaState, ReturnValues[0].GetIndex()));
    const UEnum* ResultEnum = StaticEnum<ELuaBehaviorTreeTaskResult>();
    if (!ResultEnum || !ResultEnum->IsValidEnumValue(RawResult))
    {
        UE_LOG(
            LogLuaBehaviorTreeTask,
            Error,
            TEXT("[BT.LuaTask.InvalidResult] %s.%s returned unsupported enum value %lld."),
            *LuaModuleName,
            UTF8_TO_TCHAR(FunctionName),
            RawResult);
        return false;
    }
    OutResult = static_cast<ELuaBehaviorTreeTaskResult>(RawResult);
    return true;
}

/**
 * 把 Execute/Tick 返回的强类型 Lua 枚举映射为 UE 行为树结果。
 * Aborted 只表示中止完成，不是 Execute/Tick 合法结果；误用时记录错误并返回 Failed。
 *
 * @param Result Lua 直接返回的 ELuaBehaviorTreeTaskResult。
 * @param FunctionName 产生结果的函数名，仅用于日志。
 * @return Succeeded、Failed 或 InProgress；Aborted 误用返回 Failed。
 */
EBTNodeResult::Type ULuaBehaviorTreeTask::ConvertExecuteResult(
    const ELuaBehaviorTreeTaskResult Result,
    const char* FunctionName) const
{
    if (Result == ELuaBehaviorTreeTaskResult::Succeeded) return EBTNodeResult::Succeeded;
    if (Result == ELuaBehaviorTreeTaskResult::InProgress) return EBTNodeResult::InProgress;
    if (Result == ELuaBehaviorTreeTaskResult::Failed) return EBTNodeResult::Failed;

    UE_LOG(
        LogLuaBehaviorTreeTask,
        Error,
        TEXT("[BT.LuaTask.InvalidResult] %s.%s cannot return Aborted."),
        *LuaModuleName,
        UTF8_TO_TCHAR(FunctionName));
    return EBTNodeResult::Failed;
}

/**
 * 清除当前节点实例持有的运行时弱引用和一次性日志状态。
 * 只能在游戏线程的生命周期结束路径调用；不通知 BehaviorTreeComponent。
 */
void ULuaBehaviorTreeTask::ResetActiveState()
{
    ActiveOwnerComp.Reset();
    TaskState = ETaskState::Idle;
    bMissingTickReported = false;
    bLatentFinishAllowed = false;
}
