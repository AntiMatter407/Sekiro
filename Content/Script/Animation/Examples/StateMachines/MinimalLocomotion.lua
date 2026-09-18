-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- 可被多个动画蓝图引用的最小移动状态机。
-- 本文件只关心 Entry、State、Transition、状态内部动画和可编译为 UE K2 节点的原生过渡规则。
local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Rule = require("Animation.Compiler.TransitionRule")
local AnimAssets = require("Animation.Sekiro.AnimAssets")

---@class MinimalLocomotion: LuaAnimStateMachine
local MinimalLocomotion = LuaAnimStateMachine:Extend("MinimalLocomotion")

---声明状态机拓扑；每个 State 的 Pose 节点由同名 StateGraph_* 函数独立描述。
---@param Machine LuaStateMachineNode 状态机节点及其内部原生 StateMachine Graph。
---@return nil result 该函数只声明 Entry、State 和 Transition。
function MinimalLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Move")

    -- bShouldMove 是 ABP_Minimal 声明的生成变量；Factory 会把本规则创建为原生 Property Getter 和布尔比较。
    local idle_to_move = Machine:Transition("Idle_Move", "Idle", "Move", {
        Rule = Rule.BoolProperty("bShouldMove", true),
    })
    idle_to_move.BlendDuration = 0.15
    idle_to_move.PriorityOrder = 0
    idle_to_move.BlendMode = UE.EAlphaBlendOption.Linear

    local move_to_idle = Machine:Transition("Move_Idle", "Move", "Idle", {
        Rule = Rule.BoolProperty("bShouldMove", false),
    })
    move_to_idle.BlendDuration = 0.15
    move_to_idle.PriorityOrder = 0
    move_to_idle.BlendMode = UE.EAlphaBlendOption.Linear
end

---声明 Idle 状态的动画节点；SequencePlayer 属性直接对应 UE 节点详情。
---@param Graph LuaAnimStateGraph Idle 状态独占的 StatePose Graph。
---@return nil result IdlePlayer 的姿势连接到 State Result。
function MinimalLocomotion.StateGraph_Idle(Graph)
    local idle_player = Graph:Node(
        "IdlePlayer",
        EditorNodeClass.SequencePlayer,
        {
            Sequence = AnimAssets.Locomotion.Idle,
            bLoopAnimation = true,
            PlayRate = 1.0,
        },
        "SequencePlayer")

    Graph.Result:Connect(idle_player.Pose)
end

---声明 Move 状态的动画节点；使用前向跑步循环，让最小示例中的状态切换可以直接观察。
---@param Graph LuaAnimStateGraph Move 状态独占的 StatePose Graph。
---@return nil result MovePlayer 的姿势连接到 State Result。
function MinimalLocomotion.StateGraph_Move(Graph)
    local move_player = Graph:Node(
        "MovePlayer",
        EditorNodeClass.SequencePlayer,
        {
            Sequence = AnimAssets.Locomotion.Run_Forward_Loop,
            bLoopAnimation = true,
            PlayRate = 1.0,
        },
        "SequencePlayer")

    Graph.Result:Connect(move_player.Pose)
end

return MinimalLocomotion
