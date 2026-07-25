-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKInputManager，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- 管理玩家输入到角色意图的转换。
-- RootMotion 版只写入 MoveIntent、MovementTier 和 DodgeDirection，角色位移由动画根运动驱动。
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKInputManager: USKInputManager
local SKInputManager = UnLua.Class()
local Debug = true
local DebugMoveIntent = false -- 高频移动意图默认关闭；其余输入状态边沿仍保留日志。

---按输入模块调试开关输出带功能区域的日志。
---@param area string 日志所属的输入功能区域。
---@param message string 需要输出的调试内容。
---@return nil 该函数只写入日志。
local function log_debug(area, message)
    LuaLog.Debug(Debug, "SKInputManager", area, message)
end

---把数值限制在给定闭区间内。
---@param value number 需要限制范围的数值。
---@param min_value number 允许范围的最小值。
---@param max_value number 允许范围的最大值。
---@return number clamped 限制到最小值和最大值之间的数值。
local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

---计算二维输入向量长度。
---@param x number 二维向量的横向分量。
---@param y number 二维向量的纵向分量。
---@return number length 二维向量长度。
local function length2(x, y)
    return math.sqrt(x * x + y * y)
end

---归一化二维输入向量，零向量保持为零。
---@param x number 二维向量的横向分量。
---@param y number 二维向量的纵向分量。
---@return number normalized_x 归一化横向分量。
---@return number normalized_y 归一化纵向分量。
local function normalize2(x, y)
    local length = length2(x, y)
    if length <= 0.0001 then
        return 0, 0, 0
    end

    return x / length, y / length, length
end

---从 Lua 表或 UObject 结构字段读取数值，访问失败时返回回退值。
---@param value table|userdata|nil 包含目标数值字段的 Lua 表或 UObject 结构。
---@param key string 需要读取的字段名称。
---@param fallback number 字段缺失、访问异常或无法转换为数字时的回退值。
---@return number result 读取到的数值或回退值。
local function read_struct_number(value, key, fallback)
    if value == nil then
        return fallback or 0
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, result = pcall(function()
        return value[key]
    end)
    if ok and tonumber(result) ~= nil then
        return tonumber(result)
    end

    return fallback or 0
end

---在 UnLua 完成 UObject 绑定后初始化输入边沿、缓冲和 Sprint 请求状态。
---此时 UObject 仍可能处于构造阶段，因此这里只写 Lua 私有字段，不覆盖 UPROPERTY。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKInputManager:Initialize(_initializer)
    -- 以下字段只服务于 Lua 输入编排，保存在当前 UObject 对应的 Lua 实例上。
    self.MoveInputActiveThreshold = 0.1
    self.DebugMoveIntentInterval = 0.08
    self.DodgeStepNoInputForward = 1
    self.JumpWalkHorizontalSpeed = 140.0
    self.JumpRunHorizontalSpeed = 407.0
    self.JumpSprintHorizontalSpeed = 853.0
    self.JumpRootMotionRestoreTimeout = 0.25

    -- 原始方向和输入边沿在 Tick 末统一消费，避免 Enhanced Input 同帧回调顺序改变 Step 结果。
    self.CurrentMoveInputX = 0
    self.CurrentMoveInputY = 0
    self.CurrentMoveInputAmount = 0
    self.MoveInputActive = false
    self.MoveStartedThisFrame = false
    self.MoveStartedInputX = 0
    self.MoveStartedInputY = 0
    self.DodgePressedThisFrame = false
    self.SprintInputHoldTime = 0
    self.SprintRequested = false
    self.bRestrictedZoneObserved = false
    self.bWeaponTransitionInputLock = false
    self.bJumpRootMotionIgnored = false
    self.bJumpFallingObserved = false
    self.JumpRootMotionIgnoreStartTime = 0.0
    log_debug("Initialize", "input lua host initialized")
end

---查询角色当前是否处于限制输入区域。
---@param input_manager SKInputManager 输入管理器实例。
---@return boolean restricted 当前至少被一个限制区域覆盖时返回 true。
local function is_restricted(input_manager)
    return input_manager:IsRestrictedZoneActive() == true
