# Lua 动画蓝图编写手册

本文说明如何使用 `SekiroAnimBlueprintExt` 的 Lua AnimGraph Function API 编写动画蓝图。

Lua 在编辑器编译期描述 Graph、Node、Pin、State 和 Transition。插件随后生成普通的原生 `UAnimBlueprint`。游戏运行时的姿势更新、节点求值、状态混合和 Cached Pose 仍由 UE 动画系统执行；Transition 必须使用强类型 `Rule` AST 生成原生 Transition Rule Graph。

## 一、核心写法

Lua 文件直接对应动画蓝图的结构：

| Lua 内容 | 动画蓝图内容 |
|---|---|
| `AnimGraph(graph)` | 主 AnimGraph |
| `graph:SequencePlayer()` | Sequence Player 节点 |
| `graph:StateMachine()` | State Machine 节点 |
| `StateMachine(Machine)` | Entry、State、Transition 拓扑 |
| `StateGraph_Idle(Graph)` | Idle 状态内部 Graph |
| `Rule.BoolProperty("bIsMoving", true)` | Idle 到 Move 的原生 Transition Rule |
| `Pin:Connect(SourcePin)` | 两个节点 Pin 之间的连线 |

业务代码不创建 Layer、Owned Graph、稳定 ID 或 IR，也不编写 `BuildAnimGraph()`。这些步骤由 `LuaAnimBlueprint` 基类完成。

## 二、推荐目录

```text
Content/Script/Animation/Sekiro/
├── ABP_Sekiro.lua
├── AnimAssets.lua
└── ABP_Sekiro/
    ├── StateMachines/
    │   ├── GroundLocomotion.lua
    │   ├── CrouchLocomotion.lua
    │   ├── Jump.lua
    │   └── Combat.lua
    └── Layers/
        ├── UpperBody/
        └── FullBody/
```

主文件负责总图结构。每个较大的状态机放在独立文件中。动画资源路径集中放在 `AnimAssets.lua`，状态机只引用语义化动画名。

## 三、主动画蓝图

```lua
-- Sekiro 主动画蓝图，只描述 AnimGraph 的节点和连接关系。
local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local GroundLocomotion = require(
    "Animation.Sekiro.ABP_Sekiro.StateMachines.GroundLocomotion")

---@class ABP_Sekiro: LuaAnimBlueprint
local ABP_Sekiro = LuaAnimBlueprint:Extend("ABP_Sekiro", {
    SourceModule = "Animation.Sekiro.ABP_Sekiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
})

---声明主 AnimGraph；基类已经创建 Main Layer、AnimGraph 和 Output Pose。
---@param graph LuaAnimGraph 主 Pose Graph。
---@return nil result 最终姿势连接到 graph.Result。
function ABP_Sekiro:AnimGraph(graph)
    local locomotion = graph:StateMachine(
        "GroundLocomotion",
        GroundLocomotion)

    local saved_locomotion = graph:SaveCachedPose("SavedLocomotion")
    saved_locomotion.Pose:Connect(locomotion.Pose)

    local locomotion_pose = graph:UseCachedPose(
        "UseLocomotion",
        saved_locomotion)

    local final_inertialization = graph:Inertialization(
        "FinalInertialization")
    final_inertialization.Source:Connect(locomotion_pose.Pose)

    graph.Result:Connect(final_inertialization.Pose)
end

return ABP_Sekiro:Export()
```

`graph.Result` 是基类自动创建的 Output Pose 输入 Pin。业务代码只需要把最终 Pose 连接进去。

## 四、独立状态机文件

```lua
-- GroundLocomotion 只描述地面移动状态机。
local LuaAnimStateMachine = require(
    "Animation.Compiler.LuaAnimStateMachine")
local AnimAssets = require("Animation.Sekiro.AnimAssets")

---@class GroundLocomotion: LuaAnimStateMachine
local GroundLocomotion = LuaAnimStateMachine:Extend(
    "GroundLocomotion")

---声明 Entry、State 和 Transition，不在这里编写状态内部动画节点。
---@param Machine LuaStateMachineNode 原生状态机节点的编译期对象。
---@return nil result 该函数只声明状态机拓扑。
function GroundLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")

    Machine:State("Idle")
    Machine:State("Start")
    Machine:State("Cycle")
    Machine:State("Stop")

    local idle_to_start = Machine:Transition(
        "Idle_Start",
        "Idle",
        "Start")
    idle_to_start.BlendDuration = 0.10
    idle_to_start.PriorityOrder = 0

    local start_to_cycle = Machine:Transition(
        "Start_Cycle",
        "Start",
        "Cycle")
    start_to_cycle.BlendDuration = 0.12

    local cycle_to_stop = Machine:Transition(
        "Cycle_Stop",
        "Cycle",
        "Stop")
    cycle_to_stop.BlendDuration = 0.12

    local stop_to_idle = Machine:Transition(
        "Stop_Idle",
        "Stop",
        "Idle")
    stop_to_idle.BlendDuration = 0.10
end
```

