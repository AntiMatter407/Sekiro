-- 管理角色朝向和相机模式。
-- RootMotion 版关闭 CharacterMovement 自动转向，由 Lua 按移动意图或锁定目标平滑驱动 ActorYaw。
local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKCameraManager = LuaComponent:Extend("SKCameraManager", {
    Debug = false,
    FreeActorInterpSpeed = 12.0,
})

local CameraMode = {
    Free = "Free",
    SprintAlign = "SprintAlign",
    LockOn = "LockOn",
}

function SKCameraManager:Construct(_context)
    self:LogDebug("Construct", "camera lua host constructed")
end

function SKCameraManager:ResolveCameraMode()
    if self:IsMovementTierSprint() then
        return CameraMode.SprintAlign
    end

    if self:IsLockedOn() then
        return CameraMode.LockOn
    end

    return CameraMode.Free
end

function SKCameraManager:UpdateMovementRotationSettings(mode)
    if mode == CameraMode.LockOn then
        self:SetMovementRotationSettingsForScript(false, false)
        return
    end

    if mode == CameraMode.SprintAlign then
        self:SetMovementRotationSettingsForScript(false, false)
        return
    end

    self:SetMovementRotationSettingsForScript(false, false)
end

function SKCameraManager:UpdateMoveDirectionAngle()
    if self:HasDesiredMoveYaw() then
        local target_yaw = self:GetDesiredMoveYawOrFallback(self:GetOwnerYaw())
        self:SetMoveDirectionAngleForScript(self:NormalizeDeltaYaw(self:GetOwnerYaw(), target_yaw))
        return
    end

    if self:HasOwnerVelocity() then
        local velocity_yaw = self:GetOwnerVelocityYawOrFallback(self:GetOwnerYaw())
        self:SetMoveDirectionAngleForScript(self:NormalizeDeltaYaw(self:GetOwnerYaw(), velocity_yaw))
        return
    end

    self:SetMoveDirectionAngleForScript(0)
end

function SKCameraManager:UpdateFreeMode(_delta_seconds)
    if self:HasDesiredMoveYaw() then
        -- 自由视角下移动不改变镜头，只让角色朝输入方向平滑转身以配合 RootMotion 前向位移。
        local target_yaw = self:GetDesiredMoveYawOrFallback(self:GetOwnerYaw())
        self:ApplyActorYawForScript(target_yaw, self.FreeActorInterpSpeed, _delta_seconds or 0)
    end

    self:ApplyPendingLookInputForScript()
end

function SKCameraManager:UpdateSprintAlignMode(delta_seconds)
    local owner_yaw = self:GetOwnerYaw()
    local target_yaw = self:GetDesiredMoveYawOrFallback(owner_yaw)
    self:ApplyActorYawForScript(target_yaw, self:GetSprintActorInterpSpeed(), delta_seconds or 0)

    if self:IsLockedOn() and self:HasLockTargetYaw() then
        local lock_yaw = self:GetLockTargetYawOrFallback(target_yaw)
        self:ApplyControllerYawForScript(lock_yaw, self:GetLockOnCameraYawInterpSpeed(), delta_seconds or 0)
        return
    end

    self:ApplyPendingLookInputForScript()
end

function SKCameraManager:UpdateLockOnMode(_delta_seconds)
    if not self:HasLockTargetYaw() then
        self:ApplyPendingLookInputForScript()
        return
    end

    local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
    self:ApplyActorYawForScript(target_yaw, self:GetLockOnActorInterpSpeed(), _delta_seconds or 0)
    self:ApplyControllerYawForScript(target_yaw, self:GetLockOnCameraYawInterpSpeed(), _delta_seconds or 0)
end

function SKCameraManager:Tick(_context, delta_seconds)
    self:RefreshCachedCameraComponents()
    if not self:HasOwnerCharacter() then
        self:ClearPendingLookInputForScript()
        return true
    end

    self:ValidateLockTargetForScript()

    local mode = self:ResolveCameraMode()
    self:SetCameraModeByName(mode)
    self:UpdateMovementRotationSettings(mode)

    if mode == CameraMode.SprintAlign then
        self:UpdateSprintAlignMode(delta_seconds)
    elseif mode == CameraMode.LockOn then
        self:UpdateLockOnMode(delta_seconds)
    else
        self:UpdateFreeMode(delta_seconds)
    end

    -- 朝向和相机在本帧更新后，再写入动画方向角，避免动画状态机使用上一帧朝向导致 RootMotion 偏向。
    self:UpdateMoveDirectionAngle()

    self:ClearPendingLookInputForScript()
    return true
end

return SKCameraManager:Export()
