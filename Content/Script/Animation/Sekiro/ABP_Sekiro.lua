-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Sekiro 新 Lua 动画蓝图入口。
-- Lua 在编译期声明原生 Graph，在游戏线程更新生成变量；Pose、状态时间、混合和 RootMotion 均由 UE 原生节点执行。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local AnimAssets = require("Animation.Sekiro.AnimAssets")
local CombatBasePose = require("Animation.Sekiro.Layer.Combat.CombatBasePose")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local Direction = require("Animation.Sekiro.Shared.Direction")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local DirectionEnum = "/Script/Sekiro.ESKLocomotionDirection"
local GaitEnum = "/Script/Sekiro.ESKAnimGait"
local RootMotionMode = {
    Ignore = 1,
    Everything = 2,
}

---@class ABP_Sekiro: LuaAnimBlueprint
local ABP_Sekiro = LuaAnimBlueprint:Extend("ABP_Sekiro", {
    SourceModule = "Animation.Sekiro.ABP_Sekiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
})

---把 UE Sequence Player 报告的原生动画短名解析为 Lua 资源表中的语义名称。
---快照调试器只在采样时调用该纯查询接口；未知资产返回 nil 并保留原生名称。
---@param native_asset_name string 当前实际播放的动画 UObject 短名。
---@return string|nil lua_asset_name 对应的 AnimAssets 分组路径；未登记时返回 nil。
function ABP_Sekiro.ResolveDebugAnimationName(native_asset_name)
    return AnimAssets.GetLuaAssetName(native_asset_name)
end

---把输入目标步态转换为当前姿态实际具备的 Pose 分支。
---Standing 保留 Sprint，Crouching 只允许 Walk/Run；Idle 在有移动输入的异常帧回退为 Run，避免空枚举分支。
---@param gait userdata|number 当前 DesiredGait 枚举值。
---@param crouching boolean 当前是否选择 Crouching 姿态。
---@return userdata|number pose_gait 可安全写入 PoseGait 或 LatchedActionGait 的枚举值。
local function normalize_pose_gait(gait, crouching)
    if gait == UE.ESKAnimGait.Walk then
        return UE.ESKAnimGait.Walk
    end
    if gait == UE.ESKAnimGait.Sprint and crouching ~= true then
        return UE.ESKAnimGait.Sprint
    end
    return UE.ESKAnimGait.Run
end

---把方向量化残差限制在指定范围，防止滞回区间或异常输入使骨骼过度扭转。
---@param angle number|nil 未限制的有符号角度，单位为度。
---@param max_angle number 允许的绝对值上限，单位为度。
---@return number clamped_angle 限制后的有符号角度。
local function clamp_direction_residual(angle, max_angle)
    return math.max(-max_angle, math.min(angle or 0, max_angle))
end

---以固定的每秒速率把数值移动到目标，避免 Foot IK 权重受帧率影响或在落地帧突变。
---@param current number 当前值。
---@param target number 目标值。
---@param speed number 每秒最多变化量。
---@param delta_seconds number 本帧时长，单位为秒。
---@return number result 移动后的值。
local function move_towards(current, target, speed, delta_seconds)
    local max_delta = math.max(speed or 0.0, 0.0) * math.max(delta_seconds or 0.0, 0.0)
    if current < target then
        return math.min(current + max_delta, target)
    end
    return math.max(current - max_delta, target)
end

