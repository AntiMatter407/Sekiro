local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKHUD = LuaComponent:Extend("SKHUD", {
    Debug = false,
})

local LayerZOrder = {
    Background = 0,
    HUD = 10,
    Indicator = 80,
    Menu = 100,
    Modal = 200,
    Debug = 900,
}

---在 Construct 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKHUD:Construct(_context)
    self:LogDebug("Construct", "hud lua host constructed")
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
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@return boolean handled 始终返回 true，表示 HUD 已完成 Lua UI 框架接入。
function SKHUD:BeginPlay(_context)
    self:RefreshCachedHUDOwner()

    local ui_manager = self:GetUIManagerSafe()
    if ui_manager ~= nil then
        self:ConfigureLayers(ui_manager)
        ui_manager:SetUseLuaUIManagerLogic(true)
        ui_manager:SetLuaUIManagerModuleName("Gameplay.Sekiro.UI.SKUIManager")
    end

    return true
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@param _delta_seconds number|nil C++ Tick 传入的本帧秒数；当前函数无需逐帧时间但保留签名兼容。
---@return boolean handled 始终返回 true，表示 HUD 已刷新本帧所有者缓存。
function SKHUD:Tick(_context, _delta_seconds)
    self:RefreshCachedHUDOwner()
    return true
end

return SKHUD:Export()
