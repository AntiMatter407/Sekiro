# Boss AI 系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗、精确节奏

## 概述

Boss AI（Boss AI）系统管理 Boss 敌人的战斗行为——行为状态机、攻击模式选择、阶段切换和目标管理。Boss AI 是战斗体验的"对手方"：它需要给玩家压力，同时也要制造可被利用的空隙。第一阶段实现单 Boss 对手，支持多阶段。

## 玩家幻想

Boss 不是属性海绵——它是会学习、会应对的对手。攻击模式有节奏感，连段之间有明确间隙。阶段切换时招式更新，迫使玩家重新适应。击败 Boss 不是靠数值碾压，而是靠读懂它的每一个动画、找到一个又一个可以反击的瞬间。

## 详细设计

### 核心规则

1. Boss 由行为状态机（Behavior State Machine）驱动
2. 攻击模式选择基于权重评分系统（距离、角度、HP 阶段、冷却）
3. Boss 支持多阶段（Phase），阶段间由忍杀触发切换
4. 架势崩坏触发忍杀条件
5. Boss 可发出危字攻击并通知危字系统

### 行为状态机（SKBossState）

```
[Idle]
  ├── 无目标 → 保持 Idle
  ├── 发现目标 → EnterCombat
  └── [Combat]
      ├── [Neutral] ← 默认状态，计算距离和策略
      │   ├── 距离远 (>FarThreshold) → 逼近/冲锋
      │   ├── 距离近 (<CloseThreshold) → 攻击/后退
      │   └── 距离适中 → 攻击/防御/侧移
      ├── [Attack] → 执行选定攻击模式
      │   ├── 攻击完成 → Neutral
      │   ├── 被弹刀 → 弹刀硬直(0.15s) → Neutral
      │   ├── 被识破 → 识破硬直(0.3s) → Neutral
      │   └── 被踩踏 → 踩踏硬直(0.25s) → Neutral
      ├── [Defend] → 弹刀玩家攻击
      │   ├── 弹刀成功 → Neutral
      │   └── 被命中 → 受击硬直 → Neutral
      ├── [HitStun] → 受击中
      │   ├── PostureBroken → Stagger
      │   └── 硬直结束 → Neutral
      ├── [Stagger] → 架势崩坏（0.5s）
      │   └── 可忍杀提示出现
      ├── [PhaseTransition] → 阶段切换演出
      │   └── 演出结束 → Neutral（新阶段行为）
      └── [Death] → 死亡/最终忍杀演出

  [Idle] → [Combat] 条件：玩家进入检测范围（AggroRange）
  [Combat] → [Idle] 条件：玩家进入倒地/死亡状态
```

### 攻击模式选择

Boss 的攻击模式由 FSKBossAttackPattern 数据定义：

```
FSKBossAttackPattern（Boss 攻击模式）：
  PatternID: AttackPattern_HeavySwing3
  Phase: 1
  MinRange: 0
  MaxRange: 400
  Cooldown: 3.0s（两次使用之间的最小间隔）
  Weight: 1.0（基础权重）
  WeightModifiers:
    - 玩家架势>50%: +0.3
    - 玩家HP<30%: +0.2
    - Boss HP<30%: -0.5
    - 连续使用相同模式: -0.5
```

选择算法：

```
1. 收集当前 Phase 可用的所有 AttackPattern
2. 过滤：
   ├── 距离检查（MinRange ≤ 距离 ≤ MaxRange）
   └── 冷却检查（距上次使用 ≥ Cooldown）
3. 计算每个模式的有效权重：
   EffectiveWeight = BaseWeight + Σ(Modifiers)
4. 按有效权重随机选择（加权随机）
5. 标记选中模式的冷却计时器
```

### Boss 阶段管理

```
PhaseTransition:
  ├── 触发条件：DeathblowCount > 0 且玩家完成忍杀
  ├── 执行：
  │   ├── Boss.HP = MaxHP
  │   ├── Boss.Posture = 0
  │   ├── Boss.Phase += 1
  │   ├── 加载新阶段的攻击模式列表
  │   ├── 播放阶段切换动画
  │   └── 可选的 Boss 属性调整（攻击力、速度等）
  └── DeathblowCount == 0：触发 Death 状态
```

### Boss 数据配置（FSKBossData）

| 字段 | 类型 | 说明 |
|------|------|------|
| BossID | FName | Boss 唯一标识 |
| MaxHP | float | 最大 HP |
| MaxPosture | float | 最大架势值 |
| DeathblowCount | int32 | 需要忍杀次数 |
| AttackPatterns | TArray | 各阶段攻击模式列表 |
| PhaseHPThresholds | TArray\<float\> | HP 阈值触发行为变更（可选） |
| AggroRange | float | 进入战斗的距离 |
| FarThreshold | float | 远距离阈值（用于逼近行为） |
| CloseThreshold | float | 近距离阈值（用于后撤行为） |
| MoveSpeed | float | 基础移动速度 |
| DeflectChance | float | 弹刀概率（0.0-1.0） |
| DeflectWindow | float | 弹刀窗口（秒） |
| PostureRecoveryRate | float | 架势恢复速率 |
| PerilousPatterns | TArray\<FName\> | 危字攻击模式 ID 列表 |

