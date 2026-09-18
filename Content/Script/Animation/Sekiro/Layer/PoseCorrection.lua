-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua，不持有运行时 UObject。
-- 统一构建最终 Component Space 姿势修正链，避免战斗分层与脚部求解散落在主 AnimGraph。

local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SekiroPoseCorrectionResult
---@field Pose LuaAnimNode 已恢复到 Local Space 的最终姿势节点。
---@field ToComponent LuaAnimNode Local To Component Space 节点。
---@field WeaponHandIK LuaAnimNode 收拔刀右手约束节点。
---@field FootPlacement LuaAnimNode 双脚地面检测与骨盆修正节点。
---@field FootIKAlpha LuaAnimPropertyNode Foot Placement 与 Leg IK 共用的权重属性。
---@field LegIK LuaAnimNode 双腿末端求解节点。
---@field ToLocal LuaAnimNode Component To Local Space 节点。

---@class SekiroPoseCorrectionBuilder
local PoseCorrection = {}

---在动作分层完成后构建统一的骨骼修正链。
---Lean 数据已由 USKAnimInstance 发布；当前没有匹配的只狼 Additive 资产和 Apply Additive 编译契约，
---因此本层只保留明确的接入边界，不生成无消费者变量或伪 Lean 节点。
---@param graph LuaAnimGraph FootIK 动画层函数图。
---@param source_pose LuaAnimPin 已完成 Overlay、Slot 和全身动作合成的 Local Space 输入姿势。
---@return SekiroPoseCorrectionResult result 可供主图连接和布局的修正链结果。
function PoseCorrection.Build(graph, source_pose)
    local foot_ik = Tuning.FootIK
    local weapon_ik = Tuning.WeaponIK

    local to_component = graph:Node(
        "PoseCorrectionLocalToComponent",
        EditorNodeClass.LocalToComponentSpace,
        nil,
        "LocalToComponentSpace")
    to_component.LocalPose:Connect(source_pose)

    local weapon_hand_ik = graph:Node(
        "WeaponHandIK",
        EditorNodeClass.TwoBoneIK,
        {
            IKBone = weapon_ik.IKBone,
            EffectorLocationSpace = UE.EBoneControlSpace.BCS_BoneSpace,
            EffectorTargetSocketName = weapon_ik.EffectorSocket,
            JointTargetLocationSpace = UE.EBoneControlSpace.BCS_BoneSpace,
            JointTargetBoneName = weapon_ik.JointTargetBone,
            bTakeRotationFromEffectorSpace = true,
            bAllowStretching = false,
            AlphaInputType = UE.EAnimAlphaInputType.Curve,
            AlphaCurveName = CurveNames.WeaponHandIK,
        },
        "TwoBoneIK")
    weapon_hand_ik.ComponentPose:Connect(to_component.ComponentPose)

    local foot_placement = graph:Node(
        "GroundedFootPlacement",
        EditorNodeClass.FootPlacement,
        {
            IKFootRootBone = foot_ik.IKFootRootBone,
            PelvisBone = foot_ik.PelvisBone,
            LegDefinitions = foot_ik.FootPlacementLegDefinitions,
            PlantSpeedMode = foot_ik.PlantSpeedMode,
            PlantLockType = foot_ik.PlantLockType,
            PelvisMaxOffset = foot_ik.PelvisMaxOffset,
            PelvisHorizontalRebalancingWeight = foot_ik.PelvisHorizontalRebalancingWeight,
            PlantSpeedThreshold = foot_ik.PlantSpeedThreshold,
            PlantDistanceToGround = foot_ik.PlantDistanceToGround,
            TraceStartOffset = foot_ik.TraceStartOffset,
            TraceEndOffset = foot_ik.TraceEndOffset,
            TraceSweepRadius = foot_ik.TraceSweepRadius,
            TraceMaxGroundPenetration = foot_ik.TraceMaxGroundPenetration,
            bTraceEnabled = true,
        },
        "FootPlacement")
    foot_placement.ComponentPose:Connect(weapon_hand_ik.Pose)

    -- 两个地面求解器必须共用同一权重；空中或全身动作期间同步淡出，防止 Leg IK 拉回旧目标。
    local foot_ik_alpha = graph:Property("GroundedFootIKAlpha", "FootIKAlpha")
    foot_placement.Alpha:Connect(foot_ik_alpha.Value)

    local leg_ik = graph:Node(
        "GroundedDualLegIK",
        EditorNodeClass.LegIK,
        {
            LegDefinitions = foot_ik.LegIKLegDefinitions,
            ReachPrecision = foot_ik.ReachPrecision,
            MaxIterations = foot_ik.MaxIterations,
        },
        "LegIK")
    leg_ik.ComponentPose:Connect(foot_placement.Pose)
    leg_ik.Alpha:Connect(foot_ik_alpha.Value)

    local to_local = graph:Node(
        "PoseCorrectionComponentToLocal",
        EditorNodeClass.ComponentToLocalSpace,
        nil,
        "ComponentToLocalSpace")
    to_local.ComponentPose:Connect(leg_ik.Pose)

    return {
        Pose = to_local,
        ToComponent = to_component,
        WeaponHandIK = weapon_hand_ik,
        FootPlacement = foot_placement,
        FootIKAlpha = foot_ik_alpha,
        LegIK = leg_ik,
        ToLocal = to_local,
    }
end

return PoseCorrection
