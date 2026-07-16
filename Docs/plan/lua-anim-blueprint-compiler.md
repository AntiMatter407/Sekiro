# Lua AnimBlueprint 编译器进度

> 状态：Lua 编译、原生资产生成与 PIE Transition Rule 闭环已完成  
> 创建：2026-07-13  
> 设计：[lua-anim-blueprint-compiler.md](../design/lua-anim-blueprint-compiler.md)

## 当前进度

| 编号 | 任务 | 状态 |
|------|------|------|
| 1 | 归档旧动态 Lua PoseGraph 实现 | 已完成，提交 `503a441d` |
| 2 | 删除旧插件和旧 Animation 逻辑 Lua | 已完成 |
| 3 | 恢复纯资源表 `AnimAssets.lua` | 已完成 |
| 4 | 解除游戏代码对旧插件运行时 API 的依赖 | 已完成 |
| 5 | 创建最小 Runtime + UncookedOnly 插件模块 | 已完成 |
| 6 | 定义语言无关 AnimGraph IR | 已完成 |
| 7 | 实现 Validator 与 Canonicalizer | 已完成 |
| 8 | IR Automation Tests | 已完成，12/12 通过 |
| 9 | Lua AnimGraph Function API 与原生拓扑契约 | 已完成，52/52 测试通过 |
| 10 | 原生 AnimBlueprint NodeFactory | 已完成，48/48 测试通过 |
| 11 | 自动生成最小 SequencePlayer 状态机 | 已完成，端到端保存与运行时测试通过 |

## 第一阶段完成标准

- 同一份声明重复构建得到完全一致的 Canonical IR。
- 无效 ID、坏 Link、Pin 类型错误、Pose 环和无效状态机均产生稳定 Diagnostic。
- IR 不依赖项目角色类型、资源路径和具体 AnimGraphNode 类。
- IR 与校验器只存在于 UncookedOnly 编辑器模块。
- 自动化测试和 UE5.2 编译通过。

## 第一阶段验收记录

- `SekiroAnimBlueprintExt` Runtime 模块保持空壳，不包含旧 Lua PoseGraph、项目角色类型或资源路径。
- `SekiroAnimBlueprintExtEditor` 为 `UncookedOnly`，承载 IR、Canonicalizer、Validator 与测试。
- IR 已覆盖 Blueprint、Layer、Graph、Node、Pin、Link、StateMachine、State、Transition、类型化 Property 和 Lua SourceLocation。
- Validator 已覆盖 Schema 与必填契约、稳定 ID、引用作用域、Pin 方向/类型/连接数、Pose 环、状态机 Entry、Transition 规则/混合时长/优先级冲突。
- `SekiroEditor Win64 Development` 编译通过。
- `Automation RunTests Sekiro.AnimGraphIR` 实际执行 12 项，12 项通过，退出码为 0。

第一阶段不包含 Lua DSL、AnimBlueprint 资产生成、NodeFactory 或 PIE 运行时 Lua 回调；这些从第二阶段开始实现。

## 第二阶段任务

| 编号 | 任务 | 状态 |
|------|------|------|
| 9.1 | `LuaAnimBlueprint` 继承与干净编译实例 | 已完成 |
| 9.2 | Layer、PoseGraph、Node、SequencePlayer 类 | 已完成 |
| 9.3 | StateMachine、State、Entry、Transition 类 | 已完成 |
| 9.4 | 自动稳定 ID、类型化 Property、Lua SourceLocation | 已完成 |
| 9.5 | UnLua 模块到 C++ IR 的显式导入器 | 已完成 |
| 9.6 | 合法、错误、确定性端到端测试 | 已完成，16/16 通过 |
| 9.7 | 第二阶段编写手册与验收记录 | 已完成 |
| 9.8 | StateMachine Node + Owned StateMachine Graph 原生拓扑修正 | 已完成，29 项回归测试覆盖 |
| 9.9 | State 独占 StatePoseGraph + Graph 所有权 DAG | 已完成，29/29 测试通过 |
| 9.10 | 显式 Transition Key 与状态机作用域规则名 | 已完成，34/34 测试通过 |
| 9.11 | Blueprint TargetSkeleton 契约 | 已完成，37/37 测试通过 |
| 9.12 | NodeFactory Pin/Property 注册表权威契约 | 已完成，43/43 测试通过 |
| 9.13 | Graph 内直接创建节点、设置属性并连接类型化 Pin | 已完成 |
| 9.14 | 基类自动构建 Main AnimGraph 与状态机分文件约定 | 已完成 |
| 9.15 | 原生 Save/Use Cached Pose 节点与同图姿势复用 | 已完成，52/52 测试通过 |

