# LuaAnimStateMachine

## 一、LuaAnimStateMachine 是什么

`LuaAnimStateMachine.lua` 的全部运行时代码只有：

```lua
local CompilerClass = require("Animation.Compiler.CompilerClass")

---@class LuaAnimStateMachine: CompilerClass
local LuaAnimStateMachine = CompilerClass:Extend("LuaAnimStateMachine")

return LuaAnimStateMachine
```

它是“可拆分状态机定义”的语义基类。

它不保存 State、Transition 或 Graph，也不负责构建顺序。子类只提供一组符合命名约定的方法，`LuaAnimBlueprint:ConfigureStateMachine()` 负责发现和调用这些方法。

## 二、为什么一个空基类仍然有意义

如果只看当前功能，独立状态机也可以写成普通 Lua table。但专用基类提供了几个稳定边界：

- Rider 能识别 `MinimalLocomotion: LuaAnimStateMachine` 的类型关系；
- 状态机文件与普通数据模块、AnimBlueprint 类和 Gameplay 类有明确身份区别；
- 复用 `CompilerClass` 的 `Extend()`，状态机可以继续派生公共状态机定义；
- 后续可以在基类集中加入状态机配置或辅助能力，而不修改每个业务文件；
- 项目规范可以明确规定状态机子类只实现哪些约定函数。

因此它现在是“无默认行为的接口式基类”，不是无意义的空 table。

## 三、标准独立状态机结构

```lua
local LuaAnimStateMachine =
    require("Animation.Compiler.LuaAnimStateMachine")

---@class MinimalLocomotion: LuaAnimStateMachine
local MinimalLocomotion =
    LuaAnimStateMachine:Extend("MinimalLocomotion")

function MinimalLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Move")

    Machine:Transition("Idle_Move", "Idle", "Move")
    Machine:Transition("Move_Idle", "Move", "Idle")
end

function MinimalLocomotion.StateGraph_Idle(Graph)
    local player = Graph:SequencePlayer("IdlePlayer")
    player.Sequence = AnimAssets.Idle
    Graph.Result:Connect(player.Pose)
end

function MinimalLocomotion.StateGraph_Move(Graph)
    local player = Graph:SequencePlayer("MovePlayer")
    player.Sequence = AnimAssets.Move
    Graph.Result:Connect(player.Pose)
end

function MinimalLocomotion.CanEnter_Idle_Move(Inst)
    return Inst.bIsMoving == true
end

function MinimalLocomotion.CanEnter_Move_Idle(Inst)
    return Inst.bIsMoving ~= true
end

return MinimalLocomotion
```

文件直接返回状态机类，不调用 `Export()`。

## 四、为什么不调用 Export

独立状态机不是 C++ 直接 `require` 和编译的动画蓝图入口。主动画蓝图先导入它：

```lua
local MinimalLocomotion = require(
    "Animation.Examples.StateMachines.MinimalLocomotion")
```

然后把类传给状态机节点：

```lua
local locomotion = graph:StateMachine(
    "MainStateMachine",
    MinimalLocomotion)
```

`LuaAnimBlueprint` 负责读取状态机类的方法、生成状态机 IR，并把运行时规则复制到主动画蓝图的导出模块。

因此：

- AnimBlueprint 文件返回 `ABP_Class:Export()`；
- 独立状态机文件直接返回 `StateMachineClass`。

## 五、它与 LuaStateMachineNode 的区别

两个名字相近，但职责完全不同：

| 类型 | 含义 |
|---|---|
| `LuaAnimStateMachine` | 业务状态机定义类，保存约定方法 |
| `LuaStateMachineNode` | 外层 Pose Graph 中的编译期 StateMachine AnimNode |
| `LuaAnimStateMachineGraph` | Node 拥有的 Entry、State、Transition 内部拓扑 |

调用：

```lua
graph:StateMachine("MainStateMachine", MinimalLocomotion)
```

可以理解为：

```text
创建 LuaStateMachineNode
    -> 创建 Owned LuaAnimStateMachineGraph
    -> 使用 MinimalLocomotion 定义类填写 OwnedGraph
```

`MinimalLocomotion` 本身不是 Node，也不是 Graph。

## 六、编译时的方法约定

独立状态机类提供三组方法。

### StateMachine

```lua
function MinimalLocomotion.StateMachine(Machine)
end
```

负责声明：

- Entry；
- State 列表；
- Transition 列表及混合参数。

参数 `Machine` 是 `LuaStateMachineNode`。该函数使用点号声明，没有隐式 `self`。

### StateGraph_<State>

```lua
function MinimalLocomotion.StateGraph_Idle(Graph)
end
```

