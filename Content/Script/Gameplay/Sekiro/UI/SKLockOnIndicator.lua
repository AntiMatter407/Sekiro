-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKLockOnIndicatorComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
local LuaLog = require("Gameplay.Base.LuaLog")
local Style = require("Gameplay.Sekiro.UI.CombatHUDStyle")

---@class SKLockOnIndicator: USKLockOnIndicatorComponent
local SKLockOnIndicator = UnLua.Class()
local Debug = false

---把数值限制在给定闭区间内。
---@param value number 需要限制范围的数值。
---@param min_value number 允许范围的最小值。
---@param max_value number 允许范围的最大值。
---@return number clamped 限制到最小值和最大值之间的数值。
local function clamp(value, min_value, max_value)
    if value < min_value then
        return min_value
    end

    if value > max_value then
        return max_value
    end

    return value
end

---在两个数值之间执行线性插值，用于锁定点缩放和屏幕位置平滑。
---@param from_value number 插值起点值。
---@param to_value number 插值终点值。
---@param alpha number 0..1 的混合或插值权重。
---@return number value 计算或回退后的数值。
local function lerp(from_value, to_value, alpha)
    return from_value + (to_value - from_value) * alpha
end

---在 UnLua 完成 UObject 绑定后初始化锁定点脚本状态。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKLockOnIndicator:Initialize(_initializer)
    local lock_style = Style.LockOn
    self.TargetBoneName = lock_style.TargetBoneName
    self.DisplayTarget = nil
    self.IndicatorSize = lock_style.Size
    self.IndicatorThickness = 2.0
    self.IndicatorOpacity = 1.0
    self.IndicatorColorR = lock_style.Tint.R
    self.IndicatorColorG = lock_style.Tint.G
    self.IndicatorColorB = lock_style.Tint.B
    self.IndicatorColorA = lock_style.Tint.A
    self.PositionInterpSpeed = lock_style.PositionInterpSpeed
    self.bOriginalTextureAttempted = false
    self.bOriginalTextureReady = false
    LuaLog.Debug(
        Debug,
        "SKLockOnIndicator",
        "Initialize",
        "lock-on indicator lua host initialized")
end

---首次本地显示前加载已导入的原纹理，只尝试一次，避免缺资产时每帧同步加载和刷屏。
---@return boolean ready 原纹理及图集 UV 已被 C++ 接受时返回 true；失败保持隐藏。
function SKLockOnIndicator:EnsureOriginalTexture()
    if self.bOriginalTextureAttempted then
        return self.bOriginalTextureReady
    end
    self.bOriginalTextureAttempted = true
    self:SetLockOnIndicatorDebugDrawing(false)
    local lock_style = Style.LockOn
    local texture = UE.UObject.Load(lock_style.Texture)
    if texture ~= nil and UE.UKismetSystemLibrary.IsValid(texture) then
        self.bOriginalTextureReady = self:SetLockOnIndicatorTexture(
            texture,
            UE.FVector2D(lock_style.UVMin.X, lock_style.UVMin.Y),
            UE.FVector2D(lock_style.UVMax.X, lock_style.UVMax.Y))
    end
    if not self.bOriginalTextureReady then
        LuaLog.Debug(true, "SKLockOnIndicator", "EnsureOriginalTexture", "原版锁定贴图缺失或 UV 无效：" .. lock_style.Texture)
    end
    return self.bOriginalTextureReady
end

---隐藏锁定点并清除屏幕位置平滑缓存，重新显示时从目标真实位置开始。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKLockOnIndicator:HideIndicator()
    self:SetLockOnIndicatorVisible(false)
    self.bHasSmoothedScreenPosition = false
    self.DisplayTarget = nil
end

---按原版参考画布适配显示尺寸，不延用旧橙色圆环的距离缩放规则。
---@return number scale 仅用于 UMG RenderScale；除去 UMG DPI 后避免尺寸被重复放大。
function SKLockOnIndicator:GetDisplayScale()
    local viewport = UE.UWidgetLayoutLibrary.GetViewportSize(self)
    local dpi_scale = UE.UWidgetLayoutLibrary.GetViewportScale(self)
    if viewport.X <= 0 or viewport.Y <= 0 or dpi_scale <= 0 then
        return 1
    end
    return math.min(viewport.X / Style.ReferenceSize.X, viewport.Y / Style.ReferenceSize.Y) / dpi_scale
end

---把锁定目标屏幕坐标按帧时间平滑，首次显示时直接使用目标坐标避免从屏幕原点飞入。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return number screen_x 平滑后的屏幕横坐标，单位为像素。
---@return number screen_y 平滑后的屏幕纵坐标，单位为像素。
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

---把原版尺寸、视口缩放、透明度和颜色写入 C++ 锁定点控件。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKLockOnIndicator:ApplyIndicatorStyle()
    self:SetLockOnIndicatorSize(self.IndicatorSize)
    self:SetLockOnIndicatorThickness(self.IndicatorThickness)
    self:SetLockOnIndicatorScale(self:GetDisplayScale())
    self:SetLockOnIndicatorOpacity(self.IndicatorOpacity)
    self:SetLockOnIndicatorColorRGBA(
        self.IndicatorColorR,
        self.IndicatorColorG,
        self.IndicatorColorB,
        self.IndicatorColorA)
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return boolean handled 始终返回 true；目标无效时会先隐藏锁定点。
function SKLockOnIndicator:HandleLockOnIndicatorTick(delta_seconds)
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

    local target = self:GetLockTarget()
    if self.DisplayTarget ~= target then
        -- 换目标时不能从上一敌人的腰部插值过来，否则标记短暂悬浮在两个角色之间。
        self.DisplayTarget = target
        self.bHasSmoothedScreenPosition = false
    end
    if not self:UpdateLockTargetScreenPositionForScript(self.TargetBoneName) then
        self:HideIndicator()
        return true
    end

    if not self:EnsureOriginalTexture() or not self:EnsureLockOnIndicatorWidget() then
        self:HideIndicator()
        return true
    end

    local screen_x, screen_y = self:GetSmoothedScreenPosition(delta_seconds)
    self:ApplyIndicatorStyle()
    self:SetLockOnIndicatorScreenPositionXY(screen_x, screen_y)
    self:SetLockOnIndicatorVisible(true)
    return true
end

return SKLockOnIndicator
