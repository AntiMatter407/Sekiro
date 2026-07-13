local LuaAnimBlueprint = {}

LuaAnimBlueprint.__index = LuaAnimBlueprint
---实现 Lua __call 元方法，维持类实例的创建和字段访问语义。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
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

---在 pcall 中执行动态访问，把异常转换为 nil 供调用方走回退路径。
---@param call function 需要在受保护环境中执行的零参数调用。
---@return any result 调用成功时的首个返回值；异常时为 nil。
local function try_call(call)
    local ok, result = pcall(call)
    if ok then
        return result
    end

    return nil
end

---从 UnLua 调用参数中解析真实 UObject 或运行时上下文对象。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@return userdata|table|nil context_object 解析出的 UObject/上下文；无法识别时为 nil。
local function resolve_context_object(context)
    if context == nil then
        return nil
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
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

---生成隔离不同 AnimInstance 运行时层缓存的稳定键。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return userdata|table|string runtime_key 用于索引运行时缓存的稳定键。
local function resolve_runtime_key(context, runtime_context)
    local context_object = resolve_context_object(context) or resolve_context_object(runtime_context)
    if context_object ~= nil then
        return context_object
    end

    if type(runtime_context) == "table" then
        return runtime_context
    end

    if type(context) == "table" then
        return context
    end

    return nil
end

---复制导出动画蓝图实例需要继承的运行时配置。
---@param owner userdata|table|nil 当前 Lua 实例代理的 UE 所有者对象。
---@return table config 从导出宿主复制出的独立配置表。
local function copy_export_runtime_config(owner)
    local config = {}
    for key, value in pairs(owner or {}) do
        if type(key) == "string"
            and not RuntimeFieldSkip[key]
            and type(value) ~= "function"
            and type(value) ~= "table" then
            config[key] = value
        end
    end

    return config
end

---从当前 UObject 上下文读取字段，并把 C++ 方法包装为绑定调用。
---@param instance table 参与当前类、实例或元表操作的 instance 表。
---@param key any 表字段、上下文字段或资源映射使用的键。
---@return any value 上下文字段值或绑定后的方法闭包；读取失败时为 nil。
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

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, value = pcall(function()
        return context[key]
    end)
    if not ok or value == nil then
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
        return value(context, ...)
    end
end

