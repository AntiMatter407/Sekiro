# 生存组件 — USKSurvivalComponent

| 进度文档 | 状态 | 基础依赖 |
|----------|------|----------|
| [生存组件](../plan/survival-component.md) | 已实现，验证记录见进度文档 | [GAS 数值基础](gas-numeric-foundation.md) |

## 1. 命名与范围

用户已确认名称为 **USKSurvivalComponent**，中文为“生存组件”，负责生命、死亡、回生和躯干积累/崩溃/恢复。
对应 Lua 为 `SKSurvivalComponent.lua`，相关类型、事件、getter 与任务文件统一使用 Survival 命名。
旧组件方案未曾实现，不创建旧名称兼容类；本次直接实现 Survival 名称及其原生/Lua 接口。

文档使用“躯干值”，对应现有代码中的 `Posture`（旧文档称“架势值”），不为术语变化重命名现有 GAS 属性。
它不是身体部位或物理躯干。`ESKCombatPostureState` 表示持刀/防御等动作姿态，与躯干条和躯干崩溃状态不同，继续归 CombatComponent。

本轮实现 C++ 与 Lua，不修改二进制资产。死亡默认在下一安全 Tick 收尾，回生由外部显式请求；完整命中与 Boss 规则仍属父任务。

## 2. 单一所有权

```text
ASKCharacter（玩家 / AI 共用）
  ├─ USKAbilitySystemComponent
  │   └─ USKCharacterAttributeSet : UAttributeSet
  ├─ USKSurvivalComponent       生命与躯干状态、门禁、事件
  └─ USKCombatComponent       命中裁决、防御/弹刀、动作与动画

战斗 Lua / 道具 Lua → Survival 语义请求 → ASC → GameplayEffect → AttributeSet
                                   ↑                         ↓
                                   └──── 最终快照与提交结果 ────┘
Survival 事件 → Lua 编排动画、输入锁、AI 停止、回生流程
```

| 数据 / 职责 | 唯一所有者 |
|-------------|------------|
| Health、MaxHealth、AttackPower、Armor、Posture、MaxPosture 及全部躯干参数 | USKCharacterAttributeSet |
| 属性基础值、增益聚合、GE 句柄 | GAS |
| LifeState、LifeSerial、死亡/回生过程序号 | SurvivalComponent |
| bPostureBroken、BreakSerial、崩溃进入时刻 | SurvivalComponent |
| 全部躯干可调数值：增长、封顶、强度、恢复、崩溃时长与相关混合时间 | USKCharacterAttributeSet，初始值来自角色专属平铺 Character Lua 表 |
| 防御/弹刀裁决、生命伤害攻击倍率、动作状态、ActionSerial、Montage | CombatComponent 与战斗 Lua |
| 躯干增长计算、封顶、恢复条件/计时、崩溃与恢复状态流程 | SurvivalComponent / SKSurvivalComponent.lua |
| 死亡/回生演出编排、动画资源选择 | Survival Lua；通过 Combat 通用动画接口执行 |
| Boss 阶段、忍杀节点、是否最终击败 | 后续 BossEncounter 与项目 Lua |

Survival 不新增可编辑 Health/MaxHealth/Posture/MaxPosture 属性，不保存可独立写入的数值副本；getter 和快照只读转发 GAS。
允许缓存事件前后快照用于通知与诊断，但不将缓存作为数值提交依据。生命上限、躯干上限初始化来自角色 Lua 表，运行时变化由 GE 管理。

C++ 提供确定的状态转换、游戏线程校验、提交与事件；不硬编码动画路径、角色恢复时长、回生比例、回生次数或 Boss 规则。
组件继承 `UActorComponent` 并实现项目现有 `IUnLuaInterface`，通过 `HandleSurvivalTick(DeltaSeconds)` 向 Lua 提供恢复编排入口。
C++ 不内置“每秒自动回血”或角色数值常量；Lua 读取 GAS 参数编排具体恢复策略。

### 2.1 全部躯干数值进入 AttributeSet

全部生命和战斗数值统一进入继承 `UAttributeSet` 的 `USKCharacterAttributeSet`；角色只挂载这一份属性集，不保留 Health/Combat 两个集或类型别名。
下表为已实现的完整迁移清单，所有可调字段均使用 `FGameplayAttributeData`。本轮新增 23 个参数，初始化结构及 Lua 默认配置共 30 个字段；快照、Profile、校验、通知及 GE 允许属性同步扩展。

