-- Sekiro 地面移动模式状态机。
-- Standing 与 Crouching 各自拥有完整子状态机；姿态切换发生在该层，不会把两个资产集合混进同一 StateGraph。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local Rule = require("Animation.Compiler.TransitionRule")
local StandingLocomotion = require("Animation.Sekiro.Layer.GroundLocomotion.Standing")
local CrouchingLocomotion = require("Animation.Sekiro.Layer.GroundLocomotion.Crouching")
local SprintLocomotion = require("Animation.Sekiro.Layer.GroundLocomotion.Sprint")
local Anim = require("Animation.Sekiro.AnimAssets").Locomotion
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class GroundedMode: LuaAnimStateMachine
local GroundedMode = LuaAnimStateMachine:Extend("GroundedMode")

---声明 Standing/Crouching 两个并列状态；Step 与 Sprint 后续按更高优先级加入同一层。
---@param Machine LuaStateMachineNode GroundedMode 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function GroundedMode.StateMachine(Machine)
    Machine:Entry("Standing")
    Machine:State("Standing")
    Machine:State("Crouching")
    Machine:State("Step")
    Machine:State("Sprint")
    Machine:Transition("Standing_Step", "Standing", "Step", { BlendDuration = Tuning.StepBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Standing_Sprint", "Standing", "Sprint", { BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 1 })
    Machine:Transition("Standing_Crouching", "Standing", "Crouching", { BlendDuration = 0.12, PriorityOrder = 2 })
    Machine:Transition("Crouching_Step", "Crouching", "Step", { BlendDuration = Tuning.StepBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Crouching_Sprint", "Crouching", "Sprint", { BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 1 })
    Machine:Transition("Crouching_Standing", "Crouching", "Standing", { BlendDuration = 0.12, PriorityOrder = 2 })
    Machine:Transition("Step_Sprint", "Step", "Sprint", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 0,
        Gate = Rule.TimeRemainingLessEqual(Tuning.SprintBlendDuration),
    })
    Machine:Transition("Step_Standing", "Step", "Standing", {
        BlendDuration = Tuning.StepBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.StepBlendDuration),
    })
    Machine:Transition("Step_Crouching", "Step", "Crouching", {
        BlendDuration = Tuning.StepBlendDuration, PriorityOrder = 2,
        Gate = Rule.TimeRemainingLessEqual(Tuning.StepBlendDuration),
    })
    Machine:Transition("Sprint_Step", "Sprint", "Step", { BlendDuration = Tuning.StepBlendDuration, PriorityOrder = 0 })
    Machine:Transition("Sprint_Standing", "Sprint", "Standing", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 1,
        Gate = Rule.TimeRemainingLessEqual(Tuning.SprintBlendDuration),
    })
    Machine:Transition("Sprint_Crouching", "Sprint", "Crouching", {
        BlendDuration = Tuning.SprintBlendDuration, PriorityOrder = 2,
        Gate = Rule.TimeRemainingLessEqual(Tuning.SprintBlendDuration),
    })
end

---在 Standing 状态放置站立 Locomotion 子状态机。
---@param Graph LuaAnimStateGraph Standing 状态 Pose Graph。
---@return nil result 子状态机姿势连接 State Result。
function GroundedMode.StateGraph_Standing(Graph)
    local standing = Graph:StateMachine("StandingLocomotion", StandingLocomotion)
    Graph.Result:Connect(standing.Pose)
end

---在 Crouching 状态放置蹲姿 Locomotion 子状态机。
---@param Graph LuaAnimStateGraph Crouching 状态 Pose Graph。
---@return nil result 子状态机姿势连接 State Result。
function GroundedMode.StateGraph_Crouching(Graph)
    local crouching = Graph:StateMachine("CrouchingLocomotion", CrouchingLocomotion)
    Graph.Result:Connect(crouching.Pose)
end

---构建锁定方向的一次性 Step；非锁定模式在更新入口把锁存方向固定为 Forward。
---@param Graph LuaAnimStateGraph Step 状态 Pose Graph。
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

---在 Sprint 状态放置独立 Sprint Start/Cycle/Stop 子状态机。
---@param Graph LuaAnimStateGraph Sprint 状态 Pose Graph。
---@return nil result Sprint 子状态机姿势连接 State Result。
function GroundedMode.StateGraph_Sprint(Graph)
    local sprint = Graph:StateMachine("SprintLocomotion", SprintLocomotion)
    Graph.Result:Connect(sprint.Pose)
end

---Standing 中 DodgeActive 生效时优先进入 Step。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Standing_Step(Inst)
    return Inst.bIsDodging == true
end


---Standing 中保持移动且长按达到 Sprint 意图时进入 Sprint。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Sprint。
function GroundedMode.CanEnter_Standing_Sprint(Inst)
    return Inst.bIsDodging ~= true and Inst.bHasMovementInput == true and Inst.DesiredGait == UE.ESKAnimGait.Sprint
end

---AnimInstance 进入蹲姿时切换到 Crouching 子状态机。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否切换到 Crouching。
function GroundedMode.CanEnter_Standing_Crouching(Inst)
    return Inst.bIsCrouching == true and Inst.bIsDodging ~= true
end


---Crouching 中 DodgeActive 生效时先由角色逻辑起身，再进入站立 Step 资产。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Crouching_Step(Inst)
    return Inst.bIsDodging == true
end


---蹲姿长按达到 Sprint 意图时由外层切换到站立 Sprint 分支。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Sprint。
function GroundedMode.CanEnter_Crouching_Sprint(Inst)
    return Inst.bIsDodging ~= true and Inst.bHasMovementInput == true and Inst.DesiredGait == UE.ESKAnimGait.Sprint
end

---AnimInstance 离开蹲姿时切换回 Standing 子状态机。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否切换到 Standing。
function GroundedMode.CanEnter_Crouching_Standing(Inst)
    return Inst.bIsCrouching ~= true and Inst.bIsDodging ~= true
end

---Step 播放结束且当前为站姿时返回 Standing；新移动输入由内部状态机继续处理。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Standing。
function GroundedMode.CanEnter_Step_Standing(Inst)
    return Inst.bIsDodging ~= true and Inst.bIsCrouching ~= true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---Step 播放结束且仍为蹲姿时返回 Crouching。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Crouching。
function GroundedMode.CanEnter_Step_Crouching(Inst)
    return Inst.bIsDodging ~= true and Inst.bIsCrouching == true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---Step 尾部仍保持 Sprint 意图时进入 Sprint Start。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否由 Step 升级为 Sprint。
function GroundedMode.CanEnter_Step_Sprint(Inst)
    return Inst.bIsDodging ~= true and Inst.bHasMovementInput == true and Inst.DesiredGait == UE.ESKAnimGait.Sprint
end

---Sprint 中新的 DodgeActive 边沿优先切入 Step。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否进入 Step。
function GroundedMode.CanEnter_Sprint_Step(Inst)
    return Inst.bIsDodging == true
end

---Sprint 意图结束且为站姿时，在 Sprint Stop 尾部返回 Standing。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Standing。
function GroundedMode.CanEnter_Sprint_Standing(Inst)
    return Inst.bIsDodging ~= true and Inst.bIsCrouching ~= true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

---Sprint 意图结束且恢复蹲姿时，在 Stop 尾部返回 Crouching。
---@param Inst userdata 当前生成动画实例的 UnLua 代理。
---@return boolean can_enter 是否返回 Crouching。
function GroundedMode.CanEnter_Sprint_Crouching(Inst)
    return Inst.bIsDodging ~= true and Inst.bIsCrouching == true and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
end

return GroundedMode
