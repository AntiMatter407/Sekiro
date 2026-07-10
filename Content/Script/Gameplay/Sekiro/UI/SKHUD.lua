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

function SKHUD:Construct(_context)
    self:LogDebug("Construct", "hud lua host constructed")
end

function SKHUD:GetUIManagerSafe()
    local ok, ui_manager = pcall(function()
        return self:GetUIManager()
    end)

    if ok then
        return ui_manager
    end

    return nil
end

function SKHUD:ConfigureLayers(ui_manager)
    for layer_name, z_order in pairs(LayerZOrder) do
        ui_manager:SetLayerZOrder(layer_name, z_order)
    end
end

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

function SKHUD:Tick(_context, _delta_seconds)
    self:RefreshCachedHUDOwner()
    return true
end

return SKHUD:Export()
