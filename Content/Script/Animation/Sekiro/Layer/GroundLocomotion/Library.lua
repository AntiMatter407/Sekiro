-- Sekiro 地面移动动画层配置。
-- 当前使用中的 Locomotion 动画不承担角色转向，真实朝向统一由 Movement 消费绝对目标 Yaw。
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "GroundLocomotion"

M.RootMotionRotationMode = {
    Ignore = "Ignore",
    Extract = "Extract",
    WarpToTarget = "WarpToTarget",
    SteerToTarget = "SteerToTarget",
}

M.LocomotionMode = {
    Idle = "Idle",
    Walk = "Walk",
    Run = "Run",
    Sprint = "Sprint",
}

-- GroundLocomotion 只向 C++ 提交一个 Pose，但内部使用复合状态名表达层级。
-- Standing/Crouch 各自复用 Idle/Turn/Start/Cycle/Stop，Sprint、Step 和 Jump 由顶层单独调度。
M.Branch = {
    Standing = "Standing",
    Crouch = "Crouch",
    Sprint = "Sprint",
    Jump = "Jump",
}

M.LocalState = {
    Idle = "Idle",
    Start = "Start",
    Cycle = "Cycle",
    Turn = "Turn",
    Stop = "Stop",
}

M.TurnDirection = {
    Forward = "Forward",
    Back = "Back",
    Left = "Left",
    Right = "Right",
}

M.JumpDirection = {
    Forward = "Forward",
    Back = "Back",
    Left = "Left",
    Right = "Right",
    ForwardLeft = "ForwardLeft",
    ForwardRight = "ForwardRight",
    BackLeft = "BackLeft",
    BackRight = "BackRight",
}

M.JumpLocalState = {
    Start = "Start",
    DirectionalInAir = "DirectionalInAir",
    InAir = "InAir",
    DirectionalLand = "DirectionalLand",
    LightLand = "LightLand",
    HeavyLand = "HeavyLand",
}

M.Curve = {
    CanEnterStop = "CanEnterStop",
    FootPlant = "FootPlant",
    MovePhase = "MovePhase",
}

M.State = {
    StandingIdle = "Standing.Idle",
    StandingTurn = "Standing.Turn",
    StandingStart = "Standing.Start",
    StandingCycle = "Standing.Cycle",
    StandingStop = "Standing.Stop",

    CrouchIdle = "Crouch.Idle",
    CrouchTurn = "Crouch.Turn",
    CrouchStart = "Crouch.Start",
    CrouchCycle = "Crouch.Cycle",
    CrouchStop = "Crouch.Stop",

    SprintStart = "Sprint.Start",
    SprintCycle = "Sprint.Cycle",
    SprintStop = "Sprint.Stop",

    Step = "Step",

    JumpStart = "Jump.Start",
    JumpDirectionalInAir = "Jump.DirectionalInAir",
    JumpInAir = "Jump.InAir",
    JumpDirectionalLand = "Jump.DirectionalLand",
    JumpLightLand = "Jump.LightLand",
    JumpHeavyLand = "Jump.HeavyLand",
}

M.StateList = {
    M.State.StandingIdle,
    M.State.StandingTurn,
    M.State.StandingStart,
    M.State.StandingCycle,
    M.State.StandingStop,
    M.State.CrouchIdle,
    M.State.CrouchTurn,
    M.State.CrouchStart,
    M.State.CrouchCycle,
    M.State.CrouchStop,
    M.State.SprintStart,
    M.State.SprintCycle,
    M.State.SprintStop,
    M.State.Step,
    M.State.JumpStart,
    M.State.JumpDirectionalInAir,
    M.State.JumpInAir,
    M.State.JumpDirectionalLand,
    M.State.JumpLightLand,
    M.State.JumpHeavyLand,
}

M.Tuning = {
    InputThreshold = 0.1, -- 小于该值时视为没有移动意图。

    LockedDirectionBoundaryAngle = 45, -- 锁定四方向的基础分界角，Forward/Back 与 Left/Right 各占 90 度。
    LockedDirectionHysteresisAngle = 10, -- 当前方向额外保留的角度，避免斜向边界反复切换循环 Sequence。
    LockedDirectionBlendTime = 0.10, -- 锁定 Cycle 切换方向 Sequence 的普通交叉混合时间。
    LockedOrientationWarpAlpha = 1.0, -- 四向素材完整扭转到精确移动方向；脊柱反向补偿后上半身仍朝锁定目标。
    LockedStopAlignStartNormalizedTime = 0.45, -- 斜向 Stop 从该进度开始逐步把下半身收回锁定目标方向。
    LockedStopAlignEndNormalizedTime = 0.86, -- Stop 末段完成下半身与锁定目标的对齐。
    WalkRunInertialBlendTime = 0.20, -- 仅 Walk/Run 循环互切时使用 UE Inertialization，方向变化不使用。

    IdleTurnEnterAngle = 25, -- Idle 起步超过该角度时先播放 Turn 身体动作，由 Movement 快速完成真实转向。
    IdleTurnSideAnimationAngle = 45, -- 超过该角度使用 Left/Right Turn，小角度使用 Forward Turn。
    IdleTurnBackAnimationAngle = 135, -- 超过该角度使用 Back Turn，真实旋转仍采用精确输入角。
    IdleProgramTurnExitAngle = 6, -- 程序原地转向进入 Start 时允许的剩余角度。
    IdleProgramTurnMaximumTime = 0.12, -- 持续输入时原地转向的最长等待，避免高帧率或目标变化导致起步延迟。
    SprintLargeTurnEnterAngle = 100, -- 90 度及以内保持 Sprint；超过该角度才允许制动后重新加速。
    SprintLargeTurnRequestDuration = 0.05, -- 大角度必须稳定保持该时长，过滤摇杆瞬时噪声。
    SprintRedirectRestartAngle = 12, -- Sprint Stop 转向后，剩余角度进入该范围即可重新 Start。
    SprintRedirectMinimumStopTime = 0.12, -- 至少保留 Sprint Stop 的前段制动重心。
    SprintRedirectMaximumStopTime = 0.42, -- 目标持续变化时的防卡死上限。

    WalkSteerMaxYawRate = 900, -- Walk 使用快速程序转向，约 0.1 秒完成 90 度修正。
    RunSteerMaxYawRate = 1080, -- Run 比 Walk 更灵敏，不再通过 Turn 动画改变朝向。
    SprintSteerMaxYawRate = 900, -- Sprint 在 90 度以内连续转向，不暂停循环动画。
    SprintRedirectMaxYawRate = 1440, -- Sprint 大角度制动期间快速转向，为重新加速建立朝向。
    RunTurnMaxYawRate = 1260, -- Idle 程序原地转向使用更高上限，快速建立起步朝向。

    StartBlendTime = 0.10,
    IdleBlendTime = 0.16, -- Stop 尾段开始淡入 Idle；Stop 会在该混合窗口内继续播放到末帧。
    StepBlendTime = 0.06,
    CycleBlendTime = 0.12,
    TurnBlendTime = 0.06,
    StopBlendTime = 0.08,

    StartMaximumPlayTime = 2.10, -- Start 动画时间异常时的防卡死上限，高于当前最长 Walk Start 时长 1.8667 秒。
    StartStopCurveGateMaximumWaitTime = 0.55, -- Start 失去输入后等待 CanEnterStop 脚步窗口的上限，只在曲线缺失时兜底。
    StepExitMinTime = 0.16, -- Step 至少保留该秒数的起步重心和主要 RootMotion，避免被持续输入立即截断。
    StepExitNormalizedTime = 0.78, -- DodgeActive 结束后播放到该比例即可衔接 Cycle/Idle。
    StepMaximumPlayTime = 0.65, -- 动画时长或时间读取异常时的退出兜底，防止 Step 卡住状态机。
    StepTurnMaxYawRate = 1800, -- 允许后向 Step 在约 0.1 秒内完成 180 度转向，保持只狼式快速响应。
    JumpDirectionBoundaryAngle = 22.5, -- 锁定跳跃八方向每个方向占 45 度，此值是中心方向两侧的半区间。
    JumpOrientationWarpAlpha = 0.35, -- 八向素材已经接近目标，只保留轻量姿态/上半身补偿，避免空中身体过度扭转。
    JumpStartExitNormalizedTime = 0.92, -- 起跳动画保留主要离地姿势后进入滞空；同时适配 0.1667 秒方向起跳和 1 秒原地起跳。
    JumpDirectionalMaximumAirTime = 1.55, -- 整次方向跳滞空超过该秒数才转通用 Jump_Loop；普通同高度跳跃仍使用同方向 Land。
    JumpDirectionalLandExitNormalizedTime = 0.86, -- 方向落地保留主要重心恢复后允许回到地面移动。
    JumpLightLandExitNormalizedTime = 0.86, -- 轻落地约 0.6667 秒，尾段可提前衔接 Idle/Start。
    JumpHeavyLandExitNormalizedTime = 0.82, -- 重落地约 2.5 秒，保留长硬直；蹲姿输入改用专属重落地资源。
    JumpHeavyLandingVelocityThreshold = -900, -- 最低垂直速度低于该值视为重落地，单位 cm/s。
    JumpStartMinimumVerticalVelocity = 50, -- 首次离地高于该速度才播放 Jump Start；走下台阶直接进入 InAir。
    JumpTurnMaxYawRate = 1080, -- 非锁定跳跃使用 Forward 资源，并在空中快速朝输入目标方向对齐。
    JumpBlendTime = 0.06,
    JumpLandBlendTime = 0.08,
    MoveInputHandoffTime = 0.06, -- Cycle 连续无输入达到该秒数才进入 Stop，容纳 W/S 等按键方向交接的短暂无输入帧。
    CycleStopCurveGateMaximumWaitTime = 0.65, -- 确认停止意图后等待 CanEnterStop 的防卡死上限，正常情况应在下一个脚步窗口内退出。
    StopRestartMinTime = 0.10, -- Stop 播放达到该秒数即可被新输入打断，不再按长动画的归一化进度等待。
    StopMaximumPlayTime = 1.50, -- Stop 动画时间异常时的防卡死上限，高于当前最长 Run Stop 时长 1.32 秒。
}

