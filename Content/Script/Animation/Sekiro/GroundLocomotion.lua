-- Sekiro 地面移动顶层状态机。
--
-- 层级结构与当前转向方案：
-- 1. Standing 与 Crouch 是两个复用 Idle/Turn/Start/Cycle/Stop 逻辑的子状态机。
-- 2. Sprint.Start/Cycle/Stop、Step 和 Jump 是顶层独立分支；蹲姿触发它们时先由输入层解除蹲姿。
-- 3. 运行时用 Standing.Cycle、Crouch.Start 等复合状态名提交同一个 GroundLocomotion Pose，
--    因而无需增加 C++ 动画层，现有曲线、相位匹配和 ShowDebug Animation 都仍然有效。
--
-- 转向规则：
-- 1. 当前使用中的 Locomotion 动画都不改变角色最终朝向，Movement 持续消费绝对目标 Yaw。
-- 2. Free Walk/Run、Step 和 90 度以内 Sprint 都保持当前动画，由 SteerToTarget 快速转向。
-- 3. Idle 大角度输入播放四方向 Turn 身体动作，同时由 Movement 完成真实旋转；持续输入随后进入 Start。
-- 4. Sprint 超过大角度阈值时才执行 Stop -> 程序转向 -> Start，形成制动后重新加速。
-- 5. LockOn Walk/Run 使用四方向循环并面向目标；LockOn Sprint 仍朝运动方向，镜头继续跟随锁定目标。
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")
local LayerLibrary = require("Animation.Sekiro.Layer.GroundLocomotion.Library")
local StandingLocomotionClass = require("Animation.Sekiro.Layer.GroundLocomotion.StandingLocomotion")
local CrouchLocomotionClass = require("Animation.Sekiro.Layer.GroundLocomotion.CrouchLocomotion")

local State = LayerLibrary.State
local Branch = LayerLibrary.Branch
local LocalState = LayerLibrary.LocalState
local JumpLocalState = LayerLibrary.JumpLocalState
local Tuning = LayerLibrary.Tuning
local Anim = LayerLibrary.Assets
local JumpAnim = LayerLibrary.JumpAssets
local Mode = LayerLibrary.LocomotionMode
local TurnDirection = LayerLibrary.TurnDirection
local JumpDirection = LayerLibrary.JumpDirection
local Curve = LayerLibrary.Curve
local RootMotionRotationMode = LayerLibrary.RootMotionRotationMode

-- 锁定地面移动只选择四个主方向素材，实际输入角仍保持连续。
-- Orientation Warping 使用“精确输入角 - 素材主方向角”作为残差：下半身转向真实轨迹，脊柱反向补偿以继续面向目标。
local LockedDirectionAngle = {
    [TurnDirection.Forward] = 0,
    [TurnDirection.Right] = 90,
    [TurnDirection.Back] = 180,
    [TurnDirection.Left] = -90,
}

-- Jump 有八方向素材，因此残差最多约 22.5 度，只需要轻量补偿。
local JumpDirectionAngle = {
    [JumpDirection.Forward] = 0,
    [JumpDirection.ForwardRight] = 45,
    [JumpDirection.Right] = 90,
    [JumpDirection.BackRight] = 135,
    [JumpDirection.Back] = 180,
    [JumpDirection.BackLeft] = -135,
    [JumpDirection.Left] = -90,
    [JumpDirection.ForwardLeft] = -45,
}

local GroundLocomotion = class("GroundLocomotion", BaseStateMachine, {
    LayerName = LayerLibrary.LayerName,
    EntryState = State.StandingIdle,
    State = State,
    States = State,
    StateList = LayerLibrary.StateList,
    Tuning = Tuning,
    Assets = LayerLibrary.Assets,
    AnimationSettings = LayerLibrary.AnimationSettings,
})

---把角度规范化到 -180..180 度范围。
---@param angle number|nil 相对角色朝向的角度，单位为度并按 -180..180 解释。
---@return number normalized_angle 位于 -180..180 度内的角度。
local function normalize_angle(angle)
    local normalized = (angle or 0) % 360
    if normalized > 180 then
        normalized = normalized - 360
    end
    return normalized
end

---计算 0..1 三次平滑插值，供停止姿态无突变地收回。
---@param value number 需要平滑的原始插值进度；函数会先限制到 0..1。
---@return number alpha 平滑后的 0..1 插值权重。
local function smooth_step(value)
    local clamped = math.max(0, math.min(value, 1))
    return clamped * clamped * (3 - 2 * clamped)
end

---在 Initialize 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function GroundLocomotion:Initialize()
    self.StandingLocomotion = StandingLocomotionClass()
    self.CrouchLocomotion = CrouchLocomotionClass()
end

---复合状态只用于表达层级，不参与动画资源命名；资源始终由对应子状态机显式返回。
---@param branch string 顶层分支名称，例如 Standing、Crouch 或 Sprint。
---@param local_state string 子状态名。
---@return string state_name 运行时复合状态名。
function GroundLocomotion:MakeBranchState(branch, local_state)
    return string.format("%s.%s", branch, local_state)
end

---@param state_name string 运行时状态名。
---@return string|nil branch 所属顶层分支。
---@return string local_state 子状态；Step 返回 Step。
---把 Standing.Cycle 等复合状态拆成顶层分支和局部状态；Step 作为独立状态特殊处理。
function GroundLocomotion:SplitBranchState(state_name)
    if state_name == State.Step then
        return nil, State.Step
    end

    local branch, local_state = string.match(tostring(state_name or ""), "^([^.]+)%.(.+)$")
    return branch, local_state or ""
end

---判断角色是否处于 Crouch 姿态，兼容 C++ 布尔字段和 Stance 枚举文本。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsCrouchStance()
    return self.bIsCrouching == true
        or self.Stance == "Crouching"
end

---姿态只决定 Standing/Crouch 子状态机；Sprint 和 Step 始终属于站立顶层动作。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function GroundLocomotion:GetDesiredWalkRunBranch()
    return self:IsCrouchStance() and Branch.Crouch or Branch.Standing
end

---读取或计算行走跑步状态machine，字段缺失时遵循函数内的明确回退规则。
---@param branch string 顶层状态机分支名称，例如 Standing、Crouch、Sprint 或 Jump。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetWalkRunStateMachine(branch)
    if branch == Branch.Crouch then
        return self.CrouchLocomotion
    end

    return self.StandingLocomotion
end

---返回当前 Standing/Crouch 分支对应的 WalkRun 子状态机。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetActiveWalkRunStateMachine()
    return self:GetWalkRunStateMachine(self.ActiveLocomotionBranch)
end

---读取或计算归一化时间，字段缺失时遵循函数内的明确回退规则。
---@return number value 读取或计算得到的数值。
function GroundLocomotion:GetNormalizedTime()
    return self:GetCurrentAnimationNormalizedTime(0)
end

---判断当前一次性动画是否进入目标动画的交叉混合尾窗。
---判断发生在本帧快照推进前，因此额外加入 DeltaSeconds，保证切换后旧动画恰好在混合结束时抵达末帧。
---@param blend_time number 目标状态使用的交叉混合时长，单位为秒。
---@return boolean ready 当前动画是否应开始尾段重叠过渡。
function GroundLocomotion:IsTailBlendReady(blend_time)
    local remaining_time = self:GetCurrentAnimationRemainingTime(-1)
    if remaining_time < 0 then
        return false
    end

    local transition_window = math.max(blend_time or 0, 0)
        + math.max(self.DeltaSeconds or 0, 0)
    return remaining_time <= transition_window
end

---读取当前动画的循环步态相位。MovePhase 的 0 和 1 表示同一左脚周期边界，不能按普通浮点线性混合。
---@return number|nil phase 当前 0..1 相位；曲线缺失时返回 nil。
function GroundLocomotion:GetCurrentMovePhase()
    local phase = self:GetCurrentCircularCurveValue(Curve.MovePhase, -1)
    if phase < 0 then
        return nil
    end

    return phase
end

---在状态切换判定成立的同一帧保存源动画相位，供目标 Cycle 选择匹配的起播位置。
---@param transition_name string 调试显示的切换名称。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:CaptureTransitionMovePhase(transition_name)
    self.PendingMovePhase = self:GetCurrentMovePhase()
    self.PendingMovePhaseReferenceTime = self:GetNormalizedTime()
    if self.PendingMovePhase == nil then
        self:LogDebug("PhaseMatch", string.format(
            "transition=%s sourceCurve=%s missing=true",
            tostring(transition_name),
            tostring(Curve.MovePhase)))
    end
end

---把源动画 MovePhase 映射到目标动画真正的归一化位置，而不是把相位值直接当作播放百分比。
---@param animation_ref string 目标 Sequence/BlendSpace 资源引用。
---@param blend_input number BlendSpace X 输入；Sequence 传 0。
---@param source_phase number|nil 源动画步态相位。
---@param reference_time number|nil 多个周期均匹配时优先靠近的归一化位置。
---@param reason string 调试原因。
---@return number|nil start_position 目标归一化起播位置；曲线缺失时返回 nil。
function GroundLocomotion:ResolveMatchedMovePhasePosition(
    animation_ref,
    blend_input,
    source_phase,
    reference_time,
    reason)
    if source_phase == nil then
        return nil
    end

    local start_position = self:FindCircularCurveMatchingNormalizedTime(
        animation_ref,
        Curve.MovePhase,
        source_phase,
        blend_input or 0,
        0,
        0,
        reference_time or 0,
        -1)
    if start_position < 0 then
        self:LogDebug("PhaseMatch", string.format(
            "reason=%s target=%s sourcePhase=%.3f targetCurve=%s missing=true",
            tostring(reason),
            tostring(self:GetAnimationName(animation_ref)),
            source_phase,
            tostring(Curve.MovePhase)))
        return nil
    end

    self:LogDebug("PhaseMatch", string.format(
        "reason=%s target=%s sourcePhase=%.3f targetPosition=%.3f reference=%.3f",
        tostring(reason),
        tostring(self:GetAnimationName(animation_ref)),
        source_phase,
        start_position,
        reference_time or 0))
    return start_position
