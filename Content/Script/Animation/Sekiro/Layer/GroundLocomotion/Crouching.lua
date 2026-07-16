-- Sekiro 蹲姿 Walk/Run 子状态机。
-- 它与 Standing 保持同样的 Idle/Start/Cycle/Stop 拓扑，但每个 StateGraph 明确引用蹲姿专属资产。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local Anim = require("Animation.Sekiro.AnimAssets").Locomotion
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class CrouchingLocomotion: LuaAnimStateMachine
local CrouchingLocomotion = LuaAnimStateMachine:Extend("CrouchingLocomotion")

local StartAssets = {
    Walk = { Forward = Anim.Crouch_Walk_Forward_Start, Back = Anim.Crouch_Walk_Back_Start, Left = Anim.Crouch_Walk_Left_Start, Right = Anim.Crouch_Walk_Right_Start },
    Run = { Forward = Anim.Crouch_Run_Forward_Start, Back = Anim.Crouch_Run_Back_Start, Left = Anim.Crouch_Run_Left_Start, Right = Anim.Crouch_Run_Right_Start },
}
local CycleAssets = {
    Walk = { Forward = Anim.Crouch_Walk_Forward_Loop, Back = Anim.Crouch_Walk_Back_Loop, Left = Anim.Crouch_Walk_Left_Loop, Right = Anim.Crouch_Walk_Right_Loop },
    Run = { Forward = Anim.Crouch_Run_Forward_Loop, Back = Anim.Crouch_Run_Back_Loop, Left = Anim.Crouch_Run_Left_Loop, Right = Anim.Crouch_Run_Right_Loop },
}
local StopAssets = {
    Walk = { Forward = Anim.Crouch_Walk_Forward_Stop, Back = Anim.Crouch_Walk_Back_Stop, Left = Anim.Crouch_Walk_Left_Stop, Right = Anim.Crouch_Walk_Right_Stop },
    Run = { Forward = Anim.Crouch_Run_Forward_Stop, Back = Anim.Crouch_Run_Back_Stop, Left = Anim.Crouch_Run_Left_Stop, Right = Anim.Crouch_Run_Right_Stop },
}

---声明蹲姿 Idle/Start/Cycle/Stop 与基于曲线、剩余时间的原生 Transition。
---@param Machine LuaStateMachineNode CrouchingLocomotion 原生状态机节点。
---@return nil result 只声明拓扑。
function CrouchingLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Start")
    Machine:State("Cycle")
    Machine:State("Stop")
    Machine:Transition("Idle_Start", "Idle", "Start", { BlendDuration = Tuning.StartBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Start_Stop", "Start", "Stop", {
        BlendDuration = Tuning.StopBlendDuration, PriorityOrder = 0,
        Gate = Rule.Any(Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold), Rule.TimeRemainingLessEqual(Tuning.StopBlendDuration)),
    })
    Machine:Transition("Start_Cycle", "Start", "Cycle", {
        BlendDuration = Tuning.CycleBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.CycleBlendDuration),
    })
    Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
        BlendDuration = Tuning.StopBlendDuration, PriorityOrder = 0,
        Gate = Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold),
    })
    Machine:Transition("Stop_Start", "Stop", "Start", { BlendDuration = Tuning.StartBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Stop_Idle", "Stop", "Idle", {
        BlendDuration = Tuning.IdleBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.IdleBlendDuration),
    })
end

---构建蹲姿 Idle 循环。
---@param Graph LuaAnimStateGraph Idle 状态 Pose Graph。
---@return nil result 姿势连接 State Result。
function CrouchingLocomotion.StateGraph_Idle(Graph)
    local idle = PoseSelectors.Sequence(Graph, "CrouchingIdle", Anim.Crouch_Idle, true, nil)
    Graph.Result:Connect(idle.Pose)
end

---构建蹲姿 Walk/Run × 四方向 Start。
---@param Graph LuaAnimStateGraph Start 状态 Pose Graph。
---@return nil result 选择姿势连接 State Result。
function CrouchingLocomotion.StateGraph_Start(Graph)
    local start = PoseSelectors.WalkRun(Graph, "CrouchingStart", StartAssets, "LatchedActionDirection", "LatchedActionGait", false, nil)
    Graph.Result:Connect(start.Pose)
end

---构建蹲姿 Walk/Run × 四方向 Cycle，并消费与 Standing 相同的同步组和方向变量。
---@param Graph LuaAnimStateGraph Cycle 状态 Pose Graph。
---@return nil result 惯性化后的姿势连接 State Result。
function CrouchingLocomotion.StateGraph_Cycle(Graph)
    local cycle = PoseSelectors.WalkRun(Graph, "CrouchingCycle", CycleAssets, "CycleDirection", "CycleGait", true, Tuning.DirectionSyncGroup)
    local inertialization = Graph:Inertialization("CrouchingCycleInertialization")
    inertialization.Source:Connect(cycle.Pose)
    Graph.Result:Connect(inertialization.Pose)
end

---构建蹲姿 Walk/Run × 四方向 Stop。
---@param Graph LuaAnimStateGraph Stop 状态 Pose Graph。
---@return nil result 选择姿势连接 State Result。
function CrouchingLocomotion.StateGraph_Stop(Graph)
    local stop = PoseSelectors.WalkRun(Graph, "CrouchingStop", StopAssets, "LatchedActionDirection", "LatchedActionGait", false, nil)
    Graph.Result:Connect(stop.Pose)
end

---蹲姿有输入时从 Idle 进入 Start；Sprint 和 Dodge 由 GroundedMode 外层接管。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Start。
function CrouchingLocomotion.CanEnter_Idle_Start(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---蹲姿 Start 输入释放时请求 Stop。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function CrouchingLocomotion.CanEnter_Start_Stop(Inst)
    return Inst.bHasMovementInput ~= true
end

---蹲姿 Start 保持输入时在动画尾部进入 Cycle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Cycle。
function CrouchingLocomotion.CanEnter_Start_Cycle(Inst)
    return Inst.bHasMovementInput == true
end

---蹲姿 Cycle 输入释放时等待脚步窗口进入 Stop。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function CrouchingLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst.bHasMovementInput ~= true
end

---蹲姿 Stop 出现新输入时重新进入 Start。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否重新进入 Start。
function CrouchingLocomotion.CanEnter_Stop_Start(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---蹲姿 Stop 无新输入时在尾部回 Idle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否回 Idle。
function CrouchingLocomotion.CanEnter_Stop_Idle(Inst)
    return Inst.bHasMovementInput ~= true
end

return CrouchingLocomotion
