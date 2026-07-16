# LuaAnimNode

## 一、LuaAnimNode 是什么

`LuaAnimNode` 是所有 Lua AnimGraph 节点的编译期基类。

它把前面几层能力组合起来：

- `CompilerClass`：继承和实例化；
- `IRSchema`：名称检查、稳定 ID 和源码位置；
- `NodeContracts`：NodeType、放置规则、Pin 和 Property 契约；
- `LuaAnimPin`：业务代码可以使用的具名 Pin；
- `IRValue`：把直接赋值转换为显式类型 Property；
- `ToIR()`：输出 C++ 导入器需要的 `SekiroAnimIRNode`。

它不是 UE 的 `FAnimNode_Base`，也不是某个运行时动画节点实例。它描述“应该在生成的动画蓝图里创建一个什么节点”。

## 二、它在整体流程中的位置

```text
业务 Graph
    -> graph:SequencePlayer("IdlePlayer")
    -> LuaSequencePlayerNode
    -> LuaAnimNode 基类初始化节点、Pin 和 Property
    -> LuaAnimNode:ToIR()
    -> SekiroAnimIRNode
    -> C++ Lua Importer
    -> C++ NodeFactory
    -> UAnimGraphNode_SequencePlayer
    -> 动画蓝图编译产生原生运行时 AnimNode
```

Lua 节点到 C++ 原生节点之间不是继承关系，而是“编译描述生成原生节点”的关系。

## 三、LuaAnimNodeConfig

构造节点时使用 `LuaAnimNodeConfig`：

| 字段 | 含义 |
|---|---|
| `Graph` | 节点所属的 `LuaAnimGraph` |
| `Name` | Graph 内语义名称，用于稳定 ID 和诊断 |
| `NodeType` | C++ NodeFactory 注册类型 |
| `DisplayName` | 生成到编辑器后的显示名称 |
| `OwnedGraphId` | 节点拥有的内部 Graph ID |
| `bIsGraphRoot` | 是否由 Graph 作为固定根节点创建 |
| `SourceLocation` | Lua 声明位置 |

具体节点通常替调用方补入 `NodeType`。例如：

```lua
function LuaSequencePlayerNode:Initialize(config)
    config.NodeType = "SequencePlayer"
    LuaAnimNode.Initialize(self, config)
end
```

因此业务代码不需要手写字符串类型：

```lua
local player = graph:SequencePlayer("IdlePlayer")
```

## 四、Initialize 的完整过程

### 1. 建立所属关系

```lua
self.Graph = config.Graph
```

节点必须属于一个 Pose Graph 或 StatePose Graph，不能独立存在。

### 2. 校验名称并生成稳定 ID

```lua
self.Name = IRSchema.RequireSemanticName(config.Name, "Node")
self.Id = IRSchema.MakeStableId(self.Graph.Id, "Node", self.Name)
```

例如：

```text
Layer/Main/Graph/AnimGraph/Node/IdlePlayer
```

稳定 ID 用于 IR 引用、确定性 Guid、诊断和增量重建。`Name` 是 Lua 语义名称，`DisplayName` 是编辑器显示名称，两者可以不同。

### 3. 解析 NodeType 和源码位置

```lua
self.NodeType = config.NodeType
self.SourceLocation = config.SourceLocation or CaptureSourceLocation(...)
```

`NodeType` 决定 C++ 最终创建哪种节点；源码位置用于把导入或验证错误定位回 Lua 文件。

### 4. 取得并验证节点契约

```lua
self.Contract = NodeContracts.Require(self.NodeType)
NodeContracts.ValidatePlacement(...)
```

这一阶段会拒绝：

- 未注册的 NodeType；
- 放入错误 GraphType 的节点；
- 手工创建固定根节点；
- 普通节点冒充 Graph 根节点。

### 5. 创建两种 Pin 表示

```lua
self.Pins = NodeContracts.CopyPinAssertions(self.Contract)
self.PinObjects = {}
```

这里有两套数据：

- `Pins`：准备导出的 `SekiroAnimIRPin[]` 契约快照；
- `PinObjects`：业务代码使用的 `LuaAnimPin` 对象。

每个 Pin 对象还会通过 `rawset` 挂到节点自身：

```lua
rawset(self, pin.Name, pin)
```

因此可以直接写：

```lua
player.Pose
inertialization.Source
```

### 6. 初始化属性容器

```lua
self.Properties = {}
self.PropertyNames = {}
```

- `Properties` 保存准备导出的类型化属性；
- `PropertyNames` 记录已经赋值的属性，用于检查必填项和重复声明。

## 五、直接属性赋值是怎样实现的

业务代码写：

```lua
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true
player.PlayRate = 1.0
```

这些值并没有作为普通字段直接放入节点 table。`LuaAnimNode.__newindex` 指向 `assign_node_field()`，因此不存在的字段被赋值时会先进入代理函数。

以 `player.Sequence` 为例：

```text
player.Sequence = 动画路径
    -> assign_node_field(player, "Sequence", 动画路径)
    -> NodeContracts.FindProperty()
    -> 找到 Sequence: SoftObjectPath
    -> IRValue.From("SoftObjectPath", 动画路径)
    -> player:SetProperty("Sequence", 类型化值)
    -> 写入 player.Properties
```

最终不是：

```lua
player.Sequence = "/Game/..."
```

而是生成类似：

```lua
{
    Name = "Sequence",
    Value = {
        Type = "SoftObjectPath",
        SoftObjectPathValue = "/Game/...",
    },
    DeclarationOrder = 0,
}
```

这个代理通过 `CompilerClass:Extend()` 复制到 `LuaSequencePlayerNode` 等子类，因此具体节点同样保留直接属性赋值行为。

## 六、为什么要封闭节点

节点创建并加入 Graph 后，`LuaAnimGraph:AddNode()` 会执行：

```lua
rawset(node, "bIsSealed", true)
```

封闭前，基类初始化可以写入 `Graph`、`Id`、`Contract`、`Pins` 等内部字段。

封闭后：

- 契约中注册的 Property 仍可直接赋值；
- 未注册的新字段会立即报错；
- 已经存在的内部字段按照规范不应由业务代码修改；
- 同一 Property 重复赋值会被 `SetProperty()` 拒绝。

例如：

```lua
player.PlaySpeed = 2.0
```

`PlaySpeed` 不是注册属性，节点封闭后会报告：

```text
NodeType 'SequencePlayer' has no registered Property 'PlaySpeed'
```

正确属性是：

```lua
player.PlayRate = 2.0
```

这里的封闭只是一层编译期防误用，并非完整只读代理。Lua 只有在键原本不存在时才调用 `__newindex`，所以业务代码如果主动覆盖 `player.NodeType`、`player.Graph` 等已有字段，当前实现不会经过该检查。此类写法违反 DSL 规范，C++ 验证可能在后续拒绝它，但 Lua 层目前不能完全阻止。

## 七、SetProperty 做了什么

`SetProperty(name, value)` 是直接赋值代理最终使用的内部接口。

它依次检查：

1. Property 名称格式是否合法；
2. Property 是否在当前 NodeType 契约中注册；
3. 值是否为带 `Type` 的 `IRValue`；
4. `Value.Type` 是否与契约要求一致；
5. 当前节点是否已经声明过同名 Property。

随后创建：

```lua
{
    Name = property_name,
    Value = value,
    DeclarationOrder = #self.Properties,
}
```

业务 Graph 不应该主动调用 `SetProperty()` 或构造 `IRValue`，而应直接给具体节点属性赋值。

## 八、DeclarationOrder 从哪里来

`LuaAnimNode:Initialize()` 不决定节点顺序。节点加入 Graph 时，`LuaAnimGraph:AddNode()` 设置：

```lua
node.DeclarationOrder = #self.Nodes
```

因此声明顺序由 Graph 统一管理：

```lua
local idle = graph:SequencePlayer("Idle")       -- 较早
local locomotion = graph:StateMachine("Move")  -- 较晚
```

确定性排序可以让相同 Lua 源码生成稳定 IR、稳定 Guid 和稳定编辑器布局。

## 九、OwnedGraphId

多数节点没有内部 Graph：

```lua
OwnedGraphId = ""
```

`StateMachine` 是特殊节点。`LuaStateMachineNode` 在基类初始化后创建内部 `LuaAnimStateMachineGraph`，并设置：

```lua
self.OwnedGraphId = self.OwnedGraph.Id
```

于是外层 Pose Graph 中的 StateMachine 节点可以引用保存 Entry、State 和 Transition 的内部 Graph。

`NodeContracts.ValidateExport()` 会保证：

- StateMachine 必须具有内部 Graph；
- SequencePlayer 等普通节点不能偷偷拥有内部 Graph。

## 十、ToIR 导出了什么

`ToIR()` 先检查必填 Property 和内部 Graph 所有权，然后返回纯 Lua 表：

```lua
{
    Id = self.Id,
    NodeType = self.NodeType,
    DisplayName = self.DisplayName,
    OwnedGraphId = self.OwnedGraphId,
    Pins = self.Pins,
    Properties = self.Properties,
    DeclarationOrder = self.DeclarationOrder,
    SourceLocation = self.SourceLocation,
}
```

以下编译期对象不会导出：

- `Graph` Lua 对象引用；
- `Contract`；
- `PinObjects`；
- `PropertyNames`；
- `bIsSealed`。

它们只帮助 Lua 构建和检查 IR，不属于 C++ IR 数据协议。

## 十一、基类与具体节点类

`LuaAnimNode` 提供所有节点共有的身份、Pin、Property 和导出机制。具体节点子类主要提供明确的 NodeType 与 Rider 类型补全：

| Lua 类 | NodeType | 增加的业务字段或行为 |
|---|---|---|
| `LuaSequencePlayerNode` | `SequencePlayer` | `Pose`、Sequence、循环、速率、开始位置 |
| `LuaInertializationNode` | `Inertialization` | `Source` 和 `Pose` |
| `LuaSaveCachedPoseNode` | `SaveCachedPose` | `Pose` 和 `CacheName` |
| `LuaUseCachedPoseNode` | `UseCachedPose` | `Pose` 和 `CacheName` |
| `LuaStateMachineNode` | `StateMachine` | 内部 Graph、State、Entry、Transition |

这些 Lua 子类仍然不是原生 AnimNode 子类。它们是不同原生节点类型的编译期声明类。

## 十二、完整 SequencePlayer 例子

```lua
local player = graph:SequencePlayer("IdlePlayer")
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true
player.PlayRate = 1.0
graph.Result:Connect(player.Pose)
```

节点部分的数据流：

```text
graph:SequencePlayer()
    -> LuaSequencePlayerNode:New()
    -> NodeType = SequencePlayer
    -> LuaAnimNode:Initialize()
    -> 生成稳定 ID
    -> 验证 NodeContracts
    -> 创建 player.Pose
    -> Graph:AddNode() 分配顺序并封闭节点

直接赋值
    -> __newindex
    -> IRValue.From()
    -> SetProperty()

Graph:ToIR()
    -> player:ToIR()
    -> SekiroAnimIRNode
    -> C++ 创建 UAnimGraphNode_SequencePlayer
```

## 十三、相关问答

### LuaAnimNode 是 UE AnimNode 的 Lua 子类吗？

不是。它不进入 AnimInstance Proxy，不参与多线程 Update/Evaluate，也不持有运行时 Pose。它是生成 UE AnimGraph 节点的编译期描述对象。

### 为什么 Property 不直接保存在 `player.Sequence`？

C++ IR 需要属性名称、显式类型、值和声明顺序。统一写入 `Properties` 数组可以确定性导出和验证，同时保留业务层直接赋值的简洁写法。

这也意味着当前 Property 是只写声明：执行 `player.PlayRate = 1.0` 后再读取 `player.PlayRate`，不会从 `Properties` 反查该值，通常会得到 `nil`。业务 Graph 应一次性声明节点配置，不应把节点 Property 当作普通可读写 Lua 状态。

### 为什么不允许重复设置同一个 Property？

节点声明被视为确定性的编译描述，而不是可变运行时对象。拒绝重复声明可以避免不同代码段按执行顺序覆盖配置，保持生成结果可追踪。

### `CreateNode()` 和具体节点构造函数有什么区别？

`CreateNode(name, node_type)` 可以创建任意已注册通用节点，但返回类型只能是基础 `LuaAnimNode`。`SequencePlayer()` 等具体构造函数返回精确子类，Rider 可以补全正确 Pin 和属性，因此业务代码优先使用具体构造函数。

### `assign_node_field()` 最后不是调用了 `rawset(instance, key, value)` 吗？

是，但只有“该键不是注册 Property，并且节点尚未封闭”时才会走到最后的 `rawset`。

注册 Property 会在前一个分支中转换为 IR 并立即返回：

```lua
if registered_property ~= nil then
    instance:SetProperty(key, IRValue.From(registered_property.ValueType, value))
    return
end
```

因此 `player.Sequence = ...` 和 `player.PlayRate = ...` 不会执行最后的 `rawset`。最后的 `rawset` 主要服务 `Initialize()`，用于保存 `Graph`、`Name`、`Id`、`Contract`、`Pins` 等编译器内部字段。

完整分支是：

| 情况 | 结果 |
|---|---|
| 注册 Property | 写入 `Properties`，随后 `return` |
| 未注册字段，节点未封闭 | 使用 `rawset` 保存内部字段 |
| 未注册字段，节点已封闭 | `assert` 报错，不执行 `rawset` |
| 键已经存在于实例 | Lua 不调用 `__newindex`，直接覆盖原值 |

最后一种情况也是当前封闭机制不能保护已有内部字段的原因。

## 十四、当前实现限制

- 节点封闭只能阻止新增的未注册字段，不能阻止覆盖已经存在的内部字段；
- 注册 Property 写入 IR 数组后不支持从同名字段读取；
- Property 采用一次性声明语义，不能重复赋值或覆盖；
- Lua 层的 Node Contract 是 C++ 注册表镜像，仍有双份契约同步成本；
- Lua 节点只描述编译结果，无法直接查询运行时原生节点播放状态。

前两项可以通过独立代理表或统一的 `__index`/`__newindex` 存储层增强，但会增加元表复杂度。当前实现优先保证动画蓝图声明简洁、导出确定和错误尽早暴露。
