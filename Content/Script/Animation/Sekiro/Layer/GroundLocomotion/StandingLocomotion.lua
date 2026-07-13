-- 站立 Idle/Walk/Run 子状态机的动画资源选择。
-- Walk 与 Run 始终返回明确的四方向 Sequence；步态切换惯性化由顶层 GroundLocomotion 请求。
local class = require("Animation.Base.Class")
local WalkRunStateMachine = require("Animation.Sekiro.Layer.GroundLocomotion.WalkRunStateMachine")
local Library = require("Animation.Sekiro.Layer.GroundLocomotion.Library")

local Anim = Library.Assets
local Branch = Library.Branch
local Mode = Library.LocomotionMode
local Direction = Library.TurnDirection

local StandingLocomotion = class("StandingLocomotion", WalkRunStateMachine, {
    BranchName = Branch.Standing,
})

---返回站立分支的默认 Idle Sequence。
---@return string animation_ref 站立待机动画软路径。
function StandingLocomotion:GetIdleAnimation()
    return Anim.Idle
end

---按离散方向选择站立原地转向姿势；角色真实旋转仍由 Movement 完成。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配方向的站立转向动画软路径。
function StandingLocomotion:GetTurnAnimation(direction)
    if direction == Direction.Back then
        return Anim.Idle_Back_Turn
    end
    if direction == Direction.Left then
        return Anim.Idle_Left_Turn
    end
    if direction == Direction.Right then
        return Anim.Idle_Right_Turn
    end
    return Anim.Idle_Forward_Turn
end

---按 Walk/Run 步态和四方向选择站立起步 Sequence。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的起步动画软路径。
function StandingLocomotion:GetStartAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Walk_Back_Start
        end
        if direction == Direction.Left then
            return Anim.Walk_Left_Start
        end
        if direction == Direction.Right then
            return Anim.Walk_Right_Start
        end
        return Anim.Walk_Forward_Start
    end

    if direction == Direction.Back then
        return Anim.Run_Back_Start
    end
    if direction == Direction.Left then
        return Anim.Run_Left_Start
    end
    if direction == Direction.Right then
        return Anim.Run_Right_Start
    end
    return Anim.Run_Forward_Start
end

---按 Walk/Run 步态和四方向选择站立循环 Sequence。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的循环动画软路径。
function StandingLocomotion:GetCycleAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Walk_Back_Loop
        end
        if direction == Direction.Left then
            return Anim.Walk_Left_Loop
        end
        if direction == Direction.Right then
            return Anim.Walk_Right_Loop
        end
        return Anim.Walk_Forward_Loop
    end

    if direction == Direction.Back then
        return Anim.Run_Back_Loop
    end
    if direction == Direction.Left then
        return Anim.Run_Left_Loop
    end
    if direction == Direction.Right then
        return Anim.Run_Right_Loop
    end
    return Anim.Run_Forward_Loop
end

---按 Walk/Run 步态和冻结方向选择站立停止 Sequence。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的停止动画软路径。
function StandingLocomotion:GetStopAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Walk_Back_Stop
        end
        if direction == Direction.Left then
            return Anim.Walk_Left_Stop
        end
        if direction == Direction.Right then
            return Anim.Walk_Right_Stop
        end
        return Anim.Walk_Forward_Stop
    end

    if direction == Direction.Back then
        return Anim.Run_Back_Stop
    end
    if direction == Direction.Left then
        return Anim.Run_Left_Stop
    end
    if direction == Direction.Right then
        return Anim.Run_Right_Stop
    end
    return Anim.Run_Forward_Stop
end

return StandingLocomotion