end

---判断当前上下文是否具有移动输入。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:HasMovementInput()
    if self.bHasMovementInput ~= nil then
        return self.bHasMovementInput == true
    end

    return (self.MovementInputAmount or 0) > Tuning.InputThreshold
end

---判断角色是否处于站立或蹲姿地面状态，并排除真实离地帧。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsGrounded()
    return (self.MovementState == nil
            or self.MovementState == "Grounded"
            or self.MovementState == "Crouching")
        and self.bIsInAir ~= true
end

---地面移动意图与朝向模式解耦；LockOn、Free 和 SprintAlign 都可产生移动，Step 播放期间暂时屏蔽普通移动转换。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsGroundMovementScope()
    return self:IsGrounded()
        and self.bIsDodging ~= true
end

---判断玩家输入和角色状态是否请求move。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:WantsMove()
    return self:IsGroundMovementScope() and self:HasMovementInput()
end

---直接读取动画实例暴露的 MovementTier/DesiredGait，得到本帧期望的语义状态。
---没有移动输入时返回 Idle；这里不再把 Sprint 降级成 Run。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetDesiredLocomotionMode()
    if not self:WantsMove() then
        return Mode.Idle
    end

    -- 蹲姿分支没有 Sprint。DesiredGait 由 C++ 按 Walk modifier/摇杆阈值计算，
    -- MovementTier=Crouch 只表达姿态，不再把蹲姿固定成 Walk。
    if not self:IsCrouchStance()
        and (self.MovementTier == "Sprint" or self.DesiredGait == "Sprint") then
        return Mode.Sprint
    end

    if self.DesiredGait == "Walk" or self.MovementTier == "Walk" then
        return Mode.Walk
    end

    return Mode.Run
end

---返回当前动画正在表现的 Locomotion 状态，而不是输入刚刚请求的状态。
---Turn 播放期间必须保持 FromMode，只有 Turn 完成进入 Cycle 后才提交 ToMode。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetActiveLocomotionMode()
    if self.ActiveLocomotionMode ~= nil then
        return self.ActiveLocomotionMode
    end

    if self.LockedMotionGait ~= nil then
        return self.LockedMotionGait
    end

    local _, local_state = self:SplitBranchState(self.CurrentStateName)
    return local_state == LocalState.Idle and Mode.Idle or self:GetDesiredLocomotionMode()
end

---只有 LookingDirection 且目标仍有效时使用锁定环绕；SprintAlign 即使仍持有目标，也必须使用前向 Sprint。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsLockedMovementMode(mode)
    return mode ~= Mode.Sprint
        and self.bIsLockedOn == true
        and self.RotationMode == "LookingDirection"
end

---按角色局部移动角把锁定输入分成四个主方向；项目没有斜向原始动画，因此不按字符串组合资源。
---@param angle number|nil 相对角色朝向的角度，单位为度并按 -180..180 解释。
---@return string name 规范化或分类后的语义名称。
function GroundLocomotion:ClassifyLockedMoveDirection(angle)
    local normalized = normalize_angle(angle)
    local absolute_angle = math.abs(normalized)
    if absolute_angle <= Tuning.LockedDirectionBoundaryAngle then
        return TurnDirection.Forward
    end
    if absolute_angle >= 180 - Tuning.LockedDirectionBoundaryAngle then
        return TurnDirection.Back
    end

    return normalized < 0 and TurnDirection.Left or TurnDirection.Right
end

---当前方向在基础区间外继续保留 HysteresisAngle，避免摇杆斜向或角色追目标时在边界抖动。
---@return string|nil value 解析出的模式、方向或状态名称。
function GroundLocomotion:ResolveLockedMoveDirection()
    local current = self.ActiveLockedDirection or self.LockedMotionDirection
    if not self:HasMovementInput() then
        return current or TurnDirection.Forward
    end

    local angle = normalize_angle(self.MoveDirectionAngle or 0)
    local absolute_angle = math.abs(angle)
    local boundary = Tuning.LockedDirectionBoundaryAngle
    local hysteresis = Tuning.LockedDirectionHysteresisAngle
    if current == TurnDirection.Forward and absolute_angle <= boundary + hysteresis then
        return current
    end
    if current == TurnDirection.Back and absolute_angle >= 180 - boundary - hysteresis then
        return current
    end
    if current == TurnDirection.Left
        and angle <= -boundary + hysteresis
        and angle >= -(180 - boundary + hysteresis) then
        return current
    end
    if current == TurnDirection.Right
        and angle >= boundary - hysteresis
        and angle <= 180 - boundary + hysteresis then
        return current
    end

    return self:ClassifyLockedMoveDirection(angle)
end

---原地 Turn 动画只按进入状态时的角度选择一次，播放中不因目标变化更换资源。
---Left/Right/Back 只描述身体动作类型，Movement 始终使用未量化的最新 DesiredMoveYaw。
---@param angle number|nil 相对角色朝向的角度，单位为度并按 -180..180 解释。
---@return string|nil animation_ref 匹配角度的原地转向动画引用。
---@return string direction 动画表达的离散转向方向。
function GroundLocomotion:ResolveIdleTurnAnimation(angle)
    local normalized = normalize_angle(angle)
    local absolute_angle = math.abs(normalized)
    local state_machine = self:GetActiveWalkRunStateMachine()
    if absolute_angle >= Tuning.IdleTurnBackAnimationAngle then
        return state_machine:GetTurnAnimation(TurnDirection.Back), TurnDirection.Back
    end
    if absolute_angle >= Tuning.IdleTurnSideAnimationAngle then
        local direction = normalized < 0 and TurnDirection.Left or TurnDirection.Right
        return state_machine:GetTurnAnimation(direction), direction
    end

    return state_machine:GetTurnAnimation(TurnDirection.Forward), TurnDirection.Forward
end

---不同 Locomotion 状态使用不同 Steer 速率上限；速率只防止单帧跳变，不决定最终角度。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param _rotation_mode string|nil _rotation_mode 对应的语义字符串或资源标识。
---@return number value 读取或计算得到的数值。
function GroundLocomotion:GetRootMotionYawRate(mode, _rotation_mode)
    if mode == Mode.Walk then
        return Tuning.WalkSteerMaxYawRate
    end
    if mode == Mode.Sprint then
        return Tuning.SprintSteerMaxYawRate
    end
    return Tuning.RunSteerMaxYawRate
end

---向通用插件提交当前姿势的绝对世界朝向策略。
---动画线程只提取和混合 RootMotion 平移；Movement 在最终消费世界 RootMotion 时读取最新 ActorYaw，
---重新计算到 TargetWorldYaw 的剩余角度并合成旋转，因此不会执行已经过期的相对角命令。
---@param rotation_mode string|nil rotation_mode 对应的语义字符串或资源标识。
---@param target_world_yaw number|nil 角色需要对齐的绝对世界 Yaw，单位为度。
---@param max_yaw_rate number|nil 允许的最大转向角速度，单位为度每秒。
---@param completion_time number|nil 计划完成转向的时长，单位为秒；0 表示不限定。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:SetRootMotionRotationPolicy(
    rotation_mode,
    target_world_yaw,
    max_yaw_rate,
    completion_time)
    self:CallCpp(
        "SetLuaAnimRootMotionRotationPolicyByName",
        self:GetLayerName(),
        rotation_mode,
        normalize_angle(target_world_yaw or self.ActorYaw or 0),
        max_yaw_rate or 0,
        completion_time or 0)
end

---向通用 Lua AnimGraph 节点提交方向扭转策略。
---OrientationAngle 只修正 Pose；TranslationAngle 在 Movement 最终消费 Root Motion 时修正位移，两者使用同一残差但相互独立。
---@param enabled boolean 是否启用姿态方向扭转。
---@param orientation_angle number 素材主方向到精确目标方向的相对角。
---@param warping_alpha number 姿态扭转权重。
---@param warp_translation boolean 是否旋转本帧 Root Motion 平移。
---@param translation_angle number Root Motion 平移旋转角。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:SetOrientationWarpingPolicy(
    enabled,
    orientation_angle,
    warping_alpha,
    warp_translation,
    translation_angle)
    self:CallCpp(
        "SetLuaAnimOrientationWarpingPolicyByName",
        self:GetLayerName(),
        enabled == true,
        normalize_angle(orientation_angle or 0),
        math.max(0, math.min(warping_alpha or 0, 1)),
        warp_translation == true,
        normalize_angle(translation_angle or orientation_angle or 0))
end

---清除orientationwarpingpolicy，防止旧状态污染后续更新。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:ClearOrientationWarpingPolicy()
    self:SetOrientationWarpingPolicy(false, 0, 0, false, 0)
