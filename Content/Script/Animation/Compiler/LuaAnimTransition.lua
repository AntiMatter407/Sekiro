-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- StateMachine 中一条原生 Transition 的编译期对象。
-- 对象公开 BlendDuration、PriorityOrder 和 BlendMode，使用方式对应动画蓝图 Transition 详情设置。
local CompilerClass = require("Animation.Compiler.CompilerClass")

---@class LuaAnimTransitionConfig
---@field Id string Transition 稳定 ID。
---@field Key string 状态机内唯一语义键。
---@field SourceStateId string 起始状态稳定 ID。
---@field TargetStateId string 目标状态稳定 ID。
---@field Settings LuaAnimTransitionSettings 初始过渡设置，必须包含原生 Rule。
---@field DeclarationOrder number 源码声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Lua 源码位置。

---@class LuaAnimTransition: CompilerClass
---@field Id string Transition 稳定 ID。
---@field Key string 状态机内唯一语义键。
---@field SourceStateId string 起始状态稳定 ID。
---@field TargetStateId string 目标状态稳定 ID。
---@field RuleFunctionName string IR 兼容字段；Lua DSL 只生成原生 Rule，因此固定为空字符串。
---@field BlendDuration number 原生 Transition 混合时长，单位秒。
---@field PriorityOrder number 同源 Transition 的优先级整数。
---@field BlendMode string 原生 AlphaBlend 模式注册名。
---@field Gate LuaTransitionGateExpression 完整原生 Rule AST。
---@field DeclarationOrder number 源码声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation Lua 源码位置。
local LuaAnimTransition = CompilerClass:Extend("LuaAnimTransition")

---初始化可直接配置的 Transition 对象，并提供与 UE 默认设置一致的稳定默认值。
---@param config LuaAnimTransitionConfig Transition 身份、端点和初始设置。
---@return nil result 该函数只保存编译期 Transition 数据。
function LuaAnimTransition:Initialize(config)
    local settings = config.Settings or {}
    self.Id = assert(config.Id, "LuaAnimTransition requires Id")
    self.Key = assert(config.Key, "LuaAnimTransition requires Key")
    self.SourceStateId = assert(config.SourceStateId, "LuaAnimTransition requires SourceStateId")
    self.TargetStateId = assert(config.TargetStateId, "LuaAnimTransition requires TargetStateId")
    self.RuleFunctionName = ""
    self.BlendDuration = settings.BlendDuration or 0.2
    self.PriorityOrder = settings.PriorityOrder or config.DeclarationOrder
    self.BlendMode = settings.BlendMode or "Linear"
    self.Gate = assert(settings.Rule, "LuaAnimTransition requires a native Rule")
    self.DeclarationOrder = config.DeclarationOrder or 0
    self.SourceLocation = assert(config.SourceLocation, "LuaAnimTransition requires SourceLocation")
end

---导出与 FSekiroAnimIRTransition 对应的纯 Lua 表。
---@return SekiroAnimIRTransition transition_ir 可由 C++ 导入器解析的 Transition 声明。
function LuaAnimTransition:ToIR()
    local gate_nodes = {}

    ---以后序遍历把嵌套 Gate 表达式转换为 C++ Importer 使用的扁平索引数组。
    ---nil 表示该 Transition 没有原生 Gate，返回 -1 作为无根节点标记。
    ---@param gate LuaTransitionGateExpression|nil 当前待展开的 Gate 子树。
    ---@return number root_index 当前子树根节点在 gate_nodes 中的零基索引；无 Gate 时为 -1。
    local function flatten_gate(gate)
        if gate == nil then return -1 end
        local child_indices = {}
        for _, child in ipairs(gate.Children or {}) do
            table.insert(child_indices, flatten_gate(child))
        end
        local index = #gate_nodes
        table.insert(gate_nodes, {
            Type = gate.Type,
            Name = gate.Name or "",
            Threshold = gate.Threshold or 0.0,
            ExpectedBool = gate.ExpectedBool == true,
            Children = child_indices,
        })
        return index
    end
    local gate_root = flatten_gate(self.Gate)
    return {
        Id = self.Id,
        Key = self.Key,
        SourceStateId = self.SourceStateId,
        TargetStateId = self.TargetStateId,
        RuleFunctionName = self.RuleFunctionName,
        Settings = {
            BlendDuration = self.BlendDuration,
            PriorityOrder = self.PriorityOrder,
            BlendMode = self.BlendMode,
        },
        Gate = { RootIndex = gate_root, Nodes = gate_nodes },
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimTransition
