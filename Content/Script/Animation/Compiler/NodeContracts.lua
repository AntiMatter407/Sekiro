-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- AnimGraph 节点注册契约的 Lua 前端镜像。
-- 本模块只负责在导出 IR 前提供补全与快速失败；C++ NodeType 注册表仍是 Pin、Property 和节点放置规则的唯一权威。

---@class LuaAnimNodePinContract
---@field Name string UE 节点上的稳定 Pin 名称。
---@field Direction SekiroAnimIRPinDirection Pin 的连接方向。
---@field DataType string C++ 注册表使用的稳定数据类型名。
---@field bAllowMultipleConnections boolean Pin 是否允许连接多个 Link；Pose 输出通常允许扇出。

---@class LuaAnimNodePropertyContract
---@field Name string C++ NodeFactory 接受的稳定属性名。
---@field ValueType SekiroAnimIRValueType 属性值必须携带的显式 IR 类型。
---@field bRequired boolean 导出节点前是否必须显式赋值。

---@class LuaAnimNodeContract
---@field NodeType string C++ NodeType 注册名。
---@field GraphTypes table<string, boolean> 允许放置该节点的 GraphType 集合。
---@field RootGraphType string|nil 非空时表示该节点只能作为对应 GraphType 的固定根节点。
---@field OwnedGraphType string|nil 非空时表示节点必须持有该类型的内部 Graph。
---@field bDynamicPins boolean|nil 是否由函数签名提供完整动态 Pin，而不是使用固定 Pins 模板。
---@field Pins LuaAnimNodePinContract[] 与 C++ 注册表逐项一致的 Pin 断言模板。
---@field Properties LuaAnimNodePropertyContract[] 允许写入 IR 的属性契约。

---@class LuaAnimNodeContracts
local NodeContracts = {}
local PinDirection = UE.ELuaAnimIRPinDirection
local ValueType = UE.ELuaAnimIRValueType

