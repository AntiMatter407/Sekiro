-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 将 710000 的距离权重、阶段修饰、空间过滤、冷却和拼刀结果校准为 UE 语义动作候选。
-- 本模块只处理纯数据快照；感知、导航、动画播放和事件消费仍由 UE BehaviorTree 与专用 Task 承担。

local ActionCatalog = require("AI.Genichiro.GenichiroActionCatalog")
local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")

---@alias GenichiroDistanceBand "Close"|"Mid"|"Far"|"VeryFar"

---@class GenichiroDecisionSpace
---@field Left boolean 左侧是否存在可导航空间。
---@field Right boolean 右侧是否存在可导航空间。
---@field Back boolean 后方是否存在可导航空间。

---@class GenichiroDecisionCapabilities
---@field ProjectileImpact boolean Type 2 弹射物命中与事件生产者是否可用。

---@class GenichiroDecisionContext
---@field DistanceCm number 与目标的水平距离，单位厘米。
---@field NowSeconds number 当前世界秒数或离线回放时间。
---@field SelfFlags table<string, boolean> 弦一郎持续语义状态。
---@field TargetFlags table<string, boolean> 目标持续语义状态。
---@field Space GenichiroDecisionSpace 左右和后方导航空间快照。
---@field Capabilities GenichiroDecisionCapabilities 已接入能力快照。
---@field ExternalTimer7Ready boolean 原脚本 Timer(7) 外部依赖是否就绪。
---@field SelfPostureRaw number 当前原脚本 SP 近似值，用于后续完整 Profile 校准。
---@field SelfPostureRatio number 当前架势归一化比例。
---@field SelfHealthRatio number 当前生命归一化比例；无通用来源时安全默认为一。
---@field BossPhase number 当前 Boss 阶段索引；基础弦一郎以已跨阶段次数近似原脚本忍杀计数。

---@class GenichiroWeightedCandidate
---@field ActionID string UE 语义动作 ID。
---@field Weight number 过滤后的正权重。
---@field LegacyWeight number 进入通用过滤前的原版权重。
---@field Source string 候选来源或距离分支，用于调试。

local GenichiroTacticalProfile = {}

GenichiroTacticalProfile.DistanceBand = {
    Close = "Close",
    Mid = "Mid",
    Far = "Far",
    VeryFar = "VeryFar",
}

local DistanceBand = GenichiroTacticalProfile.DistanceBand

---按 710000 的 3/5/7 Legacy 单位边界计算 UE 距离档位。
---@param distance_cm number 目标距离，单位厘米。
---@return GenichiroDistanceBand band 距离档位。
function GenichiroTacticalProfile.ResolveDistanceBand(distance_cm)
    if distance_cm <= 300.0 then
        return DistanceBand.Close
    end
    if distance_cm < 500.0 then
        return DistanceBand.Mid
    end
    if distance_cm < 700.0 then
        return DistanceBand.Far
    end
    return DistanceBand.VeryFar
end

---检查动作声明的能力依赖；能力缺失时失败关闭，不能只播放无伤害表现。
---@param action GenichiroActionDefinition 动作目录定义。
---@param capabilities GenichiroDecisionCapabilities 已接入能力快照。
---@return boolean available 动作完整能力是否可用。
local function has_required_capabilities(action, capabilities)
    for _index, capability in ipairs(action.CapabilityDependencies or {}) do
        if capabilities[capability] ~= true then
            return false
        end
    end
    return true
end

---检查动作自身所有目录冷却是否可用。
---@param action GenichiroActionDefinition 动作目录定义。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param now_seconds number 当前世界秒数或离线回放时间。
---@return boolean ready 是否全部冷却就绪。
local function are_action_cooldowns_ready(action, memory, now_seconds)
    for _index, cooldown in ipairs(action.Cooldowns or {}) do
        if not CombatMemory.IsCooldownReady(memory, cooldown.Key, now_seconds) then
            return false
        end
    end
    return true
end

---追加经过目录、能力和冷却校验的正权重候选。
---@param candidates GenichiroWeightedCandidate[] 输出候选数组。
---@param action_id string UE 语义动作 ID。
---@param weight number 原始或修饰后的权重。
---@param source string 候选来源说明。
---@param context GenichiroDecisionContext 决策快照。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@return nil 无返回值。
local function add_candidate(candidates, action_id, weight, source, context, memory)
    if weight <= 0 then
        return
    end

    local action = ActionCatalog.GetAction(action_id)
    if action == nil
        or not has_required_capabilities(action, context.Capabilities)
        or not are_action_cooldowns_ready(action, memory, context.NowSeconds) then
        return
    end

    candidates[#candidates + 1] = {
        ActionID = action_id,
        Weight = weight,
        LegacyWeight = weight,
        Source = source,
    }
end

local TacticalActionByAct = {
    [1] = "ClosePressureCombo", [2] = "SingleSlashApproach",
    [3] = "CloseBackAwarenessAttack", [5] = "LongRangeProjectile",
    [6] = "MidPressureAttack", [9] = "PosturePressureCombo",
    [10] = "FarGapCloser", [11] = "CloseTwoStepCombo",
    [15] = "PhaseOpeningAssault", [16] = "TargetStatePunish",
    [20] = "ReusableReactionStrike", [21] = "TurnToTarget",
    [22] = "SideDodge", [23] = "TimedStrafe",
    [24] = "DefensiveBackstepCounter", [25] = "Retreat",
    [26] = "TacticalHold", [27] = "MaintainTargetRange",
    [28] = "ContextReposition", [30] = "ProjectileVolleyFollowUp",
    [31] = "CloseCounterChain", [34] = "FarProjectileChain",
    [48] = "TargetStateOpeningAssault",
}

local ClashActionByIndex = {
    [1] = "ClashAlternatingResponseA", [2] = "ClashResponseK02",
    [3] = "ClashProjectileReposition", [4] = "ClashAlternatingResponseB",
    [7] = "ClashResponseK07", [9] = "ClashResponseK09",
    [10] = "ClashResponseK10", [13] = "ClashResponseK13",
    [14] = "ClashResponseK14", [15] = "ClashResponseK15",
    [17] = "ClashResponseK17", [18] = "ClashResponseK18",
    [19] = "ClashResponseK19", [20] = "ClashBackstepCounter",
    [21] = "ClashResponseK21", [30] = "ClashResponseK30",
    [31] = "ClashResponseK31", [32] = "ClashResponseK32",
    [33] = "ClashResponseK33", [34] = "ClashResponseK34",
    [35] = "ClashResponseK35", [38] = "ClashResponseK38",
    [39] = "ClashResponseK39", [40] = "ClashResponseK40",
    [43] = "ClashResponseK43", [44] = "ClashResponseK44",
    [45] = "ClashResponseK45", [46] = "ClashResponseK46",
    [47] = "ClashResponseK47",
}

---给 Legacy Act 权重槽赋值；动作索引没有实现时失败关闭。
---@param weights table<number, number> Legacy Act 权重表。
---@param act_index number Act 索引。
---@param weight number 权重。
---@return nil 无返回值。
local function set_act_weight(weights, act_index, weight)
    if TacticalActionByAct[act_index] ~= nil then
        weights[act_index] = weight
    end
end

