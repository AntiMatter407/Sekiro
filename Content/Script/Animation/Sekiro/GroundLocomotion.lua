-- Sekiro 主角地面移动状态机。
-- RootMotion 版只负责选择状态、方向和动画段；位移距离、步频和冲刺速度交给动画资源自身。
local class = require("Animation.Base.Class")
local BaseStateMachine = require("Animation.Base.LuaAnimStateMachine")
local LayerLibrary = require("Animation.Sekiro.Layer.GroundLocomotion.Library")

local State = LayerLibrary.State
local Tuning = LayerLibrary.Tuning
local Anim = LayerLibrary.Assets
local Curves = LayerLibrary.Curves
local MoveTransition = LayerLibrary.MoveTransition

local Direction = {
    Forward = "Forward",
    Back = "Back",
    Left = "Left",
    Right = "Right",
}

local GroundLocomotion = class("GroundLocomotion", BaseStateMachine, {
    LayerName = LayerLibrary.LayerName,
    EntryState = State.Idle,
    State = State,
    States = State,
    StateList = LayerLibrary.StateList,
    Tuning = Tuning,
    Assets = LayerLibrary.Assets,
    AnimationSettings = LayerLibrary.AnimationSettings,
})

function GroundLocomotion:GetCurrentStateTime()
    return self.StateTime or 0
end

function GroundLocomotion:GetNormalizedTime()
    return self:GetCurrentAnimationNormalizedTime(0)
end

function GroundLocomotion:GetGroundedEntryState()
    return self.GroundedEntryState or ""
end

function GroundLocomotion:IsGrounded()
    return (self.MovementState == nil or self.MovementState == "Grounded")
        and self.bIsInAir ~= true
end

function GroundLocomotion:HasMovementInput()
    if self.bHasMovementInput ~= nil then
        return self.bHasMovementInput == true
    end

    return (self.MovementInputAmount or 0) > Tuning.InputThreshold
end

function GroundLocomotion:IsMoving()
    if self.bIsMoving ~= nil then
        return self.bIsMoving == true
    end

    return (self.Speed or 0) > Tuning.MovingSpeedThreshold
end

function GroundLocomotion:IsWalkRunScope()
    return self:IsGrounded()
        and self.bIsDodging ~= true
end

function GroundLocomotion:IsSprintGait()
    return self.DesiredGait == "Sprint"
        or self.Gait == "Sprint"
        or self.MovementTier == "Sprint"
end

function GroundLocomotion:IsSprintState(state_name)
    return state_name == State.SprintStart
        or state_name == State.Sprint
        or state_name == State.SprintStop
end

function GroundLocomotion:IsLockedView()
    return self.bIsLockedOn == true
        or self.RotationMode == "LookingDirection"
end

function GroundLocomotion:IsFreeView()
    return not self:IsLockedView()
end

function GroundLocomotion:GetWalkRunGait()
    if self.DesiredGait == "Walk" or self.Gait == "Walk" then
        self.LastWalkRunGait = "Walk"
        return "Walk"
    end

    if self.DesiredGait == "Run" or self.Gait == "Run" then
        self.LastWalkRunGait = "Run"
        return "Run"
    end

    return self.LastWalkRunGait or "Run"
end

function GroundLocomotion:IsWalkGait()
    return self:GetWalkRunGait() == "Walk"
end

function GroundLocomotion:GetDirectionFromAngle(angle)
    local normalized_angle = angle or 0
    local abs_angle = math.abs(normalized_angle)

    if abs_angle >= 135 then
        return Direction.Back
    end

    if normalized_angle >= 45 then
        return Direction.Right
    end

    if normalized_angle <= -45 then
        return Direction.Left
    end

    return Direction.Forward
end

function GroundLocomotion:GetStableDirectionFromAngle(angle, previous_direction)
    local normalized_angle = angle or 0
    local abs_angle = math.abs(normalized_angle)
    local side_enter_angle = Tuning.DirectionSideEnterAngle or 60
    local side_exit_angle = Tuning.DirectionSideExitAngle or 30
    local back_enter_angle = Tuning.DirectionBackEnterAngle or 150
    local back_exit_angle = Tuning.DirectionBackExitAngle or 120

    -- RootMotion 下方向切换会直接改变胶囊体位移方向，所以锁定四方向使用滞回而不是 45 度硬切。
    if previous_direction == Direction.Back then
        if abs_angle >= back_exit_angle then
            return Direction.Back
        end

        if normalized_angle >= side_enter_angle then
            return Direction.Right
        end

        if normalized_angle <= -side_enter_angle then
            return Direction.Left
        end

        return Direction.Forward
    end

    if abs_angle >= back_enter_angle then
        return Direction.Back
    end

    if previous_direction == Direction.Left then
        if normalized_angle <= -side_exit_angle then
            return Direction.Left
        end

        if normalized_angle >= side_enter_angle then
            return Direction.Right
        end

        return Direction.Forward
    end

    if previous_direction == Direction.Right then
        if normalized_angle >= side_exit_angle then
            return Direction.Right
        end

        if normalized_angle <= -side_enter_angle then
            return Direction.Left
        end

        return Direction.Forward
    end

    if normalized_angle >= side_enter_angle then
        return Direction.Right
    end

    if normalized_angle <= -side_enter_angle then
        return Direction.Left
    end

    return Direction.Forward
end

function GroundLocomotion:GetTurnSideFromAngle(angle)
    local normalized_angle = angle or 0
    if normalized_angle < 0 then
        return Direction.Left
    end

    if normalized_angle > 0 then
        return Direction.Right
    end

    if self.TurnDirection == "Left" then
        return Direction.Left
    end

    return Direction.Right
end

function GroundLocomotion:GetDirectionFromScreenInput(input_x, input_y)
    local x = input_x or 0
    local y = input_y or 0
    local abs_x = math.abs(x)
    local abs_y = math.abs(y)

    if abs_x < Tuning.InputThreshold and abs_y < Tuning.InputThreshold then
        return Direction.Forward
    end

    if abs_x > abs_y then
        if x < 0 then
            return Direction.Left
        end

        return Direction.Right
    end

    if y < 0 then
        return Direction.Back
    end

    return Direction.Forward
end

function GroundLocomotion:GetLockedViewMovementDirection()
    if not self:HasMovementInput() then
        return self.LastWalkRunDirection or Direction.Forward
    end

    local direction = self:GetStableDirectionFromAngle(
        self.MoveDirectionAngle or 0,
        self.LastWalkRunDirection)
    self.LastWalkRunDirection = direction
    return direction
end

function GroundLocomotion:GetFreeViewTurnAngle()
    if self.bShouldRotationModeTurn == true then
        return self.RotationModeTransitionAngle or self.TurnAngle or self.MoveDirectionAngle or 0
    end

    if self.bShouldTurn == true or self:GetGroundedEntryState() == "Turn" then
        return self.TurnAngle or self.MoveDirectionAngle or self.DirectionDelta or 0
    end

    return self.MoveDirectionAngle or self.TurnAngle or self.DirectionDelta or 0
end

function GroundLocomotion:WantsFreeViewTurnStart()
    if not self:IsFreeView() or not self:HasMovementInput() then
        return false
    end

    return self.bShouldTurn == true
        or self.bShouldRotationModeTurn == true
        or self:GetGroundedEntryState() == "Turn"
        or math.abs(self:GetFreeViewTurnAngle()) >= Tuning.FreeViewTurnStartAngle
end

function GroundLocomotion:GetFreeViewStartDirection()
    if not self:WantsFreeViewTurnStart() then
        return Direction.Forward
    end

    return self:GetTurnSideFromAngle(self:GetFreeViewTurnAngle())
end

function GroundLocomotion:GetWalkRunDirection()
    if self:IsLockedView() then
        return self:GetLockedViewMovementDirection()
    end

    return Direction.Forward
end

function GroundLocomotion:GetStepDirectionFromDodgeInput()
    local forward = self.DodgeDirection or 0
    local lateral = self.DodgeDirectionLateral or 0

    if math.abs(lateral) > math.abs(forward) then
        if lateral < 0 then
            return Direction.Left
        end

        return Direction.Right
    end

    if forward < -Tuning.InputThreshold then
        return Direction.Back
    end

    return Direction.Forward
