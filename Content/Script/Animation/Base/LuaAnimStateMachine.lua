---@class SekiroNativePoseLink
---@field NodeId integer C++ 持久 Pose 节点编号。
---@field Generation integer C++ Pose Graph 代次，用于拒绝重新初始化前的旧句柄。

local LuaAnimNodeBase = require("Animation.Base.AnimGraph.LuaAnimNodeBase")
local LuaPoseLink = require("Animation.Base.AnimGraph.LuaPoseLink")
local LuaSequencePlayer = require("Animation.Base.AnimGraph.LuaSequencePlayer")
local LuaAnimationState = require("Animation.Base.AnimGraph.LuaAnimationState")
local LuaAnimationTransition = require("Animation.Base.AnimGraph.LuaAnimationTransition")

---@class LuaAnimStateMachine : LuaAnimNodeBase
---@field IsLuaAnimStateMachine boolean 标识实例是 Lua 状态机动画节点。
---@field AnimNodes table<string, LuaAnimNodeBase> 状态机持有的实际动画节点，键为动画层和稳定节点名。
---@field StateRecords LuaAnimationState[] 按 StateList 顺序保存的状态数据；State 本身不是 AnimNode。
---@field StateRecordByName table<string, LuaAnimationState> 状态名到状态数据的索引。
---@field StatePoseLinks LuaPoseLink[] 按 StateList 顺序连接到各状态 StateResult 的 PoseLink，对应 FAnimNode_StateMachine::StatePoseLinks。
---@field StatePoseLinkByName table<string, LuaPoseLink> 状态名到状态 Pose 连接的索引。
---@field TransitionRecords LuaAnimationTransition[] 按声明顺序保存的烘焙式转换描述。
---@field OutputPoseLink LuaPoseLink|nil 上游节点连接当前状态机时使用的输出连接。
local StateMachine = {}
setmetatable(StateMachine, LuaAnimNodeBase)

StateMachine.__index = StateMachine
StateMachine.super = LuaAnimNodeBase
StateMachine.Super = LuaAnimNodeBase
---实现 Lua __call 元方法，维持类实例的创建和字段访问语义。
---@param class table 参与实例创建或继承查找的 Lua 类表。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
StateMachine.__call = function(class, config)
    return class:new(config)
end
StateMachine.ClassName = "LuaAnimStateMachine"
StateMachine.IsLuaAnimStateMachine = true
StateMachine.IsLuaAnimNode = true
StateMachine.NodeType = "StateMachine"
StateMachine.LayerName = "Default"
StateMachine.InitialState = "Idle"
StateMachine.DefaultBlendTime = 0.15
StateMachine.DefaultLoop = true
StateMachine.Debug = false
StateMachine.DebugUpdateInterval = 0.5
StateMachine.DebugTransitions = true
StateMachine.DebugTransitionChecks = false
StateMachine.DebugPose = true
StateMachine.DebugPoseEveryFrame = false
StateMachine.DebugContext = true
StateMachine.States = {
    Idle = "Idle",
}
StateMachine.AnimationSettings = {}
StateMachine.Assets = {}

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

---复制父状态机的转换声明顺序，避免子类修改父类元数据。
---@param source table|nil 提供待复制、合并或遍历数据的源表。
---@return table edges 可独立修改的转换边数组。
local function copy_transition_order(source)
    local target = {}
    for index, edge in ipairs(source or {}) do
        target[index] = {
            From = edge.From,
            To = edge.To,
            Name = edge.Name,
        }
    end

    return target
end

local RuntimeFieldSkip = {
    Snapshot = true,
    Class = true,
    Super = true,
    super = true,
    State = true,
    States = true,
    StateList = true,
    Assets = true,
    AnimationSettings = true,
    Tuning = true,
    LayerName = true,
    EntryState = true,
    InitialState = true,
    LastFacts = true,
    LastDecision = true,
    Context = true,
    ContextObject = true,
    RuntimeContext = true,
    UpdateContext = true,
}

---把任意运行时值格式化为稳定、紧凑的动画调试文本。
---@param value any 待读取、转换、比较或写入的输入值。
---@return string text 可直接写入日志的稳定文本。
local function value_to_debug_string(value)
    if value == nil then
        return "nil"
    end

    if type(value) == "number" then
        return string.format("%.3f", value)
    end

    return tostring(value)
end

---从完整 UE 资源路径提取易读资产名，失败时保留原始文本。
---@param path string|nil path 对应的语义字符串或资源标识。
---@return string asset_name 简化资产名或原始路径文本。
local function short_asset_path(path)
    if path == nil then
        return "nil"
    end

    local text = tostring(path)
    local name = string.match(text, "([^/%.]+)%.([^/%.]+)$")
    if name ~= nil then
        return name
    end

    return text
end

---按 PathName、FullName、Name 的优先级生成 UObject 调试标识。
---@param instance table 参与当前类、实例或元表操作的 instance 表。
---@return string label 当前上下文的可读唯一标识。
local function get_debug_context_label(instance)
    local context = rawget(instance, "ContextObject")
    if context == nil then
        return "None"
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local label = try_call(function()
        return context:GetPathName()
    end)
    if label == nil or label == "" then
        ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
        ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
        label = try_call(function()
            return context:GetFullName()
        end)
    end
    if label == nil or label == "" then
        ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
        ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
        label = try_call(function()
            return context:GetName()
        end)
    end

    local identity = tostring(context)
    if label == nil or label == "" then
        return identity
    end

    if identity ~= nil and identity ~= "" and identity ~= label then
        return tostring(label) .. "@" .. identity
    end

    return tostring(label)
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
        ---按状态机类字段优先、UObject 上下文次之的顺序解析实例成员。
        ---@param instance table 参与当前类、实例或元表操作的 instance 表。
        ---@param key any 表字段、上下文字段或资源映射使用的键。
        ---@return any value 类字段、上下文字段或绑定方法；均不存在时为 nil。
        __index = function(instance, key)
            local value = class[key]
            if value ~= nil then
                return value
            end

            return read_context_index(instance, key)
        end,
    }
end

---记录 CanEnter_From_To 的声明顺序，保证自动转换不依赖 table 遍历顺序。
---@param target table 接收字段、元方法或状态记录的目标表。
---@param key any 表字段、上下文字段或资源映射使用的键。
---@param value any 待读取、转换、比较或写入的输入值。
---@return nil 该函数只更新转换顺序元数据。
local function remember_transition(target, key, value)
    if type(value) ~= "function" or type(key) ~= "string" then
        return
    end

    local from_state, to_state = string.match(key, "^CanEnter_([^_]+)_(.+)$")
    if from_state == nil or to_state == nil then
        return
    end

    local transition_order = rawget(target, "__TransitionOrder")
    if transition_order == nil then
        transition_order = {}
        rawset(target, "__TransitionOrder", transition_order)
    end

    for _, edge in ipairs(transition_order) do
        if edge.Name == key then
            return
        end
    end

    table.insert(transition_order, {
        From = from_state,
        To = to_state,
        Name = key,
    })
end

---拦截状态机类字段写入，在保存 CanEnter 函数时同步记录稳定转换顺序。
---@param target table 接收字段、元方法或状态记录的目标表。
---@param key any 表字段、上下文字段或资源映射使用的键。
---@param value any 待读取、转换、比较或写入的输入值。
---@return nil 该元方法只记录转换元数据并写入字段。
StateMachine.__newindex = function(target, key, value)
    remember_transition(target, key, value)
    rawset(target, key, value)
end

---从当前状态机类派生子类，复制转换顺序并合并角色或动画层定义。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaAnimStateMachine"
    child.__TransitionOrder = copy_transition_order(rawget(self, "__TransitionOrder"))
    setmetatable(child, self)
    return define_table(child, definition)
end

---创建当前状态机的语义子类；该入口与 Extend 保持一致。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:Class(class_name, definition)
    return self:Extend(class_name, definition)
end

---派生状态机子类；保留该别名便于业务代码表达继承关系。
---@param class_name string 用于调试和类型标识的 Lua 类名称。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:Derive(class_name, definition)
    return self:Extend(class_name, definition)
end

---把声明字段直接合并到当前状态机类，并返回当前类表。
---@param definition table|nil 类或状态机声明表，包含字段和可覆盖方法。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:Define(definition)
    return define_table(self, definition)
end

---创建独立状态机运行时，复制配置并调用可选初始化入口。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:new(config)
    local instance = {}
    setmetatable(instance, make_instance_metatable(self))
    instance.Class = self
    instance.LastFacts = nil
    instance.LastDecision = nil
    instance.AnimNodes = {}
    instance.StateRecords = {}
    instance.StateRecordByName = {}
    instance.StatePoseLinks = {}
    instance.StatePoseLinkByName = {}
    instance.TransitionRecords = {}
    instance.OutputPoseLink = nil

    for key, value in pairs(config or {}) do
        instance[key] = value
    end

    instance:BuildStatePoseLinks()
    instance:BuildTransitionRecords()

    if type(instance.__init) == "function" then
        instance:__init(config)
    elseif type(instance.Initialize) == "function" then
        instance:Initialize(config)
    end

    return instance
end

---创建并初始化当前逻辑。
---@param config table|nil 创建实例或状态机时覆盖默认字段的配置表。
---@return table value 创建、派生或导出的类/实例表。
function StateMachine:Create(config)
    return self:new(config)
end

---在 Initialize 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@param _config table|nil 调用方传入的初始化配置；基类默认实现不消费该表。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function StateMachine:Initialize(_config)
end

---在 Configure 生命周期阶段初始化本模块需要的缓存、绑定或动画层配置。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function StateMachine:Configure(_context)
end

---判断调试启用状态是否满足当前业务条件。
---@return boolean matched 当前事实和门控条件是否满足。
function StateMachine:IsDebugEnabled()
    return self.Debug == true
end

---判断调试flag启用状态是否满足当前业务条件。
---@param flag_name string 调试标志或位标志的语义名称。
---@return boolean matched 当前事实和门控条件是否满足。
function StateMachine:IsDebugFlagEnabled(flag_name)
    if not self:IsDebugEnabled() then
        return false
    end

    local value = self[flag_name]
    if value == nil and self.Class ~= nil then
        value = self.Class[flag_name]
    end
    if value == nil then
        return true
    end

    return value == true
end

---读取或计算调试名称，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetDebugName()
    return self.DebugName or self.ClassName or self:GetLayerName()
end

---输出调试调试信息，并遵守模块调试开关和频率限制。
---@param area string 日志所属的功能区域名称。
---@param message string 写入调试输出的说明文本。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function StateMachine:LogDebug(area, message)
    if not self:IsDebugEnabled() then
        return
    end

    print(string.format(
        "[LuaAnim][StateMachine][%s][%s][%s][ctx=%s] %s",
        tostring(self:GetLayerName()),
        tostring(self:GetDebugName()),
        tostring(area or "Debug"),
        get_debug_context_label(self),
        tostring(message or "")))
end

---输出调试flag调试信息，并遵守模块调试开关和频率限制。
---@param flag_name string 调试标志或位标志的语义名称。
---@param area string 日志所属的功能区域名称。
---@param message string 写入调试输出的说明文本。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function StateMachine:LogDebugFlag(flag_name, area, message)
    if self:IsDebugFlagEnabled(flag_name) then
        self:LogDebug(area, message)
    end
end

---读取或计算调试更新间隔，字段缺失时遵循函数内的明确回退规则。
---@return number value 读取或计算得到的数值。
function StateMachine:GetDebugUpdateInterval()
    return self:AsNumber(self.DebugUpdateInterval, 0.5)
end

---根据状态变化和调试间隔判断本帧是否应输出上下文摘要。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return boolean matched 当前事实和门控条件是否满足。
function StateMachine:ShouldLogContext(facts)
    if not self:IsDebugFlagEnabled("DebugContext") then
        return false
    end

    local state_name = facts and facts.State or self.CurrentStateName or self:GetEntryState()
    if self.LastDebugContextState ~= state_name then
        self.LastDebugContextState = state_name
        self.DebugContextElapsed = 0
        return true
    end

    local interval = self:GetDebugUpdateInterval()
    if interval <= 0 then
        return true
    end

    self.DebugContextElapsed = (self.DebugContextElapsed or 0) + (self.DeltaSeconds or 0)
    if self.DebugContextElapsed >= interval then
        self.DebugContextElapsed = 0
        return true
    end

    return false
end

---输出上下文摘要调试信息，并遵守模块调试开关和频率限制。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@param decision table|nil decision 对应的状态机或输入解析记录。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function StateMachine:LogContextSummary(facts, decision)
    if not self:ShouldLogContext(facts) then
        return
    end

    local decision_state = decision and decision.StateName or self.CurrentStateName or ""
    local animation_path = decision and decision.AnimationPath or nil
    local animation_name = decision and decision.AnimationName or nil
    self:LogDebug("Context", string.format(
        "state=%s decision=%s time=%.2f dt=%.3f speed=%.1f gait=%s desired=%s entry=%s locked=%s moveAngle=%.1f turnAngle=%.1f anim=%s path=%s blendX=%.1f",
        tostring(self.CurrentStateName or (facts and facts.State) or ""),
        tostring(decision_state),
        self:AsNumber(self.StateTime, 0),
        self:AsNumber(self.DeltaSeconds, 0),
        self:AsNumber(self.Speed, 0),
        value_to_debug_string(self.Gait),
        value_to_debug_string(self.DesiredGait),
        value_to_debug_string(self.GroundedEntryState),
        value_to_debug_string(self.bIsLockedOn),
        self:AsNumber(self.MoveDirectionAngle, 0),
        self:AsNumber(self.TurnAngle, 0),
        tostring(animation_name or ""),
        short_asset_path(animation_path),
        decision and self:AsNumber(decision.BlendInputX, 0) or self:AsNumber(self.CurrentBlendInputX, 0)))
end

---执行调用父类逻辑，并向调用方返回模块约定的结果。
---@param method_name string 需要在父类或当前上下文中解析的方法名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function StateMachine:CallSuper(method_name, ...)
    local class = rawget(self, "Class") or getmetatable(self)
    local super = class and rawget(class, "Super") or nil

    while super ~= nil do
        local method = rawget(super, method_name)
        if type(method) == "function" then
            return method(self, ...)
        end
        super = rawget(super, "Super")
    end

    return nil
end

---执行父类逻辑，并向调用方返回模块约定的结果。
---@param method_name string 需要在父类或当前上下文中解析的方法名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function StateMachine:super(method_name, ...)
    return self:CallSuper(method_name, ...)
end

---读取或计算动画层名称，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetLayerName()
    return self.LayerName or "Default"
end

---读取或计算上下文对象，字段缺失时遵循函数内的明确回退规则。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function StateMachine:GetContextObject()
    return rawget(self, "ContextObject")
end

---读取或计算运行时上下文，字段缺失时遵循函数内的明确回退规则。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return number value 读取或计算得到的数值。
function StateMachine:GetRuntimeContext(context, runtime_context)
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
function StateMachine:GetUpdateContext(context, runtime_context)
    return self:GetRuntimeContext(context, runtime_context) or context
end

---执行调用上下文函数逻辑，并向调用方返回模块约定的结果。
---@param function_name string 需要通过反射或上下文代理调用的函数名称。
---@param ... any 按原顺序透传给目标 Lua 方法或 C++ 反射接口的可变参数。
---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
function StateMachine:CallContextFunction(function_name, ...)
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
function StateMachine:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

---读取或计算当前曲线值，字段缺失时遵循函数内的明确回退规则。
---@param curve_name string|nil curve_name 对应的语义字符串或资源标识。
---@param fallback any 读取失败、字段缺失或类型不符时使用的默认值。
---@return number value 读取或计算得到的数值。
function StateMachine:GetCurrentCurveValue(curve_name, fallback)
    local value = self:CallCpp(
        "GetLuaAnimCurveValue",
        self:GetLayerName(),
        curve_name,
        fallback or 0)
    return self:AsNumber(value, fallback or 0)
end

---读取 0..1 循环曲线；BlendSpace 会由 C++ 使用单位圆插值，避免 0.98 与 0.02 被错误混合成 0.5。
---@param curve_name string 曲线名。
---@param fallback number|nil 曲线缺失时的返回值。
---@return number value 当前循环曲线值。
function StateMachine:GetCurrentCircularCurveValue(curve_name, fallback)
    local default_value = fallback or 0
    local value = self:CallCpp(
        "GetLuaAnimCircularCurveValue",
        self:GetLayerName(),
        curve_name,
        default_value)
    return self:AsNumber(value, default_value)
end

---在目标 Sequence 或 BlendSpace 上反查最接近指定循环相位的归一化时间。
---reference_normalized_time 只在同一相位出现多次时用于选择最近周期，不会压过相位误差。
---@param animation_ref string 动画资源别名或路径。
---@param curve_name string 0..1 循环曲线名。
---@param target_value number 需要匹配的循环相位。
---@param blend_input_x number|nil BlendSpace X 输入，Sequence 传 0。
---@param blend_input_y number|nil BlendSpace Y 输入，Sequence 传 0。
---@param blend_input_z number|nil BlendSpace Z 输入，Sequence 传 0。
---@param reference_normalized_time number|nil 多解时优先靠近的归一化位置。
---@param fallback number|nil 曲线缺失或资产无效时的返回值。
---@return number value 匹配位置；失败时返回 fallback。
function StateMachine:FindCircularCurveMatchingNormalizedTime(
    animation_ref,
    curve_name,
    target_value,
    blend_input_x,
    blend_input_y,
    blend_input_z,
    reference_normalized_time,
    fallback)
    local default_value = fallback or -1
    local value = self:CallCpp(
        "FindLuaAnimCircularCurveMatchingNormalizedTimeByPath",
        self:GetAnimationPath(animation_ref),
        curve_name,
        target_value or 0,
        blend_input_x or 0,
        blend_input_y or 0,
        blend_input_z or 0,
        reference_normalized_time or 0,
        180,
        default_value)
    return self:AsNumber(value, default_value)
end

---读取或计算当前曲线int值，字段缺失时遵循函数内的明确回退规则。
---@param curve_name string|nil curve_name 对应的语义字符串或资源标识。
---@param fallback any 读取失败、字段缺失或类型不符时使用的默认值。
---@return number value 读取或计算得到的数值。
function StateMachine:GetCurrentCurveIntValue(curve_name, fallback)
    local value = self:CallCpp(
        "GetLuaAnimCurveIntValue",
        self:GetLayerName(),
        curve_name,
        fallback or 0)
    return math.floor(self:AsNumber(value, fallback or 0) + 0.5)
end

---判断当前上下文是否具有当前曲线flag。
---@param curve_name string|nil curve_name 对应的语义字符串或资源标识。
---@param flag_mask number 需要检查的整数位掩码。
---@return boolean matched 当前事实和门控条件是否满足。
function StateMachine:HasCurrentCurveFlag(curve_name, flag_mask)
    return self:CallCpp(
        "HasLuaAnimCurveFlag",
        self:GetLayerName(),
        curve_name,
        flag_mask or 0) == true
end

---读取或计算当前动画归一化时间，字段缺失时遵循函数内的明确回退规则。
---@param fallback any 读取失败、字段缺失或类型不符时使用的默认值。
---@return number value 读取或计算得到的数值。
function StateMachine:GetCurrentAnimationNormalizedTime(fallback)
    local value = self:CallCpp(
        "GetLuaAnimNormalizedTime",
        self:GetLayerName(),
        fallback or 0)
    return self:Clamp(self:AsNumber(value, fallback or 0), 0, 1)
end

