-- Pose Graph 的 Lua 编译期类。
-- Graph 持有节点与 Link，并自动创建唯一 OutputPose 根节点。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")
local LuaInertializationNode = require("Animation.Compiler.LuaInertializationNode")
local LuaSaveCachedPoseNode = require("Animation.Compiler.LuaSaveCachedPoseNode")
local LuaSequencePlayerNode = require("Animation.Compiler.LuaSequencePlayerNode")
local LuaUseCachedPoseNode = require("Animation.Compiler.LuaUseCachedPoseNode")
local LuaPropertyGetterNode = require("Animation.Compiler.LuaPropertyGetterNode")
local LuaBlendListByBoolNode = require("Animation.Compiler.LuaBlendListByBoolNode")
local LuaBlendListByEnumNode = require("Animation.Compiler.LuaBlendListByEnumNode")
local NodeContracts = require("Animation.Compiler.NodeContracts")

---@class LuaAnimGraphConfig
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Layer LuaAnimLayer 所属动画层。
---@field Name string Graph 语义名称。
---@field Id string|nil 由所有者提供的稳定 Graph ID；主 Graph 默认由 Layer 与名称生成。
---@field GraphType string|nil Graph 注册类型；主 Graph 默认使用 Pose。
---@field RootNodeName string|nil 固定根节点语义名；主 Graph 默认使用 Output。
---@field RootNodeType string|nil 固定根节点注册类型；主 Graph 默认使用 OutputPose。
---@field RootNodeDisplayName string|nil 固定根节点编辑器名称。
---@field SourceLocation SekiroAnimIRSourceLocation|nil Graph 源码位置。
---@field DeclarationOrder number|nil 源码中的确定性声明顺序整数。

---@class LuaAnimGraph: CompilerClass
---@field Blueprint LuaAnimBlueprint 所属动画蓝图编译实例。
---@field Layer LuaAnimLayer 所属动画层。
---@field Name string Graph 语义名称。
---@field Id string Graph 稳定 ID。
---@field GraphType string Graph 注册类型；主 Graph 为 Pose，State Graph 为 StatePose。
---@field SourceLocation SekiroAnimIRSourceLocation Graph 源码位置。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field Nodes LuaAnimNode[] Graph 中的编译期节点。
---@field Links SekiroAnimIRLink[] Graph 中的 Pose Link。
---@field NodeNames table<string, LuaAnimNode> 按语义名称索引的节点。
---@field OutputNode LuaAnimNode 固定的 OutputPose 或 StateResult 根节点。
---@field Result LuaAnimPin 固定根节点的 Pose 输入 Pin，业务 Graph 把最终姿势连接到这里。
---@field RootNodeId string 固定结果节点稳定 ID。
local LuaAnimGraph = CompilerClass:Extend("LuaAnimGraph")

---初始化可输出 Pose 的 Graph，并创建与 Graph 类型匹配的固定结果节点。
---@param config LuaAnimGraphConfig 所属 Blueprint、Layer、Graph 名称和源码位置。
---@return nil result 该函数只初始化 Graph 和根节点，不返回业务值。
function LuaAnimGraph:Initialize(config)
    self.Blueprint = assert(config.Blueprint, "LuaAnimGraph requires Blueprint")
    self.Layer = assert(config.Layer, "LuaAnimGraph requires Layer")
    self.Name = IRSchema.RequireSemanticName(config.Name, "Graph")
    self.Id = config.Id or IRSchema.MakeStableId(self.Layer.Id, "Graph", self.Name)
    self.GraphType = config.GraphType or "Pose"
    self.SourceLocation = config.SourceLocation
        or IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3)
    self.DeclarationOrder = config.DeclarationOrder or 0
    self.Nodes = {}
    self.Links = {}
    self.NodeNames = {}

    ---@type LuaAnimNode
    local output_node = LuaAnimNode:New({
        Graph = self,
        Name = config.RootNodeName or "Output",
        NodeType = config.RootNodeType or "OutputPose",
        DisplayName = config.RootNodeDisplayName or "Output Pose",
        bIsGraphRoot = true,
        SourceLocation = self.SourceLocation,
    })
    self.OutputNode = output_node
    self:AddNode(output_node)
    self.RootNodeId = output_node.Id
    self.Result = output_node.Result
end

---登记一个节点对象并分配确定性的声明顺序。
---@param node LuaAnimNode LuaAnimNode 或其子类实例。
---@return LuaAnimNode node 原样返回已登记节点，便于声明链继续使用。
function LuaAnimGraph:AddNode(node)
    assert(node ~= nil and type(node.ToIR) == "function", "Graph:AddNode requires LuaAnimNode")
    assert(self.NodeNames[node.Name] == nil, string.format("Graph '%s' contains duplicate Node '%s'", self.Name, node.Name))
    node.DeclarationOrder = #self.Nodes
    self.NodeNames[node.Name] = node
    table.insert(self.Nodes, node)
    rawset(node, "bIsSealed", true)
    return node
