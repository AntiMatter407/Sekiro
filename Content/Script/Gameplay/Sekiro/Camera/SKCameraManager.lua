-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKCameraManagerComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- 管理相机模式、自由视角输入和锁定目标跟随。
-- 角色 ActorYaw 已由 Movement Lua 独占，本模块不再写入角色旋转或动画移动方向。
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKCameraManager: USKCameraManagerComponent
---@field CameraBoom userdata|nil 当前角色的 SpringArm，生命周期与所属角色一致。
---@field StepCameraLagAlpha number Step 镜头配置权重，0 为普通移动，1 为完整 Step 平滑。
---@field NormalCameraLagSpeed number 普通移动时的位置 Lag 收敛速度。
---@field NormalCameraLagMaxDistance number 普通移动时允许的最大镜头滞后距离，单位为厘米。
---@field StepCameraLagSpeed number Step 期间的位置 Lag 收敛速度，较低值用于过滤 Root Motion 冲量。
---@field StepCameraLagMaxDistance number Step 期间允许的最大镜头滞后距离，单位为厘米。
---@field StepCameraLagRecoveryDuration number Step 结束后恢复普通镜头参数的时长，单位为秒。
---@field CameraLagMaxTimeStep number SpringArm Lag 子步进的最大步长，单位为秒。
local SKCameraManager = UnLua.Class()
local Debug = true

local CameraMode = {
    Free = "Free",
    SprintAlign = "SprintAlign",
    LockOn = "LockOn",
}

---缓存角色 SpringArm 并应用普通移动的位置平滑配置。
---只启用位置 Lag 和子步进，不启用旋转 Lag，因此鼠标和锁定视角仍保持原来的响应速度。
---@return boolean configured 找到角色与 CameraBoom 且成功写入全部参数时返回 true。
function SKCameraManager:ConfigureCameraLag()
    local owner = self:GetOwner()
    if owner == nil then
        return false
    end

    ---在受保护调用中访问角色的原生 SpringArm；蓝图替换组件或反射字段缺失时仅关闭平滑，不中断相机 Tick。
    ---@return boolean configured 找到 CameraBoom 并完成运行时配置时返回 true。
    local ok, configured = pcall(function()
        local camera_boom = owner.CameraBoom
        if camera_boom == nil then
            return false
        end

        self.CameraBoom = camera_boom
        camera_boom.bEnableCameraLag = true
        camera_boom.CameraLagSpeed = self.NormalCameraLagSpeed
        camera_boom.CameraLagMaxDistance = self.NormalCameraLagMaxDistance
        camera_boom.bUseCameraLagSubstepping = true
        camera_boom.CameraLagMaxTimeStep = self.CameraLagMaxTimeStep
        return true
    end)

    return ok and configured == true
end

---根据 Step 激活状态逐帧更新 SpringArm 位置平滑参数。
---Step 开始时立即放宽镜头滞后距离以吸收 Root Motion 冲量；结束后渐进恢复，防止镜头突然追上角色。
---@param delta_seconds number|nil 本帧时长，单位为秒；异常大帧按 1/30 秒推进恢复权重。
---@return boolean updated 成功读取角色状态并写入 SpringArm 参数时返回 true。
function SKCameraManager:UpdateCameraLag(delta_seconds)
    local owner = self:GetOwner()
    local camera_boom = self.CameraBoom
    if owner == nil or camera_boom == nil then
        return self:ConfigureCameraLag()
    end

    ---受保护读取角色 Dodge 状态；蓝图字段变化时保持上一帧镜头，不让反射异常中断 Camera Tick。
    ---@return boolean is_dodging 当前角色是否处于 Step/Dodge 激活窗口。
    local state_ok, is_dodging = pcall(function()
        return owner.bIsDodging == true
    end)
    if not state_ok then
        return false
    end

    local alpha = self.StepCameraLagAlpha or 0
    if is_dodging then
        alpha = 1
    else
        local safe_delta = math.min(math.max(delta_seconds or 0, 0), 1.0 / 30.0)
        local recovery_duration = math.max(self.StepCameraLagRecoveryDuration, 0.001)
        alpha = math.max(0, alpha - safe_delta / recovery_duration)
    end
    self.StepCameraLagAlpha = alpha

    local lag_speed = self.NormalCameraLagSpeed
        + (self.StepCameraLagSpeed - self.NormalCameraLagSpeed) * alpha
    local max_distance = self.NormalCameraLagMaxDistance
        + (self.StepCameraLagMaxDistance - self.NormalCameraLagMaxDistance) * alpha

    ---受保护写入高频调参；仅改变位置平滑参数，不触碰 ControlRotation 或 ActorYaw 所有权。
    ---@return boolean updated SpringArm 仍有效且参数写入完成时返回 true。
    local update_ok, updated = pcall(function()
        camera_boom.CameraLagSpeed = lag_speed
        camera_boom.CameraLagMaxDistance = max_distance
        return true
    end)
    return update_ok and updated == true