| 属性 / 属性组 | 旧来源 / 迁移基线 | 用途 |
|----------------|-----------------|------|
| Posture / MaxPosture | 已在 GAS，默认 0 / 100 | 当前躯干积累与上限 |
| PostureRecoveryRate | 已在 GAS，默认 18 点/秒 | 基础恢复速度 |
| PostureRecoveryDelay / PostureRecoveryRampDuration | Recovery.Delay / RampDuration：0.75 / 4 秒 | 恢复等待时间与渐进时长 |
| PostureRecoveryMinRateScale / PostureRecoveryMaxRateScale | Recovery.MinimumRateScale / MaximumRateScale：1/6 / 1 | 恢复倍率起止值 |
| PostureDeflectSuccessCapRatio / PostureAttackCapRatio | SuccessCapNormalized / AttackCapNormalized：0.98 / 0.98 | 非崩溃增长封顶比例 |
| PostureMinGainScale / PostureGainFalloffExponent | MinGainScale / GainFalloffExponent：0.35 / 1.25 | 随积累增加而衰减的增长倍率 |
| PostureGainDeflectSuccess / PostureGainGuarded / PostureGainDeflectFailed | Gain：6 / 14 / 24 | 防御结果基础增长 |
| PostureGainAttackSuccess / PostureGainAttackGuarded / PostureGainAttackDeflected | Gain：4 / 10 / 18 | 攻击方反馈基础增长 |
| PostureStrengthLight / PostureStrengthHeavy / PostureStrengthThrust / PostureStrengthSpecial | AttackStrength：1 / 1.35 / 1.5 / 1.75 | 来袭类型的躯干强度倍率 |
| PostureBreakMinimumDuration | Break.MinimumLockDuration：1.5 秒 | 崩溃最短持续时间 |
| PostureBreakBlendInTime / PostureBreakBlendOutTime | Break.BlendInTime / BlendOutTime：0.04 / 0.12 秒 | 躯干崩溃演出的混合参数 |
| PostureRecoveryTargetRatio | 显式初始化为 0，保留现有恢复后清零行为 | 完成崩溃恢复时的目标积累比例 |
| RevivePostureRatio | 新增显式初始化字段，首版建议 0 | 回生时目标躯干积累比例 |

以上基线只用于平滑迁移现有项目，不代表原版只狼数值。初始化结构、Lua 字段契约与每种角色配置必须同步扩展，不能只给 AttributeSet 加字段却遗漏初始化。
Lua 配置可以作为一次性初始化输入；完成初始化后，增长、恢复和演出参数只读 GAS 当前值，不继续从 CombatConfig.Posture 或新 Lua 常量表取值。
新参数应接入 ASC 的允许属性、范围校验、只读 getter/快照与通知集合，使 GE 增益能够生效。

数值边界：资源保持 [0, 上限]；上限和增长衰减指数为正有限；增长值、强度、速度、时长及恢复倍率为非负有限；
封顶比例、恢复目标比例和回生躯干比例属于 [0,1)，最低增长倍率属于 [0,1]。
完整初始化要求最小恢复倍率不大于最大倍率；运行中的效果若临时造成倒置，计算时将最小值限制到最大值并记录诊断，不回写参数或重置计时。
RampDuration 为 0 表示等待结束后直接使用最大恢复倍率，禁止除零。

动画资源引用、枚举结果、LifeSerial、BreakSerial、运行时累计时间和“是否允许恢复”的判断不属于可调数值属性；
它们由组件/Lua 持有。数学边界 0/1 与有限值校验也不是角色配置。若未来加入按生命比例缩放躯干恢复，其可调系数同样必须先进入 AttributeSet。

## 3. 两组独立状态

### 3.1 生命状态 ESKLifeState

```text
Uninitialized ── GAS 就绪且生命 > 0 ──→ Alive
Uninitialized ── GAS 就绪且生命 = 0 ──→ Dead（初始快照，不补发死亡）
Alive ── 已提交生命归零 ──→ Dying ── FinishDeath ──→ Dead
Dead ── BeginRevive ──→ Reviving ── CompleteRevive 成功 ──→ Alive（新 LifeSerial）
                          └── CancelRevive 成功 ──→ Dead
```

- Dying 表示本生命轮次已失去战斗能力，等待项目死亡流程收尾；从进入时就关闭普通战斗和资源写入。
- Dead 表示该轮死亡已收尾，不等同于 Actor 被销毁，也不自动表示永久死亡；是否还有回生机会由项目规则决定。
- 普通治疗只允许 Alive，绝不能通过把 Health 从 0 改成正数隐式回生。
- 回生只允许从 Dead 开始；若项目希望倒地后快速回生，应先显式 FinishDeath，再启动回生。
- 生命状态与动画播放分离：没有死亡动画时，Lua 可以在安全调用点直接 FinishDeath；有动画时须配置超时收敛路径。
- 玩家与 AI 共用以上机械状态；Boss 的忍杀/阶段决策在外部完成，不能把 OnDeathStarted 当成最终击败奖励事件。