状态机文件中的函数名规则为：

```text
StateMachine
StateGraph_<StateName>
```

Transition 必须直接在拓扑旁声明完整的强类型 `Rule`。规则会进入 IR，并由 C++ Factory 生成为原生 Transition Rule Graph；状态机业务文件不声明或导出运行时 `CanEnter_*` 函数。

## 五、State Graph

每个 `Machine:State("Name")` 必须有一个对应的 `StateGraph_Name(Graph)`。

```lua
---声明 Idle 状态的动画节点。
---@param Graph LuaAnimStateGraph Idle 独占的 StatePose Graph。
---@return nil result IdlePlayer 的 Pose 连接到 State Result。
function GroundLocomotion.StateGraph_Idle(Graph)
    local idle_player = Graph:SequencePlayer("IdlePlayer")
    idle_player.Sequence = AnimAssets.Locomotion.Idle
    idle_player.bLoopAnimation = true
    idle_player.PlayRate = 1.0
    idle_player.StartPosition = 0.0

    Graph.Result:Connect(idle_player.Pose)
end
```

节点属性采用直接赋值，不需要 settings 表，也不调用 `SetProperty()`：

```lua
local player = graph:SequencePlayer("WalkPlayer")
player.Sequence = AnimAssets.Locomotion.Walk_Forward_Loop
player.bLoopAnimation = true
player.PlayRate = 1.0
```

编译器会根据 C++ 节点契约把普通 Lua 值转换为 `Bool`、`Float`、`SoftObjectPath` 等显式 IR 类型。必填属性缺失会在编译阶段报错。

## 六、Pin 连接

连接语法固定为：

```lua
目标输入Pin:Connect(来源输出Pin)
```

例如：

```lua
inertialization.Source:Connect(sequence_player.Pose)
graph.Result:Connect(inertialization.Pose)
```

编译器会检查：

- 来源必须是 Output Pin；
- 目标必须是 Input Pin；
- 两个 Pin 必须属于同一个 Graph；
- 两个 Pin 的数据类型必须一致；
- 输入 Pin 不能违反连接数量限制。

业务代码不再手写节点名和 Pin 名字符串来调用 `graph:Link()`。

## 七、Graph 布局

布局是 Graph 级编辑器元数据，不参与 Pose、Transition 或运行时动画求值。每个 Pose/State Pose Graph 使用 `Graph.LayoutStyle`，每个状态机在自己的 `StateMachine(Machine)` 中使用 `Machine.LayoutStyle`：

```lua
local LayoutStyle = require("Animation.Compiler.LayoutStyle")

function ABP_Sekiro:AnimGraph(Graph)
    Graph.LayoutStyle = LayoutStyle.HierarchicalBlocks
    local machine = Graph:StateMachine("Locomotion", Locomotion)
    local inertialization = Graph:Inertialization("Inertialization")
    inertialization.Source:Connect(machine.Pose)
    Graph.Result:Connect(inertialization.Pose)

    local main_flow = Graph:Grid("MainFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    main_flow:Place(machine, 0, 0)
    main_flow:Place(Graph.OutputNode, 2, 0)
end
```

同一 Graph 可以声明多个 Grid。`RegionColumn/RegionRow` 决定分区位置，`Place(element, column, row)` 决定元素在分区内的单元格；未显式 Place 的节点由插件根据连接关系和 `LayoutStyle` 自动补位。

`HierarchicalBlocks` 是默认风格。在 Pose Graph 中，它从 Result 反向递归：一个节点的直接输入根节点在其左侧同一列纵向对齐，每个输入连同自己的下级输入视为不可重叠的子块，再把整组抽象为上一级输入块。这样方向选择器、步态选择器和各自的 SequencePlayer 会形成清晰的局部组，而不是按拓扑深度铺成一条长线。没有连接到 Result 的节点不参与主树尺寸计算，会在主树下方单独紧凑排列。

