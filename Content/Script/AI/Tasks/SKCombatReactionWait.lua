-- Lua 类型：纯 Lua 类/数据/工具模块。本文件由通用 Behavior Tree Task 宿主调用。
-- 在 AI 架势崩坏或拼刀被弹开时占据战斗 Selector 的最高优先级，阻止新的追击和攻击决策。

---@class SKCombatReactionWaitTaskState
---@field CombatComponent USKCombatComponent 当前 AI 的战斗组件。
---@field ElapsedSeconds number 当前反应等待累计时间，单位秒。

local SKCombatReactionWait = {}

---@type table<ULuaBehaviorTreeTask, SKCombatReactionWaitTaskState>
local TaskStates = setmetatable({}, {
    __mode = "k",
})

local MaximumReactionWaitSeconds = 6.0

---读取 AI 角色公开的战斗组件；非 SKCharacter Pawn 会稳定返回 nil。
---@param pawn APawn|nil 当前行为树控制的 Pawn。
---@return USKCombatComponent|nil combat_component 可查询反应状态的战斗组件。
local function resolve_combat_component(pawn)
    if pawn == nil or pawn.GetCombatComponent == nil then
        return nil
    end
    return pawn:GetCombatComponent()
end

---判断当前战斗组件是否正处于必须压制导航和攻击决策的反应阶段。
---@param combat_component USKCombatComponent 当前 AI 的战斗组件。
---@return boolean locked 是否处于架势崩坏或被弹开状态。
local function is_reaction_locked(combat_component)
    local state = combat_component:GetCombatActionState()
    return combat_component:IsPostureBroken() == true
        or state == UE.ESKCombatActionState.PostureBroken
        or state == UE.ESKCombatActionState.DeflectReaction
end

---仅在存在战斗反应时进入等待；普通状态返回 Failed 让 Selector 继续尝试攻击和追击。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|nil 当前行为树所属控制器。
---@param pawn APawn|nil 当前控制器拥有的 Pawn。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param _configuration string 当前节点不使用配置，保留统一 Task 签名。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；反应中返回 InProgress，否则返回 Failed。
function SKCombatReactionWait.Execute(
    task,
    controller,
    pawn,
    _blackboard,
    _configuration)
    local combat_component = resolve_combat_component(pawn)
    if controller == nil
        or combat_component == nil
        or is_reaction_locked(combat_component) ~= true then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    controller:StopMovement()
    TaskStates[task] = {
        CombatComponent = combat_component,
        ElapsedSeconds = 0.0,
    }
    return UE.ELuaBehaviorTreeTaskResult.InProgress
end

---持续等待反应状态和全身动画结束，超时则失败以防异常动画永久卡住行为树。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|nil 当前行为树所属控制器。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param _configuration string 当前节点不使用配置，保留统一 Task 签名。
---@param delta_seconds number 当前行为树 Tick 间隔，单位秒。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；InProgress、Succeeded 或 Failed。
function SKCombatReactionWait.Tick(
    task,
    controller,
    _pawn,
    _blackboard,
    _configuration,
    delta_seconds)
    local state = TaskStates[task]
    if state == nil or controller == nil then
        TaskStates[task] = nil
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    controller:StopMovement()
    state.ElapsedSeconds =
        state.ElapsedSeconds + math.max(delta_seconds or 0.0, 0.0)
    if state.ElapsedSeconds >= MaximumReactionWaitSeconds then
        TaskStates[task] = nil
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    if is_reaction_locked(state.CombatComponent) ~= true
        and state.CombatComponent:IsCombatAnimationPlaying() ~= true then
        TaskStates[task] = nil
        return UE.ELuaBehaviorTreeTaskResult.Succeeded
    end
    return UE.ELuaBehaviorTreeTaskResult.InProgress
end

---中止反应等待时释放当前 Task 的弱键状态，导航是否恢复由行为树下一轮重新决定。
---@param task ULuaBehaviorTreeTask 当前被中止的 Task 实例。
---@param _controller AAIController|nil 当前行为树所属控制器。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param _configuration string 当前节点配置。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；固定返回 Succeeded，同步完成 Abort。
function SKCombatReactionWait.Abort(
    task,
    _controller,
    _pawn,
    _blackboard,
    _configuration)
    TaskStates[task] = nil
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

return SKCombatReactionWait