### 3.2 躯干状态

实现用 `bPostureBroken` 表达 Stable/Broken，不额外增加躯干状态枚举；它与 Combat 的动作姿态枚举无关。

```text
Stable ── Alive 且最终 Posture >= MaxPosture ──→ Broken
Broken ── CompletePostureRecovery 成功 ──→ Stable
Broken ── 死亡 / 离场 ──→ 取消本次崩溃流程
```

- Posture 是积累量：0 表示无积累，增加表示压力上升，减少表示恢复。
- Broken 是独立流程状态，不能简单等同于 `Posture == MaxPosture`。例如清空躯干后仍在硬直动画中，应继续 Broken。
- 只有 Alive 才评估崩溃边沿；非 Alive 时躯干状态不具备行动许可意义，查询行动许可必须同时检查生命状态。
- 同一笔资源结算同时造成生命归零和躯干满值时，死亡优先，不额外启动新的崩溃演出；原有 Broken 以 Death 原因取消。
- 多次满值通知只产生一次崩溃事件。最大躯干降低导致当前值达到上限，也要重新评估；不能只监听伤害入口。
- Combat Lua 只裁决并传递攻防结果和通用攻击类型；对应的躯干增长、非崩溃封顶及是否进入 Broken，由 Survival Lua 读取 GAS 参数统一计算，不在 Combat 重复计算。

### 3.3 序号与幂等

`LifeSerial` 标记一次生命轮次；首次有效初始化取非零值，成功回生后递增。
`TransitionSerial` 标记死亡/回生过程，`BreakSerial` 标记崩溃过程，均单调递增，不因回生归零。
异步动画/定时器回调持有组件弱引用及对应序号；对象无效、序号不符或状态不匹配时返回 StaleTransition，不改变资源或状态。
重复 FinishDeath/CompleteRevive/CompletePostureRecovery 不重复广播、扣费或开启动作；不提供公开 SetLifeState / SetPostureBroken。

## 4. GAS 提交与门禁

### 4.1 不能只包一层 Survival 接口

七个生命/躯干/免疫/回生标签由 Lua 表统一定义，并通过 [SekiroGameplay 编辑器插件](lua-gameplay-tags.md) 生成原生标签 DataTable；Survival 不再做原生注册。
BindAttributeSystem 在绑定策略前查询并缓存全部必需标签；任一缺失会报告名称、拒绝绑定，保持资源/行动门禁关闭。
标签通过 GameplayTagTableList 注册，启动时导入、编辑器生成时刷新；生成器不会给 Actor 添加状态，组件仍仅清理自己贡献的 loose tag。

目前 ASC 已公开 ApplyHealthDamage、RestoreHealth、ApplyPostureDamage、RestorePosture、ResetPosture。
如果只在 Survival 中检查死亡，现有 Lua 仍可直接调用 ASC 绕过限制。因此 ASC 已增加**通用原生资源策略接口 ISKResourcePolicy**：

1. 角色声明需要资源策略，Survival 在属性初始化前注册；缺失、未就绪或被卸载时，普通资源提交失败关闭。
2. ASC 对所有项目资源入口执行策略校验；Survival 根据状态、操作类型及 GAS 标签返回允许/拒绝原因。
3. 无 Survival 的纯数值测试 Actor 可以显式不要求策略，维持 GAS 模块的独立使用能力。
4. 策略只有一个拥有者；ASC 不保存另一份 Alive/Dead/PostureBroken 状态，不直接依赖具体的角色或 Boss 类。
5. 回生/崩溃重置使用绑定拥有者的原生内部入口，校验过程序号，不向 Blueprint/Lua 暴露 `bIgnoreLifeState` 之类的绕过开关。

此约束针对项目支持的 API；不能声称阻止任意 C++ 通过引擎底层接口修改属性。项目调用者禁止直接 SetNumericAttributeBase、
ApplyModToAttribute 或直接提交修改资源的 GE；属性集仍执行范围校验，旁路改变需诊断并重新收敛状态，不视为合法回生。

### 4.2 操作允许表

