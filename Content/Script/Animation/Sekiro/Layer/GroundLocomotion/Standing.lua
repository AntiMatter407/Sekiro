-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Standing 姿态各运动阶段的 Pose 构建器。
-- 本模块只声明 Standing 资产选择节点，不拥有状态机；Idle/Start/Cycle/Stop 的时序由 GroundedMode 统一管理。

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local Anim = AnimAssets.Locomotion

---@class StandingPoseBuilder
local Standing = {}

local SprintStartAssets = {
    Forward = Anim.Sprint_Forward_Start,
    Back = Anim.Sprint_Back_Turn_Start,
    Left = Anim.Sprint_Left_Turn_Start,
    Right = Anim.Sprint_Right_Turn_Start,
}

local StartAssets = {
    Walk = {
        Forward = Anim.Walk_Forward_Start,
        Back = Anim.Walk_Back_Start,
        Left = Anim.Walk_Left_Start,
        Right = Anim.Walk_Right_Start,
    },
    Run = {
        Forward = Anim.Run_Forward_Start,
        Back = Anim.Run_Back_Start,
        Left = Anim.Run_Left_Start,
        Right = Anim.Run_Right_Start,
    },
    Sprint = SprintStartAssets,
}

local FreeStartAssets = {
    Walk = {
        Forward = Anim.Walk_Forward_Start,
        Back = Anim.IdleToWalk_Right_Turn,
        Left = Anim.IdleToWalk_Left_Turn,
        Right = Anim.IdleToWalk_Right_Turn,
    },
    Run = {
        Forward = Anim.Run_Forward_Start,
        Back = Anim.IdleToRun_Right_Turn,
        Left = Anim.IdleToRun_Left_Turn,
        Right = Anim.IdleToRun_Right_Turn,
    },
    Sprint = SprintStartAssets,
}

local CycleAssets = {
    Walk = {
        Forward = Anim.Walk_Forward_Loop,
        Back = Anim.Walk_Back_Loop,
        Left = Anim.Walk_Left_Loop,
        Right = Anim.Walk_Right_Loop,
    },
    Run = {
        Forward = Anim.Run_Forward_Loop,
        Back = Anim.Run_Back_Loop,
        Left = Anim.Run_Left_Loop,
        Right = Anim.Run_Right_Loop,
    },
    Sprint = Anim.Sprint_Forward_Loop,
}

local StopAssets = {
    Walk = {
        Forward = Anim.Walk_Forward_Stop,
        Back = Anim.Walk_Back_Stop,
        Left = Anim.Walk_Left_Stop,
        Right = Anim.Walk_Right_Stop,
    },
    Run = {
        Forward = Anim.Run_Forward_Stop,
        Back = Anim.Run_Back_Stop,
        Left = Anim.Run_Left_Stop,
        Right = Anim.Run_Right_Stop,
    },
    Sprint = {
        Forward = Anim.Sprint_Forward_Stop,
        Back = Anim.Sprint_Forward_Stop,
        Left = Anim.Sprint_Forward_Left_Turn_Stop,
        Right = Anim.Sprint_Forward_Right_Turn_Stop,
    },
}

---构建 Standing Idle 循环，供共享 Idle 状态的姿态选择器消费。
---@param Graph LuaAnimStateGraph Idle 状态的原生 Pose Graph。
---@return LuaSequencePlayerNode pose_node Standing Idle 姿势节点。
function Standing.BuildIdle(Graph)
    return PoseSelectors.Sequence(Graph, "StandingIdle", Anim.Idle, true, nil)
end

---构建 Standing 原地左右转身；方向在动作进入边沿锁存，避免播放中途翻转。
---@param Graph LuaAnimStateGraph Turn 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Standing Turn 最终姿势节点。
function Standing.BuildTurn(Graph)
    return PoseSelectors.LeftRight(
        Graph,
        "StandingTurn",
        Anim.Idle_Left_Turn,
        Anim.Idle_Right_Turn,
        "LatchedTurnDirection")
end

---构建 Standing Walk/Run/Sprint Start，并按进入动作时锁存的朝向模式选择转向资产。
---@param Graph LuaAnimStateGraph Start 状态的原生 Pose Graph。
---@return LuaBlendListByBoolNode pose_node Standing Start 最终姿势节点。
function Standing.BuildStart(Graph)
    local locked_start = PoseSelectors.WalkRunSprint(
        Graph,
        "StandingStart",
        StartAssets,
        "LatchedActionDirection",
        "LatchedActionGait",
        false,
        nil)
    local free_start = PoseSelectors.WalkRunSprint(
        Graph,
        "StandingFreeStart",
        FreeStartAssets,
        "LatchedFreeStartDirection",
        "LatchedActionGait",
        false,
        nil)
    local locked_on = Graph:Property("StandingStartLockedOn", "bLatchedActionLockedOn")
    local start = Graph:BlendListByBool("StandingStartFacingMode")
    start.BlendTime = Tuning.StartBlendDuration
    start.TruePose:Connect(locked_start.Pose)
    start.FalsePose:Connect(free_start.Pose)
    start.ActiveValue:Connect(locked_on.Value)
    return start
end

---构建 Standing Cycle；Sprint 作为步态分支直接与 Walk/Run 循环混合，不再拥有独立子状态机。
---@param Graph LuaAnimStateGraph Cycle 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Standing Cycle 步态选择节点。
function Standing.BuildCycle(Graph)
    return PoseSelectors.WalkRunSprint(
        Graph,
        "StandingCycle",
        CycleAssets,
        "CycleDirection",
        "PoseGait",
        true,
        Tuning.DirectionSyncGroup)
end

---构建 Standing Walk/Run/Sprint Stop，使用输入释放边沿锁存的步态和方向保持一次性动画稳定。
---@param Graph LuaAnimStateGraph Stop 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Standing Stop 最终姿势节点。
function Standing.BuildStop(Graph)
    return PoseSelectors.WalkRunSprint(
        Graph,
        "StandingStop",
        StopAssets,
        "LatchedActionDirection",
        "LatchedActionGait",
        false,
        nil)
end

return Standing
