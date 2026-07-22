-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Lua 动画蓝图的编译期基类。
-- 子类只把 AnimGraph 当作动画蓝图函数编写；Layer、Graph 分配、状态子图构建与 IR 导出由基类完成。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LuaAnimLayer = require("Animation.Compiler.LuaAnimLayer")
local IRValue = require("Animation.Compiler.IRValue")

---@class LuaAnimBlueprintConfig

---@class LuaAnimBlueprintExport: LuaAnimBlueprint
---@field CompileIR fun(): SekiroAnimBlueprintIR 创建干净编译实例并导出 IR。

---@class LuaAnimBlueprint: CompilerClass
---@field SchemaVersion number IR Schema 版本整数。
---@field SourceModule string 动画蓝图 Lua 模块名。
---@field ParentAnimInstanceClass string 父 AnimInstance 类软路径。
---@field TargetSkeleton string 普通 AnimBlueprint 必填的目标 Skeleton 资产软路径。
---@field Layers LuaAnimLayer[] 本次编译声明的 Graph 所有权作用域；当前 Factory 仅支持一个 Main Layer。
---@field LayerNames table<string, LuaAnimLayer> 按语义名称索引的 Graph 所有权作用域。
---@field RuntimeFunctions table<string, function> 兼容旧模块时导出的运行时 Transition 函数；纯原生 Rule 不登记。
---@field SourceLocation SekiroAnimIRSourceLocation 动画蓝图源码位置。
---@field AnimGraph fun(self: LuaAnimBlueprint, graph: LuaAnimGraph):nil 子类必须 override 的主动画图函数。
local LuaAnimBlueprint = CompilerClass:Extend("LuaAnimBlueprint", {
    SchemaVersion = 2,
    SourceModule = "",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "",
})

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

---把 Blueprint Enum 默认值转换为底层整数 IR Value。
---@param value number Lua 动画蓝图声明的枚举整数值。
---@return SekiroAnimIRValue ir_value 可由 C++ Importer 结合 UEnum 路径解释的 Integer 值。
local function make_enum_default(value)
    return IRValue.Integer(value)
end

---初始化单次编译实例；每次 CompileIR 都创建新实例以避免热重载残留声明。
---@param _config LuaAnimBlueprintConfig 创建实例时的字段覆盖；基类当前只读取类默认字段。
---@return nil result 该函数只初始化本次编译实例，不返回业务值。
function LuaAnimBlueprint:Initialize(_config)
    self.Layers = {}
    self.LayerNames = {}
    self.RuntimeFunctions = {}
    self.Variables = {}
    self.VariableNames = {}
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

---声明一个 IR Graph 所有权作用域；当前不生成 UE Animation Layer，Factory 仅接受默认 Main Layer。
---@param name string AnimBlueprint IR 内的 Layer 语义名。
---@param build_function (fun(layer: LuaAnimLayer):nil)|nil 旧式即时配置回调；新动画蓝图无需传入。
---@return LuaAnimLayer layer 已完成声明的动画层。
function LuaAnimBlueprint:AnimationLayer(name, build_function)
    local valid_name = IRSchema.RequireSemanticName(name, "Layer")
    assert(self.LayerNames[valid_name] == nil, string.format("AnimBlueprint contains duplicate Layer '%s'", valid_name))

    ---@type LuaAnimLayer
    local layer = LuaAnimLayer:New({
        Blueprint = self,
        Name = valid_name,
        DeclarationOrder = #self.Layers,
        SourceLocation = IRSchema.CaptureSourceLocation(self.SourceModule, 3),
    })
    self.LayerNames[valid_name] = layer
    table.insert(self.Layers, layer)
    if build_function ~= nil then
        build_function(layer)
    end
    return layer
end

---登记兼容旧状态机文件提供的运行时函数；纯原生 Rule 不调用本入口。
---@param function_name string IR 中引用的完整 CanEnter_<状态机>_<过渡键> 函数名。
---@param runtime_function function 运行时以真实 UAnimInstance 代理作为显式 Inst 参数调用的规则函数。
---@return nil result 该函数只登记本次编译需要导出的函数。
function LuaAnimBlueprint:RegisterRuntimeFunction(function_name, runtime_function)
    local existing = self.RuntimeFunctions[function_name]
    assert(existing == nil or existing == runtime_function, string.format(
        "Runtime function '%s' is registered by more than one StateMachine",
        function_name))
    self.RuntimeFunctions[function_name] = runtime_function
end

---根据内联约定或独立状态机类，构建 StateMachine 拓扑、每个 State Pose Graph 和可选旧式运行时规则映射。
---强类型 Rule 不登记运行时函数；旧式独立文件使用 CanEnter_<Key>，内联写法增加节点名前缀。
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

    for _, transition in ipairs(machine.OwnedGraph.Transitions) do
        if transition.RuleFunctionName ~= "" then
            local local_rule_name = is_external
                and "CanEnter_" .. transition.Key
                or transition.RuleFunctionName
            local runtime_function = owner[local_rule_name]
            assert(type(runtime_function) == "function", string.format(
                "Transition '%s.%s' requires function '%s'",
                machine.Name,
                transition.Key,
                local_rule_name))
            self:RegisterRuntimeFunction(transition.RuleFunctionName, runtime_function)
        end
    end
end

---由基类创建默认 Main Layer 和 AnimGraph，再把 Graph 交给子类像动画蓝图函数一样填写。
---@return nil result 该函数只执行固定编译流程，业务动画蓝图不得 override。
function LuaAnimBlueprint:BuildDeclaredAnimGraph()
    assert(type(self.AnimGraph) == "function", string.format(
        "%s must override AnimGraph",
        self.ClassName or "LuaAnimBlueprint"))
    local layer = self:AnimationLayer("Main")
    local graph = layer:PoseGraph("AnimGraph")
    self:AnimGraph(graph)
    layer:SetRootGraph(graph)
end

---构建子类声明并导出规范 IR 表；该函数只在编辑器编译期调用。
---@return SekiroAnimBlueprintIR blueprint_ir 与 FSekiroAnimBlueprintIR 对应的纯 Lua 表。
function LuaAnimBlueprint:CompileIR()
    local target_skeleton = IRSchema.RequireAssetObjectPath(self.TargetSkeleton, "TargetSkeleton")
    self:BuildDeclaredAnimGraph()

    ---@type SekiroAnimIRLayer[]
    local layers = {}
    for _, layer in ipairs(self.Layers) do
        table.insert(layers, layer:ToIR())
    end

    return {
        SchemaVersion = self.SchemaVersion,
        SourceModule = self.SourceModule,
        ParentAnimInstanceClass = self.ParentAnimInstanceClass,
        TargetSkeleton = target_skeleton,
        Variables = self.Variables,
        Layers = layers,
        SourceLocation = self.SourceLocation,
    }
end

---导出供 C++ require 的模块表；CompileIR 闭包每次都从干净实例重新构建。
---@return LuaAnimBlueprintExport exported_module 包含无参 CompileIR 函数的模块表。
function LuaAnimBlueprint:Export()
    local class = self
    ---@type LuaAnimBlueprintExport
    local exported_module = setmetatable({}, { __index = class })

    local exported_runtime_function_names = {}

    ---创建独立编译实例，并把状态机文件中的规则函数同步到当前导出模块。
    ---@return SekiroAnimBlueprintIR blueprint_ir 与 FSekiroAnimBlueprintIR 对应的纯 Lua 表。
    function exported_module.CompileIR()
        for function_name in pairs(exported_runtime_function_names) do
            rawset(exported_module, function_name, nil)
        end
        exported_runtime_function_names = {}

        ---@type LuaAnimBlueprint
        local instance = class:New()
        local blueprint_ir = instance:CompileIR()
        for function_name, runtime_function in pairs(instance.RuntimeFunctions) do
            rawset(exported_module, function_name, runtime_function)
            exported_runtime_function_names[function_name] = true
        end
        return blueprint_ir
    end

    -- 运行时只会 require 主模块，不保证此前在同一 Lua 环境执行过编辑器 Check/Generate。
    -- 这里预构建一次纯 Lua 声明以发布嵌套状态机的 CanEnter_* 函数；生成资产仍只由编辑器 C++ 工厂完成。
    exported_module.CompileIR()

    return exported_module
end

return LuaAnimBlueprint