---声明本动画蓝图运行时需要的真实 GeneratedClass 变量。
---@return nil result 变量会随 IR 生成到 UAnimBlueprint GeneratedClass。
function ABP_Sekiro:DeclareVariables()
    self:Variable("CycleDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("LatchedActionDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("LatchedFreeStartDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("LatchedTurnDirection", "Enum", Direction.Cardinal.Right, DirectionEnum)
    self:Variable("PoseGait", "Enum", 2, GaitEnum)
    self:Variable("LatchedActionGait", "Enum", 2, GaitEnum)
    self:Variable("bPoseCrouching", "Bool", false)
    self:Variable("LockOnLocomotionAngle", "Float", 0.0)
    self:Variable("LatchedLockOnLocomotionAngle", "Float", 0.0)
    self:Variable("LockOnOrientationWarpingAlpha", "Float", 0.0)
    self:Variable("StopOrientationWarpingAlpha", "Float", 0.0)
    self:Variable("StepOrientationWarpingAlpha", "Float", 0.0)
    self:Variable("StopTurnDirection", "Enum", Direction.Cardinal.Right, DirectionEnum)
    self:Variable("bStopTurnRequested", "Bool", false)
    -- 预留锁定模式切换边沿；当前只记录上一帧状态，尚无 Graph 或规则消费者。
    self:Variable("bWasLockedOn", "Bool", false)
    self:Variable("bLatchedActionLockedOn", "Bool", false)
    self:Variable("bHadMovementInput", "Bool", false)
    self:Variable("bWasDodging", "Bool", false)
    self:Variable("bWasSprintRequested", "Bool", false)
    self:Variable("bTurnInPlaceRequested", "Bool", false)
    self:Variable("bWasTurnInPlaceRequested", "Bool", false)
    self:Variable("JumpDirection", "Enum", Direction.Octant.Forward, DirectionEnum)
    self:Variable("JumpDirectionResidualAngle", "Float", 0.0)
    self:Variable("JumpWarpingAlpha", "Float", 0.0)
    self:Variable("bDirectionalJump", "Bool", false)
    self:Variable("bJumpStartedLockedOn", "Bool", false)
    self:Variable("bJumpStartedCrouchedPose", "Bool", false)
    self:Variable("bWasInAir", "Bool", false)
    self:Variable("FootIKAlpha", "Float", 0.0)
    self:Variable("bCombatHasMovementInput", "Bool", false)
end

---声明根动画图：Locomotion 经惯性化后与上半身 Slot 按 Spine 分层，再进入收拔刀右手 IK、Foot Placement 与双腿 Leg IK。
---Foot Placement 自行读取 CharacterMovement 接地状态并执行双脚地面检测，Lua 只声明骨骼与调参。
---@param Graph LuaAnimGraph 基类创建的主 AnimGraph。
---@return nil result 最终姿势连接 Graph Result。
function ABP_Sekiro:AnimGraph(Graph)
    Graph.LayoutStyle = LayoutStyle.HierarchicalBlocks
    self:DeclareVariables()
    local foot_ik = Tuning.FootIK
    local weapon_ik = Tuning.WeaponIK
    local combat_base_pose = Graph:StateMachine("CombatBasePose", CombatBasePose)

    -- UE 动画姿势 Pin 不能直接扇出到两个消费者；基础战斗姿态先缓存，再供上半身 Slot 和分层混合共同读取。
    local locomotion_cache = Graph:Node(
        "LocomotionForUpperBody",
        EditorNodeClass.SaveCachedPose,
        {
            CacheName = "LocomotionForUpperBody",
        },
        "SaveCachedPose")
    locomotion_cache.Pose:Connect(combat_base_pose.Pose)
    local locomotion_for_slot = Graph:Node(
        "LocomotionSlotSource",
        EditorNodeClass.UseCachedPose,
        {
            CacheName = locomotion_cache.Name,
        },
        "UseCachedPose")
    local locomotion_for_base = Graph:Node(
        "LocomotionBlendBase",
        EditorNodeClass.UseCachedPose,
        {
            CacheName = locomotion_cache.Name,
        },
        "UseCachedPose")

    -- Slot 没有 Montage 时透传移动姿势；收拔刀播放时只替换 Spine 及其后代。
    local upper_body_slot = Graph:Node(
        "WeaponUpperBodySlot",
        EditorNodeClass.Slot,
        {
            SlotName = Tuning.UpperBody.SlotName,
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    upper_body_slot.Source:Connect(locomotion_for_slot.Pose)

    local upper_body_blend = Graph:Node(
        "WeaponUpperBodyBlend",
        EditorNodeClass.LayeredBlendPerBone,
        {
            BranchFilters = Tuning.UpperBody.BranchFilters,
            bMeshSpaceRotationBlend = true,
            bMeshSpaceScaleBlend = false,
            CurveBlendOption = "Override",
            bBlendRootMotionBasedOnRootBone = false,
        },
        "LayeredBlendPerBone")
    -- 两个输入都源自同一份 Locomotion 缓存；关闭按根骨权重筛选，避免 Spine 过滤器把基础 Root Motion 一并裁掉。
    upper_body_blend.BasePose:Connect(locomotion_for_base.Pose)
    upper_body_blend.BlendPose:Connect(upper_body_slot.Pose)

    -- 攻击、Raise/Lower 与 Deflect 都是全身动作；Slot 放在持续姿态状态机和上半身分层之后。
    local combat_full_body_slot = Graph:Node(
        "CombatFullBodySlot",
        EditorNodeClass.Slot,
        {
            SlotName = Tuning.Combat.FullBodySlotName,
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    combat_full_body_slot.Source:Connect(upper_body_blend.Pose)

    local to_component = Graph:Node(
        "SkeletalControlsLocalToComponent",
        EditorNodeClass.LocalToComponentSpace,
        nil,
        "LocalToComponentSpace")
    to_component.LocalPose:Connect(combat_full_body_slot.Pose)

    -- 挂点与刀身偏移反解得到收拔刀共用目标；Montage 曲线只在换挂帧附近平滑约束右手，
    -- 动作前段保持零权重，让原始收拔刀动画完整驱动手臂接近刀柄。
    -- 关节目标引用输入姿势的右肘位置，保留原动画肘部弯曲方向；禁止拉伸以免改变手臂比例。
    local weapon_hand_ik = Graph:Node(
        "WeaponHandIK",
        EditorNodeClass.TwoBoneIK,
        {
            IKBone = weapon_ik.IKBone,
            EffectorLocationSpace = "BoneSpace",
            EffectorTargetSocketName = weapon_ik.EffectorSocket,
            JointTargetLocationSpace = "BoneSpace",
            JointTargetBoneName = weapon_ik.JointTargetBone,
            bTakeRotationFromEffectorSpace = true,
            bAllowStretching = false,
            AlphaInputType = "Curve",
            AlphaCurveName = CurveNames.WeaponHandIK,
        },
        "TwoBoneIK")
    weapon_hand_ik.ComponentPose:Connect(to_component.ComponentPose)

    local foot_placement = Graph:Node(
        "FootPlacement",
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

    -- Foot Placement 和 Leg IK 必须使用同一权重；否则空中关闭贴地后，Leg IK 仍会把双脚拉回旧目标。
    local foot_ik_alpha = Graph:Property("FootIKAlpha", "FootIKAlpha")
    foot_placement.Alpha:Connect(foot_ik_alpha.Value)

    local leg_ik = Graph:Node(
        "DualLegIK",
        EditorNodeClass.LegIK,
        {
            LegDefinitions = foot_ik.LegIKLegDefinitions,
            ReachPrecision = foot_ik.ReachPrecision,
            MaxIterations = foot_ik.MaxIterations,
        },
        "LegIK")
    leg_ik.ComponentPose:Connect(foot_placement.Pose)
    leg_ik.Alpha:Connect(foot_ik_alpha.Value)

    local to_local = Graph:Node(
        "FootIKComponentToLocal",
        EditorNodeClass.ComponentToLocalSpace,
        nil,
        "ComponentToLocalSpace")
    to_local.ComponentPose:Connect(leg_ik.Pose)
    Graph.Result:Connect(to_local.Pose)

    local main_flow = Graph:Grid("MainFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    main_flow:Place(combat_base_pose, 0, 0)
    main_flow:Place(locomotion_cache, 1, 0)
    main_flow:Place(locomotion_for_base, 2, 0)
    main_flow:Place(locomotion_for_slot, 2, 1)
    main_flow:Place(upper_body_slot, 3, 1)
    main_flow:Place(upper_body_blend, 4, 0)
    main_flow:Place(combat_full_body_slot, 5, 0)
    main_flow:Place(to_component, 6, 0)
    main_flow:Place(weapon_hand_ik, 7, 0)
    main_flow:Place(foot_placement, 8, 0)
    main_flow:Place(foot_ik_alpha, 8, 1)
    main_flow:Place(leg_ik, 9, 0)
    main_flow:Place(to_local, 10, 0)
    main_flow:Place(Graph.OutputNode, 11, 0)
end

---每帧在游戏线程更新原生 Graph 消费的方向、步态和一次性动作锁存变量。
---非锁定移动始终选择 Forward 素材并由 Movement 朝输入方向旋转；锁定移动使用带滞回的四方向素材。
---@param Inst userdata 当前生成动画实例的 UnLua 代理，可直接访问 USKAnimInstance 字段和生成变量。
---@param delta_seconds number 本帧时长，单位为秒；用于平滑切换 Foot IK 权重。
---@return nil result 直接写入生成变量，不返回业务值。
function ABP_Sekiro.BlueprintUpdateAnimation(Inst, delta_seconds)
    local has_input = Inst.bHasMovementInput == true
    local locked_on = Inst.bIsLockedOn == true
    local crouching = Inst.bIsCrouching == true
    local pose_gait = normalize_pose_gait(Inst.DesiredGait, crouching)
    -- Foot Placement 在 UE 5.2 中不会因 CharacterMovement 进入 Falling 而自动停用，必须由 Lua 显式控制权重。
    -- 空中快速淡出可保留跳跃原姿势；落地较慢淡入可避免斜面命中变化导致骨盆和双腿瞬间弹跳。
    Inst.bCombatHasMovementInput = has_input
    local suppress_foot_ik = Inst.bIsInAir == true or Inst.bIsCombatFullBodyActionActive == true
    local foot_ik_target = suppress_foot_ik and 0.0 or 1.0
    local current_foot_ik_alpha = Inst.FootIKAlpha or foot_ik_target
    local foot_ik_speed = foot_ik_target > current_foot_ik_alpha
        and Tuning.FootIK.GroundBlendInSpeed
        or Tuning.FootIK.AirBlendOutSpeed
    Inst.FootIKAlpha = move_towards(
        current_foot_ik_alpha,
        foot_ik_target,
        foot_ik_speed,
        delta_seconds)
    -- 空中轨迹完全交给 CharacterMovement；忽略 Jump Start 的 Root Motion，避免状态混合把水平惯性衰减到零。
    -- 落地帧恢复 RootMotionFromEverything，使 Jump Land 和地面运动继续使用各自的根运动。
    Inst.RootMotionMode = Inst.bIsInAir == true
        and RootMotionMode.Ignore
        or RootMotionMode.Everything
    -- Movement 是锁定四方向的唯一权威；没有有效快照时按自由/冲刺的前向素材处理。
    local lock_on_locomotion_active = Inst.bHasLockOnLocomotionSnapshot == true
    local direction = lock_on_locomotion_active
        and Inst.LockOnCardinalDirection
        or Direction.Cardinal.Forward

    Inst.bPoseCrouching = crouching
    local turn_in_place_requested = locked_on
        and not has_input
        and Inst.bIsMoving ~= true
        and Inst.bIsInAir ~= true
        and Inst.bIsDodging ~= true
        and Inst.bIsCombatFullBodyActionActive ~= true
        and math.abs(Inst.AimYawDelta or 0) >= Tuning.IdleTurnEnterAngle
    if turn_in_place_requested and Inst.bWasTurnInPlaceRequested ~= true then
        -- 原地转向只使用最接近的左右素材；接近 180 度时稳定选择右转，不使用 Back 动画。
        Inst.LatchedTurnDirection = Direction.ResolveFreeTurnDirection(Inst.AimYawDelta)
    end
    Inst.bTurnInPlaceRequested = turn_in_place_requested
    if has_input then
        Inst.CycleDirection = direction
        Inst.LatchedActionDirection = direction
        -- Cycle 直接追随输入目标步态；Movement 的实际加减速不应让 Shift/Alt 松开后继续停留在 Sprint Pose。
        Inst.PoseGait = pose_gait
        if lock_on_locomotion_active then
            -- Graph 模式需要“实际移动方向相对当前 Actor”的角度；输入释放后继续供 Stop 重定向 Root Motion。
            Inst.LatchedLockOnLocomotionAngle = Inst.MoveDirectionAngle or 0.0
        end
        if Inst.bHadMovementInput ~= true then
            Inst.LatchedFreeStartDirection = Direction.ResolveFreeTurnDirection(
                Inst.MoveDirectionAngleBeforeRotation)
            Inst.LatchedActionGait = pose_gait
            Inst.bLatchedActionLockedOn = lock_on_locomotion_active
        end
    elseif Inst.bHadMovementInput == true then
        Inst.LatchedActionDirection = Inst.bLatchedActionLockedOn == true
            and Inst.CycleDirection
            or Direction.Cardinal.Forward
        -- 输入释放后 PoseGait 保留上一帧活动步态，使 Stop 能选择与离开 Cycle 一致的资产。
        Inst.LatchedActionGait = Inst.PoseGait
        local stop_direction_alignment_enabled = Inst.bLatchedActionLockedOn == true
            and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
        Inst.StopOrientationWarpingAlpha =
            stop_direction_alignment_enabled and 1.0 or 0.0
        -- Actor 已持续追向锁定目标，不再残留“素材主轴驱动的 ActorYaw”，因此 Stop 后不需要专用换脚回正。
        Inst.bStopTurnRequested = false
    end

    if Inst.bIsDodging == true and Inst.bWasDodging ~= true then
        Inst.LatchedActionDirection = locked_on
            and Direction.ClassifyCardinalFromAxes(
                Inst.DodgeDirection,
                Inst.DodgeDirectionLateral)
            or Direction.Cardinal.Forward
        Inst.LatchedActionGait = pose_gait
        Inst.bLatchedActionLockedOn = locked_on
        Inst.LatchedLockOnLocomotionAngle = locked_on
            and (Inst.DodgeDirection or 0.0)
            or 0.0
        Inst.StepOrientationWarpingAlpha = locked_on and 1.0 or 0.0
    end

    local sprint_requested = Inst.bHasMovementInput == true and Inst.DesiredGait == UE.ESKAnimGait.Sprint
    if sprint_requested and Inst.bWasSprintRequested ~= true then
        local sprint_direction = Direction.ResolveFreeTurnDirection(
            Inst.MoveDirectionAngleBeforeRotation)
        Inst.LatchedActionDirection = sprint_direction
        Inst.LatchedFreeStartDirection = sprint_direction
        Inst.LatchedActionGait = pose_gait
        Inst.bLatchedActionLockedOn = false
    end
    if Inst.bIsInAir == true and Inst.bWasInAir ~= true then
        -- 输入可能在离地后的下一帧释放；实际水平速度仍代表本次 Jump 已获得物理惯性，必须保持有向动画。
        local directional_jump = has_input
            or (Inst.Speed or 0) > Tuning.JumpDirectionalSpeedThreshold
        local jump_angle = directional_jump and (Inst.Angle or Inst.MoveDirectionAngle) or 0.0
        local jump_direction = locked_on
            and Direction.ClassifyOctant(jump_angle)
            or Direction.Octant.Forward
        Inst.bDirectionalJump = directional_jump
        Inst.bJumpStartedLockedOn = locked_on
        Inst.bJumpStartedCrouchedPose = Inst.bJumpStartedCrouched == true
        Inst.JumpDirection = jump_direction
        Inst.JumpDirectionResidualAngle = locked_on
            and directional_jump
            and clamp_direction_residual(
                Direction.GetOctantResidual(jump_angle, jump_direction),
                Tuning.JumpWarpingMaxAngle)
            or 0.0
        Inst.JumpWarpingAlpha = locked_on and directional_jump and 1.0 or 0.0
    end

    Inst.LockOnLocomotionAngle = lock_on_locomotion_active
        and (Inst.MoveDirectionAngle or 0.0)
        or 0.0
    Inst.LockOnOrientationWarpingAlpha = lock_on_locomotion_active
        and locked_on
        and Inst.bIsInAir ~= true
        and Inst.bIsDodging ~= true
        and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
        and 1.0
        or 0.0
    Inst.bWasLockedOn = locked_on
    Inst.bHadMovementInput = has_input
    Inst.bWasDodging = Inst.bIsDodging == true
    Inst.bWasSprintRequested = sprint_requested
    Inst.bWasTurnInPlaceRequested = turn_in_place_requested
    Inst.bWasInAir = Inst.bIsInAir == true
end

return ABP_Sekiro:Export()