end

---查询收刀或拔刀过渡是否正在临时禁止移动以外的输入。
---@param input_manager SKInputManager 输入管理器实例。
---@return boolean restricted 任一限制来源生效时返回 true。
local function is_action_restricted(input_manager)
    return input_manager.bWeaponTransitionInputLock == true
end

---查询武器状态是否禁止需要拔刀的战斗输入。
---过渡期间统一禁止；过渡结束后只要刀仍在鞘中就继续禁止。
---@param input_manager SKInputManager 输入管理器实例。
---@return boolean restricted 战斗输入当前不可提交时返回 true。
local function is_combat_restricted(input_manager)
    return is_action_restricted(input_manager)
        or tostring(input_manager:GetOwnerWeaponPresentationName()) == "Sheathed"
end

---在组件 BeginPlay 且 UObject 默认值复制完成后覆盖输入调参项。
---这些字段都是 USKInputManager 的 UPROPERTY，必须延后写入才能稳定覆盖 C++ 与蓝图默认值。
---@return nil 该函数只发布 Lua 输入配置，不返回业务值。
function SKInputManager:ReceiveBeginPlay()
    self.SprintHoldThreshold = 0.50
    self.DodgeActiveDuration = 0.35
    self.AnalogWalkEnterThreshold = 0.50
    self.AnalogRunEnterThreshold = 0.62
    log_debug("ReceiveBeginPlay", "input property overrides applied")
end

---按频率限制输出移动输入、Dodge、Step 和 Sprint 意图快照，强制日志用于记录状态边沿。
---@param reason string|nil 触发状态变化、日志或策略更新的业务原因。
---@param force boolean|nil 是否忽略频率限制并强制执行本次操作。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKInputManager:LogMoveIntentDebug(reason, force)
    if DebugMoveIntent ~= true then
        return
    end

    if force ~= true then
        self.DebugMoveIntentElapsed = (self.DebugMoveIntentElapsed or 0) + (self.LastTickDeltaSeconds or 0)
        if self.DebugMoveIntentElapsed < self.DebugMoveIntentInterval then
            return
        end
    end

    self.DebugMoveIntentElapsed = 0
    local move_intent = self:GetMoveIntent()
    local move_x = read_struct_number(move_intent, "X", 0)
    local move_y = read_struct_number(move_intent, "Y", 0)
    log_debug("MoveIntent", string.format(
        "reason=%s intent=(%.2f, %.2f) amount=%.2f tier=%s dodgeHeld=%s walkHeld=%s ownerDodging=%s",
        tostring(reason or "Tick"),
        move_x,
        move_y,
        self:GetMoveInputAmount() or 0,
        tostring(self:GetMovementTierName()),
        tostring(self:IsDodgeHeld()),
        tostring(self:IsWalkHeld()),
        tostring(self:IsOwnerDodging())))
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return boolean handled 始终返回 true，表示输入管理器已完成本帧缓冲和意图同步。
function SKInputManager:HandleInputTick(delta_seconds)
    local delta = delta_seconds or 0
    self.LastTickDeltaSeconds = delta
    local current_time = self:GetWorldTimeSecondsForScript()
    local zone_restricted = is_restricted(self)

    if self.bJumpRootMotionIgnored == true then
        if self:IsOwnerFalling() == true then
            self.bJumpFallingObserved = true
        elseif self.bJumpFallingObserved == true
            or current_time - self.JumpRootMotionIgnoreStartTime
                >= self.JumpRootMotionRestoreTimeout then
            -- InputManager 是 CombatComponent 的 Tick 前置；这里先恢复，保证同帧启动的 Land Montage 可以立即消费 Root Motion。
            self:SetOwnerAnimRootMotionIgnored(false)
            self.bJumpRootMotionIgnored = false
            self.bJumpFallingObserved = false
            self.JumpRootMotionIgnoreStartTime = 0.0
        end
    end

    if zone_restricted ~= self.bRestrictedZoneObserved then
        self.bRestrictedZoneObserved = zone_restricted
        self.bWeaponTransitionInputLock = true
        -- 进出边沿只清理非移动动作；已有 Step 可以自然收尾，跳跃必须落地后才会通过播放门槛。
        self:ClearRestrictedActionStateForScript()
        self.SprintInputHoldTime = 0
        self.SprintRequested = false
        self.DodgePressedThisFrame = false
        if self:IsOwnerCrouched() then
            self:UnCrouchOwner()
        end
        self:SetMovementTierByName("Walk")
        log_debug(
            "Restriction",
            zone_restricted and "entered zone; waiting for sheathe completion"
                or "left zone; waiting for draw completion")
    end

    local target_presentation = zone_restricted and "Sheathed" or "Drawn"
    if self.bWeaponTransitionInputLock == true
        and tostring(self:GetOwnerWeaponPresentationName()) == target_presentation
        and self:IsOwnerWeaponSlotAnimationPlaying() ~= true then
        self.bWeaponTransitionInputLock = false
        log_debug("Restriction", string.format(
            "%s completed; transition input lock released",
            zone_restricted and "sheathe" or "draw"))
    end

    local restricted = self.bWeaponTransitionInputLock == true

    if restricted then
        self.SprintInputHoldTime = 0
        self.SprintRequested = false
        self.DodgePressedThisFrame = false
        if self:IsOwnerCrouched() then
            self:UnCrouchOwner()
        end
        if not self:IsOwnerFalling() then
            self:SetMovementTierByName("Walk")
        end
    end

    if self:IsAttackHeld() then
        self:SetAttackHoldTime(self:GetAttackHoldTime() + delta)
    end

    if self:IsDodgeHeld() then
        local started_time = self.DodgeStartedTime or current_time
        self:SetDodgeHoldTime(math.max(0, current_time - started_time))
    end

    if self:IsProstheticHeld() then
        self:SetProstheticHoldTime(self:GetProstheticHoldTime() + delta)
    end

    if self:GetDodgeActiveTimeRemaining() > 0 then
        local remaining = math.max(0, self:GetDodgeActiveTimeRemaining() - delta)
        self:SetDodgeActiveState(remaining > 0, remaining)
        if remaining <= 0 then
            self:SetOwnerDodging(false)
        end
    end

    local time_since_attack = self:GetTimeSinceLastAttack() + delta
    local combo_index = self:GetComboIndex()
    if time_since_attack > 0.5 then
        combo_index = 0
    end
    self:SetComboState(combo_index, time_since_attack)

    self:PruneInputBufferForScript()

    local move_intent = self:GetMoveIntent()
    local move_x = read_struct_number(move_intent, "X", 0)
    local move_y = read_struct_number(move_intent, "Y", 0)
    local move_size = length2(move_x, move_y)

    -- Shift 按下始终产生一次 Step；Shift 保持期间，方向从零重新进入有效区时再产生一次。
    -- 同帧同时按下 Shift 和方向只结算一次，并使用这一帧最终采样到的方向。
    local dodge_pressed = self.DodgePressedThisFrame == true
    local held_move_started = self:IsDodgeHeld() and self.MoveStartedThisFrame == true
    if not restricted
        and (dodge_pressed or held_move_started)
        and (not self:IsOwnerFalling() or self:CanOwnerAirDodge()) then
        local step_x = 0
        local step_y = self.DodgeStepNoInputForward
        if self.MoveInputActive == true then
            step_x = self.CurrentMoveInputX or 0
            step_y = self.CurrentMoveInputY or 0
        elseif self.MoveStartedThisFrame == true then
            -- 极短方向输入可能在同一帧内开始并结束，仍保留开始边沿供本次 Step 消费。
            step_x = self.MoveStartedInputX or 0
            step_y = self.MoveStartedInputY or 0
        end

        self:SetOwnerDodgeDirection(step_y, step_x)
        self:SetOwnerDodging(true)
        self:SetDodgeActiveState(true, self.DodgeActiveDuration)
        self:SetPressedFlag("Dodge", true)
        self:AddBufferedInput("Dodge", 7, 0.1)
        log_debug("Dodge", string.format(
            "phase=StepRequested reason=%s direction=(%.2f, %.2f) moveActive=%s",
            dodge_pressed and "DodgePressed" or "HeldMoveStarted",
            step_y,
            step_x,
            tostring(self.MoveInputActive == true)))
    end

    if not restricted
        and self:IsDodgeHeld()
        and self.MoveInputActive == true
        and not self:IsOwnerFalling() then
        self.SprintInputHoldTime = (self.SprintInputHoldTime or 0) + delta
    else
        self.SprintInputHoldTime = 0
    end

    if move_size >= 0.1 then
        self:LogMoveIntentDebug("Tick", false)
    end

    local sprint_qualified = not restricted
        and self:IsDodgeHeld()
        and self.SprintInputHoldTime >= self.SprintHoldThreshold
        and self.MoveInputActive == true
        and not self:IsOwnerFalling()
    if sprint_qualified then
        if self.SprintRequested ~= true then
            self.SprintRequested = true
            log_debug("Dodge", string.format(
                "phase=SprintRequested sprintInputHoldTime=%.3f physicalHoldTime=%.3f moveAmount=%.2f",
                self.SprintInputHoldTime,
                self:GetDodgeHoldTime(),
                self.CurrentMoveInputAmount or 0))
        end
        if tostring(self:GetMovementTierName()) ~= "Sprint" then
            self:SetMovementTierByName("Sprint")
        end
    elseif restricted then
        self.SprintRequested = false
        if not self:IsOwnerFalling() then
            self:SetMovementTierByName("Walk")
        end
    elseif tostring(self:GetMovementTierName()) == "Sprint" then
        self.SprintRequested = false
        local input_amount = self.CurrentMoveInputAmount or 0
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        elseif input_amount <= self.MoveInputActiveThreshold then
            if self:IsWalkHeld() then
                self:SetMovementTierByName("Walk")
            else
                self:SetMovementTierByName("Run")
            end
        elseif self:IsWalkHeld() or input_amount <= self.AnalogWalkEnterThreshold then
            self:SetMovementTierByName("Walk")
        else
            self:SetMovementTierByName("Run")
        end
    else
        self.SprintRequested = false
    end

    self:ClearPressedFlagsForScript()
    self.DodgePressedThisFrame = false
    self.MoveStartedThisFrame = false
    return true
end

---处理屏幕空间移动输入，更新方向缓冲、步态和 Root Motion 使用的移动意图。
---@param input_x number|nil 屏幕空间横向输入，通常为 -1..1，负值表示左移。
---@param input_y number|nil 屏幕空间纵向输入，通常为 -1..1，负值表示后退。
---@return boolean handled 始终返回 true，表示移动输入已写入 C++ 组件。
function SKInputManager:HandleMoveInput(input_x, input_y)
    local x = input_x or 0
    local y = input_y or 0
    local normalized_x, normalized_y, raw_amount = normalize2(x, y)
    local input_amount = clamp(raw_amount, 0, 1)
    local has_input = input_amount > self.MoveInputActiveThreshold
    local was_active = self.MoveInputActive == true

    -- Triggered 只提交有效采样；释放统一由 OnMoveCompleted 确认，避免按住期间因回调间隔误判松键。
    if not has_input then return true end

    self.CurrentMoveInputX = normalized_x
    self.CurrentMoveInputY = normalized_y
    self.CurrentMoveInputAmount = input_amount
    self.MoveInputActive = true
    if has_input and not was_active then
        -- 开始边沿保留到 Tick，供 Shift 已按住时触发 Held+Move Step。
        self.MoveStartedThisFrame = true
        self.MoveStartedInputX = normalized_x
        self.MoveStartedInputY = normalized_y
    end

    -- 不再调用 AddMovementInput；RootMotion 位移由当前动画决定，输入这里只作为动画和朝向的意图来源。
    -- 第四个参数保留为零，仅兼容现有 C++ 接口；释放不再依赖计时缓冲。
    self:SetMoveIntentForScript(normalized_x, normalized_y, input_amount, 0)
    self:LogMoveIntentDebug("OnMoveInput", false)

    -- Sprint 只由 Tick 的持续输入判定写入；OnMove 仅维护非 Sprint 的基础移动档位。
    if is_action_restricted(self) and not self:IsOwnerFalling() then
        self:SetMovementTierByName("Walk")
    elseif tostring(self:GetMovementTierName()) ~= "Sprint"
        and not self:IsOwnerFalling() then
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        elseif self:IsWalkHeld() then
            self:SetMovementTierByName("Walk")
        elseif tostring(self:GetMovementTierName()) == "Walk" then
            if input_amount >= self.AnalogRunEnterThreshold then
                self:SetMovementTierByName("Run")
            else
                self:SetMovementTierByName("Walk")
            end
        elseif input_amount <= self.AnalogWalkEnterThreshold then
            self:SetMovementTierByName("Walk")
        else
            self:SetMovementTierByName("Run")
        end
    end

    return true
