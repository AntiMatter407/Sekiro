# Lua 动画蓝图编写手册

本文说明如何使用 `SekiroAnimBlueprintExt` 的 Lua AnimGraph Function API 编写动画蓝图。

Lua 在编辑器编译期描述 Graph、Node、Pin、State 和 Transition。插件随后生成普通的原生 `UAnimBlueprint`。游戏运行时的姿势更新、节点求值、状态混合和 Cached Pose 仍由 UE 动画系统执行；Transition 必须使用强类型 `Rule` AST 生成原生 Transition Rule Graph。

## 一、核心写法

Lua 文件直接对应动画蓝图的结构：

| Lua 内容 | 动画蓝图内容 |
|---|---|
| `AnimGraph(graph)` | 主 AnimGraph |
| `graph:Node(Name, Class, Properties, NodeType)` | 任意可反射的原生动画节点 |
| `graph:StateMachine()` | State Machine 节点 |
| `graph:Property()` | AnimInstance 变量 Getter |
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
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
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

    local saved_locomotion = graph:Node(
        "SavedLocomotion",
        EditorNodeClass.SaveCachedPose,
        { CacheName = "SavedLocomotion" },
        "SaveCachedPose")
    saved_locomotion.Pose:Connect(locomotion.Pose)

    local locomotion_pose = graph:Node(
        "UseLocomotion",
        EditorNodeClass.UseCachedPose,
        { CacheName = saved_locomotion.Name },
        "UseCachedPose")

    local final_inertialization = graph:Node(
        "FinalInertialization",
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
    final_inertialization.Source:Connect(locomotion_pose.Pose)

    graph.Result:Connect(final_inertialization.Pose)
end

return ABP_Sekiro:Export()
```

`graph.Result` 是基类自动创建的 Output Pose 输入 Pin。业务代码只需要把最终 Pose 连接进去。

### Animation Layer Interface、Anim Layer 与子类覆写

插件生成的是 UE 原生 Animation Blueprint Interface、Animation Layer Function Graph、
Linked Anim Layer 和 Linked Anim Graph，不会为接口或节点另外创建一套 Lua 运行时类型。
Lua 只在编辑器生成期声明结构，参数与 Pin 最终按 `UFunction` 反射签名复核。

Animation Layer Interface 使用同一个基类，将 `BlueprintKind` 设为
`AnimationLayerInterface`。接口不需要 Skeleton，也不声明主 `AnimGraph`：

```lua
-- Lua 类型：动画蓝图编译声明；生成 UE 原生 Animation Layer Interface。
local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")

---@class ALI_Locomotion: LuaAnimBlueprint
local ALI_Locomotion = LuaAnimBlueprint:Extend("ALI_Locomotion", {
    BlueprintKind = "AnimationLayerInterface",
    SourceModule = "Animation.Examples.ALI_Locomotion",
    ParentAnimInstanceClass = "",
    TargetSkeleton = "",
})

---声明接口拥有的 Animation Layer 函数签名。
---@return nil result 接口只导出函数签名，不构建姿势实现。
function ALI_Locomotion:DeclareAnimationLayers()
    self:AnimLayer("Locomotion", {
        Parameters = {
            { Name = "SourcePose", DataType = "Pose" },
            { Name = "BlendAlpha", DataType = "Float" },
        },
    })
end

return ALI_Locomotion:Export()
```

普通 AnimBlueprint 通过 `ImplementedInterfaces` 实现接口，并在
`DeclareAnimationLayers()` 中构建对应 Layer。`inputs` 的键与接口参数同名：

```lua
local locomotion_interface =
    "/Game/Animation/ALI_Locomotion.ALI_Locomotion_C"

---@class ABP_LocomotionBase: LuaAnimBlueprint
local ABP_LocomotionBase = LuaAnimBlueprint:Extend(
    "ABP_LocomotionBase",
    {
        SourceModule = "Animation.Examples.ABP_LocomotionBase",
        ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
        TargetSkeleton =
            "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
        ImplementedInterfaces = {
            locomotion_interface,
        },
    })

---实现接口声明的 Locomotion Layer。
---@return nil result 最终姿势由 SourcePose 连接到 Layer Result。
function ABP_LocomotionBase:DeclareAnimationLayers()
    self:AnimLayer(
        "Locomotion",
        {
            InterfaceClass = locomotion_interface,
            bOverride = true,
            Parameters = {
                { Name = "SourcePose", DataType = "Pose" },
                { Name = "BlendAlpha", DataType = "Float" },
            },
        },
        function(graph, inputs)
            ---把示例输入姿势直接作为 Layer 输出。
            ---@return nil result 该回调只声明 Pin 连接。
            graph.Result:Connect(inputs.SourcePose)
        end)
end
```

子 AnimBlueprint 可以复用父资产，只声明需要覆写的同名 Layer。未声明的父 Layer
由 UE 正常继承；`ParentAnimInstanceClass` 必须指向父 AnimBlueprint 的
GeneratedClass：

```lua
local ParentModule =
    require("Animation.Examples.ABP_LocomotionBase")
local locomotion_interface =
    "/Game/Animation/ALI_Locomotion.ALI_Locomotion_C"

---@class ABP_LocomotionChild: ABP_LocomotionBase
local ABP_LocomotionChild = ParentModule:Extend(
    "ABP_LocomotionChild",
    {
        SourceModule = "Animation.Examples.ABP_LocomotionChild",
        ParentAnimInstanceClass =
            "/Game/Animation/ABP_LocomotionBase.ABP_LocomotionBase_C",
    })

---只覆写父类的 Locomotion Layer。
---@return nil result 其他父 Layer 继续由 UE 继承。
function ABP_LocomotionChild:DeclareAnimationLayers()
    self:AnimLayer(
        "Locomotion",
        {
            InterfaceClass = locomotion_interface,
            bOverride = true,
            Parameters = {
                { Name = "SourcePose", DataType = "Pose" },
                { Name = "BlendAlpha", DataType = "Float" },
            },
        },
        function(graph, inputs)
            ---输出子类版本的姿势实现。
            ---@return nil result 示例直接转发输入姿势。
            graph.Result:Connect(inputs.SourcePose)
        end)
end

return ABP_LocomotionChild:Export()
```

参数表支持 `Pose`、`ComponentPose`、`Bool`、`Float`、`Byte`、`Integer`、
`Name`、`String`、`Object`、`Class` 和 `Enum`。后三种必须额外填写
`TypeObjectPath`。实现接口或覆写父 Layer 时，声明必须与 UE 反射得到的
参数名称、顺序和类型完全一致。当前 UE5.2 的 Animation Layer 函数若声明普通
数值参数，必须同时至少声明一个 Pose 或 ComponentPose 输入；纯数值、无 Pose
输入的 Layer 会在生成前给出明确诊断。

### Linked Anim Layer 与 Linked Anim Graph

`graph:LinkedAnimLayer()` 调用接口 Layer；`graph:LinkedAnimGraph()` 调用另一个
AnimBlueprint 的动画图函数。它们仍返回普通 `LuaAnimNode`，可用具名 Pin 连接：

```lua
local linked_layer = graph:LinkedAnimLayer(
    "LinkedLocomotion",
    {
        LayerName = "Locomotion",
        InterfaceClass =
            "/Game/Animation/ALI_Locomotion.ALI_Locomotion_C",
        InstanceClass =
            "/Game/Animation/ABP_LocomotionBase.ABP_LocomotionBase_C",
        Parameters = {
            { Name = "SourcePose", DataType = "Pose" },
            { Name = "BlendAlpha", DataType = "Float" },
        },
    })
linked_layer.SourcePose:Connect(source_pose.Pose)
graph.Result:Connect(linked_layer.Pose)

local linked_graph = graph:LinkedAnimGraph(
    "LinkedUpperBody",
    {
        InstanceClass =
            "/Game/Animation/ABP_UpperBody.ABP_UpperBody_C",
        GraphName = "AnimGraph",
    })
graph.Result:Connect(linked_graph.Pose)
```

`LinkedAnimLayer` 的 `InstanceClass` 可省略，表示使用当前实例上的实现。
`LinkedAnimGraph.InstanceClass` 必填。对于带输入参数的目标函数，需要在
`Parameters` 中声明完整签名，以便 Lua 侧创建可连接的具名 Pin；生成器还会用
UE 反射再次校验。若 Lua 只消费输出 Pose，可以省略 `Parameters`，由 C++ 根据
目标 `UFunction` 重建真实动态 Pin；若 Lua 需要连接某个输入，则必须声明该输入
端点。新增普通 C++ Task、AnimNode 或 AnimInstance 子类无需修改插件。

主 `AnimGraph` 与 Animation Layer 是两种不同的 UE 覆写机制：

- 子资产的主 `AnimGraph` 仍通过正常 AnimBlueprint 继承/生成规则处理，不标记为
  Animation Layer Override。
- 接口 Layer 或父类 Layer 使用 `bOverride = true`。
- 子类 Lua 不声明的父 Layer 保持继承，不会被生成器复制一份。

## 四、独立状态机文件

```lua
-- GroundLocomotion 只描述地面移动状态机。
local LuaAnimStateMachine = require(
    "Animation.Compiler.LuaAnimStateMachine")
local EditorNodeClass = require(
    "Animation.Compiler.NodeClasses.EditorNodeClass")
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
    local idle_player = Graph:Node(
        "IdlePlayer",
        EditorNodeClass.SequencePlayer,
        {
            Sequence = AnimAssets.Locomotion.Idle,
            bLoopAnimation = true,
            PlayRate = 1.0,
            StartPosition = 0.0,
        },
        "SequencePlayer")

    Graph.Result:Connect(idle_player.Pose)
end
```

普通节点都通过集中登记的 UE 编辑器节点类引用创建，初始属性放在第三个参数的属性表中：

```lua
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")

local player = graph:Node(
    "WalkPlayer",
    EditorNodeClass.SequencePlayer,
    {
        Sequence = AnimAssets.Locomotion.Walk_Forward_Loop,
        bLoopAnimation = true,
        PlayRate = 1.0,
    },
    "SequencePlayer")
```

原生类路径集中在 `Animation.Compiler.NodeClasses.EditorNodeClass`。业务脚本只引用 `EditorNodeClass.UseCachedPose` 等语义字段，不直接保存 `"/Script/..."` 字符串。第四个 `NodeType` 用于复用已有强类型 Pin 契约和结构型 C++ 适配器；现有节点应继续填写。编译器会把普通 Lua 值转换为显式 IR 类型，属性不存在、类型不兼容或必填属性缺失都会在生成阶段报错。

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
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")

function ABP_Sekiro:AnimGraph(Graph)
    Graph.LayoutStyle = LayoutStyle.HierarchicalBlocks
    local machine = Graph:StateMachine("Locomotion", Locomotion)
    local inertialization = Graph:Node(
        "Inertialization",
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
    inertialization.Source:Connect(machine.Pose)
    Graph.Result:Connect(inertialization.Pose)

    local main_flow = Graph:Grid("MainFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    main_flow:Place(machine, 0, 0)
    main_flow:Place(Graph.OutputNode, 2, 0)

    -- 精确像素坐标优先于上面的 Grid 单元格，适合需要固定画布位置的关键节点。
    Graph:SetPosition(machine, -320, 140)
end
```

同一 Graph 可以声明多个 Grid。`RegionColumn/RegionRow` 决定分区位置，`Place(element, column, row)` 决定元素在分区内的单元格；未显式 Place 的节点由插件根据连接关系和 `LayoutStyle` 自动补位。

`Graph:SetPosition(element, x, y)` 和 `Machine:SetPosition(state, x, y)` 直接声明 UE Graph 画布像素坐标。元素必须属于当前 Graph，每个元素只能声明一个精确坐标，X/Y 必须是 -1,000,000 到 1,000,000 之间的有限整数。布局优先级固定为：精确 `SetPosition` > `Grid:Place` > 自动布局。因此同一元素可以同时保留 Grid 语义与精确坐标，最终以精确坐标为准。

`HierarchicalBlocks` 是默认风格。在 Pose Graph 中，它从 Result 反向递归：一个节点的直接输入根节点在其左侧同一列纵向对齐，每个输入连同自己的下级输入视为不可重叠的子块，再把整组抽象为上一级输入块。这样方向选择器、步态选择器和各自的 SequencePlayer 会形成清晰的局部组，而不是按拓扑深度铺成一条长线。没有连接到 Result 的节点不参与主树尺寸计算，会在主树下方单独紧凑排列。

状态机的 State 由 `Machine:State()` 返回并直接放入状态机 Grid：

```lua
function Locomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    local idle = Machine:State("Idle")
    local move = Machine:State("Move")

    local entry_flow = Machine:Grid("EntryFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    entry_flow:Place(idle, 0, 0)
    Machine:SetPosition(idle, 120, 80)
    -- Move 未显式放置，将参与状态机的紧凑近方形网格。
end
```

在 StateMachine 中，未显式设置 `LayoutStyle` 时默认使用 `CompactGrid`。它只统计 Entry 或 Transition 连接到的有效自动状态，列数取数量平方根的上取整。例如 8 个有效状态采用 3×3 网格。孤立状态不参与主网格的 N 值计算，而是在主网格下方单独成组。旧 IR 缺少 Layout 时的 `Auto` 也按该规则处理。

可用风格为 `Auto`、`LeftToRight`、`RightToLeft`、`TopToBottom`、`BottomToTop`、`CompactGrid`、`Radial` 和 `HierarchicalBlocks`。Pose/StatePose Graph 的 `Auto` 按 `HierarchicalBlocks` 处理，StateMachine Graph 的 `Auto` 按 `CompactGrid` 处理；`Radial` 主要用于循环状态机。

显式 Grid 始终优先于自动排版，精确坐标又始终优先于 Grid。同一元素只能 Place 一次，同一单元格不能重复占用，节点/State 也不能放入其他 Graph 的 Grid。

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
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")

local locomotion = graph:StateMachine(
    "GroundLocomotion",
    GroundLocomotion)

local saved = graph:Node(
    "SavedLocomotion",
    EditorNodeClass.SaveCachedPose,
    { CacheName = "SavedLocomotion" },
    "SaveCachedPose")
saved.Pose:Connect(locomotion.Pose)

local full_body_base = graph:Node(
    "FullBodyBase",
    EditorNodeClass.UseCachedPose,
    { CacheName = saved.Name },
    "UseCachedPose")
local upper_body_base = graph:Node(
    "UpperBodyBase",
    EditorNodeClass.UseCachedPose,
    { CacheName = saved.Name },
    "UseCachedPose")
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

| 集中类路径引用 | NodeType | 主要 Pin/属性 |
|---|---|---|
| `EditorNodeClass.SequencePlayer` | `SequencePlayer` | `Pose`；Sequence、Loop、PlayRate、StartPosition |
| `EditorNodeClass.BlendListByBool` | `BlendListByBool` | TruePose、FalsePose、ActiveValue、Pose；BlendTime |
| `EditorNodeClass.BlendListByEnum` | `BlendListByEnum` | DefaultPose、Pose0..Pose7、ActiveValue、Pose；EnumType、EnumEntries、BlendTime |
| `EditorNodeClass.Inertialization` | `Inertialization` | `Source`、`Pose` |
| `EditorNodeClass.Slot` | `Slot` | Source、Pose；SlotName |
| `EditorNodeClass.LayeredBlendPerBone` | `LayeredBlendPerBone` | BasePose、BlendPose、BlendWeight、Pose；BranchFilters |
| `EditorNodeClass.LocalToComponentSpace` | `LocalToComponentSpace` | `LocalPose`、`ComponentPose` |
| `EditorNodeClass.OrientationWarping` | `OrientationWarping` | `ComponentPose`、角度、Alpha、Pose；脊柱与 IK 骨骼配置 |
| `EditorNodeClass.FootPlacement` | `FootPlacement` | `ComponentPose`、Alpha、Pose；骨盆、双脚、脚趾和地面检测配置 |
| `EditorNodeClass.LegIK` | `LegIK` | `ComponentPose`、Alpha、Pose；IK/FK 脚骨骼和腿链配置 |
| `EditorNodeClass.TwoBoneIK` | `TwoBoneIK` | ComponentPose、Alpha、Pose；IK 骨、末端与关节目标 |
| `EditorNodeClass.ComponentToLocalSpace` | `ComponentToLocalSpace` | `ComponentPose`、Pose |
| `EditorNodeClass.SaveCachedPose` | `SaveCachedPose` | Pose 输入、CacheName |
| `EditorNodeClass.UseCachedPose` | `UseCachedPose` | Pose 输出、CacheName |

`StateMachine` 和 `Property` 仍使用专用 Graph API；`Output Pose` 和 `State Result` 由 Graph 基类自动创建。

`FootPlacement.PlantLockType` 接受 `Unlocked`、`PivotAroundBall`、`PivotAroundAnkle` 或 `LockRotation`。`Unlocked` 只关闭脚部的世界空间锁定，地面检测、坡面旋转和骨盆高度补偿仍由原生节点执行。`IKFootRootBone` 的局部 Z 轴会被当作输入姿势的地面法线；项目骨架使用不蒙皮的 `IK_Foot_Plane`，避免依赖 `Master` 的横向局部 Z 轴。

Blend、Slot、Layered Blend Per Bone、Aim Offset、Modify Curve 等普通节点可直接填写原生编辑器类路径，不再要求为每个节点增加 Lua 子类或插件构造函数。已有 C++ 特殊生成逻辑的节点仍传 `NodeType` 复用适配器；未注册节点由 UE 反射写入属性，并由原生 Graph Schema 拒绝非法类、Pin 或连线。

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

## 双向同步工具栏

动画蓝图编辑器在原生 Compile 区域后依次提供：`Check Lua`、`Lua → AnimBlueprint`、`AnimBlueprint → Lua`、同步状态、Lua 模块名和 `Editor Debug`。

- `Check Lua` 只执行 Exchange Lua 模块的 `CompileIR()`、规范化、校验与资源预检，不修改 Graph 或文件。
- `Lua → AnimBlueprint` 是唯一显式导入入口：事务性更新 Graph 后执行一次 UE 原生编译，但不自动保存资产。
- `AnimBlueprint → Lua` 通过只读 Reader 生成 Canonical IR，再安全写入独立 `.generated.lua` 交换模块；不会修改 Graph。
- 同步状态按钮只读刷新两侧 Canonical IR 哈希。哈希包含节点、状态和过渡的 Layout Positions；状态为 `Never Synchronized`、`In Sync`、`Blueprint Changed`、`Lua Changed`、`Both Changed` 或 `Error`。
- 导入会覆盖已改变的 Blueprint 侧、导出会覆盖已改变的 Lua 侧时，工具栏会在任何写操作前明确确认；取消不会修改 Graph、Lua 文件或同步基线。
- Lua 文件监听只标记 `Lua Changed`，不会自动导入；Graph 或布局变化在点击同步状态时检测。

原生 Compile/F7 始终只编译当前 Blueprint Graph，PIE 前也只重置运行时缓存；二者都不会隐式执行 Lua 生成、导入或导出。

角色移动策略与 Pose 求值分离：`Gameplay.Sekiro.Movement.SKMovementComponent` 在原生 CharacterMovement 求值前选择锁定四向素材，并让 ActorYaw 平滑追向锁定目标；`ABP_Sekiro.BlueprintUpdateAnimation` 把实际移动方向相对当前 Actor 的角度镜像到生成变量。锁定 Start、Cycle、Stop 与 Step 使用 UE5.2 标准 `Orientation Warping` Graph 模式：节点从输入 Pose 的 `RootMotionDelta` Attribute 读取素材原方向，把 Root Motion 位移与下半身同步重定向到 `LocomotionAngle`，再通过 `Spine/Spine1/Spine2` 反向补偿保持上半身朝向。项目 Graph 中不再实例化自定义 `SpineYawCompensation` 节点；输入释放时锁存角度供 Stop 使用，Graph 方案也不再请求旧 StopTurn 换脚回正。锁定四方向按 `45°/135°` 初始分类，并保留 `10°` 滞回避免边界换腿抖动。自由移动仍由 Actor 朝输入方向旋转；Jump 继续使用八方向素材和 Manual Orientation Warping。

## 十三、调试与常见错误

### 调试 Transition

强类型 `Rule` 生成普通原生 Transition Graph，使用 UE AnimBP Debugger 查看属性值、State Weight、Transition Blend Alpha 和 Pose 结果。

需要连续观察运行时真实层级、动画来源、混合权重与 Transition 实参时，使用 `Sekiro.LuaAnim.Debug` 或 `Sekiro.LuaAnim.Snapshot [IntervalSeconds]`，再从编辑器 `Window > Lua Anim Snapshot Viewer` 打开时间轴回放。完整命令和操作说明见 [Lua 动画蓝图层级调试与快照回放](lua-anim-snapshot-debugger.md)。

`AnimGraph()`、`StateMachine()` 和 `StateGraph_*()` 是编辑器生成期函数，它们的断点只在 `Check Lua`、`Lua → AnimBlueprint` 或显式同步状态刷新中命中，不会在 PIE 每帧执行。Lua 调试器只负责 `BlueprintUpdateAnimation` 等运行时 Lua；强类型 Transition Rule 不进入 Lua Runtime。

动画蓝图编辑器工具栏的 `Editor Debug: Off/On` 是按用户持久化的 Lua 调试端口开关：

- `Off`（默认）：编辑器阶段不监听 9966，`Main.lua` 在 PIE 世界启动后才开启调试，因此只能调试运行时 Lua。
- `On`：编辑器阶段立即监听 9966，可在点击 `Check Lua`、`Lua → AnimBlueprint` 或同步状态刷新时调试 `CompileIR()` 与 Graph 声明函数；原生 Compile/F7 不执行 `CompileIR()`。
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
