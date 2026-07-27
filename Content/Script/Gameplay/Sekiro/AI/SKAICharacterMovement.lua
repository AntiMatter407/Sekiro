-- Lua 类型：UnLua UObject 运行时类。self 是 AI 角色真实的 USKMovementComponent。
-- 将导航系统上一帧请求的世界速度转换为动画移动快照，并把角色转向交给 AIController。
-- UE CharacterMovement 继续负责寻路速度、碰撞、物理、Controller Desired Rotation 和 Root Motion。

local Direction = require("Animation.Sekiro.Shared.Direction")

---@class SKAICharacterMovement: USKMovementComponent
local SKAICharacterMovement = UnLua.Class()

---在 UnLua 绑定阶段初始化 AI 移动调参；这里只写 Lua 私有字段。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取。
---@return nil result 本生命周期入口只初始化 Lua 私有状态。
function SKAICharacterMovement:Initialize(_initializer)
    self.MovementThreshold = 3.0
end

---在组件 BeginPlay 后配置 AI 复用的主角移动速度档位。
---@return nil result 本函数只发布组件速度配置。
function SKAICharacterMovement:ReceiveBeginPlay()
    self.WalkSpeed = 140.0
    self.RunSpeed = 407.0
    self.SprintSpeed = 853.0
    self:SetMovementSpeedProfileForScript(
        self.WalkSpeed,
        self.RunSpeed,
        self.SprintSpeed)
end

---由导航请求方向更新动画转向前快照，并让 CharacterMovement 消费 AIController 的期望朝向。
---巡逻时 Controller 朝向导航路径，索敌后 BehaviorTree DefaultFocus 朝向目标；Lua 不再重复写 ActorYaw。
---请求速度为空时清除快照，防止 AI 到达目标后继续保持移动姿势。
---@param _delta_seconds number|nil C++ Tick 传入的帧时长；原生旋转负责插值，本脚本仅保留事件签名。
---@return boolean handled 始终返回 true，表示 AI Lua 已处理本帧移动策略。
function SKAICharacterMovement:UpdateMovementLogic(_delta_seconds)
    self:RefreshCachedMovementComponents()
    self:SetMovementSpeedProfileForScript(
        self.WalkSpeed,
        self.RunSpeed,
        self.SprintSpeed)
    self:SetMaxWalkSpeedForScript(self.RunSpeed)
    self:SetMovementRotationSettingsForScript(false, true)
    self:SetLockOnLocomotionSnapshotForScript(
        false,
        Direction.Cardinal.Forward)
    self:SetRootMotionDirectionWarpingForScript(false, 0.0)

    if not self:HasOwnerCharacter() then
        self:ClearMoveFacingSnapshotForScript()
        return true
    end

    local requested_velocity = self:GetLastUpdateRequestedVelocity()
    local requested_x = requested_velocity.X or 0.0
    local requested_y = requested_velocity.Y or 0.0
    local requested_speed = math.sqrt(
        requested_x * requested_x + requested_y * requested_y)
    if requested_speed <= self.MovementThreshold then
        self:ClearMoveFacingSnapshotForScript()
        return true
    end

    local owner_yaw = self:GetOwnerYaw()
    local desired_yaw = Direction.NormalizeAngle(
        math.deg(math.atan(requested_y, requested_x)))
    local pre_rotation_angle = self:NormalizeDeltaYaw(
        owner_yaw,
        desired_yaw)
    self:SetMoveFacingSnapshotForScript(
        true,
        desired_yaw,
        pre_rotation_angle)
    return true
end

return SKAICharacterMovement