---读取当前动画按播放倍率换算后的剩余秒数，供一次性动画在尾段建立重叠过渡。
---@param fallback number|nil 动画资源或播放倍率无效时使用的值；建议传 -1 区分查询失败。
---@return number remaining_time 当前动画剩余秒数；查询失败时返回 fallback。
function StateMachine:GetCurrentAnimationRemainingTime(fallback)
    local default_value = fallback
    if default_value == nil then
        default_value = -1
    end

    local value = self:CallCpp(
        "GetLuaAnimRemainingTime",
        self:GetLayerName(),
        default_value)
    return self:AsNumber(value, default_value)
end

---读取或计算状态列表，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetStateList()
    local state_list = self.StateList
    if type(state_list) == "table" and #state_list > 0 then
        return state_list
    end

    local states = self.States or self.State
    if type(states) == "table" and #states > 0 then
        return states
    end

    return nil
end

---判断给定状态名是否存在于 StateList 或 State 映射中。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@return boolean matched 当前事实和门控条件是否满足。
function StateMachine:IsKnownState(state_name)
    local text = self:NameToString(state_name)
    if text == "" then
        return false
    end

    local state_list = self:GetStateList()
    if state_list ~= nil then
        for _, value in ipairs(state_list) do
            if text == value then
                return true
            end
        end
    end

    for _, value in pairs(self.States or self.State or {}) do
        if text == value then
            return true
        end
    end

    return false
end

---读取或计算入口状态，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetEntryState()
    if self.EntryState ~= nil then
        return self.EntryState
    end

    if self.InitialState ~= nil then
        return self.InitialState
    end

    local state_list = self:GetStateList()
    if state_list ~= nil and state_list[1] ~= nil then
        return state_list[1]
    end

    local states = self.States or self.State or {}
    return states.Idle or "Idle"
end

---读取或计算初始状态，字段缺失时遵循函数内的明确回退规则。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetInitialState()
    return self:GetEntryState()
end

---把数值限制到指定最小值和最大值之间。
---@param value any 待读取、转换、比较或写入的输入值。
---@param min_value number 允许范围的最小值。
---@param max_value number 允许范围的最大值。
---@return number value 计算或回退后的数值。
function StateMachine:Clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

---把动态 Lua/UObject 值转换为 number，转换失败时使用回退值。
---@param value any 待读取、转换、比较或写入的输入值。
---@param fallback any 读取失败、字段缺失或类型不符时使用的默认值。
---@return number value 计算或回退后的数值。
function StateMachine:AsNumber(value, fallback)
    local number = tonumber(value)
    if number == nil then
        return fallback or 0
    end

    return number
end

---执行名称to字符串逻辑，并向调用方返回模块约定的结果。
---@param value any 待读取、转换、比较或写入的输入值。
---@return string name 规范化或分类后的语义名称。
function StateMachine:NameToString(value)
    if value == nil then
        return ""
    end

    local text = tostring(value)
    if text == "None" then
        return ""
    end

    return text
end

---执行名称键逻辑，并向调用方返回模块约定的结果。
---@param value any 待读取、转换、比较或写入的输入值。
---@return string name 规范化或分类后的语义名称。
function StateMachine:NameKey(value)
    local text = string.lower(self:NameToString(value))
    return string.gsub(text, "[^%w]", "")
end

---执行纯文本包含检查，不把 pattern 当作 Lua 正则表达式。
---@param text string|nil text 使用的语义文本或名称。
---@param pattern string|nil pattern 使用的语义文本或名称。
---@return boolean contains 目标文本是否包含指定模式。
function StateMachine:Contains(text, pattern)
    if text == nil or pattern == nil then
        return false
    end

    return string.find(string.lower(tostring(text)), string.lower(tostring(pattern)), 1, true) ~= nil
end

---安全读取上下文字段，访问失败时返回明确回退值。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param field_name string 需要从 Lua 表或 UObject 中读取的字段名称。
---@return any result 根据输入和回退规则解析出的结果；无法解析时为 nil 或调用方提供的默认值。
function StateMachine:ReadContextField(context, field_name)
    if context == nil or field_name == nil then
        return nil
    end

    if type(context) == "table" and context[field_name] ~= nil then
        return context[field_name]
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, value = pcall(function()
        return context:GetLuaAnimPropertyText(field_name, "")
    end)
    if ok and value ~= nil and value ~= "" then
        return value
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    ok, value = pcall(function()
        return context[field_name]
    end)
    if ok and value ~= nil then
        return value
    end

    return nil
end

---安全读取上下文数值，访问失败时返回明确回退值。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param field_name string 需要从 Lua 表或 UObject 中读取的字段名称。
---@param fallback number|nil 读取失败或无法转换为数字时使用的默认值；缺失时使用 0。
---@return number value 上下文字段数值或回退值。
function StateMachine:ReadContextNumber(context, field_name, fallback)
    if type(context) == "table" and context[field_name] ~= nil then
        return self:AsNumber(context[field_name], fallback)
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, value = pcall(function()
        return context:GetLuaAnimNumberProperty(field_name, fallback or 0)
    end)
    if ok then
        return self:AsNumber(value, fallback)
    end

    local direct_number = tonumber(self:ReadContextField(context, field_name))
    if direct_number ~= nil then
        return direct_number
    end

    return fallback or 0
end

---安全读取上下文布尔值，访问失败时返回明确回退值。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param field_name string 需要从 Lua 表或 UObject 中读取的字段名称。
---@param fallback boolean|nil 读取失败或无法识别布尔语义时使用的默认值。
---@return boolean value 上下文字段布尔值或回退值。
function StateMachine:ReadContextBool(context, field_name, fallback)
    local direct_value = self:ReadContextField(context, field_name)
    if direct_value == true or direct_value == "true" or direct_value == 1 then
        return true
    end
    if direct_value == false or direct_value == "false" or direct_value == 0 then
        return false
    end

    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, value = pcall(function()
        return context:GetLuaAnimBoolProperty(field_name, fallback == true)
    end)
    if ok then
        return value == true
    end

    return fallback == true
end

---读取或计算快照，字段缺失时遵循函数内的明确回退规则。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function StateMachine:GetSnapshot(context, runtime_context)
    local update_context = self:GetUpdateContext(context, runtime_context)
    if update_context == nil then
        return nil
    end

    if type(update_context) == "table" and type(update_context.Snapshot) == "table" then
        return update_context.Snapshot
    end

    local layer_name = self:GetLayerName()
    ---在受保护调用中访问动态 Lua/UObject 数据；异常由外层 pcall 或 try_call 转换为失败结果。
    ---@return any result 目标调用的返回值；多返回值保持原始顺序透传。
    local ok, snapshot = pcall(function()
        return update_context:GetLuaAnimLayerSnapshot(layer_name)
    end)
    if ok then
        return snapshot
    end

    return nil
