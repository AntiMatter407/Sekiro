-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua；运行时规则只读取 AnimInstance 的显式战斗姿态属性。
-- 定义普通移动、地面防御与空中防御的基础姿态状态机；攻击、弹反和防御起落仍由全身 Slot 覆盖。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Rule = require("Animation.Compiler.TransitionRule")
local RootLocomotion = require("Animation.Sekiro.Layer.GroundLocomotion.Root")
local GuardPose = require("Animation.Sekiro.Layer.Combat.GuardPose")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SekiroCombatBasePose: LuaAnimStateMachine
local CombatBasePose = LuaAnimStateMachine:Extend("CombatBasePose")

---声明持续战斗姿态拓扑；Gameplay Lua 决定姿态枚举，原生规则只负责确定性的 Pose 过渡。
---@param Machine LuaStateMachineNode CombatBasePose 原生状态机节点。
---@return nil result 只声明状态与转换，不返回运行时对象。
function CombatBasePose.StateMachine(Machine)
    Machine.LayoutStyle = LayoutStyle.HierarchicalBlocks
    Machine:Entry("Normal")
    Machine:State("Normal")
    Machine:State("GuardGround")
    Machine:State("GuardAir")

    Machine:Transition("Normal_GuardGround", "Normal", "GuardGround", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsCombatGuardGroundPosture", true),
    })
    Machine:Transition("Normal_GuardAir", "Normal", "GuardAir", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.BoolProperty("bIsCombatGuardAirPosture", true),
    })
    Machine:Transition("GuardGround_GuardAir", "GuardGround", "GuardAir", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsCombatGuardAirPosture", true),
    })
    Machine:Transition("GuardGround_Normal", "GuardGround", "Normal", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bIsCombatGuardGroundPosture", false),
            Rule.BoolProperty("bIsCombatGuardAirPosture", false)),
    })
    Machine:Transition("GuardAir_GuardGround", "GuardAir", "GuardGround", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsCombatGuardGroundPosture", true),
    })
    Machine:Transition("GuardAir_Normal", "GuardAir", "Normal", {
        BlendDuration = Tuning.Combat.GuardPoseBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bIsCombatGuardGroundPosture", false),
            Rule.BoolProperty("bIsCombatGuardAirPosture", false)),
    })
end

---构建普通基础姿态；移动与跳跃仍由既有 RootLocomotion 原生状态机负责。
---@param Graph LuaAnimStateGraph Normal 状态独占的 Pose Graph。
---@return nil result RootLocomotion 经惯性化后连接到 State Result。
function CombatBasePose.StateGraph_Normal(Graph)
    local locomotion = Graph:StateMachine("RootLocomotion", RootLocomotion)
    local inertialization = Graph:Node(
        "LocomotionInertialization",
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
    inertialization.Source:Connect(locomotion.Pose)
    Graph.Result:Connect(inertialization.Pose)
end

---构建地面持续防御姿态；静止与移动选择留在状态内部，避免扩大顶层状态数量。
---@param Graph LuaAnimStateGraph GuardGround 状态独占的 Pose Graph。
---@return nil result 地面防御姿态连接到 State Result。
function CombatBasePose.StateGraph_GuardGround(Graph)
    local guard_ground = GuardPose.BuildGround(Graph)
    Graph.Result:Connect(guard_ground.Pose)
end

---构建空中持续防御姿态；空中轨迹仍由 CharacterMovement 驱动。
---@param Graph LuaAnimStateGraph GuardAir 状态独占的 Pose Graph。
---@return nil result 空中防御姿态连接到 State Result。
function CombatBasePose.StateGraph_GuardAir(Graph)
    local guard_air = GuardPose.BuildAir(Graph)
    Graph.Result:Connect(guard_air.Pose)
end

return CombatBasePose
