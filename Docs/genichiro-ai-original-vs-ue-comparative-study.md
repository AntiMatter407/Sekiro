# 从目标函数脚本到可观测行为树：弦一郎原版 AI 与 Unreal Engine 5 实现的情境化比较研究

> 文档版本：1.1  
> 研究对象：弦一郎基础战斗 AI `710000`  
> 输出形式：论文式技术研究报告

## 摘要

本文研究《只狼》弦一郎 `710000` AI 在原版目标函数脚本与本项目 Unreal Engine 5.2 实现之间的行为对应关系。研究对象不是单纯的语法迁移，而是两种运行范式对同一战斗人格的表达：原版通过权重表、子目标栈、特殊效果、Timer 与 Number 状态形成紧凑的事件驱动决策；UE 版本则以 AIController 感知、Blackboard、原生 BehaviorTree、Lua 战术 Profile、CombatComponent、Montage 与 AnimBlueprint 构成可视化、可中止且可审计的分层系统。

本文先独立说明原版 AI 的生命周期、战术选择、动作栈、中断和状态记忆，再说明 UE 版本的感知、Blackboard、BehaviorTree、Lua Profile、CombatComponent 与 AnimBlueprint 分层，最后采用情境比较法讨论目标生命周期、距离决策、目标特殊状态、场地约束、冷却与记忆、移动、普通攻击、拼刀、即时反应、阶段切换、投射物、动画执行和故障收敛。基于项目中的 40 个独立 Golden 场景，当前 Lua 战术 Profile 有 32 个场景与原版权重结果一致，8 个场景保留为可观测差异。当前 Blackboard 的 30 个输入中，17 个具备已确认 Producer，1 个仅有清理端，1 个仅有安全回退，11 个尚无运行时 Producer；8 类战斗事件中仅 `WeaponContact`、`DamageReceived`、`ProjectileImpact` 具备真实 C++ Producer。因此，当前系统已经建立了完整的 UE 设计骨架和主要战术数据，但尚不能把“数据层可表达”视为“运行时行为完全复现”。

**关键词：** 弦一郎；行为树；Lua；Unreal Engine 5；Blackboard；战斗 AI；逆向分析；语义迁移

---

## 1. 研究问题与范围

### 1.1 研究问题

本文回答三个问题：

1. 面对相同战斗情境，原版 AI 根据什么事实做出什么决策？
2. UE 版本把这些事实放在哪一层，并由哪些节点、Lua Task 和 Gameplay 组件完成执行？
3. 两者在玩家可观察结果、状态提交和中断时机上有哪些等价、差异与缺口？

### 1.2 研究边界

本文比较的是基础弦一郎 `710000` 的战斗逻辑和外层 AI 生命周期，权威原始材料为：

- `Extracted/m11_01_00_00.luabnd-lua/710000_battle.dec.lua`：战术权重、Act、Interrupt、Parry、Damaged、ShootReaction 与 Kengeki；
- `Extracted/m11_01_00_00.luabnd-lua/710000_logic.dec.lua`：警戒/战斗状态、目标清除、目标死亡与重规划。

UE 对照实现主要为：

- `Content/Script/AI/Genichiro/BT_Genichiro.lua`；
- `Content/Script/AI/Genichiro/GenichiroTacticalProfile.lua`；
- `Content/Script/AI/Genichiro/GenichiroActionCatalog.lua`；
- `Content/Script/AI/Genichiro/GenichiroCombatMemory.lua`；
- `Content/Script/AI/Tasks/SKGenichiro*.lua`；
- `Source/Sekiro/AI/SKAIController.*` 与 `Source/Sekiro/Combat/SKCombatComponent.*`。

本文不把尚未执行的编辑器检查或 PIE 测试写成已验证结论，也不把原版内部引擎函数的未知实现强行解释为确定机制。

### 1.3 一致性分类

| 分类 | 判定标准 | 本文表述 |
|---|---|---|
| 精确逻辑 | 相同输入和随机样本下，候选、权重、优先级及关键状态提交一致 | “精确对应” |
| 语义等价 | 实现机制可以不同，但玩家可观察结果和关键状态等价 | “架构等价” |
| 批准差异 | 为适配 UE 架构而主动改变实现，并有独立验收标准 | “有意差异” |
| 近似 | 原资源或运行语义无法直接重建，只能保留行为意图 | “明确近似” |
| 已知缺口 | 数据或接口已设计，但当前 Producer、中断链或结果仍不完整 | “尚未闭环” |

---

## 2. 研究方法与证据

### 2.1 证据三角验证

本文采用三类证据相互校验：

1. **原版脚本证据**：直接读取 `710000_battle.dec.lua` 和 `710000_logic.dec.lua` 的条件顺序、权重写入和状态副作用。
2. **UE 实现证据**：读取行为树 Lua 定义、战术 Profile、动作目录、反应路由及运行组件。
3. **可执行测试证据**：使用 24 个 Tactical 和 16 个 Kengeki Golden 场景，对原版独立 Oracle 与当前 Lua Profile 做差分。

原版权威文件已在一致性清单中固定 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `710000_battle.dec.lua` | `785e97e4e35663ea70bec35dae766eb3f65d1ebdeae90ecf0cbf7622fa7d63c5` |
| `710000_logic.dec.lua` | `97be03b00abd6eae790a0fc24c99e514d7e53072a82d8badec2e97279ecd49aa` |

### 2.2 情境比较单位

每个情境按以下统一结构分析：

```text
外部事实
  → 原版：SpecialEffect / Timer / Number / 距离 / 子目标栈
  → UE：Perception / Blackboard / CombatEvent / CombatMemory
  → 候选与优先级
  → 移动或战斗动作
  → 状态提交、结束、中断或失败关闭
```

比较重点不是函数名是否一一相同，而是：

- 输入事实是否真实存在；
- 条件先后是否一致；
- 随机池和权重是否一致；
- 动作开始后谁拥有位移和动画；
- 中断发生在同一语义时刻还是延后到下一次决策；
- 动作结束或失败后状态能否收敛。

---

## 3. 原版弦一郎 AI 的运行逻辑

### 3.1 外层生命周期与目标管理

原版外层逻辑由 `710000_logic.dec.lua` 承担。`Logic.Main` 首先登记自身与目标的 SpecialEffect 观察项，再调用 `COMMON_HiPrioritySetup` 处理 Boss 通用高优先级状态。发现状态、战斗状态、警戒状态和无目标状态由原版通用 AI 框架区分，最后统一进入 `COMMON_EzSetup`。

