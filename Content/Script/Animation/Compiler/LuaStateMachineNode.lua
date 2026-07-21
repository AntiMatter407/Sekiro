-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- UE StateMachine AnimNode 的 Lua 编译期抽象。
-- 节点位于 Pose Graph、输出 Pose，并拥有一个保存 Entry/State/Transition 的内部 Graph。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")
local LuaAnimStateMachineGraph = require("Animation.Compiler.LuaAnimStateMachineGraph")

---@class LuaStateMachineNodeConfig: LuaAnimNodeConfig

---@class LuaStateMachineNode: LuaAnimNode
---@field OwnedGraph LuaAnimStateMachineGraph 节点独占的内部状态机 Graph。
---@field LayoutStyle string 内部状态机未显式放置 State 的自动排版风格。
---@field Pose LuaAnimPin 状态机最终姿势输出 Pin。
local LuaStateMachineNode = LuaAnimNode:Extend("LuaStateMachineNode")

---把 StateMachine 节点的 LayoutStyle 配置转发给 OwnedGraph，其余字段继续遵守 AnimNode 属性契约。
---@param instance LuaStateMachineNode 正在接收配置的状态机节点。
---@param key string 待写字段名。
---@param value any Lua 声明值。
---@return nil result LayoutStyle 写入内部 Graph，其余字段交回基类处理。
local function assign_state_machine_field(instance, key, value)
    if key == "LayoutStyle" and rawget(instance, "OwnedGraph") ~= nil then
        instance.OwnedGraph.LayoutStyle = value
        return
    end
    LuaAnimNode.__newindex(instance, key, value)
end

LuaStateMachineNode.__newindex = assign_state_machine_field

---初始化 StateMachine AnimNode，并在所属 Layer 登记它拥有的内部状态机 Graph。
---@param config LuaStateMachineNodeConfig Graph、节点名和源码位置等构造参数。
---@return nil result 该函数只初始化节点与内部 Graph，不返回业务值。
function LuaStateMachineNode:Initialize(config)
    config.NodeType = "StateMachine"
    LuaAnimNode.Initialize(self, config)

    ---@type LuaAnimStateMachineGraph
    self.OwnedGraph = LuaAnimStateMachineGraph:New({
        Blueprint = self.Graph.Blueprint,
        Layer = self.Graph.Layer,
        OwnerNode = self,
        SourceLocation = self.SourceLocation,
    })
    self.OwnedGraphId = self.OwnedGraph.Id
    self.Graph.Layer:AddGraph(self.OwnedGraph)
end

---声明内部状态；接口保留在 StateMachine Node 上，使业务 DSL 与节点所有权保持一致。
---@param name string 状态语义名。
---@param build_function (fun(graph: LuaAnimStateGraph):nil)|nil 配置该状态独占 Pose Graph 的声明函数。
---@param settings LuaAnimStateSettings|nil bAlwaysResetOnEntry 等状态编译设置。
---@return LuaAnimState state 新建的 State 编译期实例。
function LuaStateMachineNode:State(name, build_function, settings)
    return self.OwnedGraph:State(name, build_function, settings)
end

---设置 StateMachine Node 内部 Entry 指向的默认状态。
---@param state_name string 默认进入状态的语义名。
---@return nil result 该函数只更新内部 Graph 的 Entry 引用。
function LuaStateMachineNode:Entry(state_name)
    self.OwnedGraph:Entry(state_name)
end

---声明 StateMachine Node 内部的有向 Transition。
---@param key string 状态机内唯一的 Transition Key。
---@param source_state_name string 起始状态语义名。
---@param target_state_name string 目标状态语义名。
---@param settings LuaAnimTransitionSettings|nil Transition 混合、优先级和规则函数设置。
---@return LuaAnimTransition transition 新建且可直接配置混合属性的 Transition 对象。
function LuaStateMachineNode:Transition(key, source_state_name, target_state_name, settings)
    return self.OwnedGraph:Transition(key, source_state_name, target_state_name, settings)
end

---创建内部 StateMachine Graph 的布局分区，使状态拓扑与编辑器排版在同一声明函数配置。
---@param name string 内部状态机 Graph 中唯一的布局分区名。
---@param settings LuaGraphLayoutGridSettings|nil 分区区域、单元格间距和风格覆盖。
---@return LuaGraphLayoutGrid grid 可继续 Place 本状态机 State 的布局分区。
function LuaStateMachineNode:Grid(name, settings)
    return self.OwnedGraph:Grid(name, settings)
end

return LuaStateMachineNode
