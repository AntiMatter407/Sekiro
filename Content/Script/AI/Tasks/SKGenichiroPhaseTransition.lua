-- Lua 类型：BehaviorTree Task 模块。本文件在阶段切换边界清理仅应跨一次决策生效的短期状态。
-- 阶段事实由外部系统写入 Blackboard；本任务不猜测血量阈值，也不播放独立阶段动画。

local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")
local Runtime = require("AI.Genichiro.GenichiroRuntime")

local SKGenichiroPhaseTransition = {}

---消费阶段切换标记并重置 710000 的短期连续状态。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例；本任务不持有状态。
---@param controller AAIController|table|nil 当前 AIController；本任务不直接调用。
---@param pawn APawn|table|nil 当前 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil 保留配置字符串；当前不使用。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；有待处理阶段切换时 Succeeded，否则 Failed。
function SKGenichiroPhaseTransition.Execute(task, controller, pawn, blackboard, configuration)
    local _unused = task or controller or configuration
    if pawn == nil or blackboard == nil
        or blackboard:GetValueAsBool(Runtime.Keys.bPhaseTransitionPending) ~= true then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local memory = CombatMemory.GetOrCreate(pawn)
    CombatMemory.SetValue(memory, "DeflectChainCount", 0)
    CombatMemory.SetValue(memory, "ClashAlternation", 0)
    CombatMemory.SetValue(memory, "HighClashAlternation", 0)
    CombatMemory.SetValue(memory, "ForceStrafeAfterAction", nil)
    CombatMemory.SetValue(memory, "PhaseOpeningUsed", false)
    blackboard:SetValueAsBool(Runtime.Keys.bPhaseTransitionPending, false)
    blackboard:SetValueAsName(Runtime.Keys.TacticalIntent, "Hold")
    blackboard:SetValueAsName(Runtime.Keys.SelectedActionId, "")
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

return SKGenichiroPhaseTransition
