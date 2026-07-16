# `LuaStateMachineNode.lua` 讲解

源文件：`Content/Script/Animation/Compiler/LuaStateMachineNode.lua`

## 一句话定位

`LuaStateMachineNode` 表示**放在 Pose Graph 里的状态机节点**。

它本身像动画蓝图编辑器中的 `State Machine` 方块：

- 对外输出一条 `Pose`；
- 对内独占一个保存 Entry、State 和 Transition 的状态机 Graph；
- 编译后对应 UE 原生 `UAnimGraphNode_StateMachine`；
- 它不是具体角色的状态机业务定义，也不在运行时自己计算 Pose。

## 三个容易混淆的类型

| 类型 | 代表什么 | 主要职责 |
|---|---|---|
| `LuaAnimStateMachine` | 可复用的状态机定义基类 | 让业务文件声明 `StateMachine`、`StateGraph_*` 和 `CanEnter_*` 方法 |
| `LuaStateMachineNode` | 外层 AnimGraph 中的状态机节点 | 输出 Pose，并拥有一个内部状态机 Graph |
| `LuaAnimStateMachineGraph` | 节点内部的状态机拓扑 | 保存 Entry、State、Transition，不直接输出 Pose |

可以把它们理解成：

```text
状态机业务定义 LuaAnimStateMachine
        |
        | 配置
        v
外层 Pose 节点 LuaStateMachineNode
        |
        | OwnedGraph
        v
内部拓扑 LuaAnimStateMachineGraph
        |
        +-- Entry
        +-- State
        +-- Transition
```

`LuaAnimStateMachine` 回答“这个状态机应当有哪些内容”；`LuaStateMachineNode` 回答“外层动画 Graph 中哪里放了一个状态机”；`LuaAnimStateMachineGraph` 则保存“状态机内部实际连接关系”。

## 继承关系与节点契约

```lua
local LuaStateMachineNode = LuaAnimNode:Extend("LuaStateMachineNode")
```

它继承 `LuaAnimNode`，所以自动获得：

- 稳定的节点 `Id`；
- `NodeType`；
- Pin 创建与查询；
- Property 写入；
- 节点封闭与 IR 导出；
- 所属外层 `Graph`。

`NodeContracts.lua` 对 `StateMachine` 节点的契约是：

- 可以放进 `Pose` Graph；
- 可以放进 `StatePose` Graph，因此支持嵌套状态机；
- 必须拥有一个类型为 `StateMachine` 的内部 Graph；
- 对外提供名为 `Pose` 的输出 Pin；
- `Pose` 输出允许连接到多个下游输入。

C++ 注册表使用同一套契约，并把它映射为：

```text
/Script/AnimGraph.AnimGraphNode_StateMachine
```

Lua 契约用于尽早发现声明错误，C++ 注册表和 Validator 仍是最终权威。

## `Initialize()` 做了什么

### 1. 固定节点类型

```lua
config.NodeType = "StateMachine"
LuaAnimNode.Initialize(self, config)
```

调用父类初始化后，这个对象已经是一个合法的 AnimNode，并根据节点契约获得 `Pose` 输出 Pin。

这里会修改调用方传入的 `config` table。当前调用链马上消费该配置，因此不会造成实际问题；不过业务代码不应在构造后继续复用同一张配置表。

### 2. 创建独占的内部 Graph

```lua
self.OwnedGraph = LuaAnimStateMachineGraph:New({
    Blueprint = self.Graph.Blueprint,
    Layer = self.Graph.Layer,
    OwnerNode = self,
    SourceLocation = self.SourceLocation,
})
```

内部 Graph 与节点属于同一个 Blueprint、同一个 Layer，并通过 `OwnerNode = self` 建立明确所有权。

内部 Graph 的稳定 ID 由节点 ID 派生：

```text
StateMachine Node Id
    -> StateMachine Graph Id
        -> State Id
            -> StatePose Graph Id
        -> Transition Id
```

只要业务语义名不变，重新编译就能得到同样的 ID，便于原生节点重建、诊断和调试定位。

### 3. 同时保存对象引用和序列化引用

```lua
self.OwnedGraphId = self.OwnedGraph.Id
```

这两个字段用途不同：

| 字段 | 类型 | 使用阶段 | 用途 |
|---|---|---|---|
| `OwnedGraph` | `LuaAnimStateMachineGraph` 对象 | Lua 编译期 | 调用 `State()`、`Entry()`、`Transition()` 并读取拓扑 |
| `OwnedGraphId` | `string` | IR 与 C++ 导入期 | 让 C++ 从扁平 Graph 列表中找到节点拥有的内部 Graph |

Lua 对象不能直接跨语言序列化，所以导出的 Node 只保留 `OwnedGraphId`。

