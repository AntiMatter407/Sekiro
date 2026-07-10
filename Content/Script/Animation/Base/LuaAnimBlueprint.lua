local LuaAnimBlueprint = {}

LuaAnimBlueprint.__index = LuaAnimBlueprint
LuaAnimBlueprint.__call = function(class, config)
    return class:new(config)
end

LuaAnimBlueprint.ClassName = "LuaAnimBlueprint"
LuaAnimBlueprint.IsLuaAnimBlueprint = true
LuaAnimBlueprint.DefaultLayerName = "Default"
LuaAnimBlueprint.Debug = false
LuaAnimBlueprint.DebugUpdateInterval = 0.5
LuaAnimBlueprint.DebugTransitions = true
LuaAnimBlueprint.DebugPose = true
LuaAnimBlueprint.DebugContext = true

local DebugConfigKeys = {
    "Debug",
    "DebugUpdateInterval",
    "DebugTransitions",
    "DebugTransitionChecks",
    "DebugPose",
    "DebugPoseEveryFrame",
    "DebugContext",
}

local RuntimeFieldSkip = {
    Class = true,
    Super = true,
    super = true,
    Layers = true,
    DefaultLayerName = true,
    Context = true,
    ContextObject = true,
    RuntimeContext = true,
    UpdateContext = true,
}

local function copy_table(source)
    local target = {}
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

local function define_table(target, source)
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

local function try_call(call)
    local ok, result = pcall(call)
    if ok then
        return result
    end

    return nil
end

local function resolve_context_object(context)
    if context == nil then
        return nil
    end

    local object = try_call(function()
        return context.Object
    end)
    if object ~= nil then
        return object
    end

    if type(context) ~= "table" then
        return context
    end

    return nil
end

local function read_context_index(instance, key)
    local runtime_context = rawget(instance, "RuntimeContext")
    if type(runtime_context) == "table" then
        local runtime_value = runtime_context[key]
        if runtime_value ~= nil then
            return runtime_value
        end
    end

    local context = rawget(instance, "ContextObject")
    if context == nil then
        return nil
    end

    local ok, value = pcall(function()
        return context[key]
    end)
    if not ok or value == nil then
        return nil
    end

    if type(value) ~= "function" then
        return value
    end

    return function(_self, ...)
        return value(context, ...)
    end
end

local function make_instance_metatable(class)
    return {
        __index = function(instance, key)
            local value = class[key]
            if value ~= nil then
                return value
            end

            return read_context_index(instance, key)
        end,
    }
end

local function inherit_debug_config(owner, config)
    local layer_config = copy_table(config)
    for _, key in ipairs(DebugConfigKeys) do
        if layer_config[key] == nil and owner[key] ~= nil then
            layer_config[key] = owner[key]
        end
    end

    return layer_config
end

function LuaAnimBlueprint:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaAnimBlueprint"
    setmetatable(child, self)
    return define_table(child, definition)
end

function LuaAnimBlueprint:new(config)
    local instance = {}
    setmetatable(instance, make_instance_metatable(self))

    instance.Class = self
    instance.Layers = {}
    instance.DefaultLayerName = self.DefaultLayerName

    for key, value in pairs(config or {}) do
        instance[key] = value
    end

    if type(instance.__init) == "function" then
        instance:__init(config)
    elseif type(instance.Initialize) == "function" then
        instance:Initialize(config)
    end

    return instance
end

function LuaAnimBlueprint:IsDebugEnabled()
    return self.Debug == true
end

function LuaAnimBlueprint:GetDebugName()
    return self.DebugName or self.ClassName or "LuaAnimBlueprint"
end

function LuaAnimBlueprint:LogDebug(area, message)
    if not self:IsDebugEnabled() then
        return
    end

    print(string.format(
        "[LuaAnim][Blueprint][%s][%s] %s",
        self:GetDebugName(),
        tostring(area or "Debug"),
        tostring(message or "")))
end

function LuaAnimBlueprint:GetLayerName(layer)
    if type(layer) == "table" and type(layer.GetLayerName) == "function" then
        return layer:GetLayerName()
    end

    if type(layer) == "table" and layer.LayerName ~= nil then
        return tostring(layer.LayerName)
    end

    return nil
end

function LuaAnimBlueprint:AddLayer(layer, b_default_layer)
    local layer_name = self:GetLayerName(layer)
    if layer_name == nil or layer_name == "" or layer_name == "None" then
        self:LogDebug("Layer", "ignored layer without valid name")
        return nil
    end

    self.Layers[layer_name] = layer
    if b_default_layer == true or self.DefaultLayerName == nil or self.DefaultLayerName == "Default" then
        self.DefaultLayerName = layer_name
    end

    self:LogDebug("Layer", string.format(
        "registered layer=%s default=%s class=%s",
        tostring(layer_name),
        tostring(self.DefaultLayerName),
        tostring(type(layer) == "table" and layer.ClassName or type(layer))))

    return layer
end

function LuaAnimBlueprint:CreateStateMachine(state_machine_class, config, b_default_layer)
    local state_machine = state_machine_class
    local state_machine_config = inherit_debug_config(self, config)
    if type(state_machine_class) == "table" and type(state_machine_class.new) == "function" then
        state_machine = state_machine_class:new(state_machine_config)
    elseif type(state_machine_class) == "table" then
        local class_metatable = getmetatable(state_machine_class)
        if class_metatable ~= nil and type(class_metatable.__call) == "function" then
            state_machine = state_machine_class(state_machine_config)
        end
    end

    return self:AddLayer(state_machine, b_default_layer)