end

function GroundLocomotion:GetStepDirection()
    if self:IsLockedView() and self:HasMovementInput() then
        return self:GetLockedViewMovementDirection()
    end

    if self:HasMovementInput() then
        return self:IsFreeView() and Direction.Forward or self:GetLockedViewMovementDirection()
    end

    return self:GetStepDirectionFromDodgeInput()
end

function GroundLocomotion:GetSprintStartDirection()
    return self:GetDirectionFromScreenInput(self.MoveInputX, self.MoveInputY)
end

function GroundLocomotion:GetMoveTransition(default_value)
    return self:GetCurrentCurveIntValue(Curves.MoveTransition, default_value or MoveTransition.None)
end

function GroundLocomotion:HasMoveTransitionCurve()
    return self:GetMoveTransition(-1) >= 0
end

function GroundLocomotion:IsMoveTransition(transition_value)
    return self:GetMoveTransition(MoveTransition.None) == transition_value
end

---只狼原始动画如果有取消/转段曲线，就优先信曲线；曲线缺失时按归一化时间兜底。
---RootMotion 状态不再因为速度阈值提前截断 Start/Stop，避免起步和收步根运动被吃掉。
function GroundLocomotion:IsReadyForAnimTransition(transition_value, min_time, min_normalized_time)
    if self:GetCurrentStateTime() < (min_time or 0) then
        return false
    end

    if self:HasMoveTransitionCurve() and self:IsMoveTransition(transition_value) then
        return true
    end

    return self:GetNormalizedTime() >= (min_normalized_time or 1)
end

function GroundLocomotion:IsStartReadyForCycle()
    if not self:WantsCycle() then
        return false
    end

    if self.bFreeViewStartTurnActive == true then
        return self:IsReadyForAnimTransition(
            MoveTransition.EnterLoop,
            Tuning.FreeViewTurnStartToCycleMinTime,
            Tuning.FreeViewTurnStartToCycleMinNormalizedTime)
    end

    if self:IsWalkGait() then
        return self:IsReadyForAnimTransition(
            MoveTransition.EnterLoop,
            Tuning.WalkStartToCycleMinTime,
            Tuning.WalkStartToCycleMinNormalizedTime)
    end

    return self:IsReadyForAnimTransition(
        MoveTransition.EnterLoop,
        Tuning.RunStartToCycleMinTime,
        Tuning.RunStartToCycleMinNormalizedTime)
end

function GroundLocomotion:IsStartCancelable()
    return self:GetNormalizedTime() >= Tuning.StartCancelMinNormalizedTime
        and self:GetCurrentStateTime() >= Tuning.StartCancelMinTime
end

function GroundLocomotion:IsCycleReadyForStop()
    return self:GetCurrentStateTime() >= Tuning.CycleToStopMinTime
end

function GroundLocomotion:IsStopReadyForIdle()
    if self:IsWalkGait() then
        return self:IsReadyForAnimTransition(
            MoveTransition.EnterIdle,
            Tuning.WalkStopToIdleMinTime,
            Tuning.WalkStopToIdleMinNormalizedTime)
    end

    return self:IsReadyForAnimTransition(
        MoveTransition.EnterIdle,
        Tuning.RunStopToIdleMinTime,
        Tuning.RunStopToIdleMinNormalizedTime)
end

function GroundLocomotion:IsStepFinished()
    return self:GetCurrentStateTime() >= Tuning.StepExitMinTime
        and self:GetNormalizedTime() >= Tuning.StepExitMinNormalizedTime
end

function GroundLocomotion:IsSprintStartReadyForLoop()
    return self:IsReadyForAnimTransition(
        MoveTransition.EnterLoop,
        Tuning.SprintStartToSprintMinTime,
        Tuning.SprintStartToSprintMinNormalizedTime)
end