end

---读取或计算当前状态，字段缺失时遵循函数内的明确回退规则。
---@param snapshot table|nil C++ 预计算并暴露给 Lua 的线程安全动画快照。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetCurrentState(snapshot)
    if snapshot == nil then
        return ""
    end

    return self:NameToString(snapshot.CurrentStateName)
end

---读取或计算状态时间，字段缺失时遵循函数内的明确回退规则。
---@param snapshot table|nil C++ 预计算并暴露给 Lua 的线程安全动画快照。
---@return number value 读取或计算得到的数值。
function StateMachine:GetStateTime(snapshot)
    if snapshot == nil then
        return 0
    end

    return self:AsNumber(snapshot.CurrentTime, 0)
end

---执行规范化状态逻辑，并向调用方返回模块约定的结果。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@return string name 规范化或分类后的语义名称。
function StateMachine:NormalizeState(state_name)
    local text = self:NameToString(state_name)
    if text == "" then
        return self:GetEntryState()
    end

    if self:IsKnownState(text) then
        return text
    end

    return self:GetEntryState()
end

---根据当前快照构建事实表。
---@param _context userdata|table|nil UnLua 或动画宿主传入的调用上下文；当前函数保留该参数以匹配 C++ 回调签名。
---@param snapshot table|nil C++ 预计算并暴露给 Lua 的线程安全动画快照。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return table facts 供转换和动画更新函数直接读取的本帧事实表。
function StateMachine:BuildFacts(_context, snapshot, delta_seconds)
    return {
        State = self:NormalizeState(self:GetCurrentState(snapshot)),
        StateTime = self:GetStateTime(snapshot),
        DeltaSeconds = self:AsNumber(delta_seconds, 0),
        HasPose = snapshot ~= nil and (snapshot.HasPose == true or snapshot.bHasPose == true),
    }
end

---应用运行时上下文，只修改当前实例或对应 C++ 策略。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@param snapshot table|nil C++ 预计算并暴露给 Lua 的线程安全动画快照。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function StateMachine:ApplyRuntimeContext(context, runtime_context, snapshot, delta_seconds)
    local update_context = self:GetUpdateContext(context, runtime_context)
    local runtime_table = self:GetRuntimeContext(context, runtime_context)
    local context_object = resolve_context_object(context) or resolve_context_object(runtime_context)

    self.Context = context_object or update_context
    self.ContextObject = context_object
    self.RuntimeContext = runtime_table
    self.UpdateContext = update_context
    self.Snapshot = snapshot
    self.DeltaSeconds = self:AsNumber(delta_seconds, 0)

    if type(runtime_table) ~= "table" then
        return
    end

    for key, value in pairs(runtime_table) do
        if type(key) == "string" and not RuntimeFieldSkip[key] then
            self[key] = value
        end
    end

    if type(runtime_table.Snapshot) == "table" then
        self.Snapshot = runtime_table.Snapshot
    end
end

---应用事实表，只修改当前实例或对应 C++ 策略。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return nil 该函数只更新当前实例或 C++ 运行时，不返回业务值。
function StateMachine:ApplyFacts(facts)
    self.LastFacts = facts
    if type(facts) ~= "table" then
        return
    end

    if facts.State ~= nil then
        self.CurrentState = facts.State
        self.CurrentStateName = facts.State
    end

    for key, value in pairs(facts) do
        if key ~= "State" then
            self[key] = value
        end
    end
end

---读取或计算动画设置，字段缺失时遵循函数内的明确回退规则。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetAnimationSettings(state_name)
    return (self.AnimationSettings or {})[state_name] or {}
end

---读取或计算动画路径，字段缺失时遵循函数内的明确回退规则。
---@param animation_key string|nil 动画资产表中的语义键名。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetAnimationPath(animation_key)
    if animation_key == nil then
        return nil
    end

    local assets = self.Assets or {}
    return assets[animation_key] or animation_key
end

---读取或计算动画名称，字段缺失时遵循函数内的明确回退规则。
---@param animation_ref string|table|userdata|nil 动画资源别名、软路径或已解析的 UE 动画对象。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetAnimationName(animation_ref)
    if type(animation_ref) ~= "string" then
        return nil
    end

    local assets = self.Assets or {}
    if assets[animation_ref] ~= nil then
        return animation_ref
    end

    for name, path in pairs(assets) do
        if path == animation_ref then
            return name
        end
    end

    if string.sub(animation_ref, 1, 1) ~= "/" then
        return animation_ref
    end

    return nil
end

---返回当前正在求值的状态名；无临时状态时回退当前状态或 Entry。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetEvaluatingStateName()
    return self.EvaluatingStateName or self.CurrentState or self:GetEntryState()
end

---读取或计算姿势混合时间，字段缺失时遵循函数内的明确回退规则。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param options table|nil 播放、混合、循环、相位匹配或惯性化选项。
---@return number value 读取或计算得到的数值。
function StateMachine:GetPoseBlendTime(state_name, options)
    local pose_options = options or {}
    if pose_options.BlendTime ~= nil then
        return pose_options.BlendTime
    end

    local settings = self:GetAnimationSettings(state_name)
    if settings.BlendTime ~= nil then
        return settings.BlendTime
    end

    return self.DefaultBlendTime or 0.15
end

---读取或计算姿势循环标志，字段缺失时遵循函数内的明确回退规则。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param options table|nil 播放、混合、循环、相位匹配或惯性化选项。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function StateMachine:GetPoseLoop(state_name, options)
    local pose_options = options or {}
    if pose_options.Loop ~= nil then
        return pose_options.Loop ~= false
    end
    if pose_options.bLoop ~= nil then
        return pose_options.bLoop ~= false
    end

    local settings = self:GetAnimationSettings(state_name)
    return settings.Loop ~= false
end

---从 Lua Pose 连接、动画节点或原始句柄中解析 C++ 可消费的 NativePoseLink。
---@param pose_source LuaAnimNodeBase|LuaPoseLinkBase|SekiroNativePoseLink|nil 状态图返回的节点、连接或原始句柄。
---@return SekiroNativePoseLink|nil native_pose_link 可传给 C++ Pose Graph 的原生句柄；输入无效时为 nil。
function StateMachine:ResolveNativePoseLink(pose_source)
    if pose_source == nil then
        return nil
    end

    if type(pose_source) == "table" and type(pose_source.GetNativePoseLink) == "function" then
        return pose_source:GetNativePoseLink()
    end

    return pose_source