| 请求 | Alive + Stable | Alive + Broken | Dying / Dead / Reviving |
|------|----------------|----------------|------------------------|
| 生命伤害 | 允许，受无敌门禁约束 | 允许，受无敌门禁约束 | 拒绝 |
| 普通治疗 | 允许 | 允许 | 拒绝 |
| 普通躯干伤害 | 允许，受躯干免疫门禁约束 | 拒绝，避免同一崩溃反复触发 | 拒绝 |
| 普通躯干恢复 / ResetPosture | 允许 | 拒绝，改用专用恢复请求 | 拒绝 |
| 崩溃资源重置 / 完成恢复 | 不适用 | 持当前 BreakSerial 才允许 | 拒绝 |
| 回生资源写入 | 不适用 | 不适用 | 仅 Reviving 且内部令牌有效 |

统计属性 Buff 的添加、到期和移除不因死亡冻结，也不自动恢复资源。回生时以提交时刻的最新上限计算恢复值。
无敌与躯干免疫分别查询 GAS 标签，不在 Survival 中维护可写 bool；标签名和具体效果由项目定义。
已注册状态镜像标签 `State.Life.Dying`、`State.Life.Dead`、`State.Life.Reviving`、`State.Posture.Broken`。
门禁标签为 `State.Damage.Immune`、`State.Posture.Immune`、`State.Revive.Blocked`，效果或项目规则负责提供这些门禁标签。
它们是状态的只读镜像，由 Survival 单一写入；不能通过外部添加/移除镜像标签触发生命转换。离场只撤销自己拥有的标签贡献。

### 4.3 同步通知与重入

当前 ASC 在 OnAttributeChanged、OnNumericExecuted、OnAttributesReady 通知期间仍持有重入门禁。
不能在这些通知中调用 ResetPosture、RestoreHealth 或任何 GE 写入，也不能依赖公共委托的监听者顺序建立生命状态。

原生提交边界：

1. 校验输入及策略后，执行当前 GE 资源修改；属性回调只标记待评估属性，不发布完整生命周期结果。
2. 完成该笔修改后，由 ASC 的内部策略回调先让 Survival 读取最终快照，提交死亡/崩溃状态及门禁。
3. 再发布外部数值与状态事件；此时查询能看到新状态，但同步发起另一笔资源请求或公开状态转换请求仍返回 Reentrant。
4. 事件订阅者可立即关闭攻击窗口、失效 ActionSerial、停止导航；需要写 GAS 的后续步骤排到当前调用栈返回后执行。
5. 对外部 GE 到期导致的上限变化同样执行状态重评估；公开查询不得在存在待评估状态时错误放行行动。

例如 OnPostureBroken 先记录恢复请求；下一次 Survival Lua 更新再以 BreakSerial 请求清空资源。这个延迟不会继续放行战斗，
因为 Broken 已在数值提交返回前生效。队列只存弱引用和序号；取消或离场时丢弃旧请求。

## 5. 生命伤害、治疗与复合资源结算

Survival 暴露生命伤害/治疗与躯干伤害/恢复语义接口，统一完成状态检查，再委托 ASC / GE；不另算护甲，不裁决命中。
普通伤害量必须已经经过攻击倍率与护甲计算。治疗在满血时返回 NoChange，实际变化沿用 FSKNumericResult。

为后续统一命中预留并在实施中补齐 `ApplySurvivalImpact`：输入最终 HealthDamage、PostureDamage 和来源，
两个伤害量有限且非负，至少一个为正；数值执行由 ASC 内部一个受控请求完成，Survival 只在最终快照评估一次状态。
它不是 HitRequest，不包含 Guard/Deflect、方向判定或 Boss 规则；输出包含两个实际变化量及死亡/崩溃转换结果。
不允许把两个公开 ASC 调用串联后声称具备同一笔结算的事件顺序。

组合请求预检查所有输入，使用受控原生 GE，不接受任意执行器或链式效果。GAS 属性回调仍可能看到中间值，
只有最终结果/生命周期事件可用作完整战斗事实。异常执行返回实际快照与 ResourceCommitFailed，不伪造通用事务回滚。
复合请求按通道报告接受/拒绝原因：Alive + Broken 抑制躯干通道，但仍允许生命伤害，不能因此把整次生命伤害丢弃。
生命无敌导致生命伤害为零时，是否仍接受躯干伤害由独立躯干免疫条件决定；原始请求量与实际量都保留。
整体生命状态不允许接收伤害时拒绝全部通道。即使执行异常造成部分资源改变，也必须根据实际最终快照收敛状态，不能因结果失败而漏掉死亡。

## 6. 死亡与回生协议

### 6.1 死亡

```text
资源提交完成，Health = 0
→ Survival 首次进入 Dying，记录死亡来源与序号，取消 Broken
→ 同步更新门禁与状态标签
→ OnDeathStarted
→ Lua / Combat / Controller 清理动作、窗口、输入与导航，播放可选演出
→ FinishDeath(死亡令牌)
→ Dead + OnDeathFinished
```