end

---锁定地面移动使用四向素材，但位移和下半身姿势必须跟随未量化的真实输入角。
---@return number residual_angle 本帧素材方向到精确方向的残差。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@param exact_angle number|nil 未量化的精确局部运动角，单位为度。
---@param warping_alpha number 0..1 的 Orientation Warping 姿态权重。
---@param warp_translation boolean|nil 是否启用 warp_translation 对应行为的布尔标志。
function GroundLocomotion:ApplyLockedDirectionWarp(direction, exact_angle, warping_alpha, warp_translation)
    local source_angle = LockedDirectionAngle[direction] or 0
    local target_angle = normalize_angle(exact_angle or source_angle)
    local residual_angle = normalize_angle(target_angle - source_angle)
    self:SetOrientationWarpingPolicy(
        true,
        residual_angle,
        warping_alpha or Tuning.LockedOrientationWarpAlpha,
        warp_translation == true,
        residual_angle)
    return residual_angle
end

---Jump 八向素材只保留较小的残差；Start/Land 有 Root Motion，InAir 仅扭转 Pose。
---@param warp_translation boolean|nil 是否启用 warp_translation 对应行为的布尔标志。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:ApplyJumpDirectionWarp(warp_translation)
    if self.JumpLockedOn ~= true or self.JumpWasDirectional ~= true then
        self:ClearOrientationWarpingPolicy()
        return
    end

    local source_angle = JumpDirectionAngle[self.JumpDirection] or 0
    local target_angle = normalize_angle(self.JumpMotionTargetAngle or source_angle)
    local residual_angle = normalize_angle(target_angle - source_angle)
    self:SetOrientationWarpingPolicy(
        true,
        residual_angle,
        Tuning.JumpOrientationWarpAlpha,
        warp_translation == true,
        residual_angle)
end

---Stop 的 Root Motion 继续沿输入释放前的斜向轨迹；姿态残差在尾段归零，让双腿最终重新面向锁定目标。
---@return number value 读取或计算得到的数值。
function GroundLocomotion:GetLockedStopWarpAlpha()
    local start_time = Tuning.LockedStopAlignStartNormalizedTime
    local end_time = math.max(Tuning.LockedStopAlignEndNormalizedTime, start_time + 0.001)
    local fade_progress = ((self:GetNormalizedTime() or 0) - start_time) / (end_time - start_time)
    return 1 - smooth_step(fade_progress)
end

---判断移动输入是否已经连续丢失到足以确认“真正停止”。
---Cycle 播放时间会在循环末尾回绕，因此这里累计相邻快照增量。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsMoveInputHandoffExpired()
    if self:WantsMove() then
        self.MoveInputLostDuration = nil
        self.MoveInputLostObservedTime = nil
        return false
    end

    local state_time = self.StateTime or 0
    if self.MoveInputLostObservedTime == nil then
        self.MoveInputLostDuration = 0
        self.MoveInputLostObservedTime = state_time
        return false
    end

    local observed_delta = state_time - self.MoveInputLostObservedTime
    if observed_delta < 0 then
        -- 循环动画发生回绕时无法再直接相减，使用当前更新帧的 DeltaSeconds 补上这一次增量。
        observed_delta = self.DeltaSeconds or 0
    end
    self.MoveInputLostDuration = (self.MoveInputLostDuration or 0) + math.max(observed_delta, 0)
    self.MoveInputLostObservedTime = state_time
    return self.MoveInputLostDuration >= Tuning.MoveInputHandoffTime
end

---Start 资源由步态和锁定主方向共同决定；Sprint 没有四方向循环，始终使用前向资源。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetStartAnimation(mode, direction)
    local resolved_direction = direction or TurnDirection.Forward
    if mode == Mode.Sprint then
        return Anim.Sprint_Forward_Start
    end

    return self:GetActiveWalkRunStateMachine():GetStartAnimation(mode, resolved_direction)
end

---Stop 必须沿用输入释放前锁定的方向，保证停止动作起始姿势与当前循环一致。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetStopAnimation(mode, direction)
    local resolved_direction = direction or TurnDirection.Forward
    if mode == Mode.Sprint then
        return Anim.Sprint_Forward_Stop
    end

    return self:GetActiveWalkRunStateMachine():GetStopAnimation(mode, resolved_direction)
end

---清理一次性动作字段；该操作在 Idle 和新动作入口复用，防止旧动作计划污染下一次资源选择。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:ClearTransientMotionPlan()
    self.LockedMotionGait = nil
    self.LockedMotionAnimation = nil
    self.LockedMotionDirection = nil
    self.LockedMotionFacingMode = nil
    self.ActiveLockedDirection = nil
    self.LockedMotionSourceAngle = nil
    self.LockedMotionTargetAngle = nil
    self.CurrentCycleAnimation = nil
    self.CurrentCycleMode = nil
    self.StepAnimation = nil
    self.StepDirection = nil
    self.StepLockedOn = nil
    self.StepTargetWorldYaw = nil
    self.JumpDirection = nil
    self.JumpLockedOn = nil
    self.JumpWasDirectional = nil
    self.JumpStartedCrouched = nil
    self.JumpStartAnimation = nil
    self.JumpDirectionalInAirAnimation = nil
    self.JumpDirectionalLandAnimation = nil
    self.JumpLandAnimation = nil
    self.JumpTargetWorldYaw = nil
    self.JumpMotionTargetAngle = nil
    self.JumpMinimumVerticalVelocity = nil
    self.JumpAirTime = nil
    self.ProgramTurnAnimation = nil
    self.ProgramTurnDirection = nil
    self.StopReason = nil
    self.StartStopRequestedAt = nil
    self.PendingMovePhase = nil
    self.PendingMovePhaseReferenceTime = nil
    self.SprintLargeTurnRequestTime = 0
end

---Dodge 按下当帧产生的 bIsDodging 是 Step 的入口意图，与按住后是否提升为 Sprint 相互独立。
---锁定与非锁定都允许进入 Step；两种模式的朝向策略和动画选择在 PlanStepMotion 中分开处理。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:WantsStep()
    return self:IsGrounded()
        and self.bIsDodging == true
end

---Step 进入时锁定模式、方向和动画；播放中不更换资源，但非锁定实际朝向持续追随最新输入。
---非锁定统一转向输入方向并播放前向 Step；锁定保持面向目标，按角色局部前后左右选择对应动画。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:PlanStepMotion()
    local step_locked_on = self.bIsLockedOn == true or self.RotationMode == "LookingDirection"
    -- DodgeDirection 已由组合输入解析器在时间窗结束时冻结；这里直接消费结果，避免动画层再次读取不同帧的 MoveIntent。
    local dodge_forward = self.DodgeDirection or 0
    local dodge_lateral = self.DodgeDirectionLateral or 0
    local locked_direction = nil
    if step_locked_on then
        if math.abs(dodge_lateral) > math.abs(dodge_forward) then
            locked_direction = dodge_lateral < 0 and TurnDirection.Left or TurnDirection.Right
        elseif dodge_forward < -Tuning.InputThreshold then
            locked_direction = TurnDirection.Back
        else
            locked_direction = TurnDirection.Forward
        end
    end

    self.ActiveLocomotionMode = nil
    self:ClearTransientMotionPlan()
    self.StepLockedOn = step_locked_on
    self.StepDirection = locked_direction

    if self.StepLockedOn then
        self.StepTargetWorldYaw = normalize_angle(self.ActorYaw or 0)

        if locked_direction == TurnDirection.Left then
            self.StepAnimation = Anim.Step_Left
        elseif locked_direction == TurnDirection.Right then
            self.StepAnimation = Anim.Step_Right
        elseif locked_direction == TurnDirection.Back then
            self.StepAnimation = Anim.Step_Back
        else
            self.StepAnimation = Anim.Step_Forward
        end
    else
        self.StepTargetWorldYaw = self.bHasDesiredMoveYaw == true
            and normalize_angle(self.DesiredMoveYaw or self.ActorYaw or 0)
            or normalize_angle(self.ActorYaw or 0)
        self.StepAnimation = Anim.Step_Forward
    end

    self:LogDebug("MotionPlan", string.format(
        "state=Step locked=%s direction=%s moveAngle=%.1f dodgeInput=(%.2f, %.2f) targetWorldYaw=%.1f animation=%s",
        tostring(self.StepLockedOn),
        tostring(self.StepDirection or TurnDirection.Forward),
        self.MoveDirectionAngle or 0,
        self.DodgeDirection or self.MoveInputY or 0,
        self.DodgeDirectionLateral or self.MoveInputX or 0,
        self.StepTargetWorldYaw,
        tostring(self:GetAnimationName(self.StepAnimation))))
end

---非锁定 Step 播放期间持续追随最新输入方向；锁定 Step 保持角色面向目标，侧向和后向位移来自对应动画 RootMotion。
---两种模式都不额外施加 Launch/Impulse，避免程序位移与根运动叠加。
---@return LuaSequencePlayer|nil pose_source Step 状态的持久 SequencePlayer Pose 节点；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Step()
    if self.EvaluatingResetTime == true or self.StepAnimation == nil then
        self:PlanStepMotion()
    end

    -- Step 仍按自身四方向资源和既有朝向规则处理，不能继承 Cycle/Jump 留下的方向扭转策略。
    self:ClearOrientationWarpingPolicy()
    if self.StepLockedOn then
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.Ignore,
            self.StepTargetWorldYaw,
            0)
    else
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw,
            Tuning.StepTurnMaxYawRate)
    end

    local player = self:GetSequencePlayer(State.Step)
    player.Sequence = self.StepAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.StepBlendTime
    return player
