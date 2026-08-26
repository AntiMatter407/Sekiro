# GAS 数值基础系统 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|----------|------|------|------|
| [GAS 数值基础系统](../plan/gas-numeric-foundation.md) | ✅ 已实现（编译验证） | 2026-08-26 | 2026-08-26 |

## 1. 决策与边界

本方案从 [Boss 对战基础闭环](boss-player-combat-foundation.md) 独立拆出。用户确认采用 GAS，本任务不实现生命/死亡流程。
生存组件由用户确认命名为 [USKSurvivalComponent](survival-component.md)，已在独立子任务实现，并扩展本文的资源策略与全量躯干参数。
生命、最大生命、护甲等数值绝不由该组件配置或持有。本文除明确的后续说明外，描述的是已完成的 GAS 数值基础实现。

引擎为本机 UE5.2。角色持有 ASC，Owner/Avatar 都为角色自身，玩家与 AI 通过 ASKCharacter 基类共享。
首版面向当前单机项目；不声称完成网络预测、属性复制验收或跨 Pawn 持续状态。
GameplayTag 字典通过 [SekiroGameplay 编辑器插件](lua-gameplay-tags.md) 维护：层次 Lua 表生成原生标签 DataTable，注册到 GameplayTagTableList，Survival 查询既有标签；不会把标签定义或状态副本放入 AttributeSet。

```text
ASKCharacter : IAbilitySystemInterface
  ├─ USKAbilitySystemComponent
  ├─ USKCharacterAttributeSet（生命、攻击、护甲、全部躯干属性）
  ├─ USKSurvivalComponent（生命、回生与躯干流程；数值只读 GAS）
  └─ USKCombatComponent（动作和攻防裁决；只读兼容查询转发 Survival）

每种角色独立 Lua 表（平铺 Character 属性）→ ASC 初始化 → GameplayEffect → USKCharacterAttributeSet
Lua 玩法规则 → Survival / ASC 受控语义接口 → GE → 属性变化与数值执行结果
```

C++ 负责 GAS 生命周期、有限值与资源边界、反射接口和提交；Lua 负责动作倍率、何时恢复、配置选择。
不迁移 Weapon/Projectile 的 TakeDamage 链，也不把现有 CombatComponent 伤害监听接到 GAS 扣血，以免在后续统一协议前形成双路径。

## 2. 属性契约