一轮生命只产生一次 OnDeathStarted。伤害来源来自导致零生命的提交结果；上限变化或无来源写入不得编造击杀者。
死亡不会自动 Destroy Actor、发奖励、消费忍杀节点或扣回生次数；这些操作必须由外部流程根据事件原因执行。

### 6.2 回生：Begin / Complete / Cancel

1. 项目 Lua 检查回生资格、地点及费用；组件只验证自己是否处于 Dead、是否被回生阻止标签限制，以及是否已有进行中的转换。
2. `BeginRevive(ExpectedLifeSerial)` 成功返回回生令牌并进入 Reviving；资源此时保持死亡值，普通战斗继续禁用。
3. Survival Lua 播放演出并完成所需准备，在恢复时刻调用 `CompleteRevive(Token, HealthRatio)`。
4. 验证 `0 < HealthRatio <= 1` 且有限；躯干比例读取 GAS.RevivePostureRatio，验证属于 [0,1)，按当时 GAS 上限计算目标值；不能由 Lua 另传一个躯干常量覆盖该属性。
5. 通过专用受控 GE 写入生命与躯干，验证实际资源健康且躯干未满；成功才递增 LifeSerial、进入 Alive、发布 OnRevived。
6. 提交失败保持 Reviving 与行动门禁，返回实际快照，允许同一有效令牌重试。不能先标记 Alive 再尝试恢复资源。
7. CancelRevive 先通过内部 GE 恢复死亡资源状态（Health=0、Posture=0），成功才回 Dead；失败仍保持 Reviving，不能假装回滚成功。

正常 BeginRevive 的参数校验失败不会改状态。旧回生令牌在成功、取消或离场后失效。
费用由外部以回生令牌为幂等键预留/结算/释放，不能把一个可重复广播的动画回调当成扣费依据。
首版不新增固定“可回生两次”等规则；若实现回生次数/能量，数值仍应进入 GAS AttributeSet，玩法规则留在 Lua/Ability 层。

回生后短暂无敌应由明确持有句柄的 GE 表达，时长来自配置。组件不承诺默认无敌；必须无敌的流程应先成功应用该效果再完成回生。
保留/清除装备、Buff、异常状态由 Lua 按明确的标签/句柄规则编排，不使用 RemoveAllEffects 清空装备。
同 Actor 的世界重置/检查点重生需另设显式 Reset 契约，不复用幂等 InitializeFromValues，也不假装一次普通治疗就是新生命轮次。

## 7. 躯干增长、恢复与崩溃流程

全部躯干处理逻辑统一放在 SurvivalComponent 或 SKSurvivalComponent.lua；Combat 只传递已裁决结果、提供动作/移动查询和通用演出接口。
Survival 不重新判定格挡/弹刀是否成功，也不参与具体招式选择。

### 7.1 增长与非崩溃封顶

Survival Lua 接收 `ApplyPostureImpact(Reason, AttackType, SourceActor)`，读取被修改角色的 GAS 当前参数：
按结果选择 PostureGain*，按通用攻击类型选择 PostureStrength*，按当前 Posture/MaxPosture 计算衰减倍率。

`增加量 = GAS.对应基础增长 × GAS.对应强度倍率 × [GAS.PostureMinGainScale + (1 - GAS.PostureMinGainScale) × (1 - 躯干比例) ^ GAS.PostureGainFalloffExponent]`

弹反成功和攻击方反馈分别按 GAS 的封顶比例限制本次正增长；已经达到或超过封顶时不追加，但不降低当前资源。
完成计算后使用受控数值入口提交并评估状态，拒绝未知结果/攻击类型，不在 Lua 使用 1.0 等角色默认倍率兜底。
`ApplyPostureDamage` 接收已计算的直接数值请求，不再重复乘增长倍率。
已有复合命中链应在 Survival 内先解析躯干结果，再与生命伤害形成一笔数值提交，不能先单独增加躯干后再提交生命。

### 7.2 自然恢复

Survival Lua 只持有累计时间，等待时长、渐进时长、基础速度和倍率全部每次从 GAS 读取：

`alpha = Clamp((累计合格时间 - GAS.PostureRecoveryDelay) / GAS.PostureRecoveryRampDuration, 0, 1)`

`本次恢复量 = GAS.PostureRecoveryRate × Lerp(GAS.PostureRecoveryMinRateScale, GAS.PostureRecoveryMaxRateScale, alpha) × DeltaSeconds`

