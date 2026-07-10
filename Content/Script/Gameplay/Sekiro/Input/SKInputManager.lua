-- 管理玩家输入到角色意图的转换。
-- RootMotion 版只写入 MoveIntent、MovementTier 和 DodgeDirection，角色位移由动画根运动驱动。
local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKInputManager = LuaComponent:Extend("SKInputManager", {
    Debug = false,
    SprintHoldThreshold = 0.30,
    DodgeActiveDuration = 0.35,
    MoveInputReleaseBufferDuration = 0.08,
    AnalogWalkEnterThreshold = 0.50,
    AnalogRunEnterThreshold = 0.62,
    DodgeStepNoInputForward = 1,
})

local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

local function length2(x, y)
    return math.sqrt(x * x + y * y)
end

local function normalize2(x, y)
    local length = length2(x, y)
    if length <= 0.0001 then
        return 0, 0, 0
    end

    return x / length, y / length, length
end

local function read_struct_number(value, key, fallback)
    if value == nil then
        return fallback or 0
    end

    local ok, result = pcall(function()
        return value[key]
    end)
    if ok and tonumber(result) ~= nil then
        return tonumber(result)
    end

    return fallback or 0
end

function SKInputManager:Construct(_context)
    self:LogDebug("Construct", "input lua host constructed")
end

function SKInputManager:Tick(_context, delta_seconds)
    local delta = delta_seconds or 0

    if self:IsAttackHeld() then
        self:SetAttackHoldTime(self:GetAttackHoldTime() + delta)
    end

    if self:IsDodgeHeld() then
        self:SetDodgeHoldTime(self:GetDodgeHoldTime() + delta)
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

    if self:GetMoveInputReleaseBufferRemaining() > 0 then
        local remaining = math.max(0, self:GetMoveInputReleaseBufferRemaining() - delta)
        self:SetMoveInputReleaseBufferRemaining(remaining)
        if remaining <= 0 then
            self:ClearMoveIntentForScript()
            if self:IsOwnerCrouched() then
                self:SetMovementTierByName("Crouch")
            elseif self:IsWalkHeld() then
                self:SetMovementTierByName("Walk")
            else
                self:SetMovementTierByName("Run")
            end
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

    if self:IsDodgeHeld()
        and self:GetDodgeHoldTime() >= self.SprintHoldThreshold
        and move_size >= 0.1
        and not self:IsOwnerFalling()
        and tostring(self:GetMovementTierName()) ~= "Sprint" then
        self:SetMovementTierByName("Sprint")
    elseif tostring(self:GetMovementTierName()) == "Sprint" then
        if move_size < 0.1 or self:IsOwnerFalling() then
            local input_amount = self:GetMoveInputAmount() or 0
            if self:IsOwnerCrouched() then
                self:SetMovementTierByName("Crouch")
            elseif input_amount <= 0.1 then
                if self:IsWalkHeld() then
                    self:SetMovementTierByName("Walk")
                else
                    self:SetMovementTierByName("Run")
                end
            elseif self:IsWalkHeld() then
                self:SetMovementTierByName("Walk")
            elseif input_amount <= self.AnalogWalkEnterThreshold then
                self:SetMovementTierByName("Walk")
            else
                self:SetMovementTierByName("Run")
            end
        end
    end

    self:ClearPressedFlagsForScript()
    return true
end

function SKInputManager:OnMove(_context, input_x, input_y)
    local x = input_x or 0
    local y = input_y or 0
    local normalized_x, normalized_y, raw_amount = normalize2(x, y)
    local input_amount = clamp(raw_amount, 0, 1)
    local has_input = input_amount > 0.1

    if has_input then
        -- 不再调用 AddMovementInput；RootMotion 位移由当前动画决定，输入这里只作为动画和朝向的意图来源。
        self:SetMoveIntentForScript(normalized_x, normalized_y, input_amount, self.MoveInputReleaseBufferDuration)
    elseif self:GetMoveInputReleaseBufferRemaining() <= 0 then
        self:ClearMoveIntentForScript()
    end

    if tostring(self:GetMovementTierName()) == "Sprint" then
        if input_amount < 0.1 or self:IsOwnerFalling() then
            if self:IsOwnerCrouched() then
                self:SetMovementTierByName("Crouch")
            elseif input_amount <= 0.1 then
                if self:IsWalkHeld() then
                    self:SetMovementTierByName("Walk")
                else
                    self:SetMovementTierByName("Run")
                end
            elseif self:IsWalkHeld() then
                self:SetMovementTierByName("Walk")
            elseif input_amount <= self.AnalogWalkEnterThreshold then
                self:SetMovementTierByName("Walk")
            else
                self:SetMovementTierByName("Run")
            end
        end
    elseif not self:IsOwnerFalling() and (has_input or self:GetMoveInputReleaseBufferRemaining() <= 0) then
        if self:IsOwnerCrouched() then
            self:SetMovementTierByName("Crouch")
        elseif self:IsDodgeHeld()
            and self:GetDodgeHoldTime() >= self.SprintHoldThreshold
            and input_amount > 0.1 then
            self:SetMovementTierByName("Sprint")
        elseif input_amount <= 0.1 then
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
    end

    if self:IsOwnerDodging() and has_input then
        self:SetOwnerDodgeDirection(normalized_y, normalized_x)
    end

    return true
end

function SKInputManager:OnLook(_context, input_x, input_y)
    self:SetLookIntentForScript(input_x or 0, input_y or 0)
    self:AddLookInputToCamera(input_x or 0, input_y or 0)
    return true
end

function SKInputManager:OnJumpStarted(_context)
    self:SetPressedFlag("Jump", true)
    self:AddBufferedInput("Jump", 5, 0.1)
    self:JumpOwner()
    return true
end

function SKInputManager:OnJumpCompleted(_context)
    self:StopJumpingOwner()
    return true
end

function SKInputManager:OnDodgeStarted(_context)
    self:SetHeldFlag("Dodge", true)
    self:SetDodgeHoldTime(0)
    if self:IsOwnerCrouched() then
        self:UnCrouchOwner()
    end
    return true
end

function SKInputManager:OnDodgeCompleted(_context)
    local was_short_press = self:GetDodgeHoldTime() < self.SprintHoldThreshold
    self:SetHeldFlag("Dodge", false)

    if was_short_press then
        if not self:IsOwnerFalling() or self:CanOwnerAirDodge() then
            local move_intent = self:GetMoveIntent()
            local move_x = read_struct_number(move_intent, "X", 0)
            local move_y = read_struct_number(move_intent, "Y", 0)

            if length2(move_x, move_y) <= 0.1 then
                move_x = 0
                move_y = self.DodgeStepNoInputForward
            end

            self:SetOwnerDodgeDirection(move_y, move_x)
            self:SetOwnerDodging(true)
            self:SetDodgeActiveState(true, self.DodgeActiveDuration)
            -- Step 的实际位移交给 Step 动画 RootMotion，避免额外冲量与根运动叠加导致方向偏离输入。
            self:SetPressedFlag("Dodge", true)
            self:AddBufferedInput("Dodge", 7, 0.1)
        end
    end

    local input_amount = self:GetMoveInputAmount() or 0
    if self:IsOwnerCrouched() then
        self:SetMovementTierByName("Crouch")
    elseif input_amount <= 0.1 then
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
    return true
end

function SKInputManager:OnWalkModifierStarted(_context)
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

function SKInputManager:OnWalkModifierCompleted(_context)
    self:SetHeldFlag("Walk", false)
    if tostring(self:GetMovementTierName()) == "Walk" and not self:IsOwnerFalling() then
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

function SKInputManager:OnCrouchStarted(_context)
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

function SKInputManager:OnAttackStarted(_context)
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

function SKInputManager:OnAttackCompleted(_context)
    self:SetHeldFlag("Attack", false)
    self:SetAttackHoldTime(0)
    return true
end

function SKInputManager:OnGuardStarted(_context)
    self:SetHeldFlag("Guard", true)
    self:AddBufferedInput("Guard", 5, 0.1)
    return true
end

function SKInputManager:OnGuardCompleted(_context)
    self:SetHeldFlag("Guard", false)
    return true
end

function SKInputManager:OnLockOnStarted(_context)
    self:SetPressedFlag("LockOn", true)
    self:ToggleLockTargetInViewForScript()
    return true
end

function SKInputManager:OnProstheticStarted(_context)
    self:SetPressedFlag("Prosthetic", true)
    self:SetHeldFlag("Prosthetic", true)
    self:SetProstheticHoldTime(0)
    self:AddBufferedInput("Prosthetic", 4, 0.1)
    return true
end

function SKInputManager:OnProstheticCompleted(_context)
    self:SetHeldFlag("Prosthetic", false)
    self:SetProstheticHoldTime(0)
    return true
end

function SKInputManager:OnGrappleStarted(_context)
    self:SetPressedFlag("Grapple", true)
    self:AddBufferedInput("Grapple", 2, 0.1)
    return true
end

function SKInputManager:OnInteractStarted(_context)
    self:SetPressedFlag("Interact", true)
    self:AddBufferedInput("Interact", 10, 0.1)
    return true
end

function SKInputManager:OnUseItemStarted(_context)
    self:SetPressedFlag("UseItem", true)
    self:AddBufferedInput("Item", 3, 0.1)
    return true
end

function SKInputManager:OnHealingGourdStarted(_context)
    self:SetPressedFlag("HealingGourd", true)
    self:AddBufferedInput("Item", 3, 0.1)
    return true
end

function SKInputManager:OnCycleItemNextStarted(_context)
    self:SetPressedFlag("CycleItemNext", true)
    return true
end

function SKInputManager:OnCycleItemPrevStarted(_context)
    self:SetPressedFlag("CycleItemPrev", true)
    return true
end

function SKInputManager:OnPauseStarted(_context)
    self:SetPressedFlag("Pause", true)
    return true
end

function SKInputManager:OnMenuStarted(_context)
    self:SetPressedFlag("Menu", true)
    return true
end

return SKInputManager:Export()
