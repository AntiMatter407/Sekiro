-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Sekiro Lua 动画蓝图使用的语义曲线名称。
-- Transition 只引用这里的稳定名称；曲线写入来源与阈值统一登记在动画曲线编写手册。

---@class SekiroAnimCurveNames
---@field CanEnterLoop string Start 或锁定 Jump InAir 过渡段已进入可衔接循环姿势的窗口。
---@field CanEnterStop string Start/Cycle 已进入可自然衔接 Stop 的脚步窗口。
---@field CanEnterIdle string Stop 已进入可回 Idle 或退出外层 Sprint 的窗口。
---@field CanExitStep string Step 已进入可返回普通地面移动的尾部窗口。
---@field CanExitTurn string 原地 Turn 已进入可返回 Idle 或接受后续转向的尾部窗口。
---@field CanEnterInAir string Jump Start 已进入可衔接 InAir 姿势的窗口。
---@field CanResumeMovement string Jump Land 姿势已恢复到可被地面移动打断的窗口。
---@field CanExitLand string Jump Land 已进入可退出空中外层状态的窗口。
---@field AttackSide string 攻击动作提交下一攻击侧，-1 为左、0 为保持、1 为右。
---@field CanAcceptLightAttack string 当前动作允许缓存下一段轻攻击输入。
---@field CanAcceptHeavyAttack string 当前动作允许缓存蓄力攻击输入。
---@field CanCancelToGuard string 当前动作允许被防御输入取消。
---@field CanCancelToJump string 当前地面攻击允许被跳跃输入取消。
---@field CanCancelToDodge string 当前动作允许被闪避输入取消。
---@field AttackHitbox string 原版 TAE 攻击框类型；非零区间开启当前武器攻击碰撞。
---@field DisableTurning string 原版 TAE 禁止转向窗口；大于等于 0.5 时攻击不得改变 ActorYaw。
---@field AttackTurnSpeed string 原版 TAE 攻击转向最大角速度，单位为度/秒。
---@field WeaponHandIK string 收拔刀换挂点附近约束右手到刀柄目标的连续权重。
local CurveNames = {
    CanEnterLoop = "CanEnterLoop",
    CanEnterStop = "CanEnterStop",
    CanEnterIdle = "CanEnterIdle",
    CanExitStep = "CanExitStep",
    CanExitTurn = "CanExitTurn",
    CanEnterInAir = "CanEnterInAir",
    CanResumeMovement = "CanResumeMovement",
    CanExitLand = "CanExitLand",
    AttackSide = "AttackSide",
    CanAcceptLightAttack = "CanAcceptLightAttack",
    CanAcceptHeavyAttack = "CanAcceptHeavyAttack",
    CanCancelToGuard = "CanCancelToGuard",
    CanCancelToJump = "CanCancelToJump",
    CanCancelToDodge = "CanCancelToDodge",
    AttackHitbox = "AttackHitbox",
    DisableTurning = "DisableTurning",
    AttackTurnSpeed = "AttackTurnSpeed",
    WeaponHandIK = "WeaponHandIK",
}

return CurveNames