等待结束前不提交；渐进时长为 0 时使用 alpha=1。上述倍率是读取属性后的计算结果，不是 Lua 中保留的第二份可调配置。
只有 Alive + Stable 才提交普通恢复。角色是否冲刺、闪避或正在攻击由 Combat/Movement 查询提供，不在 Survival C++ 复制动作状态。
迁移时保持原有数值基线；未来若增加生命比例影响恢复，相关可调系数也从 GAS 读取。
被命中、死亡、崩溃、回生、恢复条件失效时，Survival Lua 按策略重置计时；非法 DeltaSeconds 不产生写入。

### 7.3 崩溃过程

- 达到上限时，Survival 进入 Broken，产生新 BreakSerial 和 OnPostureBroken，事件包含触发时的满值快照。
- Survival Lua 接收事件后请求中断当前动作，读取 GAS 中的崩溃混合参数，并调用 Combat 的带生命/崩溃令牌通用演出入口。
- 当前项目“进入崩溃后清空躯干”的策略保留：调用栈返回后执行 `ResetBrokenPosture(Token)`，经 GAS 清零，但保持 Broken。
- Survival Lua 根据 GAS.PostureBreakMinimumDuration 与动画结束状态请求 `CompletePostureRecovery(Token)`；组件同时校验实际经过时间满足当前 GAS 最短时长。
  目标比例读取 GAS.PostureRecoveryTargetRatio，通过 GE 设置目标积累，确认实际值符合目标且低于最新上限后才转 Stable 并发布恢复事件；重复/过期请求无副作用。
- 持续效果修改等待时长、恢复速度、倍率或崩溃最短时长后，下次评估读取最新属性，不维护过时配置副本；已经开始的动画混合不因参数变化倒放或重启。
- 后续 Boss 可以选择不立刻清空，保持 Broken 等待忍杀或超时；Survival 不自动判定忍杀成功或消耗 Boss 节点。
- 死亡取消 Broken 时发布带 Death 原因的取消通知，不能发布正常恢复事件或释放死亡输入锁。

普通动作门禁与受击/死亡/回生演出门禁必须区分。不能简单让 BeginCombatAction 在 Broken 时拒绝所有动作，
否则连崩溃动画也无法启动；特殊演出按当前有效过程序号校验，不开放通用“忽略状态”开关。

## 8. 接口与事件

下表对应已实现 API。所有写入和查询在游戏线程执行。

| 接口 | 语义 |
|------|------|
| IsSurvivalReady / GetSurvivalSnapshot | 读取 GAS 资源快照、生命/躯干状态和序号；未就绪显式返回无效标记 |
| GetHealth / GetMaxHealth / GetHealthRatio | 只读转发生命；未就绪不把零值当作有效状态 |
| GetPosture / GetMaxPosture / GetPostureRatio | 只读转发躯干 |
| IsAlive / IsDead / IsPostureBroken | 精确状态查询；Dying 不等于 Dead |
| CanAct / CanReceiveDamage / CanBeginRevive | 组件层必要条件；不代替 Combat 动作限制或回生费用检查 |
| ApplyHealthDamage / RestoreHealth | 带生命周期门禁的 ASC 语义转发，返回 FSKNumericResult |
| ApplyPostureImpact(Reason, AttackType, SourceActor) | BlueprintNativeEvent，由 Survival Lua 计算增长/封顶；bool 表示攻防语义被接受，封顶/躯干免疫也可接受，不表示数值应用或崩溃 |
| ApplyPostureDamage / RestorePosture | 带躯干状态门禁的最终数值提交，不再次计算攻防增长倍率，返回 FSKNumericResult |
| ApplySurvivalImpact | 同一次最终生命/躯干伤害提交，返回 FSKSurvivalImpactResult |
| FinishDeath(DeathToken) | 幂等完成死亡收尾 |
| BeginRevive(ExpectedLifeSerial) | 返回结果和回生令牌；不消费项目回生费用 |
| CompleteRevive(ReviveToken, HealthRatio) | 躯干目标比例读取 GAS.RevivePostureRatio，经 GE 恢复资源，成功才进入新生命轮次 |
| CancelRevive(ReviveToken) | 明确取消回生，成功才回 Dead |
| ResetBrokenPosture(BreakToken) | 清空崩溃资源，保持 Broken |
| CompletePostureRecovery(BreakToken) | 校验 GAS 最短时长，读取 GAS.PostureRecoveryTargetRatio，资源提交成功后结束崩溃 |

