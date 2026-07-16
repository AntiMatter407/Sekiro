-- 通用组合输入解析器。
-- 业务组件负责记录原始按钮/轴输入并注册规则；解析器只处理时间窗、优先级、输入消费和确定性结算。
-- 它不直接调用 UE 接口，也不理解 Step、攻击等玩法语义。
local InputChordResolver = {}
InputChordResolver.__index = InputChordResolver

---按顺序复制数组部分，避免组合输入规则共享可变列表。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@return table copy 保持元素顺序的新数组。
local function copy_array(source)
    local result = {}
    for index, value in ipairs(source or {}) do
        result[index] = value
    end
    return result
end

---归一化组合输入轴并同时保留限制到 0..1 的输入强度。
---@param x number 二维向量的横向分量。
---@param y number 二维向量的纵向分量。
---@return number normalized_x 归一化横向分量。
---@return number normalized_y 归一化纵向分量。
---@return number amount 限制到 0..1 的原始轴强度。
local function normalize_axis(x, y)
    local axis_x = tonumber(x) or 0
    local axis_y = tonumber(y) or 0
    local amount = math.sqrt(axis_x * axis_x + axis_y * axis_y)
    if amount <= 0.0001 then
        return 0, 0, 0
    end

    return axis_x / amount, axis_y / amount, math.min(amount, 1)
end

---创建独立组合输入解析器，所有规则、轴、按键和待决记录均绑定到给定 Owner。
---@param owner userdata|table|nil 当前 Lua 实例代理的 UE 所有者对象。
---@return table value 创建、派生或导出的类/实例表。
function InputChordResolver:New(owner)
    return setmetatable({
        Owner = owner,
        Definitions = {},
        Axes = {},
        Buttons = {},
        Pending = {},
    }, self)
end

---注册组合规则。Handler 在规则确定后只调用一次，业务结果由 Owner 自行执行。
---@param name string 规则唯一名称。
---@param definition table 组合规则定义，包含 Trigger、Axis、Window、Priority、RequiredButtons、ConsumeTrigger 和 Handler。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:RegisterChord(name, definition)
    assert(type(name) == "string" and name ~= "", "chord name is required")
    assert(type(definition) == "table", "chord definition is required")
    assert(type(definition.Trigger) == "string", "chord trigger is required")
    assert(type(definition.Handler) == "function", "chord handler is required")

    self.Definitions[name] = {
        Trigger = definition.Trigger,
        Axis = definition.Axis,
        Window = math.max(tonumber(definition.Window) or 0, 0),
        Priority = tonumber(definition.Priority) or 0,
        MinimumAxisAmount = tonumber(definition.MinimumAxisAmount) or 0.1,
        RequiredButtons = copy_array(definition.RequiredButtons),
        ConsumeTrigger = definition.ConsumeTrigger ~= false,
        ResolveImmediately = definition.ResolveImmediately == true,
        WaitForAxisWhileTriggerHeld = definition.WaitForAxisWhileTriggerHeld == true,
        Handler = definition.Handler,
    }
end

