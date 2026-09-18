-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Lua 动画蓝图的编译期基类。
-- 子类把 AnimGraph 和 Animation Layer 当作动画蓝图函数编写；Graph 分配、状态子图构建与 IR 导出由基类完成。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LuaAnimLayer = require("Animation.Compiler.LuaAnimLayer")
local IRValue = require("Animation.Compiler.IRValue")
local BlueprintKind = UE.ELuaAnimIRBlueprintKind

---@class LuaAnimBlueprintConfig
---@field BlueprintKind SekiroAnimIRBlueprintKind|nil ELuaAnimIRBlueprintKind 原生枚举值。
---@field SourceModule string|nil 当前 Lua 动画声明的 require 模块名。
---@field ParentAnimInstanceClass string|nil 父 AnimInstance 或父 AnimBlueprint GeneratedClass 软路径。
---@field TargetSkeleton string|nil 普通 AnimBlueprint 的 Skeleton 资产软路径。
---@field InheritedDefaults table<string, boolean|number|string|SekiroAnimIRValue>|nil 只覆盖显式列出的父类 UPROPERTY 默认值。
---@field ImplementedInterfaces string[]|nil 实现的 Animation Layer Interface GeneratedClass 软路径。

---@class LuaAnimBlueprintExport: LuaAnimBlueprint
---@field CompileIR fun(): SekiroAnimBlueprintIR 创建干净编译实例并导出 IR。
---@field Extend fun(self: LuaAnimBlueprintExport, class_name: string, definition: table<string, any>|nil):LuaAnimBlueprint 创建可继续编译的 Lua 动画蓝图子类。

---@class LuaAnimBlueprint: CompilerClass
---@field SchemaVersion number IR Schema 版本整数。
---@field BlueprintKind SekiroAnimIRBlueprintKind ELuaAnimIRBlueprintKind 原生枚举值。
---@field SourceModule string 动画蓝图 Lua 模块名。
---@field ParentAnimInstanceClass string 父 AnimInstance 类软路径。
---@field TargetSkeleton string 普通 AnimBlueprint 必填的目标 Skeleton 资产软路径。
---@field InheritedDefaults table<string, boolean|number|string|SekiroAnimIRValue> 由 Lua 显式拥有的父类属性默认值。
---@field ImplementedInterfaces string[] 普通 AnimBlueprint 实现的 Animation Layer Interface 类软路径。
---@field Layers LuaAnimLayer[] 本次编译声明的 Main AnimGraph 与 Animation Layer Function Graph。
---@field LayerNames table<string, LuaAnimLayer> 按语义名称索引的 Graph 所有权作用域。
---@field SourceLocation SekiroAnimIRSourceLocation 动画蓝图源码位置。
---@field AnimGraph fun(self: LuaAnimBlueprint, graph: LuaAnimGraph):nil 子类必须 override 的主动画图函数。
local LuaAnimBlueprint = CompilerClass:Extend("LuaAnimBlueprint", {
    SchemaVersion = 4,
    BlueprintKind = BlueprintKind.AnimBlueprint,
    SourceModule = "",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "",
    InheritedDefaults = {},
    ImplementedInterfaces = {},
})

---把显式声明的父类默认值转换为稳定排序的类型化属性数组。
---这里只拥有调用方列出的属性；未列出的 CDO 属性仍由原生父类或蓝图编辑器维护。
---@param defaults table<string, boolean|number|string|SekiroAnimIRValue>|nil 属性名到类型化值的映射。
---@return SekiroAnimIRProperty[] inherited_defaults 可由 C++ 反射写入器消费的稳定数组。
local function compile_inherited_defaults(defaults)
    assert(defaults == nil or type(defaults) == "table",
        "InheritedDefaults must be a table")

    local property_names = {}
    for property_name in pairs(defaults or {}) do
        table.insert(property_names,
            IRSchema.RequireLuaIdentifier(property_name, "InheritedDefault"))
    end
    table.sort(property_names)

    local inherited_defaults = {}
    for declaration_order, property_name in ipairs(property_names) do
        table.insert(inherited_defaults, {
            Name = property_name,
            Value = IRValue.Infer(defaults[property_name]),
            DeclarationOrder = declaration_order - 1,
        })
    end
    return inherited_defaults
end

---@class LuaAnimLayerConfig
---@field FunctionName string|nil 对应 UE Animation Layer UFunction 名；省略时与 Layer 名一致。
---@field InterfaceClass string|nil 提供函数签名的 Animation Layer Interface GeneratedClass 软路径。
---@field bOverride boolean|nil 是否覆盖接口或父动画蓝图中的同名 Layer。
---@field Parameters LuaAnimFunctionParameterConfig[]|nil 函数输入参数签名。

---@class LuaAnimFunctionParameterConfig
---@field Name string 参数名。
---@field DataType string Pose、ComponentPose、Bool、Float、Byte、Integer、Name、String、Object、Class 或 Enum。
---@field TypeObjectPath string|nil Object、Class、Enum 参数对应的类型对象软路径。
---@field bIsPose boolean|nil 是否为 Pose 输入；Pose/ComponentPose 会自动推导为 true。

---把 Blueprint Bool 默认值转换为严格类型的 IR Value。
---@param value boolean Lua 动画蓝图声明的布尔默认值。
---@return SekiroAnimIRValue ir_value 可由 C++ Importer 读取的 Bool 值。
local function make_bool_default(value)
    return IRValue.Bool(value)
end

---把 Blueprint Float 默认值转换为严格类型的 IR Value。
---@param value number Lua 动画蓝图声明的浮点默认值。
---@return SekiroAnimIRValue ir_value 可由 C++ Importer 读取的 Float 值。
local function make_float_default(value)
    return IRValue.Float(value)
end

---把 Blueprint Byte 默认值转换为严格类型的 IR Value。
---@param value number Lua 动画蓝图声明的 0..255 整数默认值。
---@return SekiroAnimIRValue ir_value 可由 C++ Importer 读取的 Integer 值。
local function make_byte_default(value)
    return IRValue.Integer(value)
end

---把 Blueprint Enum 默认值转换为带原生枚举类型标签的 IR Value。
---@param value number Lua 动画蓝图声明的原生枚举值。
---@return SekiroAnimIRValue ir_value 可由 C++ Importer 结合 UEnum 路径校验的 Enum 值。
local function make_enum_default(value)
    return IRValue.Enum(value)
end

---规范化一个 Animation Layer 函数参数，并为稳定导入补齐顺序与源码位置。
---@param blueprint LuaAnimBlueprint 当前编译实例。
---@param parameter LuaAnimFunctionParameterConfig 原始参数配置。
---@param declaration_order number 当前函数签名内从零开始的顺序整数。
---@return SekiroAnimIRFunctionParameter normalized_parameter 可直接导出到 C++ IR 的参数声明。
local function normalize_function_parameter(
    blueprint,
    parameter,
    declaration_order)
    assert(type(parameter) == "table", "Animation Layer parameter must be a table")
    local data_types = {
        Pose = true,
        ComponentPose = true,
        Bool = true,
        Float = true,
        Byte = true,
        Integer = true,
        Name = true,
        String = true,
        Object = true,
        Class = true,
        Enum = true,
    }
    local name = IRSchema.RequireLuaIdentifier(
        parameter.Name,
        "Animation Layer Parameter")
    local data_type = assert(
        parameter.DataType,
        "Animation Layer parameter requires DataType")
    assert(
        data_types[data_type] == true,
        string.format("Unsupported Animation Layer parameter type '%s'", tostring(data_type)))
    local requires_type_object =
        data_type == "Object" or data_type == "Class" or data_type == "Enum"
    if requires_type_object then
        assert(
            type(parameter.TypeObjectPath) == "string"
                and parameter.TypeObjectPath ~= "",
            string.format(
                "Animation Layer parameter '%s' requires TypeObjectPath",
                name))
    end
    local type_object_path = requires_type_object
        and IRSchema.RequireAssetObjectPath(
            parameter.TypeObjectPath,
            "Animation Layer Parameter Type")
        or ""
    return {
        Name = name,
        DataType = data_type,
        TypeObjectPath = type_object_path,
        bIsPose = parameter.bIsPose == true
            or data_type == "Pose"
            or data_type == "ComponentPose",
        DeclarationOrder = declaration_order,
        SourceLocation = IRSchema.CaptureSourceLocation(
            blueprint.SourceModule,
            4),
    }