`FSKSurvivalTransitionToken` 包含拥有者身份、LifeSerial、TransitionSerial/BreakSerial 与类型；由组件发出并校验，不能跨角色使用。
令牌统一使用 `TransitionSerial` 字段承载过程序号，Kind 为 PostureBreak 时保存 BreakSerial。`GetDeathToken`、`GetReviveToken`、`GetPostureBreakToken` 提供当前令牌。
`FSKSurvivalSnapshot` 包含 bReady、LifeState、LifeSerial、bPostureBroken；全部 GAS 数值位于嵌套的 `Attributes` 快照中。
查询 `CanAct` 仅表示 Alive + Stable + 就绪；输入锁、移动、当前动作等条件由消费者继续检查。
ASC 数值结果增加明确的策略拒绝原因；转换结果使用 Applied、NoChange、NotReady、InvalidState、InvalidInput、
StaleTransition、Blocked、Reentrant、ResourceCommitFailed，不能将各种失败压成一个模糊 bool。

事件分为：

- 资源：OnHealthChanged、OnPostureChanged，含当前值、上限、比例；同笔请求可合并通知，不承诺模拟每个 GAS 中间值。
- 生命周期：OnSurvivalReady、OnLifeStateChanged、OnDeathStarted、OnDeathFinished、OnReviveStarted、OnRevived、OnReviveCancelled。
- 躯干流程：OnPostureBroken、OnPostureRecovered、OnPostureBreakCancelled。

流程事件包含当前令牌、原因、可空来源及快照；消费者只订阅自己需要的语义事件，不能同时用属性边沿和流程事件重复触发同一动画。
流程事件统一传递单个 `FSKSurvivalTransitionEvent`；OnSurvivalReady 无参数，OnLifeStateChanged 只传生命状态。

Combat 的 `BeginSurvivalPresentation(Token, NewState)` 返回 ActionSerial：有效崩溃令牌允许 PostureBroken 演出，死亡/回生令牌允许 AIReaction 演出。
普通动作、动画播放、输入提交和攻击窗口检查生存门禁；普通动作开始或失效会清除演出令牌，不能沿用旧令牌绕过门禁。
Combat Lua 的 `InterruptForExternalTransition` / `ResetAfterExternalTransition` 只负责通用动作清理，不写生存状态或解锁其他系统。

## 9. 初始化、解绑与现有代码迁移

