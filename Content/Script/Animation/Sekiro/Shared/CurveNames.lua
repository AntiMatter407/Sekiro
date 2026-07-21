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
local CurveNames = {
    CanEnterLoop = "CanEnterLoop",
    CanEnterStop = "CanEnterStop",
    CanEnterIdle = "CanEnterIdle",
    CanExitStep = "CanExitStep",
    CanExitTurn = "CanExitTurn",
    CanEnterInAir = "CanEnterInAir",
    CanResumeMovement = "CanResumeMovement",
    CanExitLand = "CanExitLand",
}

return CurveNames
