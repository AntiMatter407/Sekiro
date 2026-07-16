-- StateMachine Node 拥有的内部状态机 Graph。
-- 本类只声明 Entry、State 和 Transition；它不是 AnimNode，也不能直接连接 Pose Pin。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LuaAnimState = require("Animation.Compiler.LuaAnimState")
local LuaAnimTransition = require("Animation.Compiler.LuaAnimTransition")

---@class LuaAnimStateMachineGraphConfig
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Layer LuaAnimLayer 所属动画层。
---@field OwnerNode LuaStateMachineNode 拥有该内部 Graph 的 StateMachine 节点。
---@field SourceLocation SekiroAnimIRSourceLocation|nil Graph 源码位置。
---@field DeclarationOrder number|nil 源码中的确定性声明顺序整数。

---@class LuaAnimStateSettings
---@field bAlwaysResetOnEntry boolean|nil 重新进入状态时是否重置 Pose Graph。

---@class LuaAnimTransitionSettings
---@field RuleFunctionName string|nil 自定义 Lua 规则函数名；为空时由状态机节点名和 Transition Key 推导。
---@field BlendDuration number|nil 过渡混合时长，单位为秒。
---@field PriorityOrder number|nil 同一源状态下的显式过渡优先级整数。
---@field BlendMode string|nil UE 过渡混合模式注册名。

---@class LuaAnimStateMachineGraph: CompilerClass
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Layer LuaAnimLayer 所属动画层。
---@field OwnerNode LuaStateMachineNode 拥有该内部 Graph 的 StateMachine 节点。
---@field Name string Graph 语义名称。
---@field Id string Graph 稳定 ID。
---@field GraphType string Graph 注册类型，内部状态机固定为 StateMachine。
---@field SourceLocation SekiroAnimIRSourceLocation Graph 源码位置。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field EntryStateId string Entry 指向的 State 稳定 ID。
---@field States LuaAnimState[] 状态声明；每个实例独占一个 StatePose Graph。
---@field Transitions LuaAnimTransition[] 有向过渡声明。
---@field StateNames table<string, LuaAnimState> 按语义名称索引的状态。
---@field TransitionKeys table<string, boolean> 状态机作用域内已声明的 Transition Key 集合。
local LuaAnimStateMachineGraph = CompilerClass:Extend("LuaAnimStateMachineGraph")

---初始化 StateMachine Node 的内部 Graph 和空拓扑。
---@param config LuaAnimStateMachineGraphConfig Blueprint、Layer、OwnerNode 和源码位置等构造参数。
---@return nil result 该函数只初始化内部 Graph，不返回业务值。
function LuaAnimStateMachineGraph:Initialize(config)
    self.Blueprint = assert(config.Blueprint, "LuaAnimStateMachineGraph requires Blueprint")
    self.Layer = assert(config.Layer, "LuaAnimStateMachineGraph requires Layer")
    self.OwnerNode = assert(config.OwnerNode, "LuaAnimStateMachineGraph requires OwnerNode")
    self.Name = self.OwnerNode.Name .. "Graph"
    self.Id = IRSchema.MakeStableId(self.OwnerNode.Id, "Graph", "StateMachine")
    self.GraphType = "StateMachine"
    self.SourceLocation = config.SourceLocation
        or IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3)
    self.DeclarationOrder = config.DeclarationOrder or 0
    self.EntryStateId = ""
    self.States = {}
    self.Transitions = {}
    self.StateNames = {}
    self.TransitionKeys = {}
end

---声明 State 顶点，并立即创建由该 State 独占的 StatePose Graph。
---@param name string 状态语义名。
---@param build_function (fun(graph: LuaAnimStateGraph):nil)|nil 配置该状态 Pose 节点与 Link 的声明函数。
---@param settings LuaAnimStateSettings|nil bAlwaysResetOnEntry 等状态编译设置。
---@return LuaAnimState state 新建的 State 编译期实例。
function LuaAnimStateMachineGraph:State(name, build_function, settings)
    local valid_name = IRSchema.RequireSemanticName(name, "State")
    assert(self.StateNames[valid_name] == nil, string.format(
        "StateMachine '%s' contains duplicate State '%s'",
        self.OwnerNode.Name,
        valid_name))

    local state_id = IRSchema.MakeStableId(self.Id, "State", valid_name)
    local source_location = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 4)
    local LuaAnimStateGraph = require("Animation.Compiler.LuaAnimStateGraph")
    ---@type LuaAnimStateGraph
    local pose_graph = LuaAnimStateGraph:New({
        Blueprint = self.Blueprint,
        Layer = self.Layer,
        Name = valid_name,
        Id = IRSchema.MakeStableId(state_id, "Graph", "StatePose"),
        SourceLocation = source_location,
    })
    self.Layer:AddGraph(pose_graph)

    ---@type LuaAnimState
    local state = LuaAnimState:New({
        MachineGraph = self,
        Id = state_id,
        Name = valid_name,
        PoseGraph = pose_graph,
        Settings = settings,
        DeclarationOrder = #self.States,
        SourceLocation = source_location,
    })
    self.StateNames[valid_name] = state
    table.insert(self.States, state)
    if build_function ~= nil then
        build_function(pose_graph)
    end
    return state