1. ASKCharacter 创建 Survival 默认子对象并提供 GetSurvivalComponent，玩家与 AI 继承共用。
2. 先注册 ASC 资源策略，再由 Survival Lua 订阅事件、读取该角色的属性文件并调用 InitializeFromValues；不再自动从 DataAsset 初始化。
   主角/通用 AI 分别使用 Character/Attributes.lua 和 AI/Attributes.lua，每份返回一张平铺 Character 表，全部属性同级；角色 AttributeConfigModule 可指定其他敌人文件，详见 [GAS 初始化配置](gas-numeric-foundation.md#3-初始化配置)。
   初始化不依赖 Combat Lua 或 Possess 顺序；配置加载失败保持门禁，不回退到其他角色参数。
3. 初始化绑定必须同时覆盖“先订阅后就绪”和“属性已就绪再绑定”，避免漏掉 OnAttributesReady；一次绑定只广播一次 SurvivalReady。
4. 完整读取初始化快照后分类生命状态，不补发历史死亡；初始 Alive 且躯干满值则建立 Broken，并在订阅准备完成后发布初始崩溃原因。
5. 将 CombatComponent 的 bPostureBroken、SetPostureBroken 与状态事件权威迁出；读取兼容接口可以只读转发 Survival，不能保留可写副本。
6. 将 SKCombatComponent.lua 的 GetPostureGainScale、ApplyPostureImpact 数值计算、Enter/ExitPostureBroken 与 UpdatePosture 迁至 SurvivalComponent/对应 Lua。
   Combat 只转发已裁决攻防结果并执行通用动作请求，不保留躯干公式、计时、判满或主动设置状态路径。
   将 CombatConfig.Posture 的全部数值迁为 AttributeSet 字段和初始化输入；动画资源引用可迁至 Survival 的演出配置，不能夹带恢复速度等运行时数值。
7. OnDeathStarted / OnPostureBroken / OnRevived 驱动输入、动画、目标清理。使用按原因区分的输入锁，躯干恢复不能误解锁死亡或剧情锁。
8. EndPlay 时先停止接收请求，注销资源策略、解绑委托、撤销自有标签、取消定时器与待处理请求；不把卸载当作死亡，不广播虚假奖励事件。

## 10. 实施文件与验收边界

| 文件 | 变更 |
|------|------|
| Source/Sekiro/Character/SKSurvivalComponent.h/.cpp | 新组件、GAS 观察、状态转换、事件和 UnLua 通用接口 |
| Source/Sekiro/Character/SKSurvivalTypes.h | 枚举、令牌、快照和转换结果 |
| Source/Sekiro/AbilitySystem/** | 全量躯干属性及初始化/快照/校验、通用资源策略、结算结束钩子、复合请求与受控资源恢复 GE |
| Source/Sekiro/Character/SKCharacter.h/.cpp | 挂载并暴露 Survival；调整初始化顺序 |
| Source/Sekiro/Combat/SKCombatComponent.h/.cpp | 移除崩溃权威，增加状态感知与受控演出入口 |
| Content/Script/Gameplay/Sekiro/Character/SKSurvivalComponent.lua | 读取 GAS 参数执行全部躯干增长/封顶/恢复/崩溃逻辑及生命流程编排 |
| Content/Script/Gameplay/Sekiro/AbilitySystem/CharacterAttributes.lua | 按角色选择属性 Lua 文件、严格检查字段并复制为 FSKAttributeInitialization |
| Content/Script/Gameplay/Sekiro/Character/Attributes.lua、AI/Attributes.lua | 主角/通用 AI 各自一份完整生命与战斗属性表，只作初始化输入 |
| Content/Script/Gameplay/Sekiro/Combat/** | 移出全部躯干数值与处理逻辑，仅保留攻防裁决、结果转发及通用动作接口 |
| Source/Sekiro/Tests/SKSurvivalTests.cpp | 状态、门禁、幂等及初始化顺序契约测试 |

不为此功能修改插件；不在本组件中实现完整 Weapon/Projectile 命中协议、BossEncounter、回生 UI、联网预测或存档迁移。

实施验收应覆盖：

- 全部原 CombatConfig.Posture 数值均有 GAS 属性与显式初始化，运行时不再从 Combat/Lua 常量取值。
- GE 修改基础增长、封顶、恢复速度/延迟/倍率与最短崩溃时长后，Survival 使用最新属性。
- 零渐进时长、倒置恢复倍率、非法比例和未知攻防结果有确定行为。
- 初始化顺序反转、缺失策略、初始零生命、初始满躯干、重复 Possess。
- 伤害超量、治疗溢出、死亡后的直接 ASC 治疗/伤害/恢复均被门禁拒绝。
- 同笔致死且躯干满只发死亡；重复满值、上限降低、Buff 到期不会重复崩溃。
- 清空躯干后仍 Broken；恢复失败不放开行动；死亡取消崩溃不触发正常恢复。
- 回生成功新生命轮次，取消/失败不放行；重复回生令牌和旧动画回调不能再次恢复或扣费。
- 回生中 Buff 到期使用最新上限；不清除无关装备效果。
- 回调内数值请求返回 Reentrant；移出调用栈的请求重新检查生命/崩溃序号。
- 死亡与崩溃阻止普通动作，但有效令牌仍能启动对应演出；离场后无悬空委托或延迟回调。

本轮执行对应 UBT 编译与 Lua 静态检查；契约测试只编译，不自动运行。实际验证结果见进度文档。
PIE、输入模拟和运行场景验证仅在用户明确要求后执行。

当前 Lua 没有死亡/回生动画、回生倒计时、费用扣除或 UI：死亡下一安全 Tick 调用 FinishDeath，BeginRevive 后等待外部 CompleteRevive/CancelRevive。
玩家使用按原因输入锁；AI 只恢复本模块实际停止且仍属于当前控制器的同一个 BrainComponent。崩溃等待 GAS 最短时长与有限次数动画完成后请求恢复。

## 11. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 用户要求生命组件覆盖生命、死亡、回生及躯干；推荐 USKVitalsComponent，完成状态、GAS 门禁、回生与迁移设计，尚未实现 |
| 2026-08-26 | 用户确认 Survival 命名；文档、拟议类型/接口统一改为 USKSurvivalComponent，补齐全部躯干数值进入 AttributeSet、全部处理逻辑进入 Survival/对应 Lua 的迁移契约，尚未实施 |
| 2026-08-26 | 实现 Survival、ASC 资源策略及复合/内部恢复 GE；迁移全部躯干参数和 Lua 工作流，接入角色与 Combat 门禁；测试仅编译，未执行运行验收 |
| 2026-08-26 | 初始化改为主角/AI 各自一份 Lua 表，生命与战斗值集中在同文件；Survival 加载选定模块，不再优先读取角色 DataAsset |
| 2026-08-26 | 合并生命与战斗集为 USKCharacterAttributeSet，Lua 初值同步平铺；Survival 状态、门禁、复合资源请求与原有数值保持不变 |