end

---复制并规范化 Animation Layer 参数数组，避免业务配置表在编译过程中被写入内部元数据。
---@param blueprint LuaAnimBlueprint 当前编译实例。
---@param parameters LuaAnimFunctionParameterConfig[]|nil 原始函数参数数组。
---@return SekiroAnimIRFunctionParameter[] normalized_parameters 确定顺序且名称唯一的参数声明。
local function normalize_function_parameters(blueprint, parameters)
    local normalized_parameters = {}
    local parameter_names = {}
    for index, parameter in ipairs(parameters or {}) do
        local normalized = normalize_function_parameter(
            blueprint,
            parameter,
            index - 1)
        assert(
            parameter_names[normalized.Name] == nil,
            string.format(
                "Animation Layer contains duplicate parameter '%s'",
                normalized.Name))
        parameter_names[normalized.Name] = true
        table.insert(normalized_parameters, normalized)
    end
    return normalized_parameters
end

---初始化单次编译实例；每次 CompileIR 都创建新实例以避免热重载残留声明。
---@param _config LuaAnimBlueprintConfig 创建实例时的字段覆盖；基类当前只读取类默认字段。
---@return nil result 该函数只初始化本次编译实例，不返回业务值。
function LuaAnimBlueprint:Initialize(_config)
    self.Layers = {}
    self.LayerNames = {}
    self.Variables = {}
    self.VariableNames = {}
    local implemented_interfaces = {}
    for _, interface_class in ipairs(self.ImplementedInterfaces or {}) do
        table.insert(
            implemented_interfaces,
            IRSchema.RequireClassObjectPath(
                interface_class,
                "Animation Layer Interface"))
    end
    self.ImplementedInterfaces = implemented_interfaces
    self.SourceLocation = IRSchema.CaptureSourceLocation(self.SourceModule, 3)
end

---声明一个将生成到 AnimBlueprint GeneratedClass 的成员变量。
---@param name string C++/Lua 运行时共同使用的成员名。
---@param data_type string Bool、Float、Byte 或 Enum。
---@param default_value boolean|number 默认值；Byte/Enum 使用 0..255 整数。
---@param type_object_path string|nil Enum 的 UEnum 对象路径，其余类型省略。
---@return SekiroAnimIRVariable variable 可传给调试工具的变量声明。
function LuaAnimBlueprint:Variable(name, data_type, default_value, type_object_path)
    local valid_name = IRSchema.RequireLuaIdentifier(name, "AnimBlueprint Variable")
    assert(self.VariableNames[valid_name] == nil, string.format("Duplicate AnimBlueprint variable '%s'", valid_name))
    local defaults = {
        Bool = make_bool_default,
        Float = make_float_default,
        Byte = make_byte_default,
        Enum = make_enum_default,
    }
    local make_default = assert(defaults[data_type], "Variable type must be Bool, Float, Byte or Enum")
    if data_type == "Enum" then
        assert(type(type_object_path) == "string" and type_object_path ~= "", "Enum variable requires type object path")
    end
    local variable = {
        Name = valid_name,
        DataType = data_type,
        TypeObjectPath = type_object_path or "",
        DefaultValue = make_default(default_value),
        bTransient = true,
        DeclarationOrder = #self.Variables,
        SourceLocation = IRSchema.CaptureSourceLocation(self.SourceModule, 3),
    }
    self.VariableNames[valid_name] = variable
    table.insert(self.Variables, variable)
    return variable
