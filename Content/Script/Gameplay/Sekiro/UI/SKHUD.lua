-- Lua 类型：UnLua UObject 运行时类。self 是真实的 ASKHUD，可直接读写 UPROPERTY 并调用 UFUNCTION。
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKHUD: ASKHUD
---@field CombatHUDWidget SKCombatHUD|nil 原生原图显示控件，生命周期由 UIManager 管理。
local SKHUD = UnLua.Class()
local Debug = false

local LayerZOrder = {
    Background = 0,
    HUD = 10,
    Indicator = 80,
    Menu = 100,
    Modal = 200,
    Debug = 900,
}

---在 UnLua 完成 UObject 绑定后初始化 HUD 脚本状态。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKHUD:Initialize(_initializer)
    self.CombatHUDWidget = nil
    LuaLog.Debug(Debug, "SKHUD", "Initialize", "hud lua host initialized")
end

---通过受保护调用获取 C++ UI Manager，未配置组件或反射调用失败时返回 nil。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function SKHUD:GetUIManagerSafe()
    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return userdata|table|nil ui_manager C++ 返回的 UI Manager；接口不可用时由 pcall 捕获异常。
    local ok, ui_manager = pcall(function()
        return self:GetUIManager()
    end)

    if ok then
        return ui_manager
    end

    return nil
end

---把项目约定的 UI 层级 ZOrder 写入 C++ UI Manager。
---@param ui_manager userdata|table 负责 UI 层级、控件创建和输入模式的 C++ UI 管理器。
---@return nil 该函数只写入层级配置。
function SKHUD:ConfigureLayers(ui_manager)
    for layer_name, z_order in pairs(LayerZOrder) do
        ui_manager:SetLayerZOrder(layer_name, z_order)
    end
end

---在 BeginPlay 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@return boolean handled 始终返回 true，表示 HUD 已完成 Lua UI 框架接入。
function SKHUD:HandleHUDInitialized()
    self:RefreshCachedHUDOwner()

    local ui_manager = self:GetUIManagerSafe()
    if ui_manager ~= nil then
        self:ConfigureLayers(ui_manager)
        ui_manager:SetUseLuaUIManagerLogic(true)
        ui_manager:SetLuaUIManagerModuleName("Gameplay.Sekiro.UI.SKUIManager")
        if self:IsLocalPlayerController() then
            local created = ui_manager:CreateWidgetByClass("CombatHUD", UE.USKCombatHUDWidget.StaticClass(), "HUD", 0)
            if created then
                self.CombatHUDWidget = ui_manager:GetManagedWidget("CombatHUD")
                self.CombatHUDWidget:InitializeCombatHUD(self)
                -- 原图HUD不抢焦点、不改变控制器输入模式，也不拦截其他控件命中。
                self.CombatHUDWidget:SetVisibility(UE.ESlateVisibility.HitTestInvisible)
            end
        end
    end

    return true
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param _delta_seconds number|nil C++ Tick 传入的本帧秒数；当前函数无需逐帧时间但保留签名兼容。
---@return boolean handled 始终返回 true，表示 HUD 已刷新本帧所有者缓存。
function SKHUD:HandleHUDTick(_delta_seconds)
    self:RefreshCachedHUDOwner()
    if self.CombatHUDWidget ~= nil and UE.UKismetSystemLibrary.IsValid(self.CombatHUDWidget) then
        self.CombatHUDWidget:RefreshBindingsFromTick(_delta_seconds)
    end
    return true
end

---接收外部明确指定的Boss展示对象，脱锁和普通目标切换不会调用此入口。
---@param target_actor AActor|nil 新Boss展示目标；nil显式关闭。
---@param _display_name FText 外部名称资料，原字体和布局未核验前不绘制替代文字。
---@return nil result 仅更新UI绑定，不设置Boss阶段或忍杀节点。
function SKHUD:HandleBossDisplayTargetChanged(target_actor, _display_name)
    if self.CombatHUDWidget ~= nil and UE.UKismetSystemLibrary.IsValid(self.CombatHUDWidget) then
        self.CombatHUDWidget:SetBossTarget(target_actor)
    end
end

---在原生HUD移除Widget之前解除所有Survival与控制器订阅。
---@return nil result 重复调用安全，UIManager仍负责原生控件移除。
function SKHUD:HandleHUDShutdown()
    if self.CombatHUDWidget ~= nil and UE.UKismetSystemLibrary.IsValid(self.CombatHUDWidget) then
        self.CombatHUDWidget:ShutdownCombatHUD()
    end
    self.CombatHUDWidget = nil
end

return SKHUD
