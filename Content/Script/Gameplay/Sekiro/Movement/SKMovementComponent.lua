-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKMovementComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- Sekiro 角色移动策略。
-- Lua 选择速度档位对应速度、角色朝向目标和插值参数；UE CharacterMovement 继续负责物理、碰撞、Root Motion 与网络预测。

local LuaLog = require("Gameplay.Base.LuaLog")
local Direction = require("Animation.Sekiro.Shared.Direction")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SKMovementComponent: USKMovementComponent
local SKMovementComponent = UnLua.Class()
local Debug = true

---把任意角度规范到 -180..180，供世界 Yaw 和局部方向角稳定比较。
---@param angle number|nil 任意角度，单位为度；nil 按 0 处理。
---@return number normalized_angle 规范化后的有符号角度。
local function normalize_angle(angle)
    local normalized_angle = (angle or 0) % 360
    if normalized_angle > 180 then
        normalized_angle = normalized_angle - 360
    end
    return normalized_angle
end

---在 UnLua 完成 UObject 绑定后初始化纯 Lua 状态。
---此时 UObject 仍可能处于构造阶段，因此这里只写 Lua 私有字段，不覆盖 UPROPERTY。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只初始化 Lua 状态，不返回业务值。
function SKMovementComponent:Initialize(_initializer)
    self.FreeActorInterpSpeed = 12.0
    self.SprintActorInterpSpeed = 12.0
    self.LockOnActorInterpSpeed = 14.0
    self.TurnInPlaceActorInterpSpeed = 5.0
    self.TurnInPlaceEnterAngle = 25.0
    self.TurnInPlaceExitAngle = 3.0
    self.MoveInputFacingThreshold = 0.1
    self.bTurningInPlace = false
    self.LockedCycleDirection = Direction.Cardinal.Forward
    self.LastLockedSpineYawCompensation = 0.0
    LuaLog.Debug(Debug, "SKMovementComponent", "Initialize", "movement lua host initialized")
end

---在组件 BeginPlay 且 UObject 默认值复制完成后覆盖并发布 Lua 速度配置。
---WalkSpeed、RunSpeed、SprintSpeed 都是 UPROPERTY，延后写入可避免被 C++ 或蓝图默认值覆盖。
---@return nil 该函数只同步 Movement 速度档位配置。
function SKMovementComponent:ReceiveBeginPlay()
    self.WalkSpeed = 140.0
    self.RunSpeed = 407.0
    self.SprintSpeed = 853.0
    self:SetMovementSpeedProfileForScript(self.WalkSpeed, self.RunSpeed, self.SprintSpeed)
    LuaLog.Debug(Debug, "SKMovementComponent", "ReceiveBeginPlay", "movement property overrides applied")
end

---根据输入 Lua 已选择的 MovementTier 返回本帧地面速度上限。
---@return number max_walk_speed CharacterMovement 使用的最大地面速度，单位 cm/s。
function SKMovementComponent:ResolveMaxWalkSpeed()
    if self.CurrentMovementTier == UE.ESKMovementTier.Walk then
        return self.WalkSpeed
    end
    if self.CurrentMovementTier == UE.ESKMovementTier.Sprint then
        return self.SprintSpeed
    end

    -- Crouch 的最终速度仍由 UE 原生 MaxWalkSpeedCrouched 约束；这里保持站立 Run 的基线。
    return self.RunSpeed
end

---从屏幕空间移动输入和控制器 Yaw 计算目标世界 Yaw，并发布角色旋转前的局部方向角。
---@return boolean has_desired_yaw 是否存在超过阈值的有效移动目标。
---@return number desired_move_yaw 目标世界 Yaw；无有效输入时返回当前角色 Yaw。
function SKMovementComponent:PublishMoveFacingSnapshot()
    local owner_yaw = self:GetOwnerYaw()
    local input_x = self:GetMoveInputX() or 0
    local input_y = self:GetMoveInputY() or 0
    local input_amount = self:GetMoveInputAmount() or 0
    local input_length = math.sqrt(input_x * input_x + input_y * input_y)
    if input_amount <= self.MoveInputFacingThreshold
        or input_length <= self.MoveInputFacingThreshold then
        self:ClearMoveFacingSnapshotForScript()
        return false, owner_yaw
    end

    -- math.atan(lateral, forward) 与动画方向约定一致：正角为右，负角为左。
    local controller_yaw = self:GetControllerYawOrFallback(owner_yaw)
    local local_input_angle = math.deg(math.atan(input_x, input_y))
    local desired_move_yaw = normalize_angle(controller_yaw + local_input_angle)
    local pre_rotation_angle = self:NormalizeDeltaYaw(owner_yaw, desired_move_yaw)
    self:SetMoveFacingSnapshotForScript(true, desired_move_yaw, pre_rotation_angle)
    return true, desired_move_yaw
end

