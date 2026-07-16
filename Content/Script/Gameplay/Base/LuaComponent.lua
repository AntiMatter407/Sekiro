local LuaComponent = {}

LuaComponent.__index = LuaComponent
---实现 Lua __call 元方法，维持类实例的创建和字段访问语义。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
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

---创建源表的浅副本，避免实例修改类级默认配置。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@return table copy 与源表字段相同但可独立修改的浅副本。
local function copy_table(source)
    local target = {}
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

---把声明表字段合并到目标类或实例，并返回同一目标表。
---@param target table 接收字段、元方法或状态记录的目标表。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@return table target 合并声明字段后的目标表。
local function define_table(target, source)
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

---从普通 Lua 表安全读取字段，非表输入直接返回 nil。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@param key any 表字段、上下文字段或资源映射使用的键。
---@return any value 字段值；输入不是表或字段缺失时为 nil。
local function read_table_value(source, key)
    if source == nil then
        return nil
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, value = pcall(function()
        return source[key]
    end)
    if not ok then
        return nil
    end

    return value
end

---从 UnLua 调用参数中解析真实 UObject 或运行时上下文对象。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@return userdata|table|nil context_object 解析出的 UObject/上下文；无法识别时为 nil。
local function resolve_context_object(context)
    local object = read_table_value(context, "Object")
    if object ~= nil then
        return object
    end

    return context
end

---把 UnLua 上下文返回的空占位值规范化为 Lua nil。
---@param value any 待读取、转换、比较或写入的输入值。
---@return any value 移除空占位后的单个返回值。
local function normalize_context_return(value)
    local object = read_table_value(value, "Object")
    if object ~= nil then
        return object
    end

    return value
end

---逐项规范化最多四个 C++ 反射返回值并保持原顺序。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 规范化后的最多四个返回值，保持原顺序。
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

---从当前 UObject 上下文读取字段，并把 C++ 方法包装为绑定调用。
---@param instance table 参与当前类、实例或元表操作的 instance 表。
---@param key any 表字段、上下文字段或资源映射使用的键。
---@return any value 上下文字段值或绑定后的方法闭包；读取失败时为 nil。
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

    ---创建绑定真实目标实例的转发闭包，使导出调用保持面向对象方法语义。
    ---@param _self table|nil 闭包调用方隐式传入的实例；包装器已经捕获真实目标，因此不直接使用。
    ---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    return function(_self, ...)
        return normalize_context_returns(value(context, ...))
    end
end

---创建实例元表，使字段按类定义和 UObject 上下文顺序解析。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@return table metatable 负责类字段和 UObject 字段查找的实例元表。
local function make_instance_metatable(class)
    return {
        ---按类字段优先、UObject 上下文次之的顺序解析实例成员。
        ---@param instance table 参与当前类、实例或元表操作的 instance 表。
        ---@param key any 表字段、上下文字段或资源映射使用的键。
        ---@return any value 类字段、UObject 字段或绑定方法；均不存在时为 nil。
        __index = function(instance, key)
            local value = class[key]
            if value ~= nil then
                return value
            end

            return read_context_index(instance, key)
        end,
    }
end

---从当前组件类浅复制出子类，并合并子类声明字段。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function LuaComponent:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaComponent"
    setmetatable(child, self)
    return define_table(child, definition)
end

---创建当前组件类的语义子类；该入口与 Extend 保持一致以便项目类系统调用。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function LuaComponent:Class(class_name, definition)
    return self:Extend(class_name, definition)
end

---创建组件 Lua 实例，覆盖配置字段并调用可选 Initialize。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
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

---读取或计算调试名称，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function LuaComponent:GetDebugName()
    return self.DebugName or self.ClassName or "LuaComponent"
end

---判断调试启用状态是否满足当前业务条件。
---@return boolean matched 当前事实和门控条件是否满足。
function LuaComponent:IsDebugEnabled()
    return self.Debug == true
end

---在组件调试开关启用时输出带类名和功能区域的日志。
---@param area string 日志所属的功能区域名称。
---@param message string 写入调试输出的说明文本。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
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

---读取或计算上下文对象，字段缺失时遵循函数内的明确回退规则。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function LuaComponent:GetContextObject()
    return rawget(self, "ContextObject")
end

---返回 UnLua 原始调用上下文；它可能包含真实 UObject 之外的包装信息。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function LuaComponent:GetLuaContextObject()
    return rawget(self, "LuaContextObject")
end

---通过当前 UObject 上下文安全调用反射方法，异常时返回 nil 而不终止 Lua Tick。
---@param function_name string 需要通过反射或上下文代理调用的函数名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function LuaComponent:CallContextFunction(function_name, ...)
    local context = self:GetContextObject()
    if context == nil or function_name == nil then
        return nil
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
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

---以 C++ 子类调用风格转发到当前 UObject 方法，并保持多返回值顺序。
---@param function_name string 需要通过反射或上下文代理调用的函数名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function LuaComponent:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

---判断类字段是否应暴露给 UnLua；内部方法和下划线私有方法不会导出。
---@param name string|nil name 对应的语义字符串或资源标识。
---@param value any 待读取、转换、比较或写入的输入值。
---@return boolean should_export 该字段是否为可自动导出的 public 函数。
local function should_auto_export(name, value)
    if type(name) ~= "string" or type(value) ~= "function" then
        return false
    end

    if InternalMethodNames[name] then
        return false
    end

    return string.sub(name, 1, 1) ~= "_"
end

---把尚未登记的函数名加入导出列表，并用 seen 表保持继承链去重。
---@param names string[] 按继承顺序收集的导出函数名数组。
---@param seen table<string, boolean> 已登记函数名集合。
---@param name string|nil 候选函数名；无效名称会被忽略。
---@return nil 该函数只更新 names 和 seen。
local function add_export_name(names, seen, name)
    if type(name) == "string" and not seen[name] then
        table.insert(names, name)
        seen[name] = true
    end
end

---按父类到子类顺序收集所有可自动导出的 public 方法，并移除重名项。
---@return string[] function_names 稳定排序的 UnLua 导出函数名数组。
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

---构造 UnLua 模块导出表，为每个 UObject 上下文维护独立 Lua 组件实例。
---@return table value 创建、派生或导出的类/实例表。
function LuaComponent:Export()
    local class = self
    local instances = setmetatable({}, { __mode = "k" })
    local exported = {}

    ---获取或创建指定 UnLua 上下文对应的 Lua 组件实例，并刷新上下文引用。
    ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
    ---@return table instance 与当前 UObject 上下文绑定的 Lua 组件实例。
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
        ---把 UnLua 导出调用转发到当前上下文对应的 Lua 实例方法。
        ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
        ---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
        ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
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
