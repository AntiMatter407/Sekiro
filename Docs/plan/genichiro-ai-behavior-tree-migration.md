# 弦一郎 UE Lua AI 复现

| 状态 | 创建 | 更新 |
|------|------|------|
| 🟡 进行中 | 2026-08-21 | 2026-08-24 |

技术方案：[弦一郎 UE Lua AI 技术方案](../design/genichiro-ai-behavior-tree-migration.md)

## 需求描述

用户要求使用项目现有的 **Lua 动画蓝图 + Lua 行为树** 工作流，在 UE5.2 的 Gameplay Framework、
BehaviorTree、Blackboard、CharacterMovement、AnimGraph、StateMachine、Slot/Montage 和动画事件体系中，
尽可能复现弦一郎 AI。原版 `710000` 脚本只作为行为规格、权重参考和动作溯源依据，不作为运行时架构模板；
`710300`、`711000`、`711300` 在基础闭环通过后作为增量 Profile 处理。

AI 对需求的理解是：交付物必须同时具备原版的战斗性格和 UE 的可维护结构。高层意图、优先级、中止和导航
应在 UE 行为树中可观察；移动姿态、战斗 Slot、Root Motion 策略与动画状态由弦一郎专用 Lua 动画蓝图声明并
生成原生 AnimGraph；Lua 只承担数据化规则、语义动作和节点编排，不逐函数仿造原版 AI 虚拟机。

## 1. 背景

项目已完成弦一郎 `c7100` 模型、独立骨架和动画导入，并已生成基础的
`ABP_Genichiro`、`BP_GenichiroAIController`、`BB_Genichiro` 与 `BT_Genichiro`。当前
`BT_Genichiro.lua` 只覆盖目标锁定、追击、普通攻击和战斗反应等待；`ABP_Genichiro.uasset` 虽然存在，
但尚无对应的 `Content/Script/Animation/Genichiro/ABP_Genichiro.lua` 源模块。现有
`ABP_SKAICharacter.lua` 绑定只狼骨架且固定忽略 Root Motion，只能作为实现范式参考，不能直接作为弦一郎成品。

原始参考脚本位于：

- `Extracted/m11_01_00_00.luabnd-lua/710000_logic.dec.lua`；
- `Extracted/m11_01_00_00.luabnd-lua/710000_battle.dec.lua`；
- 后续变体：`710300`、`711000`、`711300`。

基础版 `710000_battle` 约 1305 行，包含 63 个函数、23 个常规动作、29 个拼刀响应、
78 次攻击 Goal 调用，以及大量冷却、内部状态和特殊效果判断。迁移不能简化为固定顺序播放动画。

## 2. 产品目标

在 UE5.2 中生成可观察、可调试、可维护的弦一郎 Lua AI 与动画系统，使其战斗体验具备以下特征：

1. 根据距离、朝向、阶段、资源状态和冷却加权选择行为；
2. 一次决策可以提交完整连段，而不是每段动画结束后随机选招；
3. 拼刀结果优先于普通决策，并改变下一次反击；
4. 玩家使用道具、远程命中、受击、架势崩坏等事件可以中断当前计划；
5. 使用短期记忆抑制重复出招，并形成交替变招；
6. 根据左右、后方可用空间选择横移、后撤或继续压制；
7. 以 UE 行为树表达“反应、阶段、战术、移动、动作”层级，不把整套 AI 藏入单个 Lua Task；
8. Lua 动画蓝图使用弦一郎骨架和动画资产，负责导航移动姿态、战斗全身 Slot、动作状态及 Root Motion 策略；
9. 行为树只表达“想做什么”，战斗组件负责“动作是否合法并如何执行”，动画蓝图负责“最终如何表现”；
10. 弦一郎专用参数、动作和资产路径不硬编码到通用 C++ 插件。

## 3. 范围

### 3.1 第一阶段范围

- 将基础版 `710000_logic` 与 `710000_battle` 转译为 UE 战术意图、反应规则和数据 Profile；
- 复现普通距离决策、权重随机、冷却、动作链、横移和后撤；
- 复现 `Interrupt`、`Parry`、`Damaged`、`ShootReaction`；
- 复现 `Kengeki_Activate` 及其基础拼刀响应；
- 建立原始动作编号到项目战斗动作和动画资产的可追溯映射；
- 由 Lua 行为树定义生成 UE 原生 `BB_Genichiro` 与 `BT_Genichiro`；
- 新建弦一郎专用 Lua 动画蓝图源及动画资产表，原地生成 UE 原生 `ABP_Genichiro`；
- 建立 BehaviorTree → Blackboard → CombatComponent → AnimBlueprint 的显式运行契约；
- 保留现有 `BP_GenichiroAIController`、`ABP_Genichiro`、`BB_Genichiro` 和 `BT_Genichiro` 资产路径。

### 3.2 后续范围

- 分析并迁移 `710300`、`711000`、`711300`；
- 经角色参数和关卡事件确认后，将变体映射为阶段、难度或独立 AI Profile；
- 补齐危字攻击、弓箭、雷电、忍杀阶段演出等特有行为；
- 按实际战斗表现进行权重和时序校准。

### 3.3 非目标

- 不在运行时直接执行反编译脚本；
- 不逐行仿造 FromSoftware 私有 `GOAL_COMMON_*` 实现；
- 不建立一个吞掉感知、导航、决策、中断和动画播放的巨型 Lua 状态机或巨型 Task；
- 不让 AnimBlueprint 决定战术或直接选择招式，也不让 BehaviorTree 逐帧选择动画姿势；
- 不把所有内部计数器和冷却都暴露成 Blackboard Key；
- 不在 C++ 中硬编码弦一郎动作编号、资产路径或权重；
- 第一阶段不承诺像素级或帧级完全一致，帧级一致性依赖 TAE Notify、攻击框和取消窗口完整度；
- 本文档阶段不启动 PIE，也不修改现有 UE 资产。

## 4. 功能需求

### FR-001 目标与战斗入口

- AIController 通过项目现有感知系统写入 `TargetActor`；
- 无目标时保持竞技场位置，不进入普通攻击决策；
- 目标变化或失效时立即终止追击和待执行动作；
- 玩家死亡、复活或目标重新有效时，行为树可正确重规划。

### FR-002 决策快照

每次准备选招时必须采集：

- 目标距离和水平夹角；
- 目标相对方向；
- 自身 HP、架势/原脚本 SP 状态和动作状态；
- 目标战斗状态、道具使用状态和特殊攻击信号；
- 当前阶段、剩余忍杀次数；
- 周围左右及后方可移动空间；
- 动作冷却和短期记忆。

### FR-003 加权动作选择

- 按原版 `<3`、`3～5`、`5～7`、`>=7` 距离区间建立候选集；
- 距离统一通过可配置比例换算到 UE 厘米，初始建议为 `1 原版单位 = 100 cm`；
- 应用阶段、目标状态、此前反应、空间、冷却等修正后进行加权随机；
- 权重小于等于 0 的动作不得进入随机池；
- 候选池为空时必须执行安全兜底动作，不允许行为树死锁；
- 自动化测试可以传入固定随机种子，保证结果可复现。

### FR-004 语义战斗动作与连段

- 一个语义战斗动作可以包含连续攻击、连段窗口、动作记忆和冷却提交；
- 接近、转身、横移和后撤属于 BehaviorTree 战术/导航分支，不混入战斗动作解释器；
- `Attack`、`ComboRepeat`、`ComboFinal` 转译为 CombatComponent 可验证的连段语义；
- 执行器必须等待当前动作完成或收到中断后再结束 Task；
- 动作完成后记录冷却、上次动作和需要延续的短期状态；
- 动画或战斗动作不存在时返回可定位错误，并安全结束当前决策。

### FR-005 空间控制

- 横移前检查左右导航空间；
- 后撤前检查角色后方空间；
- 目标位于背后时优先转身；
- 只有一侧可行时必须选择可行方向；
- 两侧都不可行时移除对应动作权重，而不是原地播放移动动画。

### FR-006 冷却与战斗记忆

至少复现以下语义状态：

- 连续拼刀/防御计数；
- 受击后强制拉扯标记；
- 拼刀反击分支交替状态；
- 阶段开场动作是否已经使用；
- 最近横移方向；
- 上一次动作编号；
- 动作冷却截止时间。

冷却和映射表保存在脚本运行时记忆中；只有需要被行为树观察或调试的摘要进入 Blackboard。

### FR-007 实时中断

以下事件具有高于普通攻击决策的优先级：

- 弹刀/防御时机；
- 拼刀结果变化；
- 自身受击；
- 远程命中；
- 架势崩坏或强制反应；
- 玩家使用道具；
- 阶段或目标状态变化。

中断必须停止普通移动决策，按照战斗系统允许的取消规则处理当前动画，并在反应结束后重新规划。

### FR-008 拼刀响应

- 支持原版 `200200`、`200201`、`200210`、`200211`、`200215`、`200216`
  六类原始信号的语义映射；
- 拼刀响应权重考虑距离、连续拼刀次数、HP/SP、阶段、交替状态和冷却；
- 连续拼刀应提高强反击、变招或后撤反击的概率；
- 未完成语义确认的原始 ID 必须保留追溯信息，不得直接猜测成最终业务名称。

### FR-009 防御、弹刀和受击反应

- 防御/弹刀只在正面角度和有效距离内触发；
- 支持约 0.1 秒的响应抑制间隔；
- 根据玩家攻击类型选择防御、弹刀、专用承受或后撤闪避；
- 连续防御计数能够修正后续反应；
- 受击后可以按原版概率触发后撤或立即反击；
- 反应动画结束前不得提交新的普通动作。

### FR-010 玩家道具惩罚

- 捕获玩家使用恢复类道具的战斗事件；
- 条件允许时立即中断当前普通计划并执行对应惩罚动作；
- 阶段或不可取消动作可以拒绝此次中断；
- 惩罚动作必须经过距离和目标有效性检查。

### FR-011 阶段与变体

- 第一阶段保留 `LegacySelfEffect200050`、`LegacySelfEffect200051` 和
  `DeathblowCount` 的明确输入；
- 未确认语义前不得把两个特殊效果直接命名为特定剧情阶段；
- 变体分析完成后，可以通过共享行为树加不同 Profile，或通过 `BossPhase` 分支实现；
- 阶段切换时清理无效冷却、强制动作和旧目标反应。

### FR-012 调试与追溯

- 能查看当前距离区间、候选动作、过滤原因、最终权重和选中动作；
- 能从 UE 动作配置追溯到原始 `ActXX/KengekiXX`、动画编号和脚本位置；
- 失败日志必须包含角色、动作 ID、步骤和失败原因；
- 调试功能关闭时不得每帧输出日志。

### FR-013 Lua 动画蓝图

- 新增 `Animation.Genichiro.ABP_Genichiro`，目标骨架必须是 `Genichiro_Skeleton`；
- 编译期 Lua 声明 UE 原生 AnimGraph、StateMachine、SequencePlayer、Blend 和 `CombatFullBodySlot`；
- 导航移动通过速度、相对目标方向和移动意图驱动 Idle/前后左右移动/转身，不由行为树直接播放循环动画；
- 攻击、弹刀、受击、架势崩坏和阶段动作通过战斗组件提交动态 Montage，并覆盖基础移动姿势；
- Root Motion 必须按“导航移动”和“已提交战斗动作”区分策略，禁止导航与动画同时驱动位移；
- AnimBlueprint 每帧只读取角色和战斗状态并更新表现变量，不写入 AI 战术决策。

### FR-014 UE 分层行为树

- Blackboard 是 AIController、BehaviorTree、战斗组件和调试工具的共享契约；
- UE Selector/Sequence/Decorator/Service 表达目标有效性、反应优先级、阶段、战术类型和 Abort；
- Lua 选择器只输出 `TacticalIntent` 与 `SelectedActionId`，接近、等待和普通导航优先使用 UE 原生节点；
- Lua 动作 Task 只负责离散战斗动作或不可由原生节点表达的短序列，不接管整个 Boss 生命周期；
- 原版 `ActXX/KengekiXX` 必须被归并为 UE 语义行为族，而不是一对一复制成 52 个树分支。

## 5. 非功能需求

### NFR-001 架构边界

- `Plugins/` 只提供通用 Lua BehaviorTree、Lua AnimBlueprint 宿主和反射编译能力；
- `Source/Sekiro/` 只提供通用战斗事件与角色能力接口；
- 弦一郎专用权重、动作编号、阶段映射和资产路径全部位于 Lua/数据层；
- 不允许插件引用 `Source/Sekiro/` 或弦一郎资产。

### NFR-002 确定性

- 同一 Lua 定义重复生成应得到稳定的 Blackboard、BehaviorTree、AnimGraph 拓扑和节点 ID；
- 固定输入快照和随机种子时，动作选择结果必须一致；
- 生成失败不得破坏已有可用资产。

### NFR-003 性能