状态机的 State 由 `Machine:State()` 返回并直接放入状态机 Grid：

```lua
function Locomotion.StateMachine(Machine)
    Machine.LayoutStyle = LayoutStyle.HierarchicalBlocks
    Machine:Entry("Idle")
    local idle = Machine:State("Idle")
    local move = Machine:State("Move")

    local entry_flow = Machine:Grid("EntryFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    entry_flow:Place(idle, 0, 0)
    -- Move 未显式放置，将参与状态机的紧凑近方形网格。
end
```

在 StateMachine 中，`HierarchicalBlocks` 只统计 Entry 或 Transition 连接到的有效状态，列数取有效自动状态数量平方根的上取整。例如 8 个有效状态采用 3×3 网格。孤立状态不参与主网格的 N 值计算，而是在主网格下方单独成组。

可用风格为 `Auto`、`LeftToRight`、`RightToLeft`、`TopToBottom`、`BottomToTop`、`CompactGrid`、`Radial` 和 `HierarchicalBlocks`。`Auto` 作为兼容入口也解析为 `HierarchicalBlocks`；`Radial` 主要用于循环状态机。

显式 Grid 始终优先于自动排版。同一元素只能 Place 一次，同一单元格不能重复占用，节点/State 也不能放入其他 Graph 的 Grid。

## 八、Transition Rule

```lua
local Rule = require("Animation.Compiler.TransitionRule")

Machine:Transition("Idle_Start", "Idle", "Start", {
    BlendDuration = 0.12,
    PriorityOrder = 0,
    Rule = Rule.All(
        Rule.BoolProperty("bHasMovementInput", true),
        Rule.BoolProperty("bIsInAir", false)),
})
```

`Rule` 表示完整的 Transition 条件，不会生成 `EvaluateLuaTransitionRule` 调用。当前强类型叶节点包括：

- `Rule.BoolProperty(Name, ExpectedValue)`：比较 AnimInstance 或生成类上的 Bool 属性；
- `Rule.CurveGreaterEqual(Name, Threshold)`：比较当前动画曲线；
- `Rule.TimeRemainingLessEqual(Seconds)`：比较源状态最相关动画的剩余时间；
- `Rule.All(...)`、`Rule.Any(...)`、`Rule.Not(...)`：组合子条件。

曲线和属性条件应写在同一棵 Rule AST 中：

```lua
Machine:Transition("Cycle_Stop", "Cycle", "Stop", {
    BlendDuration = 0.12,
    PriorityOrder = 0,
    Rule = Rule.All(
        Rule.BoolProperty("bHasMovementInput", false),
        Rule.CurveGreaterEqual("CanEnterStop", 0.5)),
})
```

原生状态机按优先级检查当前状态的出边，找到第一条原生 Rule 为 true 的过渡后开始混合。

## 九、Cached Pose

Cached Pose 对应 UE 原生的 `Save Cached Pose` 和 `Use Cached Pose`。

```lua
local locomotion = graph:StateMachine(
    "GroundLocomotion",
    GroundLocomotion)

local saved = graph:SaveCachedPose("SavedLocomotion")
saved.Pose:Connect(locomotion.Pose)

local full_body_base = graph:UseCachedPose("FullBodyBase", saved)
local upper_body_base = graph:UseCachedPose("UpperBodyBase", saved)
```

规则如下：

- `SaveCachedPose` 只能创建在主 Pose Graph；
- `UseCachedPose` 必须与目标 Save 节点位于同一个主 Pose Graph；
- 一个 Save 节点可以被多个 Use 节点引用；
- Use 与 Save 通过稳定缓存名绑定，不生成普通 Pose Link；
- 缓存由 UE 原生动画系统持有，每个 AnimInstance 相互独立；
- Lua 不保存、复制或跨线程传递运行时 Pose。

状态机拆分到另一个 Lua 文件不会形成运行时边界，但 Cached Pose 的引用范围仍按原生 Graph 所有权校验。跨 State Graph、Animation Layer 或 Linked Anim Graph 的姿势复用应使用明确的 Pose 输入输出接口，不能把 Cached Pose 当成全局变量。

## 十、主文件内的小状态机

很小且不需要复用的状态机可以直接写在动画蓝图主文件中：

