-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Lua 动画 IR 的 Graph 所有权作用域。
-- Main Layer 管理主 AnimGraph；其他 Layer 对应 UE 原生 Animation Layer Function Graph。
-- Layer 管理 Pose Graph 和节点拥有的内部 Graph；唯一根 Graph 必须是 Pose Graph。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LuaAnimGraph = require("Animation.Compiler.LuaAnimGraph")

---@alias LuaCompilerGraph LuaAnimGraph|LuaAnimStateGraph|LuaAnimStateMachineGraph

---@class LuaAnimLayerConfig
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Name string Layer 语义名称。
---@field FunctionName string|nil 对应 UE Animation Layer UFunction 名；为空时回退 Name。
---@field InterfaceClass string|nil 声明该函数签名的 Animation Layer Interface 类软路径。
---@field bOverride boolean|nil 是否覆盖接口或父 AnimBlueprint 中的同名 Layer。
---@field Parameters SekiroAnimIRFunctionParameter[]|nil 函数输入参数签名。
---@field DeclarationOrder number|nil 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation|nil Layer 源码位置。

---@class LuaAnimLayer: CompilerClass Graph 所有权作用域；Main 表示主 AnimGraph，其余实例表示 UE Animation Layer 函数。
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Name string Layer 语义名称。
---@field Id string Layer 稳定 ID。
---@field FunctionName string 对应 UE Animation Layer UFunction 名。
---@field InterfaceClass string 可选 Animation Layer Interface 类软路径。
---@field bOverride boolean 是否覆盖接口或父 AnimBlueprint 中的同名 Layer。
---@field Parameters SekiroAnimIRFunctionParameter[] 函数输入参数签名。
---@field SourceLocation SekiroAnimIRSourceLocation Layer 源码位置。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field Graphs LuaCompilerGraph[] Layer 拥有的 Graph。
---@field GraphIds table<string, LuaCompilerGraph> 按稳定 ID 索引的全部 Graph。
---@field GraphNames table<string, LuaAnimGraph> 按语义名称索引的 Layer 级主 Pose Graph。
---@field RootGraphId string 最终输出 Pose 的根 Graph ID。
local LuaAnimLayer = CompilerClass:Extend("LuaAnimLayer")

---初始化动画层身份和 Graph 注册表。
---@param config LuaAnimLayerConfig 所属 Blueprint、Layer 名称和源码位置。
---@return nil result 该函数只初始化动画层声明，不返回业务值。
function LuaAnimLayer:Initialize(config)
    self.Blueprint = assert(config.Blueprint, "LuaAnimLayer requires Blueprint")
    self.Name = IRSchema.RequireSemanticName(config.Name, "Layer")
    self.Id = IRSchema.MakeStableId("", "Layer", self.Name)
    self.FunctionName = IRSchema.RequireSemanticName(
        config.FunctionName or self.Name,
        "Animation Layer Function")
    self.InterfaceClass = config.InterfaceClass ~= nil
        and IRSchema.RequireClassObjectPath(
            config.InterfaceClass,
            "Animation Layer Interface")
        or ""
    self.bOverride = config.bOverride == true
    self.Parameters = config.Parameters or {}
    self.SourceLocation = config.SourceLocation
        or IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3)
    self.DeclarationOrder = config.DeclarationOrder or 0
    self.Graphs = {}
    self.GraphIds = {}
    self.GraphNames = {}
    self.RootGraphId = ""
end

---创建 Animation Layer 的函数 Pose Graph，并把签名参数映射为可连接的类型化输出 Pin。
---每个 Pose 参数生成一个 Linked Input Pose；普通参数挂在第一个 Pose 输入节点上，行为与 UE 编译器一致。
---@param build_function (fun(graph: LuaAnimGraph, inputs: table<string, LuaAnimPin>):nil)|nil Layer 实现回调；接口声明可为空。
---@return LuaAnimGraph graph 当前 Layer 的函数 Graph。
---@return table<string, LuaAnimPin> inputs 按参数名索引的 Pose 与普通参数输出 Pin。
function LuaAnimLayer:FunctionGraph(build_function)
    local graph = self:PoseGraph(self.FunctionName)
    local pose_parameters = {}
    local scalar_parameters = {}
    for _, parameter in ipairs(self.Parameters) do
        if parameter.bIsPose == true then
            table.insert(pose_parameters, parameter)
        else
            table.insert(scalar_parameters, parameter)
        end
    end
    assert(
        #scalar_parameters == 0 or #pose_parameters > 0,
        string.format(
            "Animation Layer '%s' declares value parameters but no Pose input",
            self.FunctionName))

    local inputs = {}
    for pose_index, pose_parameter in ipairs(pose_parameters) do
        local node = graph:LinkedInputPose(
            "Input_" .. pose_parameter.Name,
            pose_parameter,
            pose_index == 1 and scalar_parameters or {})
        inputs[pose_parameter.Name] = node[pose_parameter.Name]
        if pose_index == 1 then
            for _, scalar_parameter in ipairs(scalar_parameters) do
                inputs[scalar_parameter.Name] = node[scalar_parameter.Name]
            end
        end
    end

    if build_function ~= nil then
        build_function(graph, inputs)
    end
    self:SetRootGraph(graph)
    return graph, inputs
end

---根据语义名称计算本层 Graph 的稳定 ID，不要求 Graph 已经声明。
---@param graph_name string Pose Graph 或 StateMachine Graph 的语义名。
---@return string graph_id 本 Layer 作用域内的稳定 Graph ID。
function LuaAnimLayer:GetGraphId(graph_name)
    local valid_name = IRSchema.RequireSemanticName(graph_name, "Graph")
    return IRSchema.MakeStableId(self.Id, "Graph", valid_name)
end

---登记主 Graph 或节点/状态拥有的内部 Graph，并确保稳定 ID 在本层唯一。
---@param graph LuaCompilerGraph LuaAnimGraph 或节点拥有的内部 Graph 实例。
---@return LuaCompilerGraph graph 原样返回已登记 Graph。
function LuaAnimLayer:AddGraph(graph)
    assert(graph ~= nil and type(graph.ToIR) == "function", "Layer:AddGraph requires a compiler Graph")
    assert(self.GraphIds[graph.Id] == nil, string.format("Layer '%s' contains duplicate Graph ID '%s'", self.Name, graph.Id))
    graph.DeclarationOrder = #self.Graphs
    self.GraphIds[graph.Id] = graph
    table.insert(self.Graphs, graph)
    return graph
end

---声明一个 Pose Graph；调用方可以在返回后像动画蓝图函数一样直接放置节点和连接 Pin。
---@param name string 本 Layer 内的 Graph 语义名。
---@param build_function (fun(graph: LuaAnimGraph):nil)|nil 旧式即时配置回调；新动画蓝图无需传入。
---@return LuaAnimGraph graph 已完成声明的 Pose Graph。
function LuaAnimLayer:PoseGraph(name, build_function)
    local valid_name = IRSchema.RequireSemanticName(name, "Graph")
    assert(self.GraphNames[valid_name] == nil, string.format(
        "Layer '%s' contains duplicate named Pose Graph '%s'",
        self.Name,
        valid_name))

    ---@type LuaAnimGraph
    local graph = LuaAnimGraph:New({
        Blueprint = self.Blueprint,
        Layer = self,
        Name = valid_name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self.GraphNames[valid_name] = graph
    self:AddGraph(graph)
    if build_function ~= nil then
        build_function(graph)
    end
    return graph
end

---显式设置本层根 Pose Graph，名称错误或类型错误由最终 Validator 产生引用诊断。
---@param graph_or_name LuaAnimGraph|string Pose Graph 实例或本层 Graph 语义名。
---@return nil result 该函数只更新根 Graph 引用，不返回业务值。
function LuaAnimLayer:SetRootGraph(graph_or_name)
    if type(graph_or_name) == "table" then
        self.RootGraphId = graph_or_name.Id
        return
    end

    self.RootGraphId = self:GetGraphId(graph_or_name)
end

---导出与 FSekiroAnimIRLayer 字段一致的纯 Lua 表。
---@return SekiroAnimIRLayer ir_layer 可交给 C++ 导入器的动画层声明。
function LuaAnimLayer:ToIR()
    ---@type SekiroAnimIRGraph[]
    local graphs = {}
    for _, graph in ipairs(self.Graphs) do
        table.insert(graphs, graph:ToIR())
    end

    return {
        Id = self.Id,
        Name = self.Name,
        FunctionName = self.FunctionName,
        InterfaceClass = self.InterfaceClass,
        bOverride = self.bOverride,
        Parameters = self.Parameters,
        RootGraphId = self.RootGraphId,
        Graphs = graphs,
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimLayer
