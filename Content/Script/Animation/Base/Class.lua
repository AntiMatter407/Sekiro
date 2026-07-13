---把源表字段浅复制到目标表，供轻量类实例和配置继承复用。
---@param target table 接收字段、元方法或状态记录的目标表。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@return table target 写入源字段后的目标表。
local function copy_fields(target, source)
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

---根据类表和可选配置创建实例，并按约定调用初始化入口。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table instance 完成配置覆盖和初始化的 Lua 实例。
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

---创建支持父类查找、配置覆盖和调用式实例化的轻量 Lua 类。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param super table|nil 可选父类；缺失时创建无父类的基础类。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table class_type 可继承并可调用创建实例的 Lua 类表。
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
        ---实现 Lua __call 元方法，维持类实例的创建和字段访问语义。
        ---@param target_class table 参与当前类、实例或元表操作的 target_class 表。
        ---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
        ---@return table value 创建、派生或导出的类/实例表。
        __call = function(target_class, config)
            return new_instance(target_class, config)
        end,
    })

    return child
end

return class
