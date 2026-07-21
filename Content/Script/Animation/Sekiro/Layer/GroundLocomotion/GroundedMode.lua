-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Sekiro 地面运动阶段状态机。
-- 状态机通过 EntryRouter 恢复当前地面意图，再管理 Idle/Turn/Start/Cycle/Stop/Step 时序；Standing/Crouching 姿态与 Walk/Run/Sprint 步态在 StateGraph 内选择。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local Rule = require("Animation.Compiler.TransitionRule")
local Standing = require("Animation.Sekiro.Layer.GroundLocomotion.Standing")
local Crouching = require("Animation.Sekiro.Layer.GroundLocomotion.Crouching")
local Anim = require("Animation.Sekiro.AnimAssets").Locomotion
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local DirectionalPose = require("Animation.Sekiro.Shared.DirectionalPose")
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class GroundedMode: LuaAnimStateMachine
local GroundedMode = LuaAnimStateMachine:Extend("GroundedMode")

---把 Standing 与 Crouching Pose 合并为共享运动阶段的姿态选择节点。
---姿态变化只切换原生 BlendList 分支，不重进状态机 Entry，因此移动中的 Cycle 可以连续保留。
---@param Graph LuaAnimStateGraph 当前运动阶段的原生 Pose Graph。
---@param name string 姿态属性与选择节点使用的语义前缀。
---@param standing_pose LuaAnimNode Standing 构建器返回的 Pose 节点。
---@param crouching_pose LuaAnimNode Crouching 构建器返回的 Pose 节点。
---@return LuaBlendListByBoolNode selector Standing/Crouching 原生姿态选择节点。
local function select_stance(Graph, name, standing_pose, crouching_pose)
    local crouching = Graph:Property(name .. "Crouching", "bPoseCrouching")
    local selector = Graph:BlendListByBool(name .. "StanceSelector")
    selector.BlendTime = Tuning.StanceBlendDuration
    selector.FalsePose:Connect(standing_pose.Pose)
    selector.TruePose:Connect(crouching_pose.Pose)
    selector.ActiveValue:Connect(crouching.Value)
    return selector
end

---声明唯一的地面运动阶段状态机；步态和姿态变化不再创建状态跳转。
---@param Machine LuaStateMachineNode GroundedMode 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function GroundedMode.StateMachine(Machine)
    Machine.LayoutStyle = LayoutStyle.HierarchicalBlocks
    Machine:Entry("EntryRouter")
    Machine:State("EntryRouter")
    Machine:State("Idle")
    Machine:State("Turn")
    Machine:State("Start")
    Machine:State("Cycle")
    Machine:State("Stop")
    Machine:State("Step")

    Machine:Transition("EntryRouter_Step", "EntryRouter", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("EntryRouter_Cycle", "EntryRouter", "Cycle", {
        BlendDuration = Tuning.GaitBlendDuration,
        PriorityOrder = 1,
    })
    Machine:Transition("EntryRouter_Idle", "EntryRouter", "Idle", {
        BlendDuration = 0.0,
        PriorityOrder = 2,
    })
    Machine:Transition("Idle_Step", "Idle", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("Idle_Start", "Idle", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
    })
    Machine:Transition("Idle_Turn", "Idle", "Turn", {
        BlendDuration = Tuning.TurnBlendDuration,
        PriorityOrder = 2,
    })
    Machine:Transition("Turn_Step", "Turn", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("Turn_Start", "Turn", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
    })
    Machine:Transition("Turn_Idle", "Turn", "Idle", {
        BlendDuration = Tuning.TurnBlendDuration,
        PriorityOrder = 2,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanExitTurn, Tuning.CurveThreshold),
    })
    Machine:Transition("Start_Step", "Start", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("Start_Stop", "Start", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 1,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterStop, Tuning.CurveThreshold),
    })
    Machine:Transition("Start_Cycle", "Start", "Cycle", {
        BlendDuration = Tuning.CycleBlendDuration,
        PriorityOrder = 2,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterLoop, Tuning.CurveThreshold),
    })
    Machine:Transition("Cycle_Step", "Cycle", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 1,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterStop, Tuning.CurveThreshold),
    })
    Machine:Transition("Stop_Step", "Stop", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
    })
    Machine:Transition("Stop_Start", "Stop", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
    })
    Machine:Transition("Stop_Idle", "Stop", "Idle", {
        BlendDuration = Tuning.IdleBlendDuration,
        PriorityOrder = 2,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanEnterIdle, Tuning.CurveThreshold),
    })
    Machine:Transition("Step_Cycle", "Step", "Cycle", {
        BlendDuration = Tuning.GaitBlendDuration,
        PriorityOrder = 0,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanExitStep, Tuning.CurveThreshold),
    })
    Machine:Transition("Step_Idle", "Step", "Idle", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 1,
        Gate = Rule.CurveGreaterEqual(CurveNames.CanExitStep, Tuning.CurveThreshold),
    })
end

---构建入口分流的稳定待机姿势；通常只停留一个求值周期，随后恢复当前输入对应的阶段。
---@param Graph LuaAnimStateGraph EntryRouter 状态的原生 Pose Graph。
---@return nil result 姿态选择结果连接 State Result。
function GroundedMode.StateGraph_EntryRouter(Graph)
    local idle = select_stance(
        Graph,
        "EntryRouter",
        Standing.BuildIdle(Graph),
        Crouching.BuildIdle(Graph))
    Graph.Result:Connect(idle.Pose)
end

---构建共享 Idle，并在同一状态内选择 Standing 或 Crouching 循环。
---@param Graph LuaAnimStateGraph Idle 状态的原生 Pose Graph。
---@return nil result 姿态选择结果连接 State Result。
function GroundedMode.StateGraph_Idle(Graph)
    local idle = select_stance(Graph, "Idle", Standing.BuildIdle(Graph), Crouching.BuildIdle(Graph))
    Graph.Result:Connect(idle.Pose)
end

---构建共享 Turn，并按进入动作时锁存的方向与当前 Standing/Crouching 姿态选择一次性资产。
---@param Graph LuaAnimStateGraph Turn 状态的原生 Pose Graph。
---@return nil result 姿态选择结果连接 State Result。
function GroundedMode.StateGraph_Turn(Graph)
    local turn = select_stance(Graph, "Turn", Standing.BuildTurn(Graph), Crouching.BuildTurn(Graph))
    Graph.Result:Connect(turn.Pose)
end

---构建共享 Start；Standing/Crouching 均选择最近四向起步资产，再补齐锁定输入的量化残差。
---@param Graph LuaAnimStateGraph Start 状态的原生 Pose Graph。
---@return nil result 姿态选择结果连接 State Result。
function GroundedMode.StateGraph_Start(Graph)
    local start = select_stance(Graph, "Start", Standing.BuildStart(Graph), Crouching.BuildStart(Graph))
    local aligned = DirectionalPose.Align(
        Graph,
        "GroundedStartAlignment",
        start,
        "LatchedActionResidualAngle",
        "LatchedActionWarpingAlpha")
    Graph.Result:Connect(aligned.Pose)
end

---构建共享 Cycle；锁定 Standing/Crouching Walk/Run 都使用最近四向素材和连续残差对齐。
---@param Graph LuaAnimStateGraph Cycle 状态的原生 Pose Graph。
---@return nil result 方向扭曲并惯性化后的 Cycle 姿势连接 State Result。
function GroundedMode.StateGraph_Cycle(Graph)
    local cycle = select_stance(Graph, "Cycle", Standing.BuildCycle(Graph), Crouching.BuildCycle(Graph))
    local aligned = DirectionalPose.Align(
        Graph,
        "GroundedCycleAlignment",
        cycle,
        "DirectionResidualAngle",
        "LockOnWarpingAlpha")

    local inertialization = Graph:Inertialization("GroundedCycleInertialization")
    inertialization.Source:Connect(aligned.Pose)
    Graph.Result:Connect(inertialization.Pose)
end

---构建共享 Stop；步态、最近四向素材和量化残差都使用输入释放边沿的锁存值。
---@param Graph LuaAnimStateGraph Stop 状态的原生 Pose Graph。
---@return nil result 姿态选择结果连接 State Result。
function GroundedMode.StateGraph_Stop(Graph)
    local stop = select_stance(Graph, "Stop", Standing.BuildStop(Graph), Crouching.BuildStop(Graph))
    local aligned = DirectionalPose.Align(
        Graph,
        "GroundedStopAlignment",
        stop,
        "LatchedActionResidualAngle",
        "LatchedActionWarpingAlpha")
    Graph.Result:Connect(aligned.Pose)
end

---构建锁定方向的一次性 Step；非锁定移动由 Movement 负责把角色朝输入方向旋转。
---@param Graph LuaAnimStateGraph Step 状态的原生 Pose Graph。
---@return nil result 四方向 Step 选择姿势连接 State Result。
function GroundedMode.StateGraph_Step(Graph)
    local step = PoseSelectors.Cardinal(Graph, "Step", {
        Forward = Anim.Step_Forward,
        Back = Anim.Step_Back,
        Left = Anim.Step_Left,
        Right = Anim.Step_Right,
    }, "LatchedActionDirection", false, nil)
    Graph.Result:Connect(step.Pose)
