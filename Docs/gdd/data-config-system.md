# 数据配置系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 全部（所有战斗数值的可配置来源）

## 概述

数据配置系统是所有战斗数值的单一来源（Single Source of Truth）。它定义游戏中每个可配置参数的类型、存储方式和访问接口——包括角色属性、攻击数据、架势值、弹刀窗口、回生次数等。所有其他战斗系统通过此系统获取数值，禁止在任何游戏逻辑中硬编码数值。

## 玩家幻想

数据配置系统没有直接的玩家幻想。它的存在是为了支撑设计师和程序员的协作：设计师可以通过修改 DataAsset/DataTable 快速迭代数值平衡，无需触碰 C++ 代码。

## 详细设计

### 核心规则

1. **所有战斗数值必须来自数据资产**，禁止在 C++ 或 Blueprint 逻辑中硬编码
2. 使用 UE 原生数据容器：`UDataAsset`（单体配置）、`UDataTable`（批量行数据）、`UCurveFloat`（连续曲线）
3. 数据资产存放于 `Content/Data/` 目录，按系统分包
4. 数据资产在运行时通过 `UPROPERTY` + `TSoftObjectPtr` 异步加载，或通过 GameInstance Subsystem 集中管理引用
5. 蓝图可读取数据资产用于 UI 显示，但游戏逻辑数据流转由 C++ 层完成

### 数据资产类型定义

#### 角色属性 (FSKCharacterStats)

| 字段 | 类型 | 说明 |
|------|------|------|
| MaxHP | float | 最大生命值 |
| MaxPosture | float | 最大架势值 |
| PostureRecoveryRate | float | 每秒架势恢复量 |
| PostureRecoveryDelay | float | 不受击后开始恢复的延迟（秒） |
| PostureRecoveryScale_HP | UCurveFloat | HP百分比→架势恢复速率乘数 |
| WalkSpeed | float | 移动速度 |
| RunSpeed | float | 奔跑速度 |
| JumpHeight | float | 跳跃高度 |
| LockOnRange | float | 锁定最大距离 |

#### 攻击参数 (FSKAttackData)

| 字段 | 类型 | 说明 |
|------|------|------|
| AttackID | FName | 唯一标识 |
| HPDamage | float | 对 HP 的基础伤害 |
| PostureDamage | float | 对架势的基础伤害 |
| PostureDamage_Self | float | 对自己架势的伤害（被弹反时） |
| HitboxSize | FVector | 攻击盒尺寸 |
| HitboxOffset | FVector | 攻击盒相对角色偏移 |
| StartupFrames | int | 启动帧数（60fps 基准） |
| ActiveFrames | int | 判定持续帧数 |
| RecoveryFrames | int | 收招帧数 |
| CancelWindow | TArray\<FName\> | 可取消到的动作列表 |
| IsPerilous | bool | 是否有危字属性 |
| PerilousType | ESKPerilousType | 突刺/横扫/投技 |
| KnockbackDistance | float | 击退距离 |

#### 弹刀参数 (FSKDeflectConfig)

| 字段 | 类型 | 说明 |
|------|------|------|
| DeflectWindowFrames | int | 弹刀判定窗口（帧数） |
| DeflectPostureDamage | float | 弹刀成功对敌人架势伤害 |
| DeflectPostureDamageSelf | float | 弹刀成功对自己架势伤害（应为0或极小） |
| BlockPostureDamageSelf | float | 普通格挡对自己架势伤害 |
| BlockHPDamageRatio | float | 格挡时穿透的 HP 伤害比例 |
| DeflectSparkVFX | TSoftObjectPtr\<UNiagaraSystem\> | 弹刀火花特效 |
| DeflectSparkSFX | TSoftObjectPtr\<USoundBase\> | 弹刀音效 |

#### Boss 属性 (FSKBossData)

| 字段 | 类型 | 说明 |
|------|------|------|
| BossID | FName | Boss 唯一标识 |
| MaxHP | float | 最大 HP |
| MaxPosture | float | 最大架势值 |
| DeathblowCount | int32 | 需要忍杀次数 |
| AttackPatterns | TArray\<FSKBossAttackPattern\> | 各阶段的攻击模式列表（通过 Pattern.Phase 区分阶段） |
| PhaseHPThresholds | TArray\<float\> | HP 百分比阈值触发行为变更（可选，用于 Phase 内细分） |
| AggroRange | float | 进入战斗的距离（UE 单位） |
| FarThreshold | float | 远距离阈值——超出则 Boss 逼近 |
| CloseThreshold | float | 近距离阈值——小于则 Boss 可能后撤 |
| MoveSpeed | float | 基础移动速度 |
| DeflectChance | float | Boss 弹刀概率（0.0-1.0，可随阶段变化） |
| DeflectWindow | float | Boss 弹刀窗口（秒） |
| PostureRecoveryRate | float | 架势恢复速率（/秒） |
| PerilousPatterns | TArray\<FName\> | 危字攻击模式 ID 列表 |
| AttackCooldownMin | float | 两次攻击最小间隔（秒） |
| AttackCooldownMax | float | 两次攻击最大间隔（秒） |

> **FSKBossAttackPattern**（攻击模式子结构体）：
> | 字段 | 类型 | 说明 |
> |------|------|------|
> | PatternID | FName | 攻击模式唯一标识 |
> | Phase | int32 | 所属阶段（1-based） |
> | MinRange | float | 有效最小距离 |
> | MaxRange | float | 有效最大距离 |
> | Cooldown | float | 两次使用之间的最小间隔（秒） |
> | BaseWeight | float | 基础选择权重 |
> | WeightModifiers | TMap\<FName, float\> | 条件权重修正（如"玩家架势>50%:+0.3"） |

#### 回生参数 (FSKResurrectionConfig)

| 字段 | 类型 | 说明 |
|------|------|------|
| MaxResurrections | int | 最大回生次数 |
| ResurrectionHPRatio | float | 回生后恢复的 HP 百分比（0.0-1.0） |
| ResurrectionPostureRatio | float | 回生后恢复的架势百分比（0.0-1.0） |
| DeathblowRequirement | int | 需要多少次忍杀才能补充回生节点 |

#### 伤药葫芦参数 (FSKHealingGourdConfig)

| 字段 | 类型 | 说明 |
|------|------|------|
| MaxCharges | int | 最大使用次数 |
| HealAmount | float | 每次回血量（固定值，非百分比） |
| HealDuration | float | 喝药动作总时长（秒） |
| HealWindow | float | 实际回血发生的相对时间点（秒） |
| HealCooldown | float | 两次喝药之间的最小间隔（秒） |

### 状态和转换

数据配置系统本身无状态——它是静态数据的容器。运行时读取后缓存在对应系统中。

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| HP & 架势 | 读取角色属性、弹刀参数 | 初始化 HP/架势值、恢复速率 |
| 弹刀系统 | 读取弹刀参数 | 判定窗口、架势伤害值 |
| 普攻连段 | 读取攻击参数 | 伤害、碰撞盒、帧数据 |
| 伤药葫芦 | 读取葫芦参数 | 回血量、次数限制 |
| 回生系统 | 读取回生参数 | 复活次数、恢复百分比 |
| Boss AI | 读取 Boss 属性 | 阶段配置、攻击列表 |
| 战斗 HUD | 读取角色属性 | 显示 HP/架势条上限 |

## 公式

数据配置系统本身不执行计算，它只是数值的容器。但以下公式引用其数据：

### 架势恢复速率

```
实际恢复速率 = PostureRecoveryRate * PostureRecoveryScale_HP(CurrentHP/MaxHP)
```

- 当 HP=100% 时，倍率为 1.0
- 当 HP 降低时，倍率下降（具体曲线在 UCurveFloat 中配置）
- 预期输出范围：0.1 × PostureRecoveryRate 至 1.0 × PostureRecoveryRate

### HP 伤害（格挡时）

```
格挡穿透伤害 = HPDamage * BlockHPDamageRatio
```

- `BlockHPDamageRatio` 通常为 0.0（完美格挡不穿透 HP）
- 预期输出范围：0 至 HPDamage × 0.3

### 弹刀架势伤害

```
弹刀架势伤害 = DeflectPostureDamage（直接取值，不缩放）
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| 数据资产加载失败 | 回退到代码中的默认值（DefaultValues），并输出 Error 日志 | 确保游戏不会崩溃，但开发者能发现配置缺失 |
| 数值为负数 | 攻击数据中的伤害/架势值在加载时 Clamp 到 ≥0；恢复速率 Clamp 到 ≥0 | 负伤害/负恢复没有游戏意义 |
| 数据表行缺失 | 通过 FName 查找失败时，使用该类型的 Fallback 行（每张表第一行标记为 Default），输出 Warning | 避免空指针崩溃 |
| 曲线资产未配置 | UCurveFloat 为空时，使用常量 1.0 代替（即无修正） | 曲线是可选优化项，不应阻塞基础功能 |
| 热更新数值 | 编辑器中修改 DataAsset 后立即生效（通过 UPROPERTY 的 Transient 标记控制） | 加速迭代 |
| 两个 DataAsset 引用同一曲线 | 允许共享——曲线是无状态的纯数学对象 | 减少重复资产 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| HP & 架势 | 依赖数据配置 | 需要 MaxHP、MaxPosture、恢复速率等 |
| 弹刀系统 | 依赖数据配置 | 需要弹刀窗口、架势伤害值 |
| 普攻连段 | 依赖数据配置 | 需要攻击参数（帧数据、伤害等） |
| 闪避/识破 | 依赖数据配置 | 需要判定窗口帧数 |
| 伤药葫芦 | 依赖数据配置 | 需要回血量、次数 |
| 回生系统 | 依赖数据配置 | 需要复活次数、恢复百分比 |
| Boss AI | 依赖数据配置 | 需要 Boss 属性、阶段/攻击配置 |
| 战斗 HUD | 依赖数据配置 | 需要 MaxHP、MaxPosture 用于显示上限 |

数据配置系统本身**不依赖**任何其他战斗系统。

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| DeflectWindowFrames | 12 (0.2s@60fps) | 6-30 | 弹刀更容易 → 战斗变简单 | 弹刀更难 → 接近只狼体验 |
| PostureRecoveryRate | 30/s | 10-60 | 架势恢复快 → 鼓励防御 | 架势恢复慢 → 鼓励进攻 |
| PostureRecoveryDelay | 1.5s | 0.5-3.0 | 脱离战斗后恢复慢 | 脱离后恢复快 |
| MaxResurrections | 1 | 0-3 | 更多容错 | 更接近原版难度 |
| HealAmount | 30 (固定值) | 20-50 | 每次喝药回更多 | 每次喝药回更少 |
| BlockHPDamageRatio | 0.0 | 0.0-0.1 | 格挡也掉血 → 更难 | 格挡完美免伤 |
| LockOnRange | 1500 (UE单位) | 800-3000 | 更远锁定 → 更安全 | 更近锁定 → 更紧张 |

## 验收标准

- [ ] 所有战斗数值（HP、架势、伤害、帧数据）均来自 DataAsset，搜索代码无硬编码数值
- [ ] 每个 DataAsset 类型有对应的 `FSK*` 结构体定义，字段完整且有注释
- [ ] 存在至少一个 DefaultDataAsset 作为 Fallback——当特定资产加载失败时不崩溃
- [ ] 修改 DataAsset 数值后，PIE（Play In Editor）中立即生效
- [ ] 数据资产可通过蓝图读取（用于 UI 绑定），C++ 层通过 `GetDefaultObject` 或 Subsystem 访问
- [ ] 所有 `TSoftObjectPtr` 引用的资产（VFX/SFX）支持异步加载
- [ ] 数据表支持 Fallback 行——查找失败时使用默认行并输出 Warning