end

---声明一个主 AnimGraph 或 Animation Layer Function Graph 的 IR 所有权作用域。
---@param name string AnimBlueprint IR 内的 Layer 语义名。
---@param config_or_build LuaAnimLayerConfig|(fun(layer: LuaAnimLayer):nil)|nil Layer 函数配置或旧式即时回调。
---@param build_function (fun(layer: LuaAnimLayer):nil)|nil 配置 Layer 后执行的底层回调。
---@return LuaAnimLayer layer 已完成声明的动画层。
function LuaAnimBlueprint:AnimationLayer(
    name,
    config_or_build,
    build_function)
    local valid_name = IRSchema.RequireSemanticName(name, "Layer")
    assert(self.LayerNames[valid_name] == nil, string.format("AnimBlueprint contains duplicate Layer '%s'", valid_name))
    local config = type(config_or_build) == "table"
        and config_or_build
        or {}
    local resolved_build_function = type(config_or_build) == "function"
        and config_or_build
        or build_function

    ---@type LuaAnimLayer
    local layer = LuaAnimLayer:New({
        Blueprint = self,
        Name = valid_name,
        FunctionName = config.FunctionName,
        InterfaceClass = config.InterfaceClass,
        bOverride = config.bOverride,
        Parameters = normalize_function_parameters(
            self,
            config.Parameters),
        DeclarationOrder = #self.Layers,
        SourceLocation = IRSchema.CaptureSourceLocation(self.SourceModule, 3),
    })
    self.LayerNames[valid_name] = layer
    table.insert(self.Layers, layer)
    if resolved_build_function ~= nil then
        resolved_build_function(layer)
    end
    return layer
end

---声明一个 UE 原生 Animation Layer Function Graph，并把签名输入以具名 Pin 传给构图回调。
---接口资产可省略 build_function；普通实现或子类 Override 应连接 Graph.Result。
---@param name string Layer 语义名和默认 UFunction 名。
---@param config LuaAnimLayerConfig 函数签名、接口来源和 Override 配置。
---@param build_function (fun(graph: LuaAnimGraph, inputs: table<string, LuaAnimPin>):nil)|nil Layer Pose 实现回调。
---@return LuaAnimLayer layer 已登记并拥有 Function Graph 的动画层。
function LuaAnimBlueprint:AnimLayer(name, config, build_function)
    local layer = self:AnimationLayer(name, config or {})
    layer:FunctionGraph(build_function)
    return layer
end

---根据内联约定或独立状态机类，构建 StateMachine 拓扑和每个 State Pose Graph。
---Transition 必须声明强类型原生 Rule；运行时不再从独立状态机导出 CanEnter_* 函数。
---@param machine LuaStateMachineNode 已创建且拥有内部 StateMachine Graph 的节点。
---@param definition LuaAnimStateMachine|nil 可复用状态机描述类；为空时使用当前 AnimBlueprint 类。
---@return nil result 该函数完成状态机的全部编译期展开。
function LuaAnimBlueprint:ConfigureStateMachine(machine, definition)
    local owner = definition or self
    local is_external = definition ~= nil
    local topology_name = is_external
        and "StateMachine"
        or "StateMachine_" .. machine.Name
    local topology_function = owner[topology_name]
    assert(type(topology_function) == "function", string.format(
        "StateMachine '%s' requires function '%s'",
        machine.Name,
        topology_name))
    topology_function(machine)

    for _, state in ipairs(machine.OwnedGraph.States) do
        local state_graph_name = is_external
            and "StateGraph_" .. state.Name
            or string.format("StateGraph_%s_%s", machine.Name, state.Name)
        local state_graph_function = owner[state_graph_name]
        assert(type(state_graph_function) == "function", string.format(
            "State '%s.%s' requires function '%s'",
            machine.Name,
            state.Name,
            state_graph_name))
        state_graph_function(state.PoseGraph)
    end
