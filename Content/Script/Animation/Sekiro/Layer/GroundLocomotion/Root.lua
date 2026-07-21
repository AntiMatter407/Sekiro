-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Sekiro RootLocomotion 状态机。
-- Grounded 内嵌统一运动阶段状态机；InAir 内嵌 Jump Start/InAir/Land 子状态机。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local Rule = require("Animation.Compiler.TransitionRule")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local GroundedMode = require("Animation.Sekiro.Layer.GroundLocomotion.GroundedMode")
local JumpLocomotion = require("Animation.Sekiro.Layer.Airborne.Jump")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class RootLocomotion: LuaAnimStateMachine
local RootLocomotion = LuaAnimStateMachine:Extend("RootLocomotion")

---声明 Grounded/InAir 根状态及最高优先级的物理状态切换。
---@param Machine LuaStateMachineNode RootLocomotion 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function RootLocomotion.StateMachine(Machine)
    Machine.LayoutStyle = LayoutStyle.HierarchicalBlocks
    Machine:Entry("Grounded")
    Machine:State("Grounded")
    Machine:State("InAir")
    Machine:Transition("Grounded_InAir", "Grounded", "InAir", { BlendDuration = 0.08, PriorityOrder = 0 })
    Machine:Transition("InAir_GroundedMoving", "InAir", "Grounded", {
        BlendDuration = 0.10, PriorityOrder = 0,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanResumeMovement, Tuning.CurveThreshold),
    })
    Machine:Transition("InAir_Grounded", "InAir", "Grounded", {
        BlendDuration = 0.10, PriorityOrder = 1,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanExitLand, Tuning.CurveThreshold),
    })

end

---在 Grounded 状态内放置统一的 Idle/Start/Cycle/Stop/Step 阶段状态机。
---Standing/Crouching 姿态和 Walk/Run/Sprint 步态由各阶段 StateGraph 内的原生选择节点处理。
---@param Graph LuaAnimStateGraph Grounded 状态的原生 Pose Graph。
---@return nil result GroundedMode 状态机姿势连接 State Result。
function RootLocomotion.StateGraph_Grounded(Graph)
    local grounded_mode = Graph:StateMachine("GroundedMode", GroundedMode)
    Graph.Result:Connect(grounded_mode.Pose)
end

---在 InAir 根状态中放置 Jump Start/InAir/Land 子状态机。
---@param Graph LuaAnimStateGraph InAir 状态的原生 Pose Graph。
---@return nil result Jump 子状态机姿势连接 State Result。
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

---CharacterMovement 已接地、仍有移动输入且 Land 到达安全打断窗口时提前返回 Grounded。
---CanResumeMovement 对锁定八方向使用更晚的窗口，避免差异较大的落地姿势首帧直接硬切到 Cycle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否提前返回 Grounded。
function RootLocomotion.CanEnter_InAir_GroundedMoving(Inst)
    return Inst.bIsInAir ~= true and Inst.bHasMovementInput == true
end

---CharacterMovement 已接地且没有移动输入时，在 Jump Land 尾部完整返回 Grounded。
---有移动输入由更高优先级的 GroundedMoving 分支处理，不让普通落地提前结束。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Grounded。
function RootLocomotion.CanEnter_InAir_Grounded(Inst)
    return Inst.bIsInAir ~= true and Inst.bHasMovementInput ~= true
end

return RootLocomotion
