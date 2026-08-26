-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，不持有 UObject。
-- 使用七个 UE 原生 State 显式表达 Idle、四向锁定移动和左右原地转身，便于 AnimGraph 调试。
-- 动作 Montage 由外层 CombatFullBodySlot 覆盖，本状态机只拥有导航基础姿势。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Rule = require("Animation.Compiler.TransitionRule")
local AnimAssets = require("Animation.Genichiro.GenichiroAnimAssets")

---@class GenichiroLocomotion: LuaAnimStateMachine
local GenichiroLocomotion = LuaAnimStateMachine:Extend("GenichiroLocomotion")

local StateDefinitions = {
    { Name = "MoveForward", Flag = "bMoveForward", Asset = AnimAssets.Locomotion.MoveForward, Loop = true },
    { Name = "MoveBackward", Flag = "bMoveBackward", Asset = AnimAssets.Locomotion.MoveBackward, Loop = true },
    { Name = "MoveLeft", Flag = "bMoveLeft", Asset = AnimAssets.Locomotion.MoveLeft, Loop = true },
    { Name = "MoveRight", Flag = "bMoveRight", Asset = AnimAssets.Locomotion.MoveRight, Loop = true },
    { Name = "TurnLeft", Flag = "bTurnLeftRequested", Asset = AnimAssets.Locomotion.TurnLeft, Loop = false },
    { Name = "TurnRight", Flag = "bTurnRightRequested", Asset = AnimAssets.Locomotion.TurnRight, Loop = false },
}

---构造“所有移动与转身请求均关闭”的 Idle 过渡条件。
---@return LuaTransitionGateExpression rule 原生 All 组合规则。
local function idle_rule()
    local conditions = {}
    for _index, definition in ipairs(StateDefinitions) do
        conditions[#conditions + 1] = Rule.BoolProperty(definition.Flag, false)
    end
    return Rule.All(table.unpack(conditions))
end

---为一个来源状态声明到所有其他活动状态及 Idle 的确定性过渡。
---@param Machine LuaStateMachineNode 状态机节点。
---@param source_state string 来源状态名。
---@return nil 无返回值。
local function add_outgoing_transitions(Machine, source_state)
    local priority = 0
    for _index, target in ipairs(StateDefinitions) do
        if target.Name ~= source_state then
            local transition = Machine:Transition(
                source_state .. "_" .. target.Name,
                source_state,
                target.Name,
                { Rule = Rule.BoolProperty(target.Flag, true) })
            transition.BlendDuration = 0.12
            transition.PriorityOrder = priority
            transition.BlendMode = "Linear"
            priority = priority + 1
        end
    end
    if source_state ~= "Idle" then
        local to_idle = Machine:Transition(
            source_state .. "_Idle",
            source_state,
            "Idle",
            { Rule = idle_rule() })
        to_idle.BlendDuration = 0.15
        to_idle.PriorityOrder = priority
        to_idle.BlendMode = "Linear"
    end
end

---创建一个 SequencePlayer 并把姿势连接到当前状态结果。
---@param Graph LuaAnimStateGraph 当前状态 Pose Graph。
---@param node_name string SequencePlayer 语义名。
---@param asset_path string AnimSequence 对象路径。
---@param loop_animation boolean 是否循环。
---@return nil 无返回值。
local function build_sequence_state(Graph, node_name, asset_path, loop_animation)
    local player = Graph:Node(
        node_name .. "Player",
        EditorNodeClass.SequencePlayer,
        {
            Sequence = asset_path,
            bLoopAnimation = loop_animation,
            PlayRate = 1.0,
        },
        "SequencePlayer")
    Graph.Result:Connect(player.Pose)
end

---声明弦一郎基础移动状态机拓扑与过渡规则。
---@param Machine LuaStateMachineNode 状态机节点及内部 Graph。
---@return nil 无返回值。
function GenichiroLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    for _index, definition in ipairs(StateDefinitions) do
        Machine:State(definition.Name)
    end
    add_outgoing_transitions(Machine, "Idle")
    for _index, definition in ipairs(StateDefinitions) do
        add_outgoing_transitions(Machine, definition.Name)
    end
end

---声明 Idle 循环姿势。
---@param Graph LuaAnimStateGraph Idle 状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_Idle(Graph)
    build_sequence_state(Graph, "Idle", AnimAssets.Locomotion.Idle, true)
end

---声明前向锁定移动循环。
---@param Graph LuaAnimStateGraph 前向移动状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_MoveForward(Graph)
    build_sequence_state(Graph, "MoveForward", AnimAssets.Locomotion.MoveForward, true)
end

---声明后向锁定移动循环。
---@param Graph LuaAnimStateGraph 后向移动状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_MoveBackward(Graph)
    build_sequence_state(Graph, "MoveBackward", AnimAssets.Locomotion.MoveBackward, true)
end

---声明左向锁定移动循环。
---@param Graph LuaAnimStateGraph 左向移动状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_MoveLeft(Graph)
    build_sequence_state(Graph, "MoveLeft", AnimAssets.Locomotion.MoveLeft, true)
end

---声明右向锁定移动循环。
---@param Graph LuaAnimStateGraph 右向移动状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_MoveRight(Graph)
    build_sequence_state(Graph, "MoveRight", AnimAssets.Locomotion.MoveRight, true)
end

---声明左原地转身姿势。
---@param Graph LuaAnimStateGraph 左转状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_TurnLeft(Graph)
    build_sequence_state(Graph, "TurnLeft", AnimAssets.Locomotion.TurnLeft, false)
end

---声明右原地转身姿势。
---@param Graph LuaAnimStateGraph 右转状态图。
---@return nil 无返回值。
function GenichiroLocomotion.StateGraph_TurnRight(Graph)
    build_sequence_state(Graph, "TurnRight", AnimAssets.Locomotion.TurnRight, false)
end

return GenichiroLocomotion