第二阶段不创建或修改 `UAnimBlueprint` 资产。Lua 模块现可通过 `CompileIR` 生成完整声明，C++ 无 JSON 地导入为 `FSekiroAnimBlueprintIR` 并通过 Validator；StateMachine 作为 PoseGraph 内的 AnimNode，并通过 `OwnedGraphId` 持有内部 StateMachine Graph。

## 第二阶段验收记录

- `CompileLuaModule` 在非 PIE 环境按需启动 UnLua，执行 `require -> CompileIR` 并保持 Lua 栈平衡。
- C++ 显式解析所有 IR 结构和七种 Property Value，拒绝稀疏数组、错误字段类型和非法枚举。
- 导入错误使用稳定 `IR.Lua*` Diagnostic，不以断言或 JSON 解析失败结束流程。
- Schema v1 修正前的 `Sekiro.AnimGraphIR` 自动化测试曾执行 16 项并全部通过。
- Schema v2 已新增 `StatePose` GraphType；主图使用 `Pose + OutputPose`，每个 State 使用独占的 `StatePose + StateResult`。
- Validator 已覆盖 State Graph 缺少 Owner、多 Owner、复用 Layer 根、类型错误，以及 Node/State 所有权边形成的 Graph 环。
- Transition 现使用状态机内唯一显式 Key 生成稳定 ID；相同 Source/Target 可通过不同 Key 和优先级声明并行规则。
- 默认规则名为 `CanEnter_<StateMachineNode>_<TransitionKey>`；Validator 会拒绝非法 Lua 标识符、Lua 保留字和 Blueprint 范围内重复规则名。
- Blueprint IR 现要求显式 `TargetSkeleton` 顶层资产软路径；Validator 不加载资产，也不从父类或动画节点隐式推导 Skeleton。
- C++ NodeType 注册表现为 Pin、Property、允许 GraphType、根节点角色和 OwnedGraph 策略的唯一权威，并为后续 NodeFactory 保存 UE 编辑器节点类软路径。
- 内置契约覆盖 `OutputPose`、`StateResult`、`SequencePlayer`、`StateMachine`、`Inertialization`、`SaveCachedPose` 和 `UseCachedPose`；Link 端点只按注册 Pin 解析，不信任 Lua 自报 Pin。
- Lua 已移除任意 `AddPin` 入口；`NodeContracts.lua` 只镜像 C++ 契约用于前端快速失败，IR Pins 作为完整一致性断言。
- `Sekiro.AnimGraphIR` 于 2026-07-14 最终实际执行 52 项，52 项全部通过；新增真实 `Animation.Examples.ABP_Minimal` 模块导入测试。
- `ABP_Minimal` 已改为 AnimGraph Function API：主文件直接声明 Node/Pin，状态机拆分到独立文件，并可导出 1 个 Layer、4 个 Graph、2 个 State、2 条 Transition 和原生 Cached Pose 节点。
- Lua 5.4 独立负向烟测确认未知 NodeType、未知 Property、Property 类型错误和缺失必填 `Sequence` 均会失败；Lua 函数文档检查为 176/176。
- 同一真实模块重复编译的稳定 ID 一致。

## 第三阶段任务

| 编号 | 任务 | 状态 |
|------|------|------|
| 10.1 | transient 与内容包 AnimBlueprint 创建接口 | 已完成 |
| 10.2 | 原生 Pose、StatePose、StateMachine、Transition Graph 物化 | 已完成 |
| 10.3 | 首批注册节点、属性和 Pose Pin 连接 | 已完成 |
| 10.4 | 嵌套状态机、稳定 Guid、失败诊断与编译测试 | 已完成，5 项工厂测试通过 |

## 第三阶段验收记录