---创建实例元表，使字段按类定义和 UObject 上下文顺序解析。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@return table metatable 负责类字段和 UObject 字段查找的实例元表。
local function make_instance_metatable(class)
    return {
        ---按动画蓝图类字段优先、UObject 上下文次之的顺序解析实例成员。
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

---把宿主调试开关继承到新建动画层，同时允许局部配置覆盖。
---@param owner userdata|table|nil 当前 Lua 实例代理的 UE 所有者对象。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table config 完成调试字段继承后的配置表。
local function inherit_debug_config(owner, config)
    local layer_config = copy_table(config)
    for _, key in ipairs(DebugConfigKeys) do
        if layer_config[key] == nil and owner[key] ~= nil then
            layer_config[key] = owner[key]
        end
    end

    return layer_config
end

---从当前动画蓝图类派生子类，并合并角色专属字段和 override。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function LuaAnimBlueprint:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaAnimBlueprint"
    setmetatable(child, self)
    return define_table(child, definition)
end

---创建独立动画蓝图 Lua 运行时，初始化层表后调用角色蓝图 Initialize。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
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

---判断调试启用状态是否满足当前业务条件。
---@return boolean matched 当前事实和门控条件是否满足。
function LuaAnimBlueprint:IsDebugEnabled()
    return self.Debug == true
end

---读取或计算调试名称，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function LuaAnimBlueprint:GetDebugName()
    return self.DebugName or self.ClassName or "LuaAnimBlueprint"
end

---输出调试调试信息，并遵守模块调试开关和频率限制。
---@param area string 日志所属的功能区域名称。
---@param message string 写入调试输出的说明文本。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
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

---读取或计算动画层名称，字段缺失时遵循函数内的明确回退规则。
---@param layer table 需要注册、更新或查询的 Lua 动画层实例。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function LuaAnimBlueprint:GetLayerName(layer)
    if type(layer) == "table" and type(layer.GetLayerName) == "function" then
        return layer:GetLayerName()
    end

    if type(layer) == "table" and layer.LayerName ~= nil then
        return tostring(layer.LayerName)
    end

    return nil
end

---按稳定 LayerName 注册动画层，并可把它设为 AnimGraph 默认输出。
---@param layer table 需要注册、更新或查询的 Lua 动画层实例。
---@param b_default_layer boolean|nil 是否把该层设置为 AnimGraph 的默认输出层。
---@return table|nil layer 注册成功时返回原动画层实例；名称无效时返回 nil。
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

---创建状态机实例、继承宿主调试配置并注册为动画层。
---@param state_machine_class table 用于创建动画层实例的 Lua 状态机类。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@param b_default_layer boolean|nil 是否把该层设置为 AnimGraph 的默认输出层。
---@return table instance_or_class 创建或派生得到的 Lua 类/实例。
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

---把状态机名称解析为已注册实例；直接传入实例时保持原值。
---@param state_machine_or_name table|string 状态机实例或已注册动画层名称。
---@return table|nil state_machine 解析出的状态机实例；名称不存在时为 nil。
function LuaAnimBlueprint:UseStateMachine(state_machine_or_name)
    if type(state_machine_or_name) == "string" then
        return self:FindLayer(state_machine_or_name)
    end

    return state_machine_or_name
end

---声明 AnimGraph 根输出；第一阶段只接受 Lua 状态机或已注册动画层名称。
---每个状态必须通过 StateResult 发布，禁止绕过状态机直接提交 SequencePlayer 或原始 PoseLink。
---@param pose_source LuaAnimStateMachine|string|nil 状态机实例或已注册动画层名称。
---@return LuaAnimStateMachine|nil root_output 解析后的状态机根输出；输入不是状态机时为 nil。
function LuaAnimBlueprint:OutputPose(pose_source)
    if type(pose_source) == "string" then
        pose_source = self:UseStateMachine(pose_source)
    end

    if type(pose_source) == "table" and pose_source.IsLuaAnimStateMachine == true then
        return pose_source
    end

    self:LogDebug("AnimGraph", "root output must be a LuaAnimStateMachine")
    return nil
end

---返回默认动画层对应的状态机实例；角色子类可 override 以组合其他层。
---@return table|nil pose_source 默认 AnimGraph Pose 源。
function LuaAnimBlueprint:AnimGraph()
    return self:UseStateMachine(self.DefaultLayerName)
end

---读取或计算上下文对象，字段缺失时遵循函数内的明确回退规则。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function LuaAnimBlueprint:GetContextObject()
    return rawget(self, "ContextObject")
end

---读取或计算运行时上下文，字段缺失时遵循函数内的明确回退规则。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return number value 读取或计算得到的数值。
function LuaAnimBlueprint:GetRuntimeContext(context, runtime_context)
    if type(runtime_context) == "table" then
        return runtime_context
    end

    if type(context) == "table" and resolve_context_object(context) == nil then
        return context
    end

    return nil
end

---读取或计算更新上下文，字段缺失时遵循函数内的明确回退规则。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function LuaAnimBlueprint:GetUpdateContext(context, runtime_context)
    return self:GetRuntimeContext(context, runtime_context) or context
end

---应用上下文，只修改当前实例或对应 C++ 策略。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
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

---执行调用上下文函数逻辑，并向调用方返回模块约定的结果。
---@param function_name string 需要通过反射或上下文代理调用的函数名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function LuaAnimBlueprint:CallContextFunction(function_name, ...)
    local context = self:GetContextObject()
    if context == nil or function_name == nil then
        return nil
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
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

---执行调用C++逻辑，并向调用方返回模块约定的结果。
---@param function_name string 需要通过反射或上下文代理调用的函数名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function LuaAnimBlueprint:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

---在 Configure 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
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

---清空 C++ 动画层注册并同步默认层名称，为重新 Configure 建立干净状态。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function LuaAnimBlueprint:ResetRuntimeLayers(context)
    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    try_call(function()
        context:ClearLuaAnimLayers()
    end)

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    try_call(function()
        context:SetDefaultLuaAnimLayerName(self.DefaultLayerName)
    end)
end

---把所有 Lua 动画层名称注册到 C++ AnimInstance，并调用各层 Configure。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function LuaAnimBlueprint:RegisterRuntimeLayers(context)
    for layer_name, layer in pairs(self.Layers or {}) do
        self:LogDebug("Configure", string.format("register runtime layer=%s", tostring(layer_name)))
        ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
        ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
        try_call(function()
            context:RegisterLuaAnimLayer(layer_name)
        end)

        if type(layer) == "table" and type(layer.Configure) == "function" then
            layer:Configure(context)
        end
    end
end

---把 nil、空字符串和 None 统一映射到默认动画层名称。
---@param layer_name string|nil 动画层的唯一语义名称。
---@return string layer_name 有效动画层名称；无显式名称时为 DefaultLayerName。
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

---查找动画层，没有匹配项时返回 nil 或约定的失败值。
---@param layer_name string|nil 动画层的唯一语义名称。
---@return table|nil layer 匹配名称的动画层；不存在时回退默认层。
function LuaAnimBlueprint:FindLayer(layer_name)
    local normalized_layer_name = self:NormalizeLayerName(layer_name)
    return (self.Layers or {})[normalized_layer_name] or (self.Layers or {})[self.DefaultLayerName]
end

---更新动画层，把本帧结果同步到所属运行时。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param layer_name string|nil 动画层的唯一语义名称。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return table|nil decision 动画层本帧状态/姿势决策；层缺失时为 nil。
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

---更新 AnimGraph 根输出；状态机自行发布 Pose，直接 PoseLink 由本函数发布。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return nil Pose 通过 Pose Graph 接口直接发布，不再返回旧动画决策表。
function LuaAnimBlueprint:Update(context, delta_seconds, runtime_context)
    self:ApplyContext(context, runtime_context)

    local root_output = self:AnimGraph()
    if type(root_output) == "table" and root_output.IsLuaAnimStateMachine == true then
        local layer_name = self:GetLayerName(root_output) or self.DefaultLayerName
        self:UpdateLayer(context, layer_name, delta_seconds, runtime_context)
        return nil
    end

    self:LogDebug("Update", "AnimGraph did not return a LuaAnimStateMachine")
    return nil
end

---执行导出逻辑，并向调用方返回模块约定的结果。
---@return table value 创建、派生或导出的类/实例表。
function LuaAnimBlueprint:Export()
    local blueprint_class = rawget(self, "Class") or self
    local runtime_config = rawget(self, "Class") ~= nil and copy_export_runtime_config(self) or nil
    local default_runtime_key = {}
    local runtimes = setmetatable({}, { __mode = "k" })
    local exported = {}

    -- UnLua 的模块只会 require 一次，因此这里按 AnimInstance 切分真正的动画运行时。
    -- 每个运行时都会重新 Initialize，并拥有独立的状态机、状态时间和调试节流字段。
    ---按 AnimInstance 上下文获取或创建独立 Lua 动画蓝图运行时。
    ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
    ---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
    ---@return table runtime 与当前 AnimInstance 隔离的动画蓝图 Lua 实例。
    local function get_runtime(context, runtime_context)
        local runtime_key = resolve_runtime_key(context, runtime_context) or default_runtime_key
        local runtime = runtimes[runtime_key]
        if runtime == nil then
            runtime = blueprint_class:new(runtime_config)
            runtimes[runtime_key] = runtime
        end

        return runtime
    end

    exported.GetRuntime = get_runtime

    ---在 Configure 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
    ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
    ---@return nil Configure 只重建层注册，不返回业务值。
    function exported.Configure(context)
        return get_runtime(context, nil):Configure(context)
    end

    ---更新动画层，把本帧结果同步到所属运行时。
    ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
    ---@param layer_name string|nil 动画层的唯一语义名称。
    ---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
    ---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
    ---@return table|nil decision 指定动画层产生的本帧决策。
    function exported.UpdateLayer(context, layer_name, delta_seconds, runtime_context)
        return get_runtime(context, runtime_context):UpdateLayer(context, layer_name, delta_seconds, runtime_context)
    end

    ---更新当前逻辑，把本帧结果同步到所属运行时。
    ---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
    ---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
    ---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
    ---@return nil Pose 通过 Pose Graph 接口直接发布，不再返回旧动画决策表。
    function exported.Update(context, delta_seconds, runtime_context)
        return get_runtime(context, runtime_context):Update(context, delta_seconds, runtime_context)
    end

    return exported
end

return LuaAnimBlueprint
