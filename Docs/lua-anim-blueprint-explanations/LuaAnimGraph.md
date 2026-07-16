# LuaAnimGraph

## 一、LuaAnimGraph 是什么

`LuaAnimGraph` 是“能够产生一个最终 Pose”的 Lua 编译期 Graph 基类。

它主要负责：

- 保存 Graph 身份、类型和源码位置；
- 自动创建唯一结果根节点；
- 创建并登记 AnimNode；
- 保证节点名称唯一并分配声明顺序；
- 保存节点之间的 Pose Link；
- 检查连接的 Graph、方向、类型和数量；
- 把整个 Pose Graph 导出为 `SekiroAnimIRGraph`。

它不执行动画更新或 Pose 求值。Lua Graph 描述会经过 C++ 导入和 NodeFactory，生成真正的 UE AnimGraph。

## 二、三种 Graph 必须区分

当前 IR 中有三种 GraphType：

| GraphType | Lua 类型 | 内容 | 固定根节点 |
|---|---|---|---|
| `Pose` | `LuaAnimGraph` | 动画蓝图或动画层的主 Pose 节点网络 | `OutputPose` |
| `StatePose` | `LuaAnimStateGraph` | 某个 State 独占的 Pose 节点网络 | `StateResult` |
| `StateMachine` | `LuaAnimStateMachineGraph` | Entry、State 和 Transition 拓扑 | 无 Pose 根节点 |

前两种都会产生 Pose，所以共享 `LuaAnimGraph` 的节点和 Link 能力。

`StateMachine` Graph 不负责直接连接 Pose 节点。它描述状态拓扑，并让每个 State 引用一个独立的 `StatePose` Graph。因此 `LuaAnimStateMachineGraph` 不是 `LuaAnimGraph` 的子类。

## 三、Graph 的层级位置

```text
LuaAnimBlueprint
└── LuaAnimLayer
    └── LuaAnimGraph (Pose，Layer 根图)
        └── LuaStateMachineNode
            └── LuaAnimStateMachineGraph
                ├── State: Idle
                │   └── LuaAnimStateGraph (StatePose)
                └── State: Move
                    └── LuaAnimStateGraph (StatePose)
```

Layer 持有所有 Graph，但只有一个 `Pose` Graph 通过 `RootGraphId` 成为该 Layer 的最终输出图。

## 四、Initialize 如何建立 Graph

### 1. 保存所属 Blueprint 和 Layer

```lua
self.Blueprint = config.Blueprint
self.Layer = config.Layer
```

Graph 不能脱离动画蓝图和动画层独立存在。Blueprint 用于源码模块和状态机配置，Layer 用于 Graph 注册和所有权管理。

### 2. 生成身份

```lua
self.Name = IRSchema.RequireSemanticName(config.Name, "Graph")
self.Id = config.Id or IRSchema.MakeStableId(self.Layer.Id, "Graph", self.Name)
self.GraphType = config.GraphType or "Pose"
```

主 Graph 通常产生类似 ID：

```text
Layer/Main/Graph/AnimGraph
```

状态拥有的 Graph 可以通过 `config.Id` 传入由 State 派生的稳定 ID。

### 3. 初始化集合

```lua
self.Nodes = {}
self.Links = {}
self.NodeNames = {}
```

- `Nodes`：按确定声明顺序保存节点；
- `Links`：按确定声明顺序保存 Pose 连线；
- `NodeNames`：按语义名称检查 Graph 内节点重名。

### 4. 自动创建结果根节点

普通 `LuaAnimGraph` 默认创建：

```text
OutputPose
└── Result (Input Pose)
```

初始化后暴露：

```lua
self.OutputNode -- 固定结果节点
self.RootNodeId -- 固定结果节点 ID
self.Result     -- OutputNode.Result 输入 Pin
```

所以业务代码不创建 Output Pose，而是直接连接：

```lua
graph.Result:Connect(player.Pose)
```

根节点会作为 Graph 的第一个 Node 登记，声明顺序为 0。

## 五、StatePose Graph 如何复用基类

`LuaAnimStateGraph` 继承 `LuaAnimGraph`，只在调用基类前替换配置：

```lua
config.GraphType = "StatePose"
config.RootNodeName = "StateResult"
config.RootNodeType = "StateResult"
config.RootNodeDisplayName = "State Result"
LuaAnimGraph.Initialize(self, config)
```

