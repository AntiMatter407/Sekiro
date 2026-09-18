# Boss AI 与玩家对战基础闭环技术方案

更新：2026-08-26。数值基础独立子任务：[GAS 数值系统](../plan/gas-numeric-foundation.md)，[详细方案](gas-numeric-foundation.md)。
生存组件：[独立进度](../plan/survival-component.md)，[USKSurvivalComponent 设计](survival-component.md)（已实现，验证范围见独立进度）。

## 1. 方案摘要

本方案在现有 `USKCombatComponent`、`ASKWeapon`、`ASKAIController`、玩家 Input/Movement、Lua 战斗脚本和
Lua BehaviorTree 之上补齐双向战斗闭环。核心不是继续向弦一郎 Task 增加临时判断，而是建立玩家和所有 AI
共同依赖的事实层：生命、命中结果、动作所有权、目标、事件和阶段。

先接入角色 GAS 数值基础，再新增两个通用 Gameplay 组件：

- `USKAbilitySystemComponent` 与 AttributeSet：生命、上限、攻击、护甲、架势等数值的唯一存储与效果入口；
- `USKSurvivalComponent`：管理生命轮次、死亡、回生及躯干崩溃/恢复；不配置或保存生命/躯干数值；
- `USKBossEncounterComponent`：Boss 阶段、忍杀节点、转换中和 Encounter 结束的唯一权威。

继续复用并增强：

- `USKCombatComponent`：Guard/Deflect 裁决、攻防结果转发、动作状态、ActionSerial、Montage 与事件队列；
- `ASKWeapon`：武器连续 Sweep 和单次攻击目标去重；
- `ASKAIController`：感知、TargetActor、导航和可配置 Blackboard Producer；
- Lua：玩家战斗规则、Boss Profile、取消窗口、伤害倍率和阶段编排。

## 2. 设计原则

### 2.1 单一权威

| 状态 | 唯一权威 |
|------|----------|
| 生命、上限、攻击、护甲及全部躯干数值参数 | GAS AttributeSet |
| 数值效果和无敌标签 | GAS AbilitySystemComponent / GameplayEffect |
| 死亡、回生、生命轮次、躯干崩溃状态 | `USKSurvivalComponent` |
| 防御姿态、崩溃受击动画 | `USKCombatComponent` 与 Lua |
| 战斗动作和 Serial | `USKCombatComponent` |
| 武器攻击窗口和已命中集合 | `ASKWeapon`，由 CombatComponent 控制 |
| AI 当前目标 | `ASKAIController` + Blackboard `TargetActor` |
| AI 事件事实 | Owner 的 `USKCombatComponent` 队列 |
| Boss 阶段和忍杀节点 | `USKBossEncounterComponent` |
| 玩家/Boss 具体策略 | 项目 Lua |
| 最终 Pose | AnimBlueprint |

禁止两个模块分别保存可独立变化的同一状态。例如 AttributeSet 之外的 SurvivalComponent、Character、CombatComponent 不得保存可独立写入的生命或架势副本；
Weapon 不得在 CombatComponent 已返回 Guarded 后再次独立 `TakeDamage`。

### 2.2 通用 C++、具体 Lua

C++ 只处理：

- UObject/Actor/Component 生命周期；
- 有限数值、对象有效性、序列号和线程边界校验；
- 武器 Sweep、碰撞、委托、事件缓存和 Montage 生命周期；
- Blueprint/Lua 可调用的语义接口。

Lua 处理：

- Combat Lua 负责轻攻击连段与 Guard/Deflect 裁决；Survival Lua 读取 GAS 中的全部躯干参数，处理躯干增长、封顶、恢复和崩溃；
- Boss 动作数据、威胁窗口、反应优先级和取消策略；
- 阶段进入/退出工作流；
- 具体资产、动作 ID、伤害参数和调试摘要。

## 3. 总体架构