end

---处理屏幕空间移动输入的完整释放边沿，清除移动意图并退出 Sprint。
---@return boolean handled 始终返回 true，表示释放事件已经由 Lua 完整处理。
function SKInputManager:HandleMoveCompleted()
    self.CurrentMoveInputX = 0
    self.CurrentMoveInputY = 0
    self.CurrentMoveInputAmount = 0
    self.MoveInputActive = false
    self.MoveStartedThisFrame = false
    self.SprintInputHoldTime = 0
    self.SprintRequested = false
    self:ClearMoveIntentForScript()

    if is_action_restricted(self) and not self:IsOwnerFalling() then
        self:SetMovementTierByName("Walk")
    elseif not self:IsOwnerFalling() then
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        elseif self:IsWalkHeld() then
            self:SetMovementTierByName("Walk")
        else
            self:SetMovementTierByName("Run")
        end
    end

    self:LogMoveIntentDebug("OnMoveCompleted", true)
    return true
end

---处理视角输入并交给相机组件；移动输入不会通过该入口改变镜头。
---@param input_x number|nil 屏幕空间横向输入，通常为 -1..1，负值表示左移。
---@param input_y number|nil 屏幕空间纵向输入，通常为 -1..1，负值表示后退。
---@return boolean handled 始终返回 true，表示视角输入已提交。
function SKInputManager:HandleLookInput(input_x, input_y)
    self:SetLookIntentForScript(input_x or 0, input_y or 0)
    self:AddLookInputToCamera(input_x or 0, input_y or 0)
    return true
end

---处理跳跃按下：先由战斗状态机裁决取消窗口，再缓存起跳姿态并请求 Character Jump。
---@return boolean handled 始终返回 true，表示跳跃按下已处理。
function SKInputManager:HandleJumpStarted()
    local combat = self:GetOwnerCombatComponent()
    local jump_allowed = true
    local resume_air_guard = false
    if combat ~= nil then
        jump_allowed, resume_air_guard = combat:TryPrepareJump(
            self:GetWorldTimeSecondsForScript())
    end
    if jump_allowed ~= true then
        self:SetPressedFlag("Jump", false)
        return true
    end

    self:SetPressedFlag("Jump", true)
    self:AddBufferedInput("Jump", 5, 0.1)
    -- UE 默认禁止蹲姿胶囊直接 Jump；先解除物理蹲伏，AnimInstance 会保留起跳前姿态供 Lua 选择 Crouch_Jump_Start。
    if self:IsOwnerCrouched() then
        self:UnCrouchOwner()
    end

    local jump_speed = 0
    local jump_x = 0
    local jump_y = 0
    if self.MoveInputActive == true
        and (self.CurrentMoveInputAmount or 0) > self.MoveInputActiveThreshold then
        jump_x = self.CurrentMoveInputX or 0
        jump_y = self.CurrentMoveInputY or 0
        local movement_tier = tostring(self:GetMovementTierName())
        if movement_tier == "Walk" or movement_tier == "Crouch" then
            jump_speed = self.JumpWalkHorizontalSpeed
        elseif movement_tier == "Sprint" then
            jump_speed = self.JumpSprintHorizontalSpeed
        else
            jump_speed = self.JumpRunHorizontalSpeed
        end
    end

    -- 必须在 JumpOwner 前关闭动画 Root Motion：若等待 AnimInstance 下一帧更新，Jump Start 的首帧根运动会先覆盖水平起跳速度。
    -- 起跳前精确写入水平速度；零输入会清除地面 Root Motion 的残留速度，保证原地跳真正留在原地。
    -- JumpOwner 随后只覆盖 Z 速度，空中阶段由 CharacterMovement 的重力和惯性继续积分。
    -- 恢复责任由本输入层锁存；观察到 Falling 后的首次落地帧会在 CombatComponent 启动 Land Montage 前显式恢复。
    self.bJumpRootMotionIgnored = self:SetOwnerAnimRootMotionIgnored(true) == true
    self.bJumpFallingObserved = false
    self.JumpRootMotionIgnoreStartTime = self:GetWorldTimeSecondsForScript()
    self:SetHorizontalVelocityFromScreen(jump_x, jump_y, jump_speed)
    self:JumpOwner()
    if resume_air_guard == true
        and combat ~= nil
        and self:IsGuardHeld() == true then
        combat:StartGuardRaise(true)
    end
    return true
end

---处理跳跃释放并通知 Character 停止继续施加跳跃保持力。
---@return boolean handled 始终返回 true，表示跳跃释放已处理。
function SKInputManager:HandleJumpCompleted()
    self:StopJumpingOwner()
    return true
end

---处理闪避键按下边沿，立即冻结当前方向并触发一次 Step；持续按住由 Tick 决定是否升级 Sprint。
---@return boolean handled 始终返回 true，表示闪避按下已处理。
function SKInputManager:HandleDodgeStarted()
    if is_action_restricted(self) then
        self:SetHeldFlag("Dodge", false)
        self:SetDodgeHoldTime(0)
        self.SprintInputHoldTime = 0
        self.SprintRequested = false
        self.DodgePressedThisFrame = false
        self.DodgeStartedTime = nil
        return true
    end

    local started_time = self:GetWorldTimeSecondsForScript()
    self:SetHeldFlag("Dodge", true)
    self:SetDodgeHoldTime(0)
    self.SprintInputHoldTime = 0
    self.SprintRequested = false
    self.DodgePressedThisFrame = true
    self.DodgeStartedTime = started_time
    if self:IsOwnerCrouched() then
        self:UnCrouchOwner()
    end

    log_debug("Dodge", string.format(
        "phase=Pressed moveActive=%s direction=(%.2f, %.2f)",
        tostring(self.MoveInputActive == true),
        self.CurrentMoveInputY or 0,
        self.CurrentMoveInputX or 0))

    return true
end

---处理闪避键释放，结束 Sprint 请求但保留 Step 必需的最短动作时间。
---@return boolean handled 始终返回 true，表示闪避释放已处理。
function SKInputManager:HandleDodgeCompleted()
    local completed_time = self:GetWorldTimeSecondsForScript()
    local hold_time = self.DodgeStartedTime ~= nil
        and math.max(0, completed_time - self.DodgeStartedTime)
        or self:GetDodgeHoldTime()
    local sprint_input_hold_time = self.SprintInputHoldTime or 0
    self:SetDodgeHoldTime(hold_time)
    self:SetHeldFlag("Dodge", false)
    log_debug("Dodge", string.format(
        "phase=Completed physicalHoldTime=%.3f sprintInputHoldTime=%.3f sprintQualified=%s currentTier=%s ownerDodging=%s",
        hold_time,
        sprint_input_hold_time,
        tostring(sprint_input_hold_time >= self.SprintHoldThreshold),
        tostring(self:GetMovementTierName()),
        tostring(self:IsOwnerDodging())))

    local input_amount = self.CurrentMoveInputAmount or 0
    if is_action_restricted(self) then
        if self:IsOwnerCrouched() then
            self:UnCrouchOwner()
        end
        self:SetMovementTierByName("Walk")
    elseif self:IsOwnerCrouched() then
        self:SetMovementTierByName("Crouch")
    elseif input_amount <= self.MoveInputActiveThreshold then
        if self:IsWalkHeld() then
            self:SetMovementTierByName("Walk")
        else
            self:SetMovementTierByName("Run")
        end
    elseif self:IsWalkHeld() then
        self:SetMovementTierByName("Walk")
    elseif tostring(self:GetMovementTierName()) == "Walk" then
        if input_amount >= self.AnalogRunEnterThreshold then
            self:SetMovementTierByName("Run")
        else
            self:SetMovementTierByName("Walk")
        end
    elseif input_amount <= self.AnalogWalkEnterThreshold then
        self:SetMovementTierByName("Walk")
    else
        self:SetMovementTierByName("Run")
    end

    self:SetDodgeHoldTime(0)
    self.SprintInputHoldTime = 0
    self.SprintRequested = false
    self.DodgeStartedTime = nil
    return true
