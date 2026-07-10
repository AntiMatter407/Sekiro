local function copy_fields(target, source)
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

local function new_instance(class, config)
    if type(class.new) == "function" then
        return class:new(config)
    end

    local instance = {}
    setmetatable(instance, class)
    instance.Class = class

    copy_fields(instance, config)

    if type(instance.__init) == "function" then
        instance:__init(config)
    elseif type(instance.Initialize) == "function" then
        instance:Initialize(config)
    end

    return instance
end

local function class(class_name, super, definition)
    local class_definition = definition or {}
    local base_class = super

    if type(base_class) == "table" and type(base_class.Extend) == "function" then
        local child = base_class:Extend(class_name, class_definition)
        child.super = base_class
        child.Super = base_class
        return child
    end

    local child = {}
    child.__index = child
    child.ClassName = class_name or "LuaClass"
    child.super = base_class
    child.Super = base_class

    copy_fields(child, class_definition)

    setmetatable(child, {
        __index = base_class,
        __call = function(target_class, config)
            return new_instance(target_class, config)
        end,
    })

    return child
end

return class