- `USekiroAnimBlueprintFactoryLibrary` 可从已验证 IR 创建并编译 transient `UAnimBlueprint`，也可在指定内容包创建不覆盖已有资产的对象并标记 package dirty。
- 工厂在产生 UObject 前再次执行 Validator，并预检父 `UAnimInstance` 类、目标 `USkeleton`、注册节点类、Sequence 资产、Skeleton 兼容性和 Transition BlendMode。
- 主 Pose Graph 复用工厂默认 `UAnimGraphNode_Root`；StatePose Graph 复用 Schema 默认 `UAnimGraphNode_StateResult`，不重复创建 Result 节点。
- `StateMachine`、State 和 Transition 通过 UE 原生 `PostPlacedNewNode` 生命周期创建 `UAnimationStateMachineGraph`、`UAnimationStateGraph`、`UAnimationTransitionGraph` 及默认 Entry/Result。
- 已物化 `OutputPose`、`StateResult`、`SequencePlayer`、`StateMachine`、`Inertialization`、`SaveCachedPose` 和 `UseCachedPose`；Sequence、循环、播放速率和起播位置可写入原生节点。
- Pose Link 通过真实 Pin 与 Graph Schema 建立；Entry、并行 Transition、混合时长、优先级和 Linear BlendMode 已落到原生状态机拓扑。
- StatePose 内嵌 StateMachine 可递归生成；IR Graph、Node、State、Transition 稳定 ID 会生成确定性 `GraphGuid`/`NodeGuid`。
- `SekiroEditor Win64 Development` 编译通过；`Automation RunTests Sekiro.AnimGraphIR` 于 2026-07-14 最终实际发现并执行 52 项，52 项全部成功，退出码为 0。
- 第三阶段验收时只生成 Transition 原生规则 Graph 和默认布尔 Result；第四阶段现已接通 `RuleFunctionName` 缓存并提供保存入口，覆盖与增量重建仍未开放。

## 第四阶段任务

| 编号 | 任务 | 状态 |
|------|------|------|
| 11.1 | `LuaModule -> CompileIR -> NodeFactory -> 保存资产` 一键入口 | 已完成 |
| 11.2 | EventGraph 自动生成 `BlueprintUpdateAnimation` override | 已完成 |
| 11.3 | 游戏线程执行 Lua `CanEnter_*` 并发布规则快照 | 已完成 |
| 11.4 | Transition Rule Graph 在线程安全缓存上读取 bool Result | 已完成 |
| 11.5 | 一键保存、运行时 true/false 规则和缓存隔离测试 | 已完成，51/51 全套测试通过 |

## 第四阶段验收记录

- `CompileLuaModuleToAnimBlueprintAsset` 可从模块名完成 Lua 导入、IR 校验、原生 NodeFactory、蓝图编译和 package 保存；已有资产仍明确拒绝覆盖。
- 生成的 `BlueprintUpdateAnimation` Event 会按 Canonical IR 顺序调用 `EvaluateAndCacheTransitionRule`，并把实际 AnimInstance 作为 `self` 传给 Lua 规则。
- Lua VM 只在游戏线程运行；缓存以 AnimInstance、Lua 模块名和规则函数名三者隔离，并用 `FRWLock` 发布给动画工作线程。
- 每个原生 Transition Graph 使用标记为 `BlueprintThreadSafe` 的 `GetCachedTransitionRule` 连接 `bCanEnterTransition`，不在动画线程调用 Lua 或 UObject 反射。
- Lua 的 `CanEnter_*` 可在 PIE 中通过 UnLua/Rider Lua 断点调试；热重载后的下一次规则刷新通过 `require` 取得当前模块导出表。
- `SekiroEditor Win64 Development` 编译通过；`Automation RunTests Sekiro.AnimGraphIR` 于 2026-07-14 最终实际执行 52 项并全部成功。

## 遗留风险

- Commandlet 加载旧 `ABP_Sekiro.uasset` 时仍会报告其引用已删除的 `AnimGraphNode_SekiroLuaStateMachine` 结构；该错误不影响 52 项编译器测试，但在同一路径生成正式资产前必须重建或清理旧资产。

## ABP_Sekiro V2

新的角色动画图不移植旧动态 Pose Host，而是由 Lua 声明 UE 原生 Graph。完整设计见 [ABP_Sekiro Lua AnimBlueprint V2](../design/sekiro-lua-anim-blueprint-v2.md)。

| 编号 | 任务 | 状态 |
|------|------|------|
| 12.1 | 分析旧 ABP_Sekiro 与新编译器能力差异 | 已完成 |
| 12.2 | Root/Grounded/Standing/Crouch/Step/Sprint/Jump 状态层级设计 | 已完成 |
| 12.3 | 运行时生成变量与 `BlueprintUpdateAnimation` Lua Bridge | 已完成，支持 Lua 直接读写生成属性与调用原生函数 |
| 12.4 | 非 Pose 数据 Pin、BlendList 与 Sync Group | 已完成，方向/步态选择与循环同步已接入 |
| 12.5 | Transition Curve/Time Gate AST | 已完成，支持 LuaBool、Curve、TimeRemaining、All/Any/Not |
| 12.6 | Standing Walk/Run 原生状态机 | 已完成，Idle/Start/Cycle/Stop 已生成并通过 PIE 冒烟测试 |
| 12.7 | Crouch、Step、Sprint、Jump | 已完成第一版拓扑与业务规则，已生成并通过编译及 PIE 冒烟测试 |
| 12.8 | 生成 `ABP_Sekiro_LuaV2` 并验证运行时闭环 | 已完成，历史验证资产已在正式迁移后删除 |
| 12.9 | 视觉一致性、步态相位与锁定上半身补偿验收 | 待开始 |

## Lua 源语言模式

| 编号 | 任务 | 状态 |
|------|------|------|
| 13.1 | 标准 `UAnimBlueprint` + Lua Blueprint Extension 源身份 | 已完成 |
| 13.2 | 固定资产原地重建、编译、保存与事务回滚 | 已完成，连续编译与预检失败保护测试通过 |
| 13.3 | `Content/Script/Animation` 文件监听与 Source Dirty 修订跟踪 | 已完成，文件变化只标脏且不轮询、不编译 |
| 13.4 | `PreBeginPIE` 编译 Dirty 资产，PIE/EndPIE 禁止隐式重建 | 已完成，PIE 中只保留 Dirty 到下一次显式入口 |
| 13.5 | 将正式 `ABP_Sekiro` 接管为 Lua 源资产并完成连续编译 | 已完成，角色原引用直接使用新的 Lua 生成类 |
| 13.6 | Check Lua、Generate From Lua、Source Mode 与 Compile/F7 命令 | 已完成，Factory 7/7 自动化测试通过 |
| 13.7 | 生成图只读视图及 Lua 文件/行号调试映射 | 待开始 |
| 13.8 | Lua AnimBlueprint 资产详情面板 | 待开始 |

唯一正式资产路径为 `/Game/Characters/Sekiro/ABP_Sekiro`，Lua 模块为 `Animation.Sekiro.ABP_Sekiro`；此前位于 `Generated` 目录的三个验证资产均已删除。动画蓝图工具栏提供 `Check Lua`、`Generate From Lua` 与持久化 `Source Mode`：

- `Check Lua` 只加载与校验 Lua、构建并缓存当前修订 IR，绝不修改 Graph、调用 UE 原生编译或保存资产。
- `Generate From Lua` 使用当前成功 IR 事务性重建 Graph；缓存缺失或过期时先 Check，缺失的 AnimGraph、EventGraph 和 Root 可自动恢复，失败回滚。
- `Source Mode = Native Blueprint` 时 Compile/F7 完全委托 UE 原动作；`Source Mode = Lua` 时执行 Check、Generate，再恰好调用一次 UE 原生 Compile。
- 旧的 `HandlePreloadObjectsForCompilation` 隐式重建路径已删除，普通原生编译不再暗中修改 Graph。

Animation Lua 文件新增、修改或删除后只更新已加载 Lua AnimBlueprint 的 `SourceRevision` 和 Dirty 状态。启动 PIE 时仅同步编译 Source Mode 为 Lua 的已加载 Dirty 资产；PIE 中不执行结构重建。UI、输入和摄像机等 Animation 目录外 Gameplay Lua 不参与该流程。
