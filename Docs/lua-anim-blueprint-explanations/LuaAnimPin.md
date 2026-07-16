# LuaAnimPin

## 一、LuaAnimPin 是什么

`LuaAnimPin` 是 AnimGraph 编译期间使用的具名 Pin 句柄。

业务代码通过它写出接近动画蓝图连线的表达：

```lua
graph.Result:Connect(player.Pose)
```

其中：

- `graph.Result` 是 `OutputPose.Result` 输入 Pin；
- `player.Pose` 是 `SequencePlayer.Pose` 输出 Pin；
- `Connect()` 表示把右侧来源 Pose 接入左侧目标 Pin。

`LuaAnimPin` 不保存骨骼姿势、不执行动画计算，也不是 UE 的 `UEdGraphPin`。它只在 Lua 编译期保存端点身份，并请求所属 Graph 生成一条 Link IR。

## 二、Pin 从哪里产生

业务代码不会直接调用 `LuaAnimPin:New()`。

创建 `LuaAnimNode` 时，节点会从 `NodeContracts` 取得 Pin 契约，并为每个契约 Pin 创建一个 `LuaAnimPin`：

```text
NodeContracts.SequencePlayer.Pins
    -> { Name = "Pose", Direction = "Output", DataType = "Pose" }
    -> LuaAnimPin 实例
    -> player.Pose
```

节点使用 Pin 名把对象直接挂到自身字段，因此 Rider 和业务代码可以使用：

```lua
player.Pose
inertialization.Source
inertialization.Pose
graph.Result
```

Pin 的名称、方向和类型都来自注册契约。业务代码不能自行修改或添加 Pin。

## 三、LuaAnimPin 保存的字段

### Node

Pin 所属的 `LuaAnimNode`。生成 Link 时通过它取得节点稳定 ID 和节点契约。

### Graph

Pin 所属 Graph，实际等于 `Node.Graph`。它用于阻止跨 Graph 直接连接。

### Name

C++ 节点契约中的稳定 Pin 名，例如：

- `SequencePlayer.Pose`；
- `Inertialization.Source`；
- `OutputPose.Result`。

它不是编辑器中可以随意修改的显示文字。

### Direction

数据流方向：

- `Input`：接收数据；
- `Output`：提供数据。

### DataType

Pin 的稳定数据类型。当前动画节点主要使用 `Pose`，后续支持数值、布尔值或其他数据 Pin 时也由该字段阻止错误类型连接。

`bAllowMultipleConnections` 没有复制到 `LuaAnimPin` 实例，因为连接数量规则仍由 `NodeContracts` 持有，并在 Graph 创建 Link 时检查。

## 四、Initialize 做了什么

`Initialize(config)` 只保存 Pin 元数据：

```lua
self.Node = config.Node
self.Graph = self.Node.Graph
self.Name = config.Name
self.Direction = config.Direction
self.DataType = config.DataType
```

每项都必须存在。该阶段不会创建 Link，也不会访问 UE UObject。

## 五、Connect 为什么写在输入 Pin 上

接口设计为：

```lua
目标输入:Connect(来源输出)
```

例如：

```lua
graph.Result:Connect(player.Pose)
```

可以按赋值关系理解：

```text
graph.Result <- player.Pose
```

这和状态结果节点的“Result 输入接收上游 Pose”语义一致，也让一条语句明确区分目标和来源。

`Connect()` 首先检查：

1. `self` 必须是输入 Pin；
2. 参数必须是输出 Pin；
3. 然后把两个 Pin 交给 `Graph:LinkPins()`。

因此下面的方向是错误的：

```lua
player.Pose:Connect(graph.Result)
```

因为 `player.Pose` 是输出 Pin，不能作为 `Connect()` 的目标。

## 六、Graph 继续完成哪些检查

`LuaAnimPin:Connect()` 只做最直接的方向检查，其余规则交给所属 Graph：

### LinkPins

检查：

- 两个 Pin 是否属于同一个 Graph；
- `DataType` 是否一致。

跨 Graph 不能直接连线。状态 Graph、主 Pose Graph 和其他动画层之间需要通过状态机节点、缓存姿势或层接口等明确边界传递 Pose。

### Link

根据 `NodeContracts` 再次检查：

- 来源 Pin 确实是注册的输出 Pin；
- 目标 Pin 确实是注册的输入 Pin；
- Pin 是否允许重复连接；
- 同一个单连接输入是否已经被占用。

验证通过后，Graph 创建 `SekiroAnimIRLink`：

```lua
{
    Id = ".../Link/IdlePlayer.Pose->Output.Result",
    Source = {
        NodeId = player.Id,
        PinName = "Pose",
    },
    Target = {
        NodeId = graph.OutputNode.Id,
        PinName = "Result",
    },
    DeclarationOrder = 0,
    SourceLocation = { ... },
}
```

Link 存放在 `graph.Links` 中，而不是存放在两个 `LuaAnimPin` 对象里。

## 七、四种容易混淆的 Pin 与 Link

| 概念 | 所在层 | 作用 |
|---|---|---|
| `LuaAnimNodePinContract` | Lua 契约 | 定义某种 NodeType 应该有哪些 Pin |
| `LuaAnimPin` | Lua 编译对象 | 给业务代码提供 `player.Pose` 和 `Connect()` |
| `SekiroAnimIRPin` | 导出 IR | 向 C++ 声明 Lua 所依据的完整 Pin 契约快照 |
| `SekiroAnimIRLink` | 导出 IR | 记录两个具体节点端点之间的一条连接 |

C++ 导入和验证完成后，NodeFactory 才会创建或连接真正的 UE 编辑器节点和 Pin。生成的动画蓝图编译后，运行时再由原生 AnimNode/`FPoseLink` 完成 Pose 传递。

因此 `LuaAnimPin` 与 `FPoseLink` 也不是同一个对象：

- `LuaAnimPin` 描述编译期 Graph 端口；
- `SekiroAnimIRLink` 描述编译期连线；
- `FPoseLink` 是生成节点在运行时使用的原生 Pose 引用。

## 八、完整例子

```lua
local player = graph:SequencePlayer("IdlePlayer")
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true

local inertialization = graph:Inertialization("FinalInertialization")
inertialization.Source:Connect(player.Pose)
graph.Result:Connect(inertialization.Pose)
```

对应结构：

```text
SequencePlayer.Pose (Output)
    -> Inertialization.Source (Input)

Inertialization.Pose (Output)
    -> OutputPose.Result (Input)
```

最终生成两条 Link IR，节点 Pose 数据在生成后的原生 AnimGraph 中沿相同方向流动。

## 九、它不负责什么

`LuaAnimPin` 不负责：

- 创建 Node；
- 保存 Graph 的 Link 数组；
- 判断 Pin 是否允许多连接；
- 加载动画资产；
- 保存或混合 Pose；
- 执行 `Initialize_AnyThread`、`Update_AnyThread` 或 `Evaluate_AnyThread`；
- 表示状态机 Transition。

状态机 Transition 是 State 之间的有向拓扑及规则，不是 Pose Pin 之间的 Graph Link。

## 十、相关问答

### 为什么不写成 `player.Pose:ConnectTo(graph.Result)`？

当前 API 强调“目标接收来源”，与属性赋值方向一致，也能统一所有输入节点的写法。两种 API 都能实现相同功能，但项目只保留一种写法可减少方向混淆。

### 一个输出 Pose 能连接多个输入吗？

取决于契约的 `bAllowMultipleConnections`。当前 SequencePlayer、StateMachine、Inertialization 和 UseCachedPose 的 Pose 输出允许扇出；结果节点等 Pose 输入默认只允许一条连接。

### 可以连接两个不同 Graph 的 Pin 吗？

不能。`Graph:LinkPins()` 明确要求两端属于同一个 Graph。跨 Graph 数据流必须通过拥有关系和专门节点表达，不能直接制造悬空连线。
