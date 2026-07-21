-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
---@class LuaTransitionGateExpression
---@field Type string Gate 节点类型。
---@field Name string 曲线名；非曲线节点为空字符串。
---@field Threshold number 比较阈值。
---@field Children LuaTransitionGateExpression[] 子表达式。

---@class TransitionRule
local Rule = {}

---构造一棵尚未扁平化的 Transition Gate 表达式节点。
---@param node_type string C++ Factory 识别的 Gate 节点类型名。
---@param name string|nil 曲线名等可选语义名称；不需要名称时传 nil。
---@param threshold number|nil 时间或曲线比较阈值；组合节点可传 nil。
---@param children LuaTransitionGateExpression[]|nil 子表达式数组；叶节点可传 nil。
---@return LuaTransitionGateExpression expression_node 可组合或交给 Transition 设置的 Gate 表达式。
local function expression(node_type, name, threshold, children)
    return { Type = node_type, Name = name or "", Threshold = threshold or 0.0, Children = children or {} }
end

---读取 EventGraph 在游戏线程发布的 Lua Transition bool。
---@return LuaTransitionGateExpression gate Lua bool 叶节点。
function Rule.LuaBool()
    return expression("LuaBool")
end

---比较源状态最相关 SequencePlayer 的剩余时间。
---@param seconds number 剩余时间上限，单位秒。
---@return LuaTransitionGateExpression gate 原生时间叶节点。
function Rule.TimeRemainingLessEqual(seconds)
    assert(type(seconds) == "number" and seconds >= 0.0, "TimeRemainingLessEqual requires non-negative seconds")
    return expression("TimeRemainingLessEqual", "", seconds)
end

---比较当前 AnimInstance 曲线值。
---@param curve_name string Skeleton 曲线名。
---@param threshold number 允许过渡的最小值。
---@return LuaTransitionGateExpression gate 原生曲线叶节点。
function Rule.CurveGreaterEqual(curve_name, threshold)
    assert(type(curve_name) == "string" and curve_name ~= "", "CurveGreaterEqual requires curve name")
    return expression("CurveGreaterEqual", curve_name, threshold)
end

---要求所有子 Gate 为 true。
---@param ... LuaTransitionGateExpression 子表达式。
---@return LuaTransitionGateExpression gate All 组合节点。
function Rule.All(...)
    local children = { ... }
    assert(#children >= 1, "Rule.All requires at least one child")
    return expression("All", "", 0.0, children)
end

---要求任意子 Gate 为 true。
---@param ... LuaTransitionGateExpression 子表达式。
---@return LuaTransitionGateExpression gate Any 组合节点。
function Rule.Any(...)
    local children = { ... }
    assert(#children >= 1, "Rule.Any requires at least one child")
    return expression("Any", "", 0.0, children)
end

---反转一个子 Gate。
---@param child LuaTransitionGateExpression 子表达式。
---@return LuaTransitionGateExpression gate Not 组合节点。
function Rule.Not(child)
    assert(child ~= nil, "Rule.Not requires child")
    return expression("Not", "", 0.0, { child })
end

return Rule