---根据 Sprint、锁定和自由移动优先级选择本帧角色朝向。
---Sprint 即使仍保留锁定目标也朝移动输入；普通锁定移动始终面向目标；自由移动朝输入方向。
---@param has_desired_yaw boolean 当前是否存在有效移动目标。
---@param desired_move_yaw number 输入对应的目标世界 Yaw。
---@return boolean has_facing_target 是否需要更新 ActorYaw。
---@return number target_yaw 本帧角色目标世界 Yaw。
---@return number interp_speed 角色 Yaw 插值速度。
function SKMovementComponent:ResolveActorFacing(has_desired_yaw, desired_move_yaw)
    if self:IsMovementTierSprint() and has_desired_yaw then
        self.bTurningInPlace = false
        return true, desired_move_yaw, self.SprintActorInterpSpeed
    end

    if self:IsLockedOn() and self:HasLockTargetYaw() then
        local owner_yaw = self:GetOwnerYaw()
        local target_yaw = self:GetLockTargetYawOrFallback(owner_yaw)
        if has_desired_yaw then
            self.bTurningInPlace = false
            return true, target_yaw, self.LockOnActorInterpSpeed
        end

        local absolute_turn_angle = math.abs(self:NormalizeDeltaYaw(owner_yaw, target_yaw))
        if self.bTurningInPlace == true then
            if absolute_turn_angle <= self.TurnInPlaceExitAngle then
                self.bTurningInPlace = false
            else
                return true, target_yaw, self.TurnInPlaceActorInterpSpeed
            end
        elseif absolute_turn_angle >= self.TurnInPlaceEnterAngle then
            self.bTurningInPlace = true
            -- 首帧只锁存 Turn 请求，让 AnimInstance 在 ActorYaw 改变前取得完整偏航角并选定左右资产。
            return false, owner_yaw, self.TurnInPlaceActorInterpSpeed
        end

        -- 小角度锁定偏差由待机上半身承担，避免没有 Turn 动画时脚底持续滑动。
        return false, owner_yaw, self.TurnInPlaceActorInterpSpeed
    end

    self.bTurningInPlace = false
    if has_desired_yaw then
        return true, desired_move_yaw, self.FreeActorInterpSpeed
    end

    return false, self:GetOwnerYaw(), self.FreeActorInterpSpeed
end

---由 C++ BlueprintNativeEvent 反射分发，在原生 CharacterMovement 求值前更新速度和角色朝向。
---@param delta_seconds number|nil 当前帧时长，单位为秒；nil 按 0 处理。
---@return boolean handled 始终返回 true，表示 Lua 已处理本帧 Movement 策略。
function SKMovementComponent:UpdateMovementLogic(delta_seconds)
    self:RefreshCachedMovementComponents()
    if not self:HasOwnerCharacter() then
        self:ClearMoveFacingSnapshotForScript()
        self:SetLockOnLocomotionSnapshotForScript(false, Direction.Cardinal.Forward, 0)
        return true
    end

    self:SetMovementSpeedProfileForScript(self.WalkSpeed, self.RunSpeed, self.SprintSpeed)
    self:SetMaxWalkSpeedForScript(self:ResolveMaxWalkSpeed())

    -- ActorYaw 只由本脚本更新，关闭 CharacterMovement 的两套内置自动旋转，避免同帧争抢所有权。
    self:SetMovementRotationSettingsForScript(false, false)

    local has_desired_yaw, desired_move_yaw = self:PublishMoveFacingSnapshot()
    -- Actor 朝向承担“输入方向 - 四向动画轴”的剩余角，并平滑追向该结果。
    -- 过渡期间原始 Root Motion 会随 Actor 逐渐转弯；脊柱按实际 ActorYaw 逐帧反向补偿，
    -- 因此下半身有转向过程，而上半身仍持续看向锁定目标。
    local locked_locomotion = self:IsLockedOn()
        and self:HasLockTargetYaw()
        and has_desired_yaw
        and not self:IsMovementTierSprint()
    if locked_locomotion then
        local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
        local desired_relative_angle = self:NormalizeDeltaYaw(target_yaw, desired_move_yaw)
        self.LockedCycleDirection = Direction.ResolveCardinalWithHysteresis(
            desired_relative_angle,
            self.LockedCycleDirection,
            Tuning.LockedDirectionHysteresisAngle,
            Tuning.LockedDirectionForwardBoundaryAngle,
            Tuning.LockedDirectionBackBoundaryAngle)
        local cardinal_axis = Direction.CardinalAngle[self.LockedCycleDirection] or 0
        local residual = Direction.NormalizeAngle(desired_relative_angle - cardinal_axis)
        local target_actor_yaw = normalize_angle(target_yaw + residual)
        self:ApplyActorYawForScript(
            target_actor_yaw,
            self.LockOnActorInterpSpeed,
            delta_seconds or 0)
        local actual_actor_yaw = self:GetOwnerYaw()
        local spine_yaw_compensation =
            self:NormalizeDeltaYaw(actual_actor_yaw, target_yaw)
        self:SetLockOnLocomotionSnapshotForScript(
            true,
            self.LockedCycleDirection,
            spine_yaw_compensation)
        self.LastLockedSpineYawCompensation = spine_yaw_compensation
        return true
    end
    if self:IsLockedOn()
        and not self:IsMovementTierSprint()
        and self:GetHorizontalSpeedForScript() > 3.0
    then
        self:SetLockOnLocomotionSnapshotForScript(
            true,
            self.LockedCycleDirection,
            self.LastLockedSpineYawCompensation)
        return true
    end
    self:SetLockOnLocomotionSnapshotForScript(false, Direction.Cardinal.Forward, 0)
    local has_facing_target, target_yaw, interp_speed = self:ResolveActorFacing(
        has_desired_yaw,
        desired_move_yaw)
    if has_facing_target then
        self:ApplyActorYawForScript(target_yaw, interp_speed, delta_seconds or 0)
    end

    return true
end

return SKMovementComponent