end

---Jump 与 Step 使用同一套朝向原则：非锁定使用 Forward 资源并朝输入方向转向，锁定保持面向目标并选择角色局部方向资源。
---八方向每 45 度一个扇区，边界固定为 22.5 度，方向在离地时冻结以避免空中切换资产。
---@param angle number 输入目标方向相对角色朝向的角度。
---@return string direction JumpDirection 中的八方向之一。
function GroundLocomotion:ClassifyJumpDirection(angle)
    local normalized = normalize_angle(angle)
    local boundary = Tuning.JumpDirectionBoundaryAngle
    if normalized >= -boundary and normalized <= boundary then
        return JumpDirection.Forward
    end
    if normalized > boundary and normalized <= 3 * boundary then
        return JumpDirection.ForwardRight
    end
    if normalized > 3 * boundary and normalized <= 5 * boundary then
        return JumpDirection.Right
    end
    if normalized > 5 * boundary and normalized <= 7 * boundary then
        return JumpDirection.BackRight
    end
    if normalized < -boundary and normalized >= -3 * boundary then
        return JumpDirection.ForwardLeft
    end
    if normalized < -3 * boundary and normalized >= -5 * boundary then
        return JumpDirection.Left
    end
    if normalized < -5 * boundary and normalized >= -7 * boundary then
        return JumpDirection.BackLeft
    end

    return JumpDirection.Back
end

---读取或计算跳跃起始动画，字段缺失时遵循函数内的明确回退规则。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetJumpStartAnimation(direction)
    if direction == JumpDirection.Back then
        return JumpAnim.Jump_Start_Back
    elseif direction == JumpDirection.Left then
        return JumpAnim.Jump_Start_Left
    elseif direction == JumpDirection.Right then
        return JumpAnim.Jump_Start_Right
    elseif direction == JumpDirection.ForwardLeft then
        return JumpAnim.Jump_Start_ForwardLeft
    elseif direction == JumpDirection.ForwardRight then
        return JumpAnim.Jump_Start_ForwardRight
    elseif direction == JumpDirection.BackLeft then
        return JumpAnim.Jump_Start_BackLeft
    elseif direction == JumpDirection.BackRight then
        return JumpAnim.Jump_Start_BackRight
    end

    return JumpAnim.Jump_Start_Forward
end


---读取或计算跳跃inair动画，字段缺失时遵循函数内的明确回退规则。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetJumpInAirAnimation(direction)
    if direction == JumpDirection.Back then
        return JumpAnim.Jump_InAir_Back
    elseif direction == JumpDirection.Left then
        return JumpAnim.Jump_InAir_Left
    elseif direction == JumpDirection.Right then
        return JumpAnim.Jump_InAir_Right
    elseif direction == JumpDirection.ForwardLeft then
        return JumpAnim.Jump_InAir_ForwardLeft
    elseif direction == JumpDirection.ForwardRight then
        return JumpAnim.Jump_InAir_ForwardRight
    elseif direction == JumpDirection.BackLeft then
        return JumpAnim.Jump_InAir_BackLeft
    elseif direction == JumpDirection.BackRight then
        return JumpAnim.Jump_InAir_BackRight
    end

    return JumpAnim.Jump_InAir_Forward
end

---读取或计算跳跃落地动画，字段缺失时遵循函数内的明确回退规则。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function GroundLocomotion:GetJumpLandAnimation(direction)
    if direction == JumpDirection.Back then
        return JumpAnim.Jump_Land_Back
    elseif direction == JumpDirection.Left then
        return JumpAnim.Jump_Land_Left
    elseif direction == JumpDirection.Right then
        return JumpAnim.Jump_Land_Right
    elseif direction == JumpDirection.ForwardLeft then
        return JumpAnim.Jump_Land_ForwardLeft
    elseif direction == JumpDirection.ForwardRight then
        return JumpAnim.Jump_Land_ForwardRight
    elseif direction == JumpDirection.BackLeft then
        return JumpAnim.Jump_Land_BackLeft
    elseif direction == JumpDirection.BackRight then
        return JumpAnim.Jump_Land_BackRight
    end

    return JumpAnim.Jump_Land_Forward
end

---判断角色是否已进入 CharacterMovement 空中状态。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsAirborne()
    return self.bIsInAir == true or self.MovementState == "InAir"
end

---判断跳跃状态是否满足当前业务条件。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsJumpState(state_name)
    local branch = self:SplitBranchState(state_name)
    return branch == Branch.Jump
end

---进入 Jump 时一次性冻结起跳姿态、锁定模式和方向资源。
---无移动输入使用 Stand/Crouch 原地起跳；有输入时，非锁定固定使用 Forward，锁定按 MoveDirectionAngle 选择八方向。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:PlanJumpMotion()
    local jump_locked_on = self.bIsLockedOn == true or self.RotationMode == "LookingDirection"
    local jump_directional = self:HasMovementInput()
    local jump_motion_target_angle = normalize_angle(self.MoveDirectionAngle or 0)
    local jump_direction = JumpDirection.Forward
    if jump_locked_on and jump_directional then
        jump_direction = self:ClassifyJumpDirection(jump_motion_target_angle)
    end

    self.ActiveLocomotionMode = nil
    self:ClearTransientMotionPlan()
    self.JumpLockedOn = jump_locked_on
    self.JumpWasDirectional = jump_directional
    self.JumpStartedCrouched = self.bJumpStartedCrouched == true
    self.JumpDirection = jump_direction
    self.JumpTargetWorldYaw = jump_locked_on
        and normalize_angle(self.ActorYaw or 0)
        or normalize_angle(self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw or 0)
    self.JumpMotionTargetAngle = jump_motion_target_angle
    self.JumpMinimumVerticalVelocity = math.min(self.VerticalVelocity or 0, 0)
    self.JumpAirTime = 0

    if jump_directional then
        self.JumpStartAnimation = self:GetJumpStartAnimation(jump_direction)
        self.JumpDirectionalInAirAnimation = self:GetJumpInAirAnimation(jump_direction)
        self.JumpDirectionalLandAnimation = self:GetJumpLandAnimation(jump_direction)
    else
        self.JumpStartAnimation = self.JumpStartedCrouched
            and JumpAnim.Crouch_Jump_Start
            or JumpAnim.Stand_Jump_Start
    end

    self:LogDebug("JumpPlan", string.format(
        "locked=%s directional=%s direction=%s crouchStart=%s moveAngle=%.1f verticalVelocity=%.1f start=%s inAir=%s land=%s",
        tostring(self.JumpLockedOn),
        tostring(self.JumpWasDirectional),
        tostring(self.JumpDirection),
        tostring(self.JumpStartedCrouched),
        self.MoveDirectionAngle or 0,
        self.VerticalVelocity or 0,
        tostring(self:GetAnimationName(self.JumpStartAnimation)),
        tostring(self:GetAnimationName(self.JumpDirectionalInAirAnimation)),
        tostring(self:GetAnimationName(self.JumpDirectionalLandAnimation))))
end

---每个空中状态都更新本次跳跃的最低垂直速度；落地首帧 Velocity.Z 已归零，必须使用这个缓存判断轻重落地。
---@return nil 该函数只累计本次跳跃的空中时间和最低垂直速度。
function GroundLocomotion:UpdateJumpAirTelemetry()
    if not self:IsAirborne() then
        return
    end

    self.JumpAirTime = (self.JumpAirTime or 0) + (self.DeltaSeconds or 0)
    self.JumpMinimumVerticalVelocity = math.min(
        self.JumpMinimumVerticalVelocity or 0,
        self.VerticalVelocity or 0)
end

---应用跳跃rotationpolicy，只修改当前实例或对应 C++ 策略。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:ApplyJumpRotationPolicy()
    if self.JumpLockedOn then
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.Ignore,
            self.JumpTargetWorldYaw or self.ActorYaw,
            0)
    else
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.JumpTargetWorldYaw or self.ActorYaw,
            Tuning.JumpTurnMaxYawRate)
    end
end

---为 JumpStart 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpStart 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpStart()
    if self.EvaluatingResetTime == true or self.JumpStartAnimation == nil then
        self:PlanJumpMotion()
    end

    self:UpdateJumpAirTelemetry()
    self:ApplyJumpRotationPolicy()
    self:ApplyJumpDirectionWarp(true)
    local player = self:GetSequencePlayer(State.JumpStart)
    player.Sequence = self.JumpStartAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpBlendTime
    return player
end

---为 JumpDirectionalInAir 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpDirectionalInAir 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpDirectionalInAir()
    self:UpdateJumpAirTelemetry()
    -- InAir 原始资产没有 RootMotion；Lua 只提交 Pose，位移、重力和 AirControl 均由 CharacterMovement 计算。
    self:ApplyJumpRotationPolicy()
    self:ApplyJumpDirectionWarp(false)
    local player = self:GetSequencePlayer(State.JumpDirectionalInAir)
    player.Sequence = self.JumpDirectionalInAirAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpBlendTime
    return player
end