```text
Player Input                         Boss BehaviorTree
    ↓ FSKCombatInputEvent                ↓ SelectedActionId
Player Combat Lua                    Boss Action Lua Task
    ↓ BeginCombatAction                   ↓ BeginCombatAction
CombatComponent ───────── ActionSerial / Montage / Hitbox ─────────┐
    ↓                                                             │
Weapon Sweep / Projectile                                         │
    ↓ FSKCombatHitRequest                                         │
Target CombatComponent.ResolveCombatHit                           │
    ├─ Guard / Deflect / Dodge / Invulnerable                     │
    ├─ Survival 状态校验 → GAS 生命/躯干复合提交                       │
    ├─ Survival 按最终快照收敛死亡/崩溃状态                           │
    └─ FSKCombatHitResult + FSKAICombatEvent                       │
              ↓                                                    │
      Reaction Pending → BehaviorTree Abort → ActionSerial 转移 ───┘

SurvivalComponent.OnDeathStarted → Target 清理 / Stop BT / Stop Montage / Close Hitbox
SurvivalComponent.OnPostureBroken → 受控崩溃演出 / 躯干恢复流程
BossEncounter.OnPhaseChanged → BossPhase / PhasePending → Phase Branch
```

## 4. 数据契约

### 4.1 命中请求

`Source/Sekiro/Combat/SKCombatTypes.h` 的统一请求由攻击者组件签发，核心字段如下（完整反射声明以代码为准）：

```cpp
USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHitRequest
{
    GENERATED_BODY()

    TObjectPtr<AActor> SourceActor = nullptr;
    TObjectPtr<AActor> TargetActor = nullptr;
    int32 SourceActionSerial = 0;
    int64 SourceLifeSerial = 0;
    int64 HitSourceSerial = 0;
    ESKIncomingAttackType AttackType = ESKIncomingAttackType::Light;
    ESKCombatDamageChannel DamageChannel = ESKCombatDamageChannel::Melee;
    float HealthDamage = 0.f;
    float PostureDamage = 0.f;
    FVector ImpactPoint = FVector::ZeroVector;
    FVector AttackDirection = FVector::ZeroVector;
    FName EventTag = NAME_None;
};
```

规则：

- Source/Target 必须有效且不同；
- HealthDamage/PostureDamage 必须为有限非负数；
- 来源必须通过 CombatComponent 签发有效 `HitSourceSerial`，并匹配来源、生命轮次、攻击类型、数值和通道；调用者不能随意构造一个有效序号绕过来源校验；
- 近战票据绑定当前动作与碰撞窗口，动作结束/失效后拒绝；投射物在发射时校验动作并锁存生命轮次，允许已发射的箭在正常收招/换招后继续飞行，但死亡、回生和来源失效会使其不可结算；
- Request 是一次接触事实，不保存 UObject 以外的运行策略；
- Weapon、Projectile 和后续危险攻击都使用同一结构。
- 同一个来源票据对同一个目标只允许一次受理接触，包含格挡、弹刀和闪避；拒绝的非法请求不能修改任何资源。两边组件在结算过程中阻止重入，避免委托嵌套造成重复命中。

### 4.2 命中结果

```cpp
UENUM(BlueprintType)
enum class ESKCombatHitOutcome : uint8
{
    Ignored,
    Hit,
    Guarded,
    Deflected,
    Dodged,
    Invulnerable
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHitResult
{
    GENERATED_BODY()

    ESKCombatHitResultCode Code = ESKCombatHitResultCode::Rejected;
    ESKCombatHitOutcome Outcome = ESKCombatHitOutcome::Ignored;
    float AppliedHealthDamage = 0.f;
    float AppliedPostureDamage = 0.f;
    bool bPostureBroken = false;
    bool bKilled = false;
    int32 TargetActionSerial = 0;
    FName RejectionReason = NAME_None;
    FSKNumericResult Numeric;
};
```

`PostureBroken` 和 `Killed` 是结果标志，不与接触 Outcome 混成互斥枚举。按 Survival 设计，同笔致死并打满躯干时死亡优先，不启动新崩溃。
`Killed` 表示本次导致进入 Dying，不代表 Boss 已最终击败或不能回生；奖励/阶段结束不能仅据此标志决定。
结果另记录攻击方实际躯干反馈及崩溃标志，不把目标与来源的两个 ASC 声称为可回滚的跨角色事务。

### 4.3 纯裁决、复合提交与演出边界

1. Weapon 开窗时从 Lua `ResolveOutgoingHealthDamage` 读取源 GAS `AttackPower`；Projectile 保留现有 Lua 伤害参数。原生碰撞端不再保存固定 `100` 伤害路径。
2. `EvaluateCombatHit(Request)` 只读判断 Hit/Guarded/Deflected/Dodged，返回最终生命伤害与攻守双方的姿态语义，不播放 Montage、不写资源、不改动作号。
3. `Survival.EvaluatePostureImpact(Reason, AttackType, AdditionalDamage)` 只读 GAS 全量参数，计算衰减与封顶；附加姿态在封顶之前累加，不能绕过成功弹刀的非崩溃上限。已经崩溃的目标返回合法零姿态，生命通道仍可结算。
4. 目标通过 `ApplySurvivalImpact` 向 ASC 一次提交 Health/Posture，沿用逐通道免疫与死亡优先规则。数值回执保留真实 `Applied/NoChange/PolicyRejected` 与通道原因；免疫不等于非法接触，不能因生命免疫顺带阻断原本允许的姿态通道。
5. 目标提交受理后，再处理攻击者姿态反馈。由于目标提交的委托可能修改攻击者资源，反馈前必须重新读取来源快照并计算封顶，同时再次校验生命轮次。该反馈是独立来源 ASC 的提交，不能回滚已经提交的目标伤害；反馈失败需保留诊断信息，不能伪造成功增加值。
6. 最后调用 `HandlePostureImpactCommitted` 重置恢复计时、发布真实结果事件，并通过 `HandleCombatHitCommitted` 选择演出。演出只消费实际结果，不再调用旧 `ApplyPostureImpact`，动画失败也不能把弹刀改成普通命中。

Hit 使用 `DeflectFailed` 姿态增长语义，Guarded 使用 `Guarded`，Deflected 使用 `DeflectSuccess`；攻击方对应 `AttackSuccess/AttackGuarded/AttackDeflected`。这些语义仅选取已有 GAS 参数，不在 Combat Lua 内复制姿态公式。

旧 `HandlePostureImpact/ApplyPostureImpact` 保留给既有独立姿态演示入口，真实 Weapon/Projectile 不再使用；纯计算与提交后路径共用同一 Survival 公式。

当前边界：

- 阵营过滤使用引擎 `IGenericTeamAgentInterface` 已配置的有效 TeamId；`NoTeam` 不当作全员同队，完整阵营身份配置仍待后续。
- 闪避沿用已有 Dodge/Step 状态，精确无敌帧曲线属于后续动作窗口基建，本轮不声称恢复原版帧数。
- 未配置护甲曲线或招式倍率前不自造伤害公式；本轮近战伤害来自 GAS AttackPower，投射物来自已有 Lua Profile。GAS 的通用护甲曲线接口保持可用，完整配表另行接入。
- 提交后的演出校验动作号；迟到箭可以造成合法伤害，但不得以旧动作反馈抢占射手的新动作。

### 4.4 事件映射

| HitResult/玩法事实 | 事件接收者 | 事件类型 |
|-------------------|------------|----------|
| 任意有效武器接触 | 攻守双方按既有协议 | `WeaponContact` |
| 实际生命伤害 > 0 | 受击方 | `DamageReceived` |
| 投射物接触 | 被命中方 | `ProjectileImpact` |
| 攻击进入响应窗口 | 当前目标 | `AttackThreat` |
| 玩家使用道具 | 观察该玩家的 AI | `TargetAction` |
| 架势崩溃/强制受击 | 反应执行者 | `ReactionRequested` |
| 目标/路径/阶段事实失效 | 需要重新决策的 AI | `ForceReplan` |

事件只描述事实。ReactionRouter 决定是否响应、选哪个动作以及优先级。ProjectileImpact 沿用只发受击方的既有约定，避免射手把自己发射的箭识别为来袭；攻击方仍通过提交后通知消费弹刀反馈。

## 5. GAS 数值基础与生命、躯干状态组件

### 5.1 独立数值子任务

[GAS 数值基础系统](gas-numeric-foundation.md) 单独设计、实现和追踪，作为父任务 2.1。
该子任务已完成代码实现与 UBT 编译验证；尚未执行运行时测试，不代表本父任务对战闭环已验收。
角色持有 ASC，Health/MaxHealth/AttackPower/Armor/Posture/MaxPosture/PostureRecoveryRate 由 AttributeSet 保存，
主角与 AI 的初始值各自由一份平铺 Character Lua 表输入，不拆 Health/Combat 分组；运行时由唯一 USKCharacterAttributeSet 保存，数值变化通过 GameplayEffect 提交。
当前实现中 SurvivalComponent 持有生命/崩溃状态，CombatComponent 保留动作与攻防裁决；GAS 保存全部躯干参数，组件不持有可写数值副本。

### 5.2 USKSurvivalComponent（已实现，未做运行验收）

用户确认命名 USKSurvivalComponent，职责覆盖生命、死亡、回生和全部躯干逻辑，替代尚未实现的旧生命组件方案。
详见 [独立组件设计](survival-component.md)：生命状态与躯干状态分开，数值继续唯一存储在继承 UAttributeSet 的 GAS 属性集中。
Survival 提供带状态门禁的伤害/治疗/躯干请求及流程事件，ASC 负责 GE 执行，并在底层资源入口调用统一策略防止绕过。
回生通过 Begin/Complete/Cancel 和过程序号控制，资源提交成功才进入新生命轮次；普通治疗不隐式回生。
全部躯干数值（增长/强度/封顶、恢复速度/延迟/渐进倍率、崩溃时长及恢复比例）进入 AttributeSet；全部躯干处理逻辑迁入 SurvivalComponent/对应 Lua。
Combat 只提供攻防结果和通用动作接口，不再保存躯干数值配置或计算增长/恢复。清空躯干资源不自动结束崩溃，具体演出由 Survival Lua 编排。
角色已挂载 Survival，普通动作/输入/攻击窗口受其门禁约束。当前死亡无专用演出，在下一安全 Tick 收尾；回生资格、费用与触发时机由外部显式接入。

### 5.3 边界

GAS 数值子任务不实现死亡、复活、忍杀、Boss 阶段或 Weapon/Projectile 命中迁移。
SurvivalComponent 不重新裁决 Guard/Deflect，不计算护甲，C++ 不选择具体动画或直接控制 BehaviorTree；
对应 Survival Lua 编排全部躯干处理和演出请求，Character/Controller 响应生命周期事件执行通用清理。

### 5.4 Gameplay C++ 插件与标签生成子需求

[SekiroGameplay 插件](lua-gameplay-tags.md) 对应父任务 2.6：项目共享一份层次 Lua 字典，在可扩展的 UE 编辑器工具窗口中生成原生 GameplayTag DataTable，并注册到 GameplayTagTableList。
插件在主菜单栏增加“Lua玩法 / LuaGameplay”，功能作为独立下拉项注册；首项为“GameplayTag 的 Lua 导入”。
现有字典含 7 个 Survival 业务标签及 5 个父标签；Survival 绑定资源策略前验证全部必需标签，缺失时拒绝绑定，不重复原生注册。
字典定义与角色上的状态/GE 标签贡献分开；后续战斗标签按同一表扩展，不为角色复制标签字典。
用户已要求替换早期 Python/INI 方案；当前插件实现及迁移进度以独立子需求为准。后续 Gameplay 功能通过工具注册接口扩展，不改变标签解析器职责。

## 6. `USKCombatComponent` 增强设计

### 6.1 原子结算入口

统一入口为不可由 Lua 覆写提交过程的原生 `BlueprintCallable`：

```text
ResolveCombatHit(Request) → Result
```

实际结算顺序：

1. 验证游戏线程、对象、世界、权限、有限值、阵营与来源票据；
2. 校验双方 Survival 就绪、存活以及生命轮次；为双方 Combat 设置结算重入保护；
3. 通过只读 `EvaluateCombatHit` 取得接触结果和姿态语义。当前 Lua 使用 GuardRaise/Guarding 与 Dodge/Step 状态；方向门禁和精确窗口留到对应动作基建；
4. 通过只读 `EvaluatePostureImpact` 取得目标与来源的姿态需求；非 Hit 结果不能携带生命伤害；
5. 提交前重新验证来源票据和目标生命轮次，登记本票据已受理目标；
6. 经 Survival / GAS 一次提交目标 Health/Posture，按实际回执收敛死亡/崩溃；免疫按资源通道处理，不预先把整个接触判为 Invulnerable；
7. 重新计算并提交独立的来源姿态反馈，分别保留双方数值回执；
8. 重置有效姿态接触的恢复计时，发布 WeaponContact/ProjectileImpact、实际 DamageReceived 及崩溃 ReactionRequested，再由 Lua 消费结果编排演出；
9. 返回同一个权威结果给 Weapon/Projectile。非法请求返回 Rejected 和原因，规则缺失时失败关闭，不退回固定伤害。

Lua 扩展点是 `ResolveOutgoingHealthDamage`、`EvaluateCombatHit`、`EvaluatePostureImpact` 与提交后通知，而不是整个结算函数。纯裁决不能播放动画、写资源或改变动作号。

此处“原子”指目标 Health/Posture 使用同一复合入口、一次接触只形成一份对外结算结果；GAS 多属性修改不自带事务回滚，不能在单个属性回调中提前发布完整 HitResult，也不承诺攻守双方 ASC 的跨角色回滚。

### 6.2 ActionSerial 约束

- `BeginCombatAction` 产生新 Serial；
- Weapon 激活时锁存 Serial；
- Sweep 命中时携带锁存值；
- 近战 Serial 失效时忽略命中，不允许旧动画残留碰撞；
- 投射物签发时校验动作号，离手后允许正常收招/换招，但来源死亡、回生或对象失效后拒绝；旧箭的提交后反馈不能抢占来源的新动作；
- Abort 先失效 Serial，再关闭 Hitbox，再停止自己拥有的 Montage；
- Montage End 只有身份和 Serial 同时匹配时才能收敛状态。

### 6.3 攻击窗口

攻击窗口优先从动画语义曲线/Notify 驱动：

```text
HitWindow 0→1：ClearHitActors + ActivateHitbox + Lock ActionSerial
HitWindow 1→0：DeactivateHitbox
```

缺少窗口数据时失败关闭，不允许整段 Montage 持续命中。

## 7. Weapon 与 Projectile 迁移

### 7.1 Weapon

保留当前完整刀刃多采样 Sweep 和 `AlreadyHitActors`，修改结算端：

```text
旧：ResolveIncomingWeaponContact → 若 Hit → 固定 TakeDamage(100)
新：构造 FSKCombatHitRequest → TargetCombat.ResolveCombatHit → 消费 Result
```

Weapon 不再决定最终生命伤害是否生效，只负责提供来源、命中点、攻击方向和配置伤害。

### 7.2 Projectile

Projectile 命中时同样构造 HitRequest。`ProjectileImpact` 应在统一结算结果明确后发布，避免 ProjectileImpact 与
DamageReceived 的顺序随实现路径漂移。是否允许 Guard/Deflect Projectile 由 Lua 规则根据 DamageChannel 和
AttackType 决定。

## 8. 玩家战斗工作流

### 8.1 输入到动作

```text
InputManager
→ FSKCombatInputEvent
→ Player Combat Lua
→ 检查状态/输入缓存/取消窗口
→ BeginCombatAction
→ PlayCombatAnimation
→ 动画窗口开启 Weapon
```

Attack 和 Guard 保持现有输入事件。UseItem 应发布独立 Gameplay 意图，由玩家道具系统在实际提交成功时发布
`TargetAction`，不能只在按键边沿发布，避免没有道具也触发 Boss 惩罚。

### 8.2 Guard 与 Deflect

- Guard Started 进入 GuardRaise；
- 到达有效姿态后进入 Guarding；
- Guard Completed 进入 GuardLower；
- Deflect 窗口是 Guard Started 后的短时间语义窗口；
- 当前根据 GuardRaise/Guarding 返回 Deflected 或 Guarded，后续补方向、AttackType 与精确窗口约束；
- 攻防结果交给双方 Survival 的纯 `EvaluatePostureImpact`，由统一原生命中入口提交资源；已有 Combat 的 `HandlePostureImpact` 仅保留给独立姿态演示入口，不参与真实 Weapon/Projectile 结算；
- Deflect 产生 WeaponContact，供 Boss Kengeki 路由消费。

### 8.3 Dodge

Movement 已有 Dodge 状态。需要向 CombatComponent 提供稳定的 Dodge/Invulnerable 查询或命名无敌原因：

- 位移状态不等于全程无敌；
- 只有配置窗口内返回 Dodged/Invulnerable；
- Dodge 结束和死亡必须清除对应无敌原因。

## 9. AI 目标、导航与攻击

### 9.1 目标生命周期

AIController 订阅当前 Target SurvivalComponent 的 `OnDeathStarted`：

