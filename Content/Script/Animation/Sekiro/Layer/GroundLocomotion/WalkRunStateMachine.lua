-- Standing 与 Crouch 子状态机的共同接口。
-- 本类只描述可复用的 Idle/Turn/Start/Cycle/Stop 结构；顶层 GroundLocomotion 负责 Step、Sprint 与分支切换。
local class = require("Animation.Base.Class")

local WalkRunStateMachine = class("WalkRunStateMachine", nil, {
    BranchName = "WalkRun",
})

---返回子状态机所属分支名，顶层状态机用它构造 Standing.Cycle 等复合状态。
---@return string branch_name 当前子状态机分支名称。
function WalkRunStateMachine:GetBranchName()
    return self.BranchName
end

---把子状态名转换为运行时复合状态名，使 UE Debug 能显示当前所属子状态机。
---@param local_state string 子状态名称，例如 Idle、Turn、Start、Cycle 或 Stop。
---@return string state_name 例如 Standing.Cycle。
function WalkRunStateMachine:GetStateName(local_state)
    return string.format("%s.%s", self.BranchName, local_state)
end

---子类必须显式提供 Idle 资源，禁止根据字符串拼接动画名。
---@return string|nil animation_ref 子类提供的 Idle 动画引用；基类返回 nil 表示必须覆盖。
function WalkRunStateMachine:GetIdleAnimation()
    return nil
end

---声明转向动画选择接口，具体站姿按离散方向返回资源。
---@param _direction string|nil 需要表现的前、后、左或右方向；基类不消费。
---@return string|nil animation_ref 子类选择的转向动画引用。
function WalkRunStateMachine:GetTurnAnimation(_direction)
    return nil
end

---声明 Start 动画选择接口，子类根据 Walk/Run 和四方向返回明确 Sequence。
---@param _mode string|nil Walk 或 Run 步态；基类不消费。
---@param _direction string|nil 前、后、左或右离散方向；基类不消费。
---@return string|nil animation_ref 子类选择的 Start 动画引用。
function WalkRunStateMachine:GetStartAnimation(_mode, _direction)
    return nil
end

---声明 Cycle 动画选择接口；额外返回姿势类型和可选混合输入以兼容不同播放器。
---@param _mode string|nil Walk 或 Run 步态；基类不消费。
---@param _direction string|nil 前、后、左或右离散方向；基类不消费。
---@return string|nil animation_ref 子类选择的循环动画引用。
---@return string pose_type 姿势播放器类型；当前子类应返回 Sequence。
---@return number blend_input 兼容旧采样接口的混合输入，Sequence 使用 0。
function WalkRunStateMachine:GetCycleAnimation(_mode, _direction)
    return nil, "Sequence", 0
end

---声明 Stop 动画选择接口，停止方向必须与输入释放前的 Cycle 方向一致。
---@param _mode string|nil Walk 或 Run 步态；基类不消费。
---@param _direction string|nil 输入释放前冻结的四方向；基类不消费。
---@return string|nil animation_ref 子类选择的 Stop 动画引用。
function WalkRunStateMachine:GetStopAnimation(_mode, _direction)
    return nil
end

return WalkRunStateMachine