end

---读取或创建由当前状态机持有的 SequencePlayer 类实例。
---同一状态机、动画层和节点名始终返回同一 Lua 对象，对象自己保存节点参数与 C++ PoseLink。
---@param node_name string 节点在所属动画层内的稳定名称，建议与状态名一致且不能为空。
---@param layer_name string|nil 节点所属动画层；缺失时使用当前状态机层。
---@return LuaSequencePlayer|nil node 持久 SequencePlayer 类实例；节点名为空时为 nil。
function StateMachine:GetSequencePlayer(node_name, layer_name)
    local resolved_layer_name = layer_name or self:GetLayerName()
    local stable_node_name = tostring(node_name or "")
    if stable_node_name == "" then
        self:LogDebugFlag("DebugPose", "PoseGraph", string.format(
            "invalid SequencePlayer layer=%s node=%s",
            tostring(resolved_layer_name),
            stable_node_name))
        return nil
    end

    local cache_key = tostring(resolved_layer_name) .. "\31" .. stable_node_name
    local node = self.AnimNodes[cache_key]
    if node == nil then
        node = LuaSequencePlayer()
        node:Bind(self, stable_node_name, resolved_layer_name)
        self.AnimNodes[cache_key] = node
    end

    return node
end

---把状态动画函数返回的根 Pose 发布给 C++，由动画线程求值并执行前后 Pose 过渡。
---@param state_name string 目标或当前状态名称，用于读取目标状态 BlendTime。
---@param pose_source LuaPoseLinkBase|SekiroNativePoseLink|nil 状态动画函数返回的状态 Pose 连接。
---@return boolean published C++ 成功接受根 Pose 时为 true。
function StateMachine:PublishOutputPose(state_name, pose_source)
    if type(pose_source) == "table"
        and type(pose_source.Evaluate) == "function"
        and pose_source:Evaluate() ~= true then
        return false
    end

    local pose_link = self:ResolveNativePoseLink(pose_source)
    if pose_link == nil then
        return false
    end

    local transition_node = type(pose_source) == "table" and pose_source.IsLuaPoseLink == true
        and pose_source:GetLinkNode()
        or pose_source
    local transition_time = type(transition_node) == "table"
        and type(transition_node.GetTransitionTime) == "function"
        and transition_node:GetTransitionTime(state_name)
        or self:GetPoseBlendTime(state_name, nil)
    return self:CallCpp(
        "PublishLuaOutputPose",
        self:GetLayerName(),
        pose_link,
        math.max(self:AsNumber(transition_time, 0), 0)) == true
end

---调用目标状态的动画函数并返回该状态的根 Pose，不再构造旧动画决策表。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return LuaPoseLinkBase|LuaPoseLink|nil pose_source 状态动画函数返回的根 Pose；函数不存在或未生成 Pose 时为 nil。
function StateMachine:UpdateAnimation(state_name, facts)
    local updater = self["UpdateAnimation_" .. state_name]
    if type(updater) == "function" then
        self.EvaluatingStateName = state_name
        local animation_node = updater(self, facts)
        self.EvaluatingStateName = nil
        return self:LinkStatePose(state_name, animation_node)
    end

    self:LogDebugFlag("DebugPose", "PoseGraph", string.format(
        "missing UpdateAnimation_%s",
        tostring(state_name)))
    return nil
end

---按 StateList 顺序创建状态数据、StateResult 和 StatePoseLinks。
---拓扑固定为 StatePoseLinks[i] -> StateResult[i] -> Result -> 状态内部 AnimNode，复刻 UE 状态图输出层。
---@return nil 该函数只重建状态拓扑和名称索引。
function StateMachine:BuildStatePoseLinks()
    self.StateRecords = {}
    self.StateRecordByName = {}
    self.StatePoseLinks = {}
    self.StatePoseLinkByName = {}

    local state_list = self:GetStateList()
    if state_list ~= nil then
        for _, state_name in ipairs(state_list) do
            self:GetStatePoseLink(state_name)
        end
        return
    end

    local fallback_state_names = {}
    for _, state_name in pairs(self.States or self.State or {}) do
        table.insert(fallback_state_names, state_name)
    end
    table.sort(fallback_state_names)
    for _, state_name in ipairs(fallback_state_names) do
        self:GetStatePoseLink(state_name)
    end
end

---读取状态对应的数据记录；缺失时同时创建 StateResult 和外层 StatePoseLink。
---@param state_name string|nil 目标状态语义名称。
---@return LuaAnimationState|nil state_record 状态数据；状态名为空时为 nil。
function StateMachine:GetStateRecord(state_name)
    local resolved_state_name = self:NameToString(state_name)
    if resolved_state_name == "" then
        return nil
    end

    local state_record = self.StateRecordByName[resolved_state_name]
    if state_record ~= nil then
        return state_record
    end

    local state_index = #self.StateRecords
    local settings = self:GetAnimationSettings(resolved_state_name)
    state_record = LuaAnimationState({
        StateName = resolved_state_name,
        StateIndex = state_index,
        bAlwaysResetOnEntry = settings.bAlwaysResetOnEntry == true,
    })
    state_record.StateResult:Bind(
        self,
        resolved_state_name .. ".StateResult",
        self:GetLayerName())

    local pose_link = LuaPoseLink()
    pose_link.StateName = resolved_state_name
    pose_link.LinkID = state_index
    pose_link:Link(state_record.StateResult)

    table.insert(self.StateRecords, state_record)
    table.insert(self.StatePoseLinks, pose_link)
    self.StateRecordByName[resolved_state_name] = state_record
    self.StatePoseLinkByName[resolved_state_name] = pose_link
    return state_record
end

---读取状态图固定的 StateResult 节点。
---@param state_name string|nil 目标状态语义名称。
---@return LuaStateResult|nil state_result 状态图输出节点；状态名为空时为 nil。
function StateMachine:GetStateResult(state_name)
    local state_record = self:GetStateRecord(state_name)
    return state_record and state_record.StateResult or nil
end

---读取状态机连接到指定 StateResult 的 PoseLink；缺失状态会按 StateList 规则补建。
---@param state_name string|nil 目标状态语义名称。
---@return LuaPoseLink|nil state_pose_link 状态专属 PoseLink；状态名为空时为 nil。
function StateMachine:GetStatePoseLink(state_name)
    local resolved_state_name = self:NameToString(state_name)
    if resolved_state_name == "" then
        return nil
    end

    local pose_link = self.StatePoseLinkByName[resolved_state_name]
    if pose_link == nil then
        self:GetStateRecord(resolved_state_name)
        pose_link = self.StatePoseLinkByName[resolved_state_name]
    end

    return pose_link