---@type table<string, LuaAnimNodeContract>
local contracts = {
    OutputPose = {
        NodeType = "OutputPose",
        GraphTypes = { Pose = true },
        RootGraphType = "Pose",
        Pins = {
            {
                Name = "Result",
                Direction = PinDirection.Input,
                DataType = "Pose",
                bAllowMultipleConnections = false,
            },
        },
        Properties = {},
    },
    StateResult = {
        NodeType = "StateResult",
        GraphTypes = { StatePose = true },
        RootGraphType = "StatePose",
        Pins = {
            {
                Name = "Result",
                Direction = PinDirection.Input,
                DataType = "Pose",
                bAllowMultipleConnections = false,
            },
        },
        Properties = {},
    },
    SequencePlayer = {
        NodeType = "SequencePlayer",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "Pose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {
            { Name = "Sequence", ValueType = ValueType.SoftObjectPath, bRequired = true },
            { Name = "bLoopAnimation", ValueType = ValueType.Bool, bRequired = false },
            { Name = "PlayRate", ValueType = ValueType.Float, bRequired = false },
            { Name = "StartPosition", ValueType = ValueType.Float, bRequired = false },
            { Name = "GroupName", ValueType = ValueType.Name, bRequired = false },
            { Name = "GroupRole", ValueType = ValueType.Enum, bRequired = false },
            { Name = "GroupMethod", ValueType = ValueType.Enum, bRequired = false },
        },
    },
    StateMachine = {
        NodeType = "StateMachine",
        GraphTypes = { Pose = true, StatePose = true },
        OwnedGraphType = "StateMachine",
        Pins = {
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "Pose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {},
    },
    Inertialization = {
        NodeType = "Inertialization",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            {
                Name = "Source",
                Direction = PinDirection.Input,
                DataType = "Pose",
                bAllowMultipleConnections = false,
            },
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "Pose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {},
    },
    OrientationWarping = {
        NodeType = "OrientationWarping",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            {
                Name = "ComponentPose",
                Direction = PinDirection.Input,
                DataType = "ComponentPose",
                bAllowMultipleConnections = false,
            },
            {
                Name = "OrientationAngle",
                Direction = PinDirection.Input,
                DataType = "Float",
                bAllowMultipleConnections = false,
            },
            {
                Name = "LocomotionAngle",
                Direction = PinDirection.Input,
                DataType = "Float",
                bAllowMultipleConnections = false,
            },
            {
                Name = "Alpha",
                Direction = PinDirection.Input,
                DataType = "Float",
                bAllowMultipleConnections = false,
            },
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "ComponentPose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {
            { Name = "SpineBones", ValueType = ValueType.String, bRequired = true },
            { Name = "IKFootRootBone", ValueType = ValueType.Name, bRequired = true },
            { Name = "IKFootBones", ValueType = ValueType.String, bRequired = true },
            { Name = "RotationAxis", ValueType = ValueType.Name, bRequired = false },
            { Name = "DistributedBoneOrientationAlpha", ValueType = ValueType.Float, bRequired = false },
            { Name = "RotationInterpSpeed", ValueType = ValueType.Float, bRequired = false },
            {
                Name = "Mode",
                ValueType = ValueType.Enum,
                bRequired = false,
                DefaultValue = UE.EWarpingEvaluationMode.Manual,
            },
            { Name = "MinRootMotionSpeedThreshold", ValueType = ValueType.Float, bRequired = false },
            { Name = "LocomotionAngleDeltaThreshold", ValueType = ValueType.Float, bRequired = false },
            { Name = "WarpingAlpha", ValueType = ValueType.Float, bRequired = false },
            { Name = "OffsetAlpha", ValueType = ValueType.Float, bRequired = false },
            { Name = "MaxOffsetAngle", ValueType = ValueType.Float, bRequired = false },
        },
    },
    FootPlacement = {
        NodeType = "FootPlacement",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "ComponentPose", Direction = PinDirection.Input, DataType = "ComponentPose", bAllowMultipleConnections = false },
            { Name = "Alpha", Direction = PinDirection.Input, DataType = "Float", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "ComponentPose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "IKFootRootBone", ValueType = ValueType.Name, bRequired = true },
            { Name = "PelvisBone", ValueType = ValueType.Name, bRequired = true },
            { Name = "LegDefinitions", ValueType = ValueType.String, bRequired = true },
            { Name = "PlantSpeedMode", ValueType = ValueType.Enum, bRequired = false },
            { Name = "PlantLockType", ValueType = ValueType.Enum, bRequired = false },
            { Name = "PelvisMaxOffset", ValueType = ValueType.Float, bRequired = false },
            { Name = "PelvisHorizontalRebalancingWeight", ValueType = ValueType.Float, bRequired = false },
            { Name = "PlantSpeedThreshold", ValueType = ValueType.Float, bRequired = false },
            { Name = "PlantDistanceToGround", ValueType = ValueType.Float, bRequired = false },
            { Name = "TraceStartOffset", ValueType = ValueType.Float, bRequired = false },
            { Name = "TraceEndOffset", ValueType = ValueType.Float, bRequired = false },
            { Name = "TraceSweepRadius", ValueType = ValueType.Float, bRequired = false },
            { Name = "TraceMaxGroundPenetration", ValueType = ValueType.Float, bRequired = false },
            { Name = "bTraceEnabled", ValueType = ValueType.Bool, bRequired = false },
        },
    },
    LegIK = {
        NodeType = "LegIK",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "ComponentPose", Direction = PinDirection.Input, DataType = "ComponentPose", bAllowMultipleConnections = false },
            { Name = "Alpha", Direction = PinDirection.Input, DataType = "Float", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "ComponentPose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "LegDefinitions", ValueType = ValueType.String, bRequired = true },
            { Name = "ReachPrecision", ValueType = ValueType.Float, bRequired = false },
            { Name = "MaxIterations", ValueType = ValueType.Integer, bRequired = false },
        },
    },
    TwoBoneIK = {
        NodeType = "TwoBoneIK",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "ComponentPose", Direction = PinDirection.Input, DataType = "ComponentPose", bAllowMultipleConnections = false },
            { Name = "Alpha", Direction = PinDirection.Input, DataType = "Float", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "ComponentPose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "IKBone", ValueType = ValueType.Name, bRequired = true },
            { Name = "EffectorLocationSpace", ValueType = ValueType.Enum, bRequired = false },
            { Name = "EffectorTargetBoneName", ValueType = ValueType.Name, bRequired = false },
            { Name = "EffectorTargetSocketName", ValueType = ValueType.Name, bRequired = false },
            { Name = "JointTargetLocationSpace", ValueType = ValueType.Enum, bRequired = false },
            { Name = "JointTargetBoneName", ValueType = ValueType.Name, bRequired = false },
            { Name = "JointTargetSocketName", ValueType = ValueType.Name, bRequired = false },
            { Name = "AlphaInputType", ValueType = ValueType.Enum, bRequired = false },
            { Name = "AlphaCurveName", ValueType = ValueType.Name, bRequired = false },
            { Name = "EffectorLocationX", ValueType = ValueType.Float, bRequired = false },
            { Name = "EffectorLocationY", ValueType = ValueType.Float, bRequired = false },
            { Name = "EffectorLocationZ", ValueType = ValueType.Float, bRequired = false },
            { Name = "JointTargetLocationX", ValueType = ValueType.Float, bRequired = false },
            { Name = "JointTargetLocationY", ValueType = ValueType.Float, bRequired = false },
            { Name = "JointTargetLocationZ", ValueType = ValueType.Float, bRequired = false },
            { Name = "StartStretchRatio", ValueType = ValueType.Float, bRequired = false },
            { Name = "MaxStretchScale", ValueType = ValueType.Float, bRequired = false },
            { Name = "bTakeRotationFromEffectorSpace", ValueType = ValueType.Bool, bRequired = false },
            { Name = "bAllowStretching", ValueType = ValueType.Bool, bRequired = false },
        },
    },
    LocalToComponentSpace = {
        NodeType = "LocalToComponentSpace",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            {
                Name = "LocalPose",
                Direction = PinDirection.Input,
                DataType = "Pose",
                bAllowMultipleConnections = false,
            },
            {
                Name = "ComponentPose",
                Direction = PinDirection.Output,
                DataType = "ComponentPose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {},
    },
    ComponentToLocalSpace = {
        NodeType = "ComponentToLocalSpace",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            {
                Name = "ComponentPose",
                Direction = PinDirection.Input,
                DataType = "ComponentPose",
                bAllowMultipleConnections = false,
            },
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "Pose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {},
    },
    SaveCachedPose = {
        NodeType = "SaveCachedPose",
        GraphTypes = { Pose = true },
        Pins = {
            {
                Name = "Pose",
                Direction = PinDirection.Input,
                DataType = "Pose",
                bAllowMultipleConnections = false,
            },
        },
        Properties = {
            { Name = "CacheName", ValueType = ValueType.String, bRequired = true },
        },
    },
    UseCachedPose = {
        NodeType = "UseCachedPose",
        GraphTypes = { Pose = true },
        Pins = {
            {
                Name = "Pose",
                Direction = PinDirection.Output,
                DataType = "Pose",
                bAllowMultipleConnections = true,
            },
        },
        Properties = {
            { Name = "CacheName", ValueType = ValueType.String, bRequired = true },
        },
    },
    BoolPropertyGetter = {
        NodeType = "BoolPropertyGetter",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = { { Name = "Value", Direction = PinDirection.Output, DataType = "Bool", bAllowMultipleConnections = true } },
        Properties = { { Name = "PropertyName", ValueType = ValueType.Name, bRequired = true } },
    },
    FloatPropertyGetter = {
        NodeType = "FloatPropertyGetter",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = { { Name = "Value", Direction = PinDirection.Output, DataType = "Float", bAllowMultipleConnections = true } },
        Properties = { { Name = "PropertyName", ValueType = ValueType.Name, bRequired = true } },
    },
    BytePropertyGetter = {
        NodeType = "BytePropertyGetter",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = { { Name = "Value", Direction = PinDirection.Output, DataType = "Byte", bAllowMultipleConnections = true } },
        Properties = { { Name = "PropertyName", ValueType = ValueType.Name, bRequired = true } },
    },
    EnumPropertyGetter = {
        NodeType = "EnumPropertyGetter",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = { { Name = "Value", Direction = PinDirection.Output, DataType = "Enum", bAllowMultipleConnections = true } },
        Properties = { { Name = "PropertyName", ValueType = ValueType.Name, bRequired = true } },
    },
    BlendListByBool = {
        NodeType = "BlendListByBool",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "TruePose", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "FalsePose", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "ActiveValue", Direction = PinDirection.Input, DataType = "Bool", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "Pose", bAllowMultipleConnections = true },
        },
        Properties = { { Name = "BlendTime", ValueType = ValueType.Float, bRequired = false } },
    },
    BlendListByEnum = {
        NodeType = "BlendListByEnum",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "DefaultPose", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose0", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose1", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose2", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose3", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose4", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose5", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose6", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose7", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "ActiveValue", Direction = PinDirection.Input, DataType = "Enum", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "Pose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "EnumType", ValueType = ValueType.SoftObjectPath, bRequired = true },
            { Name = "EnumEntries", ValueType = ValueType.String, bRequired = true },
            { Name = "BlendTime", ValueType = ValueType.Float, bRequired = false },
        },
    },
    Slot = {
        NodeType = "Slot",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "Source", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "Pose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "SlotName", ValueType = ValueType.Name, bRequired = true },
            { Name = "bAlwaysUpdateSourcePose", ValueType = ValueType.Bool, bRequired = false },
        },
    },
    LayeredBlendPerBone = {
        NodeType = "LayeredBlendPerBone",
        GraphTypes = { Pose = true, StatePose = true },
        Pins = {
            { Name = "BasePose", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "BlendPose", Direction = PinDirection.Input, DataType = "Pose", bAllowMultipleConnections = false },
            { Name = "BlendWeight", Direction = PinDirection.Input, DataType = "Float", bAllowMultipleConnections = false },
            { Name = "Pose", Direction = PinDirection.Output, DataType = "Pose", bAllowMultipleConnections = true },
        },
        Properties = {
            { Name = "BranchFilters", ValueType = ValueType.String, bRequired = true },
            { Name = "bMeshSpaceRotationBlend", ValueType = ValueType.Bool, bRequired = false },
            { Name = "bMeshSpaceScaleBlend", ValueType = ValueType.Bool, bRequired = false },
            { Name = "CurveBlendOption", ValueType = ValueType.Enum, bRequired = false },
            { Name = "bBlendRootMotionBasedOnRootBone", ValueType = ValueType.Bool, bRequired = false },
        },
    },
    LinkedInputPose = {
        NodeType = "LinkedInputPose",
        GraphTypes = { Pose = true },
        bDynamicPins = true,
        Pins = {},
        Properties = {
            { Name = "PoseName", ValueType = ValueType.Name, bRequired = true },
        },
    },
    LinkedAnimLayer = {
        NodeType = "LinkedAnimLayer",
        GraphTypes = { Pose = true, StatePose = true },
        bDynamicPins = true,
        Pins = {},
        Properties = {
            { Name = "LayerName", ValueType = ValueType.Name, bRequired = true },
            { Name = "InstanceClass", ValueType = ValueType.SoftClassPath, bRequired = false },
            { Name = "InterfaceClass", ValueType = ValueType.SoftClassPath, bRequired = false },
        },
    },
    LinkedAnimGraph = {
        NodeType = "LinkedAnimGraph",
        GraphTypes = { Pose = true, StatePose = true },
        bDynamicPins = true,
        Pins = {},
        Properties = {
            { Name = "InstanceClass", ValueType = ValueType.SoftClassPath, bRequired = true },
            { Name = "GraphName", ValueType = ValueType.Name, bRequired = false },
        },
    },
}