end

---由基类创建默认 Main Layer 和 AnimGraph，再把 Graph 交给子类像动画蓝图函数一样填写。
---@return nil result 该函数只执行固定编译流程，业务动画蓝图不得 override。
function LuaAnimBlueprint:BuildDeclaredAnimGraph()
    if self.BlueprintKind == BlueprintKind.AnimBlueprint then
        assert(type(self.AnimGraph) == "function", string.format(
            "%s must override AnimGraph",
            self.ClassName or "LuaAnimBlueprint"))
        local layer = self:AnimationLayer("Main", {
            FunctionName = "AnimGraph",
        })
        local graph = layer:PoseGraph("AnimGraph")
        self:AnimGraph(graph)
        layer:SetRootGraph(graph)
    else
        assert(
            self.BlueprintKind == BlueprintKind.AnimationLayerInterface,
            string.format(
                "Unsupported AnimBlueprint kind '%s'",
                tostring(self.BlueprintKind)))
    end

    if type(self.DeclareAnimationLayers) == "function" then
        self:DeclareAnimationLayers()
    end
end

---构建子类声明并导出规范 IR 表；该函数只在编辑器编译期调用。
---@return SekiroAnimBlueprintIR blueprint_ir 与 FSekiroAnimBlueprintIR 对应的纯 Lua 表。
function LuaAnimBlueprint:CompileIR()
    local target_skeleton = self.BlueprintKind == BlueprintKind.AnimBlueprint
        and IRSchema.RequireAssetObjectPath(
            self.TargetSkeleton,
            "TargetSkeleton")
        or ""
    self:BuildDeclaredAnimGraph()

    ---@type SekiroAnimIRLayer[]
    local layers = {}
    for _, layer in ipairs(self.Layers) do
        table.insert(layers, layer:ToIR())
    end

    return {
        SchemaVersion = self.SchemaVersion,
        BlueprintKind = self.BlueprintKind,
        SourceModule = self.SourceModule,
        ParentAnimInstanceClass = self.ParentAnimInstanceClass,
        TargetSkeleton = target_skeleton,
        InheritedDefaults = compile_inherited_defaults(
            self.InheritedDefaults),
        ImplementedInterfaces = self.ImplementedInterfaces,
        Variables = self.Variables,
        Layers = layers,
        SourceLocation = self.SourceLocation,
    }
end

---导出供 C++ require 的模块表；CompileIR 闭包每次都从干净实例重新构建并返回 IR。
---@return LuaAnimBlueprintExport exported_module 包含无参 CompileIR 函数的模块表。
function LuaAnimBlueprint:Export()
    local class = self
    ---@type LuaAnimBlueprintExport
    local exported_module = setmetatable({}, { __index = class })

    ---创建独立编译实例，避免热重载或重复编译残留上一次的 Graph 与变量声明。
    ---@return SekiroAnimBlueprintIR blueprint_ir 与 FSekiroAnimBlueprintIR 对应的纯 Lua 表。
    function exported_module.CompileIR()
        ---@type LuaAnimBlueprint
        local instance = class:New()
        return instance:CompileIR()
    end

    ---从已导出的 Lua 动画蓝图继续派生编译器类；该继承只复用 Lua 声明，UE 资产父类仍由 ParentAnimInstanceClass 决定。
    ---@param class_name string 子类诊断名称。
    ---@param definition table<string, any>|nil 子类字段与 override 方法。
    ---@return LuaAnimBlueprint child 可继续调用 Export 的动画蓝图编译类。
    function exported_module:Extend(class_name, definition)
        return class:Extend(class_name, definition)
    end

    return exported_module
end

return LuaAnimBlueprint