```text
Target OnDeathStarted
→ Clear TargetActor
→ StopMovement
→ InvalidateCombatAction
→ Clear Pending Events
→ BehaviorTree 退出 EngageTarget
```

Controller UnPossess、Owner 死亡和 Encounter Reset 使用同一幂等清理入口。

### 9.2 位移所有权

定义两种互斥 Owner：

- `Navigation`：MoveTo/CharacterMovement 驱动；
- `CombatRootMotion`：战斗 Montage 驱动。

开始 CombatRootMotion 前必须 `StopOwnerAIMovement()`；BuildMoveGoal 前必须确认没有全身 Root Motion 动作。
动作结束后不自动恢复旧路径，而是让 BehaviorTree 重新规划。

## 10. 事件 Pending 与即时 Abort

### 10.1 通知边界

`USKCombatComponent` 增加通用 `OnAICombatEventPublished` 委托。C++ 委托只传递事件事实，不引用 Blackboard。

项目侧 AIController 或通用 Service：

1. 订阅 Owner CombatComponent；
2. 事件入队后把可配置 `PendingCombatEvent` Key 写 true；
3. BehaviorTree Decorator 对 Tactical 分支使用 `LowerPriority` 或 `Both` Abort；
4. ReactionRouter 排空事件后把 Pending 清回 false；
5. 新事件在 Reaction 执行期间到达时再次评估 Priority。

### 10.2 取消门禁

ActionCatalog 为动作声明：

```text
InterruptPolicy: Never | OnWindow | Always
InterruptCurve: CancelWindow
MaximumDeferredReactionSeconds
```

如果当前动作不可取消，Pending 保留；窗口打开后立即 Abort。超过最大延迟时执行安全策略并写入
`ReactionDeferredTimeout`，不能永久饥饿高优先级事件。

## 11. Boss Encounter 与阶段

### 11.1 状态

```text
CurrentPhase
RemainingDeathblowNodes
bTransitioning
bEncounterEnded
EncounterSerial
```

### 11.2 事件

- `OnPhaseTransitionRequested`；
- `OnPhaseChanged`；
- `OnDeathblowNodeConsumed`；
- `OnEncounterEnded`；
- `OnEncounterReset`。

阶段组件不依赖弦一郎 BehaviorTree。弦一郎 Lua 适配器订阅事件并更新 `BossPhase`、
`bPhaseTransitionPending` 和阶段相关 CombatMemory。

### 11.3 阶段转换顺序

```text
请求阶段切换
→ 标记 Transitioning
→ 失效当前 ActionSerial
→ Stop MoveTo / Close Hitbox / Stop owned Montage
→ 清理旧反应与阶段冷却
→ 播放阶段动作/演出
→ 写入新 Phase
→ 发布 OnPhaseChanged
→ 清除 Transitioning
→ BehaviorTree 重新规划
```

## 12. 死亡与 Reset 时序

统一死亡顺序：

```text
GAS Health 归零，SurvivalComponent 首次进入 Dying，取消原有躯干崩溃流程
→ 发布 OnDeathStarted
→ 外部订阅者使 CombatComponent ActionSerial 失效
→ DeactivateHitbox / Stop owned Montage
→ Stop AI Movement / Disable Gameplay Input
→ AIController 清理 Target 或停止自身 BehaviorTree
→ BossEncounter 决定消费忍杀节点或结束 Encounter
```

统一 Reset 顺序：

```text
InvalidateAction
→ DeactivateHitbox
→ Clear Combat/Input Event Queues
→ 经专用入口重置生命/躯干资源与 Survival 生命轮次（完整世界 Reset 契约后续细化）
→ Reset Encounter/Target
→ 恢复输入或重启 BehaviorTree
```

所有步骤必须可重复调用。
回生不使用上述普通 Reset 序列：按 Survival 的 Begin/Complete/Cancel 协议执行，不能在 Dead 状态直接调用普通 RestoreHealth 或 ResetPosture。
世界 Reset 同样需要专门的受控资源入口，不得借重复初始化绕过生命周期门禁。

### 12.1 原版战斗 UI 独立子需求

