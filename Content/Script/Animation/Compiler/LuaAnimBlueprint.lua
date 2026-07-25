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

    return exported_module
end

return LuaAnimBlueprint