| 属性集 | 属性 | 约束/用途 |
|--------|------|-----------|
| USKCharacterAttributeSet | Health | 0 到 MaxHealth，实际生命资源 |
| 同上 | MaxHealth | 就绪后必须为正有限值 |
| 同上 | IncomingDamage / IncomingHealing | 非负临时提交量；执行后消费并清零 |
| 同上 | AttackPower / Armor | 非负有限值；护甲是减伤参数而非护盾 |
| 同上 | Posture | 0 到 MaxPosture，受击积累 |
| 同上 | MaxPosture | 就绪后必须为正有限值 |
| 同上 | PostureRecoveryRate | 非负有限值，单位为架势点/秒 |
| 同上 | 躯干增长、封顶、强度、恢复渐进、崩溃及回生比例等 23 个新增参数 | 完整字段及范围见 [生存组件数值清单](survival-component.md#21-全部躯干数值进入-attributeset) |
| 同上 | IncomingPostureDamage / IncomingPostureRecovery | 临时架势变化量；消费后清零 |

所有属性使用 FGameplayAttributeData；临时属性不作为角色存档、复制或 UI 状态。
角色只创建并注册一个 `CharacterAttributes` 默认子对象；生命与战斗字段不再拆为不同 AttributeSet，旧的两个属性集类已移除。
不预建缺少消费者的法力、体力、暴击、元素抗性属性。

### 2.1 基础值与当前值

- Health/Posture 为资源，本版经 Instant GE 执行改变基础值；不允许持续修饰器直接聚合这些资源。
- 攻击、护甲、上限和全部躯干参数允许受支持的 Duration/Infinite GE 聚合。
- 上限提高保留资源绝对值，不自动补满；上限降低把资源基础值实际截断；上限恢复不补回丢失资源。
- PreAttributeBaseChange / PreAttributeChange 维护有限值与边界；PostAttributeChange 处理上限变化后的资源收敛。
- PostGameplayEffectExecute 消费临时属性并提交资源变化；不能只靠它处理持续效果添加/移除。
- 属性校验和通知不能触发死亡动画、AI 决策或隐式复活。

## 3. 初始化配置

`FSKAttributeInitialization` 显式包含 MaxHealth、InitialHealth、AttackPower、Armor、MaxPosture、InitialPosture、PostureRecoveryRate，以及 Survival 扩展的 23 个参数，共 30 个字段。
角色的全部初值统一放在**同一个角色专属 Lua 文件的平铺 Character 表**，生命、攻击、护甲和躯干都是同级字段，不再分 `Health` / `Combat` 子表。
主角使用 [Character/Attributes.lua](../../Content/Script/Gameplay/Sekiro/Character/Attributes.lua)，通用 AI 使用 [AI/Attributes.lua](../../Content/Script/Gameplay/Sekiro/AI/Attributes.lua)。两份配置独立填写全部 30 个数值，不互相引用或继承。

角色 `AttributeConfigModule` 可在蓝图默认值、场景实例或 Spawn 参数中指定点分 Lua 模块名；为空时由 Lua 根据 `ASKAICharacter` 类型选择上述文件，不依赖是否已被控制器接管。
配置选择和表转换由 [CharacterAttributes.lua](../../Content/Script/Gameplay/Sekiro/AbilitySystem/CharacterAttributes.lua) 负责，C++ 不保存角色数值或角色文件路径。
Survival Lua 的 ReceiveBeginPlay 先订阅事件，再读取表并经 `InitializeFromValues(Values)` 一次性初始化 `USKCharacterAttributeSet`；同一 ASC 已就绪时不重读配置、不回血。
加载器拒绝缺文件、非普通表、缺字段、未知字段及非有限数值；GAS 继续负责完整范围校验。错误时记录模块/原因并保持未就绪，不回退其他角色配置。

表结构示意如下；实际文件必须保留全部 30 个字段：

```lua
local Character = {
    MaxHealth = 100.0,
    InitialHealth = 100.0,
    AttackPower = 100.0,
    Armor = 0.0,
    MaxPosture = 100.0,
    -- 其余躯干参数同级填写。
}
return Character
```

两份 Lua 配置暂保留迁移基线：最大生命/初始生命 100，攻击力 100，护甲 0，最大架势 100，初始架势 0，
基础架势恢复 18 点/秒。生命/攻击仅为项目初始占位参数，不能当成原版只狼考据结果；架势值与既有恢复上限保持一致。

初始化顺序：注册属性集 → 初始化 ActorInfo → 绑定 Survival 资源策略 → Survival Lua 订阅事件 → 读取角色 Lua 表 → 校验全部输入 → GE 写入基础/上限 → GE 写入资源 → 生存状态收敛 → 就绪通知。
InitialHealth / InitialPosture 是资源绝对值，不是比例。Lua 模块表只读，每个实例复制到独立的初始化结构，不将运行时 Health/Posture 写回 require 缓存。
`USKAttributeProfile` 和 ASC 的显式 `InitializeFromProfile` 保留为独立数值层兼容 API；角色已移除 AttributeProfile 属性/优先加载路径，不再自动使用 DataAsset 初值或 InitialEffects。旧角色配置须迁入对应 Lua 文件，装备增益另行显式应用。
任何通用数值写入入口要求就绪与游戏线程；重复 Possess 只更新 ActorInfo，不重新初始化。

## 4. 数值效果与结果

使用通用原生 GE 模板和 SetByCaller 参数作为可从 Lua 调用的默认执行路径；具体数值仍来自配置/调用参数。
这些模板不引用角色动作或项目资产，角色可另行配置 Duration/Infinite GE 处理装备和 Buff。

本版 ApplyAttributeEffect 是受限的属性增益接口：只接收非周期、无自定义执行器、只修改已登记统计属性的
Duration/Infinite GE。资源恢复由 Lua 在满足条件时调用即时语义接口；尚不开放任意周期 GE、任意执行器或直接资源 Modifier。
同时拒绝携带 Conditional、Overflow、Expiration 链式效果或 GrantedAbilities 的配置，避免子效果绕过当前数值入口边界。
这使每次资源提交都能获得明确回执；后续若增加中毒/持续治疗，应单独定义每个周期的来源和结算记录，不能把效果应用时回执当成全部周期的伤害。

需要的语义入口：

- 生命伤害、治疗；
- 架势伤害、恢复、清零；
- 快照与属性变化通知；
- 配置 GE 的应用/按句柄移除；
- 显式输入曲线的护甲减伤计算。

生命伤害入口接收“已裁决、已完成护甲计算”的伤害，禁止再次计算护甲。Lua 或后续统一命中管线可先读取攻击方快照，
再以命中时目标护甲调用可配置曲线计算，最后提交 GE。缺少曲线、非法输入或非法曲线输出必须失败，不默认猜测公式。
曲线将 Armor 映射为 [0,1] 生命伤害倍率；普通治疗/架势变化不自动套用护甲。

数值执行结果区分请求量、实际变化和原因。剩余 30 生命收到 100 伤害，实际伤害为 30；满血治疗实际变化为 0。
Instant GE 不需要持续效果句柄，不能单凭 Handle.IsValid 判断数值执行是否成功。
属性变化是事实通知，不等于完整 HitResult；多个属性的 GAS 修改不承诺通用事务回滚。

## 5. 效果叠加与所有权

- 装备持有效果按具体句柄移除，不通过“减回原数值”撤销。
- 同类 Buff 的叠层、刷新时间、按来源分组遵从 GE 资产设置，不在 Lua 再做一套聚合器。
- 持续资源修饰器及多个普通 Buff 竞争 Override 不作为首版支持的配置。
- UE5.2 默认同通道倍率按偏移求和：两个 1.2 得到 1.4；如需逐项相乘需另外设计显式通道/计算。
- 网络复制与预测模式属于后续单独验收，本次不以单机编译替代网络验证。

## 6. 架势迁移

USKCombatComponent 不再保存 CurrentPosture / MaxPosture；getter 和 OnPostureChanged 从 GAS 取得数据。
删除 Combat 的任意数值 setter 及躯干写入口，仅保留 GetPostureRecoveryRate 等只读兼容查询；ApplyPostureDamage / RestorePosture 由 Survival 提供，ASC 的普通 ResetPosture 也受生存策略约束。
Survival Lua 不设置组件最大架势，而是在 ASC 未就绪时调用独立属性初始化配置。

原恢复曲线为每秒 3→18；当前为 GAS PostureRecoveryRate × GAS 最低/最高恢复倍率之间的渐进值。
延迟、渐进时长、增长、封顶与崩溃参数均来自 AttributeSet；Survival Lua 判断是否冲刺/闪避/处于动作等条件。
CombatConfig.Posture 已移除，Combat 只转发攻防结果。达到上限后 Survival 进入 Broken；普通 ResetPosture 在 Broken 中拒绝，必须持崩溃令牌使用专用恢复接口。

## 7. 文件范围与派发

| 文件/目录 | 操作 | 负责 |
|-----------|------|------|
| Source/Sekiro/AbilitySystem/** | 新增 ASC、属性集、Profile、类型和数值效果 | gameplay-programmer |
| Source/Sekiro/Character/SKCharacter.h/.cpp | 挂载 GAS 与初始化 | gameplay-programmer |
| Source/Sekiro/Combat/SKCombatComponent.h/.cpp | 架势转发与就绪门禁 | gameplay-programmer |
| Source/Sekiro/Tests/*GAS* | 新增必要契约测试 | gameplay-programmer |
| Source/Sekiro/Sekiro.Build.cs | 加入 GAS 三个模块依赖 | 主 Agent |
| Content/Script/Gameplay/Sekiro/AbilitySystem/CharacterAttributes.lua | 角色配置选择、表契约和初始化结构转换，无角色数值 | 主 Agent |
| Content/Script/Gameplay/Sekiro/Character/Attributes.lua、AI/Attributes.lua | 主角与 AI 各自完整的生命/战斗初值 | 主 Agent |
| Content/Script/Gameplay/Sekiro/Combat/CombatConfig.lua | 移除重复最大架势/恢复绝对值配置 | 主 Agent |
| Content/Script/Gameplay/Sekiro/Combat/SKCombatComponent.lua | 数值语义入口与初始化接入 | 主 Agent |
| Docs/plan、Docs/design 中本子任务及父任务 | 设计、依赖和进度同步 | 主 Agent |

不修改 Plugins、Weapon、Projectile、动画蓝图二进制资产；不自动创建新 Codex 任务。

## 8. 验证策略

UBT 编译 SekiroEditor，并检查 UHT 反射、Lua 语法/函数文档、源码 BOM、遗留 setter 引用。
补齐自动化测试代码覆盖初始化幂等、非法值拒绝、实际伤害/治疗、meta 清零、上限下降、Buff 撤销、架势变化；
默认只编译测试，不执行自动化、不启动 PIE、不模拟输入。

最终记录以实际工具输出为准；编译通过不能声称手感、死亡或完整对战已通过验收。

### 8.1 配置与调用方式

1. 主角修改 `Gameplay/Sekiro/Character/Attributes.lua`，通用 AI 修改 `Gameplay/Sekiro/AI/Attributes.lua`；每份文件都包含生命和完整战斗/躯干参数。
2. 新增敌人时复制一份完整属性 Lua 文件；例如创建 `Content/Script/Gameplay/Enemy/Attributes.lua` 后，在该角色的 `AttributeConfigModule` 填 `Gameplay.Enemy.Attributes`。不要填 `.lua` 扩展名或磁盘绝对路径。
3. 配置在该 ASC 首次初始化时生效；重复调用初始化不会恢复资源，也不能用重新初始化代替运行中的属性效果。
   修改 Lua 文件不自动重置已初始化角色；也不实现热重载或运行时重新读表。全局 `AttributeDefaults.lua` 已删除，不保留共享数值后备表。
4. 泛用 GAS Actor 若未使用当前 Survival Lua，必须自行显式初始化。未就绪时快照可能为零，不能将零快照当作有效初始值。

原生属性集已合并；如果已有蓝图或 GE 资产保存了旧属性类路径，需要重新选择 `USKCharacterAttributeSet` 上的对应属性再编译。本次仅迁移源码/脚本，不修改或宣称验证旧二进制资产。

```lua
local asc = character:GetSKAbilitySystemComponent()
-- 来源可为空；此入口接收已经完成命中与护甲裁决的最终伤害。
local result = asc:ApplyHealthDamage(25.0, attacker)
-- 读取 result.Code / RequestedAmount / ActualAmount / Before / After。
-- 这不是 TakeDamage 适配；角色资源提交会同步收敛 Survival 状态，演出由 Lua 另行编排。
local snapshot = asc:GetAttributeSnapshot()
```

公开即时入口为 ApplyHealthDamage、RestoreHealth、ApplyPostureDamage、RestorePosture、ResetPosture；
常规正量请求拒绝零/负/非有限值，ResetPosture 在已经为零时返回 NoChange。
常驻属性通知为 OnAttributeChanged，完整初始化通知为 OnAttributesReady，每笔即时请求完成通知为 OnNumericExecuted。
事件内不要同步提交另一笔数值修改；重入被拒绝，调用方应在当前通知返回后另行调度。

ASC 通过单一拥有者的 `ISKResourcePolicy` 检查资源请求；角色构造时声明需要策略，缺失或已卸载策略则失败关闭。
Survival 维护 Alive/Dying/Dead 等状态，阻止死亡后的普通治疗，提供受控回生/崩溃资源恢复；纯数值 Actor 可以不要求策略。
`ApplyResourceImpact` 在一次 GE 中提交生命/躯干两通道，再收敛最终状态；结果逐通道记录请求、实际值和拒绝原因，死亡优先于新崩溃。
内部 `RestoreOwnedResources` 不向 Lua/蓝图公开，校验绑定拥有者及当前流程授权，不提供忽略生命门禁的开关。
全部躯干数值与工作流迁移已经完成；实现和编译记录见 [生存组件进度](../plan/survival-component.md)，未执行运行测试。

## 9. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 用户确认拆分并实现；固化 GAS 数值设计、配置接入和非目标 |
| 2026-08-26 | 按本方案完成实现及 5 组契约测试代码；UBT 编译、Lua 语法与文档检查通过，运行时行为尚未测试；验证详情见进度文档 |
| 2026-08-26 | 链接后续 USKVitalsComponent 设计：生命/躯干状态与 ASC 策略集成待实施，本数值层代码未因本轮设计改变 |
| 2026-08-26 | 用户确认 USKSurvivalComponent 名称，关联完整躯干 AttributeSet 参数和处理逻辑迁移设计；本轮不改已实现 GAS 代码 |
| 2026-08-26 | 随 Survival 实现扩展 23 个参数、资源策略、复合请求和内部恢复 GE；默认初始化与全部躯干公式迁至 Survival Lua |
| 2026-08-26 | 按用户要求拆分主角/AI 专属 Lua 表，每份含 Health+Combat 全部初值；添加模块选择与严格字段校验，移除角色 Profile 优先路径和共享 AttributeDefaults |
| 2026-08-26 | 按用户要求统一为 USKCharacterAttributeSet；主角/AI 文件改为平铺 Character 表，移除 Health/Combat 分组，参数值不变 |