function GroundLocomotion:IsSprintStartCancelable()
    return self:GetCurrentStateTime() >= Tuning.SprintStartCancelMinTime
        and self:GetNormalizedTime() >= Tuning.SprintStartCancelMinNormalizedTime
end

function GroundLocomotion:IsSprintStopReadyForMove()
    return self:GetCurrentStateTime() >= Tuning.SprintStopToMoveMinTime
        and self:GetNormalizedTime() >= Tuning.SprintStopToMoveMinNormalizedTime
end

function GroundLocomotion:IsSprintStopReadyForIdle()
    return self:IsReadyForAnimTransition(
        MoveTransition.EnterIdle,
        Tuning.SprintStopToIdleMinTime,
        Tuning.SprintStopToIdleMinNormalizedTime)
end

function GroundLocomotion:WantsIdle()
    if self:WantsStep() or self:WantsSprint() then
        return false
    end

    return not self:IsWalkRunScope()
        or not self:HasMovementInput()
end

function GroundLocomotion:WantsStart()
    return self:IsWalkRunScope()
        and not self:WantsSprint()
        and self:HasMovementInput()
end

function GroundLocomotion:WantsCycle()
    return self:IsWalkRunScope()
        and not self:WantsSprint()
        and self:HasMovementInput()
end

function GroundLocomotion:WantsStop()
    return self:IsWalkRunScope()
        and not self:WantsSprint()
        and not self:HasMovementInput()
end

function GroundLocomotion:WantsStep()
    return self:IsGrounded()
        and (
            self.bIsDodging == true
            or self:GetGroundedEntryState() == "DodgeStep"
        )
end

function GroundLocomotion:WantsSprint()
    return self:IsGrounded()
        and self.bIsDodging ~= true
        and self:HasMovementInput()
        and self:IsSprintGait()
end

function GroundLocomotion:UpdateAnimation_Idle()
    self.LastWalkRunDirection = Direction.Forward
    self.StepDirection = nil
    self.SprintStartDirection = nil
    self.bFreeViewStartTurnActive = false
    return self:PlaySequence(Anim.Idle)
end

function GroundLocomotion:PlayStepByDirection(direction)
    local options = {
        Loop = false,
        BlendTime = Tuning.StepBlendTime,
    }

    if direction == Direction.Back then
        return self:PlaySequence(Anim.Step_Back, options)
    end

    if direction == Direction.Left then
        return self:PlaySequence(Anim.Step_Left, options)
    end

    if direction == Direction.Right then
        return self:PlaySequence(Anim.Step_Right, options)
    end

    return self:PlaySequence(Anim.Step_Forward, options)
end

function GroundLocomotion:UpdateAnimation_Step()
    if self.EvaluatingResetTime == true or self.StepDirection == nil then
        self.StepDirection = self:GetStepDirection()
    end

    return self:PlayStepByDirection(self.StepDirection)
end

function GroundLocomotion:PlayForwardStart()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Forward_Start)
    end

    return self:PlaySequence(Anim.Run_Forward_Start)
end

function GroundLocomotion:PlayBackStart()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Back_Start)
    end

    return self:PlaySequence(Anim.Run_Back_Start)
end

function GroundLocomotion:PlayLeftStart()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Left_Start)
    end

    return self:PlaySequence(Anim.Run_Left_Start)
end

function GroundLocomotion:PlayRightStart()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Right_Start)
    end

    return self:PlaySequence(Anim.Run_Right_Start)
end

function GroundLocomotion:PlayLeftTurnStart()
    local options = {
        Loop = false,
        BlendTime = Tuning.FreeViewTurnStartBlendTime,
    }

    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Left_Turn, options)
    end

    return self:PlaySequence(Anim.Run_Left_Turn, options)
end

function GroundLocomotion:PlayRightTurnStart()
    local options = {
        Loop = false,
        BlendTime = Tuning.FreeViewTurnStartBlendTime,
    }

    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Right_Turn, options)
    end

    return self:PlaySequence(Anim.Run_Right_Turn, options)
end

function GroundLocomotion:PlayStartByDirection(direction)
    if direction == Direction.Back then
        return self:PlayBackStart()
    end

    if direction == Direction.Left then
        return self:PlayLeftStart()
    end

    if direction == Direction.Right then
        return self:PlayRightStart()
    end

    return self:PlayForwardStart()