每个已声明 State 必须具有同名 StateGraph 函数。参数 `Graph` 是该 State 独占的 `LuaAnimStateGraph`，函数内只写这个状态的 Pose 节点和连接。该函数没有 `self`，也不会收到 AnimInstance。

### CanEnter_<TransitionKey>

```lua
function MinimalLocomotion.CanEnter_Idle_Move(Inst)
end
```

每条 Transition 必须具有对应规则函数。返回 `true` 表示原生状态机可以进入目标状态。

## 七、ConfigureStateMachine 如何使用定义类

假设：

```lua
graph:StateMachine("MainStateMachine", MinimalLocomotion)
```

Blueprint 执行：

```text
owner = MinimalLocomotion
    -> owner.StateMachine(Machine)
    -> 遍历 Machine.OwnedGraph.States
    -> owner.StateGraph_Idle(Idle.PoseGraph)
    -> owner.StateGraph_Move(Move.PoseGraph)
    -> 遍历 Transitions
    -> 取得 owner.CanEnter_Idle_Move 函数引用
    -> 按完整规则名登记到 RuntimeFunctions
```

这里没有执行 `MinimalLocomotion:New()`。编译器只从定义类 table 查找函数，然后把 `Machine` 或 `Graph` 作为唯一参数显式传入。

## 八、编译期 Machine/Graph 与运行时 Inst

这是当前设计最重要的规则。

### StateMachine 和 StateGraph 的显式参数

编译器这样调用：

```lua
topology_function(Machine)
state_graph_function(Graph)
```

所以在以下方法中：

```lua
function MinimalLocomotion.StateMachine(Machine)
function MinimalLocomotion.StateGraph_Idle(Graph)
```

这两个函数都没有隐式 `self`：`Machine` 明确来自状态机节点，`Graph` 明确来自当前 State 的 StatePose Graph。它们是编辑器编译期参数，不是运行时 AnimInstance。

### CanEnter 中的 Inst

编译器只取得函数引用：

```lua
local runtime_function = owner["CanEnter_Idle_Move"]
```

随后把函数复制到主动画蓝图导出模块。运行时桥接调用时，会传入真实 AnimInstance 作为第一个参数。

所以：

```lua
function MinimalLocomotion.CanEnter_Idle_Move(Inst)
    return Inst.bIsMoving == true
end
```

这里的 `Inst` 是生成动画蓝图使用的真实 `UAnimInstance` 代理，不是 `MinimalLocomotion` 类。点号声明刻意表明该函数不是绑定在状态机 definition 上的普通类方法。

这正是 Transition 规则可以直接读取 C++ 反射字段和调用 C++ 函数的原因。

## 九、为什么定义类应保持无状态

当前状态机 definition 不会创建实例，同一个类可以被多个动画蓝图或多个状态机节点引用。

因此不应在类 table 上保存某次构建的可变状态：

```lua
-- 不推荐：不要把某次编译对象写回共享定义类。
function MinimalLocomotion.StateMachine(Machine)
    MinimalLocomotion.CurrentMachine = Machine
end
```

这样会让后一次编译或另一个引用覆盖前一次数据，并产生热重载残留。

推荐做法：

- 拓扑数据保存在传入的 `Machine`；
- Pose 节点保存在传入的 `Graph`；
- 运行时状态保存在真实 AnimInstance/C++ 属性；
- 静态资源和常量通过模块级只读表或 `AnimAssets` 引用。

## 十、状态机定义如何复用

因为状态机定义不持有具体 Graph 实例，它可以被多个动画蓝图引用：

```lua
local locomotion = graph:StateMachine(
    "GroundLocomotion",
    SharedLocomotion)
```

每次使用都会创建独立 `LuaStateMachineNode`、OwnedGraph、State 和 StatePose Graph。共享的是定义函数，不是编译数据或运行时状态。

完整规则函数名会加入节点名，例如：

```text
CanEnter_GroundLocomotion_Idle_Move
```

从而避免同一动画蓝图内多个状态机使用相同 Transition Key 时发生名称冲突。

## 十一、继承有什么用途

状态机类可以继续继承另一个状态机定义：

```lua
local BaseLocomotion =
    LuaAnimStateMachine:Extend("BaseLocomotion")

local SekiroLocomotion =
    BaseLocomotion:Extend("SekiroLocomotion")
```

子类可以复用或 override 方法。但必须注意：

- override 的 `StateMachine()` 需要完整声明最终拓扑；
- 状态名称变化后必须提供对应 `StateGraph_*`；
- Transition Key 变化后必须提供对应 `CanEnter_*`；
- 不要把可变编译状态保存在父类或子类字段中。

