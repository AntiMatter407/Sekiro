-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Pose Graph 的 Lua 编译期类。
-- Graph 持有节点与 Link，并自动创建唯一 OutputPose 根节点。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local LayoutStyle = require("Animation.Compiler.LayoutStyle")
local LuaGraphLayoutGrid = require("Animation.Compiler.LuaGraphLayoutGrid")
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")
local NodeContracts = require("Animation.Compiler.NodeContracts")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")

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
---@field LayoutPositions SekiroAnimIRLayoutPosition[] 精确像素坐标声明。
---@field LayoutPositionIds table<string, boolean> 已声明精确坐标的元素 ID 集合。
local LuaAnimGraph = CompilerClass:Extend("LuaAnimGraph")

---@class LuaLinkedAnimLayerConfig
---@field LayerName string 目标 Animation Layer 函数名。
---@field InstanceClass string|nil 可选的实际 Linked Anim Layer AnimInstance 类软路径；为空时使用 Self。
---@field InterfaceClass string|nil 提供 Layer 签名的 Animation Layer Interface 类软路径。
---@field Parameters SekiroAnimIRFunctionParameter[]|nil 目标函数输入签名；用于生成可连接的动态输入 Pin。

---@class LuaLinkedAnimGraphConfig
---@field InstanceClass string 目标 AnimBlueprint GeneratedClass 软路径。
---@field GraphName string|nil 目标动画图函数名；省略时使用 AnimGraph。
---@field Parameters SekiroAnimIRFunctionParameter[]|nil 目标函数输入签名；用于生成可连接的动态输入 Pin。

---创建一个函数签名动态 Pin，并补齐稳定声明顺序。
---@param name string 目标 UFunction 参数名或标准 Pose 输出名。
---@param direction SekiroAnimIRPinDirection 相对当前节点的数据流方向。
---@param data_type string IR 与 C++ 注册表共同识别的参数类型名。
---@param allow_multiple_connections boolean 是否允许同一 Pin 被多个 Link 使用。
---@param declaration_order number 当前节点内从零开始的 Pin 顺序整数。
---@return SekiroAnimIRPin pin 可直接写入节点 IR 的动态 Pin。
local function make_dynamic_pin(
    name,
    direction,
    data_type,
    allow_multiple_connections,
    declaration_order)
    return {
        Name = IRSchema.RequireSemanticName(name, "Dynamic Pin"),
        Direction = direction,
        DataType = IRSchema.RequireSemanticName(data_type, "Dynamic Pin Type"),
        bAllowMultipleConnections = allow_multiple_connections,
        DeclarationOrder = declaration_order,
    }
end

