-- Sekiro 新 Lua 动画蓝图的集中调参表。
-- 数值只描述动画状态机和节点过渡，不在这里读取输入、修改 Movement 或选择动画资源。

---@class SekiroAnimBlueprintTuning
---@field InputThreshold number 认为角色具有有效移动输入的最小输入量。
---@field LockedDirectionHysteresisAngle number 锁定四方向选择的滞回角度，单位为度。
---@field IdleTurnEnterAngle number 非锁定原地起步时进入 Turn 姿态的最小角度，单位为度。
---@field SprintLargeTurnEnterAngle number Sprint 需要先制动再转向的最小角度，单位为度。
---@field StartBlendDuration number 进入 Start 的过渡时长，单位为秒。
---@field CycleBlendDuration number Start 与 Cycle 之间的过渡时长，单位为秒。
---@field StopBlendDuration number Cycle 与 Stop 之间的过渡时长，单位为秒。
---@field IdleBlendDuration number Stop 与 Idle 之间的过渡时长，单位为秒。
---@field StepBlendDuration number Step 进入与退出的过渡时长，单位为秒。
---@field SprintBlendDuration number Sprint 子状态机内部的过渡时长，单位为秒。
---@field JumpBlendDuration number Jump Start、InAir 与 Land 之间的过渡时长，单位为秒。
---@field CurveThreshold number CanEnterStop 等门控曲线被视为开启的阈值。
---@field DirectionSyncGroup string Standing/Crouch Cycle Sequence 使用的原生同步组名称。
local Tuning = {
    InputThreshold = 0.1,
    LockedDirectionHysteresisAngle = 10,
    IdleTurnEnterAngle = 25,
    SprintLargeTurnEnterAngle = 100,

    StartBlendDuration = 0.10,
    CycleBlendDuration = 0.12,
    StopBlendDuration = 0.08,
    IdleBlendDuration = 0.16,
    StepBlendDuration = 0.06,
    SprintBlendDuration = 0.10,
    JumpBlendDuration = 0.08,

    CurveThreshold = 0.5,
    DirectionSyncGroup = "SekiroLocomotion",
}

return Tuning
