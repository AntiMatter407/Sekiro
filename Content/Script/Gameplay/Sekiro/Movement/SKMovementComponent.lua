-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKMovementComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- Sekiro 角色移动策略。
-- Lua 选择速度档位对应速度、角色朝向目标和插值参数；UE CharacterMovement 继续负责物理、碰撞、Root Motion 与网络预测。

local LuaLog = require("Gameplay.Base.LuaLog")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local Direction = require("Animation.Sekiro.Shared.Direction")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SKMovementComponent: USKMovementComponent
local SKMovementComponent = UnLua.Class()
local Debug = true

---在 UnLua 完成 UObject 绑定后初始化纯 Lua 状态。
---此时 UObject 仍可能处于构造阶段，因此这里只写 Lua 私有字段，不覆盖 UPROPERTY。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只初始化 Lua 状态，不返回业务值。
function SKMovementComponent:Initialize(_initializer)
    self.FreeActorInterpSpeed = 12.0
    self.SprintActorInterpSpeed = 12.0
    self.LockOnActorInterpSpeed = 14.0
    self.TurnInPlaceActorInterpSpeed = 5.0
    self.AirActorTurnSpeedMultiplier = 0.2
    self.TurnInPlaceEnterAngle = Tuning.IdleTurnEnterAngle
    self.TurnInPlaceExitAngle = Tuning.IdleTurnExitAngle
    self.MoveInputFacingThreshold = 0.1
    self.bTurningInPlace = false
    self.LockedCycleDirection = Direction.Cardinal.Forward
    self.LastLockOnRootMotionWorldYaw = 0.0
    self.bHasLastLockOnRootMotionWorldYaw = false
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
    local desired_move_yaw = Direction.NormalizeAngle(controller_yaw + local_input_angle)
    local pre_rotation_angle = self:NormalizeDeltaYaw(owner_yaw, desired_move_yaw)
    self:SetMoveFacingSnapshotForScript(true, desired_move_yaw, pre_rotation_angle)
    return true, desired_move_yaw
end

---根据 Sprint、锁定和自由移动优先级选择本帧角色朝向。
---Sprint 即使仍保留锁定目标也朝移动输入；锁定移动面向目标；锁定待机转身由 Movement 独占 ActorYaw。
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
                -- Turn 资产的根骨起止变换相同，只负责脚步姿势；Movement 是唯一 ActorYaw 权威。
                return true, target_yaw, self.TurnInPlaceActorInterpSpeed
            end
        elseif absolute_turn_angle >= self.TurnInPlaceEnterAngle then
            self.bTurningInPlace = true
            -- 首帧保持 ActorYaw，令 AnimInstance 锁存完整 RootYawOffset 并稳定选择左右转身资产。
            return false, owner_yaw, self.TurnInPlaceActorInterpSpeed
        end

        -- 小角度锁定偏差由待机姿势承担，不触发 ActorYaw 插值。
        return false, owner_yaw, self.TurnInPlaceActorInterpSpeed
    end

    self.bTurningInPlace = false
    if has_desired_yaw then
        return true, desired_move_yaw, self.FreeActorInterpSpeed
    end

    return false, self:GetOwnerYaw(), self.FreeActorInterpSpeed
end

---按移动状态缩放角色朝向插值速度。
---空中只降低 ActorYaw 的追踪速度，不改变 CharacterMovement 维护的水平惯性和移动输入。
---@param ground_interp_speed number|nil 当前朝向模式选择的地面插值速度；nil 按 0 处理。
---@return number interp_speed 当前移动状态实际使用的非负插值速度。
function SKMovementComponent:ResolveActorYawInterpSpeed(ground_interp_speed)
    local interp_speed = math.max(ground_interp_speed or 0.0, 0.0)
    if self:IsFalling() == true then
        local air_multiplier = math.max(self.AirActorTurnSpeedMultiplier or 0.0, 0.0)
        return interp_speed * air_multiplier
    end
    return interp_speed
end