---构建完整 23 Act 战术候选；零权重保留条目不会进入候选数组。
---@param context GenichiroDecisionContext 决策快照。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@return GenichiroWeightedCandidate[] candidates 已完成空间、阶段、能力和冷却过滤的候选。
function GenichiroTacticalProfile.BuildTacticalCandidates(context, memory)
    local weights = {}
    local source = GenichiroTacticalProfile.ResolveDistanceBand(context.DistanceCm)
    local self_flags = context.SelfFlags
    local target_flags = context.TargetFlags
    local space = context.Space

    if self_flags.LegacyPhaseFlagA == true
        and CombatMemory.GetValue(memory, "PhaseOpeningUsed", false) ~= true then
        set_act_weight(weights, 15, 600.0)
        source = "PhaseOpening"
    elseif context.DistanceCm >= 700.0 then
        set_act_weight(weights, 10, 300.0)
        set_act_weight(weights, 15, 600.0)
    elseif context.DistanceCm >= 500.0 then
        set_act_weight(weights, 10, 300.0)
        set_act_weight(weights, 34, 100.0)
        set_act_weight(weights, 23, 100.0)
        if context.SelfPostureRaw <= 360.0 then
            set_act_weight(weights, 9, 300.0)
        end
    elseif context.DistanceCm > 300.0 then
        set_act_weight(weights, 1, 5.0)
        set_act_weight(weights, 2, 10.0)
        set_act_weight(weights, 6, 30.0)
        set_act_weight(weights, 11, 15.0)
        set_act_weight(weights, 23, 15.0)
        if context.SelfPostureRaw <= 360.0 then
            set_act_weight(weights, 9, 300.0)
        end
    else
        set_act_weight(weights, 3, 15.0)
        set_act_weight(weights, 11, 15.0)
        set_act_weight(weights, 23, 10.0)
        set_act_weight(weights, 31, 30.0)
        if context.ExternalTimer7Ready == true then
            set_act_weight(weights, 24, 30.0)
        end
    end

    if target_flags.TargetPunishWindow == true then
        weights = {}
        set_act_weight(weights, 1, 5.0)
        set_act_weight(weights, 2, 5.0)
        set_act_weight(weights, 3, 5.0)
        set_act_weight(weights, 11, 5.0)
        set_act_weight(weights, 10, 30.0)
        set_act_weight(weights, 48, 30.0)
        source = "TargetPunishWindow"
    end

    if target_flags.TargetSpecialAction == true then
        weights = {}
        if target_flags.TargetInFront == true then
            set_act_weight(weights, 21, 1.0)
            set_act_weight(weights, 28, 100.0)
        else
            set_act_weight(weights, 21, 100.0)
        end
        source = "TargetSpecialAction"
    elseif target_flags.TargetStatePunish == true then
        set_act_weight(weights, 16, 100.0)
    elseif target_flags.TargetState110030 == true then
        weights = {}
        set_act_weight(weights, 28, 100.0)
        source = "TargetState110030"
    elseif target_flags.TargetBehind == true then
        weights = {}
        set_act_weight(weights, 21, 100.0)
        set_act_weight(weights, 22, 1.0)
        source = "TargetBehind"
    end

    if CombatMemory.GetValue(memory, "ForceStrafeAfterAction", false) == true then
        weights = {}
        set_act_weight(weights, 23, 6000.0)
        CombatMemory.SetValue(memory, "ForceStrafeAfterAction", nil)
        source = "ForcedStrafe"
    end

    if self_flags.LegacyPhaseFlagB == true then
        weights[15] = 0.0
        weights[34] = 0.0
        weights[48] = 0.0
    end
    if target_flags.TargetRepositionRestricted == true then
        weights[23] = 0.0
        weights[24] = 0.0
        weights[31] = 10.0
    end
    if space.Left ~= true and space.Right ~= true then
        weights[22] = 0.0
        weights[23] = 0.0
    end
    if space.Back ~= true then
        weights[24] = 0.0
        weights[25] = 0.0
    end
    if not CombatMemory.IsCooldownReady(memory, "CloseActionMutualExclusion", context.NowSeconds) then
        weights[3] = 0.0
        if (weights[6] or 0.0) > 0.0 then
            weights[6] = 1.0
        end
    end
    if not CombatMemory.IsCooldownReady(memory, "RepositionSuppression", context.NowSeconds) then
        weights[24] = 0.0
    end
    if not CombatMemory.IsCooldownReady(memory, "ClashActionSuppression", context.NowSeconds) then
        weights[2] = 0.0
    end
    if not CombatMemory.IsCooldownReady(memory, "LegacyTimer6", context.NowSeconds) then
        weights[9] = 0.0
    end

    local candidates = {}
    for act_index, action_id in pairs(TacticalActionByAct) do
        add_candidate(candidates, action_id, weights[act_index] or 0.0, source, context, memory)
    end
    return candidates
end

