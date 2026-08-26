-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- Sekiro 新 Lua 动画蓝图入口。
-- Lua 在编译期声明原生 Graph，在游戏线程更新生成变量；Pose、状态时间、混合和 RootMotion 均由 UE 原生节点执行。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local AnimAssets = require("Animation.Sekiro.AnimAssets")
local CombatBasePose = require("Animation.Sekiro.Layer.Combat.CombatBasePose")
local OverlayPose = require("Animation.Sekiro.Layer.Combat.OverlayPose")
local PoseCorrection = require("Animation.Sekiro.Layer.PoseCorrection")
local Direction = require("Animation.Sekiro.Shared.Direction")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local DirectionEnum = "/Script/Sekiro.ESKLocomotionDirection"
local GaitEnum = "/Script/Sekiro.ESKAnimGait"
local OverlayStateEnum = "/Script/Sekiro.ESKAnimOverlayState"
local AnimationLayerInterface =
    "/Game/Characters/Sekiro/ALI_Sekiro.ALI_Sekiro_C"
local PoseLayerParameters = {
    {
        Name = "SourcePose",
        DataType = "Pose",
        bIsPose = true,
    },
}
local RootMotionMode = {
    Ignore = 1,
    Everything = 2,
}

---@class ABP_Sekiro: LuaAnimBlueprint
local ABP_Sekiro = LuaAnimBlueprint:Extend("ABP_Sekiro", {
    SourceModule = "Animation.Sekiro.ABP_Sekiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
    ImplementedInterfaces = {
        AnimationLayerInterface,
    },
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
    -- 预留锁定模式切换边沿；当前只记录上一帧状态，尚无 Graph 或规则消费者。
    self:Variable("bWasLockedOn", "Bool", false)
    self:Variable("bLatchedActionLockedOn", "Bool", false)
    self:Variable("bHadMovementInput", "Bool", false)
    self:Variable("bWasDodging", "Bool", false)
    self:Variable("bWasSprintRequested", "Bool", false)
    self:Variable("bTurnInPlaceRequested", "Bool", false)
    self:Variable("bWasTurnInPlaceRequested", "Bool", false)
    self:Variable("bPivotRequested", "Bool", false)
    self:Variable("bWasPivotRequested", "Bool", false)
    self:Variable("JumpDirection", "Enum", Direction.Octant.Forward, DirectionEnum)
    self:Variable("JumpDirectionResidualAngle", "Float", 0.0)
    self:Variable("JumpWarpingAlpha", "Float", 0.0)
    self:Variable("bDirectionalJump", "Bool", false)
    self:Variable("bJumpStartedLockedOn", "Bool", false)
    self:Variable("bJumpStartedCrouchedPose", "Bool", false)
    self:Variable("PeakFallSpeed", "Float", 0.0)
    self:Variable("bHeavyLand", "Bool", false)
    self:Variable("bWasInAir", "Bool", false)
    self:Variable("FootIKAlpha", "Float", 0.0)
    self:Variable("bCombatHasMovementInput", "Bool", false)
    self:Variable("bOverlayInAir", "Bool", false)
    self:Variable("PoseOverlayState", "Enum", 0, OverlayStateEnum)
end

---构建 BasePoses 动画层；只狼 Locomotion、空中与持续防御状态仍由原生状态机求值。
---@param graph LuaAnimGraph BasePoses 动画层函数图。
---@param _inputs table<string, LuaAnimPin> 本层没有输入姿势；参数只为统一动画层回调签名。
---@return nil result 状态机输出直接连接本层结果。
local function build_base_poses_layer(graph, _inputs)
    local locomotion = graph:StateMachine("BasePosesLocomotion", CombatBasePose)
    graph.Result:Connect(locomotion.Pose)

    local layout = graph:Grid("BasePosesFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    layout:Place(locomotion, 0, 0)
    layout:Place(graph.OutputNode, 1, 0)
end

---构建 BaseLayer 扩展边界；当前保持只狼基础姿势不变，后续可由角色子类独立覆盖。
---@param graph LuaAnimGraph BaseLayer 动画层函数图。
---@param inputs table<string, LuaAnimPin> SourcePose 是 BasePoses 的输出。
---@return nil result 当前直接透传输入姿势。
local function build_base_layer(graph, inputs)
    graph.Result:Connect(inputs.SourcePose)
end

---构建 OverlayLayer；Default、Sword、Guard、Combat 在独立 Function Graph 中选择。
---@param graph LuaAnimGraph OverlayLayer 动画层函数图。
---@param inputs table<string, LuaAnimPin> SourcePose 是 BaseLayer 的输出。
---@return nil result Overlay 合成结果连接本层输出。
local function build_overlay_layer(graph, inputs)
    local overlay = OverlayPose.Build(graph, inputs.SourcePose)
    graph.Result:Connect(overlay.Pose.Pose)

    local layout = graph:Grid("OverlayLayerFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    layout:Place(overlay.SourceCache, 0, 2)
    layout:Place(overlay.DefaultPose, 1, 0)
    layout:Place(overlay.SwordPose, 1, 1)
    layout:Place(overlay.GuardGroundPose, 1, 2)
    layout:Place(overlay.GuardAirPose, 1, 3)
    layout:Place(overlay.GuardAirCondition, 1, 4)
    layout:Place(overlay.GuardPose, 2, 3)
    layout:Place(overlay.CombatPose, 1, 5)
    layout:Place(overlay.OverlayState, 2, 5)
    layout:Place(overlay.Selector, 3, 2)
    layout:Place(graph.OutputNode, 4, 2)
end

---构建 LayerBlending；上半身收拔刀与全身战斗 Montage 在独立层中保持明确优先级。
---@param graph LuaAnimGraph LayerBlending 动画层函数图。
---@param inputs table<string, LuaAnimPin> SourcePose 是 OverlayLayer 的输出。
---@return nil result 全身战斗 Slot 输出连接本层结果。
local function build_layer_blending(graph, inputs)
    local upper_body_slot = graph:Node(
        "WeaponUpperBodySlot",
        EditorNodeClass.Slot,
        {
            SlotName = Tuning.UpperBody.SlotName,
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    upper_body_slot.Source:Connect(inputs.SourcePose)

    local upper_body_blend = graph:Node(
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
    upper_body_blend.BasePose:Connect(inputs.SourcePose)
    upper_body_blend.BlendPose:Connect(upper_body_slot.Pose)

    local combat_full_body_slot = graph:Node(
        "CombatFullBodySlot",
        EditorNodeClass.Slot,
        {
            SlotName = Tuning.Combat.FullBodySlotName,
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    combat_full_body_slot.Source:Connect(upper_body_blend.Pose)
    graph.Result:Connect(combat_full_body_slot.Pose)

    local layout = graph:Grid("LayerBlendingFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    layout:Place(upper_body_slot, 0, 1)
    layout:Place(upper_body_blend, 1, 0)
    layout:Place(combat_full_body_slot, 2, 0)
    layout:Place(graph.OutputNode, 3, 0)
end

---构建 FootIK 动画层；手部约束、Foot Placement 与 Leg IK 只在此处执行一次。
---@param graph LuaAnimGraph FootIK 动画层函数图。
---@param inputs table<string, LuaAnimPin> SourcePose 是 LayerBlending 的输出。
---@return nil result 骨骼修正后的 Local Space Pose 连接本层结果。
local function build_foot_ik_layer(graph, inputs)
    local correction = PoseCorrection.Build(graph, inputs.SourcePose)
    graph.Result:Connect(correction.Pose.Pose)

    local layout = graph:Grid("FootIKFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    layout:Place(correction.ToComponent, 0, 0)
    layout:Place(correction.WeaponHandIK, 1, 0)
    layout:Place(correction.FootPlacement, 2, 0)
    layout:Place(correction.FootIKAlpha, 2, 1)
    layout:Place(correction.LegIK, 3, 0)
    layout:Place(correction.ToLocal, 4, 0)
    layout:Place(graph.OutputNode, 5, 0)
end

---声明根动画图：主图只组装职责明确的 Animation Layer，并保留旧状态机作为 UE5.2 资产迁移保护。
---@param Graph LuaAnimGraph 基类创建的主 AnimGraph。
---@return nil result 最终姿势连接 Graph Result。
function ABP_Sekiro:AnimGraph(Graph)
    Graph.LayoutStyle = LayoutStyle.HierarchicalBlocks
    self:DeclareVariables()
    -- 旧资产已经序列化 CombatBasePoseGraph。保留未连接的兼容节点，避免 UE5.2 删除嵌套旧图时产生孤立节点；
    -- 实际输出改由 BasePoses Animation Layer 内的新状态机提供。
    local legacy_base_pose = Graph:StateMachine("CombatBasePose", CombatBasePose)
    local base_poses = Graph:LinkedAnimLayer("BasePoses", {
        LayerName = "BasePoses",
        InterfaceClass = AnimationLayerInterface,
    })
    local base_layer = Graph:LinkedAnimLayer("BaseLayer", {
        LayerName = "BaseLayer",
        InterfaceClass = AnimationLayerInterface,
        Parameters = PoseLayerParameters,
    })
    base_layer.SourcePose:Connect(base_poses.Pose)
    local overlay_layer = Graph:LinkedAnimLayer("OverlayLayer", {
        LayerName = "OverlayLayer",
        InterfaceClass = AnimationLayerInterface,
        Parameters = PoseLayerParameters,
    })
    overlay_layer.SourcePose:Connect(base_layer.Pose)
    local layer_blending = Graph:LinkedAnimLayer("LayerBlending", {
        LayerName = "LayerBlending",
        InterfaceClass = AnimationLayerInterface,
        Parameters = PoseLayerParameters,
    })
    layer_blending.SourcePose:Connect(overlay_layer.Pose)
    local foot_ik = Graph:LinkedAnimLayer("FootIK", {
        LayerName = "FootIK",
        InterfaceClass = AnimationLayerInterface,
        Parameters = PoseLayerParameters,
    })
    foot_ik.SourcePose:Connect(layer_blending.Pose)

    local final_inertialization = Graph:Node(
        "FinalPoseInertialization",
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
    final_inertialization.Source:Connect(foot_ik.Pose)
    Graph.Result:Connect(final_inertialization.Pose)

    local main_flow = Graph:Grid("MainFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    main_flow:Place(base_poses, 0, 0)
    main_flow:Place(base_layer, 1, 0)
    main_flow:Place(overlay_layer, 2, 0)
    main_flow:Place(layer_blending, 3, 0)
    main_flow:Place(foot_ik, 4, 0)
    main_flow:Place(final_inertialization, 5, 0)
    main_flow:Place(Graph.OutputNode, 6, 0)
    main_flow:Place(legacy_base_pose, 0, 2)
end

---声明 ALS V4 风格的职责动画层；主 AnimGraph 仅调用这些原生 Animation Layer Function Graph。
---@return nil result 各层会随 IR 生成到动画蓝图的动画层列表。
function ABP_Sekiro:DeclareAnimationLayers()
    self:AnimLayer("BasePoses", {
        InterfaceClass = AnimationLayerInterface,
        bOverride = true,
    }, build_base_poses_layer)
    self:AnimLayer("BaseLayer", {
        InterfaceClass = AnimationLayerInterface,
        bOverride = true,
        Parameters = PoseLayerParameters,
    }, build_base_layer)
    self:AnimLayer("OverlayLayer", {
        InterfaceClass = AnimationLayerInterface,
        bOverride = true,
        Parameters = PoseLayerParameters,
    }, build_overlay_layer)
    self:AnimLayer("LayerBlending", {
        InterfaceClass = AnimationLayerInterface,
        bOverride = true,
        Parameters = PoseLayerParameters,
    }, build_layer_blending)
    self:AnimLayer("FootIK", {
        InterfaceClass = AnimationLayerInterface,
        bOverride = true,
        Parameters = PoseLayerParameters,
    }, build_foot_ik_layer)
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
    Inst.bOverlayInAir = Inst.bIsInAir == true
    Inst.PoseOverlayState = Inst.OverlayState
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
    -- Pivot 读取新输入相对当前速度的夹角；进入/退出双阈值保证反向输入不会在边界反复切换。
    -- Sprint 使用自己的方向停止资产，不进入复用 Start 的 Pivot 降级状态。
    local pivot_threshold = Inst.bWasPivotRequested == true
        and Tuning.PivotExitAngle
        or Tuning.PivotEnterAngle
    local pivot_requested = has_input
        and Inst.bIsMoving == true
        and Inst.bIsInAir ~= true
        and Inst.bIsDodging ~= true
        and Inst.bIsCombatFullBodyActionActive ~= true
        and Inst.DesiredGait ~= UE.ESKAnimGait.Sprint
        and math.abs(Inst.DirectionDelta or 0.0) >= pivot_threshold
    if pivot_requested and Inst.bWasPivotRequested ~= true then
        Inst.LatchedActionDirection = direction
        Inst.LatchedActionGait = pose_gait
        Inst.bLatchedActionLockedOn = lock_on_locomotion_active
        Inst.LatchedFreeStartDirection = Direction.ResolveFreeTurnDirection(
            Inst.MoveDirectionAngleBeforeRotation)
    end
    Inst.bPivotRequested = pivot_requested

    -- ALS V4 的 Turn In Place 检查使用未平滑的瞄准偏角；RootYawOffset 只负责姿势偏差表达。
    -- Movement 会在请求产生后开始消耗 ActorYaw，若用平滑 RootYawOffset 判断，偏差可能尚未达到阈值就被旋转清除。
    local turn_yaw_delta = Inst.AimYawDelta or Inst.RootYawOffset or 0.0
    local turn_in_place_threshold = Inst.bWasTurnInPlaceRequested == true
        and Tuning.IdleTurnExitAngle
        or Tuning.IdleTurnEnterAngle
    local turn_in_place_requested = locked_on
        and not has_input
        and Inst.bIsMoving ~= true
        and Inst.bIsInAir ~= true
        and Inst.bIsDodging ~= true
        and Inst.bIsCombatFullBodyActionActive ~= true
        and math.abs(turn_yaw_delta) >= turn_in_place_threshold
    if turn_in_place_requested and Inst.bWasTurnInPlaceRequested ~= true then
        -- 原地转向只使用最接近的左右素材；接近 180 度时稳定选择右转，不使用 Back 动画。
        Inst.LatchedTurnDirection = Direction.ResolveFreeTurnDirection(turn_yaw_delta)
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
        -- 每次离地重新开始统计；Heavy Land 会在预测到高速接地或实际接地边沿锁存。
        Inst.PeakFallSpeed = 0.0
        Inst.bHeavyLand = false
    end
    if Inst.bIsInAir == true then
        Inst.PeakFallSpeed = math.max(Inst.PeakFallSpeed or 0.0, Inst.FallSpeed or 0.0)
        local heavy_land_predicted =
            (Inst.LandPredictionAmount or 0.0) >= Tuning.HeavyLandPredictionThreshold
            and (Inst.PeakFallSpeed or 0.0) >= Tuning.HeavyLandMinFallSpeed
        if heavy_land_predicted then
            Inst.bHeavyLand = true
        end
    elseif Inst.bWasInAir == true then
        -- 没有命中预测表面时仍按本次峰值速度兜底，避免从平台边缘落地后错误播放轻落地。
        Inst.bHeavyLand = Inst.bHeavyLand == true
            or (Inst.PeakFallSpeed or 0.0) >= Tuning.HeavyLandMinFallSpeed
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
    Inst.bWasPivotRequested = pivot_requested
    Inst.bWasInAir = Inst.bIsInAir == true
end

return ABP_Sekiro:Export()
