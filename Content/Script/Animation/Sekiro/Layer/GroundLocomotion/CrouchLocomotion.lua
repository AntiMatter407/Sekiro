-- 蹲姿 Idle/Walk/Run 子状态机的动画资源选择。
-- 蹲姿没有 Sprint；循环资源是独立 Sequence，因此步态或方向变化时由顶层按 MovePhase 做相位匹配。
local class = require("Animation.Base.Class")
local WalkRunStateMachine = require("Animation.Sekiro.Layer.GroundLocomotion.WalkRunStateMachine")
local Library = require("Animation.Sekiro.Layer.GroundLocomotion.Library")

local Anim = Library.Assets
local Branch = Library.Branch
local Mode = Library.LocomotionMode
local Direction = Library.TurnDirection

local CrouchLocomotion = class("CrouchLocomotion", WalkRunStateMachine, {
    BranchName = Branch.Crouch,
})

---返回蹲姿分支的默认 Idle Sequence。
---@return string animation_ref 蹲姿待机动画软路径。
function CrouchLocomotion:GetIdleAnimation()
    return Anim.Crouch_Idle
end

---按离散方向选择蹲姿原地转向姿势；胶囊和角色真实旋转由 Movement 负责。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配方向的蹲姿转向动画软路径。
function CrouchLocomotion:GetTurnAnimation(direction)
    if direction == Direction.Back then
        return Anim.Crouch_Idle_Back_Turn
    end
    if direction == Direction.Left then
        return Anim.Crouch_Idle_Left_Turn
    end
    if direction == Direction.Right then
        return Anim.Crouch_Idle_Right_Turn
    end
    return Anim.Crouch_Idle_Forward_Turn
end

---按 Walk/Run 步态和四方向选择蹲姿起步 Sequence。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的蹲姿起步动画软路径。
function CrouchLocomotion:GetStartAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Crouch_Walk_Back_Start
        end
        if direction == Direction.Left then
            return Anim.Crouch_Walk_Left_Start
        end
        if direction == Direction.Right then
            return Anim.Crouch_Walk_Right_Start
        end
        return Anim.Crouch_Walk_Forward_Start
    end

    if direction == Direction.Back then
        return Anim.Crouch_Run_Back_Start
    end
    if direction == Direction.Left then
        return Anim.Crouch_Run_Left_Start
    end
    if direction == Direction.Right then
        return Anim.Crouch_Run_Right_Start
    end
    return Anim.Crouch_Run_Forward_Start
end

---按 Walk/Run 步态和四方向选择蹲姿循环 Sequence，并声明使用 Sequence 播放器。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的蹲姿循环动画软路径。
---@return string pose_type 固定为 Sequence，供顶层统一处理。
function CrouchLocomotion:GetCycleAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Crouch_Walk_Back_Loop, "Sequence"
        end
        if direction == Direction.Left then
            return Anim.Crouch_Walk_Left_Loop, "Sequence"
        end
        if direction == Direction.Right then
            return Anim.Crouch_Walk_Right_Loop, "Sequence"
        end
        return Anim.Crouch_Walk_Forward_Loop, "Sequence"
    end

    if direction == Direction.Back then
        return Anim.Crouch_Run_Back_Loop, "Sequence"
    end
    if direction == Direction.Left then
        return Anim.Crouch_Run_Left_Loop, "Sequence"
    end
    if direction == Direction.Right then
        return Anim.Crouch_Run_Right_Loop, "Sequence"
    end
    return Anim.Crouch_Run_Forward_Loop, "Sequence"
end

---按 Walk/Run 步态和输入释放前的冻结方向选择蹲姿停止 Sequence。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@param direction string|nil 相对角色朝向解析出的语义方向。
---@return string animation_ref 匹配步态与方向的蹲姿停止动画软路径。
function CrouchLocomotion:GetStopAnimation(mode, direction)
    if mode == Mode.Walk then
        if direction == Direction.Back then
            return Anim.Crouch_Walk_Back_Stop
        end
        if direction == Direction.Left then
            return Anim.Crouch_Walk_Left_Stop
        end
        if direction == Direction.Right then
            return Anim.Crouch_Walk_Right_Stop
        end
        return Anim.Crouch_Walk_Forward_Stop
    end

    if direction == Direction.Back then
        return Anim.Crouch_Run_Back_Stop
    end
    if direction == Direction.Left then
        return Anim.Crouch_Run_Left_Stop
    end
    if direction == Direction.Right then
        return Anim.Crouch_Run_Right_Stop
    end
    return Anim.Crouch_Run_Forward_Stop
end

return CrouchLocomotion