`Logic.Interrupt` 负责少量会改变外层目标生命周期的事件：SpecialEffect `220010` 清除敌对目标，`107710` 要求重新规划，`110060` 把目标死亡标志置位并短暂接近目标，`110015` 则清除死亡标志并进入等待。由此可见，原版战斗脚本并不独立拥有感知系统，而是建立在通用目标槽、警戒状态和 Boss Setup 之上。

### 3.2 战术决策主循环

原版 `Goal.Activate` 每次进入时采集距离、生命、架势、忍杀次数和 SpecialEffect，先调用 `Kengeki_Activate`。若拼刀逻辑没有接管，则按严格的 `if/elseif` 顺序构造 Act 权重表，随后应用特殊状态、Timer、空间检查与冷却过滤，最后交给通用权重选择器提交某个 Act。每个 Act 再向子目标栈压入攻击、接近、侧移、后撤或等待 Goal。

一次普通决策可以概括为：

```text
采集距离、生命、架势、忍杀次数与 SpecialEffect
    → 优先尝试 Kengeki_Activate
    → 按目标状态和距离建立 Act 权重表
    → 应用目标状态追加或槽位改写
    → 应用 Timer、SpecialEffect、空间与 CoolTime 过滤
    → 按权重随机选择 Act
    → Act 向子目标栈压入移动、攻击、后撤或等待 Goal
```

这套决策的关键不是某个单独的 `ActXX`，而是条件顺序。目标特殊动作、Common 高优先分支、阶段开场、目标状态、背后判定和距离区间使用 `elseif` 形成互斥优先级；`109031/110125`、`109900` 与 Number2 等后处理则继续追加或改写已有权重槽。

### 3.3 动作执行与子目标栈

原版 Act 通过 `AddSubGoal` 提交动作。单个 Act 可以只播放一次攻击，也可以把接近、攻击、连击、侧移或后撤按顺序压入同一子目标栈。例如 Act31 先接近，再执行 3003→3045；Act34 先接近，再执行 3007→3011；Act23 依据左右空间和相对方向选择一段随机时长的侧移。

移动和攻击在原版中共享 Goal 栈，但由不同 `GOAL_COMMON_*` 类型承担：`ApproachTarget` 追踪目标距离，`SidewayMove` 围绕目标侧移，`LeaveTarget` 主动拉开距离，`SpinStep` 使用指定行为请求执行位移动作，`ComboAttack/Repeat/Final` 则表达连续攻击。Timer 和 Number 副作用常直接附着在 Goal 成功时点。

### 3.4 即时中断与反应

`Goal.Interrupt` 是原版战斗响应能力的核心。自身具有 `200004` 时，脚本依次检查 ParryTiming、ShootImpact、SpecialEffect 激活与 UseItem。成功反应通常先 `ClearSubGoal()`，再压入新的防御、后撤、反击或惩罚动作，因此可以在当前 Act 尚未自然结束时即时替换行为。

`Parry` 会综合目标方位、距离、Parry Timer、目标攻击 SpecialEffect、连续防御次数与自身状态，在 3100～3103、5201、5211 或“不接管”之间选择。`Damaged` 使用一次 1～100 随机数：约 15% 后闪，随后约 15% 且满足 `200050` 时反击 3009，其余返回 false。`ShootReaction` 则固定清空子目标并提交 3100。

### 3.5 Kengeki 拼刀决策

`Kengeki_Activate` 在普通战术权重之前运行。它根据 `ReturnKengekiSpecialEffect` 区分 `200200/200201/200210/200211/200215/200216`，再结合距离、Number0 连续次数、Number3/6 交替槽、阶段状态、场地空间、生命和 Timer 生成另一套权重池。

Number0 不只是“连击次数”：不同信号对它的提交时机不同。`200200` 无条件递增；`200215/200216` 只在近距递增；远距时脚本可能选择 NoAction 槽50。这个细节决定了“本轮没有动画”是否仍然消费信号并改变下次拼刀结果。

### 3.6 原版状态模型

原版没有一个集中声明的 Blackboard，而是把状态分散在多种机制中：

| 原版机制 | 典型数据 | 作用 |
|---|---|---|
| 目标槽与 AI 状态 | `TARGET_ENE_0`、Find/Battle/Caution | 感知与外层生命周期 |
| SpecialEffect | 200004、200050、109900、110621 等 | 门禁、事件、阶段与目标状态 |
| Timer | Timer0/1/3/5/6/7、Parry Interval | 冷却、抑制与时间窗口 |
| Number | Number0/2/3/6/7/10 | 连续次数、一次性标志、交替与方向记忆 |
| 子目标栈 | `AddSubGoal` / `ClearSubGoal` | 当前动作序列与即时替换 |
| 通用 CoolTime | `SetCoolTime` | 跨 Act 动画请求冷却 |

这一范式高度紧凑，但状态含义需要结合读写位置推导；同一个数字槽可能同时影响候选、动作副作用和后续中断。

这一范式的特点是：

- 决策数据与动作提交处于同一脚本；
- SpecialEffect 同时承担事件、状态和能力门禁；
- `Number(n)` 兼具计数器、一次性标志和交替槽职责；
- `Interrupt` 可以直接 `ClearSubGoal()`，即时替换当前动作；
- 移动和攻击常被连续压入同一个子目标栈。

---

## 4. UE5 中弦一郎 AI 的运行逻辑

### 4.1 可观测分层与单一职责

UE 版本把同一过程拆成五层：

```text
AIController / Perception
    ↓ TargetActor
Blackboard + UpdateContext
    ↓ 距离、空间、阶段、事件、能力
BehaviorTree
    ↓ Reaction > Phase > Tactical
Lua Profile / ActionCatalog / CombatMemory
    ↓ TacticalIntent + SelectedActionId + MoveGoal
MoveTo / CombatComponent / Montage / AnimBlueprint
```

各层权责如下：

| 层 | 权威数据 | 主要职责 | 明确不负责 |
|---|---|---|---|
| AIController | `TargetActor` | 感知、控制器生命周期、启动行为树 | 选择招式 |
| Blackboard | 当前决策快照 | 跨层可观察契约 | 长期复杂状态 |
| BehaviorTree | 分支优先级与执行顺序 | 反应、阶段、战术、导航的可视化组织 | 保存原版数字状态 |
| TacticalProfile | 候选与权重 | 复现 Act/Kengeki 选择规则 | 播放动画 |
| CombatMemory | 冷却、计数、随机种子 | 保存跨决策短期状态 | 驱动骨骼 Pose |
| ActionCatalog | 语义动作定义 | 将 Act/Kengeki 映射到移动、动画、冷却和副作用 | 感知目标 |
| CombatComponent | `ActionSerial`、动作状态、Montage | 动作生命周期与事件传输 | 决定战术权重 |
| AnimBlueprint | 最终 Pose | Locomotion、Combat Slot 和姿势合成 | 决定攻击意图 |