- 权重计算只在动作结束或有效中断后执行，不得在 Tick 中重复重建完整候选池；
- Task Tick 只监控目标、动作状态和中断信号；
- AnimBlueprint Tick 只更新轻量表现变量，Pose 求值、状态机和混合由 UE 原生 AnimGraph 执行；
- 禁止每帧加载资产、解析反编译脚本或分配大型临时表。

### NFR-004 可维护性

- 原始数字 ID 必须集中定义并附中文语义说明；
- 动作配置和决策算法分离；
- 新增变体时优先覆盖 Profile 数据，不复制整棵行为树；
- 所有新增项目 Lua 遵守 `Docs/lua-code-style.md`。

### NFR-005 兼容性

- UE 版本保持 5.2；
- 继续使用现有 `ASKAIController`、`USKCombatComponent`、UnLua 和
  `SekiroLuaBehaviorTreeExt`、`SekiroAnimBlueprintExt`；
- 不改变现有玩家角色行为树和动画蓝图资源路径。

## 6. 验收标准

### 6.1 静态与生成验收

- [ ] `BT_Genichiro.lua` Check 通过；
- [ ] `ABP_Genichiro.lua` Check 通过并使用 `Genichiro_Skeleton`；
- [ ] `BB_Genichiro` 与 `BT_Genichiro` 可以从 Lua 确定性生成；
- [ ] `ABP_Genichiro` 可以从 Lua 原地生成，包含基础移动状态机与 `CombatFullBodySlot`；
- [ ] 行为树及相关 Blueprint 编译为 `UpToDate`；
- [ ] 所有动作配置引用的战斗动作和动画资产存在；
- [ ] 基础版 23 个 `ActXX` 和已实现的 `KengekiXX` 都有追溯记录；
- [ ] 候选池为空、目标失效和资产缺失均有安全兜底。

### 6.2 自动化验收

- [ ] 四个距离区间的基础候选权重与原脚本一致；
- [ ] 冷却、阶段、空间和历史状态能正确过滤或修改权重；
- [ ] 固定种子选择可复现；
- [ ] 动作链按声明顺序执行；
- [ ] 受击、拼刀、弹刀、射击和道具事件具有正确优先级；
- [ ] 重复执行和 Abort 后不存在遗留 Task 状态。
- [ ] BehaviorTree、CombatComponent 与 AnimBlueprint 的动作序列号和状态收敛一致。

### 6.3 运行验收

运行验收必须由用户明确授权 PIE 后执行：

- [ ] 远、中、近距离行为分布符合原脚本；
- [ ] 玩家连续压刀时，弦一郎反应会随拼刀计数变化；
- [ ] 玩家喝药能够触发即时惩罚；
- [ ] 场地边缘不会向不可达方向横移或后撤；
- [ ] 动作、受击和拼刀期间没有 T Pose、永久锁死或导航残留；
- [ ] 导航移动与战斗 Root Motion 不会同时驱动角色，Idle/移动/动作/反应切换无明显滑步或跳帧；
- [ ] 阶段切换后动作集和反应正确更新。

## 任务 4.5 审计结论

2026-08-24 通过 AIBridge 完成只读编辑器审计，未启动 PIE、未生成或修改 UE 资产：

| 审计项 | 结论 | 后续约束 |
|--------|------|----------|
| Lua AnimBlueprint 编译器 | ✅ 已支持显式 `TargetSkeleton`、原生 StateMachine、SequencePlayer、Slot、资产预检及只读 Check；`ABP_Sekiro` 的 `Check Lua` 返回零诊断 | 弦一郎源必须使用现有 DSL，不新建弦一郎专用编译器节点 |
| 弦一郎 Skeleton | ✅ 56/56 白名单动画均绑定 `/Game/Characters/Genichiro/Genichiro_Skeleton`，无缺失或骨架不匹配 | 7.1 只可从已核验资产表写入对象路径；基础循环动画仍需按实际用途另行筛选 |
| 战斗 Slot | ⚠️ 编译器支持 `CombatFullBodySlot`，但 `Genichiro_Skeleton` 当前仅有 `DefaultSlot`；现有 `ABP_Genichiro` 的 Slot 节点也仍为 `DefaultSlot` | 7.3 必须先在弦一郎 Skeleton 增加 `CombatFullBodySlot`，再生成同名 Slot 节点并与 CombatComponent 契约对齐 |
| Root Motion | ✅ 56/56 白名单动画均为 `EnableRootMotion=true`、`RootMotionRootLock=AnimFirstFrame`、`ForceRootLock=false`；现有 ABP CDO 为 `RootMotionFromMontagesOnly` | 导航循环由 CharacterMovement/PathFollowing 驱动；离散战斗 Montage 才消费 Root Motion。非零根位移与双重驱动仍在 9.4/11.3 按动作验证 |
| 当前 `ABP_Genichiro` | ⚠️ ParentClass 与 TargetSkeleton 正确且编译状态为 `UpToDate`，但只有占位 Idle 状态、使用战斗动作 `3000` 和 `DefaultSlot`，没有 `SekiroLuaAnimBlueprintExtension` | 7.4 必须原地配置 `Animation.Genichiro.ABP_Genichiro` 并替换占位图，不能把现有图当成最终实现 |

审计证明现有通用插件能力足够，任务 4.5 不需要修改 `Plugins/` 或 `Source/`。Slot 资产配置、基础循环动画
用途确认和弦一郎 Lua 源模块属于任务 7 的实施输入，不构成编译器能力阻塞。

## 任务树

<!--
  状态图标：⬜ 未开始  🔄 进行中  ✅ 已完成  ❌ 已取消
  依赖标注：任务后追加 (依赖: 1, 2.1)
-->

- ✅ 1. 原版脚本现状分析
  - ✅ 1.1 定位并校验 `710000_logic.dec.lua` 与 `710000_battle.dec.lua`
  - ✅ 1.2 统计普通 Act、Kengeki、攻击 Goal、冷却和内部状态规模
  - ✅ 1.3 分析距离权重、动作链、Interrupt、Parry、Damaged 与 Kengeki
  - ✅ 1.4 识别 `710300`、`711000`、`711300` 后续变体
- ✅ 2. 需求与方案设计 (依赖: 1)
  - ✅ 2.1 明确第一阶段范围、非目标和验收标准
  - ✅ 2.2 确定“Lua 行为树 + Lua 动画蓝图 + 通用战斗接口”双源架构
  - ✅ 2.3 明确模块边界、文件范围、接口和数据流
- ✅ 3. 建立 `710000` 离线迁移清单 (依赖: 2)
  - ✅ 3.1 输出全部 Act/Kengeki 的权重、条件、冷却和动作链
  - ✅ 3.2 建立原始动画编号到 Genichiro 动画资产的映射 (依赖: 3.1)
  - ✅ 3.3 建立 SpEffect/Interrupt 到语义事件的映射并标注确认度 (依赖: 3.1)
  - ✅ 3.4 校验 TAE 攻击框、取消窗口和动作完成信号覆盖率 (依赖: 3.2)
  - ✅ 3.5 将动画归类为 Locomotion、CombatAction、Reaction、Phase 四类 UE 用途 (依赖: 3.2)
- ✅ 4. 审计现有运行时能力缺口 (依赖: 3)
  - ✅ 4.1 验证弦一郎语义动作能否由现有 Begin/Play/ActionSerial 通用接口完整提交
  - ✅ 4.2 验证 ActionSerial、动画结束、架势和拼刀状态能否覆盖执行器生命周期
  - ✅ 4.3 列出无法由现有 Lua/委托表达的通用 AI 战斗事件 (依赖: 4.1, 4.2)
  - ✅ 4.4 设计并实现通用 C++ 事件队列 (依赖: 4.3)
    - ✅ 4.4.1 新增通用事件类型、有效负载和有界 FIFO 接口
    - ✅ 4.4.2 接入 WeaponContact 与 Owner DamageReceived 生产者 (依赖: 4.4.1)
    - ✅ 4.4.3 编译验证事件传输层并记录后续生产者边界 (依赖: 4.4.2)
  - ✅ 4.5 验证 Lua AnimBlueprint 编译器、弦一郎骨架、Slot 与 Root Motion 支持 (依赖: 3.5)
- ✅ 5. 实现弦一郎 UE 语义数据与战术决策层 (依赖: 3, 4.3)
  - ✅ 5.1 新增 Legacy Signal 集中映射
  - ✅ 5.2 新增 ActionCatalog，将 Act/Kengeki 归并为 UE 语义行为族 (依赖: 3.2, 3.5)
  - ✅ 5.3 新增 TacticalProfile，用原版权重校准 UE 战术意图 (依赖: 3.1)
  - ✅ 5.4 新增 CombatMemory，管理冷却和原脚本短期状态
  - ✅ 5.5 完成固定随机种子的纯数据决策测试 (依赖: 5.2, 5.3, 5.4)