end

---创建并登记一个原生 SequencePlayer 节点；资产和播放器参数由调用方直接写入返回对象。
---@param name string Graph 内的节点语义名。
---@return LuaSequencePlayerNode node 新建的空 SequencePlayer 节点。
function LuaAnimGraph:SequencePlayer(name)
    ---@type LuaSequencePlayerNode
    local node = LuaSequencePlayerNode:New({
        Graph = self,
        Name = name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    return node
end

---创建 AnimInstance 生成变量的原生 Getter，供数据 Pin 直接连接选择节点。
---@param name string Getter 节点语义名。
---@param variable_name string 已由 AnimBlueprint:Variable 声明的成员名。
---@return LuaPropertyGetterNode node 具有 Value 输出 Pin 的 Getter。
function LuaAnimGraph:Property(name, variable_name)
    local variable = assert(self.Blueprint.VariableNames[variable_name], string.format("Unknown AnimBlueprint variable '%s'", tostring(variable_name)))
    local node = LuaPropertyGetterNode:New({
        Graph = self,
        Name = name,
        Variable = variable,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    return node
end

---创建原生 BlendListByBool；Pose 与 Bool 输入均通过具名 Pin 连接。
---@param name string Graph 内节点语义名。
---@return LuaBlendListByBoolNode node Bool Pose 选择节点。
function LuaAnimGraph:BlendListByBool(name)
    local node = LuaBlendListByBoolNode:New({ Graph = self, Name = name, SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3) })
    self:AddNode(node)
    return node
end

---创建原生 BlendListByEnum；最多显式暴露八个枚举项 Pose Pin。
---@param name string Graph 内节点语义名。
---@param enum_type string UEnum 对象路径。
---@param enum_entries string[] 枚举项短名，顺序对应 Pose0..Pose7。
---@return LuaBlendListByEnumNode node Enum Pose 选择节点。
function LuaAnimGraph:BlendListByEnum(name, enum_type, enum_entries)
    local node = LuaBlendListByEnumNode:New({
        Graph = self,
        Name = name,
        EnumType = enum_type,
        EnumEntries = enum_entries,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    return node
end

---创建并登记一个已注册的通用原生 AnimNode；业务代码通常使用具体节点构造函数。
---@param name string Graph 内的节点语义名。
---@param node_type string C++ NodeFactory 注册的稳定 NodeType。
---@return LuaAnimNode node 新建并登记的编译期节点。
function LuaAnimGraph:CreateNode(name, node_type)
    ---@type LuaAnimNode
    local node = LuaAnimNode:New({
        Graph = self,
        Name = name,
        NodeType = node_type,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    return node
end

---创建原生 Inertialization 节点，用于消费请求并平滑连接输入姿势。
---@param name string Graph 内的节点语义名。
---@return LuaInertializationNode node 提供 Source 输入和 Pose 输出的节点。
function LuaAnimGraph:Inertialization(name)
    ---@type LuaInertializationNode
    local node = LuaInertializationNode:New({
        Graph = self,
        Name = name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    return node
end

---创建原生 Save Cached Pose 节点；缓存名默认与节点语义名相同。
---UE 只允许该节点放在主 Pose Graph，状态内部应使用 Use Cached Pose 读取已有缓存。
---@param name string Graph 和生成动画蓝图中的缓存姿势名。
---@return LuaSaveCachedPoseNode node 提供 Pose 输入的 Save Cached Pose 节点。
function LuaAnimGraph:SaveCachedPose(name)
    ---@type LuaSaveCachedPoseNode
    local node = LuaSaveCachedPoseNode:New({
        Graph = self,
        Name = name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    node.CacheName = name
    return node
end

---创建原生 Use Cached Pose 节点并绑定当前 Pose Graph 中已声明的 Save Cached Pose 节点。
---@param name string 当前 Use Cached Pose 节点的语义名。
---@param saved_pose LuaSaveCachedPoseNode 目标 Save Cached Pose 节点，必须属于当前 Pose Graph。
---@return LuaUseCachedPoseNode node 提供 Pose 输出的 Use Cached Pose 节点。
function LuaAnimGraph:UseCachedPose(name, saved_pose)
    assert(saved_pose ~= nil and saved_pose.NodeType == "SaveCachedPose", "UseCachedPose requires a SaveCachedPose Node")
    assert(saved_pose.Graph == self, "UseCachedPose target must belong to the same Pose Graph")
    ---@type LuaUseCachedPoseNode
    local node = LuaUseCachedPoseNode:New({
        Graph = self,
        Name = name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    node.CacheName = saved_pose.Name
    return node
end

---创建并登记一个拥有内部 StateMachine Graph 的原生状态机节点。
---@param name string Pose Graph 内的节点语义名。
---@param definition LuaAnimStateMachine|nil 独立状态机描述类；为空时按当前 AnimBlueprint 的 StateMachine_<节点名> 约定查找。
---@return LuaStateMachineNode node 新建的 StateMachine 节点。
function LuaAnimGraph:StateMachine(name, definition)
    local LuaStateMachineNode = require("Animation.Compiler.LuaStateMachineNode")
    local valid_name = IRSchema.RequireLuaIdentifier(name, "StateMachine Node")
    assert(self.NodeNames[valid_name] == nil, string.format(
        "Graph '%s' contains duplicate Node '%s'",
        self.Name,
        valid_name))

    ---@type LuaStateMachineNode
    local node = LuaStateMachineNode:New({
        Graph = self,
        Name = valid_name,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    self:AddNode(node)
    self.Blueprint:ConfigureStateMachine(node, definition)
    return node
end

---连接两个类型化 Pin，并在进入底层 Link API 前验证 Graph、方向和数据类型。
---@param source_pin LuaAnimPin 提供数据的输出 Pin。
---@param target_pin LuaAnimPin 接收数据的输入 Pin。
---@return SekiroAnimIRLink link 新建的稳定 Link IR。
function LuaAnimGraph:LinkPins(source_pin, target_pin)
    assert(source_pin.Graph == self and target_pin.Graph == self, "Connected Pins must belong to the same Graph")
    assert(source_pin.DataType == target_pin.DataType, string.format(
        "Cannot connect data type '%s' to '%s'",
        source_pin.DataType,
        target_pin.DataType))
    return self:Link(source_pin.Node, source_pin.Name, target_pin.Node, target_pin.Name)
end

---连接两个节点的具名 Pin，并根据端点语义自动生成稳定 Link ID。
---@param source_node LuaAnimNode 提供输出 Pin 的节点。
---@param source_pin string 源节点输出 Pin 名称。
---@param target_node LuaAnimNode 接收输入 Pin 的节点。
---@param target_pin string 目标节点输入 Pin 名称。
---@return SekiroAnimIRLink link 新建的 Link IR 表。
function LuaAnimGraph:Link(source_node, source_pin, target_node, target_pin)
    assert(source_node ~= nil and target_node ~= nil, "Graph:Link requires source and target Nodes")
    local source_contract = NodeContracts.RequirePin(source_node.Contract, source_pin, "Output")
    local target_contract = NodeContracts.RequirePin(target_node.Contract, target_pin, "Input")
    for _, existing_link in ipairs(self.Links) do
        local source_is_reused = existing_link.Source.NodeId == source_node.Id
            and existing_link.Source.PinName == source_pin
        local target_is_reused = existing_link.Target.NodeId == target_node.Id
            and existing_link.Target.PinName == target_pin
        assert(source_contract.bAllowMultipleConnections == true or not source_is_reused, string.format(
            "Output Pin '%s.%s' does not allow multiple connections",
            source_node.Name,
            source_pin))
        assert(target_contract.bAllowMultipleConnections == true or not target_is_reused, string.format(
            "Input Pin '%s.%s' does not allow multiple connections",
            target_node.Name,
            target_pin))
    end
    local link_name = string.format("%s.%s->%s.%s", source_node.Name, source_pin, target_node.Name, target_pin)
    ---@type SekiroAnimIRLink
    local link = {
        Id = IRSchema.MakeStableId(self.Id, "Link", link_name),
        Source = {
            NodeId = source_node.Id,
            PinName = source_pin,
        },
        Target = {
            NodeId = target_node.Id,
            PinName = target_pin,
        },
        DeclarationOrder = #self.Links,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    }
    table.insert(self.Links, link)
    return link
end

---把指定节点的 Pose 输出连接到 Graph 的 OutputPose 根节点。
---@param source_node LuaAnimNode 最终输出 Pose 的节点。
---@param source_pin string|nil Pose 输出 Pin 名，默认 Pose。
---@return SekiroAnimIRLink link 新建的根输出 Link。
function LuaAnimGraph:Output(source_node, source_pin)
    return self:Link(source_node, source_pin or "Pose", self.OutputNode, "Result")
end

---导出与 FSekiroAnimIRGraph 对应的 Pose Graph 表。
---@return SekiroAnimIRGraph ir_graph 可由 C++ 导入器解析的 Graph 声明。
function LuaAnimGraph:ToIR()
    ---@type SekiroAnimIRNode[]
    local nodes = {}
    for _, node in ipairs(self.Nodes) do
        table.insert(nodes, node:ToIR())
    end

    return {
        Id = self.Id,
        Name = self.Name,
        GraphType = self.GraphType,
        RootNodeId = self.RootNodeId,
        Nodes = nodes,
        Links = self.Links,
        StateMachine = {
            EntryStateId = "",
            States = {},
            Transitions = {},
        },
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimGraph
