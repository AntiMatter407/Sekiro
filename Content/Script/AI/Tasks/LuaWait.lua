-- Lua 类型：纯 Lua 类/数据/工具模块。本文件由通用 Behavior Tree Task 宿主按节点模块名调用。
-- 示例演示 Execute/Tick/Abort 生命周期；状态以 Task UObject 为弱键隔离，不会在多个 AI 间共享。

---@class LuaWaitTaskState
---@field ElapsedSeconds number 当前实例已经等待的秒数。
---@field DurationSeconds number 当前实例需要等待的总秒数。

local LuaWait = {}

---@type table<ULuaBehaviorTreeTask, LuaWaitTaskState>
local TaskStates = setmetatable({}, {
    __mode = "k",
})

---开始一次等待；Configuration 可直接填写秒数字符串，缺失或无效时采用一秒。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例，用作隔离状态的稳定弱键。
---@param _controller AAIController|nil 当前行为树所属控制器；本示例无需访问。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn；本示例无需访问。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板；本示例无需访问。
---@param configuration string C++ 节点原样传入的 Configuration。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；返回 InProgress，使行为树继续调用 Tick。
function LuaWait.Execute(
    task,
    _controller,
    _pawn,
    _blackboard,
    configuration)
    local duration_seconds = tonumber(configuration) or 1.0
    TaskStates[task] = {
        ElapsedSeconds = 0.0,
        DurationSeconds = math.max(duration_seconds, 0.0),
    }
    return UE.ELuaBehaviorTreeTaskResult.InProgress
end

---累计当前 Task 实例的等待时间，到达配置时长后完成节点。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param _controller AAIController|nil 当前行为树所属控制器；本示例无需访问。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn；本示例无需访问。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板；本示例无需访问。
---@param _configuration string Execute 已解析的配置；保留参数以匹配统一契约。
---@param delta_seconds number 当前行为树 Tick 的帧间隔秒数。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；未到时返回 InProgress，到达时返回 Succeeded。
function LuaWait.Tick(
    task,
    _controller,
    _pawn,
    _blackboard,
    _configuration,
    delta_seconds)
    local state = TaskStates[task]
    if state == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    state.ElapsedSeconds =
        state.ElapsedSeconds + math.max(delta_seconds or 0.0, 0.0)
    if state.ElapsedSeconds < state.DurationSeconds then
        return UE.ELuaBehaviorTreeTaskResult.InProgress
    end

    TaskStates[task] = nil
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

---行为树中止节点时移除实例状态，避免下次执行继承旧进度。
---@param task ULuaBehaviorTreeTask 当前被中止的 Task 实例。
---@param _controller AAIController|nil 当前行为树所属控制器；本示例无需访问。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn；本示例无需访问。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板；本示例无需访问。
---@param _configuration string 当前节点配置；本示例无需重新解析。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；返回 Succeeded，通用宿主会同步完成 Abort。
function LuaWait.Abort(
    task,
    _controller,
    _pawn,
    _blackboard,
    _configuration)
    TaskStates[task] = nil
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

return LuaWait
