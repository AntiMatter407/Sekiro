-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Crouching 姿态各运动阶段的 Pose 构建器。
-- 本模块与 Standing 分别维护资产表，但不重复 Idle/Start/Cycle/Stop 状态机和 Transition 规则。

local Anim = require("Animation.Sekiro.AnimAssets").Locomotion
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class CrouchingPoseBuilder
local Crouching = {}

local StartAssets = {
    Walk = {
        Forward = Anim.Crouch_Walk_Forward_Start,
        Back = Anim.Crouch_Walk_Back_Start,
        Left = Anim.Crouch_Walk_Left_Start,
        Right = Anim.Crouch_Walk_Right_Start,
    },
    Run = {
        Forward = Anim.Crouch_Run_Forward_Start,
        Back = Anim.Crouch_Run_Back_Start,
        Left = Anim.Crouch_Run_Left_Start,
        Right = Anim.Crouch_Run_Right_Start,
    },
}

local CycleAssets = {
    Walk = {
        Forward = Anim.Crouch_Walk_Forward_Loop,
        Back = Anim.Crouch_Walk_Back_Loop,
        Left = Anim.Crouch_Walk_Left_Loop,
        Right = Anim.Crouch_Walk_Right_Loop,
    },
    Run = {
        Forward = Anim.Crouch_Run_Forward_Loop,
        Back = Anim.Crouch_Run_Back_Loop,
        Left = Anim.Crouch_Run_Left_Loop,
        Right = Anim.Crouch_Run_Right_Loop,
    },
}

local StopAssets = {
    Walk = {
        Forward = Anim.Crouch_Walk_Forward_Stop,
        Back = Anim.Crouch_Walk_Back_Stop,
        Left = Anim.Crouch_Walk_Left_Stop,
        Right = Anim.Crouch_Walk_Right_Stop,
    },
    Run = {
        Forward = Anim.Crouch_Run_Forward_Stop,
        Back = Anim.Crouch_Run_Back_Stop,
        Left = Anim.Crouch_Run_Left_Stop,
        Right = Anim.Crouch_Run_Right_Stop,
    },
}

---构建 Crouching Idle 循环，供共享 Idle 状态的姿态选择器消费。
---@param Graph LuaAnimStateGraph Idle 状态的原生 Pose Graph。
---@return LuaSequencePlayerNode pose_node Crouching Idle 姿势节点。
function Crouching.BuildIdle(Graph)
    return PoseSelectors.Sequence(Graph, "CrouchingIdle", Anim.Crouch_Idle, true, nil)
end

---构建 Crouching 原地左右转身；与 Standing 共用锁存方向和退出曲线语义。
---@param Graph LuaAnimStateGraph Turn 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Crouching Turn 最终姿势节点。
function Crouching.BuildTurn(Graph)
    return PoseSelectors.LeftRight(
        Graph,
        "CrouchingTurn",
        Anim.Crouch_Idle_Left_Turn,
        Anim.Crouch_Idle_Right_Turn,
        "LatchedTurnDirection")
end

---构建 Crouching Walk/Run Start；Sprint 由 Movement 先退出蹲姿后在 Standing 分支表现。
---@param Graph LuaAnimStateGraph Start 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Crouching Start 姿势节点。
function Crouching.BuildStart(Graph)
    return PoseSelectors.WalkRun(
        Graph,
        "CrouchingStart",
        StartAssets,
        "LatchedActionDirection",
        "LatchedActionGait",
        false,
        nil)
end

---构建 Crouching Walk/Run Cycle，并与 Standing Cycle 共用方向和同步组。
---@param Graph LuaAnimStateGraph Cycle 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Crouching Cycle 姿势节点。
function Crouching.BuildCycle(Graph)
    return PoseSelectors.WalkRun(
        Graph,
        "CrouchingCycle",
        CycleAssets,
        "CycleDirection",
        "PoseGait",
        true,
        Tuning.DirectionSyncGroup)
end

---构建 Crouching Walk/Run Stop，使用动作边沿锁存的方向和步态。
---@param Graph LuaAnimStateGraph Stop 状态的原生 Pose Graph。
---@return LuaBlendListByEnumNode pose_node Crouching Stop 姿势节点。
function Crouching.BuildStop(Graph)
    return PoseSelectors.WalkRun(
        Graph,
        "CrouchingStop",
        StopAssets,
        "LatchedActionDirection",
        "LatchedActionGait",
        false,
        nil)
end

return Crouching