---在节点契约中查找属性；该查询用于区分普通 Lua 字段与可直接赋值的原生节点属性。
---@param contract LuaAnimNodeContract 待查询的节点契约。
---@param property_name string Lua 代码赋值的字段名。
---@return LuaAnimNodePropertyContract|nil property 匹配属性；普通字段返回 nil。
function NodeContracts.FindProperty(contract, property_name)
    for _, registered_property in ipairs(contract.Properties) do
        if registered_property.Name == property_name then
            return registered_property
        end
    end

    return nil
end

---查询可选节点契约，供反射节点复用已有 Pin 和结构型适配信息。
---未知 NodeType 返回 nil，由 UE 原生节点类和 Schema 在后续阶段完成权威校验。
---@param node_type string 待查询的稳定 NodeType。
---@return LuaAnimNodeContract|nil contract 已注册契约；未知类型返回 nil。
function NodeContracts.Find(node_type)
    return contracts[node_type]
end

---取得 C++ 已注册节点类型的 Lua 镜像；未知类型立即失败，避免继续产生无效 IR。
---@param node_type string 待查询的 C++ NodeType 注册名。
---@return LuaAnimNodeContract contract 对应的只读前端契约；调用方不得修改其内容。
function NodeContracts.Require(node_type)
    local contract = contracts[node_type]
    assert(contract ~= nil, string.format("NodeType '%s' is not registered", tostring(node_type)))
    return contract
end

---检查节点是否位于契约允许的 Graph，并验证固定根节点只能由 Graph 构造流程创建。
---@param contract LuaAnimNodeContract 待检查节点的前端契约。
---@param graph_type string 节点所属 Graph 的注册类型。
---@param is_graph_root boolean 当前节点是否由 Graph 作为固定根节点创建。
---@return nil result 校验成功时不返回值，失败时抛出包含 NodeType 的断言。
function NodeContracts.ValidatePlacement(contract, graph_type, is_graph_root)
    assert(contract.GraphTypes[graph_type] == true, string.format(
        "NodeType '%s' cannot be placed in GraphType '%s'",
        contract.NodeType,
        tostring(graph_type)))

    if contract.RootGraphType ~= nil then
        assert(is_graph_root == true, string.format(
            "NodeType '%s' can only be created as a Graph root",
            contract.NodeType))
        assert(contract.RootGraphType == graph_type, string.format(
            "NodeType '%s' requires root GraphType '%s'",
            contract.NodeType,
            contract.RootGraphType))
    else
        assert(is_graph_root ~= true, string.format(
            "NodeType '%s' cannot be used as a Graph root",
            contract.NodeType))
    end
end

---复制注册 Pin 为 IR 断言，避免业务节点直接增删 Pin 或改变方向和类型。
---@param contract LuaAnimNodeContract 待导出节点的前端契约。
---@return SekiroAnimIRPin[] pins 按 C++ 注册顺序复制的完整 Pin 断言数组。
function NodeContracts.CopyPinAssertions(contract)
    ---@type SekiroAnimIRPin[]
    local pins = {}
    for index, registered_pin in ipairs(contract.Pins) do
        table.insert(pins, {
            Name = registered_pin.Name,
            Direction = registered_pin.Direction,
            DataType = registered_pin.DataType,
            bAllowMultipleConnections = registered_pin.bAllowMultipleConnections,
            DeclarationOrder = index - 1,
        })
    end
    return pins