end

---进入 Grounded 时若 Dodge 仍在生效，优先恢复 Step，避免移动分支抢占一次性动作。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_EntryRouter_Step(Inst)
    return Inst.bIsDodging == true
end

---进入 Grounded 时若方向输入已经存在，直接恢复 Cycle，不把落地前的旧输入误当作新起步。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否直接进入 Cycle。
function GroundedMode.CanEnter_EntryRouter_Cycle(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true
end

---进入 Grounded 时没有移动或闪避意图则进入 Idle；之后的新输入仍走正常 Idle 到 Start 路径。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Idle。
function GroundedMode.CanEnter_EntryRouter_Idle(Inst)
    return Inst.bHasMovementInput ~= true and Inst.bIsDodging ~= true
end

---Idle 中出现 Dodge 边沿时优先进入 Step，避免普通移动 Start 抢占一次性动作。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Idle_Step(Inst)
    return Inst.bIsDodging == true
end

---Idle 中出现有效移动输入时进入 Start；步态和姿态由 StateGraph 选择器决定。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Start。
function GroundedMode.CanEnter_Idle_Start(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true
end

---Idle 中没有移动或闪避，且 Lua 已检测到锁定偏航超过阈值时进入原地 Turn。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Turn。
function GroundedMode.CanEnter_Idle_Turn(Inst)
    return Inst.bTurnInPlaceRequested == true
        and Inst.bHasMovementInput ~= true
        and Inst.bIsDodging ~= true
end

---Turn 中出现 Dodge 边沿时立即进入 Step，不等待转向尾部曲线。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Turn_Step(Inst)
    return Inst.bIsDodging == true
end

---Turn 中出现移动输入时立即进入 Start，允许玩家打断原地转向。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Start。
function GroundedMode.CanEnter_Turn_Start(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true
end

---Turn 在 CanExitTurn 曲线窗口返回 Idle；若目标仍偏离，Idle 下一帧会重新请求下一次 Turn。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Idle。
function GroundedMode.CanEnter_Turn_Idle(Inst)
    return Inst.bHasMovementInput ~= true and Inst.bIsDodging ~= true
end

---Start 中出现新的 Dodge 边沿时允许 Step 打断起步动画。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Start_Step(Inst)
    return Inst.bIsDodging == true
end

---Start 期间释放移动输入时请求 Stop；资产曲线决定自然衔接窗口。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function GroundedMode.CanEnter_Start_Stop(Inst)
    return Inst.bHasMovementInput ~= true and Inst.bIsDodging ~= true
end

---Start 保持移动输入时请求 Cycle；CanEnterLoop 曲线负责选择脚步衔接点。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Cycle。
function GroundedMode.CanEnter_Start_Cycle(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true
end

---Cycle 中出现 Dodge 边沿时优先进入 Step。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Cycle_Step(Inst)
    return Inst.bIsDodging == true
end

---Cycle 只有在移动输入真正释放后才请求 Stop；Shift/Alt 变化不会离开 Cycle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否请求 Stop。
function GroundedMode.CanEnter_Cycle_Stop(Inst)
    return Inst.bHasMovementInput ~= true and Inst.bIsDodging ~= true
end

---Stop 中出现 Dodge 边沿时让一次性 Step 取得最高优先级。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Stop_Step(Inst)
    return Inst.bIsDodging == true
end

---Stop 期间重新出现移动输入时立即进入新 Start，允许停止动画被反向操作取消。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否重新进入 Start。
function GroundedMode.CanEnter_Stop_Start(Inst)
    return Inst.bHasMovementInput == true and Inst.bIsDodging ~= true
end

---Stop 没有新输入时在 CanEnterIdle 曲线窗口返回 Idle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Idle。
function GroundedMode.CanEnter_Stop_Idle(Inst)
    return Inst.bHasMovementInput ~= true and Inst.bIsDodging ~= true
end

---Step 结束且仍有移动输入时直接进入 Cycle，避免再次播放 Start 或 Sprint Start。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否直接进入 Cycle。
function GroundedMode.CanEnter_Step_Cycle(Inst)
    return Inst.bIsDodging ~= true and Inst.bHasMovementInput == true
end

---Step 结束且没有移动输入时返回当前姿态对应的 Idle。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Idle。
function GroundedMode.CanEnter_Step_Idle(Inst)
    return Inst.bIsDodging ~= true and Inst.bHasMovementInput ~= true
end

return GroundedMode
