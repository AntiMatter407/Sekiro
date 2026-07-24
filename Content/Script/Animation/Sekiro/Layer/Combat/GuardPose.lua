-- Lua 类型：动画蓝图编译描述模块；编译对象是纯 Lua 表，运行时状态只通过 AnimInstance 属性读取。
-- 构建防御持续姿态：静止时使用 Guard Idle，移动时按当前四方向选择 Guard Move。

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

---构建地面静止/移动与空中持续防御姿态选择；该层不播放 Raise、Lower 或 Deflect 一次性动作。
---@param Graph LuaAnimGraph 根动画图。
---@return LuaBlendListByBoolNode pose_node 防御持续姿态的最终节点。
function GuardPose.Build(Graph)
    local idle = PoseSelectors.Sequence(Graph, "GuardIdle", Anim.Idle, true, nil)
    local move = PoseSelectors.Cardinal(
        Graph,
        "GuardMove",
        MoveAssets,
        "CycleDirection",
        true,
        nil)
    local has_movement_input = Graph:Property("GuardHasMovementInput", "bCombatHasMovementInput")
    local selector = Graph:BlendListByBool("GuardIdleMoveSelector")
    selector.BlendTime = Tuning.Combat.GuardPoseBlendDuration
    selector.FalsePose:Connect(idle.Pose)
    selector.TruePose:Connect(move.Pose)
    selector.ActiveValue:Connect(has_movement_input.Value)

    local air_idle = PoseSelectors.Sequence(Graph, "GuardAirIdle", Anim.Air_Idle, true, nil)
    local is_in_air = Graph:Property("GuardIsInAir", "bIsInAir")
    local air_selector = Graph:BlendListByBool("GuardGroundAirSelector")
    air_selector.BlendTime = Tuning.Combat.GuardPoseBlendDuration
    air_selector.FalsePose:Connect(selector.Pose)
    air_selector.TruePose:Connect(air_idle.Pose)
    air_selector.ActiveValue:Connect(is_in_air.Value)
    return air_selector
end

return GuardPose