end

---解析具名 Pin 并检查期望方向，供 Link 声明在进入 C++ 前尽早发现端点拼写错误。
---@param contract LuaAnimNodeContract 端点所属节点的前端契约。
---@param pin_name string Link 使用的稳定 Pin 名称。
---@param direction SekiroAnimIRPinDirection Link 对该端点要求的方向。
---@return LuaAnimNodePinContract pin 匹配的注册 Pin 契约。
function NodeContracts.RequirePin(contract, pin_name, direction)
    for _, registered_pin in ipairs(contract.Pins) do
        if registered_pin.Name == pin_name then
            assert(registered_pin.Direction == direction, string.format(
                "Pin '%s.%s' is '%s', expected '%s'",
                contract.NodeType,
                pin_name,
                registered_pin.Direction,
                direction))
            return registered_pin
        end
    end

    error(string.format("NodeType '%s' has no registered Pin '%s'", contract.NodeType, pin_name))
end

---解析具名 Property，禁止 Lua 节点向 IR 注入 C++ 注册表之外的任意属性。
---@param contract LuaAnimNodeContract 属性所属节点的前端契约。
---@param property_name string 待赋值的稳定属性名。
---@return LuaAnimNodePropertyContract property 匹配的属性契约。
function NodeContracts.RequireProperty(contract, property_name)
    local registered_property = NodeContracts.FindProperty(contract, property_name)
    if registered_property ~= nil then
        return registered_property
    end

    error(string.format(
        "NodeType '%s' has no registered Property '%s'",
        contract.NodeType,
        property_name))
end

---在导出前验证必填 Property 与 OwnedGraph 声明，使 Lua 错误定位停留在原始节点声明附近。
---@param contract LuaAnimNodeContract 待导出节点的前端契约。
---@param property_names table<string, boolean> 当前节点已经显式赋值的属性名集合。
---@param owned_graph_id string 当前节点拥有的内部 Graph ID；无所有权时为空字符串。
---@return nil result 校验成功时不返回值，失败时抛出缺失契约信息。
function NodeContracts.ValidateExport(contract, property_names, owned_graph_id)
    for _, registered_property in ipairs(contract.Properties) do
        if registered_property.bRequired == true then
            assert(property_names[registered_property.Name] == true, string.format(
                "NodeType '%s' requires Property '%s'",
                contract.NodeType,
                registered_property.Name))
        end
    end

    if contract.OwnedGraphType ~= nil then
        assert(owned_graph_id ~= "", string.format(
            "NodeType '%s' requires an owned '%s' Graph",
            contract.NodeType,
            contract.OwnedGraphType))
    else
        assert(owned_graph_id == "", string.format(
            "NodeType '%s' cannot own an internal Graph",
            contract.NodeType))
    end
end

return NodeContracts
