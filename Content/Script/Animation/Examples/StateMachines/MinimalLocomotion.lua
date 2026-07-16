-- 可被多个动画蓝图引用的最小移动状态机。
-- 本文件只关心 Entry、State、Transition、状态内部动画和运行时进入条件。
local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")

---@class MinimalLocomotion: LuaAnimStateMachine
local MinimalLocomotion = LuaAnimStateMachine:Extend("MinimalLocomotion")

---声明状态机拓扑；每个 State 的 Pose 节点由同名 StateGraph_* 函数独立描述。
---@param Machine LuaStateMachineNode 状态机节点及其内部原生 StateMachine Graph。
---@return nil result 该函数只声明 Entry、State 和 Transition。
function MinimalLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Move")

    local idle_to_move = Machine:Transition("Idle_Move", "Idle", "Move")
    idle_to_move.BlendDuration = 0.15
    idle_to_move.PriorityOrder = 0

    local move_to_idle = Machine:Transition("Move_Idle", "Move", "Idle")
    move_to_idle.BlendDuration = 0.15
    move_to_idle.PriorityOrder = 0
end

---声明 Idle 状态的动画节点；SequencePlayer 属性直接对应 UE 节点详情。
---@param Graph LuaAnimStateGraph Idle 状态独占的 StatePose Graph。
---@return nil result IdlePlayer 的姿势连接到 State Result。
function MinimalLocomotion.StateGraph_Idle(Graph)
    local idle_player = Graph:SequencePlayer("IdlePlayer")
    idle_player.Sequence = "/Engine/EngineMeshes/SkeletalCube_Anim.SkeletalCube_Anim"
    idle_player.bLoopAnimation = true
    idle_player.PlayRate = 1.0

    Graph.Result:Connect(idle_player.Pose)
end

---声明 Move 状态的动画节点；示例复用引擎动画，只验证独立状态图编译流程。
---@param Graph LuaAnimStateGraph Move 状态独占的 StatePose Graph。
---@return nil result MovePlayer 的姿势连接到 State Result。
function MinimalLocomotion.StateGraph_Move(Graph)
    local move_player = Graph:SequencePlayer("MovePlayer")
    move_player.Sequence = "/Engine/EngineMeshes/SkeletalCube_Anim.SkeletalCube_Anim"
    move_player.bLoopAnimation = true
    move_player.PlayRate = 1.0

    Graph.Result:Connect(move_player.Pose)
end

---判断是否从 Idle 进入 Move；运行时 Inst 是生成动画蓝图使用的真实 UAnimInstance 代理。
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 示例固定不进入 Move，项目代码可直接读取 C++ 反射变量。
function MinimalLocomotion.CanEnter_Idle_Move(Inst)
    return false
end

---判断是否从 Move 返回 Idle；运行时 Inst 是生成动画蓝图使用的真实 UAnimInstance 代理。
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 示例固定允许返回 Idle。
function MinimalLocomotion.CanEnter_Move_Idle(Inst)
    return true
end

return MinimalLocomotion