end

function GroundLocomotion:UpdateAnimation_Start()
    if self:IsFreeView() then
        local turn_direction = self:GetFreeViewStartDirection()
        self.bFreeViewStartTurnActive = turn_direction ~= Direction.Forward

        if turn_direction == Direction.Left then
            return self:PlayLeftTurnStart()
        end

        if turn_direction == Direction.Right then
            return self:PlayRightTurnStart()
        end

        self.LastWalkRunDirection = Direction.Forward
        return self:PlayForwardStart()
    end

    self.bFreeViewStartTurnActive = false
    self.LastWalkRunDirection = self:GetLockedViewMovementDirection()
    return self:PlayStartByDirection(self.LastWalkRunDirection)
end

function GroundLocomotion:PlayCycleByDirection(direction)
    if self:IsWalkGait() then
        if direction == Direction.Back then
            return self:PlaySequence(Anim.Walk_Back_Loop)
        end

        if direction == Direction.Left then
            return self:PlaySequence(Anim.Walk_Left_Loop)
        end

        if direction == Direction.Right then
            return self:PlaySequence(Anim.Walk_Right_Loop)
        end

        return self:PlaySequence(Anim.Walk_Forward_Loop)
    end

    if direction == Direction.Back then
        return self:PlaySequence(Anim.Run_Back_Loop)
    end

    if direction == Direction.Left then
        return self:PlaySequence(Anim.Run_Left_Loop)
    end

    if direction == Direction.Right then
        return self:PlaySequence(Anim.Run_Right_Loop)
    end

    return self:PlaySequence(Anim.Run_Forward_Loop)
end

function GroundLocomotion:UpdateAnimation_Cycle()
    self.bFreeViewStartTurnActive = false
    self.LastWalkRunDirection = self:GetWalkRunDirection()
    return self:PlayCycleByDirection(self.LastWalkRunDirection)
end

function GroundLocomotion:PlayForwardStop()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Forward_Stop)
    end

    return self:PlaySequence(Anim.Run_Forward_Stop)
end

function GroundLocomotion:PlayBackStop()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Back_Stop)
    end

    return self:PlaySequence(Anim.Run_Back_Stop)
end

function GroundLocomotion:PlayLeftStop()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Left_Stop)
    end

    return self:PlaySequence(Anim.Run_Left_Stop)
end

function GroundLocomotion:PlayRightStop()
    if self:IsWalkGait() then
        return self:PlaySequence(Anim.Walk_Right_Stop)
    end

    return self:PlaySequence(Anim.Run_Right_Stop)
end

function GroundLocomotion:PlayStopByDirection(direction)
    if direction == Direction.Back then
        return self:PlayBackStop()
    end

    if direction == Direction.Left then
        return self:PlayLeftStop()
    end

    if direction == Direction.Right then
        return self:PlayRightStop()
    end

    return self:PlayForwardStop()
end

function GroundLocomotion:UpdateAnimation_Stop()
    if self:IsFreeView() then
        self.LastWalkRunDirection = Direction.Forward
    end

    return self:PlayStopByDirection(self.LastWalkRunDirection or Direction.Forward)
end

function GroundLocomotion:PlaySprintStartByDirection(direction)
    local options = {
        Loop = false,
        BlendTime = Tuning.SprintStartBlendTime,
    }

    if direction == Direction.Back then
        return self:PlaySequence(Anim.Sprint_Back_Turn_Start, options)
    end

    if direction == Direction.Left then
        return self:PlaySequence(Anim.Sprint_Left_Turn_Start, options)
    end

    if direction == Direction.Right then
        return self:PlaySequence(Anim.Sprint_Right_Turn_Start, options)
    end

    return self:PlaySequence(Anim.Sprint_Forward_Start, options)
end

function GroundLocomotion:UpdateAnimation_SprintStart()
    if self.EvaluatingResetTime == true or self.SprintStartDirection == nil then
        self.SprintStartDirection = self:GetSprintStartDirection()
    end

    return self:PlaySprintStartByDirection(self.SprintStartDirection)