end

---在 UnLua 完成 UObject 绑定后初始化相机脚本状态。
---此时 UObject 仍可能处于构造阶段，因此这里只写 Lua 私有字段，不覆盖 UPROPERTY。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKCameraManager:Initialize(_initializer)
    -- 相机平滑配置和运行时缓存只属于当前 UObject 的 Lua 实例。
    self.NormalCameraLagSpeed = 20.0
    self.NormalCameraLagMaxDistance = 60.0
    self.StepCameraLagSpeed = 9.0
    self.StepCameraLagMaxDistance = 120.0
    self.StepCameraLagRecoveryDuration = 0.35
    self.CameraLagMaxTimeStep = 1.0 / 60.0
    self.StepCameraLagAlpha = 0
    LuaLog.Debug(Debug, "SKCameraManager", "Initialize", "camera lua host initialized")
end

---在组件 BeginPlay 且 UObject 默认值复制完成后应用 Lua 相机配置并配置 SpringArm。
---UPROPERTY 必须在此阶段覆盖，避免 Initialize 的早期赋值被 C++ 构造或蓝图模板重新写回。
---@return nil 该函数只应用相机运行时配置。
function SKCameraManager:ReceiveBeginPlay()
    self.MaxLockOnRange = 4000.0

    if not self:ConfigureCameraLag() then
        LuaLog.Debug(
            Debug,
            "SKCameraManager",
            "ReceiveBeginPlay",
            "camera lag configuration unavailable")
    end
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

---更新非锁定相机：只消费视角输入，不因移动输入主动改变镜头。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只提交自由视角输入。
function SKCameraManager:UpdateFreeMode(delta_seconds)
    self:ApplyPendingLookInputForScript()
end

---更新冲刺相机：角色朝向由 Movement Lua 处理，仍有锁定目标时镜头继续追踪目标。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只更新角色或控制器朝向。
function SKCameraManager:UpdateSprintAlignMode(delta_seconds)
    -- Sprint 期间 Movement 朝输入方向；ControllerYaw 可继续跟踪仍有效的锁定目标。
    if self:IsLockedOn() and self:HasLockTargetYaw() then
        local lock_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
        self:ApplyControllerYawForScript(lock_yaw, self:GetLockOnCameraYawInterpSpeed(), delta_seconds or 0)
        return
    end

    self:ApplyPendingLookInputForScript()
end

---更新锁定模式：相机持续看向目标；角色朝向由 Movement Lua 同步处理。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只更新角色或控制器朝向。
function SKCameraManager:UpdateLockOnMode(delta_seconds)
    if not self:HasLockTargetYaw() then
        self:ApplyPendingLookInputForScript()
        return
    end

    local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
    self:ApplyControllerYawForScript(target_yaw, self:GetLockOnCameraYawInterpSpeed(), delta_seconds or 0)
end

---由 C++ BlueprintNativeEvent 反射分发，执行本模块的逐帧更新并同步最新输入和状态。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return boolean handled 始终返回 true，表示相机逻辑已处理本帧更新。
function SKCameraManager:UpdateCameraLogic(delta_seconds)
    self:RefreshCachedCameraComponents()
    if not self:HasOwnerCharacter() then
        self:ClearPendingLookInputForScript()
        return true
    end

    self:UpdateCameraLag(delta_seconds)
    self:ValidateLockTargetForScript()

    local mode = self:ResolveCameraMode()
    self:SetCameraModeByName(mode)

    if mode == CameraMode.SprintAlign then
        self:UpdateSprintAlignMode(delta_seconds)
    elseif mode == CameraMode.LockOn then
        self:UpdateLockOnMode(delta_seconds)
    else
        self:UpdateFreeMode(delta_seconds)
    end

    self:ClearPendingLookInputForScript()
    return true
end

return SKCameraManager
