# Lua 动画蓝图编写手册

本文说明如何使用 `SekiroAnimBlueprintExt` 的 Lua AnimGraph Function API 编写动画蓝图。

Lua 在编辑器编译期描述 Graph、Node、Pin、State 和 Transition。插件随后生成普通的原生 `UAnimBlueprint`。游戏运行时的姿势更新、节点求值、状态混合和 Cached Pose 仍由 UE 动画系统执行；Lua 只在游戏线程计算 Transition 规则并发布线程安全结果。

## 一、核心写法

Lua 文件直接对应动画蓝图的结构：

| Lua 内容 | 动画蓝图内容 |
|---|---|
| `AnimGraph(graph)` | 主 AnimGraph |
| `graph:SequencePlayer()` | Sequence Player 节点 |
| `graph:StateMachine()` | State Machine 节点 |
| `StateMachine(Machine)` | Entry、State、Transition 拓扑 |
| `StateGraph_Idle(Graph)` | Idle 状态内部 Graph |
| `CanEnter_Idle_Move(Inst)` | Idle 到 Move 的 Transition Rule |
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
CanEnter_<TransitionKey>
```

主动画蓝图实例化时使用的节点名会自动加入最终运行时规则名：

```text
状态机文件：CanEnter_Idle_Start
生成的规则：CanEnter_GroundLocomotion_Idle_Start
```

这个重命名和导出由基类完成，状态机业务文件不需要写转发函数。

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

## 七、Transition Rule

```lua
---角色有地面移动输入时，从 Idle 进入 Start。
---@param Inst userdata 生成动画蓝图实际使用的 USKAnimInstance 的 UnLua 代理。
---@return boolean can_enter 是否允许本帧开始 Transition。
function GroundLocomotion.CanEnter_Idle_Start(Inst)
    return Inst.bHasMovementInput == true
        and Inst.bIsInAir ~= true
end
```

规则函数的 `Inst` 是真实 `UAnimInstance` 的 UnLua 代理，因此可以直接访问反射暴露的 C++ 属性和函数：

```lua
---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许进入目标状态。
function GroundLocomotion.CanEnter_Cycle_Stop(Inst)
    return Inst.bHasMovementInput ~= true
        and Inst:GetCurveValue("CanEnterStop") >= 0.5
end
```

不要在 Transition 中保存 Pose、创建 AnimNode 或修改 Graph。Graph 声明只发生在编辑器编译期，Transition Rule 只在游戏线程计算布尔结果。

## 八、Cached Pose

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

## 九、主文件内的小状态机

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
    Machine:Transition("None_React", "None", "React")
end

function ABP_Sekiro.StateGraph_HitReaction_None(Graph)
    -- 声明 None 的 Pose 节点并连接 Graph.Result。
end

function ABP_Sekiro.StateGraph_HitReaction_React(Graph)
    -- 声明 React 的 Pose 节点并连接 Graph.Result。
end

---@param Inst userdata 当前 Transition 所属 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许进入受击状态。
function ABP_Sekiro.CanEnter_HitReaction_None_React(Inst)
    return Inst.bShouldPlayHitReaction == true
end
```

主文件内约定为：

```text
StateMachine_<MachineName>
StateGraph_<MachineName>_<StateName>
CanEnter_<MachineName>_<TransitionKey>
```

## 十、当前节点范围

| Graph API | 生成的原生节点 | 主要 Pin/属性 |
|---|---|---|
| `SequencePlayer` | `UAnimGraphNode_SequencePlayer` | `Pose`；Sequence、Loop、PlayRate、StartPosition |
| `StateMachine` | `UAnimGraphNode_StateMachine` | `Pose`；Owned StateMachine Graph |
| `Inertialization` | `UAnimGraphNode_Inertialization` | `Source`、`Pose` |
| `SaveCachedPose` | `UAnimGraphNode_SaveCachedPose` | `Pose` 输入、CacheName |
| `UseCachedPose` | `UAnimGraphNode_UseCachedPose` | `Pose` 输出、CacheName |

`Output Pose` 和 `State Result` 由 Graph 基类自动创建。

Blend、Slot、Layered Blend Per Bone、Aim Offset、Modify Curve 等节点需要先在 C++ NodeFactory 注册契约，Lua 才会开放对应构造函数。业务代码不能通过任意字符串绕过注册表创建未知节点。

## 十一、编译期与运行时

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
    -> 游戏线程调用 CanEnter_* Lua 规则
    -> C++ 发布线程安全布尔快照
    -> 原生 Transition Graph 读取快照
    -> UE Worker Thread 更新和求值原生 AnimNode
    -> 输出最终 Pose
```

Lua 不在 Worker Thread 中执行 Pose 求值。

## 十二、调试与常见错误

### Rider 调试 Transition

在 `CanEnter_*` 函数内设置 Lua 断点。PIE 中由实际 AnimInstance 调用规则时可以命中。`AnimGraph()`、`StateMachine()` 和 `StateGraph_*()` 是编辑器编译期函数，生成资产后不会每帧调用。

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

### Transition 函数缺失

```text
Transition 'GroundLocomotion.Idle_Start' requires function 'CanEnter_Idle_Start'
```

独立状态机文件不要添加状态机节点名前缀；基类会在导出时自动补上。

### Pin 方向错误

```text
Connect target 'Node.Pose' must be an Input Pin
```

调用顺序应为 `输入Pin:Connect(输出Pin)`。

## 十三、完整参考

项目内可运行的最小示例：

- `Content/Script/Animation/Examples/ABP_Minimal.lua`
- `Content/Script/Animation/Examples/StateMachines/MinimalLocomotion.lua`

编译器入口：

- `Content/Script/Animation/Compiler/LuaAnimBlueprint.lua`
- `Content/Script/Animation/Compiler/LuaAnimGraph.lua`
- `Content/Script/Animation/Compiler/LuaAnimStateMachine.lua`
- `Content/Script/Animation/Compiler/LuaAnimPin.lua`