[原版战斗 UI](original-combat-ui.md) 对应父任务 10.5：从原版游戏资产提取敌我血量条、架势条和锁定素材，按原版布局实现显示。
复用现有 HUD/UIManager；资源条只读 GAS/Survival，锁定 UI 保留 CameraManager 目标和投影，只替换程序绘制外观。
原版图集与锁定素材已导入为两张 UE Texture2D，主要坐标/UV 已核查，基础 HUD/Lua 绑定与 C++ 导入窗口已实现并编译；资产设置/来源与保存结果已核对。完整原版显隐、成长条长和动画仍待后续复原，尚未运行验收。
普通敌人资源条与 Boss 固定 HUD 分别处理展示来源，不把所有敌方显示对象强制等同锁定目标。
可通过 LuaGameplay 顶层菜单增加独立资源导入功能，通用编辑器能力留在插件，项目 HUD 与角色规则留在 Source/Sekiro 和项目 Lua。
资源提取及玩家 HUD 可在 GAS/Survival 之后先实施；完整普通敌人受击显示和 Boss 阶段装饰分别依赖命中协议与 Encounter 系统。

## 13. 文件变更规划

### 13.1 预计新增

| 文件 | 职责 |
|------|------|
| `Source/Sekiro/AbilitySystem/**` | 独立子任务实现 GAS 数值基础 |
| `Source/Sekiro/Character/SKSurvivalComponent.h/.cpp` | 生命、死亡、回生和躯干状态；不存储权威资源数值 |
| `Source/Sekiro/Character/SKSurvivalTypes.h` | 生命周期/躯干状态、过程序号、快照与结果 |
| `Content/Script/Gameplay/Sekiro/Character/SKSurvivalComponent.lua` | 全部躯干计算、恢复/崩溃流程与生命流程编排 |
| `Source/Sekiro/Combat/SKBossEncounterComponent.h/.cpp` | Boss 阶段、忍杀节点和 Encounter 生命周期 |
| `Content/Script/Gameplay/Sekiro/AbilitySystem/**` | GAS 项目初始参数 |
| `Content/Script/Gameplay/Sekiro/Combat/SKBossEncounterComponent.lua` | 项目 Boss 阶段工作流 |
| `Source/Sekiro/Tests/SKCombatHitTestComponent.h/.cpp`、`SKSurvivalTests.cpp` | 原生命中票据、资源提交与事件契约测试，当前仅编译 |
| `Script/tests/lua/test_combat_hit_rules.lua` | 命中纯裁决、姿态封顶、提交后演出规则测试，当前仅语法编译 |
| `Script/tests/lua/test_player_boss_combat_foundation.lua` | 玩家/Boss 双向规则离线测试 |
| `Content/UI/Combat/**`、`Content/Script/Gameplay/Sekiro/UI/CombatHUDStyle.lua`（规划） | 原版血量/架势/锁定素材、Widget 及项目布局配置，详见独立 UI 设计 |

### 13.2 预计修改

| 文件 | 修改内容 |
|------|----------|
| `Source/Sekiro/Combat/SKCombatTypes.h` | HitRequest、HitResult、DamageChannel、Outcome |
| `Source/Sekiro/Combat/SKCombatComponent.h/.cpp` | 原子命中结算、Pending 委托、死亡收敛 |
| `Source/Sekiro/Weapon/SKWeapon.h/.cpp` | 迁移固定伤害到统一 HitRequest |
| `Source/Sekiro/AI/SKAIBattleProjectile.h/.cpp` | 投射物统一结算 |
| `Source/Sekiro/Character/SKCharacter.h/.cpp` | 挂载 GAS 并暴露查询；后续接入 SurvivalComponent |
| `Source/Sekiro/AI/SKAIController.h/.cpp` | Target 死亡清理和可配置 Pending Key |
| `Content/Script/Gameplay/Sekiro/Combat/SKCombatComponent.lua` | 玩家 Guard/Deflect/Dodge/伤害规则 |
| `Content/Script/AI/Genichiro/BT_Genichiro.lua` | Pending Decorator/Service 和阶段 Producer 契约 |
| `Content/Script/AI/Tasks/SKGenichiro*.lua` | 统一 Hit/Reaction/Phase 状态消费 |

除已独立规划的 Gameplay 工具及 UI 通用导入入口外，默认不修改 `Plugins/`。若 BehaviorTree 现有 DSL 无法声明所需 Decorator/Service，应先证明缺少的是通用能力，
再单独建立插件接口子需求。