### 4. 将内部 Graph 登记到 Layer

```lua
self.Graph.Layer:AddGraph(self.OwnedGraph)
```

IR 并不把完整 Graph table 嵌套在 Node 里面。一个 Layer 下的 Graph 都放在统一数组中，Node 通过 ID 引用自己的内部 Graph。

简化后的 IR 关系如下：

```lua
Layer.Graphs = {
    AnimGraph,
    GroundLocomotionGraph,
    IdleStatePoseGraph,
    MoveStatePoseGraph,
}

StateMachineNode.OwnedGraphId = GroundLocomotionGraph.Id
```

这种扁平存储便于 C++ 建立全局 ID 索引，也能统一检查：

- Graph 是否真的存在；
- 是否与拥有它的节点位于同一 Layer；
- 一个内部 Graph 是否恰好只有一个所有者；
- Graph 所有权是否形成循环。

## 为什么 `State()`、`Entry()`、`Transition()` 要包在 Node 上

三个方法都只是把请求转发给 `OwnedGraph`：

```lua
function LuaStateMachineNode:Entry(state_name)
    self.OwnedGraph:Entry(state_name)
end
```

这里的包装不是为了兼容旧接口，而是为了维持正确的使用语义：业务作者拿到的是外层状态机节点，因此应该写：

```lua
machine:Entry("Idle")
machine:State("Idle")
machine:Transition("Idle_Move", "Idle", "Move")
```

而不必越过节点所有权，直接操作：

```lua
machine.OwnedGraph:Entry("Idle")
```

节点是公开的声明句柄，内部 Graph 是节点负责管理的编译器数据结构。

## `State()`

```lua
function LuaStateMachineNode:State(name, build_function, settings)
    return self.OwnedGraph:State(name, build_function, settings)
end
```

它会在内部状态机 Graph 中创建：

1. 一个 State 顶点；
2. 该 State 独占的 `StatePose` Graph；
3. State 到 StatePose Graph 的稳定引用。

`build_function` 仍支持立即构建 State Graph，但当前推荐结构是只声明状态：

```lua
machine:State("Idle")
```

随后由动画蓝图编译器按照命名约定调用：

```lua
function GroundLocomotion.StateGraph_Idle(Graph)
    -- 在这里声明 SequencePlayer 等 Pose 节点。
end
```

这样状态拓扑和状态内部动画逻辑可以清楚分开。

## `Entry()`

```lua
machine:Entry("Idle")
```

它只根据名称计算目标 State 的稳定 ID，因此可以先写 Entry，再声明 State：

```lua
machine:Entry("Idle")
machine:State("Idle")
```

此时 Lua 前端不会立即查表确认 `Idle` 已存在。所有声明完成后，C++ Validator 会检查：

- 是否设置了 Entry；
- Entry 指向的 State 是否真实存在。

这让业务代码可以按照“先看到入口，再看到状态列表”的自然顺序书写。

## `Transition()`

```lua
machine:Transition("Idle_Move", "Idle", "Move", {
    BlendDuration = 0.2,
})
```

它声明从源 State 到目标 State 的有向边。内部 Graph 负责：

- 校验 Transition Key 是否是合法 Lua 标识符；
- 保证同一状态机内 Key 唯一；
- 生成源状态和目标状态的稳定 ID；
- 生成默认规则函数名；
- 保存混合时长、优先级和混合模式。

默认规则名由“状态机节点名 + Transition Key”组成：

```text
CanEnter_<StateMachineNodeName>_<TransitionKey>
```

源状态和目标状态也允许稍后声明。最终由 C++ Validator 检查它们是否存在。

运行时 Transition 函数使用显式 AnimInstance 参数：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许进入目标状态。
function ABP_Sekiro.CanEnter_GroundLocomotion_Idle_Move(Inst)
    return Inst.GroundSpeed > 0.0
end
```

这里不使用冒号和隐式 `self`，因为 `Inst` 是 C++ 在运行时显式压入的真实 AnimInstance 代理。

## 完整创建顺序

一段状态机声明大致经历以下过程：

```text
AnimGraph 创建 LuaStateMachineNode
    -> LuaAnimNode.Initialize()
       创建节点身份和 Pose Pin
    -> 创建 LuaAnimStateMachineGraph
    -> 写入 OwnedGraphId
    -> 把内部 Graph 登记到 Layer
    -> 外层 AnimGraph:AddNode(node)
       设置声明顺序并封闭节点
    -> LuaAnimBlueprint 配置状态机定义
       调用 Entry / State / Transition
    -> 调用各 StateGraph_<State>()
       填充每个 StatePose Graph
    -> 导出扁平 IR