### 4.2 AIController、Blackboard 与目标上下文

UE 由 `ASKAIController` 感知并维护 `TargetActor`，由 Blackboard 显式保存当前决策所需的距离、空间、阶段、意图、动作、反应、Serial 和调试状态。`SKGenichiroUpdateContext` 是每次进入战斗决策前的快照边界：它验证目标与 CombatComponent，计算水平距离，探测左右/后方导航空间，并读取动作状态与能力。

与原版 SpecialEffect 隐式查询不同，UE 中每个 Blackboard Key 都应具备 Producer、刷新时机、失效清理和无来源时的失败关闭策略。当前 30 个 Key 中有 17 个已确认 Producer，其余输入是否存在直接决定某些 Profile 分支能否在 PIE 自然到达。

### 4.3 行为树优先级与意图分支

当前行为树使用如下结构：

```text
GenichiroBossBehavior (Selector)
├─ EngageTarget (TargetActor 已设置，Abort Both)
│  ├─ FocusTarget
│  ├─ UpdateContext
│  └─ ReactionPhaseTacticalPriority (Selector)
│     ├─ ImmediateReaction
│     ├─ PhaseTransition
│     └─ TacticalDecision
│        ├─ CombatAction
│        ├─ ApproachThenCombat
│        ├─ MoveThenCombat
│        ├─ CombatThenMove
│        ├─ Reposition
│        └─ DefensiveHold
└─ HoldArenaWithoutTarget
```

这与原版“先 Kengeki/Interrupt，再普通 Act”的行为优先关系一致，但实现边界不同：原版依靠脚本入口和即时 Interrupt；UE 依靠树的左到右优先级、Blackboard 条件和 Task 生命周期。

### 4.4 Lua Profile、动作目录与战斗记忆

`GenichiroTacticalProfile` 保存四档距离权重、特殊状态修饰、空间过滤、冷却过滤和六类 Kengeki 权重矩阵。`GenichiroActionCatalog` 把原版 Act/Kengeki 编号转换为具有业务含义的动作 ID，并声明 ExecutionMode、移动策略、动画步骤、ProjectileCue、冷却和状态副作用。`GenichiroCombatMemory` 用命名字段承接原版 Timer/Number 语义，并保存确定性随机种子。

`SKGenichiroSelectIntent` 不直接播放动画，而是输出 `TacticalIntent` 与 `SelectedActionId`。行为树根据意图进入纯战斗、先移动后战斗、先战斗后移动、纯重定位或 Hold 分支，使高层流程能够在 UE 行为树调试器中观察。

### 4.5 导航、动作执行与动画表现

UE 移动统一采用 `BuildMoveGoal → BTTask_MoveTo → CommitMove`。Lua 负责从动作策略构造候选点，AIController 负责投影到 NavMesh，原生 MoveTo 负责路径跟随，只有成功到达后才提交移动副作用。这样可以避免路径失败仍写入原版 Timer/Number 等价状态。

战斗动作由 `SKGenichiroExecuteCombatAction` 与 CombatComponent 协作。CombatComponent 的 `ActionSerial` 是动作所有权权威；Task 负责顺序推进 ActionCatalog 中的紧密动画步骤，监视 Montage、目标有效性、Serial 和超时。AnimBlueprint 只负责 Locomotion、CombatFullBodySlot 与最终 Pose 合成，不参与招式选择。

### 4.6 事件队列、反应路由与失败收敛

CombatComponent 保存统一战斗事件队列，`SKGenichiroReactionRouter` 是唯一 Lua 消费者。它排空当前批次、更新 CombatMemory，再按 Priority 与 EventSerial 选出唯一反应。当前树结构表现为 Reaction 高于 Phase、Phase 高于 Tactical，但事件发生时能否即时中止正在运行的 Latent Task，还取决于事件 Producer 与 Abort 链是否闭环。

UE 额外提供原版脚本中不显式存在的故障收敛：目标、位置、动画、投射物、导航或 Serial 失败会写入 `DebugFailureReason`，Task 返回 Failed 或安全 Hold。Actor 位置通过 UE 5.2 的 `K2_GetActorLocation` 反射入口读取，避免 UnLua 别名缺失导致行为树任务异常。

---

## 5. 面对不同情景与角色状态的处理对比

### 5.1 情境一：没有目标、发现目标与目标丢失

#### 原版处理

原版外层 `Logic.Main` 由通用 Boss AI 框架管理发现、警戒和战斗状态。`COMMON_HiPrioritySetup` 与 `COMMON_EzSetup` 承担大量引擎级生命周期逻辑。SpecialEffect `220010` 触发 `ClearEnemyTarget()`；`107710` 触发 `Replanning()`；`110060/110015` 处理目标死亡相关的接近或等待状态。

#### UE 处理

UE 由 `ASKAIController` 的感知结果写入 Pawn 类型的 `TargetActor`。行为树 `EngageTarget` 使用 `HasTarget` Decorator，且 `FlowAbortMode=Both`：目标写入时进入战斗分支，目标清空时中止当前 Engage 分支。没有目标时，根 Selector 进入 `HoldArenaWithoutTarget`，等待约 `0.40±0.10` 秒后重评估。

`SKGenichiroUpdateContext` 在目标存在后读取距离与场地空间。Actor 位置统一通过运行时兼容层调用 UE 5.2 反射接口 `K2_GetActorLocation`；位置不可读时以 `ActorLocationUnavailable` 失败关闭，而不是让 Lua Task 抛异常。

#### 比较结论

| 维度 | 原版 | UE | 当前状态 |
|---|---|---|---|
| 目标来源 | 通用 AI 框架与敌对目标槽 | AIController 感知写 Blackboard | 架构等价 |
| 目标丢失 | `ClearEnemyTarget` / 通用重规划 | 清空 `TargetActor`，Decorator Abort Both | 主要链路已具备 |
| 目标死亡特殊过程 | SpecialEffect 110060/110015 | 尚无专用 Producer/状态适配 | 尚未闭环 |
| 无目标行为 | 通用 EzSetup/警戒状态 | BehaviorTree 安全 Wait | 有意差异 |

### 5.2 情境二：普通距离决策

原版把目标距离划分为四档；本项目按 `1 原版距离单位 = 100 cm` 转换：

| 原版条件 | UE 档位 | 基础候选权重摘要 | 战斗意图 |
|---|---|---|---|
| `distance >= 7` | `VeryFar`，`>=700 cm` | Act10=300，Act15=600 | 远距接近、阶段式突进 |
| `5 <= distance < 7` | `Far`，`500～699 cm` | Act10=300，Act34=100，Act23=100；低架势时 Act9=300 | 缩距、远程链、横移 |
| `3 < distance < 5` | `Mid`，`301～499 cm` | Act1=5，Act2=10，Act6=30，Act11=15，Act23=15；低架势时 Act9=300 | 压迫和中距离组合 |
| `distance <= 3` | `Close`，`<=300 cm` | Act3=15，Act11=15，Act23=10，Act31=30；Timer7 就绪时 Act24=30 | 近身连段、反击、后撤 |

UE 的 `GenichiroTacticalProfile.BuildTacticalCandidates` 保留上述边界和权重，随后把 Act 编号映射为 `FarGapCloser`、`TimedStrafe`、`CloseCounterChain` 等语义动作。Golden 测试已覆盖 3、5、7 三个边界，当前结果与原版一致。

差异主要不在“选什么”，而在“怎么执行”：原版 Act 内部直接压入接近或攻击 Goal；UE 输出 `TacticalIntent` 和 `SelectedActionId`，再由行为树选择纯战斗、先移动后战斗或先战斗后移动分支。

### 5.3 情境三：目标正在执行特殊动作

#### 原版处理

当目标具有 `110060` 或 `110010` 时，原版在距离分支之前处理：

- 目标位于前方 90°：Act21=1，Act28=100，以情境重定位为主；
- 目标不在前方：Act21=100，以转身面对目标为主。

这一分支优先于阶段开场、目标背后和普通距离权重。

#### UE 处理

UE 使用 Blackboard 的 `TargetSpecialAction` 与 `TargetInFront` 表达相同事实，Profile 清空普通候选池并写入相同权重结构。动作执行被翻译为 `TurnToTarget` 或 `ContextReposition`，由原生导航和 Focus 承担朝向/重定位。

#### 当前限制

权重逻辑已通过离线 Golden 场景，但 `TargetSpecialAction` 和 `TargetInFront` 尚无真实运行时 Producer。因此当前属于“逻辑精确、输入未接入”：测试可构造该情境，PIE 中却不会自然发生。

### 5.4 情境四：目标处于可惩罚状态

原版存在两类容易混淆的目标状态：

1. `109031/110125`：仅当距离 `<=5` 时，在现有候选池上追加 Act16=100；不清空其他候选。
2. `109900`：改写若干槽位，但没有整体清空权重表，因此远距已有的 Act23、Act34 等未被显式归零的槽应保留。

当前 UE Profile 的差异为：

- `TargetStatePunish` 没有检查 `<=500 cm`，导致距离 510 cm 时仍错误加入 Act16；
- `TargetPunishWindow` 会先 `weights={}`，导致远距 Act23=100、Act34=100 被错误清除。

这两项属于已登记的精确逻辑缺口，且对应 Blackboard 输入也尚无 Producer。修复时应先校正 Profile 的“追加/改写而非清空”语义，再接入目标状态适配器。

### 5.5 情境五：目标在背后或需要重新面对目标

原版在目标位于背后时将 Act21=100、Act22=1，优先转身，极低概率侧向躲避。UE 使用 `TargetBehind` 构建相同候选，再把 Act21 翻译为保持当前位置并交给 Focus/朝向系统，把 Act22 翻译为可导航侧移。

当前 `TargetBehind` 没有运行时 Producer，因此实际 PIE 中主要依赖 `BTService_DefaultFocus` 持续看向 `TargetActor`，这能提供视觉上的“面对目标”，但没有复现原版这一情境下的显式权重决策。二者在外观上可能相似，在动作选择统计上并不等价。

### 5.6 情境六：阶段开场、生命与架势变化

#### 原版处理

原版读取自身生命率、架势值和忍杀次数，并通过 `200050/200051` 等 SpecialEffect 修饰候选：

- `Number(7)==0` 且自身具有 `200050` 时，以 Act15=600 执行一次阶段开场；
- Far/Mid 距离且架势值不高于 360 时加入 Act9=300；
- `200051` 会过滤 Act15、Act34、Act48 等阶段相关动作；
- Kengeki 中生命率不高于 0.75 且 Timer6 可用时可加入 K40。

#### UE 处理

UE 使用 `LegacyPhaseFlagA/B`、`SelfPostureRaw`、`SelfHealthRatio`、`BossPhase` 和 CombatMemory 中的 `PhaseOpeningUsed` 表达这些事实。架势从 CombatComponent 获取，阶段/生命/Legacy 状态通过 Blackboard 进入 Profile。

#### 当前限制

- `LegacyPhaseFlagA/B` 没有运行时 Producer；
- `BossPhase` 没有 Boss 阶段或忍杀次数适配器；
- `SelfHealthRatio` 目前仅在未初始化时写入安全值 `1.0`，不会随生命更新；
- `bPhaseTransitionPending` 只有消费清理端，没有产生端。

因此阶段和低生命权重目前是“数据结构存在，但自然运行输入缺失”。

### 5.7 情境七：左右或后方空间受限

#### 原版处理

原版在随机选择前调用多组 `SpaceCheck`：

- ±45° 空间不足时过滤 Act22；
- ±90° 空间不足时过滤 Act23；
- 后方 2 单位不足时过滤 Act24；
- 后方 1 单位不足时过滤 Act25。

Act23 内部还会再次检查左右空间并选择侧移方向。Act24、Act25 则分别执行后撤动作或离开目标。

#### UE 处理

`SKGenichiroUpdateContext` 从 Pawn 当前位置沿左右和后方构造候选点，并通过 AIController 的导航投影接口写入：

- `bCanMoveLeft`；
- `bCanMoveRight`；
- `bCanMoveBack`。

Profile 在选择前过滤依赖空间的动作；`SKGenichiroBuildMoveGoal` 再次建立候选点并投影到 NavMesh。若两侧都不可用，侧移动作失败关闭；若后方不可用，后撤动作不会进入候选或无法构造 MoveGoal。

#### 比较结论

这属于语义等价而非几何精确：原版 `SpaceCheck` 的角度和长度语义被简化为 UE 导航点投影，当前探测距离、NavMesh 边界、Agent Radius 与原版碰撞查询并不相同，需要 PIE 在墙边、场地边缘和障碍物旁验证。

### 5.8 情境八：冷却、一次性状态与强制横移

原版使用 Timer、Number 和 `SetCoolTime` 共同抑制重复动作。UE 使用 CombatMemory 中的命名冷却、状态写入和确定性随机种子替代数字槽。