end

---处理 Walk 修饰键按下，把当前非 Sprint 移动档位切换为 Walk。
---@return boolean handled 始终返回 true，表示 Walk 修饰键按下已处理。
function SKInputManager:HandleWalkModifierStarted()
    self:SetHeldFlag("Walk", true)
    if tostring(self:GetMovementTierName()) ~= "Sprint" and not self:IsOwnerFalling() then
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        else
            self:SetMovementTierByName("Walk")
        end
    end
    return true
end

---处理 Walk 修饰键释放，按当前输入强度恢复 Run 或保持无输入档位。
---@return boolean handled 始终返回 true，表示 Walk 修饰键释放已处理。
function SKInputManager:HandleWalkModifierCompleted()
    self:SetHeldFlag("Walk", false)
    if is_action_restricted(self) and not self:IsOwnerFalling() then
        self:SetMovementTierByName("Walk")
    elseif tostring(self:GetMovementTierName()) == "Walk" and not self:IsOwnerFalling() then
        local input_amount = self:GetMoveInputAmount() or 0
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        elseif input_amount <= 0.1 then
            self:SetMovementTierByName("Run")
        elseif input_amount >= self.AnalogRunEnterThreshold then
            self:SetMovementTierByName("Run")
        else
            self:SetMovementTierByName("Walk")
        end
    end
    return true
end

---处理蹲姿键按下，在站立与 Crouch 之间切换并同步 MovementTier。
---@return boolean handled 始终返回 true，表示蹲姿切换已处理。
function SKInputManager:HandleCrouchStarted()
    if is_action_restricted(self) then
        if self:IsOwnerCrouched() then
            self:UnCrouchOwner()
        end
        self:SetMovementTierByName("Walk")
        return true
    end

    self:SetPressedFlag("Crouch", true)

    if self:IsOwnerCrouched() then
        self:UnCrouchOwner()
        self:SetMovementTierByName("Run")
    else
        self:CrouchOwner()
        self:SetMovementTierByName("Crouch")
    end

    return true
end

---处理攻击键按下，写入攻击意图并尝试消费对应动作缓冲。
---@return boolean handled 始终返回 true，表示攻击按下已处理。
function SKInputManager:HandleAttackStarted()
    if is_combat_restricted(self) then
        self:SetHeldFlag("Attack", false)
        self:SetAttackHoldTime(0)
        return true
    end

    self:SetPressedFlag("Attack", true)
    self:SetHeldFlag("Attack", true)
    self:SetAttackHoldTime(0)
    self:AddBufferedInput("Attack", 2, 0.1)

    local combo_index = 1
    if self:GetTimeSinceLastAttack() <= 0.5 and self:GetComboIndex() > 0 then
        combo_index = self:GetComboIndex() + 1
    end
    self:SetComboState(combo_index, 0)
    return true
end

---处理攻击键释放，清除持续攻击意图。
---@return boolean handled 始终返回 true，表示攻击释放已处理。
function SKInputManager:HandleAttackCompleted()
    self:SetHeldFlag("Attack", false)
    self:SetAttackHoldTime(0)
    return true
end