当前基类没有提供 `super` 构建协议或拓扑合并机制。继承主要复用方法，不会自动合并两套 State 列表。

## 十二、当前实现边界

- 基类本身没有抽象方法声明，缺少 `StateMachine`、`StateGraph_*` 或 `CanEnter_*` 时由 `ConfigureStateMachine()` 运行到对应阶段才报错；
- definition 当前按类 table 提供函数查找，不创建独立实例，因此不支持每个状态机引用独有的 Lua 配置状态；
- 编译期回调只显式接收 `Machine` 或 `Graph`，运行时规则只显式接收真实 AnimInstance 代理 `Inst`，三者来源不得混用；
- 基类没有自动拓扑继承或合并能力；
- `CanEnter_*` 只返回状态切换条件，不直接计算 Pose，也不应修改 Graph；
- 运行时 Lua 规则的线程和缓存边界由 C++ Transition Bridge 管理，不因为继承 `LuaAnimStateMachine` 就获得动画工作线程执行能力。

## 十三、相关问答

### 这个类只有一行 Extend，能不能删掉？

技术上当前业务定义可以退化为普通 table，但会失去明确类型、继承边界、统一规范和未来扩展入口。保留专用基类能让状态机模块的角色清楚且可检查。

### 独立状态机需要调用 New 吗？

不需要。当前 `LuaAnimBlueprint` 从 definition 类 table 查找声明函数，但调用时只传入显式 `Machine` 或 `Graph`。Graph、State 和 Transition 的实例由对应编译器类创建。

### CanEnter 里能调用状态机类的方法吗？

`CanEnter_*` 必须使用点号和显式 `Inst` 参数，不能把它当作状态机类方法。如果需要纯 Lua 公共函数，应使用模块级 `local function` 或显式引用只读工具模块；需要角色状态时直接读取 `Inst` 暴露的 AnimInstance 属性和函数。

### StateGraph 函数会在运行时调用吗？

不会。它只在编辑器编译动画蓝图时声明状态内部节点。生成资产后，原生 State Graph 在运行时求值。

### 为什么 CanEnter 使用显式 Inst？

这不是 Lua 继承产生的，而是 C++ 调用规则函数时显式传入的。

Lua 函数不会永久绑定到状态机 definition。为了让第一个参数的外部来源一眼可见，项目统一使用点号和显式 `Inst`：

```lua
function MinimalLocomotion.CanEnter_Idle_Move(Inst)
    return Inst.bIsMoving == true
end
```

编译状态机时，`LuaAnimBlueprint` 取得这个裸函数引用，并按包含状态机节点名的完整名称复制到主动画蓝图导出模块：

```text
MinimalLocomotion.CanEnter_Idle_Move
    -> ABP 模块.CanEnter_MainStateMachine_Idle_Move
```

生成的动画蓝图在 `BlueprintUpdateAnimation` 中调用 C++ `EvaluateAndCacheTransitionRule()`。C++ 找到模块中的完整规则函数后执行：

```cpp
UnLua::PushUObject(State, AnimInstance);
lua_pcall(State, 1, 1, 0);
```

`PushUObject` 把当前真实 `UAnimInstance` 包装为 UnLua userdata 并压入 Lua 栈；`lua_pcall` 指定一个参数，所以该 userdata 成为规则函数的第一个形参，也就是 `Inst`。

因此：

```lua
Inst.bIsMoving
Inst.GroundSpeed
Inst:IsFalling()
```

会通过 UnLua 对这个真实 UObject 的反射代理读取 UPROPERTY 或调用 UFUNCTION。`Inst` 在 Lua 中是 userdata 代理，不是 `MinimalLocomotion` table，也不是裸 C++ 指针，但它代表同一个 AnimInstance UObject。

完整运行链分为两段：

```text
游戏线程 BlueprintUpdateAnimation
    -> EvaluateAndCacheTransitionRule(
        当前 AnimInstance,
        主 Lua 模块名,
        完整规则函数名)
    -> require 主模块
    -> 查找规则函数
    -> PushUObject(AnimInstance)
    -> Lua function(self)
    -> 要求严格返回 boolean
    -> 发布线程安全缓存

动画状态机 Transition Graph
    -> GetCachedTransitionRule(
        当前 AnimInstance,
        主 Lua 模块名,
        完整规则函数名)
    -> 只读取最近发布的 bool
    -> 连接 bCanEnterTransition
```

Lua 规则只在游戏线程执行。动画工作线程不运行 Lua、不反射 UObject，只读取由 `FRWLock` 保护的最近一次布尔快照。这就是当前系统既允许规则直接读取 AnimInstance，又不让 Lua 进入动画多线程求值路径的原理。