end

function GroundLocomotion:UpdateAnimation_Sprint()
    self.SprintStartDirection = nil
    return self:PlaySequence(Anim.Sprint_Forward_Loop, {
        Loop = true,
        BlendTime = Tuning.SprintLoopBlendTime,
    })
end

function GroundLocomotion:UpdateAnimation_SprintStop()
    self.SprintStartDirection = nil
    local stop_anim = Anim.Sprint_Forward_Stop
    if self:HasMovementInput() and not self:WantsIdle() then
        stop_anim = Anim.Sprint_Forward_To_Run or stop_anim
    end

    return self:PlaySequence(stop_anim, {
        Loop = false,
        BlendTime = Tuning.SprintStopBlendTime,
    })
end

function GroundLocomotion:CanEnter_Any_Step()
    return self:WantsStep()
end

function GroundLocomotion:CanEnter_Any_SprintStart()
    return self:WantsSprint()
        and not self:IsSprintState(self.CurrentStateName or self.CurrentState)
end

function GroundLocomotion:CanEnter_Any_Idle()
    return not self:WantsStep()
        and not self:WantsSprint()
        and not self:IsWalkRunScope()
end

function GroundLocomotion:CanEnter_Idle_Start()
    return self:WantsStart()
end

function GroundLocomotion:CanEnter_Idle_SprintStart()
    return self:WantsSprint()
end

function GroundLocomotion:CanEnter_Step_SprintStart()
    return not self:WantsStep()
        and self:WantsSprint()
        and self:IsStepFinished()
end

function GroundLocomotion:CanEnter_Step_Start()
    return not self:WantsStep()
        and self:WantsStart()
        and self:IsStepFinished()
end

function GroundLocomotion:CanEnter_Step_Idle()
    return not self:WantsStep()
        and self:WantsIdle()
        and self:IsStepFinished()
end

function GroundLocomotion:CanEnter_Start_SprintStart()
    return self:WantsSprint()
end

function GroundLocomotion:CanEnter_Start_Stop()
    return self:WantsStop()
        and self:IsStartCancelable()
end

function GroundLocomotion:CanEnter_Start_Cycle()
    return self:IsStartReadyForCycle()
end

function GroundLocomotion:CanEnter_Cycle_SprintStart()
    return self:WantsSprint()
end

function GroundLocomotion:CanEnter_Cycle_Start()
    return self:WantsFreeViewTurnStart()
        and self:GetCurrentStateTime() >= Tuning.CycleTurnReenterMinTime
end

function GroundLocomotion:CanEnter_Cycle_Stop()
    return self:WantsStop()
        and self:IsCycleReadyForStop()
end

function GroundLocomotion:CanEnter_Stop_SprintStart()
    return self:WantsSprint()
end

function GroundLocomotion:CanEnter_Stop_Start()
    return self:WantsStart()
        and self:GetNormalizedTime() >= Tuning.StopCancelMinNormalizedTime
end

function GroundLocomotion:CanEnter_Stop_Idle()
    return self:WantsIdle()
        and self:IsStopReadyForIdle()
end

function GroundLocomotion:CanEnter_SprintStart_Sprint()
    return self:WantsSprint()
        and self:IsSprintStartReadyForLoop()
end

function GroundLocomotion:CanEnter_SprintStart_SprintStop()
    return not self:WantsSprint()
        and self:IsSprintStartCancelable()
end

function GroundLocomotion:CanEnter_Sprint_SprintStop()
    return not self:WantsSprint()
end

function GroundLocomotion:CanEnter_SprintStop_SprintStart()
    return self:WantsSprint()
end

function GroundLocomotion:CanEnter_SprintStop_Start()
    return not self:WantsSprint()
        and self:WantsStart()
        and self:IsSprintStopReadyForMove()
end

function GroundLocomotion:CanEnter_SprintStop_Idle()
    return not self:WantsSprint()
        and self:WantsIdle()
        and self:IsSprintStopReadyForIdle()
end

return GroundLocomotion
