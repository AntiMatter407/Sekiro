# Boss AI 与玩家对战基础闭环

| 状态 | 创建 | 更新 |
|------|------|------|
| 🟡 进行中 | 2026-08-26 | 2026-08-26 |

技术方案：[Boss AI 与玩家对战基础闭环技术方案](../design/boss-player-combat-foundation.md)

关联需求：[弦一郎 UE Lua AI 复现](genichiro-ai-behavior-tree-migration.md)

独立子任务：[GAS 数值基础系统](gas-numeric-foundation.md)（任务 2.1，已实现并通过编译验证）。
独立子任务：[生存组件](survival-component.md)（任务 2.2–2.5，USKSurvivalComponent 已实现；仅编译/静态验证，不代表对战闭环验收）。
独立子需求：[Gameplay C++ 插件与 Lua 标签资产生成](lua-gameplay-tags.md)（任务 2.6，插件窗口及原生 DataTable 生成代码已编译，首份资产生成与迁移等待编辑器重启确认）。
独立 UI 子需求：[原版敌我血量、架势与锁定 UI](original-combat-ui.md)（任务 10.5，原图已导入、基础显示与弦一郎 Boss 排版已实现；共用 Pelvis 锁定定位的 DebugGame 补编译/重载及运行视觉仍待验证）。

## 需求描述

在继续校准弦一郎 Boss AI 与玩家角色的实际对战表现前，先完成一套由玩家与 AI 共同使用的双向战斗基础闭环。
闭环必须覆盖生命与死亡、命中结算、武器窗口、攻击/防御/弹刀、动作生命周期、目标管理、导航、战斗事件、
即时中断、Boss 阶段和诊断测试。弦一郎行为树只消费这些模块产生的稳定事实，不直接补偿底层战斗能力缺失。

本需求遵循项目核心架构原则：C++ 提供通用状态、碰撞、生命周期和事件接口；Lua 负责玩家战斗规则、Boss
策略与工作流编排；BehaviorTree 负责 AI 高层执行顺序；AnimBlueprint/Montage 负责最终动作表现。

## 1. 背景与现状

### 1.1 已有基础

当前项目已经具备：

- `USKCombatComponent` 的战斗动作状态、`ActionSerial` 和攻击/防御输入队列；GAS/Survival 已成为生命、架势、崩溃与回生的唯一权威；
- 动态 Montage 播放、结束回调、动画曲线采样和 `CombatFullBodySlot` 契约；
- `ASKWeapon` 的连续刀刃 Sweep、单次攻击目标去重、Hit/Guarded/Deflected 接触分类；
- `FSKAICombatEvent` 队列，以及 `WeaponContact`、`DamageReceived`、`ProjectileImpact` 三类真实 Producer；
- `ASKAIController` 的视觉感知、`TargetActor`、行为树启动和导航投影；
- 玩家 Attack/Guard 输入桥接，以及移动、闪避、锁定、相机和动画基础系统；
- 弦一郎 Lua BehaviorTree、ActionCatalog、CombatMemory、战术 Profile 和投射物执行器。
- 统一 `CombatHitRequest → CombatHitResult`：近战和箭矢接入同一原生结算，Lua 纯裁决与提交后演出分离；实现/编译情况见任务 3，运行验收尚未进行。

### 1.2 当前阻塞

- 统一命中已接入 Weapon/Projectile，仍需经用户授权验证实际玩家/Boss 双向命中、免疫和死亡边沿；
- 近战基础伤害已读取 GAS AttackPower；完整招式倍率、护甲曲线、方向门禁和精确闪避窗口尚未配表接入；
- 有效 TeamId 的同队过滤已实现，玩家/AI 的完整阵营身份配置仍待补齐；
- 玩家道具、攻击威胁、强制反应、强制重规划和 Legacy 语义信号缺少真实 Producer；
- 行为树虽然静态优先级为 Reaction > Phase > Tactical，但运行中动作没有完整的事件驱动即时 Abort；
- `BossPhase` 没有阶段权威，`bPhaseTransitionPending` 只有消费端；
- `SelfHealthRatio` 只有安全默认值，不会随真实生命刷新；
- 玩家/Boss 死亡、目标清理、动作终止和战斗重置尚未形成统一流程。

