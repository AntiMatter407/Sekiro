local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKLockOnIndicator = LuaComponent:Extend("SKLockOnIndicator", {
    Debug = false,

    TargetHeightOffset = 24.0,
    IndicatorSize = 46.0,
    IndicatorThickness = 2.0,
    IndicatorOpacity = 1.0,
    IndicatorColorR = 1.0,
    IndicatorColorG = 0.35,
    IndicatorColorB = 0.05,
    IndicatorColorA = 1.0,

    NearDistance = 280.0,
    FarDistance = 1500.0,
    NearScale = 1.12,
    FarScale = 0.82,
    PositionInterpSpeed = 24.0,
})

local function clamp(value, min_value, max_value)
    if value < min_value then
        return min_value
    end

    if value > max_value then
        return max_value
    end

    return value
end

local function lerp(from_value, to_value, alpha)
    return from_value + (to_value - from_value) * alpha
end

function SKLockOnIndicator:Construct(_context)
    self:LogDebug("Construct", "lock-on indicator lua host constructed")
end

function SKLockOnIndicator:HideIndicator()
    self:SetLockOnIndicatorVisible(false)
    self.bHasSmoothedScreenPosition = false
end

function SKLockOnIndicator:GetDistanceScale()
    local distance = self:GetLockTargetDistance()
    local range = math.max(1.0, self.FarDistance - self.NearDistance)
    local alpha = clamp((distance - self.NearDistance) / range, 0.0, 1.0)
    return lerp(self.NearScale, self.FarScale, alpha)
end

function SKLockOnIndicator:GetSmoothedScreenPosition(delta_seconds)
    local target_x = self:GetCachedLockTargetScreenX()
    local target_y = self:GetCachedLockTargetScreenY()

    if self.bHasSmoothedScreenPosition ~= true then
        self.SmoothedScreenX = target_x
        self.SmoothedScreenY = target_y
        self.bHasSmoothedScreenPosition = true
        return target_x, target_y
    end

    local alpha = clamp((delta_seconds or 0.0) * self.PositionInterpSpeed, 0.0, 1.0)
    self.SmoothedScreenX = lerp(self.SmoothedScreenX or target_x, target_x, alpha)
    self.SmoothedScreenY = lerp(self.SmoothedScreenY or target_y, target_y, alpha)
    return self.SmoothedScreenX, self.SmoothedScreenY
end

function SKLockOnIndicator:ApplyIndicatorStyle()
    self:SetLockOnIndicatorSize(self.IndicatorSize)
    self:SetLockOnIndicatorThickness(self.IndicatorThickness)
    self:SetLockOnIndicatorScale(self:GetDistanceScale())
    self:SetLockOnIndicatorOpacity(self.IndicatorOpacity)
    self:SetLockOnIndicatorColorRGBA(
        self.IndicatorColorR,
        self.IndicatorColorG,
        self.IndicatorColorB,
        self.IndicatorColorA)
end

function SKLockOnIndicator:Tick(_context, delta_seconds)
    self:RefreshCachedLockOnComponents()
    if not self:HasOwnerCharacter() or not self:IsLocalPlayerControlled() then
        self:HideIndicator()
        return true
    end

    self:ValidateLockTargetForScript()
    if not self:IsLockedOn() then
        self:HideIndicator()
        return true
    end

    if not self:UpdateLockTargetScreenPositionForScript(self.TargetHeightOffset) then
        self:HideIndicator()
        return true
    end

    if not self:EnsureLockOnIndicatorWidget() then
        return true
    end

    local screen_x, screen_y = self:GetSmoothedScreenPosition(delta_seconds)
    self:ApplyIndicatorStyle()
    self:SetLockOnIndicatorScreenPositionXY(screen_x, screen_y)
    self:SetLockOnIndicatorVisible(true)
    return true
end

return SKLockOnIndicator:Export()
