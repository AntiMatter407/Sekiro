# `LuaAnimState.lua` 讲解

源文件：`Content/Script/Animation/Compiler/LuaAnimState.lua`

## 一句话定位

`LuaAnimState` 是状态机内部一个 State 顶点的编译期记录。

它不保存当前是否激活、状态时间或混合权重，而是保存生成 UE 原生 State 所需的静态信息：

- State 的稳定身份和显示名；
- 所属 `LuaAnimStateMachineGraph`；
- 独占的 `LuaAnimStateGraph`；
- State 到 StatePose Graph 的稳定 `GraphId` 引用；
- `bAlwaysResetOnEntry` 设置；
- 声明顺序和源码位置。

## 它在结构中的位置

```text
LuaAnimStateMachineGraph
    |
    +-- LuaAnimState "Idle"
            |
            +-- PoseGraph: LuaAnimStateGraph 对象
            +-- GraphId: StatePose Graph 稳定 ID
```

`LuaAnimState` 是拓扑中的顶点；`LuaAnimStateGraph` 是双击该 State 后看到的内部 Pose Graph。两者不是同一个对象。

## `Initialize()`

`LuaAnimStateMachineGraph:State()` 会先创建 StatePose Graph，再把它传给 `LuaAnimState:New()`：

```lua
local State = LuaAnimState:New({
    MachineGraph = MachineGraph,
    Id = StateId,
    Name = "Idle",
    PoseGraph = IdleGraph,
    Settings = Settings,
    DeclarationOrder = 0,
    SourceLocation = SourceLocation,
})
```

### `MachineGraph`

```lua
self.MachineGraph = assert(config.MachineGraph, ...)
```

这是 State 所属的内部状态机 Graph。它只在 Lua 编译期用于表达所有权，不会写入 State IR。

### `PoseGraph` 与 `GraphId`

```lua
self.PoseGraph = assert(config.PoseGraph, ...)
self.GraphId = self.PoseGraph.Id
```

两个字段用途不同：

| 字段 | 类型 | 用途 |
|---|---|---|
| `PoseGraph` | `LuaAnimStateGraph` | 编译期填写 SequencePlayer、StateMachine 和 Pose Link |
| `GraphId` | `string` | 导出后让 C++ 找到该 State 的原生 Bound Graph 内容 |

一个 State 必须独占一个 StatePose Graph。C++ Validator 会检查 `GraphId`：

- 指向的 Graph 必须存在；
- 必须在同一 Layer；
- `GraphType` 必须是 `StatePose`；
- 必须恰好只有一个 State 拥有它。

### `bAlwaysResetOnEntry`

```lua
self.bAlwaysResetOnEntry = config.Settings ~= nil
    and config.Settings.bAlwaysResetOnEntry == true
```

只有显式传入 `true` 才会开启，否则为 `false`。C++ 会原样写入：

```cpp
StateNode->bAlwaysResetOnEntry = State.bAlwaysResetOnEntry;
```

它采用 UE 原生 State 的重入重置语义。开启后，重新进入该 State 时会要求重置其状态节点执行状态；是否需要开启应按动画连续性决定，不应作为默认选项滥用。

当前底层写法可以传入：

```lua
Machine:State("Idle", nil, {
    bAlwaysResetOnEntry = true,
})
```

但项目业务规范优先保持 `Machine:State("Idle")` 简洁；确实需要重入重置时才提供设置。

### `DeclarationOrder`

State 声明顺序从 `0` 开始记录，用于生成确定、可复现的 IR 和编辑器布局。它不是运行时状态优先级。

### `SourceLocation`

保存声明该 State 的 Lua 模块和行号。StatePose Graph 缺失、类型错误或多重所有时，诊断可以指回业务声明位置。

## `ToIR()`

`ToIR()` 只输出跨 Lua/C++ 边界需要的值：

```lua
{
    Id = self.Id,
    Name = self.Name,
    GraphId = self.GraphId,
    bAlwaysResetOnEntry = self.bAlwaysResetOnEntry,
    DeclarationOrder = self.DeclarationOrder,
    SourceLocation = self.SourceLocation,
}
```

`MachineGraph` 和 `PoseGraph` 是 Lua 对象引用，不能直接序列化，所以不会导出。它们的关系分别由所在 `States` 数组和 `GraphId` 表达。

## C++ 对应关系

C++ 为每条 `FSekiroAnimIRState` 创建：

- 一个 `UAnimStateNode`；
- 该节点自动拥有的 `UAnimationStateGraph`；
- 稳定的 `NodeGuid` 和 Graph Guid；
- State 名称；
- `bAlwaysResetOnEntry` 设置。

随后根据 `GraphId` 把对应 `StatePose` IR Graph 物化到该 `UAnimationStateGraph` 中。

运行时再由 UE 编译为原生状态机中的 State 描述和该状态的 AnimNode 网络。Lua `LuaAnimState` table 本身不会保留到 PIE。

## 相关问答

### `LuaAnimState` 是不是运行时当前状态？

不是。它是编译期 State 声明。当前状态、状态时间、权重和过渡进度由运行时 `FAnimNode_StateMachine` 管理。

### 为什么同时保留 `PoseGraph` 和 `GraphId`？

`PoseGraph` 供 Lua 编译器继续构建对象；`GraphId` 供 IR 和 C++ 跨语言引用。它们分别服务对象阶段和序列化阶段。

### 这里的 `self` 是什么？

是当前 `LuaAnimState` 编译器实例。业务 `StateGraph_*(Graph)` 不会接收它，只会收到该 State 的 `LuaAnimStateGraph`。
