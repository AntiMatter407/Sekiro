-- Sekiro Jump 子状态机。
-- Start/Land 使用带 RootMotion 的八方向原始资产，InAir 只输出姿势并由 CharacterMovement 负责轨迹。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local Anim = require("Animation.Sekiro.AnimAssets").Jump
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class JumpLocomotion: LuaAnimStateMachine
local JumpLocomotion = LuaAnimStateMachine:Extend("JumpLocomotion")

local StartAssets = {
    Forward = Anim.Jump_Start_Forward, ForwardLeft = Anim.Jump_Start_ForwardLeft,
    Left = Anim.Jump_Start_Left, BackLeft = Anim.Jump_Start_BackLeft,
    Back = Anim.Jump_Start_Back, BackRight = Anim.Jump_Start_BackRight,
    Right = Anim.Jump_Start_Right, ForwardRight = Anim.Jump_Start_ForwardRight,
}
local InAirAssets = {
    Forward = Anim.Jump_InAir_Forward, ForwardLeft = Anim.Jump_InAir_ForwardLeft,
    Left = Anim.Jump_InAir_Left, BackLeft = Anim.Jump_InAir_BackLeft,
    Back = Anim.Jump_InAir_Back, BackRight = Anim.Jump_InAir_BackRight,
    Right = Anim.Jump_InAir_Right, ForwardRight = Anim.Jump_InAir_ForwardRight,
}
local LandAssets = {
    Forward = Anim.Jump_Land_Forward, ForwardLeft = Anim.Jump_Land_ForwardLeft,
    Left = Anim.Jump_Land_Left, BackLeft = Anim.Jump_Land_BackLeft,
    Back = Anim.Jump_Land_Back, BackRight = Anim.Jump_Land_BackRight,
    Right = Anim.Jump_Land_Right, ForwardRight = Anim.Jump_Land_ForwardRight,
}

---声明 Jump Start/InAir/Land；物理落地立即进入 Land，根状态机等待 Land 尾部再回 Grounded。
---@param Machine LuaStateMachineNode JumpLocomotion 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function JumpLocomotion.StateMachine(Machine)
    Machine:Entry("Start")
    Machine:State("Start")
    Machine:State("InAir")
    Machine:State("Land")
    Machine:Transition("Start_Land", "Start", "Land", { BlendDuration = Tuning.JumpBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Start_InAir", "Start", "InAir", {
        BlendDuration = Tuning.JumpBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.JumpBlendDuration),
    })
    Machine:Transition("InAir_Land", "InAir", "Land", { BlendDuration = Tuning.JumpBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Land_Start", "Land", "Start", { BlendDuration = Tuning.JumpBlendDuration, PriorityOrder = 0 })
end

---构建带 RootMotion 的八方向 Jump Start。
---@param Graph LuaAnimStateGraph Start 状态 Pose Graph。
---@return nil result Start 姿势连接 State Result。
function JumpLocomotion.StateGraph_Start(Graph)
    local start = PoseSelectors.Octant(Graph, "JumpStart", StartAssets, "JumpDirection", false)
    Graph.Result:Connect(start.Pose)
end

---构建不提供位移的八方向 InAir 姿势。
---@param Graph LuaAnimStateGraph InAir 状态 Pose Graph。
---@return nil result InAir 姿势连接 State Result。
function JumpLocomotion.StateGraph_InAir(Graph)
    local in_air = PoseSelectors.Octant(Graph, "JumpInAir", InAirAssets, "JumpDirection", true)
    Graph.Result:Connect(in_air.Pose)
end

---构建带 RootMotion 的八方向 Land。
---@param Graph LuaAnimStateGraph Land 状态 Pose Graph。
---@return nil result Land 姿势连接 State Result。
function JumpLocomotion.StateGraph_Land(Graph)
    local land = PoseSelectors.Octant(Graph, "JumpLand", LandAssets, "JumpDirection", false)
    Graph.Result:Connect(land.Pose)
end

---Start 期间已经落地时直接进入 Land，处理极短腾空或碰撞提前接地。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Land。
function JumpLocomotion.CanEnter_Start_Land(Inst)
    return Inst.bIsInAir ~= true
end

---仍在空中时于 Start 尾部进入 InAir。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 InAir。
function JumpLocomotion.CanEnter_Start_InAir(Inst)
    return Inst.bIsInAir == true
end

---CharacterMovement 报告落地时立即从 InAir 进入 Land。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Land。
function JumpLocomotion.CanEnter_InAir_Land(Inst)
    return Inst.bIsInAir ~= true
end

---Land 过程中再次离地时重新进入 Start，支持连续跳跃和台阶边缘情况。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否重新进入 Start。
function JumpLocomotion.CanEnter_Land_Start(Inst)
    return Inst.bIsInAir == true
end

return JumpLocomotion