---为 JumpInAir 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpInAir 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpInAir()
    if self.JumpTargetWorldYaw == nil then
        self:PlanJumpMotion()
        self.JumpWasDirectional = false
    end

    self:UpdateJumpAirTelemetry()
    self:ApplyJumpRotationPolicy()
    self:ClearOrientationWarpingPolicy()
    local player = self:GetSequencePlayer(State.JumpInAir)
    player.Sequence = JumpAnim.Jump_Loop
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = true
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpBlendTime
    return player
end

---为 JumpDirectionalLand 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpDirectionalLand 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpDirectionalLand()
    self:ApplyJumpRotationPolicy()
    self:ApplyJumpDirectionWarp(true)
    -- Land 原始资产带 RootMotion，资源在触地首帧按物理速度方向冻结，播放期间不再随输入更换。
    local player = self:GetSequencePlayer(State.JumpDirectionalLand)
    player.Sequence = self.JumpDirectionalLandAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpLandBlendTime
    return player
end

---为 JumpLightLand 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpLightLand 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpLightLand()
    self:ClearOrientationWarpingPolicy()
    self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    local player = self:GetSequencePlayer(State.JumpLightLand)
    player.Sequence = JumpAnim.Jump_Light_Stand
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpLandBlendTime
    return player
end

---为 JumpHeavyLand 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source JumpHeavyLand 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_JumpHeavyLand()
    self:ClearOrientationWarpingPolicy()
    self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    -- 重落地硬直较长；期间按下蹲姿立即改用对应的 Crouch 版本，结束后自然进入 Crouch 子状态机。
    self.JumpLandAnimation = self:IsCrouchStance()
        and JumpAnim.Jump_Heavy_Crouch
        or JumpAnim.Jump_Heavy_Stand
    local player = self:GetSequencePlayer(State.JumpHeavyLand)
    player.Sequence = self.JumpLandAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.JumpLandBlendTime
    return player
end

---Start 进入时锁定朝向模式、方向和资源；锁定环绕不因播放期间输入改变而替换一次性动画。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function GroundLocomotion:PlanStartMotion()
    local mode = self:GetDesiredLocomotionMode()
    local locked_movement = self:IsLockedMovementMode(mode)
    local direction = locked_movement
        and self:ResolveLockedMoveDirection()
        or TurnDirection.Forward
    self.LockedMotionGait = mode
    self.LockedMotionDirection = direction
    self.LockedMotionFacingMode = locked_movement and "LockOn" or "Free"
    self.LockedMotionAnimation = self:GetStartAnimation(mode, direction)
    if locked_movement then
        self.LockedMotionSourceAngle = LockedDirectionAngle[direction] or 0
        self.LockedMotionTargetAngle = normalize_angle(self.MoveDirectionAngle or self.LockedMotionSourceAngle)
    end
    self.ActiveLocomotionMode = mode
    self.StopReason = nil
    self.SprintLargeTurnRequestTime = 0
    self:LogDebug("MotionPlan", string.format(
        "state=Start mode=%s facingMode=%s direction=%s animation=%s",
        tostring(mode),
        tostring(self.LockedMotionFacingMode),
        tostring(direction),
        tostring(self:GetAnimationName(self.LockedMotionAnimation))))
end

---为 Idle 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source Idle 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Idle()
    self:ClearOrientationWarpingPolicy()
    if not self:IsLockedMovementMode(Mode.Run) and self.bHasDesiredMoveYaw == true then
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.DesiredMoveYaw,
            Tuning.RunSteerMaxYawRate)
    else
        self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    end
    self.ActiveLocomotionMode = Mode.Idle
    self:ClearTransientMotionPlan()
    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = self:GetActiveWalkRunStateMachine():GetIdleAnimation()
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = nil
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.IdleBlendTime
    return player
end

---为 Start 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source Start 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Start()
    if self.EvaluatingResetTime == true or self.LockedMotionAnimation == nil then
        self:ClearTransientMotionPlan()
        self:PlanStartMotion()
    end

    local mode = self.LockedMotionGait or self:GetDesiredLocomotionMode()
    if self.LockedMotionFacingMode == "LockOn" then
        self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
        self:ApplyLockedDirectionWarp(
            self.LockedMotionDirection,
            self.LockedMotionTargetAngle,
            Tuning.LockedOrientationWarpAlpha,
            true)
    else
        self:ClearOrientationWarpingPolicy()
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw,
            self:GetRootMotionYawRate(mode, RootMotionRotationMode.SteerToTarget))
    end
    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = self.LockedMotionAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.StartBlendTime
    return player
end

---Cycle 表现稳定移动。Walk、Run、Sprint 都直接播放明确 Sequence，不再采样速度 BlendSpace。
---Walk/Run 循环互切时保持 MovePhase；第一阶段使用 Pose 交叉混合承接相位匹配后的新播放器。
---@return LuaSequencePlayer|nil pose_source Cycle 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Cycle()
    local entering_cycle = self.EvaluatingResetTime == true
    local transition_phase = self.PendingMovePhase
    local transition_reference_time = self.PendingMovePhaseReferenceTime
    if self.EvaluatingResetTime == true then
        local entering_locked_direction = self.LockedMotionDirection
        self.MoveInputLostDuration = nil
        self.MoveInputLostObservedTime = nil
        if self.LockedMotionGait ~= nil then
            self.ActiveLocomotionMode = self.LockedMotionGait
        else
            self.ActiveLocomotionMode = self:GetDesiredLocomotionMode()
        end
        self.ActiveLockedDirection = entering_locked_direction
        self.LockedMotionAnimation = nil
        self.LockedMotionGait = nil
        self.LockedMotionDirection = nil
        self.LockedMotionFacingMode = nil
    end

    local desired_mode = self:GetDesiredLocomotionMode()
    local mode = self.ActiveLocomotionMode or desired_mode
    if mode ~= Mode.Sprint and desired_mode ~= Mode.Sprint then
        mode = desired_mode
        self.ActiveLocomotionMode = mode
    end

    if mode == Mode.Sprint then
        self.ActiveLockedDirection = nil
        self:ClearOrientationWarpingPolicy()
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw,
            self:GetRootMotionYawRate(mode, RootMotionRotationMode.SteerToTarget))
        local start_position = nil
        if entering_cycle then
            start_position = self:ResolveMatchedMovePhasePosition(
                Anim.Sprint_Forward_Loop,
                0,
                transition_phase,
                transition_reference_time,
                "EnterSprintCycle")
        end
        self.PendingMovePhase = nil
        self.PendingMovePhaseReferenceTime = nil
        self.CurrentCycleAnimation = Anim.Sprint_Forward_Loop
        self.CurrentCycleMode = Mode.Sprint
        local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
        player.Sequence = Anim.Sprint_Forward_Loop
        player.AnimationName = nil
        player.PlayRate = 1.0
        player.bLoop = true
        player.bResetTime = nil
        player.StartPosition = start_position
        player.BlendTime = Tuning.CycleBlendTime
        return player
    end

    local locked_movement = self:IsLockedMovementMode(mode)
    local direction = locked_movement and self:ResolveLockedMoveDirection() or TurnDirection.Forward
    local cycle_animation = self:GetActiveWalkRunStateMachine():GetCycleAnimation(mode, direction)
    local previous_mode = self.CurrentCycleMode
    local animation_changed = self.CurrentCycleAnimation ~= nil
        and self.CurrentCycleAnimation ~= cycle_animation
    local walk_run_gait_changed = not entering_cycle
        and animation_changed
        and (previous_mode == Mode.Walk or previous_mode == Mode.Run)
        and (mode == Mode.Walk or mode == Mode.Run)
        and previous_mode ~= mode
    local source_phase = transition_phase
    local reference_time = transition_reference_time
    if not entering_cycle and animation_changed then
        source_phase = self:GetCurrentMovePhase()
        reference_time = self:GetNormalizedTime()
    end
    local start_position = nil
    if entering_cycle or animation_changed then
        start_position = self:ResolveMatchedMovePhasePosition(
            cycle_animation,
            0,
            source_phase,
            reference_time,
            entering_cycle and "EnterWalkRunCycle" or "SwitchWalkRunCycle")
    end

    if self.ActiveLockedDirection ~= direction or animation_changed then
        self:LogDebug("CycleSequence", string.format(
            "mode=%s previousMode=%s direction=%s animation=%s changed=%s inertialization=%s phaseMatched=%s",
            tostring(mode),
            tostring(previous_mode),
            tostring(direction),
            tostring(self:GetAnimationName(cycle_animation)),
            tostring(animation_changed),
            tostring(walk_run_gait_changed),
            tostring(start_position ~= nil)))
    end

    self.ActiveLockedDirection = locked_movement and direction or nil
    if locked_movement then
        self.LockedMotionSourceAngle = LockedDirectionAngle[direction] or 0
        self.LockedMotionTargetAngle = normalize_angle(self.MoveDirectionAngle or self.LockedMotionSourceAngle)
        self:ApplyLockedDirectionWarp(
            direction,
            self.LockedMotionTargetAngle,
            Tuning.LockedOrientationWarpAlpha,
            true)
    else
        self.LockedMotionSourceAngle = nil
        self.LockedMotionTargetAngle = nil
        self:ClearOrientationWarpingPolicy()
    end
    self.CurrentCycleAnimation = cycle_animation
    self.CurrentCycleMode = mode
    self.PendingMovePhase = nil
    self.PendingMovePhaseReferenceTime = nil

    if locked_movement then
        self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    else
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw,
            self:GetRootMotionYawRate(mode, RootMotionRotationMode.SteerToTarget))
    end

    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = cycle_animation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = true
    player.bResetTime = nil
    player.StartPosition = start_position
    player.BlendTime = walk_run_gait_changed
        and Tuning.WalkRunInertialBlendTime
        or (locked_movement and Tuning.LockedDirectionBlendTime or Tuning.CycleBlendTime)
    return player
