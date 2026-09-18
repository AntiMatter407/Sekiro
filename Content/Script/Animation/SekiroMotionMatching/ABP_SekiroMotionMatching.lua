-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua；运行时数据由 USKMotionMatchingAnimInstance 采集。
-- Motion Matching 新系统的独立顶层动画入口。
-- 当前直接生成原生 Motion Matching 节点；不复用 Classic 状态机、图层或运行时动作逻辑。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local EditorNodeClass = require(
    "Animation.Compiler.NodeClasses.EditorNodeClass")
local ReflectedNodeSpec = require(
    "Animation.Compiler.ReflectedNodeSpec")
local ReflectedDataType = require("Animation.Compiler.ReflectedDataType")
local IRValue = require("Animation.Compiler.IRValue")
local AnimAssets = require("Animation.SekiroMotionMatching.AnimAssets")
local SekiroMotionMatchingConfig = require(
    "Animation.SekiroMotionMatching.SekiroMotionMatchingConfig")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class ABP_SekiroMotionMatching: LuaAnimBlueprint
local ABP_SekiroMotionMatching = LuaAnimBlueprint:Extend(
    "ABP_SekiroMotionMatching",
    {
        SourceModule = "Animation.SekiroMotionMatching.ABP_SekiroMotionMatching",
        ParentAnimInstanceClass = "/Script/Sekiro.SKMotionMatchingAnimInstance",
        TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
        InheritedDefaults = {
            -- Chooser 是正式动画蓝图类默认配置；运行时只消费引用，不硬编码项目资产路径。
            LocomotionDatabaseChooser = IRValue.SoftObjectPath(
                SekiroMotionMatchingConfig.Chooser.AssetPath),
            -- 生命流程标签只暂停基础 Locomotion 搜索，不代表 FullBody RootMotion 已取得所有权。
            LocomotionBlockingTags = IRValue.Struct(
                '(GameplayTags=((TagName="State.Life.Dying"),'
                    .. '(TagName="State.Life.Dead"),'
                    .. '(TagName="State.Life.Reviving")))'),
            MotionMatchingFootIKGroundBlendInSpeed =
                IRValue.Float(
                    SekiroMotionMatchingConfig.FootIK.GroundBlendInSpeed),
            MotionMatchingFootIKAirBlendOutSpeed =
                IRValue.Float(
                    SekiroMotionMatchingConfig.FootIK.AirBlendOutSpeed),
        },
    })

