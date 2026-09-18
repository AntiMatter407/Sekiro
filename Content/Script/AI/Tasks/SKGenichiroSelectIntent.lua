-- Lua 类型：BehaviorTree Task 模块。本文件由 ULuaBehaviorTreeTask 在普通战术决策边界调用。
-- 基于 Blackboard 快照、CombatMemory 与 TacticalProfile 建立候选池，输出稳定语义意图和动作 ID。
-- 本任务同步完成；空候选池显式进入 Hold，不伪造默认攻击。

local ActionCatalog = require("AI.Genichiro.GenichiroActionCatalog")
local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")
local Runtime = require("AI.Genichiro.GenichiroRuntime")
local TacticalProfile = require("AI.Genichiro.GenichiroTacticalProfile")

local SKGenichiroSelectIntent = {}

local DefaultRandomSeed = 710000

---建立普通战术候选并写入本次可观察决策结果。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController；本任务不直接调用。
---@param pawn APawn|table|nil 当前弦一郎 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil 可选固定随机种子，未配置时从 CombatMemory 读取。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；Succeeded 或 Failed。
function SKGenichiroSelectIntent.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    local _unused = task or controller
    if pawn == nil or blackboard == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local keys = Runtime.Keys
    local target_actor = blackboard:GetValueAsObject(keys.TargetActor)
    if not Runtime.IsObjectValid(target_actor) then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local memory = CombatMemory.GetOrCreate(pawn)
    local decision_serial = CombatMemory.NextDecisionSerial(memory)
    local context = Runtime.CaptureDecisionContext(pawn, blackboard, 0.0)
    local candidates = TacticalProfile.BuildTacticalCandidates(context, memory)
    local configured_seed = tonumber(configuration)
    local current_seed = configured_seed
        or CombatMemory.GetValue(memory, "RandomSeed", DefaultRandomSeed)
    local selected, next_seed = TacticalProfile.SelectWeighted(candidates, current_seed)
    CombatMemory.SetValue(memory, "RandomSeed", next_seed)

    local summary = Runtime.BuildCandidateSummary(candidates, selected)
    CombatMemory.SetValue(memory, "LastCandidateSummary", summary)
    blackboard:SetValueAsInt(keys.DebugDecisionSerial, decision_serial)
    blackboard:SetValueAsString(keys.DebugCandidateSummary, summary)

    if selected == nil then
        blackboard:SetValueAsName(keys.TacticalIntent, "Hold")
        blackboard:SetValueAsName(keys.SelectedActionId, "")
        return UE.ELuaBehaviorTreeTaskResult.Succeeded
    end

    local action = ActionCatalog.GetAction(selected.ActionID)
    if action == nil then
        blackboard:SetValueAsString(keys.DebugFailureReason, "SelectedActionMissing")
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    blackboard:SetValueAsName(
        keys.TacticalIntent,
        Runtime.ResolveTacticalIntent(action))
    blackboard:SetValueAsName(keys.SelectedActionId, action.ID)
    CombatMemory.SetValue(memory, "SelectedActionID", action.ID)
    CombatMemory.SetValue(memory, "SelectedDecisionSerial", decision_serial)
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

return SKGenichiroSelectIntent
