local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKUIManager = LuaComponent:Extend("SKUIManager", {
    Debug = false,
})

local LayerName = {
    Background = "Background",
    HUD = "HUD",
    Indicator = "Indicator",
    Menu = "Menu",
    Modal = "Modal",
    Debug = "Debug",
}

function SKUIManager:Construct(_context)
    self:LogDebug("Construct", "ui manager lua host constructed")
end

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

function SKUIManager:CloseWidget(widget_name)
    if widget_name == nil then
        return
    end

    self:HideWidget(widget_name)
end

function SKUIManager:DestroyWidget(widget_name)
    if widget_name == nil then
        return
    end

    self:RemoveWidget(widget_name)
end

function SKUIManager:ShowCursor()
    self:SetMouseCursorVisible(true)
end

function SKUIManager:HideCursor()
    self:SetMouseCursorVisible(false)
end

function SKUIManager:UseGameInput()
    self:SetGameOnlyInputMode()
end

function SKUIManager:UseMenuInput(focus_widget_name)
    self:SetUIOnlyInputMode(focus_widget_name or "", true)
end

function SKUIManager:UseGameAndUIInput(focus_widget_name)
    self:SetGameAndUIInputMode(focus_widget_name or "", true)
end

function SKUIManager:Tick(_context, _delta_seconds)
    self:RefreshCachedUIOwner()
    if not self:HasPlayerController() or not self:IsLocalPlayerController() then
        return true
    end

    return true
end

return SKUIManager:Export()
