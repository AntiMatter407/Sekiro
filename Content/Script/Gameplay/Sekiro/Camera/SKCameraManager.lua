-- 管理角色朝向和相机模式。
-- RootMotion 版关闭 CharacterMovement 自动转向，由 Lua 按移动意图或锁定目标平滑驱动 ActorYaw。
local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKCameraManager = LuaComponent:Extend("SKCameraManager", {
    Debug = true,
    FreeActorInterpSpeed = 12.0,
})

local CameraMode = {
    Free = "Free",
    SprintAlign = "SprintAlign",
    LockOn = "LockOn",
}

---在 Construct 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKCameraManager:Construct(_context)
    self:LogDebug("Construct", "camera lua host constructed")
end

---根据当前输入和运行时状态解析相机模式，避免调用方重复边界判断。
---@return string|nil value 解析出的模式、方向或状态名称。
function SKCameraManager:ResolveCameraMode()
    if self:IsMovementTierSprint() then
        return CameraMode.SprintAlign
    end

    if self:IsLockedOn() then
        return CameraMode.LockOn
    end

    return CameraMode.Free
end

---根据相机模式关闭 CharacterMovement 自动旋转，把 ActorYaw 控制权留给 Root Motion 或 Lua 相机逻辑。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@return nil 该函数只更新 C++ Movement 旋转配置。
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

---使用期望移动 Yaw 或当前速度 Yaw 更新角色局部移动角。
---该值会在同帧被 Lua 动画状态机用于四向资源选择和方向扭转。
---@return nil 该函数只把方向角同步到 C++ 动画实例。
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

---只在动画声明的旋转所有者变化时记录一次，避免逐帧日志淹没转向问题。
---@param mode string|nil 当前移动、相机或动画模式的语义名称。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKCameraManager:LogActorYawOwnerDebug(mode)
    local root_motion_owns_yaw = self:IsActorYawOwnedByRootMotion() == true
    if self.LastRootMotionOwnsActorYaw == root_motion_owns_yaw then
        return
    end

    self.LastRootMotionOwnsActorYaw = root_motion_owns_yaw
    self:LogDebug("ActorYawOwner", string.format(
        "owner=%s cameraMode=%s",
        root_motion_owns_yaw and "RootMotion" or "Script",
        tostring(mode)))
end

---更新非锁定相机：只消费视角输入，不因移动输入主动改变镜头或 ActorYaw。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只提交自由视角输入。
function SKCameraManager:UpdateFreeMode(delta_seconds)
    -- 非锁定移动不改变镜头，也不直接旋转角色；ActorYaw 由动画 RootMotion 独占。
    self:ApplyPendingLookInputForScript()
end

---更新冲刺对齐模式：角色朝移动方向，仍有锁定目标时相机继续追踪目标。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只更新角色或控制器朝向。
function SKCameraManager:UpdateSprintAlignMode(delta_seconds)
    local owner_yaw = self:GetOwnerYaw()
    local target_yaw = self:GetDesiredMoveYawOrFallback(owner_yaw)
    if not self:IsActorYawOwnedByRootMotion() then
        self:ApplyActorYawForScript(target_yaw, self:GetSprintActorInterpSpeed(), delta_seconds or 0)
    end

    -- 锁定 SprintStart 期间只暂停 ActorYaw；ControllerYaw 仍持续跟踪锁定目标。
    if self:IsLockedOn() and self:HasLockTargetYaw() then
        local lock_yaw = self:GetLockTargetYawOrFallback(target_yaw)
        self:ApplyControllerYawForScript(lock_yaw, self:GetLockOnCameraYawInterpSpeed(), delta_seconds or 0)
        return
    end

    self:ApplyPendingLookInputForScript()
end

---更新锁定模式：相机持续看向目标，Root Motion 未持有 Yaw 时角色也平滑朝向目标。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只更新角色或控制器朝向。
function SKCameraManager:UpdateLockOnMode(delta_seconds)
    if not self:HasLockTargetYaw() then
        self:ApplyPendingLookInputForScript()
        return
    end

    local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
    if not self:IsActorYawOwnedByRootMotion() then
        self:ApplyActorYawForScript(target_yaw, self:GetLockOnActorInterpSpeed(), delta_seconds or 0)
    end
    self:ApplyControllerYawForScript(target_yaw, self:GetLockOnCameraYawInterpSpeed(), delta_seconds or 0)
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return boolean handled 始终返回 true，表示相机逻辑已处理本帧更新。
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
    self:LogActorYawOwnerDebug(mode)

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
