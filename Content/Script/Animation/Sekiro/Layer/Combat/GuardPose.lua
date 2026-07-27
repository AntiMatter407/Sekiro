-- Lua 类型：动画蓝图编译描述模块；编译对象是纯 Lua 表，运行时状态只通过 AnimInstance 属性读取。
-- 构建防御持续姿态：静止时使用 Guard Idle，移动时按当前四方向选择 Guard Move。

local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Anim = require("Animation.Sekiro.AnimAssets").Guard
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SekiroGuardPoseBuilder
local GuardPose = {}

local MoveAssets = {
    Forward = Anim.Move_Forward,
    Back = Anim.Move_Back,
    Left = Anim.Move_Left,
    Right = Anim.Move_Right,
}

---构建地面静止与移动持续防御姿态；该层不播放 Raise、Lower 或 Deflect 一次性动作。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@return LuaAnimNode pose_node 防御持续姿态的最终节点。
function GuardPose.BuildGround(Graph)
    local idle = PoseSelectors.Sequence(Graph, "GuardIdle", Anim.Idle, true, nil)
    local move = PoseSelectors.Cardinal(
        Graph,
        "GuardMove",
        MoveAssets,
        "CycleDirection",
        true,
        nil)
    local has_movement_input = Graph:Property("GuardHasMovementInput", "bCombatHasMovementInput")
    local selector = Graph:Node(
        "GuardIdleMoveSelector",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.Combat.GuardPoseBlendDuration },
        "BlendListByBool")
    selector.FalsePose:Connect(idle.Pose)
    selector.TruePose:Connect(move.Pose)
    selector.ActiveValue:Connect(has_movement_input.Value)
    return selector
end

---构建空中持续防御姿态；离散举刀和收刀动作仍由全身 Slot 独占。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@return LuaAnimNode pose_node 循环播放空中防御姿态的节点。
function GuardPose.BuildAir(Graph)
    local air_idle = PoseSelectors.Sequence(Graph, "GuardAirIdle", Anim.Air_Idle, true, nil)
    return air_idle
end

return GuardPose