```

因此，`LuaStateMachineNode:Initialize()` 只建立“节点和内部容器”，并不会在这里硬编码具体状态。

## C++ 如何生成真正的 UE 状态机

C++ Factory 读取 StateMachine Node 后执行：

1. 在目标原生 Pose Graph 中创建 `UAnimGraphNode_StateMachine`；
2. 取得 UE 自动创建的 `EditorStateMachineGraph`；
3. 用 `Node.OwnedGraphId` 在当前 Layer 的 IR Graph 索引中查找内部 Graph；
4. 为原生 Graph 写入稳定 `GraphGuid` 和名称；
5. 创建 Entry、State、Transition 编辑器节点；
6. 为每个 State 构建原生 StatePose Graph；
7. 为 Transition 构建读取缓存规则的原生 Transition Graph；
8. 交给 UE 动画蓝图编译器生成运行时 AnimNode 和 `FPoseLink`。

最终对应关系是：

| Lua 编译期对象 | UE 编辑器对象 | UE 运行时结果 |
|---|---|---|
| `LuaStateMachineNode` | `UAnimGraphNode_StateMachine` | `FAnimNode_StateMachine` |
| `OwnedGraph` | `UAnimationStateMachineGraph` | 状态机状态与过渡描述 |
| State 的 Pose Graph | `UAnimationStateGraph` | 状态对应的 Pose 节点网络 |
| `Pose` Pin/IR Link | 编辑器 Pin/Link | `FPoseLink` |

## 嵌套状态机为什么可行

`StateMachine` 节点既允许放在 `Pose` Graph，也允许放在 `StatePose` Graph。

所以一个 State 内部可以再次声明状态机节点：

```text
Main AnimGraph
    -> GroundLocomotion StateMachine
        -> Locomotion StatePose Graph
            -> StandingLocomotion StateMachine
```

每个节点都拥有自己的 `LuaAnimStateMachineGraph`，所有内部 Graph 仍登记在同一个 Layer，通过稳定 ID 组成所有权树。C++ Validator 会拒绝循环拥有、跨 Layer 引用和多重所有者。

## 编译期与运行时边界

`LuaStateMachineNode` 和 `OwnedGraph` 都是**编辑器编译期对象**。

进入 PIE 或打包游戏后：

- Pose 更新、状态权重、动画混合由 UE 原生 AnimNode 执行；
- 原生状态机继续使用 UE 的更新、缓存和多线程求值体系；
- Lua Transition 规则在游戏线程读取 `Inst`，结果写入线程安全缓存；
- 原生 Transition Graph 只读取缓存结果，不会在动画工作线程直接运行 Lua；
- 编译期的 Lua Graph table 不参与每帧 Pose 求值。

因此，这套系统模拟的是“用 Lua 写动画蓝图编辑器声明”，而不是“用 Lua 重写 `FAnimNode_StateMachine` 的运行时算法”。

## 当前实现需要注意的边界

1. `Initialize()` 会把 `NodeType` 写进传入的 `config`，不要在业务层复用该 table。
2. `State()` 的 `build_function` 是保留能力；新代码优先使用独立 `StateGraph_<State>()`。
3. Entry、Transition 的目标存在性是在最终 Validator 阶段检查，不是调用方法时立即检查。
4. `OwnedGraph` 和 `OwnedGraphId` 是编译器内部字段，业务代码不应手动覆盖。
5. 节点封闭能阻止多数非法新字段，但 Lua 对已有 table 键的直接覆盖无法完全依靠 `__newindex` 拦截，仍需遵守编译器 API 边界。

## 相关问答

### 为什么状态机内部 Graph 不直接嵌套导出到 Node？

因为 StatePose Graph 还会继续拥有 Node、Link，甚至嵌套状态机。统一把所有 Graph 登记到 Layer，再用稳定 ID 建立引用，能让 C++ 一次建立索引，并统一完成所有权、唯一性、循环和跨 Layer 校验。

### 为什么调用的是 `machine:State()`，而不是 `machine.OwnedGraph:State()`？

因为业务层声明的是“这个状态机节点有哪些状态”。`OwnedGraph` 是节点内部保存拓扑的实现对象，不应该泄漏成业务 DSL 的必需知识。这里的薄转发是在表达所有权，不是在给运行时逻辑额外套层。

### `LuaStateMachineNode` 是运行时 AnimNode 吗？

不是。它是生成动画蓝图时使用的 Lua 编译期对象。真正运行的是该声明生成的 `FAnimNode_StateMachine`。

### 状态机节点为什么可以输出 Pose，而内部状态机 Graph 却没有 Pose Pin？

内部 Graph 描述的是 Entry、State、Transition 拓扑；每个 State 再引用自己的 StatePose Graph。原生状态机根据当前状态和过渡权重求值这些 StatePose，最后由外层 `LuaStateMachineNode.Pose` 提供统一输出。