## 2. 产品目标

1. 玩家与 Boss 通过同一命中与伤害协议交互；
2. 玩家可以攻击、防御、弹刀、闪避、受击、架势崩溃和死亡；
3. Boss 可以使用相同协议攻击玩家并接收玩家攻击；
4. 生命、架势、死亡、阶段和目标状态都有唯一权威与事件；
5. MoveTo 与 Root Motion 任一时刻只有一个位移所有者；
6. 战斗事件可以在允许的取消窗口内抢占运行中的 AI 动作；
7. Boss 阶段变化可以产生明确事件并驱动 Blackboard；
8. 所有动作开始、完成、中断、替换、超时和失败均可追踪；
9. 在接入完整弦一郎权重前，可用玩家与通用 AI 假人验证双向闭环；
10. 不在 C++ 中硬编码弦一郎动作编号、资产路径、阶段名或战术权重。

## 3. 范围

### 3.1 本需求范围

- GAS 数值基础（独立子任务）与后续通用生命、躯干、死亡和回生组件；
- 通用战斗命中请求、结算结果和伤害应用边界；
- 玩家/Boss 共用武器和投射物命中闭环；
- 玩家攻击、防御、弹刀、闪避、使用道具的语义事件；
- CombatComponent 动作所有权、武器窗口和动画完成收敛；
- AI 目标生命周期、导航与 Root Motion 单一所有权；
- AI 战斗事件 Producer、Pending 状态和即时 Abort 链；
- 通用 Boss 阶段/忍杀状态组件；
- HUD/日志/Blackboard 调试快照；
- 使用原版资产与排版的敌我血量/架势 HUD，以及原版锁定标记替换；
- 离线、编译、编辑器静态和用户 PIE 验收用例。

### 3.2 非目标

- 本需求不调整弦一郎原版权重和出招概率；
- 不补齐 `710300/711000/711300` 变体；
- 不追求原版帧级碰撞与网络同步完全一致；
- 不在 AnimBlueprint 中选择战术动作；
- 不让 BehaviorTree 直接计算生命、伤害或弹刀结果；
- 不为弦一郎建立专用 C++ CombatComponent；
- 默认不修改 `Plugins/`，除非后续证明通用宿主接口确实不足；
- 未获得用户明确指令前不启动 PIE 或模拟玩家输入。

## 4. 功能需求

### FR-001 生命、躯干与死亡权威

- 先接入 GAS：AttributeSet 唯一管理生命、生命上限、护甲、攻击力和架势等数值，GameplayEffect 负责提交变化；
- 后续新增 USKSurvivalComponent 管理生命轮次、死亡、回生及躯干崩溃/恢复，不保存或配置生命/躯干上限；无敌统一由 GAS 效果/标签表示；
- 正有限伤害只能通过统一入口提交；
- 生命归零只触发一次死亡事件；
- 死亡后拒绝普通战斗输入、伤害和 AI 动作；
- 组件提供 Blueprint/Lua 可用的查询、写入和事件接口；
- 已有基础躯干数值已迁入 GAS；后续将剩余增长、恢复、崩溃等全部可调参数迁入 AttributeSet，全部躯干逻辑迁至 SurvivalComponent/对应 Lua。
- `USKCombatComponent` 只保留攻防裁决、结果转发和通用动作接口，不保留躯干参数或计算逻辑。
- 普通治疗不隐式回生；躯干清零不自动解除崩溃；同次资源结算死亡优先于新崩溃。

### FR-002 统一命中请求与结算结果

- 定义 `FSKCombatHitRequest`：来源、目标、ActionSerial、LifeSerial、HitSourceSerial、AttackType、DamageChannel、生命伤害、架势伤害、方向和 EventTag；
- 定义 `FSKCombatHitResult`：Rejected/Committed、Ignored/Hit/Guarded/Deflected/Dodged/Invulnerable 接触结果，独立 PostureBroken/Killed 标志，以及攻守双方实际资源回执；
- 武器和投射物都调用目标 CombatComponent 的统一结算入口；
- 一次接触只产生一次权威结果，不允许先判定 Guard 再独立重复扣血；
- CombatComponent 负责战斗裁决，Survival 校验生命/躯干状态，GAS 提交合法数值变化；Survival 根据最终快照管理死亡或崩溃流程。

### FR-003 武器与攻击窗口

- 保留连续刀刃 Sweep 和单次动作目标去重；
- 攻击窗口由 Montage 曲线/Notify 或 ActionCatalog 时点显式开启和关闭；
- 新动作开始、结束、Abort、死亡和武器切换时必须关闭 Hitbox；
- 近战命中请求锁存攻击者 `ActionSerial`，过期动作不得继续结算；投射物发射后允许正常收招/换招，但生命轮次变化后失效；
- 阵营、自身、无效对象和重复目标必须失败关闭。

### FR-004 玩家战斗控制闭环

- Attack/Guard 输入继续通过 `FSKCombatInputEvent` 进入 Lua；
- 补齐轻攻击连段、防御抬手/保持/放下、弹刀、受击、架势崩溃和死亡动作；
- 闪避有效期可使结算返回 Dodged 或 Invulnerable；
- 玩家使用道具发布 `TargetAction` 中性事件，不直接引用弦一郎；
- 输入锁、动作状态和 Montage 结束必须在失败/Abort 时收敛。

### FR-005 动画与 Root Motion 所有权

- Locomotion 由 CharacterMovement/AnimGraph 管理；
- 已提交战斗 Montage 才能消费战斗 Root Motion；
- AI 战斗动作开始前停止当前 MoveTo；
- 导航动作不得播放带位移的战斗 Montage；
- 动作结束、中断或死亡后恢复正确的移动和输入状态；
- 攻击窗口、取消窗口和投射物 Cue 均由可追溯动画语义驱动。

### FR-006 AI 目标与导航生命周期

- `ASKAIController` 继续作为 `TargetActor` Producer；
- 目标死亡、失效、超出策略范围或 Controller 结束时清空目标；
- Target 变化使用 BehaviorTree Abort Both 终止旧分支；
- BuildMoveGoal、MoveTo、CommitMove 保持三段提交；
- 无路径、不可投影和位置不可读时写入明确失败原因；
- 竞技场边缘和障碍物环境不得选择不可达侧移或后撤。

### FR-007 战斗事件 Producer

- 保留 WeaponContact、DamageReceived、ProjectileImpact；
- 新增 AttackThreat：攻击进入可响应威胁窗口时发布；
- 新增 TargetAction：玩家使用道具或其他可惩罚动作时发布；
- 新增 ReactionRequested：强制受击、架势崩溃等系统请求；
- 新增 ForceReplan：目标、阶段或移动事实失效时发布；
- SemanticSignal 只承载已确认中性语义，不直接暴露原版数字状态给 C++。

### FR-008 AI 即时中断

- CombatComponent 发布事件时提供 Pending 通知，但不写 Blackboard；
- AIController 或项目侧 BehaviorTree Service 将 Pending 状态写入可配置 Blackboard Key；
- Pending Decorator 触发当前 Tactical 分支 Abort；
- ReactionRouter 批量消费事件并按 Priority/EventSerial 选择反应；
- CombatComponent 通过 ActionSerial 安全转移动作所有权；
- 不可取消动作可延迟反应，但必须记录延迟原因和最大等待时间。

### FR-009 Boss 阶段与忍杀状态

- 新增通用 Boss Encounter/Phase 组件；
- 管理当前阶段、剩余忍杀节点、阶段转换中和战斗结束状态；
- 阶段变化只发布中性事件，不选择具体动作；
- 项目 Lua 把阶段事件映射到 `BossPhase` 与 `bPhaseTransitionPending`；
- 阶段切换中锁定普通伤害/输入的策略由 Lua 配置；
- 阶段完成后清理旧动作、导航、事件和无效冷却。

### FR-010 死亡、目标清理与战斗重置

