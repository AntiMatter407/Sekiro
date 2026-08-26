# Lua AnimBlueprint 编译器设计

> 状态：编译前端、原生 NodeFactory、一键资产生成与 PIE Transition Rule 闭环已完成  
> 更新：2026-07-14

## 目标

Lua 不在运行时模拟 `FAnimNode`、`FPoseLink` 或资产播放器 Tick。Lua 作为动画蓝图的编译器前端，声明 Graph、Node、Pin、Link、StateMachine 和 Transition；编辑器模块将声明转换为语言无关 IR，再生成并编译原生 `UAnimBlueprint`。Cook 后的动画更新、Pose 求值、Root Motion、Notify 和状态过渡仍由 UE 原生 C++ 动画系统执行。

```text
Lua 动画蓝图源码
    -> Lua DSL
    -> AnimGraph IR
    -> IR Validator / Canonicalizer
    -> AnimBlueprint Node Factory
    -> UAnimBlueprint + GeneratedClass
    -> UE 动画线程 Update / Evaluate
```

## 模块边界

| 模块 | 类型 | 职责 |
|------|------|------|
| `SekiroAnimBlueprintExt` | Runtime | 后续仅放生成资产运行时确实需要的通用接口；第一阶段保持最小模块 |
| `SekiroAnimBlueprintExtEditor` | UncookedOnly | IR、校验器、Lua 前端、节点工厂和动画蓝图生成器，不进入打包游戏 |
| `Content/Script/Animation/Sekiro/AnimAssets.lua` | 纯数据 | 保存动画资源语义名与 UE 资源路径，不承担状态机和 Graph 逻辑 |

插件禁止包含 Sekiro 角色、Locomotion 状态名或项目资源路径。角色动画图只存在于后续 Lua 源码中。

## 第一阶段：稳定 IR

IR 是 Lua 与 C++ 编译器之间的唯一契约，并且不直接引用具体 `UAnimGraphNode`。节点通过可扩展 `NodeType` 注册名描述，后续由 NodeFactory 映射到 UE 节点类型。

### 身份规则

- Blueprint、Layer、Graph、Node、State 和 Transition 使用可读的稳定字符串 ID。
- ID 来自源码语义路径，不使用数组下标、内存地址或运行时 UObject 名称。
- Pin 在所属 Node 内使用稳定名称，Link 使用 `NodeId + PinName` 端点。
- Transition 的执行优先级使用显式字段保存；确定性排序不得改变优先级语义。

建议 ID：

```text
Layer/GroundLocomotion
Layer/GroundLocomotion/Graph/MainAnimGraph
Layer/GroundLocomotion/Graph/MainAnimGraph/Node/Locomotion
Layer/GroundLocomotion/Graph/MainAnimGraph/Node/Locomotion/Graph/StateMachine
Layer/GroundLocomotion/Graph/MainAnimGraph/Node/Locomotion/Graph/StateMachine/State/Idle
Layer/GroundLocomotion/Graph/MainAnimGraph/Node/Locomotion/Graph/StateMachine/State/Idle/Graph/StatePose
Layer/GroundLocomotion/Graph/MainAnimGraph/Node/Locomotion/Graph/StateMachine/Transition/Idle_Start
```

### 数据组成

- Blueprint：SchemaVersion、Lua SourceModule、父 AnimInstance 类路径、目标 Skeleton 资产软路径、Layers。
- Layer/Graph：稳定 ID、名称、Graph 类型、Root Node、Nodes、Links、StateMachine 内部拓扑。
- Node：稳定 ID、NodeType、显示名、可选 OwnedGraphId、输入/输出 Pins、类型化 Properties。
- Pin/Link：方向、数据类型、连接端点。
- StateMachine Node：位于 Pose 或 StatePose Graph，输出 Pose，并通过 OwnedGraphId 独占一个 StateMachine Graph。
- StateMachine Graph：EntryStateId、States、Transitions，不能直接作为 Layer 根输出。
- State：独占 StatePose Graph、`bAlwaysResetOnEntry`；Graph ID 由 State ID 派生。
- Transition：状态机内唯一 Key、From/To、强类型原生 Rule AST、可选兼容 `CanEnter_*` 函数名、显式 Priority、Blend 设置。
- SourceLocation/Diagnostic：Lua 模块、行号、错误代码、严重级别和消息。

属性值使用明确类型容器，第一阶段支持 Bool、Integer、Float、Name、String、SoftObjectPath 和 SoftClassPath。禁止把属性表拼成 JSON 字符串后再由后端猜类型。

### 校验顺序

1. 检查空 ID、重复 ID 和重复 Pin。
2. 检查 Root、Node、Pin、Graph、State 和 Transition 引用。
3. 检查 Pin 方向、Pin 类型兼容和单输入多连接。
4. 检查 Pose Graph 环。
5. 检查 StateMachine Entry 和 Transition 两端状态。
6. 检查 Transition 规则函数名和优先级冲突。
7. Canonicalize 后输出确定性 IR。

只要存在 Error 级 Diagnostic，后续 AnimBlueprint 生成阶段就不得执行。

第一阶段实现额外约束如下：

- `SchemaVersion` 当前必须为 `2`，并且必须声明 `SourceModule`、父 `AnimInstance` 类与普通动画蓝图使用的 `TargetSkeleton`。
- `TargetSkeleton` 必须是显式顶层资产对象软路径；Validator 只检查路径结构，不加载资产、不从父类或 SequencePlayer 推导，也不在本阶段判断 UObject 类型。
- 当前 IR 只描述普通具体 AnimBlueprint；UE 原生 `bIsTemplate + null TargetSkeleton` 的模板蓝图语义留待模板/动画层接口阶段扩展。
- Graph、Node 和 Pin 必须声明注册类型；Node 内 Property 名不可为空或重复。
- Layer 根必须是所属 Layer 内的 Pose Graph。
- `StateMachine` Node 必须拥有同 Layer 内的 StateMachine Graph；内部 Graph 必须恰好有一个所有者。
- State 必须独占同 Layer 内的 StatePose Graph；禁止复用 Layer 根、跨 Layer 引用或多个 State 共享 Graph。
- Graph 所有权边不得形成环；嵌套状态机必须保持 `Pose/StatePose -> StateMachine -> StatePose` 的有向所有权结构。
- Transition Key 必须在所属状态机内唯一并满足 Lua 标识符规则；同一 From/To 可用不同 Key 声明并行 Transition。
- 默认规则名为 `CanEnter_<StateMachineNode>_<TransitionKey>`，所有显式或默认规则函数名在动画蓝图 Lua 类中必须唯一。
- 同一源 State 的 Transition 必须使用不同的显式优先级，`BlendDuration` 不得为负数。
- Canonicalize 按显式 Transition 优先级排序，不以稳定 ID 改写过渡语义。

## PIE 调试边界

后续方案允许在 PIE 中调试 Lua 的 Transition 与动画更新逻辑，但编译期和运行期职责必须分开：

- `BuildAnimGraph`、Node/Link 声明和资产生成属于编辑器编译期，只在重新生成动画蓝图时执行和调试。
- `NativeUpdateAnimation`、`BlueprintUpdateAnimation` 与兼容旧模块的 `CanEnter_*` 属于 PIE 运行期，可通过 UnLua 与 Rider Lua 调试器断点调试。
- 新 Transition 使用 BoolProperty、Curve、TimeRemaining、All/Any/Not 强类型 AST，由 Factory 生成完全原生的 Rule Graph；仅旧模块按需调用 Lua `CanEnter_*`。
- Lua 来源 AnimBlueprint 仍因 `BlueprintUpdateAnimation` 暂时关闭多线程动画更新；本阶段不改变线程模型。
- Pose、Root Motion、Notify、SequencePlayer 和混合仍由 UE 原生节点执行；Lua 不直接操作 `FCompactPose` 或在工作线程进入 UnLua VM。

因此，Lua 可以像蓝图逻辑一样参与 PIE 调试，但不在动画工作线程直接执行 Lua VM，也不替代原生 Pose 求值。

## 第二阶段：Lua OOP 编译前端

Lua 编译前端位于 `Content/Script/Animation/Compiler/`，类关系如下：

```text
CompilerClass
├─ LuaAnimBlueprint
├─ LuaAnimLayer
├─ LuaAnimGraph
│  └─ LuaAnimStateGraph
├─ LuaAnimNode
│  └─ LuaStateMachineNode
├─ LuaAnimState
└─ LuaAnimStateMachineGraph
```

这里的类只模拟动画蓝图编辑器声明行为，不模拟 `FAnimNode` 的运行时生命周期：

- `LuaAnimBlueprint:BuildAnimGraph` 是子类唯一必须 override 的编译期入口。
- `AnimationLayer` 和 `PoseGraph` 负责声明主图作用域；Layer 根必须显式设置为 Pose Graph。
- 普通节点统一由 `LuaAnimGraph:Node` 创建 `LuaAnimNode`，编辑器类路径来自 `EditorNodeClass`；NodeType 只复用已有 Pin 契约和结构型适配器。
- `LuaStateMachineNode` 是 Pose 节点，并拥有 `LuaAnimStateMachineGraph`；`State` 创建 `LuaAnimState` 及其独占 `LuaAnimStateGraph`。
- `LuaAnimStateGraph` 使用 `StatePose` GraphType 和 `StateResult` 根节点，与主图的 `Pose + OutputPose` 保持原生语义区分。
- `Export().CompileIR` 每次创建全新实例，避免 UnLua 热重载后残留上一次 Graph 数组。
- 模块导出表沿继承链暴露子类方法，为 PIE 调用 `BlueprintUpdateAnimation` 与兼容旧模块的 `CanEnter_*` 保持同一模块形态；纯原生 Rule 不注册运行时函数。

### Lua 与 C++ 边界

Lua 导出字段名与 IR USTRUCT 保持一致，集合统一使用 1-based 数组。C++ 导入器逐字段读取并检查 Lua 类型，不接收 JSON，也不根据字符串内容猜测 Property 类型。

```text
require(ModuleName)
    -> Module.CompileIR()
    -> Lua Blueprint IR table
    -> 显式字段/类型解析
    -> FSekiroAnimBlueprintIR
    -> Canonicalize + Validate
```

导入或 DSL 错误统一转换为稳定 `IR.Lua*` Diagnostic，并携带模块名与可用源码行号。导入器在非 PIE 编辑器命令行环境中也必须按需启动 UnLua Env。

## 与 UE5.2 原生结构的对应

```text
UAnimationGraph
  UAnimGraphNode_Root
    <- UAnimGraphNode_StateMachine
         EditorStateMachineGraph -> UAnimationStateMachineGraph
           UAnimStateNode -> UAnimationStateGraph -> UAnimGraphNode_StateResult
           UAnimStateTransitionNode -> Transition Rule Graph
```

Schema v2 已对齐外层 StateMachine 节点、内部 StateMachine Graph、State 独占 StatePose Graph 及其所有权 DAG。IR 的 Link 表示编译期 Pose 连接，后续由 UE 编译器写入 `FPoseLink`；Lua 不应把 `FPoseLink` 伪装成可执行节点。`FBakedAnimationStateMachine`、`StatePoseLinks`、节点索引和 Debug 映射属于 UE 编译产物，不进入 Lua IR，也不由 Lua 手工维护。

### NodeType 注册契约

C++ NodeType 注册表是节点结构的唯一权威，保存编辑器节点类路径、允许放置的 GraphType、固定 Pin、可写 Property、根节点角色和 OwnedGraph 规则。Lua 的 `NodeContracts.lua` 只镜像这些信息以提供 IDE 类型和快速失败；C++ Validator 必须再次独立校验，后续 NodeFactory 也只读取 C++ 注册表。

| NodeType | UE 编辑器节点 | GraphType | Pin | Property / OwnedGraph |
|----------|---------------|-----------|-----|-----------------------|
| `OutputPose` | `AnimGraphNode_Root` | `Pose` 固定根 | `Result: Input Pose` | 无 |
| `StateResult` | `AnimGraphNode_StateResult` | `StatePose` 固定根 | `Result: Input Pose` | 无 |
| `SequencePlayer` | `AnimGraphNode_SequencePlayer` | `Pose`、`StatePose` | `Pose: Output Pose` | `Sequence` 必填；循环、速率和起播时间可选 |
| `StateMachine` | `AnimGraphNode_StateMachine` | `Pose`、`StatePose` | `Pose: Output Pose` | 必须拥有 `StateMachine` Graph |
| `Inertialization` | `AnimGraphNode_Inertialization` | `Pose`、`StatePose` | `Source: Input Pose`、`Pose: Output Pose` | 无 |
| `FootPlacement` | `AnimGraphNode_FootPlacement` | `Pose`、`StatePose` | `ComponentPose`、`Alpha`、`Pose` | IK 根、骨盆和双腿定义必填；地面检测、种植速度和 `PlantLockType` 可选 |
| `LegIK` | `AnimGraphNode_LegIK` | `Pose`、`StatePose` | `ComponentPose`、`Alpha`、`Pose` | 双腿 IK/FK 定义必填；精度与迭代数可选 |

IR 的 `Pins` 是完整一致性断言，不是创建真实 Pin 的命令。未知 NodeType、缺失或多余 Pin、Pin 方向/类型/连接数不一致、未知 Property、必填 Property 缺失、Property 类型不一致、错误 GraphType 或 OwnedGraph 都必须在创建资产前被拒绝。Link 解析使用注册 Pin，不信任 Lua 自报结构。

NodeFactory 和后续节点扩展阶段需要补齐的原生概念：

| 差异 | 当前状态 | 处理阶段 |
|------|----------|----------|
| 专用 Graph/Result UObject | 已生成对应原生 Graph、Result/Entry UObject，并保持正确 Outer/SubGraphs | 已完成 |
| Transition `BoundGraph` 和 bool Result | 已生成原生规则 Graph，并连接按需直调 Lua 规则的纯函数节点 | 已完成 |
| 自定义 Transition Blend Graph、BlendProfile、CustomBlendCurve | IR 尚未声明 | 节点扩展阶段 |
| Conduit、State Alias、Bidirectional/Shared Rule | IR 尚未声明 | 状态机扩展阶段 |
| AnimLayer 接口签名、输入 Pose、Linked Anim Layer | 当前 Layer 仅是编译作用域 | AnimationLayer 阶段 |
| Pin 的完整 K2 类型和 Optional Pin 暴露规则 | 注册表已覆盖稳定名称、方向和注册数据类型；完整 `FEdGraphPinType` 与 Optional Pin 暴露仍待节点生成时从真实节点核对 | NodeFactory 阶段 |
| SequencePlayer 同步组、Role、Method、PlayRateBasis | 当前只覆盖最小播放器属性 | AssetPlayer 扩展阶段 |
| Lua 规则的游戏线程按需求值 | Lua 来源资产关闭多线程 Update，Transition Graph 使用 AnimInstance、模块名和规则名直接求值 | 已完成 |

## 第三阶段：原生 NodeFactory

NodeFactory 只消费已经通过 Validator 的规范 IR，并把声明还原为 UE 编辑器原生对象。真实 Pin、内部 Graph 和默认 Result/Entry 节点均由对应 Schema 与节点生命周期创建；IR 中的 Pin 只用于校验和稳定端点解析，不直接构造 `UEdGraphPin`。

生成顺序固定为：

1. 再次验证 IR，并在创建 UObject 前加载父 `UAnimInstance` 类与目标 `USkeleton`。
2. 通过 UE 的 AnimBlueprint 工厂创建蓝图及默认 `UAnimationGraph`、`UAnimGraphNode_Root`。
3. 递归创建注册节点；`StateMachine`、`State` 和 `Transition` 通过 `PostPlacedNewNode` 创建各自拥有的原生内部 Graph。
4. 将 IR 属性写入真实节点，并按注册 Pin 名解析原生 Pin，通过 Graph Schema 建立 Pose Link。
5. 连接 StateMachine Entry、State 与 Transition 拓扑，最后调用 UE 蓝图编译器验证生成类。

当前后端支持一个普通主动画层，以及播放器、状态机、混合、缓存姿势、空间转换、Orientation Warping、Foot Placement 和 Leg IK 等已注册节点。多动画层、模板 AnimBlueprint 和自定义 Transition Blend Graph 必须返回明确诊断，不能被静默忽略。

`OutputPose` 与 `StateResult` 映射到 Schema 已创建的唯一默认 Result 节点，不重复创建。StateMachine Node 拥有 `UAnimationStateMachineGraph`，State 拥有 `UAnimationStateGraph`，Transition 拥有 `UAnimationTransitionGraph`；这些 Graph 同时保持正确的 UObject Outer 和父 Graph `SubGraphs` 关系。IR 的 Graph、Node、State 与 Transition 稳定 ID 映射为确定性 `GraphGuid`/`NodeGuid`，为后续增量重建和调试映射提供身份基础。

Transition 后端对强类型 Rule AST 直接生成 Bool 属性 Getter、Curve、TimeRemaining 与布尔组合节点，不创建 `EvaluateLuaTransitionRule`。兼容旧模块仍可生成 Lua 调用，并将返回值与附加 Gate 做 AND。`BlueprintUpdateAnimation` Event 继续负责 Lua 动画参数更新；Lua 来源资产仍关闭多线程动画更新，本阶段不调整其他 Runtime Lua 行为。

一键入口 `CompileLuaModuleToAnimBlueprintAsset` 串联 `LuaModule -> CompileIR -> NodeFactory -> Blueprint Compile -> SavePackage`。它只创建新资产并拒绝覆盖，失败通过结构化 Diagnostic 返回；成功后生成物是标准 `UAnimBlueprint` 与 `GeneratedClass`，运行时不依赖编辑器模块。

## 编辑器源码模式

现有标准 `UAnimBlueprint` 通过持久化的 `USekiroLuaAnimBlueprintExtension` 保存 Lua 模块名和 `Source Mode`。插件使用官方 `IAnimationBlueprintEditorModule` Toolbar Extender，在原生 Compile 区段后增加以下控件：

| 控件 | 行为 |
|------|------|
| `Check Lua` | Lua require/HotReload、IR 构建、Validator 和资产预检；只缓存成功 IR 并输出文件、行、列诊断 |
| `Generate From Lua` | 用当前修订成功 IR 事务性重建 Graph；不保存，也不调用目标动画蓝图原生编译 |
| `Source: Native/Lua` | 持久化选择普通 Compile/F7 的源码 |

Native 模式保留捕获到的 UE 原生 `FUIAction`，不会运行 Lua 或修改 Graph。Lua 模式的 Compile/F7 固定执行 `Check -> Generate -> Original Compile`，只有前两步成功才委托原动作一次。命令包装发生在官方 Toolbar Extender 已提供有效 `FUICommandList` 后，不在 ToolMenus 动态构建阶段读取 ToolkitCommands。

Graph 提交前完成 Lua、IR 和资源预检；实际提交位于编辑器事务内。AnimGraph、EventGraph 或 Root 缺失时按 UE 原生 Schema 重建外壳，物化失败则撤销事务，因此失败 Lua 不会破坏当前可用 Graph。旧的 `HandlePreloadObjectsForCompilation` 隐式方案不再使用。

## IR 反向写回 Lua

`FSekiroAnimGraphIRLuaWriter` 将 Reader 产生的 IR 先经 `Canonicalize + Validate`，再按 USTRUCT 字段声明顺序写为可读的纯 IR table。数组顺序由 Canonical IR 确定，字符串使用 Lua 双引号转义，整数、双精度浮点、所有 `IRValue` 联合字段、Transition Gate 与 `Layout.Positions` 均完整保留。模块只公开 `CompileIR()`，并通过 `IR.SourceModule` 继承原运行时模块的 Transition Rule；因此交换模块不会取代 EventGraph `BlueprintUpdateAnimation` 桥接使用的原模块。

`AnimBlueprintToLua` 固定写入 `<LuaModuleName>.generated.lua`，手写 `<LuaModuleName>.lua` 从不是写目标。模块名必须由 ASCII 字母、数字和下划线组成的点分段，拒绝绝对路径、分隔符、空分段、`..` 和 ScriptRoot 越界。安全提交流程为：

1. Reader 读取当前标准 `UAnimBlueprint`，Writer 仅在内存生成文本。
2. 写入 ScriptRoot 内同目录唯一临时模块，强制 UTF-8 无 BOM。
3. 用现有 `CompileLuaModule` 回读临时模块，并比较两份 Canonical IR。
4. 仅当回读完全相等时替换 `.generated.lua`；旧目标默认保留一份 `.bak`，任何失败都恢复旧文件。

`USekiroLuaAnimBlueprintExtension.GeneratedLuaModuleName` 仅表示编辑器交换源。`Check Lua` 和 Lua → AnimBlueprint 优先读它，空值时回退到旧资产的 `LuaModuleName`；运行时 Bridge 始终使用 IR 内的 `SourceModule`。本阶段不增加工具栏、自动 Hash 或隐式覆盖行为。

## 后续阶段

1. Lua DSL 始终只转换为 IR，不直接调用编辑器 UObject。
2. 扩展 Blend、BlendSpace、Slot、Curve 和 AnimLayer；`Inertialization` 与嵌套状态机的最小原生后端已经完成。
3. 增加全局 `BlueprintUpdateAnimation` Lua override 和状态参数更新契约；当前闭环只负责 Transition Rule。
4. 增加源码变更监听、自动重新生成、Cook 校验和生成资产只读保护。