M.Assets = {}
for animation_name, animation_path in pairs(AnimAssets.Locomotion) do
    M.Assets[animation_name] = animation_path
end
for animation_name, animation_path in pairs(AnimAssets.Jump) do
    M.Assets[animation_name] = animation_path
end
M.JumpAssets = AnimAssets.Jump

M.AnimationSettings = {
    [M.State.StandingIdle] = { BlendTime = M.Tuning.IdleBlendTime, Loop = true },
    [M.State.StandingTurn] = { BlendTime = M.Tuning.TurnBlendTime, Loop = false },
    [M.State.StandingStart] = { BlendTime = M.Tuning.StartBlendTime, Loop = false },
    [M.State.StandingCycle] = { BlendTime = M.Tuning.CycleBlendTime, Loop = true },
    [M.State.StandingStop] = { BlendTime = M.Tuning.StopBlendTime, Loop = false },

    [M.State.CrouchIdle] = { BlendTime = M.Tuning.IdleBlendTime, Loop = true },
    [M.State.CrouchTurn] = { BlendTime = M.Tuning.TurnBlendTime, Loop = false },
    [M.State.CrouchStart] = { BlendTime = M.Tuning.StartBlendTime, Loop = false },
    [M.State.CrouchCycle] = { BlendTime = M.Tuning.CycleBlendTime, Loop = true },
    [M.State.CrouchStop] = { BlendTime = M.Tuning.StopBlendTime, Loop = false },

    [M.State.SprintStart] = { BlendTime = M.Tuning.StartBlendTime, Loop = false },
    [M.State.SprintCycle] = { BlendTime = M.Tuning.CycleBlendTime, Loop = true },
    [M.State.SprintStop] = { BlendTime = M.Tuning.StopBlendTime, Loop = false },

    [M.State.Step] = { BlendTime = M.Tuning.StepBlendTime, Loop = false },

    [M.State.JumpStart] = { BlendTime = M.Tuning.JumpBlendTime, Loop = false },
    [M.State.JumpDirectionalInAir] = { BlendTime = M.Tuning.JumpBlendTime, Loop = false },
    [M.State.JumpInAir] = { BlendTime = M.Tuning.JumpBlendTime, Loop = true },
    [M.State.JumpDirectionalLand] = { BlendTime = M.Tuning.JumpLandBlendTime, Loop = false },
    [M.State.JumpLightLand] = { BlendTime = M.Tuning.JumpLandBlendTime, Loop = false },
    [M.State.JumpHeavyLand] = { BlendTime = M.Tuning.JumpLandBlendTime, Loop = false },
}

return M
