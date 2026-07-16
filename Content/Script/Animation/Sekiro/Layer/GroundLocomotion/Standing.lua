-- Sekiro 站立 Walk/Run 子状态机。
-- 拓扑固定为 Idle -> Start -> Cycle -> Stop -> Idle；资源选择由原生枚举 BlendList 完成。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local AnimAssets = require("Animation.Sekiro.AnimAssets")
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local Anim = AnimAssets.Locomotion

---@class StandingLocomotion: LuaAnimStateMachine
local StandingLocomotion = LuaAnimStateMachine:Extend("StandingLocomotion")

local StartAssets = {
    Walk = { Forward = Anim.Walk_Forward_Start, Back = Anim.Walk_Back_Start, Left = Anim.Walk_Left_Start, Right = Anim.Walk_Right_Start },
    Run = { Forward = Anim.Run_Forward_Start, Back = Anim.Run_Back_Start, Left = Anim.Run_Left_Start, Right = Anim.Run_Right_Start },
}

local FreeStartAssets = {
    Walk = {
        Forward = Anim.Walk_Forward_Start,
        Back = Anim.Walk_Back_Start,
        Left = Anim.IdleToWalk_Left_Turn,
        Right = Anim.IdleToWalk_Right_Turn,
    },
    Run = {
        Forward = Anim.Run_Forward_Start,
        Back = Anim.Run_Back_Start,
        Left = Anim.IdleToRun_Left_Turn,
        Right = Anim.IdleToRun_Right_Turn,
    },
}

local CycleAssets = {
    Walk = { Forward = Anim.Walk_Forward_Loop, Back = Anim.Walk_Back_Loop, Left = Anim.Walk_Left_Loop, Right = Anim.Walk_Right_Loop },
    Run = { Forward = Anim.Run_Forward_Loop, Back = Anim.Run_Back_Loop, Left = Anim.Run_Left_Loop, Right = Anim.Run_Right_Loop },
}

local StopAssets = {
    Walk = { Forward = Anim.Walk_Forward_Stop, Back = Anim.Walk_Back_Stop, Left = Anim.Walk_Left_Stop, Right = Anim.Walk_Right_Stop },
    Run = { Forward = Anim.Run_Forward_Stop, Back = Anim.Run_Back_Stop, Left = Anim.Run_Left_Stop, Right = Anim.Run_Right_Stop },
}

---声明 Standing 的状态、Entry 和原生 Transition；Lua 只回答移动意图，动画时机由 Gate 决定。
---@param Machine LuaStateMachineNode StandingLocomotion 原生状态机节点。
---@return nil result 只声明拓扑，不返回运行时值。
function StandingLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Start")
    Machine:State("Cycle")
    Machine:State("Stop")

    Machine:Transition("Idle_Start", "Idle", "Start", { BlendDuration = Tuning.StartBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Start_Stop", "Start", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 0,
        Gate = Rule.Any(
            Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold),
            Rule.TimeRemainingLessEqual(Tuning.StopBlendDuration)),
    })
    Machine:Transition("Start_Cycle", "Start", "Cycle", {
        BlendDuration = Tuning.CycleBlendDuration,
        PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.CycleBlendDuration),
    })
    Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 0,
        Gate = Rule.CurveGreaterEqual("CanEnterStop", Tuning.CurveThreshold),
    })
    Machine:Transition("Stop_Start", "Stop", "Start", { BlendDuration = Tuning.StartBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Stop_Idle", "Stop", "Idle", {
        BlendDuration = Tuning.IdleBlendDuration,
        PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.IdleBlendDuration),
    })
end

---构建 Standing Idle 的单一循环 SequencePlayer。
---@param Graph LuaAnimStateGraph Idle 状态的原生 Pose Graph。
---@return nil result Idle 姿势直接连接 State Result。
function StandingLocomotion.StateGraph_Idle(Graph)
    local idle = PoseSelectors.Sequence(Graph, "StandingIdle", Anim.Idle, true, nil)
    Graph.Result:Connect(idle.Pose)
