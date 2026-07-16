# `LuaAnimStateMachineGraph.lua` 讲解

源文件：`Content/Script/Animation/Compiler/LuaAnimStateMachineGraph.lua`

## 一句话定位

`LuaAnimStateMachineGraph` 是 `LuaStateMachineNode` 独占的**状态机拓扑容器**。

它只保存：

- Entry 指向哪个 State；
- 有哪些 State；
- State 之间有哪些有向 Transition；
- 每个 State 和 Transition 对应的稳定引用及编译设置。

它不保存 SequencePlayer 等动画节点，也不能连接 Pose Pin。每个 State 的具体动画节点位于该 State 独占的 `LuaAnimStateGraph` 中。

## 与普通 AnimGraph 的区别

| 对比项 | `LuaAnimGraph` | `LuaAnimStateMachineGraph` |
|---|---|---|
| 表示内容 | Pose 节点网络 | 状态机拓扑 |
| 基类 | `CompilerClass` | `CompilerClass` |
| Node/Pin/Link | 有 | 没有 |
| Result 节点 | `OutputPose` 或 `StateResult` | 没有 |
| 专用内容 | `Nodes`、`Links`、`RootNodeId` | `EntryStateId`、`States`、`Transitions` |
| 能否直接输出 Pose | 能 | 不能 |

两者都直接继承 `CompilerClass`。`LuaAnimStateMachineGraph` 没有继承 `LuaAnimGraph`，因为状态之间的箭头不是 `FPoseLink`，Entry 也不是 Pose Result。

一个完整状态机的结构是：

```text
LuaStateMachineNode
    |
    +-- OwnedGraph: LuaAnimStateMachineGraph
            |
            +-- EntryStateId
            +-- State Idle
            |       +-- GraphId -> Idle LuaAnimStateGraph
            +-- State Move
            |       +-- GraphId -> Move LuaAnimStateGraph
            +-- Transition Idle_Move
                    +-- SourceStateId -> Idle
                    +-- TargetStateId -> Move
```

## 配置和主要字段

创建内部 Graph 时必须传入：

| 字段 | 类型 | 含义 |
|---|---|---|
| `Blueprint` | `LuaAnimBlueprint` | 当前动画蓝图编译实例 |
| `Layer` | `LuaAnimLayer` | 当前状态机所属动画层 |
| `OwnerNode` | `LuaStateMachineNode` | 唯一拥有该内部 Graph 的外层节点 |
| `SourceLocation` | `SekiroAnimIRSourceLocation` | 声明位置，用于诊断 |

初始化后维护两组数据：

### 导出数据

| 字段 | 用途 |
|---|---|
| `EntryStateId` | Entry 目标 State 的稳定 ID |
| `States` | 按声明顺序保存 `LuaAnimState` |
| `Transitions` | 按声明顺序保存 `LuaAnimTransition` |

### 编译期索引

| 字段 | 用途 |
|---|---|
| `StateNames` | 检查 State 名是否重复并按名称查找 State |
| `TransitionKeys` | 检查 Transition Key 是否重复 |

`StateNames` 和 `TransitionKeys` 不会导出到 IR，它们只是 Lua 前端快速验证使用的索引。

## `Initialize()`

### 建立所有权

```lua
self.Blueprint = assert(config.Blueprint, ...)
self.Layer = assert(config.Layer, ...)
self.OwnerNode = assert(config.OwnerNode, ...)
```

这里的 `self` 是正在构造的 `LuaAnimStateMachineGraph` 编译器对象。它不是业务状态机定义，也不是 AnimInstance。

`OwnerNode` 是强制字段。C++ Validator 还会最终确认：

- 该 Graph 存在一个所有者；
- 所有者只能有一个；
- 所有者必须是 StateMachine Node；
- Node 和内部 Graph 必须位于同一个 Layer；
- Graph 所有权不能形成循环。

### 生成名称和稳定 ID

```lua
self.Name = self.OwnerNode.Name .. "Graph"
self.Id = IRSchema.MakeStableId(
    self.OwnerNode.Id,
    "Graph",
    "StateMachine")
```

假设外层节点叫 `GroundLocomotion`，内部 Graph 的显示名称就是 `GroundLocomotionGraph`。ID 从外层节点 ID 派生，因此同一个状态机节点重新编译时仍能获得稳定身份。

### 初始化空拓扑

```lua
self.EntryStateId = ""
self.States = {}
self.Transitions = {}
self.StateNames = {}
self.TransitionKeys = {}
```

空字符串表示 Entry 尚未设置。最终导出前必须至少设置一个有效 Entry，否则 C++ Validator 会报告 `MissingEntryState`。

## `State()`

业务写法：

```lua
Machine:State("Idle")
```

实际转发到：

```lua
Machine.OwnedGraph:State("Idle")
```

该函数完成的工作不只是向数组插入一个名字。

### 1. 验证语义名

```lua
local valid_name = IRSchema.RequireSemanticName(name, "State")
```

名称必须是有效、非空且规范化的业务语义名。

### 2. 阻止重复 State

```lua
assert(self.StateNames[valid_name] == nil, ...)
```

同一个状态机中不能声明两个同名 State。

### 3. 生成 State 稳定 ID

```lua
local state_id = IRSchema.MakeStableId(
    self.Id,
    "State",
    valid_name)
```

State ID 由内部状态机 Graph ID 和状态名共同决定。

### 4. 创建独占 StatePose Graph

```lua
local pose_graph = LuaAnimStateGraph:New({ ... })
self.Layer:AddGraph(pose_graph)
```

每个 State 都会立即创建自己的 `LuaAnimStateGraph`。例如：

```text
Idle State
    -> Idle StatePose Graph
        -> SequencePlayer
        -> StateResult
```

该 StatePose Graph 同样登记到 Layer 的扁平 Graph 列表中。

### 5. 创建 State 对象

```lua
local state = LuaAnimState:New({
    MachineGraph = self,
    Id = state_id,
    Name = valid_name,
    PoseGraph = pose_graph,
    ...
})
```

`LuaAnimState` 保存 State 身份、显示名、StatePose Graph 引用以及 `bAlwaysResetOnEntry`。

### 6. 保存顺序和索引

```lua
self.StateNames[valid_name] = state
table.insert(self.States, state)
```

`DeclarationOrder = #self.States` 在插入前计算，所以 IR 中声明顺序从 `0` 开始。

### 7. 可选立即构建回调

```lua
if build_function ~= nil then
    build_function(pose_graph)
end
```

这是底层保留能力。当前项目标准不在 `Machine:State()` 中传 Builder，而是分开编写：

```lua
function GroundLocomotion.StateGraph_Idle(Graph)
    -- 只填写 Idle 的 Pose 节点和连接。
end
```

这样拓扑列表和状态动画逻辑更清楚。

## `Entry()`

```lua
function LuaAnimStateMachineGraph:Entry(state_name)
    local valid_name = IRSchema.RequireSemanticName(
        state_name,
        "Entry State")
    self.EntryStateId = IRSchema.MakeStableId(
        self.Id,
        "State",
        valid_name)
end
```

`Entry()` 保存的是目标 State ID，不是 State 对象。

因此允许先写：

```lua
Machine:Entry("Idle")
Machine:State("Idle")
```

调用 `Entry()` 时，`Idle` 还没有加入 `StateNames`，但根据同样的稳定 ID 规则，两次计算最后会指向同一个 State。

这种设计支持先声明入口、后列出状态。C++ Validator 在完整 IR 到齐后检查目标 State 是否真实存在。

当前多次调用 `Entry()` 会让后一次覆盖前一次。业务状态机应只声明一次 Entry。

## `Transition()`

业务写法：

```lua
local idle_to_move = Machine:Transition(
    "Idle_Move",
    "Idle",
    "Move")

idle_to_move.BlendDuration = 0.15
idle_to_move.PriorityOrder = 0
```

### Transition Key

`Key` 是状态机内一条 Transition 的独立身份，不只是显示文字。

它参与：

- Transition 稳定 ID；
- 默认 Lua 规则函数名；
- 重复检查；
- 原生 Transition Graph 名称；
- 错误诊断定位。

同一对 State 可以存在多条 Transition，但每条必须使用不同 Key。

### 源状态和目标状态

```lua
SourceStateId = MakeStableId(GraphId, "State", SourceName)
TargetStateId = MakeStableId(GraphId, "State", TargetName)
```

和 Entry 一样，Transition 只保存端点 ID，因此允许 State 稍后声明。最终 Validator 检查两个端点是否都属于当前状态机。

### 默认规则函数名

```lua
CanEnter_<StateMachineNodeName>_<TransitionKey>
```

例如节点名为 `GroundLocomotion`，Key 为 `Idle_Move`：

```text
CanEnter_GroundLocomotion_Idle_Move
```

独立状态机文件可以只提供局部名称：

```lua
function GroundLocomotion.CanEnter_Idle_Move(Inst)
```

`LuaAnimBlueprint:ConfigureStateMachine()` 会把该函数登记为完整运行时名称。

### 默认 Transition 设置

`LuaAnimTransition` 当前提供：

