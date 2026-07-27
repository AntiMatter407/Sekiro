-- Lua 类型：纯 Lua 类/数据/工具模块。本文件由通用 Behavior Tree Task 宿主调用。
-- 在目标进入近战距离后提交一次 AI 轻攻击，并等待攻击、被弹开或架势崩坏动画完整结束。
-- 精确动画、攻击框和架势规则仍由 SKCombatComponent Lua 管理，本任务只负责行为树生命周期。

---@class SKCombatAttackTaskConfig
---@field AttackRange number 允许启动攻击的水平距离，单位厘米。
---@field FacingAngleDegrees number 允许启动攻击的最大水平朝向误差，单位度。
---@field FacingWaitSeconds number 已进入范围但尚未面向目标时允许等待的最长时间，单位秒。

---@class SKCombatAttackTaskState
---@field CombatComponent USKCombatComponent 当前 AI 的战斗组件。
---@field TargetActor AActor 本次攻击锁存的目标 Actor。
---@field Config SKCombatAttackTaskConfig 当前节点解析后的调参。
---@field ElapsedSeconds number 当前任务累计运行时间，单位秒。
---@field bAttackStarted boolean 是否已经成功提交攻击动作。
---@field ActionSerial number 攻击开始后记录的战斗动作序列号。

local SKCombatAttack = {}

---@type table<USekiroLuaBehaviorTreeTask, SKCombatAttackTaskState>
local TaskStates = setmetatable({}, {
    __mode = "k",
})

local MaximumTaskDurationSeconds = 6.0
local TargetKeyName = "TargetActor"
local AttackRequestName = "AutoLight"

---解析行为树节点的三个逗号分隔参数；非法字段分别回退到安全默认值。
---@param configuration string|nil 节点配置，格式为“攻击距离,朝向角度,朝向等待秒数”。
---@return SKCombatAttackTaskConfig config 已限制到有效范围的攻击任务配置。
local function parse_configuration(configuration)
    local text = configuration or ""
    local range_text, angle_text, wait_text =
        string.match(text, "^%s*([^,]+)%s*,%s*([^,]+)%s*,%s*([^,]+)%s*$")
    return {
        AttackRange = math.max(tonumber(range_text) or 230.0, 0.0),
        FacingAngleDegrees = math.max(
            math.min(tonumber(angle_text) or 40.0, 180.0),
            0.0),
        FacingWaitSeconds = math.max(tonumber(wait_text) or 0.25, 0.0),
    }
end

---读取 AI 角色公开的战斗组件；非 SKCharacter Pawn 会稳定返回 nil。
---@param pawn APawn|nil 当前行为树控制的 Pawn。
---@return USKCombatComponent|nil combat_component 可用于攻击请求的战斗组件。
local function resolve_combat_component(pawn)
    if pawn == nil or pawn.GetCombatComponent == nil then
        return nil
    end
    return pawn:GetCombatComponent()
end

---判断当前 Pawn 是否已经在允许的水平角度内面向目标。
---@param pawn APawn 当前 AI Pawn。
---@param target_actor AActor 当前目标。
---@param facing_angle_degrees number 最大水平朝向误差，单位度。
---@return boolean facing 是否满足攻击起手朝向。
local function is_facing_target(pawn, target_actor, facing_angle_degrees)
    local minimum_dot = math.cos(math.rad(facing_angle_degrees))
    return pawn:GetHorizontalDotProductTo(target_actor) >= minimum_dot
end

---在确认距离和朝向后提交一次抽象轻攻击，并锁存动作序列号供 Tick 等待。
---@param state SKCombatAttackTaskState 当前任务实例状态。
---@param controller AAIController 当前 AIController。
---@return boolean started 是否成功启动战斗组件攻击。
local function try_start_attack(state, controller)
    local combat_component = state.CombatComponent
    if combat_component:IsPostureBroken() == true
        or combat_component:GetCombatActionState() ~= UE.ESKCombatActionState.Neutral
        or combat_component:IsCombatAnimationPlaying() == true then
        return false
    end

    controller:StopMovement()
    if combat_component:RequestAIAttack(AttackRequestName) ~= true then
        return false
    end

    state.bAttackStarted = true
    state.ActionSerial = combat_component:GetActionSerial()
    return true
end

---验证目标距离并创建本次攻击任务状态；朝向已经满足时立即启动攻击。
---@param task USekiroLuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|nil 当前行为树所属控制器。
---@param pawn APawn|nil 当前控制器拥有的 Pawn。
---@param blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param configuration string 节点配置字符串。
---@return string result Failed 或 InProgress。
function SKCombatAttack.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    if controller == nil or pawn == nil or blackboard == nil then
        return "Failed"
    end

    local target_actor = blackboard:GetValueAsObject(TargetKeyName)
    local combat_component = resolve_combat_component(pawn)
    local config = parse_configuration(configuration)
    if target_actor == nil
        or combat_component == nil
        or pawn:GetHorizontalDistanceTo(target_actor) > config.AttackRange then
        return "Failed"
    end

    local state = {
        CombatComponent = combat_component,
        TargetActor = target_actor,
        Config = config,
        ElapsedSeconds = 0.0,
        bAttackStarted = false,
        ActionSerial = 0,
    }
    TaskStates[task] = state
    controller:StopMovement()

    if is_facing_target(
        pawn,
        target_actor,
        config.FacingAngleDegrees) == true
        and try_start_attack(state, controller) ~= true then
        TaskStates[task] = nil
        return "Failed"
    end
    return "InProgress"
end

---等待 AI 转正后启动攻击，并在攻击及其反应动画完全结束后完成节点。
---@param task USekiroLuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|nil 当前行为树所属控制器。
---@param pawn APawn|nil 当前控制器拥有的 Pawn。
---@param blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param _configuration string Execute 已经解析的节点配置。
---@param delta_seconds number 当前行为树 Tick 间隔，单位秒。
---@return string result InProgress、Succeeded 或 Failed。
function SKCombatAttack.Tick(
    task,
    controller,
    pawn,
    blackboard,
    _configuration,
    delta_seconds)
    local state = TaskStates[task]
    if state == nil or controller == nil or pawn == nil or blackboard == nil then
        TaskStates[task] = nil
        return "Failed"
    end

    local current_target = blackboard:GetValueAsObject(TargetKeyName)
    if current_target == nil or current_target ~= state.TargetActor then
        TaskStates[task] = nil
        return "Failed"
    end

    state.ElapsedSeconds =
        state.ElapsedSeconds + math.max(delta_seconds or 0.0, 0.0)
    if state.ElapsedSeconds >= MaximumTaskDurationSeconds then
        TaskStates[task] = nil
        return "Failed"
    end

    if state.bAttackStarted ~= true then
        if pawn:GetHorizontalDistanceTo(current_target)
            > state.Config.AttackRange + 30.0 then
            TaskStates[task] = nil
            return "Failed"
        end

        controller:StopMovement()
        if is_facing_target(
            pawn,
            current_target,
            state.Config.FacingAngleDegrees) == true then
            if try_start_attack(state, controller) ~= true then
                TaskStates[task] = nil
                return "Failed"
            end
        elseif state.ElapsedSeconds >= state.Config.FacingWaitSeconds then
            TaskStates[task] = nil
            return "Failed"
        end
        return "InProgress"
    end

    local combat_component = state.CombatComponent
    local combat_state = combat_component:GetCombatActionState()
    if combat_state == UE.ESKCombatActionState.Neutral
        and combat_component:IsCombatAnimationPlaying() ~= true then
        TaskStates[task] = nil
        return "Succeeded"
    end
    return "InProgress"
end

---中止行为树攻击节点时只释放任务状态，不强行截断已经进入不可取消区间的战斗动画。
---@param task USekiroLuaBehaviorTreeTask 当前被中止的 Task 实例。
---@param _controller AAIController|nil 当前行为树所属控制器。
---@param _pawn APawn|nil 当前控制器拥有的 Pawn。
---@param _blackboard UBlackboardComponent|nil 当前行为树黑板。
---@param _configuration string 当前节点配置。
---@return string result 固定返回 Succeeded，同步完成 Abort。
function SKCombatAttack.Abort(
    task,
    _controller,
    _pawn,
    _blackboard,
    _configuration)
    TaskStates[task] = nil
    return "Succeeded"
end

return SKCombatAttack