---处理防御键按下，写入 Guard 意图供战斗系统消费。
---@return boolean handled 始终返回 true，表示防御按下已处理。
function SKInputManager:HandleGuardStarted()
    if is_combat_restricted(self) then
        self:SetHeldFlag("Guard", false)
        return true
    end

    self:SetHeldFlag("Guard", true)
    self:AddBufferedInput("Guard", 5, 0.1)
    return true
end

---处理防御键释放，结束持续 Guard 意图。
---@return boolean handled 始终返回 true，表示防御释放已处理。
function SKInputManager:HandleGuardCompleted()
    self:SetHeldFlag("Guard", false)
    return true
end

---处理锁定键按下，切换或搜索锁定目标并让相机组件接管目标视角。
---@return boolean handled 始终返回 true，表示锁定输入已处理。
function SKInputManager:HandleLockOnStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("LockOn", true)
    self:ToggleLockTargetInViewForScript()
    return true
end

---处理义手键按下，写入 Prosthetic 动作意图。
---@return boolean handled 始终返回 true，表示义手按下已处理。
function SKInputManager:HandleProstheticStarted()
    if is_combat_restricted(self) then
        self:SetHeldFlag("Prosthetic", false)
        self:SetProstheticHoldTime(0)
        return true
    end

    self:SetPressedFlag("Prosthetic", true)
    self:SetHeldFlag("Prosthetic", true)
    self:SetProstheticHoldTime(0)
    self:AddBufferedInput("Prosthetic", 4, 0.1)
    return true
end

---处理义手键释放，清除持续 Prosthetic 意图。
---@return boolean handled 始终返回 true，表示义手释放已处理。
function SKInputManager:HandleProstheticCompleted()
    self:SetHeldFlag("Prosthetic", false)
    self:SetProstheticHoldTime(0)
    return true
end

---处理钩绳键按下，提交 Grapple 动作意图。
---@return boolean handled 始终返回 true，表示钩绳输入已处理。
function SKInputManager:HandleGrappleStarted()
    if is_combat_restricted(self) then
        return true
    end

    self:SetPressedFlag("Grapple", true)
    self:AddBufferedInput("Grapple", 2, 0.1)
    return true
end

---处理交互键按下，提交 Interact 动作意图。
---@return boolean handled 始终返回 true，表示交互输入已处理。
function SKInputManager:HandleInteractStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("Interact", true)
    self:AddBufferedInput("Interact", 10, 0.1)
    return true
end

---处理使用道具键按下，提交 UseItem 动作意图。
---@return boolean handled 始终返回 true，表示道具输入已处理。
function SKInputManager:HandleUseItemStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("UseItem", true)
    self:AddBufferedInput("Item", 3, 0.1)
    return true
end

---处理伤药葫芦快捷键，提交 HealingGourd 动作意图。
---@return boolean handled 始终返回 true，表示治疗道具输入已处理。
function SKInputManager:HandleHealingGourdStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("HealingGourd", true)
    self:AddBufferedInput("Item", 3, 0.1)
    return true
end

---处理下一个道具输入，通知物品系统向后切换当前快捷道具。
---@return boolean handled 始终返回 true，表示道具切换输入已处理。
function SKInputManager:HandleCycleItemNextStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("CycleItemNext", true)
    return true
end

---处理上一个道具输入，通知物品系统向前切换当前快捷道具。
---@return boolean handled 始终返回 true，表示道具切换输入已处理。
function SKInputManager:HandleCycleItemPrevStarted()
    if is_action_restricted(self) then
        return true
    end

    self:SetPressedFlag("CycleItemPrev", true)
    return true
end

---处理暂停键按下，提交 Pause 菜单意图。
---@return boolean handled 始终返回 true，表示暂停输入已处理。
function SKInputManager:HandlePauseStarted()
    self:SetPressedFlag("Pause", true)
    return true
end

---处理主菜单键按下，提交 Menu UI 意图。
---@return boolean handled 始终返回 true，表示菜单输入已处理。
function SKInputManager:HandleMenuStarted()
    self:SetPressedFlag("Menu", true)
    return true
end

return SKInputManager