end

---构建 Walk/Run × 四方向 Start；方向与步态在输入起始边沿锁定，状态中不会换片。
---@param Graph LuaAnimStateGraph Start 状态的原生 Pose Graph。
---@return nil result 选择结果连接 State Result。
function StandingLocomotion.StateGraph_Start(Graph)
    local locked_start = PoseSelectors.WalkRun(
        Graph, "StandingStart", StartAssets, "LatchedActionDirection", "LatchedActionGait", false, nil)
    local free_start = PoseSelectors.WalkRun(
        Graph, "StandingFreeStart", FreeStartAssets, "LatchedFreeStartDirection", "LatchedActionGait", false, nil)
    local locked_on = Graph:Property("StandingStartLockedOn", "bLatchedActionLockedOn")
    local start = Graph:BlendListByBool("StandingStartFacingMode")
    start.BlendTime = Tuning.StartBlendDuration
    start.TruePose:Connect(locked_start.Pose)
    start.FalsePose:Connect(free_start.Pose)
    start.ActiveValue:Connect(locked_on.Value)
    Graph.Result:Connect(start.Pose)
end

---构建 Walk/Run × 四方向 Cycle，并用同步组和 Inertialization 平滑方向与步态变化。
---@param Graph LuaAnimStateGraph Cycle 状态的原生 Pose Graph。
---@return nil result 惯性化后的循环姿势连接 State Result。
function StandingLocomotion.StateGraph_Cycle(Graph)
    local cycle = PoseSelectors.WalkRun(
        Graph, "StandingCycle", CycleAssets, "CycleDirection", "CycleGait", true, Tuning.DirectionSyncGroup)
    local inertialization = Graph:Inertialization("StandingCycleInertialization")
    inertialization.Source:Connect(cycle.Pose)
    Graph.Result:Connect(inertialization.Pose)
end

---构建 Walk/Run × 四方向 Stop；使用输入释放边沿锁定的最后方向和步态。
---@param Graph LuaAnimStateGraph Stop 状态的原生 Pose Graph。
---@return nil result Stop 选择结果连接 State Result。
function StandingLocomotion.StateGraph_Stop(Graph)
    local stop = PoseSelectors.WalkRun(
        Graph, "StandingStop", StopAssets, "LatchedActionDirection", "LatchedActionGait", false, nil)
    Graph.Result:Connect(stop.Pose)
end

---有有效移动输入且当前不是 Dodge/Sprint 时，从 Idle 进入普通 Walk/Run Start。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Start。
function StandingLocomotion.CanEnter_Idle_Start(Inst)
    return Inst.bHasMovementInput == true
        and Inst.bIsDodging ~= true
        and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---Start 期间输入释放时请求 Stop；Gate 会等待曲线窗口或接近动画结尾。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求进入 Stop。
function StandingLocomotion.CanEnter_Start_Stop(Inst)
    return Inst.bHasMovementInput ~= true
end

---Start 仍有输入时请求 Cycle；原生剩余时间 Gate 保证 Start 主体完整播放。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求进入 Cycle。
function StandingLocomotion.CanEnter_Start_Cycle(Inst)
    return Inst.bHasMovementInput == true
end

---Cycle 输入释放时请求 Stop；CanEnterStop 曲线选择自然脚步窗口。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求进入 Stop。
function StandingLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst.bHasMovementInput ~= true
end

---Stop 中重新出现普通移动输入时立即回 Start，使短暂停止可以流畅反悔。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否打断 Stop 并重新进入 Start。
function StandingLocomotion.CanEnter_Stop_Start(Inst)
    return Inst.bHasMovementInput == true
        and Inst.bIsDodging ~= true
        and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---Stop 没有新输入时在动画尾部回 Idle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否允许回到 Idle。
function StandingLocomotion.CanEnter_Stop_Idle(Inst)
    return Inst.bHasMovementInput ~= true
end

return StandingLocomotion
