-- Sekiro 第一阶段 RootLocomotion 状态机。
-- Grounded 内嵌 StandingLocomotion；InAir 暂时输出 Jump Loop，后续替换为完整 JumpSM 而不改变根拓扑。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local GroundedMode = require("Animation.Sekiro.Layer.GroundLocomotion.GroundedMode")
local JumpLocomotion = require("Animation.Sekiro.Layer.Airborne.Jump")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class RootLocomotion: LuaAnimStateMachine
local RootLocomotion = LuaAnimStateMachine:Extend("RootLocomotion")

---声明 Grounded/InAir 根状态及最高优先级的物理状态切换。
---@param Machine LuaStateMachineNode RootLocomotion 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function RootLocomotion.StateMachine(Machine)
    Machine:Entry("Grounded")
    Machine:State("Grounded")
    Machine:State("InAir")
    Machine:Transition("Grounded_InAir", "Grounded", "InAir", { BlendDuration = 0.08, PriorityOrder = 0 })
    Machine:Transition("InAir_Grounded", "InAir", "Grounded", {
        BlendDuration = 0.10, PriorityOrder = 0,
        Gate = Rule.TimeRemainingLessEqual(Tuning.JumpBlendDuration),
    })
end

---在 Grounded 状态内放置包含 Standing/Crouching 子状态机的 GroundedMode。
---@param Graph LuaAnimStateGraph Grounded 状态的原生 Pose Graph。
---@return nil result Standing 状态机姿势连接 State Result。
function RootLocomotion.StateGraph_Grounded(Graph)
    local grounded_mode = Graph:StateMachine("GroundedMode", GroundedMode)
    Graph.Result:Connect(grounded_mode.Pose)
end

---在 InAir 根状态中放置 Jump Start/InAir/Land 子状态机。
---@param Graph LuaAnimStateGraph InAir 状态的原生 Pose Graph。
---@return nil result Jump Loop 姿势连接 State Result。
function RootLocomotion.StateGraph_InAir(Graph)
    local jump = Graph:StateMachine("JumpLocomotion", JumpLocomotion)
    Graph.Result:Connect(jump.Pose)
end

---CharacterMovement 报告离地时立刻进入 InAir，优先于所有地面动作。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 InAir。
function RootLocomotion.CanEnter_Grounded_InAir(Inst)
    return Inst.bIsInAir == true
end

---CharacterMovement 报告落地时返回 Grounded；完整 JumpSM 接入后 Land 会在子状态机内处理。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Grounded。
function RootLocomotion.CanEnter_InAir_Grounded(Inst)
    return Inst.bIsInAir ~= true
end

return RootLocomotion