| 原版状态 | 原版效果 | UE 表达 | 当前差异 |
|---|---|---|---|
| Timer0 未完成 | Act3=0，Act6 降为 1 | `CloseActionMutualExclusion` | UE 当前把 Act6 完全过滤，Golden 差异 |
| Timer1 未完成 | Act2=0 | `ClashActionSuppression` 等命名冷却 | 语义映射需持续审计 |
| Timer3 未完成 | Act24=0 | `RepositionSuppression` | Golden 一致 |
| Timer6 未完成 | Act9=0 | `LegacyTimer6` | Golden 一致 |
| Number2==1 | Act23=6000，随后清零；其他候选保留 | `ForceStrafeAfterAction` | UE 当前清空其他候选，Golden 差异 |
| Number3/6 | Kengeki 低链/高链交替 | `ClashAlternation` / `HighClashAlternation` | 已进入 CombatMemory |
| Number0 | 连续拼刀计数 | `DeflectChainCount` | 远距提交语义仍有差异 |

命名状态提高了可读性，但迁移时必须保留原版“覆盖某个槽”与“清空整个候选池”的区别。当前 8 个 Golden 差异中有 3 个直接源于这种副作用语义偏差。

### 5.9 情境九：选择普通战术动作

#### 原版处理

原版完成候选权重、空间、Timer 和 CoolTime 过滤后，调用通用权重选择逻辑，随后直接执行对应 `ActXX`。随机性来源是原版 AI 运行时随机函数。

#### UE 处理

`SKGenichiroSelectIntent` 执行以下步骤：

1. 验证 `TargetActor`；
2. 从 Blackboard 与 CombatComponent 捕获决策快照；
3. 调用 `BuildTacticalCandidates`；
4. 使用 CombatMemory 中的显式线性同余随机种子进行加权选择；
5. 写入 `DebugDecisionSerial` 和 `DebugCandidateSummary`；
6. 输出语义 `TacticalIntent` 与 `SelectedActionId`；
7. 空候选时进入 `Hold`，不伪造默认攻击。

显式随机种子是批准差异：它不尝试复刻原版全局 RNG 流，而是保证同一 AI 的离线测试、复现和回放稳定。对玩家而言，应验收长时间动作频率和情境倾向，而不是逐次随机结果。

### 5.10 情境十：接近、侧移、后撤与保持距离

#### 原版处理

原版移动通过 `GOAL_COMMON_ApproachTarget`、`SidewayMove`、`LeaveTarget`、`SpinStep` 与 `Approach_Act_Flex` 表达。移动时长、目标距离、侧移角度和部分方向带随机性；移动 Goal 与攻击 Goal 可以连续压栈。

#### UE 处理

UE 将移动分为三段所有权链：

```text
BuildMoveGoal → 原生 BTTask_MoveTo → CommitMove
```

`BuildMoveGoal` 根据动作的 Movement Policy 构造以下语义：

| UE Policy | 原版意图 | UE 处理 |
|---|---|---|
| `ApproachTarget` | 接近并保留攻击距离 | 计算目标到自身的水平向量，生成目标外侧 MoveGoal |
| `NavigationStrafe` | 左右侧移 | 根据空间与最近方向选择侧边，沿 Actor Right 偏移 |
| `NavigationSafeBackstep` | 安全后撤 | 沿 Actor Forward 负方向建立后方点 |
| `NavigationSafeLongBackstep` | 长后撤 | 使用更长 RequiredSpaceCm |
| `RetreatFromTarget` | 离开目标 | 后方导航目标 |
| `ContextReposition` | 近距侧移、远距缩距 | 依据 DistanceCm 切换策略 |
| `MaintainTargetRange` | 接近/后退后侧移 | 当前以目标距离点近似 |
| `TurnToTarget` | 面向目标 | 保持当前位置，由 Focus/朝向系统完成 |

MoveTo 的当前关键参数为：接受半径 45 cm、允许 Strafe、禁止 Partial Path、观察 Blackboard 值但不跟踪移动 Actor，且 MoveGoal 预先完成导航投影。

#### 行为差异

- 原版移动以时间和目标 Actor 为中心；UE 多数路径以一次计算的静态 MoveGoal 为中心；
- 原版侧移角度约 30°～45° 且时长随机；UE 使用世界空间侧向偏移和导航到点；
- 原版 `SpinStep` 可以携带专用动画与 Root Motion；UE 必须明确导航或 Root Motion 只能有一个位移所有者；
- 原版移动成功的 Timer/Number 副作用隐含在 Goal；UE 仅在 `CommitMove` 后提交，避免路径失败也写入冷却。

因此该部分是批准差异，需用玩家可观察的距离、方向、耗时、墙边稳定性和“双重位移”作为 PIE 验收标准。

### 5.11 情境十一：普通攻击与紧密连段

原版 Act 可以连续压入多个攻击 Goal，例如 Act31 是接近后执行 3003→3045，Act34 是接近后执行 3007→3011，Act48 是接近后执行 3013→3015 并写 Number7。

UE 不把每一个动画步骤拆成行为树节点，而是在 ActionCatalog 中将紧密连段记录为动作步骤，由 `SKGenichiroExecuteCombatAction` 顺序启动：

1. 校验动作和目标；
2. 通过 CombatComponent 启动新的动作 Serial；
3. 播放当前步骤的动态 Montage；
4. Tick 中校验目标、超时、Serial 与 Montage 状态；
5. 当前步骤结束后推进下一步骤；
6. 全部完成后提交冷却和 CombatMemory 状态；
7. Abort 或失败时只清理自己拥有的动作。

这是一种 UE 风格的结构选择：行为树保留高层意图和可中止边界，紧密到不能被战术层拆开的动画链留在单个 Latent Task 内。其关键一致性不是“树上节点数相同”，而是动作顺序、取消窗口、命中事件和结束副作用一致。

### 5.12 情境十二：拼刀与连续防御（Kengeki）

#### 原版处理

`Kengeki_Activate` 在普通 Act 之前执行。它读取 `ReturnKengekiSpecialEffect`，支持 `200200/200201/200210/200211/200215/200216` 六类信号，并依据距离、Number0 连续次数、Number3/6 交替槽、阶段 SpecialEffect、空间、生命和 Timer 生成 Kengeki 权重池。

关键行为包括：

- `200200` 无条件递增 Number0；
- `200200/200201` 距离不小于 2.5 时进入 NoAction 槽 50；
- `200215/200216` 仅在距离小于 2 时递增 Number0，远距进入低权重 NoAction；
- 连击次数达到门槛后进入更强的多候选反应池；
- Number3/6 控制低链与高链动作交替；
- Kengeki 权重选择完成后直接接管本轮普通战术。

#### UE 处理

WeaponContact 或显式 SemanticSignal 进入 CombatComponent 事件队列。`ReactionRouter` 是唯一 Lua 消费者，它先更新 `DeflectChainCount` 等 CombatMemory，再调用 `BuildClashCandidates`，最后以 Priority 60 输出 `ReactionType=Kengeki` 和动作 ID。行为树的 ImmediateReaction 位于普通 Tactical 之前。

#### 当前差异

24 个 Tactical 与 16 个 Kengeki Golden 场景中，下列四个远距 Kengeki 场景存在差异：

| 场景 | 原版 | UE 当前结果 | 原因 |
|---|---|---|---|
| 200200 远距 | NoAction 槽50=100，且提交计数 | 空候选 | UE 未建模 NoAction 语义槽 |
| 200201 远距 | NoAction 槽50=100，不提交计数 | 空候选 | 同上 |
| 200215 远距 | NoAction 槽50=10，不提交计数 | 空候选 | 同上 |
| 200216 远距 | NoAction 槽50=10，不提交计数 | 空候选 | 同上 |

“空候选”与“NoAction”在动画表现上可能都是什么也不做，但状态提交并不完全等价，尤其 `200200` 的计数仍应递增。因此这是精确逻辑缺口，不能仅以视觉相似判定通过。

### 5.13 情境十三：Parry、弹射物命中、受击与使用道具

这是当前差异最集中的部分。

#### 原版 Interrupt 优先级

原版在自身具有 `200004` 且不在梯子动作时，按以下顺序处理：

1. ParryTiming；
2. ShootImpact；
3. ActivateSpecialEffect；
4. UseItem；
5. 都未接管时返回 false，继续当前子目标。

一旦接管，原版通常调用 `ClearSubGoal()`，当前动作会立刻被替换。

#### UE 事件优先级

ReactionRouter 当前使用：

| UE 事件 | Priority | 当前动作 |
|---|---:|---|
| ForceReplan | 90 | 清理调度，进入 Hold |
| ReactionRequested | 80 | 强制反应 |
| AttackThreat | 65 | 3100～3103 语义防御 |
| Kengeki / Deflected WeaponContact | 60 | 拼刀候选 |
| ProjectileImpact | 55 | 有后方空间时长后撤，否则标准防御 |
| DamageReceived | 50 | 标准防御反应 |
| TargetAction/UseItem | 40 | 道具惩罚动作 |

同一批事件会被排空，再按 Priority 和较新的 EventSerial 选出唯一反应。这比 FIFO 首项直接执行更稳定，但属于 UE 的明确设计。

#### 逐情境差异

| 情境 | 原版逻辑 | UE 当前逻辑 | 结论 |
|---|---|---|---|
| Parry | 方向、距离、Timer、目标攻击状态、连续防御次数与 SpEffect 共同选择 3100～3103、5201、5211 或不接管 | AttackThreat 按攻击类型直接映射 3100～3103 | 尚未复现完整判定 |
| ShootImpact | 固定清空子目标并播放 3100 | ProjectileImpact 有后方空间时选择长后撤，否则 3100 | 明确行为差异，当前不精确 |
| Damaged | 约 15% 后闪；随后约 15% 且 200050 有效时反击 3009；其余不接管 | 固定选择 StandardGuardReaction | 随机分布和“不接管”语义缺失 |
| UseItem | 通过门禁后立即 3023；200051 且忍杀次数>=2 时不触发 | TargetAction 映射 TargetUseItemPunish | Producer 和门禁未接入 |
| 3710030 等强制反应 | SpecialEffect 触发特定 Endure/Combo，并写 Timer | ReactionRequested/SemanticSignal 可表达动作 | 事件 Producer 尚不完整 |

更重要的是，当前行为树没有运行中动作的事件驱动 Abort Service。事件通常要等当前 `ExecuteCombatAction` 结束、超时或其他现有中止条件后，树才会再次进入 `RouteReaction`。因此树形顺序虽然是 Reaction 优先，时间语义还不是原版的“即时抢占”。

### 5.14 情境十四：投射物动作与命中反应

原版 Type 2 行为由动画 TAE 时点产生投射物语义。UE 已从相关动画中提取 Type 2 时间点，并由 ActionCatalog 的 ProjectileCue 驱动通用 `ASKAIBattleProjectile`：

1. `ExecuteCombatAction` 到达 Cue 时间；
2. 从 Pawn 位置和前向量计算生成点；
3. 以 `TargetActor` 当前位置作为瞄准点；
4. 生成通用弹射物；
5. 命中时向被命中者 CombatComponent 发布 `ProjectileImpact`；
6. ReactionRouter 在下一次可消费边界路由该事件。

能力不存在时，依赖投射物伤害闭环的动作会失败关闭，避免只播放射箭动画却没有玩法效果。Actor 位置读取失败会写入 `ActorLocationUnavailable`，投射物生成失败则写入 `ProjectileSpawnFailed`。

当前 `ProjectileImpact` 已有真实 Producer，但其反应选择与原版 `ShootReaction` 不同，且即时抢占尚未闭环。

### 5.15 情境十五：阶段切换

原版阶段行为分散在忍杀次数、SpecialEffect、Number7 和不同候选过滤中。UE 将阶段提升为显式 `BossPhase` 与 `bPhaseTransitionPending`，行为树把 `PhaseTransition` 放在普通 Tactical 之前，并在消费后等待 0.05 秒重新评估。

这一结构更适合 UE 编辑器观察，也便于阶段系统、演出和 Gameplay 状态解耦。然而当前 `bPhaseTransitionPending` 只有 `SKGenichiroPhaseTransition` 的清理逻辑，没有 Producer；`BossPhase` 也没有真实阶段适配器。因此树上存在阶段分支不等于运行时会进入该分支。

### 5.16 情境十六：动作被替换、超时或目标消失

原版依靠子目标栈和 `ClearSubGoal()` 管理替换。UE 使用 `ActionSerial` 解决 Latent Task 的所有权问题：

- Task 开始时记录 ExpectedSerial；
- 如果 CombatComponent Serial 已变化，当前 Task 判定为 `CombatActionSuperseded`；
- 目标失效或超过最大时长时，写 `CombatActionFaultTimeoutOrTargetLost`；
- Abort 只清理由自己拥有的动作，避免旧 Task 停止新动作；
- Montage 结束后才推进连段或提交完成副作用。

这一机制是 UE 下的语义增强：它不是原版 Number/GoalStack 的逐字段复刻，但为异步 Montage、行为树 Abort 和跨帧任务提供了必要的一致性边界。

### 5.17 情境十七：动画不存在或行为请求无法一一映射

动作映射必须区分：

- Legacy 行为请求 ID；
- TAE 事件来源动画 ID；
- 动作来源动画；
- UE 实际可播放资产；
- 为 UE 组合出的移动/动画近似。