| 字段 | 默认值 | 含义 |
|---|---:|---|
| `BlendDuration` | `0.2` | 交叉混合时长，单位秒 |
| `PriorityOrder` | Transition 声明顺序 | 同一源 State 下的检查优先级 |
| `BlendMode` | `Linear` | Alpha Blend 模式 |

当前 C++ 生成路径只接受并生成 `Linear`。不要把 `BlendMode` 改为尚未注册支持的值。

同一源 State 的多个 Transition 不能使用相同 `PriorityOrder`，该规则由最终 Validator 检查。

## `ToIR()`

`ToIR()` 先分别导出所有 State 和 Transition，然后生成专用 Graph 结构：

```lua
{
    Id = self.Id,
    Name = self.Name,
    GraphType = "StateMachine",
    RootNodeId = "",
    Nodes = {},
    Links = {},
    StateMachine = {
        EntryStateId = self.EntryStateId,
        States = states,
        Transitions = transitions,
    },
}
```

`RootNodeId`、`Nodes` 和 `Links` 为空不是功能缺失。StateMachine Graph 使用 UE 专用拓扑节点，不使用普通 AnimGraph Pose Link 表达 Entry 和 Transition。

每个 State 的 Pose 网络通过 `State.GraphId` 跳转到另一个 `StatePose` Graph。

## C++ 如何物化拓扑

C++ `BuildStateMachineGraph()` 按以下顺序处理 IR：

1. 遍历 `States`，创建原生 `UAnimStateNode`；
2. 读取 `State.GraphId`，找到对应 StatePose IR Graph；
3. 使用 UE 自动创建的 `UAnimationStateGraph` 作为 State 的 Bound Graph；
4. 设置稳定 Guid、状态名和 `bAlwaysResetOnEntry`；
5. 根据 `EntryStateId` 将原生 Entry 节点连接到目标 State；
6. 遍历 `Transitions`，创建 `UAnimStateTransitionNode`；
7. 根据 `SourceStateId` 和 `TargetStateId` 建立有向连接；
8. 写入混合时长、优先级并生成 Transition Rule Graph；
9. 最后为每个 State 构建其 StatePose 节点网络。

先创建所有 State，再连接 Entry 和 Transition，正是 Lua 前端允许前向引用 State 的原因。

## 编译期与运行时边界

`LuaAnimStateMachineGraph` 只在编辑器编译 Lua 动画蓝图时存在。

运行时：

- UE 原生 `FAnimNode_StateMachine` 保存当前 State、状态权重和过渡进度；
- StatePose Graph 中的原生 AnimNode 计算 Pose；
- Transition Graph 读取 Lua 规则的线程安全缓存；
- `States`、`StateNames` 等 Lua table 不参与每帧更新；
- Lua 不通过本类手动推进当前 State。

## 当前边界

1. 业务代码应通过 `Machine:State()`、`Machine:Entry()` 和 `Machine:Transition()` 使用它，不直接访问 `OwnedGraph`。
2. State 名和 Transition Key 在同一个状态机内必须唯一。
3. `Entry()` 当前允许重复调用并以后一次为准，规范上只调用一次。
4. State 和 Transition 可以前向引用，但错误会推迟到最终 Validator 才报告。
5. `build_function` 是保留接口；新业务代码统一使用显式 `StateGraph_*(Graph)`。
6. 当前原生生成器只支持 `Linear` Transition BlendMode。

## 相关问答

### 为什么它也叫 Graph，却没有 Node 和 Link？

因为 UE 的 StateMachine Graph 表达的是 Entry、State 和 Transition 拓扑，不是 Pose 数据流。真正的 Pose Node 和 `FPoseLink` 位于每个 State 独占的 StatePose Graph 中。

### 为什么 State 声明时就创建 StatePose Graph？

每个 UE State 都必须拥有一个独立 Bound Graph。声明 State 时立即创建并登记对应 Graph，可以从一开始建立稳定的一对一所有权，并阻止不同 State 共享同一张 Pose Graph。

### 为什么 Entry 和 Transition 不直接保存 State 对象？

IR 必须跨 Lua/C++ 边界，Lua 对象引用无法直接序列化。稳定 ID 既可以跨语言传递，又允许 Entry 和 Transition 在 State 声明之前引用目标。

### 这里的 `self` 是什么？

本文件属于编译器内部类，`LuaAnimStateMachineGraph:State()` 等方法中的 `self` 明确是当前内部 Graph 实例。业务状态机回调已经改为无隐式参数的 `StateMachine(Machine)`、`StateGraph_*(Graph)` 和 `CanEnter_*(Inst)`，不会把这里的编译器实例传给业务函数。