```lua
function ABP_Sekiro:AnimGraph(graph)
    local hit_reaction = graph:StateMachine("HitReaction")
    graph.Result:Connect(hit_reaction.Pose)
end

function ABP_Sekiro.StateMachine_HitReaction(Machine)
    Machine:Entry("None")
    Machine:State("None")
    Machine:State("React")
    Machine:Transition("None_React", "None", "React", {
        Rule = Rule.BoolProperty("bShouldPlayHitReaction", true),
    })
end

function ABP_Sekiro.StateGraph_HitReaction_None(Graph)
    -- 声明 None 的 Pose 节点并连接 Graph.Result。
end

function ABP_Sekiro.StateGraph_HitReaction_React(Graph)
    -- 声明 React 的 Pose 节点并连接 Graph.Result。
end

```

主文件内约定为：

```text
StateMachine_<MachineName>
StateGraph_<MachineName>_<StateName>
```

## 十一、当前节点范围

| Graph API | 生成的原生节点 | 主要 Pin/属性 |
|---|---|---|
| `SequencePlayer` | `UAnimGraphNode_SequencePlayer` | `Pose`；Sequence、Loop、PlayRate、StartPosition |
| `StateMachine` | `UAnimGraphNode_StateMachine` | `Pose`；Owned StateMachine Graph |
| `Inertialization` | `UAnimGraphNode_Inertialization` | `Source`、`Pose` |
| `LocalToComponentSpace` | `UAnimGraphNode_LocalToComponentSpace` | `LocalPose`、`ComponentPose` |
| `OrientationWarping` | `UAnimGraphNode_OrientationWarping` | `ComponentPose`、`OrientationAngle`、`Alpha`、`Pose`；脊柱与 IK 骨骼配置 |
| `FootPlacement` | `UAnimGraphNode_FootPlacement` | `ComponentPose`、`Alpha`、`Pose`；骨盆、双脚、脚趾和地面检测配置 |
| `LegIK` | `UAnimGraphNode_LegIK` | `ComponentPose`、`Alpha`、`Pose`；IK/FK 脚骨骼和腿链配置 |
| `ComponentToLocalSpace` | `UAnimGraphNode_ComponentToLocalSpace` | `ComponentPose`、`Pose` |
| `SaveCachedPose` | `UAnimGraphNode_SaveCachedPose` | `Pose` 输入、CacheName |
| `UseCachedPose` | `UAnimGraphNode_UseCachedPose` | `Pose` 输出、CacheName |

`Output Pose` 和 `State Result` 由 Graph 基类自动创建。

`FootPlacement.PlantLockType` 接受 `Unlocked`、`PivotAroundBall`、`PivotAroundAnkle` 或 `LockRotation`。`Unlocked` 只关闭脚部的世界空间锁定，地面检测、坡面旋转和骨盆高度补偿仍由原生节点执行。`IKFootRootBone` 的局部 Z 轴会被当作输入姿势的地面法线；项目骨架使用不蒙皮的 `IK_Foot_Plane`，避免依赖 `Master` 的横向局部 Z 轴。

Blend、Slot、Layered Blend Per Bone、Aim Offset、Modify Curve 等节点需要先在 C++ NodeFactory 注册契约，Lua 才会开放对应构造函数。业务代码不能通过任意字符串绕过注册表创建未知节点。

## 十二、编译期与运行时

### 编辑器编译期

```text
require 动画蓝图 Lua 模块
    -> CompileIR()
    -> 基类创建 Main Layer 和 AnimGraph
    -> 调用 AnimGraph(graph)
    -> 展开独立状态机模块
    -> 校验 Node、Pin、Property、Graph 所有权
    -> C++ NodeFactory 生成原生 AnimBlueprint
```

### 游戏运行时

```text
UAnimInstance 更新
    -> 游戏线程更新 Lua 动画参数
    -> 原生状态机检查当前状态的出边
    -> Transition Rule Graph 求值原生属性、曲线与时间节点
    -> 原生状态机执行 Transition 和 Pose 混合
    -> UE 原生 AnimNode 更新并求值
    -> 输出最终 Pose
```

Lua 来源动画蓝图的 Transition 全部使用强类型原生 Rule AST，可保持并行动画更新。`BlueprintUpdateAnimation` 仍先在游戏线程采集并写入变量，Sequence Player、混合、状态机、Root Motion、Curve 和 Notify 随后由 UE 原生 AnimNode 求值。启用 `RootMotionFromEverything` 时，引擎仍可能为需要即时根运动的帧选择同步更新。