- 玩家死亡时 Boss 停止当前动作、清空目标并进入安全状态；
- Boss 死亡时停止 BehaviorTree、导航、Montage、Hitbox 和投射物生产；
- 重置顺序固定为动作失效 → 碰撞关闭 → 事件清空 → 属性恢复 → 目标恢复；
- 重复 Reset 必须幂等；
- 不允许死亡 Actor 继续产生攻击威胁或命中事件。

### FR-011 调试与可观测性

- HUD 或调试面板可查看双方 Health、Posture、ActionState、ActionSerial 和 Target；
- Blackboard 可查看 PendingReaction、SelectedActionId、DebugCandidateSummary 和 DebugFailureReason；
- 每个命中结果可记录 Source、Target、ActionSerial、Result 和实际伤害；
- 正式运行默认关闭逐帧日志；
- 所有测试失败必须能定位到 Producer、结算、动作、导航或动画层。

### FR-012 原版战斗 UI

- 从本地原版 GFX、布局包与纹理包识别、提取并导入敌我血量/架势和锁定素材，保留可追溯来源；
- 依据原版布局、图集区域和显示状态实现主角、普通敌人及 Boss UI，不以占位 ProgressBar 代替正式原版效果；
- 生命与架势只读取 GAS Character AttributeSet/Survival 提交后的快照，UI 不保存或计算第二套资源数值；
- 替换当前程序绘制的锁定圆环/刻线，保留既有目标选择、相机与屏幕投影逻辑，禁止两套标记叠加；
- 处理就绪、换 Pawn、切目标、死亡/回生和目标销毁；区分普通目标条与 Boss Encounter 的固定展示来源；
- 原版精确坐标、填充方向、动画和图集 UV 需有证据后实现，资源名扫描不等于视觉复现完成；
- 适配 DPI/安全区域/宽屏，运行时只依赖已导入 UE 资产，不依赖原版安装目录或编辑器插件；
- 拆分与验收见独立子需求 [original-combat-ui](original-combat-ui.md)。

## 5. 非功能需求

### NFR-001 架构边界

- `Source/Sekiro/` 使用 `SK` 前缀，提供通用 Gameplay 能力；
- `Plugins/` 不引用项目角色或资产；
- Lua 负责具体伤害倍率、弹刀窗口、阶段工作流和 Boss 策略；
- C++ 负责有限值校验、碰撞、委托、组件生命周期和游戏线程边界。

### NFR-002 确定性与幂等

- 同一 HitRequest 只能提交一次结果；
- 近战 ActionSerial 过期请求必须失败；已发射投射物以来源生命轮次为有效性边界，迟到反馈不能抢占射手新动作；
- 死亡和阶段事件只能在状态边沿发布一次；
- Reset、Abort 和 Hitbox Deactivate 可重复调用且结果一致。

### NFR-003 性能

- 武器 Sweep 只在攻击窗口运行；
- 事件通知不得每帧轮询整个队列；
- 不在 Tick 中加载资产或创建 Montage；
- Blackboard 只保存 AI 需要观察的摘要，不复制完整事件队列。

### NFR-004 可测试性

- C++ 组件提供自动化测试可调用的纯输入边界；
- Lua 战斗规则可使用 Mock 离线验证；
- 编辑器资产检查和 PIE 情境测试分开；
- PIE 仍由用户执行并反馈结果。

## 6. 验收门槛

### Gate A：属性与命中闭环

- [ ] 玩家攻击训练假人，生命和架势按 HitResult 变化；
- [ ] Guarded/Deflected 不重复扣除普通命中生命；
- [ ] 同一攻击窗口不会重复命中同一 Actor；
- [ ] 生命归零只触发一次死亡。

### Gate B：玩家战斗闭环

- [ ] 玩家攻击、防御、弹刀、闪避、受击、架势崩溃和死亡均能收敛；
- [ ] 输入锁和 Montage 在 Abort/死亡后恢复；
- [ ] 玩家使用道具能发布 TargetAction。

### Gate C：AI 基础闭环

