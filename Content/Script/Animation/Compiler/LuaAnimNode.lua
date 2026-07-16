-- Lua AnimGraph 节点的编译期抽象。
-- 节点只声明 Pin 和类型化 Property，不负责运行时 Update、Evaluate 或 Pose 计算。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local IRSchema = require("Animation.Compiler.IRSchema")
local IRValue = require("Animation.Compiler.IRValue")
local LuaAnimPin = require("Animation.Compiler.LuaAnimPin")
local NodeContracts = require("Animation.Compiler.NodeContracts")

---@class LuaAnimNodeConfig
---@field Graph LuaAnimGraph 所属 Pose Graph。
---@field Name string 节点语义名称。
---@field NodeType string|nil NodeFactory 注册类型；具体子类可在初始化前补入。
---@field DisplayName string|nil 编辑器显示名称。
---@field OwnedGraphId string|nil 节点独占的内部 Graph ID。
---@field bIsGraphRoot boolean|nil 是否由 Graph 构造流程创建为固定根节点。
---@field SourceLocation SekiroAnimIRSourceLocation|nil 节点源码位置。

---@class LuaAnimNode: CompilerClass
---@field Graph LuaAnimGraph 所属 Pose Graph。
---@field Name string 节点语义名称。
---@field Id string 节点稳定 ID。
---@field NodeType string NodeFactory 注册类型。
---@field DisplayName string 编辑器显示名称。
---@field OwnedGraphId string 节点独占的内部 Graph ID。
---@field SourceLocation SekiroAnimIRSourceLocation 节点源码位置。
---@field Contract LuaAnimNodeContract C++ NodeType 注册表的 Lua 前端镜像。
---@field Pins SekiroAnimIRPin[] 从注册契约复制的完整 Pin 断言。
---@field PinObjects table<string, LuaAnimPin> 供业务 Graph 直接连接的具名 Pin 对象。
---@field Properties SekiroAnimIRProperty[] Lua 请求 NodeFactory 写入的类型化属性值。
---@field PropertyNames table<string, boolean> 已赋值属性名集合，用于重复和必填校验。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
local LuaAnimNode = CompilerClass:Extend("LuaAnimNode")

---拦截节点注册属性的直接赋值，并把普通 Lua 值转换为契约要求的显式 IR Value。
---Pin 和编译器内部字段仍按普通 Lua 字段保存，业务代码无需调用 SetProperty。
---@param instance LuaAnimNode 正在接收赋值的节点实例。
---@param key string 待写入的字段或注册属性名。
---@param value any Lua 动画蓝图提供的字段值。
---@return nil result 该函数直接更新节点实例或属性 IR。
local function assign_node_field(instance, key, value)
    local contract = rawget(instance, "Contract")
    local registered_property = contract ~= nil
        and NodeContracts.FindProperty(contract, key)
        or nil
    if registered_property ~= nil then
        instance:SetProperty(key, IRValue.From(registered_property.ValueType, value))
        return
    end

    assert(rawget(instance, "bIsSealed") ~= true, string.format(
        "NodeType '%s' has no registered Property '%s'",
        tostring(rawget(instance, "NodeType")),
        tostring(key)))

    rawset(instance, key, value)
end

LuaAnimNode.__newindex = assign_node_field

---初始化节点身份，并从 C++ 注册契约的 Lua 镜像复制完整 Pin 断言。
---@param config LuaAnimNodeConfig Graph、名称、节点类型和源码位置等构造参数。
---@return nil result 该函数只初始化节点声明，不返回业务值。
function LuaAnimNode:Initialize(config)
    self.Graph = assert(config.Graph, "LuaAnimNode requires Graph")
    self.Name = IRSchema.RequireSemanticName(config.Name, "Node")
    self.Id = IRSchema.MakeStableId(self.Graph.Id, "Node", self.Name)
    self.NodeType = IRSchema.RequireSemanticName(config.NodeType, "NodeType")
    self.DisplayName = config.DisplayName or self.Name
    self.OwnedGraphId = config.OwnedGraphId or ""
    self.SourceLocation = config.SourceLocation
        or IRSchema.CaptureSourceLocation(self.Graph.Blueprint.SourceModule, 3)
    self.Contract = NodeContracts.Require(self.NodeType)
    NodeContracts.ValidatePlacement(self.Contract, self.Graph.GraphType, config.bIsGraphRoot == true)
    self.Pins = NodeContracts.CopyPinAssertions(self.Contract)
    self.PinObjects = {}
    for _, pin_contract in ipairs(self.Contract.Pins) do
        ---@type LuaAnimPin
        local pin = LuaAnimPin:New({
            Node = self,
            Name = pin_contract.Name,
            Direction = pin_contract.Direction,
            DataType = pin_contract.DataType,
        })
        self.PinObjects[pin.Name] = pin
        rawset(self, pin.Name, pin)
    end
    self.Properties = {}
    self.PropertyNames = {}
end

---向节点写入一个已注册的显式类型化 Property；名称和 Value.Type 必须符合 NodeType 契约。
---@param name string NodeFactory 注册的属性名。
---@param value SekiroAnimIRValue 由 IRValue 构造的类型化属性值。
---@return SekiroAnimIRProperty property 新建的 Property IR 表。
function LuaAnimNode:SetProperty(name, value)
    local property_name = IRSchema.RequireSemanticName(name, "Property")
    local registered_property = NodeContracts.RequireProperty(self.Contract, property_name)
    assert(value ~= nil and value.Type ~= nil, "Node Property requires a typed IRValue")
    assert(value.Type == registered_property.ValueType, string.format(
        "Property '%s.%s' requires IR value type '%s', got '%s'",
        self.NodeType,
        property_name,
        registered_property.ValueType,
        tostring(value.Type)))
    assert(self.PropertyNames[property_name] == nil, string.format(
        "Node '%s' contains duplicate Property '%s'",
        self.Name,
        property_name))

    ---@type SekiroAnimIRProperty
    local property = {
        Name = property_name,
        Value = value,
        DeclarationOrder = #self.Properties,
    }
    self.PropertyNames[property_name] = true
    table.insert(self.Properties, property)
    return property
end

---导出与 FSekiroAnimIRNode 字段一致的纯 Lua 表。
---@return SekiroAnimIRNode ir_node 可交给 C++ 导入器的节点声明。
function LuaAnimNode:ToIR()
    NodeContracts.ValidateExport(self.Contract, self.PropertyNames, self.OwnedGraphId)
    return {
        Id = self.Id,
        NodeType = self.NodeType,
        DisplayName = self.DisplayName,
        OwnedGraphId = self.OwnedGraphId,
        Pins = self.Pins,
        Properties = self.Properties,
        DeclarationOrder = self.DeclarationOrder or 0,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimNode
