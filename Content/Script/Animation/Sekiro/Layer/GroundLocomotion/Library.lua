local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "GroundLocomotion"

M.State = {
    Idle = "Idle",
    Step = "Step",
    Start = "Start",
    Cycle = "Cycle",
    Stop = "Stop",
    SprintStart = "SprintStart",
    Sprint = "Sprint",
    SprintStop = "SprintStop",
}

M.StateList = {
    M.State.Idle,
    M.State.Step,
    M.State.Start,
    M.State.Cycle,
    M.State.Stop,
    M.State.SprintStart,
    M.State.Sprint,
    M.State.SprintStop,
}

M.Tuning = {
    WalkSpeed = 140, -- RootMotion 版仅保留为调试参考，不再驱动播放速率。
    RunSpeed = 407, -- RootMotion 版仅保留为调试参考，不再驱动播放速率。
    SprintSpeed = 853, -- RootMotion 版仅保留为调试参考，不再驱动播放速率。
    MovingSpeedThreshold = 3, -- 只作为兼容读取 bIsMoving 的兜底阈值。
    InputThreshold = 0.1, -- 屏幕移动输入小于该值时视为无移动意图。
    WalkStartToCycleMinTime = 0.20, -- 起步至少播放的秒数；曲线存在时仍以曲线为准。
    RunStartToCycleMinTime = 0.24,
    WalkStartToCycleMinNormalizedTime = 0.90, -- 无 MoveTransition 曲线时保留大部分 RootMotion 起步段。
    RunStartToCycleMinNormalizedTime = 0.88,
    FreeViewTurnStartToCycleMinTime = 0.22,
    FreeViewTurnStartToCycleMinNormalizedTime = 0.90,
    StartCancelMinTime = 0.12, -- 输入刚松开时不要马上掐掉起步根运动。
    StartCancelMinNormalizedTime = 0.35,
    CycleToStopMinTime = 0.04,
    WalkStopToIdleMinTime = 0.18,
    RunStopToIdleMinTime = 0.20,
    WalkStopToIdleMinNormalizedTime = 0.86,
    RunStopToIdleMinNormalizedTime = 0.90,
    StopCancelMinNormalizedTime = 0.35,
    StepBlendTime = 0.06,
    StepExitMinTime = 0.16,
    StepExitMinNormalizedTime = 0.78,
    FreeViewTurnStartAngle = 55, -- 非锁定下输入方向和角色朝向差距较大时，先播转身起步。
    DirectionSideEnterAngle = 60, -- 锁定四方向从 Forward 进入 Left/Right 的角度，留出滞回避免边界抖动。
    DirectionSideExitAngle = 30, -- 锁定四方向从 Left/Right 回到 Forward 的角度，低于进入阈值形成滞回。
    DirectionBackEnterAngle = 150, -- 锁定四方向进入 Back 的角度。
    DirectionBackExitAngle = 120, -- 锁定四方向离开 Back 的角度。
    CycleTurnReenterMinTime = 0.22,
    FreeViewTurnStartBlendTime = 0.10,
    SprintStartBlendTime = 0.10,
    SprintLoopBlendTime = 0.12,
    SprintStopBlendTime = 0.10,
    SprintStartToSprintMinTime = 0.18,
    SprintStartToSprintMinNormalizedTime = 0.80,
    SprintStartCancelMinTime = 0.12,
    SprintStartCancelMinNormalizedTime = 0.35,
    SprintStopToMoveMinTime = 0.16,
    SprintStopToMoveMinNormalizedTime = 0.45,
    SprintStopToIdleMinTime = 0.18,
    SprintStopToIdleMinNormalizedTime = 0.82,
}

M.Assets = AnimAssets.Locomotion

M.Curves = {
    MoveTransition = "MoveTransition",
    FootPlant = "FootPlant",
    MovePhase = "MovePhase",
}

M.MoveTransition = {
    None = 0,
    EnterLoop = 1,
    EnterStop = 2,
    EnterIdle = 3,
}

M.FootPlant = {
    None = 0,
    Left = 1,
    Right = 2,
    Both = 3,
}

M.AnimationSettings = {
    [M.State.Idle] = { BlendTime = 0.16, Loop = true },
    [M.State.Step] = { BlendTime = 0.06, Loop = false },
    [M.State.Start] = { BlendTime = 0.10, Loop = false },
    [M.State.Cycle] = { BlendTime = 0.12, Loop = true },
    [M.State.Stop] = { BlendTime = 0.08, Loop = false },
    [M.State.SprintStart] = { BlendTime = 0.10, Loop = false },
    [M.State.Sprint] = { BlendTime = 0.12, Loop = true },
    [M.State.SprintStop] = { BlendTime = 0.10, Loop = false },
}

return M