- [ ] AI 获取/失去玩家目标时行为树正确进入和退出 Engage；
- [ ] AI 单段攻击能够命中玩家并产生统一 HitResult；
- [ ] 接近、侧移、后撤不发生导航与 Root Motion 双重位移；
- [ ] 目标死亡后 AI 停止动作并清空目标。

### Gate D：事件与即时中断

- [ ] WeaponContact、DamageReceived、ProjectileImpact、AttackThreat 和 TargetAction 都有真实 Producer；
- [ ] AI 普通攻击运行期间收到可取消反应时能够 Abort；
- [ ] ActionSerial 保证旧 Task 不停止新反应；
- [ ] 不可取消窗口的延迟有明确诊断。

### Gate E：Boss 阶段闭环

- [ ] BossPhase 有真实状态权威；
- [ ] 阶段变化产生 Pending 并被行为树消费；
- [ ] 阶段切换能清理旧动作、导航和事件；
- [ ] Boss 最终死亡能结束整个 Encounter。

### Gate F：弦一郎接入

- [ ] 弦一郎 23 Act/29 Kengeki 使用统一战斗协议；
- [ ] `SelfHealthRatio`、`BossPhase`、目标状态与事件 Key 都有 Producer；
- [ ] Reaction > Phase > Tactical 具备实际运行时抢占语义；
- [ ] 用户 PIE 情境测试通过后，才进入权重和手感校准。

### Gate G：原版战斗 UI（独立子需求）

- [ ] 敌我生命/架势条使用可追溯原版素材，数值与 GAS 一致；
- [ ] 排版、透明度、填充与显隐有原版布局/参考画面对照；
- [ ] 原版锁定标记替换旧绘制，切目标和失锁不残留旧图像或数值；
- [ ] 初始化、上限变化、死亡/回生和目标销毁正确刷新或隐藏 UI；
- [ ] 不同分辨率/DPI 下布局正确；用户明确授权后完成场景和视觉验收。

## 7. 任务树

<!--
  状态图标：⬜ 未开始  🔄 进行中  ✅ 已完成  ❌ 已取消
  依赖标注：任务后追加 (依赖: 1, 2.1)
-->

- ✅ 1. 需求建立与基线审计
  - ✅ 1.1 盘点 CombatComponent、Weapon、Input、AIController 和 AnimInstance 已有能力
  - ✅ 1.2 识别生命、统一结算、事件 Producer、即时 Abort 和阶段状态缺口
  - ✅ 1.3 输出需求文档和详细技术方案
- 🔄 2. GAS 数值基础、生命/躯干生命周期与 Gameplay 编辑器工具（2.1–2.5 已完成；2.6 插件迭代中） (依赖: 1)
  - ✅ 2.1 [独立子任务：GAS 数值基础系统](gas-numeric-foundation.md)
  - ✅ 2.2 [USKSurvivalComponent 生命、回生与躯干状态设计](survival-component.md) (依赖: 2.1)
  - ✅ 2.3 实现 Survival C++ 组件、全量躯干 AttributeSet 参数、ASC 策略及 Lua/Blueprint 接口 (依赖: 2.2)
  - ✅ 2.4 接入玩家和通用 AI 死亡、显式回生接口及全部躯干逻辑，清理 Combat 数值/公式 (依赖: 2.3)
  - ✅ 2.5 增加状态门禁、死亡/崩溃优先和回生幂等契约测试代码，仅编译未执行 (依赖: 2.4)
  - 🔄 2.6 [独立子需求：Gameplay C++ 插件与 Lua 标签资产生成](lua-gameplay-tags.md)，可扩展编辑器窗口、原生 DataTable 与标签源注册 (依赖: 2.1, 2.4)
- 🔄 3. 统一命中结算协议（代码已实现，运行验收待授权；标签导入器资产迁移不阻塞原生接口） (依赖: 2.1–2.5)
  - ✅ 3.1 定义 HitRequest、HitResult 与 DamageChannel
  - ✅ 3.2 实现 CombatComponent 原子结算入口，Lua 纯裁决/纯躯干计算与提交后演出分离
  - ✅ 3.3 迁移 Weapon 普通命中，移除独立固定伤害路径
  - ✅ 3.4 迁移 AI Projectile 命中路径，锁存发射票据与生命轮次
  - 🔄 3.5 补充身份、去重、免疫、复合提交与 Lua 规则测试代码；仅编译/语法检查，Hit/Guarded/Deflected/Dodged/PostureBroken/Killed 运行矩阵未执行
- ⬜ 4. 玩家战斗闭环 (依赖: 3)
  - ⬜ 4.1 完成轻攻击连段和攻击窗口
  - ⬜ 4.2 完成 Guard/Deflect 与架势反馈
  - ⬜ 4.3 接入闪避无敌和受击/死亡收敛
  - ⬜ 4.4 接入使用道具中性事件
  - ⬜ 4.5 完成玩家 Lua 离线与编译验证
- ⬜ 5. 动画和位移所有权 (依赖: 3, 4)
  - ⬜ 5.1 固化 Hitbox/Cancel/Projectile 语义曲线
  - ⬜ 5.2 固化 MoveTo 与 Root Motion 排他规则
  - ⬜ 5.3 验证 Montage 完成、Abort、死亡和超时清理
  - ⬜ 5.4 检查玩家与弦一郎 AnimBlueprint Slot 契约
- ⬜ 6. AI 目标与导航闭环 (依赖: 2, 5)
  - ⬜ 6.1 目标死亡/失效/重置清理
  - ⬜ 6.2 接近、侧移、后撤和无路径失败关闭
  - ⬜ 6.3 AI 单动作攻击玩家闭环
  - ⬜ 6.4 完成 AI 编译与编辑器静态验收
- ⬜ 7. 战斗事件 Producer (依赖: 3, 4, 6)
  - ⬜ 7.1 AttackThreat Producer
  - ⬜ 7.2 TargetAction Producer
  - ⬜ 7.3 ReactionRequested Producer
  - ⬜ 7.4 ForceReplan 与 SemanticSignal 边界
  - ⬜ 7.5 事件去重、顺序和队列溢出测试
- ⬜ 8. AI 即时中断链 (依赖: 7)
  - ⬜ 8.1 Pending 事件通知与 Blackboard Producer
  - ⬜ 8.2 BehaviorTree Abort Decorator/Service 接入
  - ⬜ 8.3 ActionSerial 所有权转移与不可取消窗口
  - ⬜ 8.4 Reaction 延迟和失败原因诊断
- ⬜ 9. Boss 阶段与忍杀状态 (依赖: 2, 8)
  - ⬜ 9.1 通用 Encounter/Phase 组件
  - ⬜ 9.2 BossPhase 与 PhasePending Producer
  - ⬜ 9.3 阶段切换动作、无敌和状态清理
  - ⬜ 9.4 Boss 最终死亡与 Encounter 结束
- 🔄 10. 原版战斗 UI、调试与测试场景 (完整验收依赖: 3, 6, 8, 9；10.5 的资产与玩家 HUD 可先开展)
  - ⬜ 10.1 Health/Posture/ActionSerial/Target 调试快照
  - ⬜ 10.2 双向战斗训练假人和固定位置测试用例
  - ⬜ 10.3 自动化、UBT 与资产编译回归
  - ⬜ 10.4 输出用户 PIE 测试步骤并暂停等待反馈
  - 🔄 10.5 [独立 UI 子需求：原版敌我血量/架势条与锁定标记](original-combat-ui.md)（两张原版UE纹理已实际导入，基础 HUD/Lua 与导入窗口已实现并编译；原版完整时序/视觉未验收）
- ⬜ 11. 弦一郎正式接入 (依赖: 10)
  - ⬜ 11.1 补齐弦一郎 Blackboard Producer
  - ⬜ 11.2 将 ActionCatalog 接入统一 HitRequest/Result
  - ⬜ 11.3 验证 Reaction/Phase/Tactical 即时优先级
  - ⬜ 11.4 按 Golden 差异和用户 PIE 反馈校准行为

## 8. 风险与决策

