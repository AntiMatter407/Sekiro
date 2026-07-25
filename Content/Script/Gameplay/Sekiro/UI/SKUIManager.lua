-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKUIManagerComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKUIManager: USKUIManagerComponent
local SKUIManager = UnLua.Class()
local Debug = false

local LayerName = {
    Background = "Background",
    HUD = "HUD",
    Indicator = "Indicator",
    Menu = "Menu",
    Modal = "Modal",
    Debug = "Debug",
}

---在 UnLua 完成 UObject 绑定后初始化 UI Manager 脚本状态。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function SKUIManager:Initialize(_initializer)
    LuaLog.Debug(Debug, "SKUIManager", "Initialize", "ui manager lua host initialized")
end

---按软类路径创建或复用控件并显示到指定 UI 层。
---控件名为空、类加载失败或 C++ 创建失败时返回 false。
---@param widget_name string UI 管理器中用于唯一标识控件实例的名称。
---@param widget_class_path string UMG Widget Blueprint 的 UE 软类路径。
---@param layer_name string|nil UI 层名称；缺失时使用 HUD 层。
---@param z_order_offset number|nil 相对 UI 层基础 ZOrder 的附加偏移。
---@return boolean opened 控件是否已经存在或成功创建并显示。
function SKUIManager:OpenWidgetByPath(widget_name, widget_class_path, layer_name, z_order_offset)
    local name = widget_name or ""
    if name == "" then
        return false
    end

    if not self:HasManagedWidget(name) then
        local created = self:CreateWidgetByPath(
            name,
            widget_class_path or "",
            layer_name or LayerName.HUD,
            z_order_offset or 0)
        if created ~= true then
            return false
        end
    end

    self:ShowWidget(name)
    return true
end

---按已解析的 UUserWidget 类创建或复用控件并显示到指定 UI 层。
---该入口避免重复加载类资源，适合调用方已经持有 Widget Class 的场景。
---@param widget_name string UI 管理器中用于唯一标识控件实例的名称。
---@param widget_class userdata|table 已经解析的 UUserWidget 类对象。
---@param layer_name string|nil UI 层名称；缺失时使用 HUD 层。
---@param z_order_offset number|nil 相对 UI 层基础 ZOrder 的附加偏移。
---@return boolean opened 控件是否已经存在或成功创建并显示。
function SKUIManager:OpenWidgetByClass(widget_name, widget_class, layer_name, z_order_offset)
    local name = widget_name or ""
    if name == "" then
        return false
    end

    if not self:HasManagedWidget(name) then
        local created = self:CreateWidgetByClass(
            name,
            widget_class,
            layer_name or LayerName.HUD,
            z_order_offset or 0)
        if created ~= true then
            return false
        end
    end

    self:ShowWidget(name)
    return true
end

---隐藏已管理控件但保留实例，供后续再次打开时复用。
---@param widget_name string UI 管理器中用于唯一标识控件实例的名称。
---@return nil 该函数只改变控件可见性。
function SKUIManager:CloseWidget(widget_name)
    if widget_name == nil then
        return
    end

    self:HideWidget(widget_name)
end

---从 UI 管理器移除控件并释放受管实例。
---@param widget_name string UI 管理器中用于唯一标识控件实例的名称。
---@return nil 该函数只移除控件实例。
function SKUIManager:DestroyWidget(widget_name)
    if widget_name == nil then
        return
    end

    self:RemoveWidget(widget_name)
end

---显示鼠标光标，通常与菜单输入模式配套使用。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKUIManager:ShowCursor()
    self:SetMouseCursorVisible(true)
end

---隐藏鼠标光标，恢复纯游戏操作时调用。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKUIManager:HideCursor()
    self:SetMouseCursorVisible(false)
end

---把 PlayerController 输入模式切换为仅游戏输入。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKUIManager:UseGameInput()
    self:SetGameOnlyInputMode()
end

---切换为仅 UI 输入，并可把键盘焦点交给指定受管控件。
---@param focus_widget_name string|nil 切换输入模式后优先获得焦点的控件名称。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKUIManager:UseMenuInput(focus_widget_name)
    self:SetUIOnlyInputMode(focus_widget_name or "", true)
end

---切换为游戏与 UI 混合输入，并可指定初始焦点控件。
---@param focus_widget_name string|nil 切换输入模式后优先获得焦点的控件名称。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function SKUIManager:UseGameAndUIInput(focus_widget_name)
    self:SetGameAndUIInputMode(focus_widget_name or "", true)
end

---执行本模块的逐帧更新，把最新输入、状态或 UI 结果同步到 C++ 运行时。
---@param _delta_seconds number|nil C++ Tick 传入的本帧秒数；当前函数无需逐帧时间但保留签名兼容。
---@return boolean handled 始终返回 true，表示 Lua 已完成 UI 管理器本帧刷新。
function SKUIManager:HandleUIManagerTick(_delta_seconds)
    self:RefreshCachedUIOwner()
    if not self:HasPlayerController() or not self:IsLocalPlayerController() then
        return true
    end

    return true
end

return SKUIManager