角色移动策略与 Pose 求值分离：`Gameplay.Sekiro.Movement.SKMovementComponent` 在原生 CharacterMovement 求值前用 Lua 决定速度和 ActorYaw，并发布转向前输入角；`ABP_Sekiro.BlueprintUpdateAnimation` 用 `MoveInputX/MoveInputY` 即时选择锁定 Cycle 基础素材，用 Movement 快照的 `MoveDirectionAngle` 对齐实际轨迹。自由移动的 Back 输入必须映射到 Left/Right Turn 后让角色转向移动方向，Back Sequence 仅表示角色身体朝前时向后退，适用于锁定移动。锁定地面移动使用前后扇区扩宽的四向动画：Forward 覆盖 `0°..60°`，Back 覆盖 `120°..180°`，中间使用 Left/Right；Forward/Back 离开时保留 `10°` 防抖容差，侧向进入前后扇区时在基础边界立即切换。Cycle 的四条 Sequence 必须在方向选择器之前分别应用各自主轴残差，再混合已经对齐的 Pose；禁止在选择器后用当前方向残差旋转新旧混合结果。Jump 仍使用最近八向动画。方向对齐通过 `Animation.Sekiro.Shared.DirectionalPose` 的原生 Orientation Warping 完成，并由原生节点反向补偿脊柱以保持上半身朝向目标。

## 十三、调试与常见错误

### 调试 Transition

强类型 `Rule` 生成普通原生 Transition Graph，使用 UE AnimBP Debugger 查看属性值、State Weight、Transition Blend Alpha 和 Pose 结果。

需要连续观察运行时真实层级、动画来源、混合权重与 Transition 实参时，使用 `Sekiro.LuaAnim.Debug` 或 `Sekiro.LuaAnim.Snapshot [IntervalSeconds]`，再从编辑器 `Window > Lua Anim Snapshot Viewer` 打开时间轴回放。完整命令和操作说明见 [Lua 动画蓝图层级调试与快照回放](lua-anim-snapshot-debugger.md)。

`AnimGraph()`、`StateMachine()` 和 `StateGraph_*()` 是编辑器生成期函数，它们的断点只在 `Check Lua`、`Generate From Lua` 或 Lua 源码编译流程中命中，不会在 PIE 每帧执行。Lua 调试器只负责 `BlueprintUpdateAnimation` 等运行时 Lua；强类型 Transition Rule 不进入 Lua Runtime。

动画蓝图编辑器工具栏的 `Editor Debug: Off/On` 是按用户持久化的 Lua 调试端口开关：

- `Off`（默认）：编辑器阶段不监听 9966，`Main.lua` 在 PIE 世界启动后才开启调试，因此只能调试运行时 Lua。
- `On`：编辑器阶段立即监听 9966，可在点击 `Check Lua`、`Generate From Lua` 或 Compile 时调试 `CompileIR()` 与 Graph 声明函数。
- 从 `On` 切换为 `Off` 会立即停止编辑器调试器并释放端口；PIE/SIE 运行期间禁止切换。

### 必填 Sequence 缺失

```text
Node 'IdlePlayer' is missing required Property 'Sequence'
```

检查是否直接设置了：

```lua
idle_player.Sequence = AnimAssets.Locomotion.Idle
```

### StateGraph 函数缺失

```text
State 'GroundLocomotion.Cycle' requires function 'StateGraph_Cycle'
```

每个 State 必须存在对应函数，函数名大小写必须与 State 名完全一致。

### Transition 缺少原生 Rule

```text
Transition 'GroundLocomotion.Idle_Start' requires a native Rule
```

每条 Transition 都必须在 `Machine:Transition` 的设置表中提供 `Rule`。

### Pin 方向错误

```text
Connect target 'Node.Pose' must be an Input Pin
```

调用顺序应为 `输入Pin:Connect(输出Pin)`。

## 十四、完整参考

项目内可运行的最小示例：

- `Content/Script/Animation/Examples/ABP_Minimal.lua`
- `Content/Script/Animation/Examples/StateMachines/MinimalLocomotion.lua`

编译器入口：

- `Content/Script/Animation/Compiler/LuaAnimBlueprint.lua`
- `Content/Script/Animation/Compiler/LuaAnimGraph.lua`
- `Content/Script/Animation/Compiler/LuaAnimStateMachine.lua`
- `Content/Script/Animation/Compiler/LuaAnimPin.lua`