---记录连续轴快照。只有输入开始、结束或方向明显变化时才更新时间戳，持续按住不会被误判成新组合。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param x number 二维向量的横向分量。
---@param y number 二维向量的纵向分量。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:RecordAxis(name, x, y, event_time)
    local axis_x, axis_y, amount = normalize_axis(x, y)
    local previous = self.Axes[name]
    local changed = previous == nil
        or (previous.Amount <= 0.1) ~= (amount <= 0.1)
    if not changed and amount > 0.1 and previous.Amount > 0.1 then
        local direction_dot = previous.X * axis_x + previous.Y * axis_y
        changed = direction_dot < 0.95
    end

    self.Axes[name] = {
        X = axis_x,
        Y = axis_y,
        Amount = amount,
        ChangedTime = changed and event_time or previous.ChangedTime,
    }

    -- 某些组合允许先按住触发键、稍后再给方向。轴出现时按优先级立即结算，不再等待固定窗口。
    local candidates = {}
    for chord_name, pending in pairs(self.Pending) do
        local definition = self.Definitions[chord_name]
        local trigger = definition ~= nil and self.Buttons[definition.Trigger] or nil
        if definition ~= nil
            and definition.Axis == name
            and definition.WaitForAxisWhileTriggerHeld
            and amount >= definition.MinimumAxisAmount
            and trigger ~= nil
            and trigger.Held == true
            and self:AreRequiredButtonsHeld(definition) then
            table.insert(candidates, {
                Name = chord_name,
                Priority = pending.Priority,
                StartedTime = pending.StartedTime,
            })
        end
    end

    ---比较两条候选记录并建立稳定顺序，供外层 table.sort 使用。
    ---@param left table 排序比较中的左侧候选记录。
    ---@param right table 排序比较中的右侧候选记录。
    ---@return boolean less_than 左侧记录是否应排在右侧记录之前。
    table.sort(candidates, function(left, right)
        if left.Priority == right.Priority then
            return left.StartedTime < right.StartedTime
        end
        return left.Priority > right.Priority
    end)
    for _, candidate in ipairs(candidates) do
        self:ResolveChord(candidate.Name, event_time, "AxisInput")
    end
end

---记录按钮按下时间和 Held 状态，供组合规则判断先后顺序。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:RecordButtonStarted(name, event_time)
    self.Buttons[name] = {
        Held = true,
        PressedTime = event_time,
        ReleasedTime = nil,
    }
end

---记录按钮释放时间并清除 Held 状态，保留历史供当前窗口完成结算。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:RecordButtonCompleted(name, event_time)
    local button = self.Buttons[name] or {}
    button.Held = false
    button.ReleasedTime = event_time
    self.Buttons[name] = button
end

---检查规则声明的所有 RequiredButtons 是否仍处于按住状态。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return boolean resolved 组合条件是否满足并已完成结算。
function InputChordResolver:AreRequiredButtonsHeld(definition)
    for _, button_name in ipairs(definition.RequiredButtons) do
        local button = self.Buttons[button_name]
        if button == nil or button.Held ~= true then
            return false
        end
    end
    return true
end

---判断输入轴稳定状态是否满足当前业务条件。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return boolean resolved 组合条件是否满足并已完成结算。
function InputChordResolver:IsAxisStable(definition, event_time)
    if definition.Axis == nil then
        return definition.ResolveImmediately
    end

    local axis = self.Axes[definition.Axis]
    return axis ~= nil
        and axis.Amount >= definition.MinimumAxisAmount
        and event_time - (axis.ChangedTime or event_time) >= definition.Window
end

---执行取消by触发键逻辑，并向调用方返回模块约定的结果。
---@param trigger_name string|nil trigger_name 使用的语义文本或名称。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:CancelByTrigger(trigger_name)
    for chord_name, pending in pairs(self.Pending) do
        if pending.Trigger == trigger_name then
            self.Pending[chord_name] = nil
        end
    end
end

---结算指定组合并冻结当前轴快照；同一触发键的低优先级候选会被一次性消费。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@param reason string|nil 触发状态变化、日志或策略更新的业务原因。
---@return boolean resolved 组合条件是否满足并已完成结算。
function InputChordResolver:ResolveChord(name, event_time, reason)
    local pending = self.Pending[name]
    local definition = self.Definitions[name]
    if pending == nil or definition == nil then
        return false
    end

    self.Pending[name] = nil
    if not self:AreRequiredButtonsHeld(definition) then
        return false
    end

    local axis = definition.Axis ~= nil and self.Axes[definition.Axis] or nil
    local result = {
        Name = name,
        Trigger = definition.Trigger,
        StartedTime = pending.StartedTime,
        ResolvedTime = event_time,
        WaitTime = math.max(event_time - pending.StartedTime, 0),
        Reason = reason or "WindowElapsed",
        AxisX = axis and axis.X or 0,
        AxisY = axis and axis.Y or 0,
        AxisAmount = axis and axis.Amount or 0,
    }

    if definition.ConsumeTrigger then
        self:CancelByTrigger(definition.Trigger)
    end
    definition.Handler(self.Owner, result)
    return true
end

---开始一个候选。稳定方向已经提前按住时立即结算；存在先后顺序歧义时进入短暂待决窗口。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return boolean resolved 组合条件是否满足并已完成结算。
function InputChordResolver:BeginChord(name, event_time)
    local definition = self.Definitions[name]
    if definition == nil then
        return false
    end

    self.Pending[name] = {
        Trigger = definition.Trigger,
        StartedTime = event_time,
        Priority = definition.Priority,
    }
    if self:AreRequiredButtonsHeld(definition)
        and self:IsAxisStable(definition, event_time) then
        return self:ResolveChord(name, event_time, "StableInput")
    end

    return false
end

---按钮在窗口结束前释放时立即结算最高优先级候选，保证极短点击不会被吞掉。
---@param trigger_name string|nil trigger_name 使用的语义文本或名称。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@param reason string|nil 触发状态变化、日志或策略更新的业务原因。
---@return boolean resolved 组合条件是否满足并已完成结算。
function InputChordResolver:ResolveByTrigger(trigger_name, event_time, reason)
    local candidates = {}
    for chord_name, pending in pairs(self.Pending) do
        if pending.Trigger == trigger_name then
            table.insert(candidates, {
                Name = chord_name,
                Priority = pending.Priority,
                StartedTime = pending.StartedTime,
            })
        end
    end

    ---比较两条候选记录并建立稳定顺序，供外层 table.sort 使用。
    ---@param left table 排序比较中的左侧候选记录。
    ---@param right table 排序比较中的右侧候选记录。
    ---@return boolean less_than 左侧记录是否应排在右侧记录之前。
    table.sort(candidates, function(left, right)
        if left.Priority == right.Priority then
            return left.StartedTime < right.StartedTime
        end
        return left.Priority > right.Priority
    end)
    for _, candidate in ipairs(candidates) do
        if self:ResolveChord(candidate.Name, event_time, reason or "TriggerReleased") then
            return true
        end
    end

    return false
end

---每帧结算已到期候选；优先级高的规则先消费共享触发键，结果不依赖 Lua table 遍历顺序。
---@param event_time number|nil event_time 对应的数值参数；单位和有效范围由当前函数语义定义。
---@return nil 该函数只更新组合输入解析器内部状态。
function InputChordResolver:Update(event_time)
    local due = {}
    for chord_name, pending in pairs(self.Pending) do
        local definition = self.Definitions[chord_name]
        local axis = definition ~= nil and self.Axes[definition.Axis] or nil
        local trigger = definition ~= nil and self.Buttons[definition.Trigger] or nil
        local waiting_for_axis = definition ~= nil
            and definition.WaitForAxisWhileTriggerHeld
            and trigger ~= nil
            and trigger.Held == true
            and (axis == nil or axis.Amount < definition.MinimumAxisAmount)
        if definition ~= nil
            and event_time - pending.StartedTime >= definition.Window
            and not waiting_for_axis then
            table.insert(due, {
                Name = chord_name,
                Priority = definition.Priority,
                StartedTime = pending.StartedTime,
            })
        end
    end

    ---比较两条候选记录并建立稳定顺序，供外层 table.sort 使用。
    ---@param left table 排序比较中的左侧候选记录。
    ---@param right table 排序比较中的右侧候选记录。
    ---@return boolean less_than 左侧记录是否应排在右侧记录之前。
    table.sort(due, function(left, right)
        if left.Priority == right.Priority then
            return left.StartedTime < right.StartedTime
        end
        return left.Priority > right.Priority
    end)
    for _, candidate in ipairs(due) do
        self:ResolveChord(candidate.Name, event_time, "WindowElapsed")
    end
end

return InputChordResolver