其中 `5211` 是明确近似：它保留独立的长后撤行为请求语义，但由于没有可直接播放的同号动画资产，当前使用 `5201` 表现并叠加 UE 长后撤移动策略。验收目标不是证明 5211 资产存在，而是检查其后撤距离、耗时、中断窗口和玩家手感是否接近原版。

---

## 6. 当前一致性结果

### 6.1 Golden 场景统计

当前测试集包含：

- Tactical 场景：24；
- Kengeki 场景：16；
- 总计：40；
- 当前权重结果一致：32；
- 已登记差异：8。

### 6.2 八个已登记差异

| 场景 | 原版结果 | UE 当前结果 | 修复方向 |
|---|---|---|---|
| `target_state_punish_too_far` | 不加入 Act16 | 错误加入 Act16=100 | 增加 `DistanceCm<=500` 门禁 |
| `target_109900_preserves_far_slots` | 保留 Act23/34 | Act23/34 被清空 | 改为槽位覆盖，不重建权重表 |
| `forced_strafe_preserves_other_slots` | Act23=6000，其他槽保留 | 只剩 Act23 | 不清空候选，仅覆盖 Act23 |
| `timer0_suppresses_close_action` | Act6 从30降为1 | Act6 被完全过滤 | 区分降权和禁用冷却 |
| `signal_200200_far_commits_counter` | NoAction=100，计数递增 | 空候选 | 建立 NoAction 语义并独立提交计数 |
| `signal_200201_far_does_not_commit_counter` | NoAction=100，不递增 | 空候选 | 保留 NoAction 但不提交计数 |
| `signal_200215_far_noaction` | NoAction=10，不递增 | 空候选 | 同上 |
| `signal_200216_far_noaction` | NoAction=10，不递增 | 空候选 | 同上 |

### 6.3 输入与事件闭环统计

Blackboard 共 30 个 Key：

| Producer 状态 | 数量 | 含义 |
|---|---:|---|
| Confirmed | 17 | 有明确写入者、刷新和清理策略 |
| Clear Only | 1 | `bPhaseTransitionPending` 只有消费清理端 |
| Fallback Only | 1 | `SelfHealthRatio` 只有安全默认值 |
| Missing | 11 | 阶段、Legacy SpEffect、目标状态、方向和 Timer7 等尚无 Producer |

战斗事件共 8 类，其中真实 C++ Producer 为：

- `WeaponContact`；
- `DamageReceived`；
- `ProjectileImpact`。

尚无真实 Producer 的事件为 `AttackThreat`、`TargetAction`、`ReactionRequested`、`ForceReplan` 与 `SemanticSignal`。

这意味着 ReactionRouter 的接口和离线测试覆盖面大于当前 PIE 中自然可触发的行为面。

---

## 7. 讨论

### 7.1 为什么不应逐函数复制原版 Lua

原版脚本依赖只狼引擎的 Goal 栈、SpecialEffect、TAE、内部寻路和通用 AI 库。若把每个 `ActXX` 和 `KengekiXX` 原样翻译成 UE Lua 函数，会形成一个不可观察的第二套运行时：行为树只剩壳，导航、动画和中断仍隐藏在脚本中。这既不能利用 UE 的 BehaviorTree 调试器，也容易造成导航与 Root Motion 双重拥有位移。

当前方案把可视化决策层交给原生 BehaviorTree，把数据化权重交给 Lua Profile，把紧密动作链交给 ActionCatalog/Latent Task，把物理与动画生命周期交给组件。这种分层属于合理的架构迁移。

### 7.2 “树上优先”不等于“运行时即时抢占”

Reaction 分支位于 Tactical 之前，只能证明每次进入 Selector 时反应具有更高选择优先级。若事件发生时当前 Latent Task 没有被中止，ReactionRouter 就不能立刻运行。因此应分别验证：

- 静态拓扑优先级；
- 事件 Producer 是否产生事实；
- Blackboard/Service 是否触发 Abort；
- CombatComponent 是否安全转移动作所有权；
- 新反应从事件发生到动画启动的延迟。

原版 `ClearSubGoal()` 的时间语义只有在这五层同时闭环后才算复现。

### 7.3 “空候选”不总是等价于“不行动”

Kengeki 槽50说明原版的 NoAction 也可能携带状态提交。UE 当前空候选会进入 Hold，但不会自然表达“本轮已消费信号、计数已提交、故意不播放动作”。因此后续应建立显式 `NoAction` 或 `ConsumeAndHold` 语义，而不是仅依赖空数组。

### 7.4 数据可表达不等于运行时可到达

Profile 已经包含多种阶段、目标状态和方向分支，但 11 个 Blackboard Key 缺少 Producer。只有当 Producer、刷新时机、失效清理和失败关闭全部明确后，该分支才是运行时功能。文档和测试应持续区分：

- Profile 能否计算；
- BehaviorTree 能否选择；
- Gameplay 系统能否产生输入；
- PIE 能否自然到达；
- 玩家是否观察到正确结果。

---

## 8. 分阶段验证建议

### 8.1 离线层

1. 保持 40 个 Golden 场景为原版逻辑回归基线；
2. 修复 8 个已登记差异后，将其从允许差异集移除；
3. 为 NoAction 和状态提交增加独立断言；
4. 对 23 个 Act、29 个 Kengeki 的动作步骤、冷却和状态写入做目录契约检查；
5. 继续保证 ReactionRouter 是唯一事件消费者。

### 8.2 编辑器静态层

1. 确认生成的 BehaviorTree 节点顺序为 Reaction → Phase → Tactical；
2. 确认 `TargetActor` 类型为 Pawn，HasTarget 使用 Abort Both；
3. 确认所有 MoveTo 读取 `MoveGoal`，而非直接读取 Actor；
4. 确认 Genichiro Character 使用正确 AIController、BehaviorTree 和 Auto Possess AI；
5. 确认 Montage 使用正确 Skeleton 与 `CombatFullBodySlot`；
6. 逐个核对 5211 的语义请求与 5201 表现近似没有被错误宣称为同号资产。

### 8.3 PIE 情境层

建议按依赖从低到高测试：

1. **目标生命周期**：无目标等待、发现玩家、失去视野、目标清空；
2. **基础距离**：分别在 250、400、600、800 cm 观察候选摘要和动作倾向；
3. **空间约束**：开阔地、左墙、右墙、背墙、场地边缘；
4. **移动所有权**：接近后攻击、后撤后反击，检查是否滑动或双重位移；
5. **普通连段**：检查步骤顺序、Montage 完成和 ActionSerial 收敛；
6. **投射物**：检查 Cue、生成点、瞄准、命中与 ProjectileImpact；
7. **拼刀**：检查低链、高链、交替和远距 NoAction；
8. **即时反应**：在攻击动作中触发受击、弹射物命中、Parry、UseItem，测量抢占延迟；
9. **阶段与低生命**：待 Producer 接入后验证阶段开场、阶段过滤和 K40；
10. **5211 近似**：人工比较后撤距离、时长、取消窗口和镜头观感。