- ✅ 6. 实现第一里程碑 Lua 行为树节点 (依赖: 5)
  - ✅ 6.1 实现 `SKGenichiroUpdateContext` 与 `SKGenichiroSelectIntent`
  - ✅ 6.2 实现语义战斗动作 Task 的 Execute/Tick/Abort (依赖: 6.1)
  - ✅ 6.3 实现 `SKGenichiroReactionRouter` (依赖: 4.4)
  - ✅ 6.4 使用 UE `MoveTo`/Blackboard MoveGoal 实现接近、横移和后撤 (依赖: 6.1)
  - ✅ 6.5 接入 AttackThreat、受击、弹刀、喝药惩罚和六类拼刀响应 (依赖: 6.3)
  - ✅ 6.6 接入六个代表语义动作，追溯到 Act01、Act03、Act10、Act15、Act23、Act24 (依赖: 6.2, 6.4)
  - ✅ 6.7 实现第一阶段 Type 2 弹射物与 ProjectileImpact 生产者 (依赖: 5.2, 6.2, 6.3)
- ✅ 7. 建立弦一郎 Lua 动画蓝图 (依赖: 3.5, 4.5)
  - ✅ 7.1 新增 `GenichiroAnimAssets.lua` 和动画用途清单
  - ✅ 7.2 新增 Idle/锁定移动/转身基础状态机 (依赖: 7.1)
  - ✅ 7.3 接入 `CombatFullBodySlot`、动作状态和 Root Motion 策略 (依赖: 7.2)
    - ✅ 7.3.1 在 `Genichiro_Skeleton` 增加 `CombatFullBodySlot`
    - ✅ 7.3.2 在 Lua AnimGraph 接入同名 Slot、动作状态与 `RootMotionFromMontagesOnly` 策略 (依赖: 7.3.1)
  - ✅ 7.4 从 Lua 原地生成并编译 `ABP_Genichiro` (依赖: 7.3)
- ✅ 8. 扩展并生成 UE 行为树资产 (依赖: 6, 7)
  - ✅ 8.1 扩展 `BT_Genichiro.lua` 的反应/阶段/战术/移动层级
  - ✅ 8.2 更新 Blackboard 跨系统契约键 (依赖: 8.1)
  - ✅ 8.3 安全生成 `BB_Genichiro` 与 `BT_Genichiro` (依赖: 8.1, 8.2)
  - ✅ 8.4 编译行为树、动画蓝图和 `BP_GenichiroAIController` (依赖: 7.4, 8.3)
- ✅ 9. 第一里程碑无 PIE 验收 (依赖: 8)
  - ✅ 9.1 执行 Lua 规范与函数文档检查
  - ✅ 9.2 执行权重、冷却、空间过滤和固定种子测试
  - ✅ 9.3 执行 Task 生命周期、Abort 和目标失效测试
  - ✅ 9.4 执行动画资产、骨架、Slot、Root Motion 契约检查
  - ✅ 9.5 执行修改范围对应的 C++/AnimBlueprint/BehaviorTree 编译
- ✅ 10. 完整复现 `710000` 行为 (依赖: 9)
  - ✅ 10.1 录入剩余普通 Act 的 UE 语义动作
  - ✅ 10.2 录入剩余 Kengeki 响应
  - ✅ 10.3 对齐全部冷却、Number 状态和特殊信号
  - ✅ 10.4 补齐对应动画状态、动作与反应表现
  - ✅ 10.5 完成全量静态与编译验收
- 🟡 V. 原版/UE 一致性分阶段复核（每个阶段独立汇报；编辑器与 PIE 由用户执行）
  - ✅ V1 固化一致性分类、原版权威源哈希、证据区间和 OFFLINE/EDITOR_STATIC/PIE 门禁
  - ✅ V2 建立不依赖 UE 实现的原版 Golden 决策快照（24 Tactical + 16 Kengeki）
  - ✅ V3 对 TacticalProfile 执行逐案例差分；32 项一致，8 项差异已锁定且未修改运行时
  - ✅ V4 校验 ActionCatalog、冷却与 CombatMemory 提交契约（60 动作、52 冷却、17 状态写入）
  - ✅ V5 审计 Blackboard 每个 Key 的 Producer、刷新与清理路径（17/30 闭环，13 项未闭环）
  - ✅ V6 执行 Task Execute/Tick/Abort 离线生命周期注入测试
  - ✅ V7 执行事件优先级和即时中断注入测试（3/8 真实 Producer；无运行中即时抢占路径）
  - ⏸ V8 UE 编辑器静态资产/行为树检查；到达本阶段后暂停并交由用户操作
  - ⏸ V9 PIE 场景、移动/Root Motion 与概率耐久测试；到达本阶段后暂停并交由用户操作
