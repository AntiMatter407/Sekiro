-- Lua 类型：纯 Lua 工具模块。负责声明通用行为树 DSL 与强类型 IR，不创建 UObject、不保存资产。

---@class LuaBehaviorTreeValue
---@field Type number 原生 ELuaBehaviorTreeValueType 类型标签。
---@field Value any 与类型标签对应的原始值。

---@class LuaBehaviorTreeSourceLocation
---@field LuaModule string Lua 模块名。
---@field Line number 一基源码行号。
---@field Column number 一基源码列号，Lua 调试库无法提供时为零。

---@class LuaBehaviorTreeProperty
---@field Name string 反射属性名。
---@field Value LuaBehaviorTreeValue 强类型属性值。
---@field DeclarationOrder number 属性声明顺序。

---@class LuaBehaviorTreeNodeIR
---@field Id string 全局稳定 ID。
---@field ParentId string 父主节点 ID。
---@field ClassPath string UE 类路径。
---@field DisplayName string 编辑器显示名。
---@field Properties LuaBehaviorTreeProperty[] 强类型反射属性。
---@field DeclarationOrder number 节点声明顺序。
---@field SourceLocation LuaBehaviorTreeSourceLocation 源码定位。

---@class LuaBlackboardKeyIR
---@field Id string 全局稳定 ID。
---@field Name string Blackboard Key 名。
---@field ClassPath string UBlackboardKeyType 子类路径。
---@field Properties LuaBehaviorTreeProperty[] KeyType 属性。
---@field bInstanceSynced boolean 是否跨实例同步。
---@field DeclarationOrder number Key 声明顺序。
---@field SourceLocation LuaBehaviorTreeSourceLocation 源码定位。

---@class LuaBehaviorTreeIR
---@field SchemaVersion number Schema 版本。
---@field SourceModule string 唯一源码模块。
---@field RootNodeId string 根 Composite ID。
---@field ParentBlackboard string 可选父 Blackboard 对象路径。
---@field Nodes LuaBehaviorTreeNodeIR[] 主节点。
---@field Decorators LuaBehaviorTreeNodeIR[] Decorator 节点。
---@field Services LuaBehaviorTreeNodeIR[] Service 节点。
---@field BlackboardKeys LuaBlackboardKeyIR[] Blackboard Keys。

---@class LuaBehaviorTreeDefinitionConfig
---@field SourceModule string Lua 模块名。
---@field ParentBlackboard string|nil 可选父 Blackboard 对象路径。

---@class LuaBehaviorTreeNode
---@field Definition LuaBehaviorTreeDefinition 所属定义。
---@field IR LuaBehaviorTreeNodeIR 对应 IR 节点。

---@class LuaBehaviorTreeDefinition
---@field SourceModule string Lua 模块名。
---@field ParentBlackboard string 父 Blackboard 路径。
---@field RootNodeId string 根节点 ID。
---@field Nodes LuaBehaviorTreeNodeIR[] 主节点。
---@field Decorators LuaBehaviorTreeNodeIR[] Decorator 节点。
---@field Services LuaBehaviorTreeNodeIR[] Service 节点。
---@field BlackboardKeys LuaBlackboardKeyIR[] Blackboard Keys。
---@field UsedIds table<string, boolean> 已使用稳定 ID 集合。
---@field DeclarationOrder number 全局声明顺序。
---@field BehaviorTree fun(self: LuaBehaviorTreeDefinition, tree: LuaBehaviorTreeDefinition):nil|nil 业务树声明回调。
---@field DeclareBlackboard fun(self: LuaBehaviorTreeDefinition, blackboard: LuaBehaviorTreeDefinition):nil|nil Blackboard 声明回调。

local Value = {}
local ValueType = UE.ELuaBehaviorTreeValueType
local Node = {}
Node.__index = Node
local Definition = {}
Definition.__index = Definition

---创建一个显式类型值，禁止编译器根据 Lua 原生类型猜测 FProperty。
---@param type_name number C++ importer 支持的原生 ELuaBehaviorTreeValueType。
---@param value any 标签对应的值。
---@return LuaBehaviorTreeValue typed_value 强类型 IR 值。
local function typed_value(type_name, value)
    return {
        Type = type_name,
        Value = value,
    }
end

---创建 Bool 强类型值。
---@param value boolean 布尔值。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Bool(value)
    return typed_value(ValueType.Bool, value)
end

---创建 Integer 强类型值。
---@param value number 整数值。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Integer(value)
    return typed_value(ValueType.Integer, value)
end

---创建 Float 强类型值。
---@param value number 浮点值。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Float(value)
    return typed_value(ValueType.Float, value)
end

---创建 String 强类型值。
---@param value string 字符串值。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.String(value)
    return typed_value(ValueType.String, value)
end

---创建 Name 强类型值。
---@param value string FName 文本。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Name(value)
    return typed_value(ValueType.Name, value)
end

---创建 Text 强类型值。
---@param value string 本地化源文本。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Text(value)
    return typed_value(ValueType.Text, value)
end

---创建 Enum 强类型值。
---@param value number UnLua 暴露的原生 UENUM 成员值，例如 UE.EPathFollowingRequestResult.RequestSuccessful。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Enum(value)
    assert(type(value) == "number", "Enum 强类型值必须直接传入 UE.EEnumType.Member")
    return typed_value(ValueType.Enum, value)
end

---创建硬对象引用强类型值。
---@param path string UObject 路径；空字符串表示 nullptr。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Object(path)
    return typed_value(ValueType.Object, path)
end

---创建软对象引用强类型值。
---@param path string UObject 软路径。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.SoftObject(path)
    return typed_value(ValueType.SoftObject, path)
end

---创建硬类引用强类型值。
---@param path string UClass 路径；空字符串表示 nullptr。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Class(path)
    return typed_value(ValueType.Class, path)
end

---创建软类引用强类型值。
---@param path string UClass 软路径。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.SoftClass(path)
    return typed_value(ValueType.SoftClass, path)
end

---创建 Struct 强类型值，每个字段仍必须使用 Value 构造器。
---@param fields table<string, LuaBehaviorTreeValue> 结构体字段。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Struct(fields)
    return typed_value(ValueType.Struct, fields)
end

---创建 Array 强类型值，每个元素仍必须使用 Value 构造器。
---@param items LuaBehaviorTreeValue[] 数组元素。
---@return LuaBehaviorTreeValue typed_value 强类型值。
function Value.Array(items)
    return typed_value(ValueType.Array, items)
end

---捕获声明调用点；行号只用于诊断，不参与稳定 ID。
---@param module_name string 当前 Lua 模块名。
---@param stack_level number debug.getinfo 调用栈层级。
---@return LuaBehaviorTreeSourceLocation location 源码定位。
local function capture_location(module_name, stack_level)
    local info = debug.getinfo(stack_level, "l")
    return {
        LuaModule = module_name,
        Line = info ~= nil and info.currentline or 0,
        Column = 0,
    }
end

---将属性 map 规范化为按名称排序的数组，保证跨进程确定性。
---@param properties table<string, LuaBehaviorTreeValue>|nil 属性 map。
---@return LuaBehaviorTreeProperty[] result 属性数组。
local function compile_properties(properties)
    local names = {}
    for name, _ in pairs(properties or {}) do
        names[#names + 1] = name
    end
    table.sort(names)

    local result = {}
    for index, name in ipairs(names) do
        local value = properties[name]
        assert(type(value) == "table" and type(value.Type) == "number",
            "属性 " .. name .. " 必须使用 LuaBehaviorTree.Value 构造器")
        result[#result + 1] = {
            Name = name,
            Value = value,
            DeclarationOrder = index,
        }
    end
    return result
end

---分配全局唯一且可复现的稳定 ID；重复语义名直接报错而不是自动改名。
---@param self LuaBehaviorTreeDefinition 当前定义。
---@param role string 节点角色。
---@param name string 语义名称。
---@return string id 稳定 ID。
function Definition:AllocateId(role, name)
    local id = role .. ":" .. name
    assert(self.UsedIds[id] ~= true, "重复稳定 ID：" .. id)
    self.UsedIds[id] = true
    return id
end

---创建主节点；具体 Composite/Task 角色由 C++ 根据 ClassPath 继承关系判断。
---@param parent_id string 父节点 ID，根节点为空字符串。
---@param class_path string UBTCompositeNode 或 UBTTaskNode 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 新节点。
function Definition:AddMainNode(parent_id, class_path, name, properties)
    self.DeclarationOrder = self.DeclarationOrder + 1
    local ir = {
        Id = self:AllocateId("Node", name),
        ParentId = parent_id,
        ClassPath = class_path,
        DisplayName = name,
        Properties = compile_properties(properties),
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = capture_location(self.SourceModule, 3),
    }
    self.Nodes[#self.Nodes + 1] = ir
    return setmetatable({
        Definition = self,
        IR = ir,
    }, Node)
end

---创建根 Composite；每个定义只允许一个根。
---@param class_path string UBTCompositeNode 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 根节点。
function Definition:Composite(class_path, name, properties)
    assert(self.RootNodeId == "", "一个行为树只能声明一个根 Composite")
    local node = self:AddMainNode("", class_path, name, properties)
    self.RootNodeId = node.IR.Id
    return node
end

---在当前节点下创建 Composite；C++ 会验证实际继承关系。
---@param class_path string UBTCompositeNode 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 新 Composite。
function Node:Composite(class_path, name, properties)
    return self.Definition:AddMainNode(self.IR.Id, class_path, name, properties)
end

---在当前节点下创建 Task；C++ 会验证实际继承关系。
---@param class_path string UBTTaskNode 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 新 Task。
function Node:Task(class_path, name, properties)
    return self.Definition:AddMainNode(self.IR.Id, class_path, name, properties)
end

---在当前节点下创建由通用运行时宿主分派的 UnLua Task。
---每个节点只声明模块名和配置字符串；不同模块共用同一个 C++ 类型，无需为 Lua Task 新增 UCLASS。
---@param name string 稳定语义名与编辑器显示名。
---@param lua_module_name string 相对 Content/Script 的 Lua require 模块名。
---@param configuration string|nil 原样传给 Execute/Tick/Abort 的可选配置字符串。
---@return LuaBehaviorTreeNode node 新 UnLua Task。
function Node:LuaTask(name, lua_module_name, configuration)
    assert(type(lua_module_name) == "string" and lua_module_name ~= "",
        "LuaTask 的 lua_module_name 不能为空")
    return self:Task(
        "/Script/LuaBehaviorTree.LuaBehaviorTreeTask",
        name,
        {
            LuaModuleName = Value.String(lua_module_name),
            Configuration = Value.String(configuration or ""),
        })
end

---给当前主节点挂载 Decorator。
---@param class_path string UBTDecorator 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 新 Decorator。
function Node:Decorator(class_path, name, properties)
    local definition = self.Definition
    definition.DeclarationOrder = definition.DeclarationOrder + 1
    local ir = {
        Id = definition:AllocateId("Decorator", name),
        ParentId = self.IR.Id,
        ClassPath = class_path,
        DisplayName = name,
        Properties = compile_properties(properties),
        DeclarationOrder = definition.DeclarationOrder,
        SourceLocation = capture_location(definition.SourceModule, 3),
    }
    definition.Decorators[#definition.Decorators + 1] = ir
    return setmetatable({
        Definition = definition,
        IR = ir,
    }, Node)
end

---给当前主节点挂载 Service。
---@param class_path string UBTService 子类路径。
---@param name string 稳定语义名与显示名。
---@param properties table<string, LuaBehaviorTreeValue>|nil 强类型属性。
---@return LuaBehaviorTreeNode node 新 Service。
function Node:Service(class_path, name, properties)
    local definition = self.Definition
    definition.DeclarationOrder = definition.DeclarationOrder + 1
    local ir = {
        Id = definition:AllocateId("Service", name),
        ParentId = self.IR.Id,
        ClassPath = class_path,
        DisplayName = name,
        Properties = compile_properties(properties),
        DeclarationOrder = definition.DeclarationOrder,
        SourceLocation = capture_location(definition.SourceModule, 3),
    }
    definition.Services[#definition.Services + 1] = ir
    return setmetatable({
        Definition = definition,
        IR = ir,
    }, Node)
end

---声明一个 Blackboard Key；KeyType 类及属性完全由 UE 反射解析。
---@param class_path string UBlackboardKeyType 子类路径。
---@param name string Blackboard Key 名。
---@param properties table<string, LuaBehaviorTreeValue>|nil KeyType 强类型属性。
---@param instance_synced boolean|nil 是否跨实例同步。
---@return nil result 本函数只追加声明，不返回值。
function Definition:Key(class_path, name, properties, instance_synced)
    self.DeclarationOrder = self.DeclarationOrder + 1
    self.BlackboardKeys[#self.BlackboardKeys + 1] = {
        Id = self:AllocateId("BlackboardKey", name),
        Name = name,
        ClassPath = class_path,
        Properties = compile_properties(properties),
        bInstanceSynced = instance_synced == true,
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = capture_location(self.SourceModule, 3),
    }
end

---执行 Blackboard 与行为树声明回调并导出规范 IR。
---@return LuaBehaviorTreeIR ir 可交给 C++ importer 的强类型 IR。
function Definition:CompileIR()
    self.Nodes = {}
    self.Decorators = {}
    self.Services = {}
    self.BlackboardKeys = {}
    self.UsedIds = {}
    self.DeclarationOrder = 0
    self.RootNodeId = ""

    if self.DeclareBlackboard ~= nil then
        self:DeclareBlackboard(self)
    end
    assert(self.BehaviorTree ~= nil, "定义必须实现 BehaviorTree(tree)")
    self:BehaviorTree(self)
    assert(self.RootNodeId ~= "", "BehaviorTree 必须声明根 Composite")

    return {
        SchemaVersion = 1,
        SourceModule = self.SourceModule,
        RootNodeId = self.RootNodeId,
        ParentBlackboard = self.ParentBlackboard,
        Nodes = self.Nodes,
        Decorators = self.Decorators,
        Services = self.Services,
        BlackboardKeys = self.BlackboardKeys,
    }
end

---@class LuaBehaviorTree
---@field Value table 强类型值构造器集合。
local LuaBehaviorTree = {
    Value = Value,
}

---创建可由业务模块覆写 DeclareBlackboard 与 BehaviorTree 的定义对象。
---@param config LuaBehaviorTreeDefinitionConfig 定义配置。
---@return LuaBehaviorTreeDefinition definition 新定义。
function LuaBehaviorTree.New(config)
    assert(type(config) == "table", "config 必须是 table")
    assert(type(config.SourceModule) == "string" and config.SourceModule ~= "", "SourceModule 不能为空")
    ---@type LuaBehaviorTreeDefinition
    local definition = setmetatable({
        SourceModule = config.SourceModule,
        ParentBlackboard = config.ParentBlackboard or "",
        RootNodeId = "",
        Nodes = {},
        Decorators = {},
        Services = {},
        BlackboardKeys = {},
        UsedIds = {},
        DeclarationOrder = 0,
    }, Definition)

    ---绑定定义实例，使 C++ 以无参函数调用 CompileIR 时不依赖隐式 self。
    ---@return LuaBehaviorTreeIR ir 当前定义编译出的强类型 IR。
    definition.CompileIR = function()
        return Definition.CompileIR(definition)
    end
    return definition
end

return LuaBehaviorTree