---由 C++ BlueprintNativeEvent 反射分发，在原生 CharacterMovement 求值前更新速度和角色朝向。
---@param delta_seconds number|nil 当前帧时长，单位为秒；nil 按 0 处理。
---@return boolean handled 始终返回 true，表示 Lua 已处理本帧 Movement 策略。
function SKMovementComponent:UpdateMovementLogic(delta_seconds)
    self:RefreshCachedMovementComponents()
    if not self:HasOwnerCharacter() then
        self:ClearMoveFacingSnapshotForScript()
        self:SetLockOnLocomotionSnapshotForScript(false, Direction.Cardinal.Forward)
        self:SetRootMotionDirectionWarpingForScript(false, 0.0)
        self.bHasLastLockOnRootMotionWorldYaw = false
        return true
    end

    self:SetMovementSpeedProfileForScript(self.WalkSpeed, self.RunSpeed, self.SprintSpeed)
    self:SetMaxWalkSpeedForScript(self:ResolveMaxWalkSpeed())

    -- ActorYaw 只由本脚本更新，关闭 CharacterMovement 的两套内置自动旋转，避免同帧争抢所有权。
    self:SetMovementRotationSettingsForScript(false, false)

    local has_desired_yaw, desired_move_yaw = self:PublishMoveFacingSnapshot()
    local combat_full_body_active =
        self:IsOwnerCombatFullBodyActionActiveForScript() == true
    -- 锁定普通移动由 Actor 平滑追向目标；四向素材只负责提供最近的基础步态。
    -- AnimGraph 的 Graph Orientation Warping 负责下半身与脊柱姿势，
    -- Movement 原生回调把 CharacterMovement 最终消费的 Root Motion 水平平移转到输入世界方向。
    -- 全身战斗动作拥有自己的 Root Motion，期间必须退出锁定移动方向修正，防止按住方向键时误转攻击位移。
    local can_warp_locomotion_root_motion = not combat_full_body_active

    if combat_full_body_active then
        self:SetLockOnLocomotionSnapshotForScript(false, Direction.Cardinal.Forward)
        self:SetRootMotionDirectionWarpingForScript(false, 0.0)
        self.bHasLastLockOnRootMotionWorldYaw = false

        -- 攻击只在原版 TAE 允许的窗口按确定角速度转向。受击、防御等其他全身动作不复用普通移动旋转，
        -- 避免胶囊体在动画脚掌承重阶段持续旋转而产生滑步。
        if self:IsOwnerAttackActionActiveForScript() == true then
            local turning_disabled = self:SampleOwnerCombatSequenceCurveForScript(
                CurveNames.DisableTurning)
            local attack_turn_speed = self:SampleOwnerCombatSequenceCurveForScript(
                CurveNames.AttackTurnSpeed)
            if turning_disabled < 0.5 and attack_turn_speed > 0.0 then
                local has_attack_target = false
                local attack_target_yaw = self:GetOwnerYaw()
                if self:IsLockedOn() and self:HasLockTargetYaw() then
                    has_attack_target = true
                    attack_target_yaw = self:GetLockTargetYawOrFallback(attack_target_yaw)
                elseif has_desired_yaw then
                    has_attack_target = true
                    attack_target_yaw = desired_move_yaw
                end

                if has_attack_target then
                    self:ApplyActorYawRateForScript(
                        attack_target_yaw,
                        attack_turn_speed,
                        delta_seconds or 0)
                end
            end
        end
        return true
    end
    local locked_locomotion = self:IsLockedOn()
        and self:HasLockTargetYaw()
        and has_desired_yaw
        and not self:IsMovementTierSprint()
        and can_warp_locomotion_root_motion
    if locked_locomotion then
        local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
        local desired_relative_angle = self:NormalizeDeltaYaw(target_yaw, desired_move_yaw)
        self.LockedCycleDirection = Direction.ResolveCardinalWithHysteresis(
            desired_relative_angle,
            self.LockedCycleDirection,
            Tuning.LockedDirectionHysteresisAngle,
            Tuning.LockedDirectionForwardBoundaryAngle,
            Tuning.LockedDirectionBackBoundaryAngle)
        self:ApplyActorYawForScript(
            target_yaw,
            self:ResolveActorYawInterpSpeed(self.LockOnActorInterpSpeed),
            delta_seconds or 0)
        self:SetLockOnLocomotionSnapshotForScript(
            true,
            self.LockedCycleDirection)
        self.LastLockOnRootMotionWorldYaw = desired_move_yaw
        self.bHasLastLockOnRootMotionWorldYaw = true
        self:SetRootMotionDirectionWarpingForScript(true, desired_move_yaw)
        return true
    end
    if self:IsLockedOn()
        and self:HasLockTargetYaw()
        and not self:IsMovementTierSprint()
        and can_warp_locomotion_root_motion
        and self:GetHorizontalSpeedForScript() > 3.0
    then
        local target_yaw = self:GetLockTargetYawOrFallback(self:GetOwnerYaw())
        self:ApplyActorYawForScript(
            target_yaw,
            self:ResolveActorYawInterpSpeed(self.LockOnActorInterpSpeed),
            delta_seconds or 0)
        self:SetLockOnLocomotionSnapshotForScript(
            true,
            self.LockedCycleDirection)
        local root_motion_world_yaw = self.bHasLastLockOnRootMotionWorldYaw == true
            and self.LastLockOnRootMotionWorldYaw
            or target_yaw
        self:SetRootMotionDirectionWarpingForScript(true, root_motion_world_yaw)
        return true
    end
    self:SetLockOnLocomotionSnapshotForScript(false, Direction.Cardinal.Forward)
    self:SetRootMotionDirectionWarpingForScript(false, 0.0)
    self.bHasLastLockOnRootMotionWorldYaw = false
    local has_facing_target, target_yaw, interp_speed = self:ResolveActorFacing(
        has_desired_yaw,
        desired_move_yaw)
    if has_facing_target then
        self:ApplyActorYawForScript(
            target_yaw,
            self:ResolveActorYawInterpSpeed(interp_speed),
            delta_seconds or 0)
    end

    return true
end

return SKMovementComponent
