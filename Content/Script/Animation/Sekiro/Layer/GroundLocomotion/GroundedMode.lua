-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua；Transition 规则生成 UE 原生属性与 Gate 节点。
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

    -- Dodge 优先恢复 Step，避免已有移动输入抢占一次性动作。
    Machine:Transition("EntryRouter_Step", "EntryRouter", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- 进入 Grounded 时已经有移动输入则直接恢复 Cycle，不重播 Start。
    Machine:Transition("EntryRouter_Cycle", "EntryRouter", "Cycle", {
        BlendDuration = Tuning.GaitBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- 没有移动或闪避意图时进入稳定 Idle。
    Machine:Transition("EntryRouter_Idle", "EntryRouter", "Idle", {
        BlendDuration = 0.0,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- Idle 中 Dodge 优先进入 Step。
    Machine:Transition("Idle_Step", "Idle", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- Idle 中出现有效移动输入时进入 Start。
    Machine:Transition("Idle_Start", "Idle", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- 锁定待机偏航超过阈值时进入原地 Turn。
    Machine:Transition("Idle_Turn", "Idle", "Turn", {
        BlendDuration = Tuning.TurnBlendDuration,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bTurnInPlaceRequested", true),
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- Turn 中 Dodge 可以立即打断到 Step。
    Machine:Transition("Turn_Step", "Turn", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- Turn 中出现移动输入时立即打断到 Start。
    Machine:Transition("Turn_Start", "Turn", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- Turn 在退出曲线窗口内且没有新输入时返回 Idle。
    Machine:Transition("Turn_Idle", "Turn", "Idle", {
        BlendDuration = Tuning.TurnBlendDuration,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false),
            Rule.CurveGreaterEqual(CurveNames.CanExitTurn, Tuning.CurveThreshold)),
    })
    -- Start 中新的 Dodge 边沿立即打断到 Step。
    Machine:Transition("Start_Step", "Start", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- Start 期间释放移动输入后，在资产曲线窗口请求 Stop。
    Machine:Transition("Start_Stop", "Start", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false),
            Rule.CurveGreaterEqual(CurveNames.CanEnterStop, Tuning.CurveThreshold)),
    })
    -- Start 保持移动输入时，在循环曲线窗口进入 Cycle。
    Machine:Transition("Start_Cycle", "Start", "Cycle", {
        BlendDuration = Tuning.CycleBlendDuration,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.BoolProperty("bIsDodging", false),
            Rule.CurveGreaterEqual(CurveNames.CanEnterLoop, Tuning.CurveThreshold)),
    })
    -- Cycle 中 Dodge 优先进入 Step。
    Machine:Transition("Cycle_Step", "Cycle", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- Cycle 仅在移动输入释放后，于停止曲线窗口进入 Stop。
    Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
        BlendDuration = Tuning.StopBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false),
            Rule.CurveGreaterEqual(CurveNames.CanEnterStop, Tuning.CurveThreshold)),
    })
    -- Stop 中 Dodge 取得最高优先级。
    Machine:Transition("Stop_Step", "Stop", "Step", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsDodging", true),
    })
    -- Stop 期间重新出现移动输入时立即进入新 Start。
    Machine:Transition("Stop_Start", "Stop", "Start", {
        BlendDuration = Tuning.StartBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.BoolProperty("bIsDodging", false)),
    })
    -- Stop 没有新输入时，在待机曲线窗口返回 Idle。
    Machine:Transition("Stop_Idle", "Stop", "Idle", {
        BlendDuration = Tuning.IdleBlendDuration,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.BoolProperty("bIsDodging", false),
            Rule.CurveGreaterEqual(CurveNames.CanEnterIdle, Tuning.CurveThreshold)),
    })
    -- Step 结束且仍有移动输入时直接进入 Cycle，不再次播放 Start。
    Machine:Transition("Step_Cycle", "Step", "Cycle", {
        BlendDuration = Tuning.GaitBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.All(
            Rule.BoolProperty("bIsDodging", false),
            Rule.BoolProperty("bHasMovementInput", true),
            Rule.CurveGreaterEqual(CurveNames.CanExitStep, Tuning.CurveThreshold)),
    })
    -- Step 结束且没有移动输入时返回当前姿态对应的 Idle。
    Machine:Transition("Step_Idle", "Step", "Idle", {
        BlendDuration = Tuning.StepBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bIsDodging", false),
            Rule.BoolProperty("bHasMovementInput", false),
            Rule.CurveGreaterEqual(CurveNames.CanExitStep, Tuning.CurveThreshold)),
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
        "StartDirectionResidualAngle",
        "StartWarpingAlpha")
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

return GroundedMode