因此 State Graph 仍然使用相同的节点、Pin 和 Link API，但最终输出连接到 `StateResult.Result`，而不是主图的 `OutputPose.Result`。

业务 StateGraph 函数仍写成：

```lua
function GroundLocomotion.StateGraph_Idle(Graph)
    local player = Graph:SequencePlayer("IdlePlayer")
    player.Sequence = AnimAssets.Locomotion.Idle
    player.bLoopAnimation = true
    Graph.Result:Connect(player.Pose)
end
```

## 六、AddNode

`AddNode(node)` 负责把节点纳入 Graph：

1. 检查对象提供 `ToIR()`；
2. 检查 Graph 内节点名称不重复；
3. 根据 `#self.Nodes` 设置 `DeclarationOrder`；
4. 写入 `NodeNames` 和 `Nodes`；
5. 设置 `bIsSealed = true`，结束节点内部初始化阶段。

所有具体节点构造函数都会自动调用 `AddNode()`，业务代码通常不直接使用它。

## 七、节点构造函数

### SequencePlayer

```lua
local player = graph:SequencePlayer("IdlePlayer")
player.Sequence = AnimAssets.Locomotion.Idle
player.bLoopAnimation = true
```

创建 `LuaSequencePlayerNode`，由原生 SequencePlayer 在生成后的动画蓝图中播放动画序列。

### CreateNode

```lua
local node = graph:CreateNode("CustomName", "RegisteredNodeType")
```

这是已注册 NodeType 的低层通用入口。它只返回基础 `LuaAnimNode`，Rider 无法提供具体节点字段补全，因此业务 Graph 优先使用具名构造函数。

### Inertialization

```lua
local inertialization = graph:Inertialization("FinalInertialization")
inertialization.Source:Connect(state_machine.Pose)
graph.Result:Connect(inertialization.Pose)
```

创建原生惯性化节点的编译描述。

### SaveCachedPose 与 UseCachedPose

```lua
local saved_pose = graph:SaveCachedPose("LocomotionPose")
saved_pose.Pose:Connect(state_machine.Pose)

local used_pose = graph:UseCachedPose("UseLocomotionPose", saved_pose)
```

Graph 自动设置两者的 `CacheName`。当前实现要求 Save 和 Use 属于同一个主 `Pose` Graph，且 `SaveCachedPose` 不能放进 StatePose Graph。

### StateMachine

```lua
local locomotion = graph:StateMachine("GroundLocomotion", GroundLocomotion)
graph.Result:Connect(locomotion.Pose)
```

它创建外层 Pose Graph 中的 `LuaStateMachineNode`。该节点随后创建并拥有一个独立的 `LuaAnimStateMachineGraph`，Blueprint 再根据传入定义或命名约定配置 Entry、State 和 Transition。

因此 StateMachine Node 是有 `Pose` 输出的节点，StateMachine Graph 是该节点拥有的内部拓扑，两者不是同一个对象。

## 八、LinkPins 与 Link

业务代码调用：

```lua
target_input:Connect(source_output)
```

`LuaAnimPin:Connect()` 最终进入 `graph:LinkPins(source_pin, target_pin)`，检查：

- 两个 Pin 都属于当前 Graph；
- 两个 Pin 的 `DataType` 相同。

然后低层 `Link()` 继续检查：

1. 来源 Pin 在契约中注册为 `Output`；
2. 目标 Pin 在契约中注册为 `Input`；
3. 不允许多连接的来源或目标 Pin 没有被复用。

验证通过后生成：

```lua
{
    Id = ".../Link/IdlePlayer.Pose->Output.Result",
    Source = {
        NodeId = idle_player.Id,
        PinName = "Pose",
    },
    Target = {
        NodeId = output_node.Id,
        PinName = "Result",
    },
    DeclarationOrder = #self.Links,
    SourceLocation = { ... },
}
```

Link 由 Graph 集中保存，因为它属于 Graph 拓扑，而不属于任意单个 Pin。业务代码应使用具名 Pin 的 `Connect()`，不要手写节点和 Pin 名调用 `Link()`。

## 九、Output 便捷函数

```lua
graph:Output(player)
```

等价于低层调用：

```lua
graph:Link(player, "Pose", graph.OutputNode, "Result")
```

当前项目更推荐类型清晰的写法：

```lua
graph.Result:Connect(player.Pose)
```

后者不需要手写 Pin 名，并且 Rider 能识别两端类型。

## 十、ToIR

`ToIR()` 先逐个调用节点的 `ToIR()`，再导出：