---声明正式 Motion Matching 主图，动画候选数据的验收范围与图结构独立。
---Pose Search 数据库作为节点默认对象持久化；后续动态搜索请求通过 UE5.8 Anim Node Function 接入。
---FullBody Slot 与 Motion Matching 共同位于 Pose 门禁内；动作期间 Slot 会保持 Source 更新，
---避免退出 ActionOwned 后 Motion Matching 从已停更的节点恢复。History Collector 记录最终 Pose。
---其后视觉节点不反向污染搜索历史。
---@param graph LuaAnimGraph 基类创建的主 Pose Graph。
---@return nil result Motion Matching Pose 直接连接到主图输出。
function ABP_SekiroMotionMatching:AnimGraph(graph)
    -- 父类已从本次 QuerySnapshot 发布轨迹；运行时不在 Lua 中读取组件或重新预测。
    local trajectory = graph:ParentProperty(
        "MotionMatchingTrajectory",
        "MotionMatchingTrajectory",
        ReflectedDataType.MotionTrajectory)
    local pose_branch_enabled = graph:ParentProperty(
        "MotionMatchingPoseBranchEnabled",
        "bMotionMatchingPoseBranchEnabled",
        ReflectedDataType.Bool)
    local orientation_warping_angle = graph:ParentProperty(
        "MotionMatchingOrientationWarpingAngle",
        "MotionMatchingOrientationWarpingAngle",
        ReflectedDataType.Float)
    local orientation_warping_alpha = graph:ParentProperty(
        "MotionMatchingOrientationWarpingAlpha",
        "MotionMatchingOrientationWarpingAlpha",
        ReflectedDataType.Float)
    local foot_ik_alpha = graph:ParentProperty(
        "MotionMatchingFootIKAlpha",
        "MotionMatchingFootIKAlpha",
        ReflectedDataType.Float)
    local motion_matching = graph:ReflectedNode(
        "MotionMatching",
        ReflectedNodeSpec.MotionMatching,
        {
            -- 节点默认库只负责编辑器预览；运行时 Update Function 会按 Phase 用 Chooser 覆盖。
            Database = SekiroMotionMatchingConfig.Databases.Stationary.AssetPath,
            -- 启用节点内部 BlendStack，避免每次重搜直接硬切到新姿势。
            BlendTime = SekiroMotionMatchingConfig.Node.BlendTime,
            MaxActiveBlends = SekiroMotionMatchingConfig.Node.MaxActiveBlends,
            -- 空 BlendProfile 由全量新建节点的 null 默认值保证；当前使用全身统一混合。
            BlendOption = SekiroMotionMatchingConfig.Node.BlendOption,
            PoseJumpThresholdTime =
                SekiroMotionMatchingConfig.Node.PoseJumpThresholdTime,
            PoseReselectHistory = SekiroMotionMatchingConfig.Node.PoseReselectHistory,
            SearchThrottleTime = SekiroMotionMatchingConfig.Node.SearchThrottleTime,
            PlayRate = SekiroMotionMatchingConfig.Node.PlayRate,
            PlayRateMultiplier =
                SekiroMotionMatchingConfig.Node.PlayRateMultiplier,
            -- 禁用额外惯性化，避免与节点内部 Blend Stack 双重混合。
            bUseInertialBlend =
                SekiroMotionMatchingConfig.Node.bUseInertialBlend,
        })
    -- 搜索前持续应用尚未获得合法选择的请求；这不是状态确认，只决定本帧是否打断 Continuing Pose。
    motion_matching:BindFunction(
        ReflectedNodeSpec.MotionMatching.Functions.Update,
        "Update_MotionMatching_SearchRequest")
    -- UE5.8 的 StateUpdated 钩子是本方案的 PostSelection 采集点，结果通过 C++ 邮箱回到游戏线程。
    motion_matching:BindFunction(
        ReflectedNodeSpec.MotionMatching.Functions.StateUpdated,
        "Update_MotionMatching_PostSelection")
    local history_collector = graph:ReflectedNode(
        "PoseSearchHistoryCollector",
        ReflectedNodeSpec.PoseSearchHistoryCollector,
        {
            -- 使用项目显式构建的轨迹，禁止 Collector 再生成另一份预测。
            bGenerateTrajectory = false,
        })
    local safe_pose = graph:Node(
        "InvalidQueryIdle",
        EditorNodeClass.SequencePlayer,
        {
            Sequence = AnimAssets.Fallback.Idle,
            bLoopAnimation = true,
            PlayRate = 1.0,
        },
        "SequencePlayer")
    local pose_gate = graph:Node(
        "MotionMatchingPoseGate",
        EditorNodeClass.BlendListByBool,
        {
            -- Pose 可见性与搜索准入分离；无动作且没有合法搜索库时才回退安全 Idle。
            BlendTime = 0.0,
        },
        "BlendListByBool")
    local combat_full_body_slot = graph:Node(
        "CombatFullBodySlot",
        EditorNodeClass.Slot,
        {
            -- 与 SKCombatComponent 创建动态 Montage 时使用的 Slot 保持唯一同名边界。
            SlotName = Tuning.Combat.FullBodySlotName,
            -- FullBody 动作期间继续更新基础姿势，便于结束后恢复 Motion Matching。
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    local orientation_settings = SekiroMotionMatchingConfig.OrientationWarping
    local to_component = graph:Node(
        "MotionMatchingLocalToComponent",
        EditorNodeClass.LocalToComponentSpace,
        nil,
        "LocalToComponentSpace")
    local orientation_warping = graph:Node(
        "MotionMatchingOrientationWarping",
        EditorNodeClass.OrientationWarping,
        {
            -- Graph 模式会改写 UE5.8 RootMotion 属性；Manual 模式只消费 Coordinator 已施加的姿势残差。
            Mode = orientation_settings.Mode,
            SpineBones = orientation_settings.SpineBones,
            IKFootRootBone = orientation_settings.IKFootRootBone,
            IKFootBones = orientation_settings.IKFootBones,
            RotationAxis = orientation_settings.RotationAxis,
            DistributedBoneOrientationAlpha =
                orientation_settings.DistributedBoneOrientationAlpha,
            RotationInterpSpeed = orientation_settings.RotationInterpSpeed,
        },
        "OrientationWarping")
    local foot_ik_settings = SekiroMotionMatchingConfig.FootIK
    local foot_placement = graph:Node(
        "MotionMatchingFootPlacement",
        EditorNodeClass.FootPlacement,
        {
            IKFootRootBone = foot_ik_settings.IKFootRootBone,
            PelvisBone = foot_ik_settings.PelvisBone,
            LegDefinitions = foot_ik_settings.FootPlacementLegDefinitions,
            PlantSpeedMode = foot_ik_settings.PlantSpeedMode,
            PlantLockType = foot_ik_settings.PlantLockType,
            PelvisMaxOffset = foot_ik_settings.PelvisMaxOffset,
            PelvisHorizontalRebalancingWeight =
                foot_ik_settings.PelvisHorizontalRebalancingWeight,
            PlantSpeedThreshold = foot_ik_settings.PlantSpeedThreshold,
            PlantDistanceToGround = foot_ik_settings.PlantDistanceToGround,
            TraceStartOffset = foot_ik_settings.TraceStartOffset,
            TraceEndOffset = foot_ik_settings.TraceEndOffset,
            TraceSweepRadius = foot_ik_settings.TraceSweepRadius,
            TraceMaxGroundPenetration =
                foot_ik_settings.TraceMaxGroundPenetration,
            bTraceEnabled = true,
        },
        "FootPlacement")
    local leg_ik = graph:Node(
        "MotionMatchingLegIK",
        EditorNodeClass.LegIK,
        {
            LegDefinitions = foot_ik_settings.LegIKLegDefinitions,
            ReachPrecision = foot_ik_settings.ReachPrecision,
            MaxIterations = foot_ik_settings.MaxIterations,
        },
        "LegIK")
    local to_local = graph:Node(
        "MotionMatchingComponentToLocal",
        EditorNodeClass.ComponentToLocalSpace,
        nil,
        "ComponentToLocalSpace")

    -- UE5.8 通过 Collector 向搜索提供轨迹；不是 Motion Matching 节点的直接输入。
    history_collector.TransformTrajectory:Connect(trajectory.Value)
    combat_full_body_slot.Source:Connect(motion_matching.Pose)
    pose_gate.FalsePose:Connect(safe_pose.Pose)
    pose_gate.TruePose:Connect(combat_full_body_slot.Pose)
    pose_gate.ActiveValue:Connect(pose_branch_enabled.Value)
    history_collector.Source:Connect(pose_gate.Pose)
    to_component.LocalPose:Connect(history_collector.Pose)
    orientation_warping.ComponentPose:Connect(to_component.ComponentPose)
    orientation_warping.OrientationAngle:Connect(orientation_warping_angle.Value)
    orientation_warping.Alpha:Connect(orientation_warping_alpha.Value)
    foot_placement.ComponentPose:Connect(orientation_warping.Pose)
    foot_placement.Alpha:Connect(foot_ik_alpha.Value)
    leg_ik.ComponentPose:Connect(foot_placement.Pose)
    leg_ik.Alpha:Connect(foot_ik_alpha.Value)
    to_local.ComponentPose:Connect(leg_ik.Pose)

    graph.Result:Connect(to_local.Pose)
end

---接收生成动画蓝图的更新事件；Motion Matching 快照由原生 AnimInstance 生命周期统一采集。
---@param _Inst USKMotionMatchingAnimInstance 当前动画实例，本实现无需再次写入。
---@param _delta_seconds number 当前动画更新步长，本实现无需消费。
---@return nil result 本事件只消费 UE 更新回调，不返回业务数据。
function ABP_SekiroMotionMatching.BlueprintUpdateAnimation(
    _Inst,
    _delta_seconds)
end

return ABP_SekiroMotionMatching:Export()