- ⬜ 11. PIE 行为验收 (依赖: 10；需用户明确授权)
  - ⬜ 11.1 验证四档距离行为分布
  - ⬜ 11.2 验证场地边缘、连续拼刀和喝药惩罚
  - ⬜ 11.3 验证 AnimGraph、Root Motion、攻击框、取消窗口和导航一致性
- ⬜ 12. 迁移变体 Profile (依赖: 10)
  - ⬜ 12.1 分析 `710300/711000/711300` 差异
  - ⬜ 12.2 确认变体与阶段/难度/角色形态对应关系
  - ⬜ 12.3 以 Profile 和动画层差异覆盖并执行对应验收 (依赖: 12.1, 12.2)

## 第一实施里程碑

第一里程碑只交付最小但完整的战斗闭环：

1. 弦一郎 Lua AnimBlueprint 的 Idle、锁定移动、转身、全身战斗 Slot 与 Root Motion 契约；
2. UE 行为树中的反应、阶段、战术、移动和动作层级；
3. `710000` 四档距离权重经 UE 战术意图校准；
4. Act01、Act03、Act10、Act15、Act23、Act24 对应的六个语义动作；
5. 目标追击、转身、横移和后撤由 Blackboard 与 UE 导航节点执行；
6. 普通受击、弹刀、喝药惩罚和一组 `200200/200201` 拼刀响应；
7. 固定种子决策测试、Task 生命周期测试与动画契约检查。

该里程碑通过后再扩充剩余动作，避免在战斗事件和动画链尚未闭环时一次性铺开全部 63 个函数。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-21 | 创建需求、范围、验收标准和初始实施清单 |
| 2026-08-21 | 按 `/plan` 技能模板重构为 DAG 任务树，补齐依赖和叶子任务完成标准 |
| 2026-08-21 | 调整为 UE 架构优先的 Lua 行为树 + Lua 动画蓝图双源方案，原版脚本降为行为参考与校准依据 |
| 2026-08-21 | 开始任务 3.1：整理 `710000` Act/Kengeki 离线迁移清单 |
| 2026-08-21 | 完成任务 3.1：覆盖 23 个 Act、29 个 Kengeki、全部权重矩阵、过滤、冷却、Timer 和 Number 状态 |
| 2026-08-21 | 开始任务 3.2：校验 Legacy 动画编号与 Genichiro UE 动画资产映射 |
| 2026-08-21 | 完成任务 3.2：50 个 Legacy 编号完成映射，48 个资产唯一存在，确认 `3013`、`3092` 缺失 |
| 2026-08-21 | 修复任务 3.2 资产阻塞：解析 TAE 动画别名并生成 `3013`、`3092`，50 个 Legacy 编号现已全部具备 UE 资产 |
| 2026-08-21 | 完成任务 3.3：整理 40 个数值 SpEffect、3 个 Interrupt、道具/突进标签，建立 A/B/C 确认度和 UE 语义事件映射；补导 `3101/3102/3103`；当时暂将 `5211` 列为资产阻塞 |
| 2026-08-21 | 完成任务 3.4：55 个动作请求已解析为 54 个物理动画资产；确认 34 个近战动作与 18 个弹射物动作有原始伤害事件，但 Genichiro 曲线尚未写入、当前取消映射覆盖为 0，动作完成应统一使用 Montage + ActionSerial |
| 2026-08-21 | 修复 `5211`：确认其为 EzState/动作请求而非物理动画 ID，映射为 `DefensiveLongBackstep`，复用 `5201` 表现并由 UE 位移策略承载长后撤；动画资产缺失数归零 |
| 2026-08-21 | 完成任务 3.5：56 个白名单物理资产归类为 3 个离散 Locomotion、48 个 CombatAction、5 个 Reaction、0 个独立 Phase；其余 102 个未被 `710000` 引用的包内动画保持隔离 |
| 2026-08-21 | 完成任务 4.1：确认现有 Begin/Play/ActionSerial 足以复用为动作传输层；识别通用 Reaction 状态、Type 2 弹射物消费两个确定缺口，以及 `5211` 长后撤位移条件缺口 |
| 2026-08-21 | 完成任务 4.2：确认 ActionSerial、Montage 身份和 PostureBroken 能闭合底层生命周期；当前 Task 缺少序列号校验和 Abort 所有权移交，Kengeki 瞬时结果也未形成行为树可消费事件 |
| 2026-08-21 | 完成任务 4.3：确认瞬时武器接触、受伤、威胁、弹射物、目标动作和语义信号无法由现有状态/委托可靠传输；决定执行 4.4 通用有界事件队列，并细分为类型接口、现有生产者接入和编译验证 |
| 2026-08-21 | 完成任务 4.4.1：新增 BlueprintType 通用事件、组件本地 EventSerial/时间戳和 32 条有界 FIFO；EndPlay 自动清理，Sekiro 模块编译通过，未接入生产者 |
| 2026-08-24 | 完成任务 4.4.2：接入攻守双方 WeaponContact 与 Owner DamageReceived；普通 Hit 在守方稳定按接触、伤害顺序入队，Sekiro 模块编译通过 |
| 2026-08-24 | 完成任务 4.4.3：事件类型、队列、WeaponContact/DamageReceived 调用点、BOM 与模块编译复核通过；关闭 4.4，修正 6.3 依赖并新增 6.7 Type 2 弹射物叶子任务 |
| 2026-08-24 | 完成任务 4.5：确认编译器原生支持 StateMachine/SequencePlayer/Slot 与只读 Check；56 个白名单动画全部匹配弦一郎骨架并启用 Root Motion；识别 `CombatFullBodySlot` 缺失和现有 ABP 占位图，拆入 7.3/7.4 修复 |
| 2026-08-24 | 完成任务 5.1：新增 `GenichiroLegacySignals.lua`，集中登记 40 个数值信号和 5 个符号化输入；C 级/纯适配器失败关闭，4 项离线契约测试与 428/428 Lua 函数文档检查通过 |
| 2026-08-24 | 完成任务 5.2：新增 `GenichiroActionCatalog.lua`，固化 56 项动画白名单并登记 6 个代表 Act、4 个首组 Kengeki 响应和 `5211→5201` 别名；6 项契约测试与 437/437 Lua 函数文档检查通过 |
| 2026-08-24 | 完成任务 5.3～5.5：新增 TacticalProfile 与实例级 CombatMemory；用 UnLua 同版本 Lua 5.4.3 实际执行距离权重、空间/阶段/能力过滤、拼刀计数、冷却和固定种子回归测试 |
| 2026-08-24 | 完成任务 6：新增上下文、意图选择、导航目标、战斗动作、反应路由、阶段与意图 Gate Task；补充通用导航投影、ActionSerial 所有权、AI 事件 FIFO 和 Type 2 弹射物生产者 |
| 2026-08-24 | 完成任务 7：生成 7 状态锁定移动图，在弦一郎骨架登记 `CombatFullBodySlot`，AnimGraph 使用同名 Slot 与 `RootMotionFromMontagesOnly`，`ABP_Genichiro` 原地编译为 UpToDate |
| 2026-08-24 | 完成任务 8：行为树生成 30 个 Blackboard Key、44 个主节点、目标 Both Abort Decorator 和 Focus Service；生成 BB/BT、弹射物 Blueprint，AIController 绑定并编译成功 |
| 2026-08-24 | 完成任务 9：531/531 Lua 函数文档、决策与 Task 生命周期测试、56/56 动画资产、骨架 Slot、Root Motion、蓝图和 SekiroEditor 编译全部通过；未启动 PIE |
| 2026-08-25 | 修复动作索引模型：ActionCatalog 不再把行为请求直接当作物理动画号，新增 c9997/TAE/动作源/事件源/UE 资产解析字段；3013、3092、3101～3103 和 5211 回归通过 |
| 2026-08-24 | 完成任务 10：ActionCatalog 达到 23 Act/29 Kengeki，覆盖六类拼刀矩阵、全部已知冷却/Number/Timer/特殊状态映射、分支级副作用和 K38 阶段条件；保留外部 SpEffect/生命属性生产者边界 |
| 2026-08-24 | 完成任务 7 资产收尾：清理旧 `New State Machine` 占位图并重新落盘；`ABP_Genichiro` 仅保留 7 状态 `GenichiroLocomotionGraph`，编译状态为 UpToDate |
| 2026-08-25 | 修复任务 7 基础动画映射：从 c9997 的 IdleBattle/MoveBattle/TurnBattle 状态反查 TAE 与资产，纠正全部 7 项误映射并补充显式来源字段和回归测试 |