end

---把 UpdateAnimation 返回的动画节点接到该状态 StateResult.Result。
---外层 StatePoseLink 始终连接 StateResult，不会因状态每帧选择不同动画节点而改变拓扑身份。
---@param state_name string|nil 需要配置 Pose 的状态名称。
---@param pose_source LuaAnimNodeBase|LuaPoseLinkBase|nil 状态动画函数返回的节点或已有连接。
---@return LuaPoseLink|nil state_pose_link 完成连接的状态 PoseLink；输入无效时为 nil。
function StateMachine:LinkStatePose(state_name, pose_source)
    local state_pose_link = self:GetStatePoseLink(state_name)
    local state_record = self:GetStateRecord(state_name)
    if state_pose_link == nil or state_record == nil or pose_source == nil then
        return nil
    end

    if state_record:SetResult(pose_source) ~= true then
        return nil
    end

    return state_pose_link
end

---读取当前状态 PoseLink 最终映射出的 C++ 原生句柄。
---@return SekiroNativePoseLink|nil native_pose_link 当前状态节点的原生输出；状态尚未连接时为 nil。
function StateMachine:GetNativePoseLink()
    local state_pose_link = self:GetStatePoseLink(self.CurrentStateName or self.CurrentState)
    return state_pose_link and state_pose_link:GetNativePoseLink() or nil
end

---求值当前状态 PoseLink，使状态机本身可以像其他 LuaAnimNodeBase 节点一样被上游 PoseLink 连接。
---@return boolean evaluated 当前状态已连接并成功求值时为 true。
function StateMachine:Evaluate()
    local state_pose_link = self:GetStatePoseLink(self.CurrentStateName or self.CurrentState)
    return state_pose_link ~= nil and state_pose_link:Evaluate() == true
end

---执行状态机节点更新生命周期；内部复用状态机原有的状态切换和上下文同步入口。
---@param context table|userdata|nil Lua 动画图更新上下文。
---@param delta_seconds number|nil 本帧增量时间，单位为秒。
---@return boolean updated 状态机完成本帧更新时为 true。
function StateMachine:UpdateNode(context, delta_seconds)
    self:Update(context, delta_seconds, nil)
    return true
end

---保持当前状态，更新其 Pose 节点参数并继续发布同一根 Pose。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return LuaPoseLinkBase|LuaPoseLink|nil pose_source 已发布的当前状态根 Pose；发布失败时为 nil。
function StateMachine:Keep(state_name, facts)
    self.EvaluatingResetTime = false
    local pose_source = self:UpdateAnimation(state_name, facts)
    self.EvaluatingResetTime = nil
    if pose_source == nil or not self:PublishOutputPose(state_name, pose_source) then
        return nil
    end

    self.CurrentState = state_name
    self.CurrentStateName = state_name
    return pose_source
end

---判断目标状态重新进入时是否需要重置原生资产播放器。
---未处于活动过渡链的状态按普通首次进入处理；仍有 Pose 权重的状态默认延续时间，只有
---bAlwaysResetOnEntry 明确启用时才强制重置，与 UE 状态机的重入语义保持一致。
---@param state_name string|nil 即将进入的目标状态名称。
---@return boolean reset_time 本次进入是否应递增播放器重置序号。
function StateMachine:ShouldResetStateOnEntry(state_name)
    local state_record = self:GetStateRecord(state_name)
    if state_record == nil or state_record.bAlwaysResetOnEntry == true then
        return true
    end

    local state_result = state_record.StateResult
    if state_result == nil or state_result:GetNativePoseLink() == nil then
        return true
    end

    local is_active = self:CallCpp(
        "IsLuaPoseLinkActive",
        state_result:GetNativePoseLink())
    return is_active ~= true
end

---进入目标状态，按 UE 重入语义决定是否重置播放器，并用目标状态 BlendTime 发布根 Pose。
---@param state_name string|nil 目标或当前状态的语义名称，必须来自状态机集中定义。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return LuaPoseLinkBase|LuaPoseLink|nil pose_source 已发布的目标状态根 Pose；发布失败时为 nil。
function StateMachine:Enter(state_name, facts)
    local from_state = self.CurrentStateName or self.CurrentState or (facts and facts.State) or ""
    local reset_time = self:ShouldResetStateOnEntry(state_name)
    self:LogDebugFlag("DebugTransitions", "Enter", string.format(
        "%s -> %s reset=%s",
        tostring(from_state),
        tostring(state_name),
        tostring(reset_time)))

    self.EvaluatingResetTime = reset_time
    local pose_source = self:UpdateAnimation(state_name, facts)
    self.EvaluatingResetTime = nil
    if pose_source == nil or not self:PublishOutputPose(state_name, pose_source) then
        return nil
    end

    self.CurrentState = state_name
    self.CurrentStateName = state_name
    return pose_source
end

---读取或计算转换函数名称，字段缺失时遵循函数内的明确回退规则。
---@param from_state string 转换起点状态名称。
---@param to_state string 转换目标状态名称。
---@return string|nil value 解析出的语义名称、状态或资源引用。
function StateMachine:GetTransitionFunctionName(from_state, to_state)
    return "CanEnter_" .. from_state .. "_" .. to_state
end

---按 CanEnter_<From>_<To> 命名规则查找 Transition 函数并执行，未声明转换时返回 false。
---@param from_state string 转换起点状态名称。
---@param to_state string 转换目标状态名称。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return boolean allowed 对应 Transition 是否存在且条件返回 true。
function StateMachine:CanEnterState(from_state, to_state, facts)
    local method = self[self:GetTransitionFunctionName(from_state, to_state)]
    if type(method) == "function" then
        local result = method(self, facts) == true
        if self:IsDebugFlagEnabled("DebugTransitionChecks") then
            self:LogDebug("TransitionCheck", string.format(
                "%s -> %s = %s",
                tostring(from_state),
                tostring(to_state),
                tostring(result)))
        end
        return result
    end

    return false
end

---尝试从起点进入目标状态，门控失败时不修改状态机。
---@param from_state string 转换起点状态名称。
---@param to_state string 转换目标状态名称。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table|nil decision 转换成功时的 Enter 决策；条件不满足时为 nil。
function StateMachine:TryEnter(from_state, to_state, facts)
    if self:CanEnterState(from_state, to_state, facts) then
        return self:Enter(to_state, facts)
    end

    return nil
end

