-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Lua 动画蓝图编译器使用的轻量类基类。
-- 本模块只提供继承与实例化，不依赖 UObject、UnLua 运行时上下文或项目业务类型。
---@class CompilerClass
---@field __index CompilerClass
---@field ClassName string 编译器类的诊断名称。
---@field Super CompilerClass|nil 父类；根类没有父类。
local CompilerClass = {}

CompilerClass.__index = CompilerClass
CompilerClass.ClassName = "CompilerClass"

---合并类声明字段；编译器类只做浅复制，实例级数组必须由 Initialize 创建。
---@param target table<string, any> 接收类声明字段的目标表。
---@param definition table<string, any>|nil 子类提供的字段和方法声明。
---@return table<string, any> target 合并完成后的目标表。
local function merge_definition(target, definition)
    for key, value in pairs(definition or {}) do
        target[key] = value
    end

    return target
end

---创建当前类的子类，并通过元表保留父类方法查找链。
---@param class_name string 子类用于诊断和调试显示的名称。
---@param definition table<string, any>|nil 子类字段及 override 方法声明。
---@return CompilerClass child 新建的编译器子类；具体静态类型由接收变量的 `---@class` 声明确定。
function CompilerClass:Extend(class_name, definition)
    local child = {
        ClassName = class_name or "CompilerClass",
        Super = self,
    }
    local inherited_new_index = rawget(self, "__newindex")
    if inherited_new_index ~= nil then
        child.__newindex = inherited_new_index
    end
    child.__index = child
    setmetatable(child, { __index = self })
    return merge_definition(child, definition)
end

---创建当前类的独立实例，并把构造参数统一交给可选 Initialize 处理。
---@param config table<string, any>|nil 传给 Initialize 的构造参数；基类不隐式复制字段。
---@return CompilerClass instance 新建并完成初始化的类实例；子类调用处由具体配置和返回注释收窄类型。
function CompilerClass:New(config)
    local instance = setmetatable({}, self)
    if type(instance.Initialize) == "function" then
        instance:Initialize(config or {})
    end

    return instance
end

return CompilerClass