每次 PIE 应同时记录：`TargetActor`、`DistanceCm`、`ReactionPriority`、`TacticalIntent`、`SelectedActionId`、`ActionSerial`、`DebugCandidateSummary` 和 `DebugFailureReason`。

---

## 9. 结论

弦一郎 AI 的核心不是某一组动画 ID，而是一套由距离、目标状态、场地空间、架势、阶段、冷却、连续拼刀和即时事件共同塑造的优先级系统。原版以高度压缩的 Lua Goal 脚本实现这一系统；UE 版本则把它展开为可观察的 BehaviorTree 和 Blackboard，并用 Lua Profile 保留权重，用 ActionCatalog 保留动作语义，用 CombatComponent 与 AnimBlueprint 承担跨帧执行。

当前实现已经完成了主要架构迁移：目标进入行为树、四档距离权重、23 个 Act 与 29 个 Kengeki 的语义目录、原生导航分支、动作 Serial、Type 2 投射物和战斗事件队列均已有明确承载。离线比较显示 40 个 Golden 场景中 32 个与原版一致，说明战术数据主体已建立。

然而，当前系统仍处于“主体结构完成、运行时语义尚未全部闭环”的阶段。八个已登记权重差异、十一项缺失 Blackboard Producer、五类缺失事件 Producer，以及运行中动作缺少即时事件驱动抢占，是决定最终战斗手感是否接近原版的关键问题。后续工作的正确顺序应是：先修复可离线证明的权重和状态提交差异，再接入事实 Producer，随后建立安全的即时 Abort，最后通过编辑器静态检查和逐情境 PIE 测试验证玩家可观察结果。

换言之，本项目的目标不应是让 UE “看起来执行了原版 Lua”，而应是让 UE 用自己的设计语言——感知、Blackboard、BehaviorTree、导航、Gameplay 组件、Montage 和 AnimGraph——重新表达同一个弦一郎，并使每一个差异都可解释、可测试、可追溯。

---

## 参考资料

1. `Extracted/m11_01_00_00.luabnd-lua/710000_battle.dec.lua`，弦一郎原版战斗决策脚本。
2. `Extracted/m11_01_00_00.luabnd-lua/710000_logic.dec.lua`，弦一郎原版外层 AI 逻辑脚本。
3. `Docs/design/genichiro-ai-behavior-tree-migration.md`，弦一郎 Lua 行为树与动画蓝图迁移详细设计。
4. `Content/Script/AI/Genichiro/BT_Genichiro.lua`，UE 原生 BehaviorTree/Blackboard 编译定义。
5. `Content/Script/AI/Genichiro/GenichiroTacticalProfile.lua`，战术与 Kengeki 权重实现。
6. `Content/Script/AI/Genichiro/GenichiroActionCatalog.lua`，动作、动画、移动与副作用目录。
7. `Content/Script/AI/Tasks/SKGenichiroReactionRouter.lua`，战斗事件批处理与反应优先级。
8. `Script/tests/fixtures/genichiro_original_golden_cases.json`，原版独立 Golden 场景。
9. `Script/tests/fixtures/genichiro_ai_conformance_manifest.json`，一致性分类、证据和测试门禁。
10. `Script/tests/fixtures/genichiro_blackboard_producers.json`，Blackboard Producer 审计清单。

## 附录 A：情境—实现—验证速查表

| 情境 | 原版主要入口 | UE 主要入口 | 当前等级 | 首要验证 |
|---|---|---|---|---|
| 无目标/发现目标 | `Logic.Main` / COMMON Setup | AIController + HasTarget | 架构等价 | Target 写入与清理 |
| 目标死亡 | Logic SpecialEffect | 尚无专用适配 | 缺口 | 死亡 Producer |
| 四档距离 | `Goal.Activate` | TacticalProfile | 精确对应 | 300/500/700 边界 |
| 目标特殊动作 | 110060/110010 | TargetSpecialAction | 逻辑有、Producer 缺 | 前后方向输入 |
| 可惩罚状态 | 109031/110125/109900 | TargetStatePunish/PunishWindow | 2 项 Golden 差异 | 追加与覆盖语义 |
| 目标在背后 | `IsInsideTarget(B)` | TargetBehind + Focus | Producer 缺 | 转身与侧移概率 |
| 阶段开场 | 200050 + Number7 | PhaseFlag + Memory | Producer 缺 | 一次性提交 |
| 低生命/架势 | Hp/Sp/Timer6 | Health/Posture + Memory | 部分闭环 | 实时生命来源 |
| 空间限制 | `SpaceCheck` | Nav 投影 | 语义等价 | 墙边与边缘 |
| 强制横移 | Number2 | ForceStrafeAfterAction | Golden 差异 | 保留其他候选 |
| 普通战术 | 权重选择 + Act | SelectIntent | 主体完成 | 长时间频率 |
| 接近/侧移/后撤 | GOAL_COMMON Move | Build/MoveTo/Commit | 批准差异 | 距离与移动所有权 |
| 连段 | 子目标栈 | ActionCatalog + Latent Task | 架构等价 | 顺序和取消窗口 |
| Kengeki | `Kengeki_Activate` | WeaponContact + Router | 主体完成 | 远距 NoAction |
| Parry | `Goal.Parry` | AttackThreat 路由 | 缺口 | 完整门禁与即时抢占 |
| ShootImpact | `ShootReaction` | ProjectileImpact | 行为差异 | 固定3100或后撤 |
| 受击 | `Damaged` | DamageReceived | 行为差异 | 15%/15%/不接管 |
| 使用道具 | `Interupt_Use_Item` | TargetAction | Producer 缺 | 门禁和3023 |
| 投射物发射 | TAE Type 2 | ProjectileCue + Actor | 主要闭环 | Cue与命中事件 |
| 阶段切换 | SpecialEffect/忍杀状态 | Phase 分支 | Producer 缺 | Pending 产生端 |
| 动作替换 | `ClearSubGoal` | ActionSerial/Abort | 部分等价 | 事件驱动 Abort |
| 5211 | SpinStep 5211 | 5201 + 长后撤 | 明确近似 | 手感人工验收 |
| 调用失败 | 原版引擎内部处理 | DebugFailureReason | UE 增强 | 失败关闭与日志 |