```lua
{
    Id = self.Id,
    Name = self.Name,
    GraphType = self.GraphType,
    RootNodeId = self.RootNodeId,
    Nodes = nodes,
    Links = self.Links,
    StateMachine = {
        EntryStateId = "",
        States = {},
        Transitions = {},
    },
    DeclarationOrder = self.DeclarationOrder,
    SourceLocation = self.SourceLocation,
}
```

Pose 和 StatePose Graph 的 `StateMachine` 字段是空拓扑，只为保持统一 C++ IR 结构。真正的 StateMachine Graph 会在该字段中输出 Entry、States 和 Transitions，并且没有普通 Pose 节点根结构。

## 十一、完整主 Graph 示例

```lua
function ABP_Sekiro:AnimGraph(graph)
    local locomotion = graph:StateMachine(
        "GroundLocomotion",
        GroundLocomotion)

    local inertialization = graph:Inertialization(
        "FinalInertialization")

    inertialization.Source:Connect(locomotion.Pose)
    graph.Result:Connect(inertialization.Pose)
end
```

对应结构：

```text
GroundLocomotion.Pose
    -> FinalInertialization.Source

FinalInertialization.Pose
    -> OutputPose.Result
```

Graph 导出的 Nodes 至少包含：

```text
0: OutputPose
1: GroundLocomotion StateMachine
2: FinalInertialization
```

并导出两条 Pose Link。

## 十二、Lua Graph 与生成后的 UE Graph

Lua `LuaAnimGraph` 是短生命周期的编译对象。C++ 接收 IR 后会：

1. 导入 `FSekiroAnimIRGraph`；
2. 验证根节点、NodeType、Pin、Property、Link 和 Graph 所有权；
3. 创建对应 `UEdGraph`；
4. 创建 `UAnimGraphNode_*`；
5. 根据 IR Link 连接原生 `UEdGraphPin`；
6. 让 UE 动画蓝图编译器生成运行时原生节点和 `FPoseLink`。

游戏运行时不会每帧执行 `LuaAnimGraph:ToIR()`，也不会通过 LuaAnimGraph 计算 Pose。

## 十三、当前实现边界

- `AddNode()` 没有显式检查 `node.Graph == self`，具体构造函数能保证所属关系，但业务代码不应手工把其他 Graph 的节点加入当前 Graph；
- 低层 `Link()` 没有像 `LinkPins()` 一样直接检查节点所属 Graph，因此业务代码必须使用 Pin `Connect()`；
- Graph 当前没有封闭或 Finalize 状态，调用 `ToIR()` 后理论上仍可继续修改；标准编译流程只导出一次；
- Lua 层不会完成全部图语义验证，例如最终根输入完整性、循环和跨 Graph 所有权仍由 C++ Validator 兜底；
- `SaveCachedPose`/`UseCachedPose` 当前限制在同一主 Pose Graph，尚未抽象更广泛的缓存作用域；
- 当前只注册少量 Pose 节点，Blend、Slot、曲线和更多 UE AnimNode 需要继续扩展 NodeContracts 与 C++ NodeFactory。

这些边界不影响当前标准 DSL 路径，但说明 `AddNode()`、`Link()` 和 `CreateNode()` 属于编译器低层能力，不应成为普通动画业务代码的主要入口。

## 十四、相关问答

### Graph 和 Layer 有什么区别？

Layer 是 Graph 的容器和动画输出边界，一个 Layer 可以拥有主 Pose Graph、状态机 Graph 和多个 StatePose Graph。Graph 只管理自身节点和连线。

### 为什么每个 State 都需要独立 Graph？

UE 状态机中的每个 State 都拥有自己的 `UAnimationStateGraph`。独立 Graph 能让每个状态拥有不同 SequencePlayer、Blend 和处理节点，并由 StateMachine 在状态切换时选择和混合它们。

### 为什么状态机拓扑不直接放进 LuaAnimGraph？

Pose Graph 是节点数据流，StateMachine Graph 是状态拓扑，两者的根、元素和验证规则不同。分开建模更接近 UE 原生编辑器架构，也避免在同一个类里混合 Node Link 与 State Transition。

### Graph 会在游戏运行时存在吗？

Lua 编译对象不会作为运行时 Pose Graph 执行。生成后的 UE 动画蓝图资产中存在原生图和编译节点，运行时由 `FAnimInstanceProxy`、原生 AnimNode 和 `FPoseLink` 工作。