end

function LuaAnimBlueprint:UseStateMachine(state_machine_or_name)
    if type(state_machine_or_name) == "string" then
        return self:FindLayer(state_machine_or_name)
    end

    return state_machine_or_name
end

function LuaAnimBlueprint:OutputPose(pose_source)
    return pose_source
end

function LuaAnimBlueprint:AnimGraph()
    return self:UseStateMachine(self.DefaultLayerName)
end

function LuaAnimBlueprint:GetContextObject()
    return rawget(self, "ContextObject")
end

function LuaAnimBlueprint:GetRuntimeContext(context, runtime_context)
    if type(runtime_context) == "table" then
        return runtime_context
    end

    if type(context) == "table" and resolve_context_object(context) == nil then
        return context
    end

    return nil
end

function LuaAnimBlueprint:GetUpdateContext(context, runtime_context)
    return self:GetRuntimeContext(context, runtime_context) or context
end

function LuaAnimBlueprint:ApplyContext(context, runtime_context)
    local update_context = self:GetUpdateContext(context, runtime_context)
    local runtime_table = self:GetRuntimeContext(context, runtime_context)
    local context_object = resolve_context_object(context) or resolve_context_object(runtime_context)

    self.Context = context_object or update_context
    self.ContextObject = context_object
    self.RuntimeContext = runtime_table
    self.UpdateContext = update_context

    if type(runtime_table) ~= "table" then
        return
    end

    for key, value in pairs(runtime_table) do
        if type(key) == "string" and not RuntimeFieldSkip[key] then
            self[key] = value
        end
    end
end

function LuaAnimBlueprint:CallContextFunction(function_name, ...)
    local context = self:GetContextObject()
    if context == nil or function_name == nil then
        return nil
    end

    local ok, result1, result2, result3, result4 = pcall(function(...)
        local method = context[function_name]
        if type(method) ~= "function" then
            return nil
        end

        return method(context, ...)
    end, ...)

    if ok then
        return result1, result2, result3, result4
    end

    return nil
end

function LuaAnimBlueprint:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

function LuaAnimBlueprint:Configure(context)
    if context == nil then
        self:LogDebug("Configure", "skipped because context is nil")
        return
    end

    self:ApplyContext(context, nil)
    self:LogDebug("Configure", string.format("defaultLayer=%s", tostring(self.DefaultLayerName)))
    self:ResetRuntimeLayers(context)
    self:RegisterRuntimeLayers(context)
end

function LuaAnimBlueprint:ResetRuntimeLayers(context)
    try_call(function()
        context:ClearLuaAnimLayers()
    end)

    try_call(function()
        context:SetDefaultLuaAnimLayerName(self.DefaultLayerName)
    end)
end

function LuaAnimBlueprint:RegisterRuntimeLayers(context)
    for layer_name, layer in pairs(self.Layers or {}) do
        self:LogDebug("Configure", string.format("register runtime layer=%s", tostring(layer_name)))
        try_call(function()
            context:RegisterLuaAnimLayer(layer_name)
        end)

        if type(layer) == "table" and type(layer.Configure) == "function" then
            layer:Configure(context)
        end
    end
end

function LuaAnimBlueprint:NormalizeLayerName(layer_name)
    if layer_name == nil then
        return self.DefaultLayerName
    end

    local text = tostring(layer_name)
    if text == "" or text == "None" then
        return self.DefaultLayerName
    end

    return text
end

function LuaAnimBlueprint:FindLayer(layer_name)
    local normalized_layer_name = self:NormalizeLayerName(layer_name)
    return (self.Layers or {})[normalized_layer_name] or (self.Layers or {})[self.DefaultLayerName]
end

function LuaAnimBlueprint:UpdateLayer(context, layer_name, delta_seconds, runtime_context)
    self:ApplyContext(context, runtime_context)

    local layer = self:FindLayer(layer_name)
    if type(layer) ~= "table" then
        self:LogDebug("Update", string.format("missing layer=%s", tostring(layer_name)))
        return nil
    end

    if layer.IsLuaAnimStateMachine == true and type(layer.Update) == "function" then
        return layer:Update(context, delta_seconds, runtime_context)
    end

    if type(layer.Update) == "function" then
        return layer.Update(runtime_context or context, delta_seconds)
    end

    return nil
end

function LuaAnimBlueprint:Update(context, delta_seconds, runtime_context)
    self:ApplyContext(context, runtime_context)

    local root_pose = self:AnimGraph()
    local layer_name = self:GetLayerName(root_pose) or self.DefaultLayerName
    return self:UpdateLayer(context, layer_name, delta_seconds, runtime_context)
end

function LuaAnimBlueprint:Export()
    local instance = self
    local configure = self.Configure
    local update_layer = self.UpdateLayer
    local update = self.Update

    function instance.Configure(context)
        return configure(instance, context)
    end

    function instance.UpdateLayer(context, layer_name, delta_seconds, runtime_context)
        return update_layer(instance, context, layer_name, delta_seconds, runtime_context)
    end

    function instance.Update(context, delta_seconds, runtime_context)
        return update(instance, context, delta_seconds, runtime_context)
    end

    return instance
end

return LuaAnimBlueprint