### Boss 防御行为

Boss 不持续防御——在 Neutral 状态根据 DeflectChance 概率弹刀玩家的攻击：

```
当玩家攻击命中判定到达 Boss：
  随机 Roll = Random(0, 1)
  如果 Roll < DeflectChance：
    Boss 弹刀成功 → 播放弹刀动画 → 短暂后撤 → Neutral
  否则：
    Boss 受击 → 受击硬直 → Neutral
```

注意：Boss 的 DeflectChance 可随阶段增加（Phase 1: 0.3, Phase 2: 0.5）

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| HP & 架势 | 输入 HP/Posture | Boss 状态监控 |
| 忍杀 | 输出忍杀条件 | PostureBroken→忍杀 |
| 危字系统 | 输出危字类型 | 攻击模式标记为危字 |
| 动画系统 | 输出攻击 Montage | Boss 攻击动画 |
| 碰撞检测 | 输出攻击盒 | Boss 攻击命中判定 |
| 伤害计算 | 输出伤害 | 从 FSKAttackData 获取 |
| 战斗 HUD | 输出 Boss HP/Posture | HUD 更新 |
| 数据配置 | 依赖 FSKBossData | Boss 参数 |

## 公式

### 攻击模式权重计算

```
EffectiveWeight = BaseWeight + Σ(Modifier_i)
SelectedPattern = WeightedRandom(AvailablePatterns, EffectiveWeight)
```

示例：HeavySwing3 基础权重 1.0，玩家架势 > 50%：+0.3，玩家 HP < 30%：+0.2 → 有效权重 1.5

### Boss 架势恢复

```
PostureRecoveryPerSec = PostureRecoveryRate * Curve(Boss.HP%)
// Boss HP 越低，架势恢复越慢
```

| 变量 | 典型值 |
|------|--------|
| PostureRecoveryRate | 15/秒 |
| DeflectChance (Phase 1) | 0.3 |
| DeflectChance (Phase 2) | 0.5 |
| FarThreshold | 600 UE 单位 |
| CloseThreshold | 200 UE 单位 |

### Boss 架势恢复 HP 曲线

```
HP% 100  → 1.0×
HP% 75   → 0.9×
HP% 50   → 0.7×
HP% 25   → 0.4×
HP% 0    → 0.0×
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| 玩家远离 Boss 超出 AggroRange | Boss 回到 Idle，HP 和 Posture 缓慢恢复 | 脱战恢复机制 |
| Boss 所有攻击模式都在冷却 | Boss 执行 Neutral 行为（逼近/后撤/弹刀）直到有可用攻击 | 不应有 AI 死锁 |
| 阶段切换中玩家攻击 | 阶段切换动画期间 Boss 无敌 | 演出完整性 |
| 两个攻击模式权重相同 | 距离更近的模式优先，距离相同则随机 | 打破平局 |
| Boss HP=0 但未被忍杀 | 保持可忍杀状态直到忍杀执行，不自动死亡 | 忍杀是必须动作 |
| Boss 攻击中被打入 PostureBroken | 攻击中断，进入 Stagger 状态 | 架势崩坏优先于攻击 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| HP & 架势 | 依赖 HP/架势 | Boss 状态 |
| 忍杀系统 | 输出忍杀条件 | PostureBroken |
| 危字系统 | 输出危字类型 | 危字攻击标记 |
| 动画系统 | 依赖动画 | Boss 动画 Montage |
| 碰撞检测 | 输出攻击盒 | Boss 命中 |
| 伤害计算 | 输出伤害 | Boss 攻击伤害 |
| 战斗 HUD | 输出 Boss 状态 | HP/Posture 条 |
| 数据配置 | 依赖 FSKBossData | Boss 参数 |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| AggroRange | 2000 | 1500-3000 | 更远进入战斗 | 必须更近 |
| DeflectChance | 0.3 | 0.1-0.7 | Boss 弹刀更频繁 | Boss 更易命中 |
| FarThreshold | 600 | 400-800 | Boss 更早逼近 | Boss 更晚逼近 |
| CloseThreshold | 200 | 100-300 | Boss 更早后撤 | Boss 更接近 |
| PostureRecoveryRate | 15/s | 10-25/s | Boss 架势恢复更快 | 架势压力更持久 |
| AttackCooldownMin | 1.5s | 1.0-3.0 | 两次攻击间隔更长 | 攻击更频繁 |

## 验收标准

- [ ] Boss 进入 AggroRange 后从 Idle 切换到 Combat
- [ ] Boss 攻击模式按权重随机选择
- [ ] 相同攻击模式有冷却，不会连续重复使用
- [ ] Boss 被弹刀后进入短暂硬直（0.15s）
- [ ] Boss 架势崩坏后进入 Stagger 状态（0.5s）
- [ ] Boss 被识破/踩踏后进入对应硬直
- [ ] 阶段切换正确：HP 回满、架势清空、新攻击模式解锁
- [ ] DeathblowCount=0 时 Boss 进入 Death 状态
- [ ] Boss 脱战后 HP 和架势缓慢恢复