---构建六种 Legacy Kengeki 信号的完整正权重响应池。
---@param semantic_signal string LegacyKengeki200200 或 LegacyKengeki200201。
---@param context GenichiroDecisionContext 决策快照。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@return GenichiroWeightedCandidate[] candidates 已过滤拼刀响应候选。
function GenichiroTacticalProfile.BuildClashCandidates(semantic_signal, context, memory)
    local signal_id = tonumber(string.match(semantic_signal or "", "(%d+)$"))
    if signal_id ~= 200200 and signal_id ~= 200201
        and signal_id ~= 200210 and signal_id ~= 200211
        and signal_id ~= 200215 and signal_id ~= 200216 then
        return {}
    end
    local close_threshold = (signal_id == 200215 or signal_id == 200216) and 200.0 or 250.0
    if (signal_id == 200200 or signal_id == 200201
        or signal_id == 200215 or signal_id == 200216)
        and context.DistanceCm >= close_threshold then
        return {}
    end

    local chain_count = CombatMemory.GetValue(memory, "DeflectChainCount", 0)
    if type(chain_count) ~= "number" then
        chain_count = 0
    end
    local weights = {}
    local alternation = CombatMemory.GetValue(memory, "ClashAlternation", 0)
    local high_alternation = CombatMemory.GetValue(memory, "HighClashAlternation", 0)
    if signal_id == 200210 then
        weights = { [2] = 100.0, [17] = 100.0, [38] = 50.0, [31] = 50.0 }
    elseif signal_id == 200211 then
        weights = { [2] = 100.0, [10] = 50.0, [31] = 50.0, [38] = 50.0 }
    elseif (signal_id == 200200 or signal_id == 200201) and chain_count >= 2 then
        weights = { [3] = 60.0, [20] = 60.0, [38] = 50.0, [15] = 30.0, [43] = 50.0, [39] = 30.0 }
        if signal_id == 200200 then
            weights[30] = 5.0
            weights[high_alternation == 0 and 32 or 33] = 20.0
        else
            weights[high_alternation == 0 and 32 or 33] = 50.0
        end
    elseif (signal_id == 200200 or signal_id == 200201) then
        weights[alternation == 0 and 1 or 4] = alternation == 0 and 50.0 or 100.0
    elseif signal_id == 200216 and chain_count >= 3 then
        weights = { [3] = 60.0, [20] = 20.0, [38] = 100.0, [43] = 50.0 }
        weights[high_alternation == 0 and 32 or 33] = 50.0
    elseif signal_id == 200215 and chain_count >= 3 then
        weights = { [20] = 20.0, [38] = 30.0, [31] = 15.0, [43] = 10.0, [39] = 10.0 }
        weights[high_alternation == 0 and 32 or 33] = 50.0
    else
        weights[20] = 50.0
        weights[alternation == 0 and 1 or 14] = 50.0
    end

    if context.SelfFlags.LegacyPhaseFlagB == true then
        for _index, kengeki_index in ipairs({ 3, 9, 15, 32, 33, 39, 46 }) do
            weights[kengeki_index] = 0.0
        end
    elseif context.SelfFlags.LegacyPhaseFlagA == true then
        weights[20] = 0.0
    end
    if context.Space.Left ~= true and context.Space.Right ~= true then
        weights[20] = 0.0
    end
    if CombatMemory.IsCooldownReady(memory, "LegacyTimer6", context.NowSeconds)
        and (context.SelfHealthRatio or 1.0) <= 0.75 then
        weights[40] = 50.0
    end

    local candidates = {}
    for kengeki_index, weight in pairs(weights) do
        local action_id = ClashActionByIndex[kengeki_index]
        if action_id ~= nil then
            add_candidate(candidates, action_id, weight, semantic_signal, context, memory)
        end
    end
    return candidates
end

---推进显式线性同余随机种子，避免依赖全局 math.random 导致测试和回放不可复现。
---@param seed number 当前非负整数种子。
---@return number next_seed 下一非负整数种子。
local function next_seed(seed)
    return (seed * 1103515245 + 12345) % 2147483648
end

---按正权重和显式种子选择候选；空池返回 nil，让调用方进入 Hold 安全兜底。
---@param candidates GenichiroWeightedCandidate[] 已过滤候选。
---@param seed number 当前随机种子。
---@return GenichiroWeightedCandidate|nil selected 选中候选；空池返回 nil。
---@return number next_random_seed 下一次决策应保存的种子。
function GenichiroTacticalProfile.SelectWeighted(candidates, seed)
    local random_seed = next_seed(math.max(math.floor(seed), 0))
    local total_weight = 0.0
    for _index, candidate in ipairs(candidates) do
        total_weight = total_weight + math.max(candidate.Weight, 0.0)
    end
    if total_weight <= 0.0 then
        return nil, random_seed
    end

    local roll = (random_seed / 2147483648) * total_weight
    local accumulated = 0.0
    for _index, candidate in ipairs(candidates) do
        accumulated = accumulated + math.max(candidate.Weight, 0.0)
        if roll < accumulated then
            return candidate, random_seed
        end
    end
    return candidates[#candidates], random_seed
end

return GenichiroTacticalProfile
