local M = {}

local GroundLocomotion = require("Sekiro.AnimBlueprint.Rule.GroundLocomotion")

M.DefaultLayerName = "GroundLocomotion"

M.Layers = {
    GroundLocomotion = GroundLocomotion,
}

local function normalize_layer_name(layer_name)
    if layer_name == nil then
        return M.DefaultLayerName
    end

    local text = tostring(layer_name)
    if text == "" or text == "None" then
        return M.DefaultLayerName
    end

    return text
end

local function find_layer(layer_name)
    return M.Layers[normalize_layer_name(layer_name)]
end

function M.UpdateLayer(context, layer_name, delta_seconds, optional_delta_seconds)
    if context == M then
        context = layer_name
        layer_name = delta_seconds
        delta_seconds = optional_delta_seconds
    end

    local layer = find_layer(layer_name)
    if type(layer) ~= "table" or type(layer.Update) ~= "function" then
        return nil
    end

    return layer.Update(context, delta_seconds)
end

function M.Update(context, delta_seconds, optional_delta_seconds)
    if context == M then
        context = delta_seconds
        delta_seconds = optional_delta_seconds
    end

    return M.UpdateLayer(context, M.DefaultLayerName, delta_seconds)
end

return M