end

---Turn 动画负责身体姿势，Movement 负责角色真实旋转；二者不会叠加根旋转。
---动画在进入状态时按方向冻结，目标 Yaw 每帧更新；短促输入消失后回 Idle，持续输入随后进入 Start。
---@return LuaSequencePlayer|nil pose_source Turn 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Turn()
    self:ClearOrientationWarpingPolicy()
    if self.EvaluatingResetTime == true or self.ProgramTurnAnimation == nil then
        self.ProgramTurnAnimation, self.ProgramTurnDirection = self:ResolveIdleTurnAnimation(
            self.MoveDirectionAngle or 0)
        self:LogDebug("ProgramTurn", string.format(
            "phase=PoseSelected angle=%.1f direction=%s animation=%s yawOwner=Movement",
            self.MoveDirectionAngle or 0,
            tostring(self.ProgramTurnDirection),
            tostring(self:GetAnimationName(self.ProgramTurnAnimation))))
    end

    self.ActiveLocomotionMode = Mode.Idle
    self:SetRootMotionRotationPolicy(
        RootMotionRotationMode.SteerToTarget,
        self.bHasDesiredMoveYaw == true and self.DesiredMoveYaw or self.ActorYaw,
        Tuning.RunTurnMaxYawRate)
    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = self.ProgramTurnAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.TurnBlendTime
    return player
end

---为 Stop 状态配置并返回持久 SequencePlayer Pose 节点。
---@return LuaSequencePlayer|nil pose_source Stop 状态的根 Pose；配置失败时为 nil。
function GroundLocomotion:UpdateAnimation_Stop()
    if self.EvaluatingResetTime == true or self.LockedMotionAnimation == nil then
        local mode = self:GetActiveLocomotionMode()
        local locked_movement = self:IsLockedMovementMode(mode)
        local direction = locked_movement
            and (self.ActiveLockedDirection or self:ResolveLockedMoveDirection())
            or TurnDirection.Forward
        self.LockedMotionGait = mode
        self.LockedMotionDirection = direction
        self.LockedMotionFacingMode = locked_movement and "LockOn" or "Free"
        self.LockedMotionAnimation = self:GetStopAnimation(mode, direction)
        if locked_movement then
            self.LockedMotionSourceAngle = LockedDirectionAngle[direction] or 0
            self.LockedMotionTargetAngle = normalize_angle(
                self.LockedMotionTargetAngle or self.MoveDirectionAngle or self.LockedMotionSourceAngle)
        end
        self:LogDebug("MotionPlan", string.format(
            "state=Stop mode=%s facingMode=%s direction=%s animation=%s",
            tostring(mode),
            tostring(self.LockedMotionFacingMode),
            tostring(direction),
            tostring(self:GetAnimationName(self.LockedMotionAnimation))))
    end

    if self.StopReason == "SprintRedirect" and self.bHasDesiredMoveYaw == true then
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.DesiredMoveYaw,
            Tuning.SprintRedirectMaxYawRate)
    elseif self.LockedMotionFacingMode == "LockOn" then
        self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    elseif self.bHasDesiredMoveYaw == true then
        self:SetRootMotionRotationPolicy(
            RootMotionRotationMode.SteerToTarget,
            self.DesiredMoveYaw,
            self:GetRootMotionYawRate(self.LockedMotionGait, RootMotionRotationMode.SteerToTarget))
    else
        self:SetRootMotionRotationPolicy(RootMotionRotationMode.Ignore, self.ActorYaw, 0)
    end
    if self.LockedMotionFacingMode == "LockOn" then
        self:ApplyLockedDirectionWarp(
            self.LockedMotionDirection,
            self.LockedMotionTargetAngle,
            Tuning.LockedOrientationWarpAlpha * self:GetLockedStopWarpAlpha(),
            true)
    else
        self:ClearOrientationWarpingPolicy()
    end
    local player = self:GetSequencePlayer(self:GetEvaluatingStateName())
    player.Sequence = self.LockedMotionAnimation
    player.AnimationName = nil
    player.PlayRate = 1.0
    player.bLoop = false
    player.bResetTime = nil
    player.StartPosition = nil
    player.BlendTime = Tuning.StopBlendTime
    return player
end

---把复合状态分派给共享的本地状态动画函数，并原样返回该状态生成的根 Pose。
---Jump 使用独立的 UpdateAnimation_JumpXxx，避免与 Standing/Crouch/Sprint 的 Start 等本地状态重名。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return LuaPoseLinkBase|LuaPoseLink|nil pose_source 当前状态生成的根 Pose；状态或更新函数无效时为 nil。
function GroundLocomotion:UpdateAnimation(state_name, facts)
    if state_name == State.Step then
        self.ActiveLocomotionBranch = Branch.Standing
        return BaseStateMachine.UpdateAnimation(self, state_name, facts)
    end

    local branch, local_state = self:SplitBranchState(state_name)
    local updater = branch == Branch.Jump
        and self["UpdateAnimation_Jump" .. local_state]
        or self["UpdateAnimation_" .. local_state]
    if branch == nil or type(updater) ~= "function" then
        self:LogDebug("Hierarchy", string.format(
            "invalid composite state=%s; returning to entry",
            tostring(state_name)))
        return nil
    end

    self.ActiveLocomotionBranch = branch
    self.EvaluatingStateName = state_name
    local animation_node = updater(self, facts)
    self.EvaluatingStateName = nil
    return self:LinkStatePose(state_name, animation_node)
end

---首次创建 Pose 时按物理状态选择入口；角色已在空中时不能先闪一帧地面 Idle。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return string|table|nil selection 按优先级选出的状态、资源或对象。
function GroundLocomotion:SelectEntryState(facts)
    local entry_state = self:MakeBranchState(self:GetDesiredWalkRunBranch(), LocalState.Idle)
    if self:IsAirborne() then
        entry_state = (self.VerticalVelocity or 0) > Tuning.JumpStartMinimumVerticalVelocity
            and State.JumpStart
            or State.JumpInAir
    end
    self:LogDebugFlag("DebugTransitions", "Entry", string.format(
        "entry=%s hasPose=%s",
        tostring(entry_state),
        tostring(facts and facts.HasPose)))
    return self:Enter(entry_state, facts)
end

---进入局部状态并构造相应状态决策。
---@param branch string 顶层状态机分支名称，例如 Standing、Crouch、Sprint 或 Jump。
---@param local_state string 复合状态中的局部状态名称，例如 Idle、Start 或 Cycle。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:EnterLocalState(branch, local_state, facts)
    return self:Enter(self:MakeBranchState(branch, local_state), facts)
end

---Standing/Crouch 两个子状态机共享同一套曲线门控，只替换姿态专属动画资源。
---@param branch string 顶层状态机分支名称，例如 Standing、Crouch、Sprint 或 Jump。
---@param local_state string 复合状态中的局部状态名称，例如 Idle、Start 或 Cycle。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:UpdateWalkRunBranch(branch, local_state, facts)
    if local_state == LocalState.Idle then
        if self:CanEnter_Idle_Turn() then
            return self:EnterLocalState(branch, LocalState.Turn, facts)
        end
        if self:CanEnter_Idle_Start() then
            return self:EnterLocalState(branch, LocalState.Start, facts)
        end
    elseif local_state == LocalState.Start then
        if self:CanEnter_Start_Stop() then
            return self:EnterLocalState(branch, LocalState.Stop, facts)
        end
        if self:CanEnter_Start_Cycle() then
            return self:EnterLocalState(branch, LocalState.Cycle, facts)
        end
    elseif local_state == LocalState.Cycle then
        if self:CanEnter_Cycle_Stop() then
            return self:EnterLocalState(branch, LocalState.Stop, facts)
        end
    elseif local_state == LocalState.Turn then
        if self:CanEnter_Turn_Idle() then
            return self:EnterLocalState(branch, LocalState.Idle, facts)
        end
        if self:CanEnter_Turn_Start() then
            return self:EnterLocalState(branch, LocalState.Start, facts)
        end
    elseif local_state == LocalState.Stop then
        if self:CanEnter_Stop_Start() then
            return self:EnterLocalState(branch, LocalState.Start, facts)
        end
        if self:CanEnter_Stop_Idle() then
            return self:EnterLocalState(branch, LocalState.Idle, facts)
        end
    else
        return self:EnterLocalState(branch, LocalState.Idle, facts)
    end

    return self:Keep(self:MakeBranchState(branch, local_state), facts)
end

---Sprint 是顶层独立分支，不属于 Standing/Crouch 子状态机。
---退出 Sprint 后只回到当帧姿态分支；正常输入层会在 Step/Sprint 开始前解除蹲姿。
---@param local_state string 复合状态中的局部状态名称，例如 Idle、Start 或 Cycle。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:UpdateSprintBranch(local_state, facts)
    if local_state == LocalState.Start then
        if self:CanEnter_Start_Stop() then
            return self:EnterLocalState(Branch.Sprint, LocalState.Stop, facts)
        end
        if self:CanEnter_Start_Cycle() then
            return self:EnterLocalState(Branch.Sprint, LocalState.Cycle, facts)
        end
    elseif local_state == LocalState.Cycle then
        if self:CanEnter_Cycle_Stop() then
            return self:EnterLocalState(Branch.Sprint, LocalState.Stop, facts)
        end
    elseif local_state == LocalState.Stop then
        if self:CanEnter_Stop_Start() then
            if self:GetDesiredLocomotionMode() == Mode.Sprint then
                return self:EnterLocalState(Branch.Sprint, LocalState.Start, facts)
            end
            return self:EnterLocalState(self:GetDesiredWalkRunBranch(), LocalState.Start, facts)
        end
        if self:CanEnter_Stop_Idle() then
            return self:EnterLocalState(self:GetDesiredWalkRunBranch(), LocalState.Idle, facts)
        end
    else
        return self:EnterLocalState(Branch.Sprint, LocalState.Stop, facts)
    end

    return self:Keep(self:MakeBranchState(Branch.Sprint, local_state), facts)
end

---方向跳在专属 InAir 结束前落地时使用同方向 Land；转入通用 Jump_Loop 后按缓存的最低下落速度选择轻/重落地。
---@param use_directional_land boolean 是否保留方向落地资源。
---@param facts table 当前状态机事实。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:EnterJumpLanding(use_directional_land, facts)
    if use_directional_land and self.JumpDirectionalLandAnimation ~= nil then
        local land_direction = self.JumpLockedOn
            and self:ClassifyJumpDirection(self.Angle or self.MoveDirectionAngle or 0)
            or JumpDirection.Forward
        self.JumpDirection = land_direction
        self.JumpMotionTargetAngle = normalize_angle(self.Angle or self.MoveDirectionAngle or 0)
        self.JumpDirectionalLandAnimation = self:GetJumpLandAnimation(land_direction)
        self:LogDebug("JumpLand", string.format(
            "type=Directional direction=%s minimumVerticalVelocity=%.1f animation=%s rootMotion=true",
            tostring(self.JumpDirection),
            self.JumpMinimumVerticalVelocity or 0,
            tostring(self:GetAnimationName(self.JumpDirectionalLandAnimation))))
        return self:Enter(State.JumpDirectionalLand, facts)
    end

    if (self.JumpMinimumVerticalVelocity or 0) <= Tuning.JumpHeavyLandingVelocityThreshold then
        self:LogDebug("JumpLand", string.format(
            "type=Heavy minimumVerticalVelocity=%.1f threshold=%.1f crouching=%s",
            self.JumpMinimumVerticalVelocity or 0,
            Tuning.JumpHeavyLandingVelocityThreshold,
            tostring(self:IsCrouchStance())))
        return self:Enter(State.JumpHeavyLand, facts)
    end

    self:LogDebug("JumpLand", string.format(
        "type=Light minimumVerticalVelocity=%.1f threshold=%.1f",
        self.JumpMinimumVerticalVelocity or 0,
        Tuning.JumpHeavyLandingVelocityThreshold))
    return self:Enter(State.JumpLightLand, facts)
end

---退出跳跃toground并选择后续合法状态。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:ExitJumpToGround(facts)
    local desired_branch = self:GetDesiredWalkRunBranch()
    if self:WantsMove() then
        if self:GetDesiredLocomotionMode() == Mode.Sprint then
            return self:EnterLocalState(Branch.Sprint, LocalState.Start, facts)
        end

        return self:EnterLocalState(desired_branch, LocalState.Start, facts)
    end

    return self:EnterLocalState(desired_branch, LocalState.Idle, facts)
end

---Jump 顶层流程：Start -> DirectionalInAir/通用 InAir -> DirectionalLand/LightLand/HeavyLand -> 地面分支。
---Transition 由真实 bIsInAir 决定落地，归一化时间只用于单次起跳和方向滞空动画播完后的阶段推进。
---@param local_state string 复合状态中的局部状态名称，例如 Idle、Start 或 Cycle。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:UpdateJumpBranch(local_state, facts)
    if local_state == JumpLocalState.Start then
        if not self:IsAirborne() then
            return self:EnterJumpLanding(self.JumpWasDirectional == true, facts)
        end
        if self:GetNormalizedTime() >= Tuning.JumpStartExitNormalizedTime then
            if self.JumpWasDirectional then
                return self:Enter(State.JumpDirectionalInAir, facts)
            end
            return self:Enter(State.JumpInAir, facts)
        end
    elseif local_state == JumpLocalState.DirectionalInAir then
        if not self:IsAirborne() then
            return self:EnterJumpLanding(true, facts)
        end
        if (self.JumpAirTime or 0) >= Tuning.JumpDirectionalMaximumAirTime then
            return self:Enter(State.JumpInAir, facts)
        end
    elseif local_state == JumpLocalState.InAir then
        if not self:IsAirborne() then
            return self:EnterJumpLanding(false, facts)
        end
    elseif local_state == JumpLocalState.DirectionalLand then
        if self:IsAirborne() then
            return self:Enter(State.JumpStart, facts)
        end
        if self:GetNormalizedTime() >= Tuning.JumpDirectionalLandExitNormalizedTime then
            return self:ExitJumpToGround(facts)
        end
    elseif local_state == JumpLocalState.LightLand then
        if self:IsAirborne() then
            return self:Enter(State.JumpStart, facts)
        end
        if self:GetNormalizedTime() >= Tuning.JumpLightLandExitNormalizedTime then
            return self:ExitJumpToGround(facts)
        end
    elseif local_state == JumpLocalState.HeavyLand then
        if self:IsAirborne() then
            return self:Enter(State.JumpStart, facts)
        end
        if self:GetNormalizedTime() >= Tuning.JumpHeavyLandExitNormalizedTime then
            return self:ExitJumpToGround(facts)
        end
    else
        return self:IsAirborne()
            and self:Enter(State.JumpInAir, facts)
            or self:ExitJumpToGround(facts)
    end

    return self:Keep(self:MakeBranchState(Branch.Jump, local_state), facts)
end

---顶层状态调度顺序：Jump 物理状态 -> Step 抢占 -> 姿态分支切换 -> Sprint -> 子状态机内部转移。
---复合状态由这里显式调度，避免基类把状态名中的层级分隔符误当作普通 Transition 函数名。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table decision 本帧应保持或进入的状态及姿势决策。
function GroundLocomotion:UpdateState(facts)
    local state_name = self:NormalizeState(facts and facts.State or nil)
    local branch, local_state = self:SplitBranchState(state_name)
    self.ActiveLocomotionBranch = branch or Branch.Standing

    if branch == Branch.Jump then
        return self:UpdateJumpBranch(local_state, facts)
    end

    if self:IsAirborne() then
        if (self.VerticalVelocity or 0) > Tuning.JumpStartMinimumVerticalVelocity then
            return self:Enter(State.JumpStart, facts)
        end
        return self:Enter(State.JumpInAir, facts)
    end

    if state_name ~= State.Step and self:WantsStep() then
        return self:Enter(State.Step, facts)
    end

    if state_name == State.Step then
        if not self:IsStepReadyToExit() then
            return self:Keep(State.Step, facts)
        end

        if self:WantsMove() then
            if self:GetDesiredLocomotionMode() == Mode.Sprint then
                return self:EnterLocalState(Branch.Sprint, LocalState.Start, facts)
            end

            self:CaptureTransitionMovePhase("Step->WalkRunCycle")
            return self:EnterLocalState(
                self:GetDesiredWalkRunBranch(),
                LocalState.Cycle,
                facts)
        end

        return self:EnterLocalState(self:GetDesiredWalkRunBranch(), LocalState.Idle, facts)
    end

    if branch == Branch.Sprint then
        return self:UpdateSprintBranch(local_state, facts)
    end

    local desired_branch = self:GetDesiredWalkRunBranch()
    if branch ~= desired_branch then
        local target_local_state = LocalState.Idle
        if self:WantsMove() then
            target_local_state = local_state == LocalState.Cycle
                and LocalState.Cycle
                or LocalState.Start
            if target_local_state == LocalState.Cycle then
                self:CaptureTransitionMovePhase(string.format(
                    "%s.%s->%s.%s",
                    tostring(branch),
                    tostring(local_state),
                    tostring(desired_branch),
                    tostring(target_local_state)))
            end
        end

        self:LogDebug("Hierarchy", string.format(
            "stance branch %s -> %s local=%s targetLocal=%s moving=%s",
            tostring(branch),
            tostring(desired_branch),
            tostring(local_state),
            tostring(target_local_state),
            tostring(self:WantsMove())))
        return self:EnterLocalState(desired_branch, target_local_state, facts)
    end

    if self:WantsMove() and self:GetDesiredLocomotionMode() == Mode.Sprint then
        return self:EnterLocalState(Branch.Sprint, LocalState.Start, facts)
    end

    return self:UpdateWalkRunBranch(branch, local_state, facts)
end

---非锁定 Idle 遇到大角度移动输入时先保持 Idle 姿势执行程序转向。
---锁定朝向由 Camera/Movement 持续维护，不进入 Turn；小角度输入直接进入 Start。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Idle_Turn()
    if not self:WantsMove() or self:IsLockedMovementMode(self:GetDesiredLocomotionMode()) then
        return false
    end

    local angle = math.abs(normalize_angle(self.MoveDirectionAngle or 0))
    if angle < Tuning.IdleTurnEnterAngle then
        return false
    end

    self:LogDebug("ProgramTurn", string.format(
        "phase=IdleEnter angle=%.1f targetYaw=%.1f pose=Idle",
        angle,
        self.DesiredMoveYaw or self.ActorYaw or 0))
    return true