| 风险 | 影响 | 设计决策 |
|------|------|----------|
| GAS 与旧组件重复保存数值 | 双重写入、Buff 聚合与 Lua 不一致 | 数值唯一归 AttributeSet；Survival 管理生命/躯干状态，Combat 不保留可写崩溃副本 |
| Weapon 先判断接触再独立 TakeDamage | 可能重复结算或结果不一致 | 统一为一次 HitRequest/HitResult 原子结算 |
| AI 事件直接从 C++ 写弦一郎 Blackboard | 通用层硬编码角色策略 | C++ 只发布 Pending；项目 Controller/Service 写可配置 Key |
| 每个事件都强制中断 Montage | 破坏不可取消动作和阶段演出 | Lua/ActionCatalog 声明取消窗口，超时有兜底 |
| MoveTo 与 Root Motion 同时驱动 | 滑步、瞬移、路径残留 | 动作开始前显式停止导航，导航动作禁止战斗 Root Motion |
| 阶段系统直接选择招式 | Encounter 与 AI 策略耦合 | 阶段组件只发布状态，行为树/Lua 选择动作 |
| 直接在弦一郎上联调所有基础模块 | 难以定位问题 | 先玩家↔训练假人，再通用 AI↔玩家，最后弦一郎 |

## 9. 变更记录

2026-08-26：完成任务 3.1–3.4 的统一命中基建。Weapon 开窗读取 GAS AttackPower，Weapon/Projectile 均使用签发来源票据和统一结算，不再独立 TakeDamage；目标生命/躯干复合提交，Lua 纯规则与演出分离，保留免疫、致死优先、动作/生命轮次、去重及重入门禁。ProjectileImpact 只发受击方，避免射手误响应自身箭矢。

验证：`SekiroEditor Win64 Development -Module=Sekiro` 最终编译成功（11.03 秒），主任务再次增量确认 `Target is up to date`（exit 0）；3 个 Lua 文件仅语法编译，594/594 函数文档完整，13 个 C++ 文件 UTF-8 BOM 与变更空白检查通过。新增 3 项原生契约测试及 1 份 Lua 规则测试仅编译，未执行测试、未启动 PIE。日志：`Script/temp/build_combat_hit_foundation_development.log`。当前 DebugGame 编辑器尚未获准重启，新反射接口仍待 DebugGame 补编译/重载；任务 3.5 与所有运行 Gate 保持未验收。

2026-08-26：新增独立 UI 子需求 10.5 和 FR-012 / Gate G；已通过 C++ 导入器保存原版图集/锁定 Texture2D，核查主要布局并完成基础 UI、Lua 绑定与导入窗口，编译/静态及资产设置检查通过。完整原版时序与视觉仍未验收，未启动 PIE。

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 创建需求；完成现有战斗基础审计、范围、验收 Gate 和任务树 |
| 2026-08-26 | 按用户要求独立拆出 gas-numeric-foundation 并开始设计实现；生命组件更名 SKLifeComponent，死亡工作流延后至数值基础完成 |
| 2026-08-26 | 子任务 2.1 已实现并通过 UBT、Lua 静态检查；契约测试仅编译未执行，LifeComponent、死亡工作流和统一命中协议仍待实施 |
| 2026-08-26 | 用户将生命组件范围扩至躯干并要求更合适命名；完成 USKVitalsComponent 设计，替代旧 LifeComponent 方案；仅设计项 2.2 完成，代码待实现 |
| 2026-08-26 | 用户确认 Survival 并要求实施：完成 2.3–2.5 的原生/Lua 实现与契约测试代码，验证记录见独立子任务；未启动 PIE 或执行测试，任务 3 的统一命中仍待实施 |
| 2026-08-26 | 用户确认 Survival 命名；任务链接和接口统一更名，补齐全量躯干属性与逻辑迁移要求，代码仍待实施 |
| 2026-08-26 | 新增并完成子需求 2.6：Lua 层次表生成 UE GameplayTag 工具；首批 12 标签与 Survival 查询接入完成，未执行运行验收 |
| 2026-08-26 | 用户要求将 2.6 改为通用 Gameplay C++ 插件，提供 UE 编辑器窗口和原生标签 DataTable；重新打开任务进行替换和资产迁移 |
