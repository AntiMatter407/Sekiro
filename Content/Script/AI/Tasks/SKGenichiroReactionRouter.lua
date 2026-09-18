-- Lua 类型：BehaviorTree Task 模块。本文件是弦一郎高优先级战斗事件的唯一裁决入口。
-- 每次 Execute 排空 CombatComponent 当前事件批次，先更新 CombatMemory，再按优先级和 EventSerial 选择一次反应。
-- 模块只输出 ReactionType、ReactionPriority 和 SelectedActionId；动画仍由独立战斗动作 Task 执行。

local ActionCatalog = require("AI.Genichiro.GenichiroActionCatalog")
local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")
local LegacySignals = require("AI.Genichiro.GenichiroLegacySignals")
local Runtime = require("AI.Genichiro.GenichiroRuntime")
local TacticalProfile = require("AI.Genichiro.GenichiroTacticalProfile")

---@class GenichiroRoutedReaction
---@field Priority number 行为树反应优先级。
---@field ReactionType string 可观察反应类型。
---@field ActionID string|nil 可执行动作 ID；ForceReplan 可为空。
---@field EventSerial number 来源事件序号。
---@field SemanticSignal string|nil Legacy 中性语义信号。

local SKGenichiroReactionRouter = {}

---兼容 UnLua out 参数和离线 Mock 返回约定，消费一条通用战斗事件。
---@param combat_component USKCombatComponent|table 战斗组件。
---@return boolean consumed 是否取得事件。
---@return FSKAICombatEvent|table|nil event 取得的事件。
local function consume_event(combat_component)
    local ok, first, second = pcall(
        combat_component.ConsumeAICombatEvent,
        combat_component)
    if not ok then
        return false, nil
    end
    if type(first) == "boolean" then
        return first, second
    end
    return first ~= nil, first
end

---将 EventTag 解析为 LegacySignals 中已确认且可驱动行为的语义；未知标签失败关闭。
---@param event_tag userdata|string|number|nil 事件中性标签或 Legacy 数值文本。
---@return string|nil semantic_signal 已确认语义。
local function resolve_semantic_signal(event_tag)
    local text = Runtime.NameToString(event_tag)
    local numeric_id = tonumber(text)
    local definition = nil
    if numeric_id ~= nil then
        definition = LegacySignals.GetByLegacyID(numeric_id)
    else
        definition = LegacySignals.GetByName(text)
    end
    if LegacySignals.CanDriveBehavior(definition) then
        return definition.SemanticEvent
    end
    if string.match(text, "^LegacyKengeki%d+$") ~= nil then
        return text
    end
    return nil
end

---按来袭攻击类型选择对应 3100～3103 中性防御反应。
---@param attack_type userdata|number ESKIncomingAttackType 原生枚举值。
---@return string action_id 对应反应动作 ID。
local function guard_action_for_attack_type(attack_type)
    if attack_type == UE.ESKIncomingAttackType.Heavy then
        return "StrongGuardReaction"
    end
    if attack_type == UE.ESKIncomingAttackType.Thrust then
        return "RushGuardReaction"
    end
    if attack_type == UE.ESKIncomingAttackType.Special then
        return "SpecialGuardReaction"
    end
    return "StandardGuardReaction"
end

---从拼刀语义候选中执行确定性选择，并更新随机种子。
---@param semantic_signal string LegacyKengeki 语义。
---@param context GenichiroDecisionContext 当前快照。
---@param memory GenichiroCombatMemoryState 战斗记忆。
---@return string|nil action_id 选中的拼刀动作。
local function select_clash_action(semantic_signal, context, memory)
    local candidates = TacticalProfile.BuildClashCandidates(
        semantic_signal,
        context,
        memory)
    local seed = CombatMemory.GetValue(memory, "RandomSeed", 710000)
    local selected, next_seed = TacticalProfile.SelectWeighted(candidates, seed)
    CombatMemory.SetValue(memory, "RandomSeed", next_seed)
    return selected ~= nil and selected.ActionID or nil
end

---消费事件事实并更新短期记忆；该步骤对批次中所有事件执行，不受最终优先级选择影响。
---@param event FSKAICombatEvent|table 当前事件。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@return string|nil semantic_signal 解析到的 Legacy 语义。
local function apply_event_memory(event, memory)
    local semantic_signal = resolve_semantic_signal(event.EventTag)
    if event.EventType == UE.ESKAICombatEventType.WeaponContact
        and (semantic_signal == "LegacyKengeki200200"
            or semantic_signal == "LegacyKengeki200215"
            or semantic_signal == "LegacyKengeki200216") then
        if event.ContactResult == UE.ESKWeaponContactResult.Deflected
            or event.ContactResult == UE.ESKWeaponContactResult.Guarded then
            CombatMemory.Increment(memory, "DeflectChainCount", 1, 0, 99)
        end
    elseif event.EventType == UE.ESKAICombatEventType.DamageReceived then
        CombatMemory.SetValue(memory, "LastDamageEventSerial", event.EventSerial or 0)
    elseif event.EventType == UE.ESKAICombatEventType.ForceReplan then
        CombatMemory.SetValue(memory, "ForceStrafeAfterAction", nil)
    end
    if semantic_signal == "DeflectChainReset" then
        CombatMemory.SetValue(memory, "DeflectChainCount", 0)
    elseif semantic_signal ~= nil then
        CombatMemory.SetValue(memory, "LastSemanticSignal", semantic_signal)
    end
    return semantic_signal
end