---按调用方给定顺序尝试多个目标状态，返回第一个成功转换。
---@param from_state string 转换起点状态名称。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@param to_states string[] 候选目标状态数组，顺序即转换优先级。
---@return table|nil decision 第一个成功转换的 Enter 决策；全部失败时为 nil。
function StateMachine:TryTransitions(from_state, facts, to_states)
    for _, to_state in ipairs(to_states or {}) do
        local decision = self:TryEnter(from_state, to_state, facts)
        if decision ~= nil then
            return decision
        end
    end

    return nil
end

---按类声明顺序把 CanEnter_<From>_<To> 烘焙为转换描述，并登记到源状态。
---Any 转换只进入状态机级数组，不属于某个普通状态的出边。
---@return nil 该函数重建转换描述，不执行规则。
function StateMachine:BuildTransitionRecords()
    self.TransitionRecords = {}
    local seen = {}

    for _, state_record in ipairs(self.StateRecords or {}) do
        state_record.Transitions = {}
    end

    ---登记一条已解析的转换，并保持类声明优先级稳定。
    ---@param from_state string 转换源状态名称，Any 表示全局转换。
    ---@param to_state string 转换目标状态名称。
    ---@param rule_name string Lua 规则函数名。
    ---@return nil 该局部函数只登记转换描述。
    local function add_transition(from_state, to_state, rule_name)
        if seen[rule_name] == true or type(self[rule_name]) ~= "function" then
            return
        end

        local transition = LuaAnimationTransition({
            PreviousState = from_state,
            NextState = to_state,
            RuleName = rule_name,
            CrossfadeDuration = self:GetPoseBlendTime(to_state, nil),
            Priority = #self.TransitionRecords,
        })
        if transition:IsValid() ~= true then
            return
        end

        table.insert(self.TransitionRecords, transition)
        seen[rule_name] = true
        if from_state ~= "Any" and self:IsKnownState(from_state) then
            local source_state = self:GetStateRecord(from_state)
            if source_state ~= nil then
                source_state:AddTransition(transition)
            end
        end
    end

    for _, edge in ipairs(rawget(self.Class or {}, "__TransitionOrder") or {}) do
        if type(edge.Name) == "string" then
            add_transition(edge.From, edge.To, edge.Name)
        end
    end

    for key, value in pairs(self.Class or getmetatable(self) or self) do
        if type(key) == "string" and type(value) == "function" and seen[key] ~= true then
            local from_state, to_state = string.match(key, "^CanEnter_([^_]+)_(.+)$")
            if from_state ~= nil and to_state ~= nil then
                add_transition(from_state, to_state, key)
            end
        end
    end

    return nil
end

---读取当前状态机已烘焙的稳定顺序转换描述。
---@return LuaAnimationTransition[] transitions 转换描述数组。
function StateMachine:GetDeclaredTransitionEdges()
    return self.TransitionRecords or {}
end

---读取或计算自动转换转换边，字段缺失时遵循函数内的明确回退规则。
---@param from_state string 转换起点状态名称。
---@return table|userdata|nil value 解析出的配置表或 UE 运行时对象。
function StateMachine:GetAutoTransitionEdges(from_state)
    local edges = {}

    for _, edge in ipairs(self:GetDeclaredTransitionEdges()) do
        if edge.PreviousState == "Any"
            and edge.NextState ~= from_state
            and self:IsKnownState(edge.NextState) then
            table.insert(edges, edge)
        end
    end

    local source_state = self:GetStateRecord(from_state)
    for _, edge in ipairs(source_state and source_state.Transitions or {}) do
        if edge.NextState ~= from_state and self:IsKnownState(edge.NextState) then
            table.insert(edges, edge)
        end
    end

    return edges
end

---尝试执行自动transitions，条件不满足时保持当前状态且不产生非法副作用。
---@param from_state string 转换起点状态名称。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return table|boolean|nil result 成功时的状态决策或 true；不满足条件时为 nil/false。
function StateMachine:TryAutoTransitions(from_state, facts)
    for _, edge in ipairs(self:GetAutoTransitionEdges(from_state)) do
        if self:CanEnterState(edge.PreviousState, edge.NextState, facts) then
            self:LogDebugFlag("DebugTransitions", "Transition", string.format(
                "current=%s rule=%s from=%s to=%s",
                tostring(from_state),
                tostring(edge.RuleName),
                tostring(edge.PreviousState),
                tostring(edge.NextState)))
            return self:Enter(edge.NextState, facts)
        end
    end

    return nil
end

---按照状态机优先级选择入口状态。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return string|table|nil selection 按优先级选出的状态、资源或对象。
function StateMachine:SelectEntryState(facts)
    self:LogDebugFlag("DebugTransitions", "Entry", string.format(
        "entry=%s hasPose=%s",
        tostring(self:GetEntryState()),
        tostring(facts and facts.HasPose)))
    return self:Enter(self:GetEntryState(), facts)
end

---更新状态：按声明顺序检查转换；没有转换时保持当前状态并更新其动画节点。
---@param facts table|nil 当前状态机事实表，包含本帧快照、状态时间和转换所需数据。
---@return LuaPoseLinkBase|LuaPoseLink|nil pose_source 本帧保持或进入状态后发布的状态根 Pose。
function StateMachine:UpdateState(facts)
    local state_name = self:NormalizeState(facts and facts.State or nil)
    local transition_decision = self:TryAutoTransitions(state_name, facts)
    if transition_decision ~= nil then
        return transition_decision
    end

    return self:Keep(state_name, facts)
end

---执行一次完整状态机更新：读取快照、求值转换并把根 Pose 直接发布给 C++ Pose Graph。
---@param context userdata|table|nil 当前 UnLua、动画蓝图或组件运行时上下文，用于访问 C++ 对象和快照。
---@param delta_seconds number|nil 本帧增量时间，单位为秒；缺失时按 0 处理。
---@param runtime_context userdata|table|nil 本帧 C++ 运行时上下文；存在时优先作为字段和接口调用目标。
---@return nil Pose 已通过 PublishLuaOutputPose 直接提交，不再向 C++ 返回旧动画决策表。
function StateMachine:Update(context, delta_seconds, runtime_context)
    local update_context = self:GetUpdateContext(context, runtime_context)
    local snapshot = self:GetSnapshot(context, runtime_context)
    self:ApplyRuntimeContext(context, runtime_context, snapshot, delta_seconds)

    local facts = self:BuildFacts(update_context, snapshot, delta_seconds)
    self:ApplyFacts(facts)

    local root_pose = nil
    if not facts.HasPose then
        root_pose = self:SelectEntryState(facts)
    else
        root_pose = self:UpdateState(facts)
    end
    if root_pose == nil then
        root_pose = self:SelectEntryState(facts)
    end

    self.LastDecision = root_pose
    self:LogContextSummary(facts, nil)
    return nil
end

return StateMachine
