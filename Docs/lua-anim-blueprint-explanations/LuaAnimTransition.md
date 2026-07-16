# `LuaAnimTransition.lua` 讲解

源文件：`Content/Script/Animation/Compiler/LuaAnimTransition.lua`

## 一句话定位

`LuaAnimTransition` 是状态机中一条有向 Transition 的编译期记录。

它描述：

- 从哪个 State 出发；
- 进入哪个 State；
- 使用哪个 Lua 规则函数；
- 原生过渡混合多久；
- 同源 Transition 的优先级；
- 使用哪种 Alpha Blend 模式。

它不是运行时 Transition 实例，不保存当前 BlendAlpha、剩余时间或是否正在过渡。

## 创建方式

业务通过状态机节点创建：

```lua
local IdleToMove = Machine:Transition(
    "Idle_Move",
    "Idle",
    "Move")

IdleToMove.BlendDuration = 0.15
IdleToMove.PriorityOrder = 0
IdleToMove.BlendMode = "Linear"
```

`Machine` 把调用转发给内部 `LuaAnimStateMachineGraph`。Graph 负责生成 ID、端点 State ID 和规则函数名，再创建 `LuaAnimTransition`。

## 身份字段

### `Id`

由状态机 Graph ID 和 Transition Key 派生：

```text
StateMachineGraphId + "Transition" + "Idle_Move"
```

它用于稳定 Guid、诊断和原生节点重建。

### `Key`

`Key` 是状态机内部唯一的 Transition 语义名。它不是根据源状态和目标状态临时拼接出来的显示文本，而是业务显式声明的身份。

同一对 State 可以有多条 Transition，但 Key 不能重复。

### `SourceStateId` 与 `TargetStateId`

分别表示有向边的起点和终点。它们是稳定 ID，不是 Lua State 对象，所以能够序列化给 C++，也允许先声明 Transition、后声明 State。

最终 C++ Validator 会确认两个端点都存在于当前状态机。

### `RuleFunctionName`

这是生成的原生 Transition Rule Graph 用来查询缓存的完整函数名，例如：

```text
CanEnter_GroundLocomotion_Idle_Move
```

独立状态机文件中的业务函数仍可写成局部名称：

```lua
---@param Inst userdata 当前 AnimInstance 的 UnLua UObject 代理。
---@return boolean can_enter 是否允许进入 Move。
function GroundLocomotion.CanEnter_Idle_Move(Inst)
    return Inst.bIsMoving == true
end
```

`LuaAnimBlueprint` 编译时把它登记到完整运行时名称。

## 过渡设置

### `BlendDuration`

默认值为 `0.2` 秒。C++ 写入：

```cpp
TransitionNode->CrossfadeDuration =
    Transition.Settings.BlendDuration;
```

它控制源 State Pose 与目标 State Pose 的原生交叉混合时长。`0` 表示立即切换，负数会被 Validator 拒绝。

### `PriorityOrder`

默认使用 Transition 的声明顺序。规范化 IR 会先按 `PriorityOrder` 从小到大排序，再使用声明顺序和 ID 保证结果确定。

同一个源 State 发出的 Transition 必须使用不同的 `PriorityOrder`，否则 Validator 会报错。

该字段最终写入原生 `UAnimStateTransitionNode::PriorityOrder`，由 UE 状态机使用。

### `BlendMode`

Lua 默认值是 `Linear`。当前 C++ Preflight 只接受 `Linear`，生成时固定写入 `EAlphaBlendOption::Linear`。

因此当前字段虽然被保留在 IR 契约中，但还不能实际选择 Cubic、Hermite 等其他模式。设置非 `Linear` 会在生成前失败。

## 为什么可以创建后直接赋值

`LuaAnimTransition` 是简单编译期配置对象，不是 `LuaAnimNode`，因此无需通过 Node Property 契约写入：

```lua
Transition.BlendDuration = 0.12
Transition.PriorityOrder = 1
```

`ToIR()` 才会读取这些最终字段。C++ Validator 负责检查负时长、重复优先级、无效规则名等跨对象约束。

## `DeclarationOrder`

它记录源码声明顺序，从 `0` 开始：

- 没有显式 Priority 时作为默认 Priority；
- Priority 相同时作为确定性排序的后续条件；
- 用于稳定输出和诊断。

正常业务仍应为同一源 State 的多条出口明确指定不同优先级。

## `ToIR()`

```lua
{
    Id = self.Id,
    Key = self.Key,
    SourceStateId = self.SourceStateId,
    TargetStateId = self.TargetStateId,
    RuleFunctionName = self.RuleFunctionName,
    Settings = {
        BlendDuration = self.BlendDuration,
        PriorityOrder = self.PriorityOrder,
        BlendMode = self.BlendMode,
    },
    DeclarationOrder = self.DeclarationOrder,
    SourceLocation = self.SourceLocation,
}
```

这份 IR 足以让 C++ 创建原生 Transition Node、连接两个 State、写入混合设置并生成规则 Graph。

## C++ 生成的 Transition Graph

C++ 为每条 Transition 创建：

- `UAnimStateTransitionNode`；
- 从 Source State 到 Target State 的原生连接；
- `UAnimationTransitionGraph`；
- 默认 `UAnimGraphNode_TransitionResult`；
- `GetCachedTransitionRule()` 调用节点；
- AnimInstance Self、模块名和 `RuleFunctionName` 参数；
- Getter 返回值到 `bCanEnterTransition` 的连接。

生成的原生 Transition Graph **不会直接执行 Lua**。它只读取如下缓存键对应的布尔值：

```text
AnimInstance + LuaModuleName + RuleFunctionName
```

这样动画工作线程读取缓存时不需要访问 Lua VM 或反射系统。

## Lua Rule 的发布与读取

设计上的完整流程应当是：

```text
游戏线程
    -> EvaluateAndCacheTransitionRule(AnimInstance, Module, Rule)
    -> require Lua 模块
    -> 调用 CanEnter_*(Inst)
    -> 发布严格 boolean 到线程安全缓存

原生 Transition Graph
    -> GetCachedTransitionRule(...)
    -> 读取最近一次快照
    -> 输出 bCanEnterTransition
```

Lua Rule 必须返回真正的 boolean。`nil`、数字、字符串、Lua 错误或函数缺失都会按 `false` 处理并记录错误。

## 自动生成的运行时发布入口

C++ Factory 的 `BuildEventGraph()` 会在生成的 AnimBlueprint Event Graph 中创建 `BlueprintUpdateAnimation` override，并按规范化 Transition 顺序串联全部 `EvaluateAndCacheTransitionRule()` 调用。

每个调用节点都自动写入：

- 当前 AnimInstance Self；
- Lua `SourceModule`；
- Transition 的完整 `RuleFunctionName`。

因此发布与读取链路已经生成：

```text
BlueprintUpdateAnimation
    -> EvaluateAndCacheTransitionRule(Rule A)
    -> EvaluateAndCacheTransitionRule(Rule B)
    -> ...

Transition Rule Graph
    -> GetCachedTransitionRule(Current Rule)
```

之前仅搜索普通 C++ 调用表达式会漏掉这条路径，因为 Factory 使用反射取得 `UFunction`，再创建 `UK2Node_CallFunction`，而不是在 C++ 每帧直接调用该函数。

当前仍需注意一个独立边界：外部状态机的完整运行时规则名是在主 Lua 模块执行 `CompileIR()` 时复制到导出 table。编辑器编译后同一 UnLua 环境具备这些函数；全新打包进程只执行 `require` 而未执行 `CompileIR()` 时，仍需保证完整规则函数被静态导出或由运行时安全的初始化步骤准备。这个问题属于 Lua 模块运行时导出生命周期，不是缓存发布链缺失。

## 编译期与运行时边界

`LuaAnimTransition` 只存在于编辑器编译期。PIE 中实际运行的是：

- `FAnimNode_StateMachine` 的 Transition 检查和混合；
- 原生 Transition Rule Graph；
- 线程安全规则缓存；
- 游戏线程上的 Lua Rule 调用入口。

BlendAlpha、源/目标状态权重和过渡剩余时间由 UE 原生状态机管理，不存放在 Lua Transition table 中。

## 相关问答

### `CanEnter_*(Inst)` 返回 true 后，是 Lua 自己切换 State 吗？

不是。Lua 只提供进入条件。原生 Transition Graph 读取条件后，由 `FAnimNode_StateMachine` 决定切换、推进混合并更新状态权重。

### `BlendDuration` 会让 Lua 每帧计算 BlendAlpha 吗？

不会。它被写入原生 Transition Node，BlendAlpha 由 UE 原生状态机计算。

### 为什么规则 Graph 不直接调用 Lua？

动画 Graph 可能在工作线程更新或求值，而 UnLua、UObject 反射和多数游戏逻辑不能直接在该线程安全执行。缓存桥把 Lua 执行留在游戏线程，把工作线程读取限制为加锁的布尔快照。

### 现在 Lua Transition 已经能自动工作吗？

生成资产已经自动包含每帧发布和原生规则图读取链。在编辑器完成 `CompileIR()` 后，PIE 路径可以闭环。全新打包进程仍需验证或修复外部状态机完整规则函数的运行时导出生命周期，避免模块首次 `require` 时尚未包含动态复制的规则名。