---把单条事件转译为反应提案；动作存在性和能力在返回前校验。
---@param event FSKAICombatEvent|table 当前事件。
---@param semantic_signal string|nil 事件解析语义。
---@param context GenichiroDecisionContext 当前快照。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@return GenichiroRoutedReaction|nil reaction 当前事件的反应提案。
local function route_event(event, semantic_signal, context, memory)
    local event_serial = tonumber(event.EventSerial) or 0
    if event.EventType == UE.ESKAICombatEventType.ForceReplan then
        return { Priority = 90, ReactionType = "ForceReplan", ActionID = nil, EventSerial = event_serial }
    end
    if event.EventType == UE.ESKAICombatEventType.ReactionRequested then
        local action_id = semantic_signal == "ConditionalReaction3017"
            and "ConditionalProjectileReaction" or "ForcedEndureReaction"
        return { Priority = 80, ReactionType = "ForcedReaction", ActionID = action_id, EventSerial = event_serial }
    end
    if event.EventType == UE.ESKAICombatEventType.AttackThreat then
        return {
            Priority = 65,
            ReactionType = "AttackThreat",
            ActionID = guard_action_for_attack_type(event.AttackType),
            EventSerial = event_serial,
        }
    end

    if semantic_signal ~= nil and string.match(semantic_signal, "^LegacyKengeki%d+$") ~= nil then
        local action_id = select_clash_action(semantic_signal, context, memory)
        if action_id ~= nil then
            return {
                Priority = 60,
                ReactionType = "Kengeki",
                ActionID = action_id,
                EventSerial = event_serial,
                SemanticSignal = semantic_signal,
            }
        end
    end
    if event.EventType == UE.ESKAICombatEventType.WeaponContact
        and event.ContactResult == UE.ESKWeaponContactResult.Deflected then
        local action_id = select_clash_action("LegacyKengeki200200", context, memory)
        if action_id ~= nil then
            return { Priority = 60, ReactionType = "Kengeki", ActionID = action_id, EventSerial = event_serial }
        end
    end
    if event.EventType == UE.ESKAICombatEventType.ProjectileImpact then
        local action_id = context.Space.Back == true
            and "DefensiveLongBackstep" or "StandardGuardReaction"
        return { Priority = 55, ReactionType = "ProjectileImpact", ActionID = action_id, EventSerial = event_serial }
    end
    if event.EventType == UE.ESKAICombatEventType.DamageReceived then
        return { Priority = 50, ReactionType = "DamageReceived", ActionID = "StandardGuardReaction", EventSerial = event_serial }
    end
    if event.EventType == UE.ESKAICombatEventType.TargetAction
        and (semantic_signal == "TargetUseItem" or Runtime.NameToString(event.EventTag) == "TargetUseItem") then
        return { Priority = 40, ReactionType = "TargetUseItem", ActionID = "TargetUseItemPunish", EventSerial = event_serial }
    end
    return nil
end

---比较反应优先级；同优先级选择 EventSerial 更新的一条，保证批次内结果确定。
---@param current GenichiroRoutedReaction|nil 当前最佳反应。
---@param candidate GenichiroRoutedReaction|nil 新提案。
---@return GenichiroRoutedReaction|nil selected 更新后的最佳反应。
local function choose_higher_priority(current, candidate)
    if candidate == nil then
        return current
    end
    if current == nil
        or candidate.Priority > current.Priority
        or (candidate.Priority == current.Priority
            and candidate.EventSerial > current.EventSerial) then
        return candidate
    end
    return current
end

---排空事件批次并输出一次最高优先级反应；Clear 配置用于反应动作完成后的显式收尾。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController；本任务不直接调用。
---@param pawn APawn|table|nil 当前 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil Clear 表示清理反应输出，其余值执行路由。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；Succeeded 或 Failed。
function SKGenichiroReactionRouter.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    local _unused = task or controller
    if pawn == nil or blackboard == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    if Runtime.NameToString(configuration) == "Clear" then
        blackboard:SetValueAsName(Runtime.Keys.ReactionType, "")
        blackboard:SetValueAsInt(Runtime.Keys.ReactionPriority, 0)
        blackboard:SetValueAsName(Runtime.Keys.SelectedActionId, "")
        return UE.ELuaBehaviorTreeTaskResult.Succeeded
    end

    local combat_component = Runtime.GetCombatComponent(pawn)
    if combat_component == nil or combat_component.ConsumeAICombatEvent == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local memory = CombatMemory.GetOrCreate(pawn)
    local context = Runtime.CaptureDecisionContext(pawn, blackboard, 0.0)
    local best_reaction = nil
    while true do
        local consumed, event = consume_event(combat_component)
        if not consumed or event == nil then
            break
        end
        local semantic_signal = apply_event_memory(event, memory)
        best_reaction = choose_higher_priority(
            best_reaction,
            route_event(event, semantic_signal, context, memory))
    end

    if best_reaction == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    if best_reaction.ActionID ~= nil
        and ActionCatalog.GetAction(best_reaction.ActionID) == nil then
        blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "ReactionActionMissing")
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    blackboard:SetValueAsName(Runtime.Keys.ReactionType, best_reaction.ReactionType)
    blackboard:SetValueAsInt(Runtime.Keys.ReactionPriority, best_reaction.Priority)
    blackboard:SetValueAsName(
        Runtime.Keys.SelectedActionId,
        best_reaction.ActionID or "")
    blackboard:SetValueAsName(
        Runtime.Keys.TacticalIntent,
        best_reaction.ActionID ~= nil and "CombatAction" or "Hold")
    CombatMemory.SetValue(memory, "LastReactionType", best_reaction.ReactionType)
    CombatMemory.SetValue(memory, "LastReactionEventSerial", best_reaction.EventSerial)
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

return SKGenichiroReactionRouter