end

---设置内部 Entry 节点指向的默认状态；目标可在本调用后声明并由 Validator 最终校验。
---@param state_name string 默认进入状态的语义名。
---@return nil result 该函数只更新 Entry 引用，不返回业务值。
function LuaAnimStateMachineGraph:Entry(state_name)
    local valid_name = IRSchema.RequireSemanticName(state_name, "Entry State")
    self.EntryStateId = IRSchema.MakeStableId(self.Id, "State", valid_name)
end

---声明具有显式身份的有向 Transition；同一对状态可通过不同 Key 声明多条并行规则。
---@param key string 状态机内唯一的 Transition Key，同时参与稳定 ID 与默认规则函数名生成。
---@param source_state_name string 起始状态语义名。
---@param target_state_name string 目标状态语义名。
---@param settings LuaAnimTransitionSettings|nil BlendDuration、PriorityOrder、BlendMode 和可选 RuleFunctionName。
---@return LuaAnimTransition transition 新建且可直接配置混合属性的 Transition 对象。
function LuaAnimStateMachineGraph:Transition(key, source_state_name, target_state_name, settings)
    local transition_key = IRSchema.RequireLuaIdentifier(key, "Transition Key")
    assert(self.TransitionKeys[transition_key] == nil, string.format(
        "StateMachine '%s' contains duplicate Transition Key '%s'",
        self.OwnerNode.Name,
        transition_key))
    local source_name = IRSchema.RequireSemanticName(source_state_name, "Transition Source State")
    local target_name = IRSchema.RequireSemanticName(target_state_name, "Transition Target State")
    ---@type LuaAnimTransitionSettings
    local transition_settings = settings or {}
    local rule_function_name = string.format("CanEnter_%s_%s", self.OwnerNode.Name, transition_key)
    if transition_settings.RuleFunctionName ~= nil then
        rule_function_name = IRSchema.RequireLuaIdentifier(
            transition_settings.RuleFunctionName,
            "Transition Rule Function")
    end
    ---@type LuaAnimTransition
    local transition = LuaAnimTransition:New({
        Id = IRSchema.MakeStableId(self.Id, "Transition", transition_key),
        Key = transition_key,
        SourceStateId = IRSchema.MakeStableId(self.Id, "State", source_name),
        TargetStateId = IRSchema.MakeStableId(self.Id, "State", target_name),
        RuleFunctionName = rule_function_name,
        Settings = transition_settings,
        DeclarationOrder = #self.Transitions,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 4),
    })
    self.TransitionKeys[transition_key] = true
    table.insert(self.Transitions, transition)
    return transition
end

---导出内部 StateMachine Graph；其所有者由外层 StateMachine Node 的 OwnedGraphId 表达。
---@return SekiroAnimIRGraph ir_graph 可由 C++ 导入器解析的状态机内部 Graph。
function LuaAnimStateMachineGraph:ToIR()
    ---@type SekiroAnimIRState[]
    local states = {}
    for _, state in ipairs(self.States) do
        table.insert(states, state:ToIR())
    end

    ---@type SekiroAnimIRTransition[]
    local transitions = {}
    for _, transition in ipairs(self.Transitions) do
        table.insert(transitions, transition:ToIR())
    end

    return {
        Id = self.Id,
        Name = self.Name,
        GraphType = self.GraphType,
        RootNodeId = "",
        Nodes = {},
        Links = {},
        StateMachine = {
            EntryStateId = self.EntryStateId,
            States = states,
            Transitions = transitions,
        },
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimStateMachineGraph
