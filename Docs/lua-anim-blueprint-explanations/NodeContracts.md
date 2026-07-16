# NodeContracts

## 一、NodeContracts 是什么

`NodeContracts.lua` 是 C++ AnimGraph 节点注册表在 Lua 编译器前端的一份契约镜像。

它回答以下问题：

- 当前支持哪些 `NodeType`；
- 节点允许放在哪种 Graph 中；
- 节点是否只能作为 Graph 固定根节点；
- 节点是否必须拥有内部 Graph；
- 节点有哪些 Pin，Pin 的方向和数据类型是什么；
- 节点允许设置哪些 Property，Property 的类型是什么，是否必填。

它不负责创建 UE 节点，也不负责运行时 Pose 求值。实际节点类、Pin 和属性规则的最终权威仍然是 C++ `FSekiroAnimGraphNodeRegistry`。

## 二、为什么 Lua 还要保存一份镜像

Lua 在构造 IR 时尚未进入 C++ NodeFactory。如果没有前端契约，下面的问题只能等到完整 IR 导入 C++ 后才会发现：

- 把 `SequencePlayer` 放进不支持的 Graph；
- 把输出 Pin 当作输入 Pin 使用；
- 拼错 `Sequence` 或 `PlayRate`；
- 忘记给 `SequencePlayer` 设置必填的动画资源；
- 创建 `StateMachine` 节点却没有为它建立内部状态机 Graph；
- 给普通节点强行附加内部 Graph。

`NodeContracts` 在 Lua 编译阶段立即报错，使错误位置更接近原始 Graph 代码。

代价是 Lua 和 C++ 同时保存契约，二者必须保持一致。因此当前原则是：

- C++ 注册表是唯一权威；
- Lua 表只是补全和快速失败用的镜像；
- C++ 导入器和 Validator 仍会再次验证，不信任 Lua 镜像。

## 三、一个节点契约的结构

以 `SequencePlayer` 为例：

```lua
SequencePlayer = {
    NodeType = "SequencePlayer",
    GraphTypes = {
        Pose = true,
        StatePose = true,
    },
    Pins = {
        {
            Name = "Pose",
            Direction = "Output",
            DataType = "Pose",
            bAllowMultipleConnections = true,
        },
    },
    Properties = {
        { Name = "Sequence", ValueType = "SoftObjectPath", bRequired = true },
        { Name = "bLoopAnimation", ValueType = "Bool", bRequired = false },
        { Name = "PlayRate", ValueType = "Float", bRequired = false },
        { Name = "StartPosition", ValueType = "Float", bRequired = false },
    },
}
```

它表达的是：

- 注册名是 `SequencePlayer`；
- 可以放在主 Pose Graph 或状态内部的 StatePose Graph；
- 有一个叫 `Pose` 的输出 Pin；
- `Pose` 可以连接到多个下游节点；
- `Sequence` 必须设置，并按软对象路径导入；
- 循环、播放速率和开始时间是可选属性。

这里没有保存 SequencePlayer 的播放状态，也不包含 `Update_AnyThread` 或 `Evaluate_AnyThread`。那些行为属于 C++ 原生 AnimNode。

## 四、Contract 字段含义

### NodeType

C++ NodeFactory 使用的稳定类型名，例如：

```text
SequencePlayer
StateMachine
Inertialization
SaveCachedPose
```

它不是 UE C++ 类名。C++ 注册表会把它映射到具体编辑器节点类，例如 `SequencePlayer` 对应 `UAnimGraphNode_SequencePlayer`。

### GraphTypes

节点允许出现的 Graph 类型集合：

- `Pose`：动画蓝图或动画层的主 Pose Graph；
- `StatePose`：状态机中某个 State 独占的 Pose Graph；
- `StateMachine`：状态机拓扑 Graph。

例如 `SaveCachedPose` 当前只允许位于 `Pose` Graph，`SequencePlayer` 同时允许 `Pose` 和 `StatePose`。

### RootGraphType

非空时表示节点是某种 Graph 的固定根节点：

- `OutputPose.RootGraphType = "Pose"`；
- `StateResult.RootGraphType = "StatePose"`。

业务代码不能手动创建这些根节点，它们由 Graph 构造过程自动建立。

### OwnedGraphType

非空时表示节点必须拥有一个内部 Graph。

当前 `StateMachine` 节点必须拥有一个 `StateMachine` Graph。节点放在外层 Pose Graph 中，但 State、Transition 和 Entry 拓扑位于它拥有的内部 Graph 中。

### Pins

描述节点的稳定连接接口：

- `Name`：Pin 名；
- `Direction`：`Input` 或 `Output`；
- `DataType`：当前主要是 `Pose`；
- `bAllowMultipleConnections`：是否允许扇出或多个连接。

Lua 只能使用契约存在的 Pin，不能自行添加、删除或修改 Pin。

### Properties

描述 NodeFactory 允许写入的节点属性：

- `Name`：属性稳定名；
- `ValueType`：传给 `IRValue.From()` 的显式 IR 类型；
- `bRequired`：导出节点前是否必须设置。

## 五、当前内置节点

| NodeType | 允许的 Graph | Pin | Property | 特殊约束 |
|---|---|---|---|---|
| `OutputPose` | `Pose` | `Result` 输入 | 无 | `Pose` Graph 固定根节点 |
| `StateResult` | `StatePose` | `Result` 输入 | 无 | `StatePose` Graph 固定根节点 |
| `SequencePlayer` | `Pose`、`StatePose` | `Pose` 输出 | `Sequence`、循环、速率、开始位置 | `Sequence` 必填 |
| `StateMachine` | `Pose`、`StatePose` | `Pose` 输出 | 无 | 必须拥有 `StateMachine` Graph |
| `Inertialization` | `Pose`、`StatePose` | `Source` 输入、`Pose` 输出 | 无 | 普通 Pose 节点 |
| `SaveCachedPose` | `Pose` | `Pose` 输入 | `CacheName` | `CacheName` 必填 |
| `UseCachedPose` | `Pose` | `Pose` 输出 | `CacheName` | `CacheName` 必填 |

## 六、函数逐个说明

### FindProperty

在节点契约的 `Properties` 中查找属性。它主要供 `LuaAnimNode.__newindex` 判断一次直接赋值究竟是节点属性还是普通 Lua 内部字段。

找不到时返回 `nil`，不会报错。

### Require

按 `NodeType` 取得完整节点契约。类型未注册时立即报错：

```text
NodeType 'UnknownNode' is not registered
```

### ValidatePlacement

检查节点是否可以放入当前 Graph，并保护固定根节点：

- `SequencePlayer` 可以放进 `Pose` 和 `StatePose`；
- `OutputPose` 只能由 `Pose` Graph 作为根节点创建；
- 普通节点不能冒充 Graph 根节点。

### CopyPinAssertions

把契约中的 Pin 复制为节点 IR 的 `Pins` 数组。

这些 Pin 是提交给 C++ 的一致性断言，不是由 Lua 定义的新 Pin。C++ 会拿它们和权威注册表再次比较，防止 Lua 与 C++ 契约漂移。

### RequirePin

按照 Pin 名和期望方向查找 Pin。它可以提前发现：

- Pin 名拼写错误；
- 把输入 Pin 用作输出；
- 把输出 Pin 用作输入。

### RequireProperty

要求属性必须存在于节点契约中。与 `FindProperty` 不同，找不到时会报错，适合明确执行属性写入的路径。

### ValidateExport

节点导出 IR 以前执行最后的前端检查：

- 所有 `bRequired = true` 的属性必须已经赋值；
- 要求内部 Graph 的节点必须具有 `OwnedGraphId`；
- 不允许拥有内部 Graph 的节点，其 `OwnedGraphId` 必须为空。

## 七、一次 SequencePlayer 声明如何经过契约

业务代码：

```lua
local player = graph:SequencePlayer("IdlePlayer")
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true
graph.Result:Connect(player.Pose)
```

编译过程：

```text
创建 SequencePlayer
    -> NodeContracts.Require("SequencePlayer")
    -> ValidatePlacement() 检查 GraphType
    -> CopyPinAssertions() 创建 Pose Pin 断言

player.Sequence = ...
    -> LuaAnimNode.__newindex
    -> FindProperty("Sequence")
    -> IRValue.From("SoftObjectPath", value)
    -> LuaAnimNode:SetProperty()

连接 player.Pose
    -> RequirePin() 检查 Pin 名、方向和数据类型

导出节点
    -> ValidateExport()
    -> 检查 Sequence 已设置
    -> 生成 SekiroAnimIRNode

C++ 导入
    -> 再与 FSekiroAnimGraphNodeRegistry 权威契约核对
    -> NodeFactory 创建原生 SequencePlayer 编辑器节点
```

## 八、常见错误示例

### 忘记设置必填动画

```lua
local player = graph:SequencePlayer("IdlePlayer")
```

导出时会报告 `SequencePlayer` 缺少 `Sequence` Property。

### 拼错属性名

```lua
player.PlaySpeed = 1.0
```

节点密封以后，该字段既不是注册属性，也不是编译器内部字段，因此会立即失败，而不会悄悄生成无效配置。

### 错误连接方向

```lua
player.Pose:Connect(graph.Result)
```

正确规则是目标输入 Pin 连接来源输出 Pin：

```lua
graph.Result:Connect(player.Pose)
```

### 手工创建根节点

业务 Graph 不应主动创建 `OutputPose` 或 `StateResult`。Graph 基类会自动创建并持有正确的根节点。

## 九、当前设计边界

Lua `contracts` 表和 C++ `FSekiroAnimGraphNodeRegistry` 存在重复数据，这是当前实现刻意接受的前端镜像成本。新增 NodeType 时必须同时更新二者，并通过一致性测试保证字段、顺序和类型相同。

后续更稳妥的方向，是让 C++ 注册表自动导出只读契约给 Lua，或在构建阶段生成 `NodeContracts.lua`。这样 Lua 仍然保留补全和快速失败能力，同时消除手工同步风险。
