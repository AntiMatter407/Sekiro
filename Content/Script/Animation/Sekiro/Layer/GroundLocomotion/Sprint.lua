-- Sekiro Sprint 子状态机。
-- Sprint 使用独立 Start/Cycle/Stop；Cycle 只有前向动画，角色转向与位移方向由 Movement 负责。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local Anim = require("Animation.Sekiro.AnimAssets").Locomotion
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SprintLocomotion: LuaAnimStateMachine
local SprintLocomotion = LuaAnimStateMachine:Extend("SprintLocomotion")

---声明 SprintStart/SprintCycle/SprintStop 和动画门控。
---@param Machine LuaStateMachineNode SprintLocomotion 原生状态机节点。
---@return nil result 只声明拓扑。
function SprintLocomotion.StateMachine(Machine)
    Machine:Entry("Start")
    Machine:State("Start")
    Machine:State("Cycle")
    Machine:State("Stop")
    Machine:Transition("Start_Stop", "Start", "Stop", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 0,
        Gate = Rule.Any(Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold), Rule.TimeRemainingLessEqual(Tuning.SprintBlendDuration)),
    })
    Machine:Transition("Start_Cycle", "Start", "Cycle", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.SprintBlendDuration),
    })
    Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 0,
        Gate = Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold),
    })
    Machine:Transition("Stop_Start", "Stop", "Start", { BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 0 })
end

---构建按进入角锁定的四方向 Sprint Start；这些资产表达起步转向姿态，不负责最终角色旋转。
---@param Graph LuaAnimStateGraph Start 状态 Pose Graph。
---@return nil result Start 选择姿势连接 State Result。
function SprintLocomotion.StateGraph_Start(Graph)
    local start = PoseSelectors.Cardinal(Graph, "SprintStart", {
        Forward = Anim.Sprint_Forward_Start,
        Back = Anim.Sprint_Back_Turn_Start,
        Left = Anim.Sprint_Left_Turn_Start,
        Right = Anim.Sprint_Right_Turn_Start,
    }, "LatchedActionDirection", false, nil)
    Graph.Result:Connect(start.Pose)
end

---构建唯一的前向 Sprint Cycle，方向改变期间保持循环并交给 Movement 快速转向。
---@param Graph LuaAnimStateGraph Cycle 状态 Pose Graph。
---@return nil result Sprint Loop 连接 State Result。
function SprintLocomotion.StateGraph_Cycle(Graph)
    local cycle = PoseSelectors.Sequence(Graph, "SprintCycle", Anim.Sprint_Forward_Loop, true, Tuning.DirectionSyncGroup)
    Graph.Result:Connect(cycle.Pose)
end

---构建 Sprint Stop 选择器；后向没有专属 Stop，显式回退到 Forward Stop。
---@param Graph LuaAnimStateGraph Stop 状态 Pose Graph。
---@return nil result Stop 选择姿势连接 State Result。
function SprintLocomotion.StateGraph_Stop(Graph)
    local stop = PoseSelectors.Cardinal(Graph, "SprintStop", {
        Forward = Anim.Sprint_Forward_Stop,
        Back = Anim.Sprint_Forward_Stop,
        Left = Anim.Sprint_Forward_Left_Turn_Stop,
        Right = Anim.Sprint_Forward_Right_Turn_Stop,
    }, "LatchedActionDirection", false, nil)
    Graph.Result:Connect(stop.Pose)
end

---Sprint 意图结束或移动输入释放时，从 Start 转向 Stop。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function SprintLocomotion.CanEnter_Start_Stop(Inst)
    return Inst.DesiredGait ~= UE.ESKAnimGait.Sprint or Inst.bHasMovementInput ~= true
end

---保持 Sprint 意图和输入时，在 Start 尾部进入 Cycle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Cycle。
function SprintLocomotion.CanEnter_Start_Cycle(Inst)
    return Inst.DesiredGait == UE.ESKAnimGait.Sprint and Inst.bHasMovementInput == true
end

---Cycle 中失去 Sprint 意图或输入时，在脚步窗口进入 Stop。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function SprintLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst.DesiredGait ~= UE.ESKAnimGait.Sprint or Inst.bHasMovementInput ~= true
end

---Stop 期间重新获得 Sprint 意图和移动输入时重新起步。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否重新进入 Start。
function SprintLocomotion.CanEnter_Stop_Start(Inst)
    return Inst.DesiredGait == UE.ESKAnimGait.Sprint and Inst.bHasMovementInput == true
end

return SprintLocomotion
