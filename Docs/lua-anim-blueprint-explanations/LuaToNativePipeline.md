# Lua 到 UE 原生动画结构的编译与运行链

## 核心结论

当前方案不是让 Lua 对象直接继承 C++ `FAnimNode`，而是让 Lua充当动画蓝图的**声明语言和编译器前端**：

```text
Lua Graph 声明
    -> Lua IR table
    -> C++ IR Struct
    -> UE 编辑器 Graph 和 UAnimGraphNode
    -> UE Blueprint Compiler
    -> 运行时 FAnimNode 与 FPoseLink
```

Lua 与 C++ 的真正边界有两处：

1. 编辑器编译期：UnLua 把 `CompileIR()` 返回的 Lua table 交给 C++ IR Importer。
2. PIE 运行时：UnLua 把真实 `UAnimInstance` 代理作为 `Inst` 传给 Transition Rule。

## 第一阶段：Lua 声明 Graph

业务代码写的是编译期对象：

```lua
function ABP_Minimal:AnimGraph(Graph)
    local Machine = Graph:StateMachine(
        "MainStateMachine",
        MinimalLocomotion)
    Graph.Result:Connect(Machine.Pose)
end
```

状态机文件声明：

```lua
function MinimalLocomotion.StateMachine(Machine)
    Machine:Entry("Idle")
    Machine:State("Idle")
    Machine:State("Move")
    Machine:Transition("Idle_Move", "Idle", "Move")
end
```

这些 `Graph`、`Machine`、Node 和 Pin 都是 Lua 编译器对象。此时尚未创建 `UAnimBlueprint`，也没有 `FAnimNode_StateMachine`。

## 第二阶段：Lua 导出 IR

主模块返回：

```lua
return ABP_Minimal:Export()
```

`Export()` 生成一个供 C++ `require` 的模块 table，其中包含：

```lua
Module.CompileIR()
```

调用 `CompileIR()` 时，Lua 会创建干净的编译实例，执行 `AnimGraph()`、状态机拓扑函数和所有 `StateGraph_*()`，最后返回纯数据 table：

```lua
{
    SchemaVersion = 2,
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "...",
    Layers = {
        {
            Name = "Main",
            RootGraphId = "...",
            Graphs = { ... },
        },
    },
}
```

Node 也只是一段 IR：

```lua
{
    Id = "...",
    NodeType = "SequencePlayer",
    Pins = { ... },
    Properties = { ... },
}
```

所以 Lua 中的 `Player.Sequence = Path` 不是立刻通过反射修改一个 C++ Sequence Player，而是写入待导出的类型化 Property。

## 第三阶段：C++ 通过 UnLua 导入

`USekiroAnimGraphIRLibrary::CompileLuaModule()` 在编辑器游戏线程中：

1. 获取 UnLua 环境和主 `lua_State`；
2. 调用 `require(LuaModuleName)`；
3. 确认模块返回 table；
4. 查找无参 `CompileIR`；
5. 调用 `CompileIR()`；
6. 严格读取返回 table 的每个字段；
7. 转换成 `FSekiroAnimBlueprintIR`、`FSekiroAnimIRLayer`、`FSekiroAnimIRGraph`、`FSekiroAnimIRNode` 等 C++ 值类型。

Importer 不接受随意的 Lua 隐式转换：

- string 必须是真正的 Lua string；
- boolean 必须是真正的 boolean；
- integer 必须是整数且在 C++ 范围内；
- 数组必须连续；
- 枚举名必须受支持；
- 必填字段缺失会产生稳定诊断代码。

这一步是 Lua table 与 C++ Struct 的明确挂钩点。

## 第四阶段：Validator 与 Node Registry

C++ 不会信任 Lua IR，而会再次验证：

- Schema 版本；
- 稳定 ID 和重复项；
- Graph 所有权；
- Node 是否允许位于当前 GraphType；
- Pin 名、方向、类型和连接数量；
- Node Property 名称和值类型；
- State、Entry 和 Transition 引用；
- 资源路径和父 AnimInstance Class；
- 当前仅支持一个 `Main` Layer。

`FSekiroAnimGraphNodeRegistry` 是节点映射权威。例如：

```text
Lua NodeType "SequencePlayer"
    -> /Script/AnimGraph.AnimGraphNode_SequencePlayer

Lua NodeType "StateMachine"
    -> /Script/AnimGraph.AnimGraphNode_StateMachine
```

因此只在 Lua 中发明一个 `NodeType` 名字不会自动得到 C++ 节点。必须先在 C++ Registry 注册原生类、Pin 和 Property 契约，再提供 Lua 前端包装。

## 第五阶段：Factory 创建原生 AnimBlueprint

`USekiroAnimBlueprintFactoryLibrary` 使用 `UAnimBlueprintFactory` 创建真正的 `UAnimBlueprint`，并设置：

- Parent AnimInstance Class；
- Target Skeleton；
- 资产包和对象名。

随后 `FNativeAnimBlueprintBuilder` 遍历规范 IR。

### 创建 Node

根据 `NodeType` 创建真实编辑器节点：

```text
SequencePlayer  -> UAnimGraphNode_SequencePlayer
StateMachine    -> UAnimGraphNode_StateMachine
Inertialization -> UAnimGraphNode_Inertialization
SaveCachedPose  -> UAnimGraphNode_SaveCachedPose
UseCachedPose   -> UAnimGraphNode_UseCachedPose
```

Node 通过 `FGraphNodeCreator` 完成 UE 标准生命周期，并获得由 Lua稳定 ID 生成的 Guid。

### 写入 Property

例如 SequencePlayer 的 IR Property 会被写入其原生节点数据：

- `Sequence` 加载为真实 `UAnimSequence`；
- `bLoopAnimation` 写入循环设置；
- `PlayRate` 写入播放倍率；
- `StartPosition` 写入起播时间。

### 创建 Pin 连接

C++ 根据 IR Link 找到原生源 Pin 和目标 Pin，再调用当前 AnimationGraph Schema 的 `TryCreateConnection()`。

它不直接强行 `MakeLinkTo()`，因此 UE 原生 Schema 仍负责最终类型和连接规则。

### 创建状态机

StateMachine 使用专门路径生成：

- `UAnimGraphNode_StateMachine`；
- `UAnimationStateMachineGraph`；
- `UAnimStateNode` 和 `UAnimationStateGraph`；
- `UAnimStateTransitionNode`；
- Entry、State 和 Transition 连接；
- 每个 State 内的 Pose Node 网络。

这时在动画蓝图编辑器中看到的已经是普通 UE 原生 Graph，而不是自定义 Lua Host 节点。

## 第六阶段：UE 编译为运行时结构

Factory 完成编辑器 Graph 后调用：

```cpp
FKismetEditorUtilities::CompileBlueprint(...)
```

UE 自己的 AnimBlueprint Compiler 随后把编辑器节点编译为 Generated Class 中的运行时结构：

```text
UAnimGraphNode_SequencePlayer -> FAnimNode_SequencePlayer
UAnimGraphNode_StateMachine   -> FAnimNode_StateMachine
编辑器 Pose Pin/Link           -> FPoseLink
```

所以多线程 Update/Evaluate、Pose 缓冲区、骨骼变换、状态权重和 BlendAlpha 都继续使用 UE 原生实现。

Lua 不需要，也不能在运行时工作线程中模拟 `FPoseLink`。

## PIE 运行时：Pose 路径

进入 PIE 后，主要 Pose 路径不再执行 Lua Graph 声明：

```text
USkeletalMeshComponent
    -> 生成 AnimInstance
    -> AnimInstanceProxy
    -> FAnimNode_StateMachine::Update
    -> FAnimNode_SequencePlayer::Update/Evaluate
    -> FPoseLink 传递 Pose
    -> 输出最终骨骼姿势
```

`AnimGraph()`、`StateMachine(Machine)` 和 `StateGraph_*(Graph)` 都只在生成资产时运行一次，不会每帧调用。

## PIE 运行时：Transition Lua 路径

Transition 是目前保留 Lua 运行时逻辑的部分。

Factory 为每条 Transition 生成独立 Rule Graph：

```text
FAnimNode_StateMachine
    -> 检查当前状态出边
    -> Transition Rule Graph
    -> EvaluateLuaTransitionRule(Current Rule)
    -> bCanEnterTransition
```

对应出边被检查时，调用在游戏线程执行：

1. 通过 UnLua `require(SourceModule)`；
2. 按完整 `RuleFunctionName` 查找函数；
3. 创建可直接访问反射属性和函数的 AnimInstance `Inst` 代理；
4. 调用 `CanEnter_*(Inst)`；
5. 要求返回严格 boolean；
6. 将结果与原生 Gate 组合后直接交给 Transition Result。

生成的 `BlueprintUpdateAnimation` Event 只负责 Lua 动画参数更新，不会全量计算 Transition。Lua 来源 AnimBlueprint 关闭多线程 Update，以便 Rule Graph 安全进入 UnLua。

实际状态切换、过渡混合和 BlendAlpha 仍由 `FAnimNode_StateMachine` 完成。

## Lua 与 C++ 到底是什么关系

| Lua 对象或值 | C++/UE 对应方式 |
|---|---|
| `LuaAnimBlueprint` | 生成 `FSekiroAnimBlueprintIR`，不是 `UAnimBlueprint` 子类实例 |
| `LuaAnimGraph` | 生成 `FSekiroAnimIRGraph`，随后物化为 `UAnimationGraph` |
| `LuaAnimNode` | 生成 `FSekiroAnimIRNode`，Registry 决定真实 `UAnimGraphNode` 类 |
| `LuaAnimPin` | 生成 Link 端点，Factory 查找真实 `UEdGraphPin` |
| `LuaAnimState` | 生成 `FSekiroAnimIRState`，Factory 创建 `UAnimStateNode` |
| `LuaAnimTransition` | 生成 IR Transition，Factory 创建原生 Transition 和规则 Graph |
| `Inst` | UnLua 包装的真实 `UAnimInstance` UObject 代理 |

前六项是“描述并生成”，最后一项才是“运行时直接绑定真实 UObject”。

## 当前需要继续关注的边界

1. Factory 当前只支持一个 `Main` IR Layer，不是真正的 UE Animation Layer。
2. Node 能力仅限 C++ Registry 已注册类型。
3. Transition Lua 在游戏线程执行，不能直接参与动画工作线程 Pose 求值。
4. 外部状态机完整规则名目前由 `CompileIR()` 动态复制到主模块 table；需要验证全新打包进程首次 `require` 时的导出可见性。
5. 生成资产是普通原生 AnimBlueprint，后续 Lua 源码变更不会自动改变已生成资产，必须重新编译生成。
