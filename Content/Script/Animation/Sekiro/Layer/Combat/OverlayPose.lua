-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua，不持有运行时 UObject。
-- 在主 AnimGraph 中显式构建 Default、Sword、Guard、Combat 四条 ALS V4 风格 Overlay 分支。

local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local GuardPose = require("Animation.Sekiro.Layer.Combat.GuardPose")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local OverlayStateEnum = "/Script/Sekiro.ESKAnimOverlayState"

---@class SekiroOverlayPoseResult
---@field Pose LuaAnimNode 四条 Overlay 分支合成后的局部空间姿势。
---@field SourceCache LuaAnimNode 当前动画层输入姿势的缓存节点。
---@field DefaultPose LuaAnimNode 普通移动分支的缓存读取节点。
---@field SwordPose LuaAnimNode 持刀移动分支的缓存读取节点。
---@field GuardGroundPose LuaAnimNode 地面防御姿势节点。
---@field GuardAirPose LuaAnimNode 空中防御姿势节点。
---@field GuardAirCondition LuaAnimPropertyNode Guard Ground/Air 选择条件。
---@field GuardPose LuaAnimNode Guard Ground/Air 合成节点。
---@field CombatPose LuaAnimNode 全身战斗动作之下的基础姿势回退分支。
---@field OverlayState LuaAnimPropertyNode 当前 ESKAnimOverlayState 属性节点。
---@field Selector LuaAnimNode Default、Sword、Guard、Combat 枚举选择节点。

---@class SekiroOverlayPoseBuilder
local OverlayPose = {}

---从同一动画层输入姿势展开四条可独立替换的 Overlay 分支。
---Default、Sword 和 Combat 当前共享只狼基础移动姿势，但保持独立节点边界；
---Guard 使用专用持续姿势，攻击和弹反仍由选择器之后的全身 Slot 覆盖。
---@param graph LuaAnimGraph OverlayLayer 动画层函数图。
---@param source_pose LuaAnimPin Linked Input Pose 提供的局部空间姿势。
---@return SekiroOverlayPoseResult result 可供动画层连接和布局的 Overlay 构建结果。
function OverlayPose.Build(graph, source_pose)
    local source_cache = graph:Node(
        "OverlaySourcePose",
        EditorNodeClass.SaveCachedPose,
        {
            CacheName = "OverlaySourcePose",
        },
        "SaveCachedPose")
    source_cache.Pose:Connect(source_pose)

    local default_pose = graph:Node(
        "DefaultOverlayPose",
        EditorNodeClass.UseCachedPose,
        {
            CacheName = source_cache.Name,
        },
        "UseCachedPose")
    local sword_pose = graph:Node(
        "SwordOverlayPose",
        EditorNodeClass.UseCachedPose,
        {
            CacheName = source_cache.Name,
        },
        "UseCachedPose")
    local combat_pose = graph:Node(
        "CombatOverlayFallbackPose",
        EditorNodeClass.UseCachedPose,
        {
            CacheName = source_cache.Name,
        },
        "UseCachedPose")

    local guard_ground_pose = GuardPose.BuildGround(graph)
    local guard_air_pose = GuardPose.BuildAir(graph)
    local guard_air_condition = graph:Property("GuardOverlayInAir", "bOverlayInAir")
    local guard_pose = graph:Node(
        "GuardGroundAirOverlay",
        EditorNodeClass.BlendListByBool,
        {
            BlendTime = Tuning.Combat.GuardPoseBlendDuration,
        },
        "BlendListByBool")
    guard_pose.FalsePose:Connect(guard_ground_pose.Pose)
    guard_pose.TruePose:Connect(guard_air_pose.Pose)
    guard_pose.ActiveValue:Connect(guard_air_condition.Value)

    local overlay_state = graph:Property("ActiveOverlayState", "PoseOverlayState")
    local selector = graph:Node(
        "ALSOverlayStateSelector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = OverlayStateEnum,
            EnumEntries = "Default|Sword|Guard|Combat",
            BlendTime = Tuning.Combat.GuardPoseBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(default_pose.Pose)
    selector.Pose0:Connect(default_pose.Pose)
    selector.Pose1:Connect(sword_pose.Pose)
    selector.Pose2:Connect(guard_pose.Pose)
    selector.Pose3:Connect(combat_pose.Pose)
    selector.ActiveValue:Connect(overlay_state.Value)

    return {
        Pose = selector,
        SourceCache = source_cache,
        DefaultPose = default_pose,
        SwordPose = sword_pose,
        GuardGroundPose = guard_ground_pose,
        GuardAirPose = guard_air_pose,
        GuardAirCondition = guard_air_condition,
        GuardPose = guard_pose,
        CombatPose = combat_pose,
        OverlayState = overlay_state,
        Selector = selector,
    }
end

return OverlayPose