---按目标动画函数签名生成 Linked Anim Layer/Graph 的输入 Pin 和标准 Pose 输出。
---@param parameters SekiroAnimIRFunctionParameter[]|nil 目标函数的输入参数声明。
---@return SekiroAnimIRPin[] pins 完整动态 Pin 列表；最后一个 Pin 固定为 Pose 输出。
local function make_linked_function_pins(parameters)
    local pins = {}
    for _, parameter in ipairs(parameters or {}) do
        table.insert(pins, make_dynamic_pin(
            parameter.Name,
            "Input",
            parameter.DataType,
            false,
            #pins))
    end
    table.insert(pins, make_dynamic_pin(
        "Pose",
        "Output",
        "Pose",
        true,
        #pins))
    return pins
end

---按 Layer 签名生成单个 Linked Input Pose 节点的输出 Pin。
---普通参数只附着在第一个 Pose 输入节点，避免 UE 编译器重复收集同名函数参数。
---@param pose_parameter SekiroAnimIRFunctionParameter 当前 Pose 输入参数。
---@param scalar_parameters SekiroAnimIRFunctionParameter[] 由第一个 Pose 节点承载的普通参数；其他 Pose 节点传空表。
---@return SekiroAnimIRPin[] pins 当前 Linked Input Pose 的完整输出 Pin。
local function make_layer_input_pins(pose_parameter, scalar_parameters)
    local pins = {
        make_dynamic_pin(
            pose_parameter.Name,
            "Output",
            pose_parameter.DataType,
            true,
            0),
    }
    for _, parameter in ipairs(scalar_parameters) do
        table.insert(pins, make_dynamic_pin(
            parameter.Name,
            "Output",
            parameter.DataType,
            true,
            #pins))
    end
    return pins
end

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
    self.LayoutStyle = LayoutStyle.HierarchicalBlocks
    self.LayoutGrids = {}
    self.LayoutGridNames = {}
    self.LayoutElementIds = {}
    self.LayoutPositions = {}
    self.LayoutPositionIds = {}

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

---创建当前 Pose Graph 独占的布局分区；多个 Grid 通过 RegionColumn/RegionRow 分隔画布区域。
---@param name string Graph 内唯一的布局分区名。
---@param settings LuaGraphLayoutGridSettings|nil 分区区域、单元格间距和风格覆盖。
---@return LuaGraphLayoutGrid grid 可继续 Place 当前 Graph 节点的布局分区。
function LuaAnimGraph:Grid(name, settings)
    local valid_name = IRSchema.RequireSemanticName(name, "Layout Grid")
    assert(self.LayoutGridNames[valid_name] == nil, string.format(
        "Graph '%s' contains duplicate Layout Grid '%s'",
        self.Name,
        valid_name))
    local grid = LuaGraphLayoutGrid:New({
        Graph = self,
        Name = valid_name,
        Settings = settings,
    })
    self.LayoutGridNames[valid_name] = grid
    table.insert(self.LayoutGrids, grid)
    return grid
end

---登记一个节点对象并分配确定性的声明顺序。
---@param node LuaAnimNode LuaAnimNode 或其子类实例。
---@return LuaAnimNode node 原样返回已登记节点，便于声明链继续使用。
function LuaAnimGraph:AddNode(node)
    assert(node ~= nil and type(node.ToIR) == "function", "Graph:AddNode requires LuaAnimNode")
    assert(self.NodeNames[node.Name] == nil, string.format("Graph '%s' contains duplicate Node '%s'", self.Name, node.Name))
    -- 反射节点会把普通字段赋值解释为 UE 属性；声明顺序属于编译器内部元数据，必须绕过该拦截。
    rawset(node, "DeclarationOrder", #self.Nodes)
    self.NodeNames[node.Name] = node
    table.insert(self.Nodes, node)
    rawset(node, "bIsSealed", true)
    return node
end

---兼容旧业务入口并转发到统一反射节点构造。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 新建的空 SequencePlayer 节点。
function LuaAnimGraph:SequencePlayer(name)
    return self:Node(
        name,
        EditorNodeClass.SequencePlayer,
        nil,
        "SequencePlayer")
end

---把当前 Graph 的节点固定到 UE 画布精确像素坐标。
---精确坐标可与 Grid 声明并存且优先级更高；同一元素不允许重复设置精确坐标。
---@param element LuaAnimNode 当前 Pose 或 StatePose Graph 直接拥有的节点。
---@param x number UE Graph 画布横向像素整数坐标。
---@param y number UE Graph 画布纵向像素整数坐标。
---@return LuaAnimNode element 原样返回已定位节点，便于继续声明。
function LuaAnimGraph:SetPosition(element, x, y)
    assert(element ~= nil and type(element.Id) == "string", "Graph:SetPosition requires LuaAnimNode")
    assert(element.Graph == self, "Positioned node must belong to the same Graph")
    assert(self.LayoutPositionIds[element.Id] == nil, "Graph element may only have one exact position")
    local position_x = IRSchema.RequireLayoutCoordinate(x, "X")
    local position_y = IRSchema.RequireLayoutCoordinate(y, "Y")
    self.LayoutPositionIds[element.Id] = true
    table.insert(self.LayoutPositions, {
        ElementId = element.Id,
        X = position_x,
        Y = position_y,
    })
    return element
end

---创建 AnimInstance 生成变量的原生 Getter；变量类型决定复用的注册契约。
---@param name string Getter 节点语义名。
---@param variable_name string 已由 AnimBlueprint:Variable 声明的成员名。
---@return LuaAnimNode node 具有 Value 输出 Pin 的 Getter。
function LuaAnimGraph:Property(name, variable_name)
    local variable = assert(self.Blueprint.VariableNames[variable_name], string.format("Unknown AnimBlueprint variable '%s'", tostring(variable_name)))
    local node_types = {
        Bool = "BoolPropertyGetter",
        Float = "FloatPropertyGetter",
        Byte = "BytePropertyGetter",
        Enum = "EnumPropertyGetter",
    }
    return self:Node(
        name,
        EditorNodeClass.VariableGet,
        {
            PropertyName = variable.Name,
        },
        assert(node_types[variable.DataType], "Unsupported PropertyGetter variable type"))
end

---兼容旧业务入口并转发到统一反射节点构造。
---@param name string Graph 内节点语义名。
---@return LuaAnimNode node Bool Pose 选择节点。
function LuaAnimGraph:BlendListByBool(name)
    return self:Node(
        name,
        EditorNodeClass.BlendListByBool,
        nil,
        "BlendListByBool")
end

---兼容旧业务入口并转发到统一反射节点构造。
---@param name string Graph 内节点语义名。
---@param enum_type string UEnum 对象路径。
---@param enum_entries string[] 枚举项短名，顺序对应 Pose0..Pose7。
---@return LuaAnimNode node Enum Pose 选择节点。
function LuaAnimGraph:BlendListByEnum(name, enum_type, enum_entries)
    assert(type(enum_entries) == "table" and #enum_entries <= 8, "BlendListByEnum supports at most eight entries")
    return self:Node(
        name,
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = enum_type,
            EnumEntries = table.concat(enum_entries, "|"),
        },
        "BlendListByEnum")
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

---通过 UE 反射创建并登记原生 AnimGraph 节点。
---可选 NodeType 只复用已有 Pin 契约和结构型 C++ 适配器；未知类仍由 UE 原生 Schema 权威校验。
---@param name string Graph 内的节点语义名。
---@param editor_node_class string 原生编辑器节点类路径；项目内置节点统一从 EditorNodeClass 模块引用。
---@param properties table<string, boolean|number|string|SekiroAnimIRValue>|nil 反射属性路径和值；支持 Node.Yaw 或嵌套结构路径。
---@param node_type string|nil 已注册 NodeType；现有结构型节点传入该值，新增普通节点可省略。
---@return LuaAnimNode node 新建并登记的反射节点。
function LuaAnimGraph:Node(name, editor_node_class, properties, node_type)
    assert(
        type(editor_node_class) == "string" and editor_node_class ~= "",
        "Graph:Node requires a non-empty UAnimGraphNode class path")
    ---@type LuaAnimNode
    local node = LuaAnimNode:New({
        Graph = self,
        Name = name,
        NodeType = node_type or "Reflected",
        EditorNodeClass = editor_node_class,
        SourceLocation = IRSchema.CaptureSourceLocation(self.Blueprint.SourceModule, 3),
    })
    local property_names = {}
    for property_name in pairs(properties or {}) do
        table.insert(property_names, property_name)
    end
    table.sort(property_names)
    for _, property_name in ipairs(property_names) do
        local value = properties[property_name]
        node:AssignProperty(property_name, value)
    end
    self:AddNode(node)
    return node
end

---创建并登记一个由函数签名提供动态 Pin 的结构型原生 AnimGraph 节点。
---该入口只供编译器内部的 Layer/Linked Graph API 使用；业务 Lua 应调用具名方法而不是手工提供 Pins。
---@param name string Graph 内节点语义名。
---@param editor_node_class string 原生编辑器节点类路径。
---@param node_type string C++ NodeFactory 注册的结构型 NodeType。
---@param properties table<string, boolean|number|string|SekiroAnimIRValue>|nil 节点配置属性。
---@param dynamic_pins SekiroAnimIRPin[] 已根据函数签名生成的完整 Pin 断言。
---@return LuaAnimNode node 可通过具名 Pin 直接参与连接的节点。
function LuaAnimGraph:DynamicNode(
    name,
    editor_node_class,
    node_type,
    properties,
    dynamic_pins)
    assert(
        type(dynamic_pins) == "table" and #dynamic_pins > 0,
        "Graph:DynamicNode requires function signature Pins")
    ---@type LuaAnimNode
    local node = LuaAnimNode:New({
        Graph = self,
        Name = name,
        NodeType = node_type,
        EditorNodeClass = editor_node_class,
        DynamicPins = dynamic_pins,
        SourceLocation = IRSchema.CaptureSourceLocation(
            self.Blueprint.SourceModule,
            3),
    })
    local property_names = {}
    for property_name in pairs(properties or {}) do
        table.insert(property_names, property_name)
    end
    table.sort(property_names)
    for _, property_name in ipairs(property_names) do
        node:AssignProperty(property_name, properties[property_name])
    end
    self:AddNode(node)
    return node
end

---创建 Animation Layer Function Graph 的原生 Linked Input Pose 节点。
---调用方由 LuaAnimLayer 统一管理 Pose 索引和普通参数归属，业务动画脚本不直接调用。
---@param name string Graph 内节点语义名。
---@param pose_parameter SekiroAnimIRFunctionParameter 当前 Pose 输入参数。
---@param scalar_parameters SekiroAnimIRFunctionParameter[] 第一个 Pose 输入节点承载的普通参数。
---@return LuaAnimNode node 输出 Pose 参数及可选普通参数的函数输入节点。
function LuaAnimGraph:LinkedInputPose(
    name,
    pose_parameter,
    scalar_parameters)
    return self:DynamicNode(
        name,
        EditorNodeClass.LinkedInputPose,
        "LinkedInputPose",
        {
            PoseName = pose_parameter.Name,
        },
        make_layer_input_pins(
            pose_parameter,
            scalar_parameters or {}))
end

---创建调用 Animation Layer 函数的原生 Linked Anim Layer 节点。
---动态 Pin 必须与接口或目标 AnimInstance 上的 UFunction 签名一致，最终仍由 UE Schema 复核。
---@param name string Graph 内节点语义名。
---@param config LuaLinkedAnimLayerConfig Layer 名、接口/实例类与函数输入签名。
---@return LuaAnimNode node 提供签名输入 Pin 和标准 Pose 输出的 Linked Anim Layer 节点。
function LuaAnimGraph:LinkedAnimLayer(name, config)
    assert(type(config) == "table", "LinkedAnimLayer requires config")
    local properties = {
        LayerName = assert(config.LayerName, "LinkedAnimLayer requires LayerName"),
    }
    if config.InstanceClass ~= nil and config.InstanceClass ~= "" then
        properties.InstanceClass = IRSchema.RequireClassObjectPath(
            config.InstanceClass,
            "Linked Anim Layer InstanceClass")
    end
    if config.InterfaceClass ~= nil and config.InterfaceClass ~= "" then
        properties.InterfaceClass = IRSchema.RequireClassObjectPath(
            config.InterfaceClass,
            "Linked Anim Layer InterfaceClass")
    end
    return self:DynamicNode(
        name,
        EditorNodeClass.LinkedAnimLayer,
        "LinkedAnimLayer",
        properties,
        make_linked_function_pins(config.Parameters))
end

---创建调用另一个 AnimBlueprint 动画图函数的原生 Linked Anim Graph 节点。
---目标类必须与当前 Skeleton 兼容；GraphName 为空时由生成器绑定目标主 AnimGraph。
---@param name string Graph 内节点语义名。
---@param config LuaLinkedAnimGraphConfig 目标 AnimInstance 类、图函数名与输入签名。
---@return LuaAnimNode node 提供签名输入 Pin和标准 Pose 输出的 Linked Anim Graph 节点。
function LuaAnimGraph:LinkedAnimGraph(name, config)
    assert(type(config) == "table", "LinkedAnimGraph requires config")
    local properties = {
        InstanceClass = IRSchema.RequireClassObjectPath(
            assert(
                config.InstanceClass,
                "LinkedAnimGraph requires InstanceClass"),
            "Linked Anim Graph InstanceClass"),
    }
    if config.GraphName ~= nil and config.GraphName ~= "" then
        properties.GraphName = config.GraphName
    end
    return self:DynamicNode(
        name,
        EditorNodeClass.LinkedAnimGraph,
        "LinkedAnimGraph",
        properties,
        make_linked_function_pins(config.Parameters))
end

---创建原生 Inertialization 节点，用于消费请求并平滑连接输入姿势。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供 Source 输入和 Pose 输出的节点。
function LuaAnimGraph:Inertialization(name)
    return self:Node(
        name,
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
end

---创建原生 Slot 节点；调用方必须显式设置 SlotName，并把基础姿势连接到 Source。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供 Source 输入和 Pose 输出的 Slot 节点。
function LuaAnimGraph:Slot(name)
    return self:Node(
        name,
        EditorNodeClass.Slot,
        nil,
        "Slot")
end

---创建固定一个覆盖姿势的原生 Layered Blend Per Bone 节点。
---调用方必须设置 BranchFilters，并分别连接 BasePose、BlendPose；BlendWeight 可选连接。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供分骨骼姿势混合 Pin 的节点。
function LuaAnimGraph:LayeredBlendPerBone(name)
    return self:Node(
        name,
        EditorNodeClass.LayeredBlendPerBone,
        nil,
        "LayeredBlendPerBone")
end

---创建原生 Local To Component Space 节点，为 Skeletal Control 提供组件空间姿势。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供 LocalPose 输入和 ComponentPose 输出的转换节点。
function LuaAnimGraph:LocalToComponentSpace(name)
    return self:Node(
        name,
        EditorNodeClass.LocalToComponentSpace,
        nil,
        "LocalToComponentSpace")
end

---创建原生 Component To Local Space 节点，使 Skeletal Control 输出可继续连接普通 Pose 节点。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供 ComponentPose 输入和 Pose 输出的转换节点。
function LuaAnimGraph:ComponentToLocalSpace(name)
    return self:Node(
        name,
        EditorNodeClass.ComponentToLocalSpace,
        nil,
        "ComponentToLocalSpace")
end

---创建原生 Orientation Warping 节点，使用手动角度旋转下半身并经脊柱反向补偿上半身。
---@param name string Graph 内的节点语义名。
---@return LuaAnimNode node 提供姿势、角度和强度输入的方向扭曲节点。
function LuaAnimGraph:OrientationWarping(name)
    return self:Node(
        name,
        EditorNodeClass.OrientationWarping,
        nil,
        "OrientationWarping")
end

---创建原生 Foot Placement 节点，由 UE 完成双脚地面检测、脚部锁定、坡面旋转和骨盆补偿。
---@param name string Graph 内节点语义名。
---@return LuaAnimNode node 提供组件空间姿势输入、强度输入和姿势输出的节点。
function LuaAnimGraph:FootPlacement(name)
    return self:Node(
        name,
        EditorNodeClass.FootPlacement,
        nil,
        "FootPlacement")
end

---创建原生 Leg IK 节点，根据 IK 脚目标求解一条或多条 FK 腿链。
---@param name string Graph 内节点语义名。
---@return LuaAnimNode node 提供组件空间姿势输入、强度输入和姿势输出的节点。
function LuaAnimGraph:LegIK(name)
    return self:Node(
        name,
        EditorNodeClass.LegIK,
        nil,
        "LegIK")
end

---创建原生 Two Bone IK 节点，根据 Effector 和关节方向目标求解一条双骨骼链。
---@param name string Graph 内节点语义名。
---@return LuaAnimNode node 提供组件空间姿势输入、强度输入和姿势输出的节点。
function LuaAnimGraph:TwoBoneIK(name)
    return self:Node(
        name,
        EditorNodeClass.TwoBoneIK,
        nil,
        "TwoBoneIK")
end

---创建原生 Save Cached Pose 节点；缓存名默认与节点语义名相同。
---UE 只允许该节点放在主 Pose Graph，状态内部应使用 Use Cached Pose 读取已有缓存。
---@param name string Graph 和生成动画蓝图中的缓存姿势名。
---@return LuaAnimNode node 提供 Pose 输入的 Save Cached Pose 节点。
function LuaAnimGraph:SaveCachedPose(name)
    return self:Node(
        name,
        EditorNodeClass.SaveCachedPose,
        {
            CacheName = name,
        },
        "SaveCachedPose")
end

---创建原生 Use Cached Pose 节点并绑定当前 Pose Graph 中已声明的 Save Cached Pose 节点。
---@param name string 当前 Use Cached Pose 节点的语义名。
---@param saved_pose LuaAnimNode 目标 Save Cached Pose 节点，必须属于当前 Pose Graph。
---@return LuaAnimNode node 提供 Pose 输出的 Use Cached Pose 节点。
function LuaAnimGraph:UseCachedPose(name, saved_pose)
    assert(saved_pose ~= nil and saved_pose.NodeType == "SaveCachedPose", "UseCachedPose requires a SaveCachedPose Node")
    assert(saved_pose.Graph == self, "UseCachedPose target must belong to the same Pose Graph")
    return self:Node(
        name,
        EditorNodeClass.UseCachedPose,
        {
            CacheName = saved_pose.Name,
        },
        "UseCachedPose")
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

---解析节点实际声明的 Pin；结构型动态节点使用实例 Pin，普通节点继续使用注册契约。
---@param node LuaAnimNode 待校验的连线端点节点。
---@param pin_name string 待解析的 Pin 名称。
---@param direction SekiroAnimIRPinDirection 当前连线要求的 Pin 方向。
---@return LuaAnimNodePinContract pin_contract 包含方向、类型和复用约束的实际 Pin 契约。
local function require_node_pin(node, pin_name, direction)
    if node.Contract ~= nil and node.Contract.bDynamicPins ~= true then
        return NodeContracts.RequirePin(node.Contract, pin_name, direction)
    end

    for _, declared_pin in ipairs(node.Pins or {}) do
        if declared_pin.Name == pin_name then
            assert(declared_pin.Direction == direction, string.format(
                "Pin '%s.%s' is '%s', expected '%s'",
                node.NodeType,
                pin_name,
                declared_pin.Direction,
                direction))
            return declared_pin
        end
    end

    error(string.format(
        "NodeType '%s' has no declared Pin '%s'",
        node.NodeType,
        pin_name))
end

---连接两个节点的具名 Pin，并根据端点语义自动生成稳定 Link ID。
---@param source_node LuaAnimNode 提供输出 Pin 的节点。
---@param source_pin string 源节点输出 Pin 名称。
---@param target_node LuaAnimNode 接收输入 Pin 的节点。
---@param target_pin string 目标节点输入 Pin 名称。
---@return SekiroAnimIRLink link 新建的 Link IR 表。
function LuaAnimGraph:Link(source_node, source_pin, target_node, target_pin)
    assert(source_node ~= nil and target_node ~= nil, "Graph:Link requires source and target Nodes")
    IRSchema.RequireSemanticName(source_pin, "Source Pin")
    IRSchema.RequireSemanticName(target_pin, "Target Pin")
    local source_contract = require_node_pin(source_node, source_pin, "Output")
    local target_contract = require_node_pin(target_node, target_pin, "Input")
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

    ---@type SekiroAnimIRLayoutGrid[]
    local layout_grids = {}
    for _, grid in ipairs(self.LayoutGrids) do
        table.insert(layout_grids, grid:ToIR())
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
        Layout = {
            Style = self.LayoutStyle,
            Grids = layout_grids,
            Positions = self.LayoutPositions,
        },
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimGraph
