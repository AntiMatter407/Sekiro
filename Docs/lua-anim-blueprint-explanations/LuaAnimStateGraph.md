# `LuaAnimStateGraph.lua` 讲解

源文件：`Content/Script/Animation/Compiler/LuaAnimStateGraph.lua`

## 一句话定位

`LuaAnimStateGraph` 表示一个 State 内部真正计算 Pose 的编译期 Graph，对应 UE 的 `UAnimationStateGraph`。

业务函数：

```lua
function MinimalLocomotion.StateGraph_Move(Graph)
    local Player = Graph:SequencePlayer("MovePlayer")
    Player.Sequence = AnimAssets.Move
    Graph.Result:Connect(Player.Pose)
end
```

这里显式参数 `Graph` 就是 `LuaAnimStateGraph`，没有隐式 `self`，也不是 AnimInstance。

## 为什么这个类很短

`LuaAnimStateGraph` 直接继承 `LuaAnimGraph`：

```lua
local LuaAnimStateGraph = LuaAnimGraph:Extend(
    "LuaAnimStateGraph")
```

普通 Pose Graph 与 StatePose Graph 都是 Pose 节点网络，所以 StateGraph 可以直接复用：

- Node 注册；
- Pin 创建和连接；
- Link 验证；
- SequencePlayer、StateMachine、Inertialization；
- Cached Pose；
- 节点封闭和 IR 导出。

这个子类只需要固定“我是 State 内部 Graph”以及“我的根节点是 State Result”。

## `Initialize()`

```lua
config.GraphType = "StatePose"
config.RootNodeName = "StateResult"
config.RootNodeType = "StateResult"
config.RootNodeDisplayName = "State Result"
LuaAnimGraph.Initialize(self, config)
```

### `GraphType = "StatePose"`

GraphType 决定哪些 Node 可以放入该 Graph。`NodeContracts` 和 C++ 注册表都会检查节点的允许范围。

例如：

- `SequencePlayer` 允许放在 `Pose` 和 `StatePose`；
- `StateMachine` 允许放在 `Pose` 和 `StatePose`，所以支持子状态机；
- `StateResult` 只能作为 `StatePose` 的固定根节点；
- `OutputPose` 只能作为主 `Pose` Graph 的固定根节点。

### 固定 `StateResult`

父类初始化会根据配置自动创建根节点：

```text
NodeType: StateResult
Pin: Result
Direction: Input
DataType: Pose
```

因此业务函数直接使用：

```lua
Graph.Result:Connect(Player.Pose)
```

这对应动画蓝图 State 内部的 `State Result` 节点。

`Result` 只允许一个上游连接。复杂状态动画需要先通过 Blend、Inertialization、子状态机等节点合成一条最终 Pose，再连接到 `Graph.Result`。

## 与主 AnimGraph 的差别

| 项目 | 主 `LuaAnimGraph` | `LuaAnimStateGraph` |
|---|---|---|
| GraphType | `Pose` | `StatePose` |
| 根节点类型 | `OutputPose` | `StateResult` |
| UE Graph | `UAnimationGraph` | `UAnimationStateGraph` |
| 所有者 | Layer 根或其他入口 | 一个 `LuaAnimState` |
| 业务入口 | `AnimGraph(Graph)` | `StateGraph_<State>(Graph)` |

它们的节点和 Pose Link 机制相同，生命周期和所有权不同。

## 创建过程

`LuaAnimStateMachineGraph:State("Move")` 会立即：

1. 生成 Move State 的稳定 ID；
2. 从 State ID 派生 StatePose Graph ID；
3. 创建 `LuaAnimStateGraph`；
4. 自动创建 `StateResult` 根节点；
5. 将 Graph 登记到当前 Layer；
6. 创建引用该 Graph 的 `LuaAnimState`；
7. `LuaAnimBlueprint` 查找并调用 `StateGraph_Move(Graph)` 填充节点。

因此业务代码不应该手动 `New()` 一个 StateGraph，也不需要创建 State Result。

## IR 导出

它沿用 `LuaAnimGraph:ToIR()`，导出：

- `GraphType = "StatePose"`；
- `RootNodeId` 指向自动创建的 StateResult；
- 状态内部 Nodes；
- 状态内部 Pose Links；
- 声明顺序和源码位置。

State 本身通过 `GraphId` 引用这份 IR Graph。

## C++ 生成过程

C++ 创建 `UAnimStateNode` 时，UE 会自动为它创建 `UAnimationStateGraph` 和默认 `UAnimGraphNode_StateResult`。

Factory 不另造一张无关 Graph，而是：

1. 取得 `StateNode->BoundGraph`；
2. 确认它是 `UAnimationStateGraph`；
3. 设置稳定 Guid 和 State 名；
4. 复用默认 State Result；
5. 按 StatePose IR 创建原生 AnimGraph Node；
6. 通过 AnimationGraph Schema 建立 Pose Link。

这与手动在 UE 动画蓝图中打开 State 并编辑内部 Graph 的结果一致。

## 嵌套状态机

因为 `StateMachine` Node 允许位于 `StatePose` Graph，可以写：

```lua
function Locomotion.StateGraph_Grounded(Graph)
    local SubMachine = Graph:StateMachine(
        "StandingLocomotion",
        StandingLocomotion)
    Graph.Result:Connect(SubMachine.Pose)
end
```

这表示 `Grounded` State 的 Pose 完全来自一个子状态机。子状态机仍拥有自己的内部 StateMachine Graph，各层所有权通过稳定 ID 连接。

## 编译期与运行时

`LuaAnimStateGraph` 只用于生成 Graph。进入 PIE 后：

- SequencePlayer 等变成 UE 原生 AnimNode；
- Pose Link 变成原生 `FPoseLink`；
- 当前 State 激活时，由 UE 更新和求值这张节点网络；
- Lua `StateGraph_*(Graph)` 不会每帧执行；
- `Graph` 参数不会在运行时传给 AnimInstance。

## 当前边界

1. 只能放置 `StatePose` 契约允许的节点。
2. 最终必须恰好有一条有效 Pose 连接到 `Graph.Result`。
3. 一个 StatePose Graph 必须恰好属于一个 State。
4. 构造时会修改内部传入的 `config`，业务代码不应直接构造或复用该配置表。
5. 它目前拥有的节点能力取决于 Node Registry，尚未注册的原生动画节点不能仅靠 Lua 名称直接使用。

## 相关问答

### `StateGraph_Move(Graph)` 会在 PIE 中更新 Move 动画吗？

不会。它只在编辑器编译阶段声明 Move State 内有哪些节点以及如何连接。PIE 中实际更新的是生成后的原生节点。

### 为什么根节点叫 `StateResult`，不是 `OutputPose`？

因为它不是整张 AnimBlueprint 的最终输出，而是某个 State 的 Pose 返回值。UE 对这两种 Graph 使用不同的原生 Result 节点和所有权生命周期。

### 这里还能放子状态机吗？

可以。`StateMachine` 的节点契约允许 `StatePose`，因此一个 State 可以把 Pose 委托给另一个状态机。
