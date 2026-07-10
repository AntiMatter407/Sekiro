local LuaComponent = {}

LuaComponent.__index = LuaComponent
LuaComponent.__call = function(class, config)
    return class:new(config)
end

LuaComponent.ClassName = "LuaComponent"
LuaComponent.Debug = false
LuaComponent.AutoExport = true

local InternalMethodNames = {
    Class = true,
    Extend = true,
    new = true,
    Initialize = true,
    GetDebugName = true,
    IsDebugEnabled = true,
    LogDebug = true,
    GetContextObject = true,
    GetLuaContextObject = true,
    CallContextFunction = true,
    CallCpp = true,
    GetExportFunctionNames = true,
    Export = true,
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

local function read_table_value(source, key)
    if source == nil then
        return nil
    end

    local ok, value = pcall(function()
        return source[key]
    end)
    if not ok then
        return nil
    end

    return value
end

local function resolve_context_object(context)
    local object = read_table_value(context, "Object")
    if object ~= nil then
        return object
    end

    return context
end

local function normalize_context_return(value)
    local object = read_table_value(value, "Object")
    if object ~= nil then
        return object
    end

    return value
end

local function normalize_context_returns(...)
    local count = select("#", ...)
    if count == 0 then
        return
    end

    local values = { ... }
    for index = 1, count do
        values[index] = normalize_context_return(values[index])
    end

    return table.unpack(values, 1, count)
end

local function read_context_index(instance, key)
    local context = rawget(instance, "ContextObject")
    if context == nil then
        return nil
    end

    local value = read_table_value(context, key)
    if value == nil then
        return nil
    end

    if type(value) ~= "function" then
        return value
    end

    return function(_self, ...)
        return normalize_context_returns(value(context, ...))
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

function LuaComponent:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaComponent"
    setmetatable(child, self)
    return define_table(child, definition)
end

function LuaComponent:Class(class_name, definition)
    return self:Extend(class_name, definition)
end

function LuaComponent:new(config)
    local instance = {}
    setmetatable(instance, make_instance_metatable(self))
    instance.Class = self

    for key, value in pairs(config or {}) do
        instance[key] = value
    end

    if type(instance.Initialize) == "function" then
        instance:Initialize(config)
    end

    return instance
end

function LuaComponent:GetDebugName()
    return self.DebugName or self.ClassName or "LuaComponent"
end

function LuaComponent:IsDebugEnabled()
    return self.Debug == true
end

function LuaComponent:LogDebug(area, message)
    if not self:IsDebugEnabled() then
        return
    end

    print(string.format(
        "[LuaComponent][%s][%s] %s",
        tostring(self:GetDebugName()),
        tostring(area or "Debug"),
        tostring(message or "")))
end

function LuaComponent:GetContextObject()
    return rawget(self, "ContextObject")
end

function LuaComponent:GetLuaContextObject()
    return rawget(self, "LuaContextObject")
end

function LuaComponent:CallContextFunction(function_name, ...)
    local context = self:GetContextObject()
    if context == nil or function_name == nil then
        return nil
    end

    local ok, result1, result2, result3, result4 = pcall(function(...)
        local method = read_table_value(context, function_name)
        if type(method) ~= "function" then
            return nil
        end

        return normalize_context_returns(method(context, ...))
    end, ...)

    if ok then
        return result1, result2, result3, result4
    end

    return nil
end

function LuaComponent:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

local function should_auto_export(name, value)
    if type(name) ~= "string" or type(value) ~= "function" then
        return false
    end

    if InternalMethodNames[name] then
        return false
    end

    return string.sub(name, 1, 1) ~= "_"
end

local function add_export_name(names, seen, name)
    if type(name) == "string" and not seen[name] then
        table.insert(names, name)
        seen[name] = true
    end
end

function LuaComponent:GetExportFunctionNames()
    local names = {}
    local seen = {}
    local chain = {}
    local current = self

    while current ~= nil do
        table.insert(chain, 1, current)
        current = rawget(current, "Super") or rawget(current, "super")
    end

    for _, class in ipairs(chain) do
        if rawget(class, "AutoExport") ~= false then
            for name, value in pairs(class) do
                if should_auto_export(name, value) then
                    add_export_name(names, seen, name)
                end
            end
        end

    end

    return names
end

function LuaComponent:Export()
    local class = self
    local instances = setmetatable({}, { __mode = "k" })
    local exported = {}

    local function get_instance(context)
        if context == nil then
            return class
        end

        local context_object = resolve_context_object(context)
        local instance_key = context_object or context
        local instance = instances[instance_key]
        if instance == nil then
            instance = class:new({
                ContextObject = context_object,
                LuaContextObject = context,
                Context = context_object or context,
            })
            instances[instance_key] = instance

            if type(instance.Construct) == "function" then
                instance:Construct(context)
            end
        else
            instance.ContextObject = context_object
            instance.LuaContextObject = context
            instance.Context = context_object or context
        end

        return instance
    end

    exported.GetInstance = get_instance

    for _, function_name in ipairs(class:GetExportFunctionNames()) do
        exported[function_name] = function(context, ...)
            local instance = get_instance(context)
            local method = instance[function_name]
            if type(method) ~= "function" then
                return false
            end

            return method(instance, context, ...)
        end
    end

    return exported
end

return LuaComponent