## 14. 测试策略

### 14.1 离线测试

- HitRequest 有限值、对象、Serial 和阵营过滤；
- Hit/Guarded/Deflected/Dodged/Invulnerable 结果矩阵；
- Health/Posture 变化和死亡边沿；
- 事件类型、Source/Target、Serial 和顺序；
- Reset/Abort/Death 幂等；
- Pending 事件优先级和不可取消延迟；
- Boss 阶段边沿和重复请求。

### 14.2 编译与编辑器静态测试

- UBT 编译 `SekiroEditor` 对应模块；
- 玩家、通用 AI 和弦一郎 Blueprint 编译；
- BehaviorTree/Blackboard Key、Decorator、Abort Mode 静态检查；
- AnimBlueprint Slot、Root Motion 和语义曲线检查；
- Weapon/Projectile 资产引用和碰撞通道检查。
- 原版战斗 UI 的素材来源、图集映射、Widget/Material 编译与 GAS 绑定静态检查，实际视觉验收另行授权。

### 14.3 用户 PIE 测试

按 Gate 顺序执行，每遇到需要 PIE 的步骤暂停并由用户操作：

1. 玩家攻击静止假人；
2. 假人 Guard/Deflect 玩家攻击；
3. 通用 AI 单攻击命中玩家；
4. 玩家 Guard/Deflect AI 攻击；
5. AI 攻击中被玩家命中并即时 Abort；
6. 玩家使用道具触发 TargetAction；
7. 墙边、场地边缘、无路径和移动目标；
8. Boss 阶段切换、忍杀节点和最终死亡；
9. 最后接入弦一郎完整行为树。

每一步必须记录 Health、Posture、ActionState、ActionSerial、TargetActor、PendingEvent、SelectedActionId 和
DebugFailureReason。

## 15. 实施分批

| 批次 | 内容 | 出口 |
|------|------|------|
| Batch 0 | 独立 gas-numeric-foundation 子任务 | 属性唯一存储、GE 数值接口与架势迁移 |
| Batch 1 | SurvivalComponent + HitRequest/Result + Weapon | 生命/躯干状态一致，玩家可伤害假人并死亡 |
| Batch 2 | 玩家 Guard/Deflect/Dodge + 动画窗口 | 玩家战斗闭环 |
| Batch 3 | AI Target/Nav/单攻击 + Projectile | 通用 AI 可与玩家互伤 |
| Batch 4 | 全事件 Producer + Pending Abort | AI 可即时反应 |
| Batch 5 | Boss Encounter/Phase/Death | Boss 生命周期闭环 |
| Batch 6 | 弦一郎 Producer 与 ActionCatalog 接入 | 进入行为和手感校准 |

每个 Batch 独立完成代码、离线测试、编译和文档更新；需要编辑器或 PIE 时按用户约定暂停。

## 16. 变更记录

2026-08-26 实施补充：统一命中原生入口、来源票据、Weapon/Projectile 迁移及 Lua 纯裁决/姿态求值/提交后演出已完成。近战取 GAS AttackPower，护甲曲线与具体招式倍率仍待配表。Development UBT 与增量确认通过，Lua 仅语法编译；新增契约测试尚未执行，DebugGame 编辑器重载与场景验收仍待授权。准确验证范围见主计划任务 3.5，不能据此视为完整对战闭环验收。

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 按用户要求将 GAS 数值系统拆成独立子任务；取消 Vitality 数值所有权，生命组件改名 SKLifeComponent 并延后实现 |
| 2026-08-26 | Batch 0 GAS 数值基础已实现并通过编译与静态检查；Batch 1 生命周期和统一命中链尚未实施 |
| 2026-08-26 | 扩大生命组件职责至躯干状态，推荐命名 USKVitalsComponent；完成独立设计并同步状态所有权、复合结算和回生边界，尚未实现 |
| 2026-08-26 | 用户确认名称 USKSurvivalComponent；同步全量躯干参数归 AttributeSet、全部躯干逻辑归 Survival/对应 Lua，仍为待实现设计 |
| 2026-08-26 | 实现 Survival、GAS 全量躯干参数和状态门禁，完成 Lua 迁移；统一 Weapon/Projectile 命中及完整对战验收仍待后续任务 |