end

---判断状态机是否允许从 Idle 切换到 Start，并执行该转换需要的门控副作用。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Idle_Start()
    return self:WantsMove()
end

---判断状态机是否允许从 Start 切换到 Stop，并执行该转换需要的门控副作用。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Start_Stop()
    local sprint_cancelled = self.ActiveLocomotionBranch == Branch.Sprint
        and self:GetDesiredLocomotionMode() ~= Mode.Sprint
    if self:WantsMove() and not sprint_cancelled then
        self.StartStopRequestedAt = nil
        return false
    end

    if self.StartStopRequestedAt == nil then
        self.StartStopRequestedAt = self.StateTime or 0
    end

    local curve_value = self:GetCurrentCurveValue(Curve.CanEnterStop, 0)
    if curve_value >= 0.5 then
        self.StopReason = sprint_cancelled and "SprintExit" or "StartCancelled"
        return true
    end

    local wait_time = math.max((self.StateTime or 0) - self.StartStopRequestedAt, 0)
    local normalized_time = self:GetNormalizedTime()
    if wait_time < Tuning.StartStopCurveGateMaximumWaitTime and normalized_time < 0.999 then
        return false
    end

    -- 正常路径必须由 CanEnterStop 开窗；这里只处理曲线缺失或动画已结束，防止一次短输入把状态机永久留在 Start。
    self:LogDebug("CurveFallback", string.format(
        "transition=Start->Stop curve=%s value=%.3f waitTime=%.3f maximumWait=%.3f normalizedTime=%.3f animation=%s",
        tostring(Curve.CanEnterStop),
        curve_value,
        wait_time,
        Tuning.StartStopCurveGateMaximumWaitTime,
        normalized_time,
        tostring(self:GetAnimationName(self.LockedMotionAnimation))))
    self.StopReason = sprint_cancelled and "SprintExitFallback" or "StartCancelledFallback"
    return true
end

---Start 在移动意图持续存在时于尾段进入 Cycle，旧 Start 作为 Previous Pose 在混合期继续播放到末帧。
---进入过渡前捕获 MovePhase，使 Cycle 从同一脚步相位开始；最大时间仅防止异常资产卡死。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Start_Cycle()
    if not self:WantsMove() then
        return false
    end
    if self.ActiveLocomotionBranch == Branch.Sprint
        and self:GetDesiredLocomotionMode() ~= Mode.Sprint then
        return false
    end

    if self:IsTailBlendReady(Tuning.CycleBlendTime) then
        self:CaptureTransitionMovePhase("Start->Cycle")
        return true
    end

    if (self.StateTime or 0) < Tuning.StartMaximumPlayTime then
        return false
    end

    -- 只有动画时间无法正常推进时才允许超时退出，正常资产始终完整播放到末尾。
    self:LogDebug("AnimationFallback", string.format(
        "transition=Start->Cycle stateTime=%.3f normalizedTime=%.3f remainingTime=%.3f maximumTime=%.3f animation=%s",
        self.StateTime or 0,
        self:GetNormalizedTime(),
        self:GetCurrentAnimationRemainingTime(-1),
        Tuning.StartMaximumPlayTime,
        tostring(self:GetAnimationName(self.LockedMotionAnimation))))
    self:CaptureTransitionMovePhase("Start->CycleFallback")
    return true
end

---判断状态机是否允许从 Cycle 切换到 Stop，并执行该转换需要的门控副作用。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Cycle_Stop()
    local active_mode = self:GetActiveLocomotionMode()
    local desired_mode = self:GetDesiredLocomotionMode()

    -- Sprint 松开或失去 Sprint 资格时先播放前向 Stop，再按当前输入进入目标 Start/Idle。
    if active_mode == Mode.Sprint and desired_mode ~= Mode.Sprint then
        self.StopReason = "SprintExit"
        self.SprintLargeTurnRequestTime = 0
        return true
    end

    -- Sprint 在 90 度以内持续 Steer；超过大角度并稳定保持时才制动、程序转向、重新加速。
    if active_mode == Mode.Sprint and desired_mode == Mode.Sprint and self:WantsMove() then
        local angle = math.abs(normalize_angle(self.MoveDirectionAngle or 0))
        if angle >= Tuning.SprintLargeTurnEnterAngle then
            self.SprintLargeTurnRequestTime = (self.SprintLargeTurnRequestTime or 0)
                + (self.DeltaSeconds or 0)
            if self.SprintLargeTurnRequestTime >= Tuning.SprintLargeTurnRequestDuration then
                self.StopReason = "SprintRedirect"
                self.SprintLargeTurnRequestTime = 0
                self:LogDebug("SprintRedirect", string.format(
                    "phase=Stop angle=%.1f targetYaw=%.1f threshold=%.1f",
                    angle,
                    self.DesiredMoveYaw or self.ActorYaw or 0,
                    Tuning.SprintLargeTurnEnterAngle))
                return true
            end
        else
            self.SprintLargeTurnRequestTime = 0
        end
        return false
    end

    self.SprintLargeTurnRequestTime = 0
    -- 键盘在相反方向之间切换时可能短暂产生零输入；先保留 Cycle，让新方向直接恢复实时 Steer。
    if not self:IsMoveInputHandoffExpired() then
        return false
    end

    local curve_value = self:GetCurrentCurveValue(Curve.CanEnterStop, 0)
    if curve_value >= 0.5 then
        return true
    end
    local curve_wait_time = math.max(
        (self.MoveInputLostDuration or 0) - Tuning.MoveInputHandoffTime,
        0)
    if curve_wait_time < Tuning.CycleStopCurveGateMaximumWaitTime then
        return false
    end

    -- Loop 理应很快遇到下一段脚步窗口；超过完整等待上限说明样本曲线缺失或相位错误。
    self:LogDebug("CurveFallback", string.format(
        "transition=Cycle->Stop curve=%s value=%.3f waitTime=%.3f maximumWait=%.3f mode=%s animation=%s",
        tostring(Curve.CanEnterStop),
        curve_value,
        curve_wait_time,
        Tuning.CycleStopCurveGateMaximumWaitTime,
        tostring(self.CurrentCycleMode),
        tostring(self:GetAnimationName(self.CurrentCycleAnimation))))
    return true
end

---短促输入消失时结束程序原地转向并保持 Idle。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Turn_Idle()
    return not self:WantsMove()
end

---程序原地转向不会直接跳入循环；对齐或达到短上限后从 Start 建立移动重心。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Turn_Start()
    return self:WantsMove()
        and (math.abs(normalize_angle(self.MoveDirectionAngle or 0)) <= Tuning.IdleProgramTurnExitAngle
            or (self.StateTime or 0) >= Tuning.IdleProgramTurnMaximumTime)
end

---判断状态机是否允许从 Stop 切换到 Start，并执行该转换需要的门控副作用。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Stop_Start()
    if not self:WantsMove() then
        return false
    end

    if self.StopReason == "SprintRedirect" then
        if (self.StateTime or 0) < Tuning.SprintRedirectMinimumStopTime then
            return false
        end
        return math.abs(normalize_angle(self.MoveDirectionAngle or 0)) <= Tuning.SprintRedirectRestartAngle
            or (self.StateTime or 0) >= Tuning.SprintRedirectMaximumStopTime
    end

    return (self.StateTime or 0) >= Tuning.StopRestartMinTime
end

---Stop 在没有新移动输入时于尾段进入 Idle，旧 Stop 作为 Previous Pose 在混合期继续播放到末帧。
---Stop/Idle 不具有移动步态相位，直接用尾段 Pose 交叉混合；重新移动仍优先走 Stop->Start。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:CanEnter_Stop_Idle()
    if self:WantsMove() then
        return false
    end

    if self:IsTailBlendReady(Tuning.IdleBlendTime) then
        return true
    end

    if (self.StateTime or 0) < Tuning.StopMaximumPlayTime then
        return false
    end

    -- 只有动画时间无法正常推进时才允许超时退出，正常资产始终完整播放到末尾。
    self:LogDebug("AnimationFallback", string.format(
        "transition=Stop->Idle stateTime=%.3f normalizedTime=%.3f remainingTime=%.3f maximumTime=%.3f animation=%s",
        self.StateTime or 0,
        self:GetNormalizedTime(),
        self:GetCurrentAnimationRemainingTime(-1),
        Tuning.StopMaximumPlayTime,
        tostring(self:GetAnimationName(self.LockedMotionAnimation))))
    return true
end

---输入层的 DodgeActive 结束后才允许退出，确保短按确实完成一次垫步而不是一帧脉冲。
---正常按动画比例退出；MaximumPlayTime 只处理资源时间异常或曲线数据缺失。
---@return boolean matched 当前事实和门控条件是否满足。
function GroundLocomotion:IsStepReadyToExit()
    if self:WantsStep() or (self.StateTime or 0) < Tuning.StepExitMinTime then
        return false
    end
    if (self.StateTime or 0) >= Tuning.StepMaximumPlayTime then
        return true
    end

    return self:GetNormalizedTime() >= Tuning.StepExitNormalizedTime
end

return GroundLocomotion
