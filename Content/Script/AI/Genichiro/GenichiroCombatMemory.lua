-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 保存弦一郎实例级冷却、短期战斗状态和调试序号；Blackboard 只同步需要跨节点观察的摘要。
-- Owner 到 Memory 使用弱键，避免 Pawn 销毁后纯 Lua 表继续持有 UObject。

---@class GenichiroCombatMemoryState
---@field CooldownDeadlineByKey table<string, number> UE 稳定冷却键到截止秒数。
---@field Values table<string, number|string|boolean> 连续拼刀、交替标记和阶段摘要。
---@field LastActionID string|nil 最近完成的 UE 语义动作。
---@field LastLegacyKind string|nil 最近动作的原始来源类别。
---@field LastLegacyIndex number|nil 最近动作的原始编号。
---@field DecisionSerial number 决策边界递增序号，仅用于确定性日志追踪。

local GenichiroCombatMemory = {}

local MemoryByOwner = setmetatable({}, { __mode = "k" })

---创建空的实例级战斗记忆；该函数也供不依赖 UObject 的固定种子测试使用。
---@return GenichiroCombatMemoryState memory 新记忆实例。
function GenichiroCombatMemory.New()
    return {
        CooldownDeadlineByKey = {},
        Values = {},
        LastActionID = nil,
        LastLegacyKind = nil,
        LastLegacyIndex = nil,
        DecisionSerial = 0,
    }
end

---获取 Pawn 对应记忆，不存在时创建；nil Owner 返回独立记忆以支持离线测试。
---@param owner userdata|table|nil 弦一郎 Pawn 或离线测试所有者。
---@return GenichiroCombatMemoryState memory 对应记忆实例。
function GenichiroCombatMemory.GetOrCreate(owner)
    if owner == nil then
        return GenichiroCombatMemory.New()
    end

    local memory = MemoryByOwner[owner]
    if memory == nil then
        memory = GenichiroCombatMemory.New()
        MemoryByOwner[owner] = memory
    end
    return memory
end

---清除 Owner 的全部短期状态；阶段重置、目标生命周期结束或 Pawn EndPlay 时调用。
---@param owner userdata|table|nil 需要清理的弦一郎 Pawn；nil 表示无需操作。
---@return nil 无返回值。
function GenichiroCombatMemory.Reset(owner)
    if owner ~= nil then
        MemoryByOwner[owner] = nil
    end
end

---读取语义状态；未登记键返回调用方提供的默认值。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param key string 稳定语义键。
---@param default_value number|string|boolean|nil 键不存在时的返回值。
---@return number|string|boolean|nil value 当前值或默认值。
function GenichiroCombatMemory.GetValue(memory, key, default_value)
    local value = memory.Values[key]
    if value == nil then
        return default_value
    end
    return value
end

---写入语义状态；nil 会删除键，避免使用额外“是否有效”布尔字段。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param key string 稳定语义键。
---@param value number|string|boolean|nil 新值；nil 表示删除。
---@return nil 无返回值。
function GenichiroCombatMemory.SetValue(memory, key, value)
    memory.Values[key] = value
end

---对数值状态执行有下限和上限的增量更新，适合连续拼刀计数。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param key string 稳定语义键。
---@param delta number 增量。
---@param min_value number|nil 最小值；nil 表示不限制。
---@param max_value number|nil 最大值；nil 表示不限制。
---@return number value 更新后的数值。
function GenichiroCombatMemory.Increment(memory, key, delta, min_value, max_value)
    local current = GenichiroCombatMemory.GetValue(memory, key, 0)
    if type(current) ~= "number" then
        current = 0
    end

    local value = current + delta
    if min_value ~= nil then
        value = math.max(value, min_value)
    end
    if max_value ~= nil then
        value = math.min(value, max_value)
    end
    memory.Values[key] = value
    return value
end

---判断稳定冷却键在指定世界时间是否可用。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param key string UE 稳定冷却键。
---@param now_seconds number 当前世界秒数或离线测试时间。
---@return boolean ready 当前是否不在冷却中。
function GenichiroCombatMemory.IsCooldownReady(memory, key, now_seconds)
    local deadline = memory.CooldownDeadlineByKey[key]
    return deadline == nil or now_seconds >= deadline
end

---提交冷却截止时间；较短的重复提交不会缩短已有冷却。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param key string UE 稳定冷却键。
---@param duration_seconds number 从当前时刻开始的持续秒数。
---@param now_seconds number 当前世界秒数或离线测试时间。
---@return number deadline 最终冷却截止秒数。
function GenichiroCombatMemory.SetCooldown(memory, key, duration_seconds, now_seconds)
    local deadline = now_seconds + math.max(duration_seconds, 0)
    local previous = memory.CooldownDeadlineByKey[key]
    if previous ~= nil then
        deadline = math.max(deadline, previous)
    end
    memory.CooldownDeadlineByKey[key] = deadline
    return deadline
end

---递增并返回决策序号，供调试输出关联同一次候选构建和选择结果。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@return number serial 新决策序号。
function GenichiroCombatMemory.NextDecisionSerial(memory)
    memory.DecisionSerial = memory.DecisionSerial + 1
    return memory.DecisionSerial
end

---按目录定义的提交点应用冷却，确保失败或 Abort 不会错误写入 OnComplete 冷却。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param action GenichiroActionDefinition 目录动作定义。
---@param commit_point string 当前生命周期提交点。
---@param now_seconds number 当前世界秒数或离线测试时间。
---@return nil 无返回值。
function GenichiroCombatMemory.ApplyCooldowns(memory, action, commit_point, now_seconds)
    for _index, cooldown in ipairs(action.Cooldowns or {}) do
        if cooldown.CommitPoint == commit_point then
            GenichiroCombatMemory.SetCooldown(
                memory,
                cooldown.Key,
                cooldown.Seconds,
                now_seconds)
        end
    end
end

---按目录定义的提交点应用状态副作用；令牌值由执行器通过 resolved_values 显式解析。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param action GenichiroActionDefinition 目录动作定义。
---@param commit_point string 当前生命周期提交点。
---@param resolved_values table<string, number|string|boolean>|nil 执行器解析的动态值。
---@return nil 无返回值。
function GenichiroCombatMemory.ApplyStateWrites(memory, action, commit_point, resolved_values)
    local values = resolved_values or {}
    for _index, state_write in ipairs(action.StateWrites or {}) do
        if state_write.CommitPoint == commit_point then
            local value = state_write.Value
            if type(value) == "string" and values[value] ~= nil then
                value = values[value]
            end
            GenichiroCombatMemory.SetValue(memory, state_write.Key, value)
        end
    end
end

---记录已完成动作的追溯摘要；动作失败或被抢占时不得调用。
---@param memory GenichiroCombatMemoryState 战斗记忆实例。
---@param action GenichiroActionDefinition 已完成动作定义。
---@return nil 无返回值。
function GenichiroCombatMemory.RecordCompletedAction(memory, action)
    memory.LastActionID = action.ID
    memory.LastLegacyKind = action.SourceKind
    memory.LastLegacyIndex = action.LegacyIndex
end

return GenichiroCombatMemory
