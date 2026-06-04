# 伤害计算系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗

## 概述

伤害计算系统是所有战斗伤害的中央处理节点。它接收碰撞检测系统输出的 FSKHitResult，查询数据配置中的攻击参数，应用弹刀/格挡/防御的减免规则，最终计算 HP 伤害和架势伤害，并将结果输出给 HP & 架势系统。它不执行碰撞，不做动画反馈——只是一个纯逻辑计算层。

## 玩家幻想

伤害计算本身没有直接的玩家幻想。它的正确性体现在：弹刀 = 无伤，格挡 = 架势压力，裸吃 = 巨痛——这三种结果的差异让玩家感受到弹刀的价值。

## 详细设计

### 核心规则

1. 接收 FSKHitResult → 查询 FSKAttackData（通过 AttackID）
2. 按优先级应用伤害修正：
   - 优先：弹刀成功 → HP伤害=0，架势伤害=弹刀公式
   - 其次：格挡 → HP伤害 = HPDamage × BlockHPDamageRatio
   - 再次：识破/踩踏成功 → HP伤害=0，架势伤害=特殊公式
   - 默认：直接命中 → 全额 HP伤害 + 架势伤害
3. 输出结构：FSKDamageResult（HP伤害值、架势伤害值、受击类型）
4. 计算结果传递给 HP & 架势系统执行

### 伤害修正链

```
原始伤害（来自 FSKAttackData）
→ 弹刀减免（如适用）
→ 格挡减免（如适用）
→ 防御姿态减免（如适用）
→ 伤害类型修正（物理/火焰/等——第一阶段全物理）
→ 最终 HP伤害 + 架势伤害
```

### FSKDamageResult

| 字段 | 类型 | 说明 |
|------|------|------|
| HPDamage | float | 最终 HP 伤害 |
| PostureDamage | float | 最终架势伤害 |
| HitReaction | ESKHitReaction | 轻/重/击飞/崩坏 |
| AttackDirection | FVector | 攻击方向（用于击退） |
| bIsDeflected | bool | 是否被弹刀 |
| bIsGuarded | bool | 是否被格挡 |
| bIsMikiri | bool | 是否被识破 |
| KnockbackForce | float | 击退力度 |

### 受击反应分级

| 级别 | 触发条件 | 表现 |
|------|---------|------|
| 轻 (Light) | 小伤害，架势<50% | 轻微硬直（0.1s），可立即弹刀 |
| 中 (Medium) | 中伤害，架势>50% | 中等硬直（0.2s），短暂后仰 |
| 重 (Heavy) | 大伤害/危字命中 | 长硬直（0.4s），大幅后仰 |
| 击飞 (Knockback) | 爆炸/特殊攻击 | 物理击退 + 倒地（第一阶段预留） |
| 崩坏 (Broken) | 架势满 → 由 HP&架势系统直接触发 | PostureBroken 硬直 |

### 伤害类型（第一阶段）

| 类型 | 说明 |
|------|------|
| Slash | 斩击——普攻主要伤害类型 |
| Thrust | 突刺——蓄力攻击/敌人突刺 |
| Sweep | 横扫——敌人横扫攻击 |
| Grab | 投技——无视防御 |

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 碰撞检测 | 输入 FSKHitResult | 命中原始数据 |
| 数据配置 | 查询 FSKAttackData | 攻击参数 |
| 弹刀系统 | 输入弹刀/格挡状态 | 减伤倍率 |
| 闪避/识破 | 输入识破/踩踏状态 | 特殊伤害公式 |
| HP & 架势 | 输出 FSKDamageResult | 最终伤害值 |

## 公式

### 默认伤害（无减免）

```
HPDamage = AttackData.HPDamage
PostureDamage = AttackData.PostureDamage
```

### 弹刀减免

```
HPDamage = 0
PostureDamage_Self = DeflectPostureDamageSelf
PostureDamage_Attacker = DeflectPostureDamage
```

### 格挡减免

```
HPDamage = AttackData.HPDamage * BlockHPDamageRatio
PostureDamage = AttackData.PostureDamage * BlockPostureRatio
```

### 识破伤害

```
HPDamage = 0
PostureDamage = AttackData.PostureDamage * MikiriPostureDamageMultiplier (1.5×)
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| AttackID 未找到 | 使用默认攻击参数（DefaultAttackData），输出 Error | 不崩溃，但开发者可见的配置缺失 |
| HP伤害为负（治疗技能） | Clamp 到 0（第一阶段没有治疗攻击） | 预留但禁用 |
| 同时弹刀+被命中 | 弹刀优先——判定在前，命中在后 | 弹刀窗口命中先于普通命中 |
| 0 倍率 | 伤害=0，但受击反应仍触发（轻级） | 应该有一个视觉反馈 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 碰撞检测 | 依赖碰撞 | FSKHitResult |
| 数据配置 | 依赖数据配置 | FSKAttackData |
| 弹刀系统 | 依赖弹刀 | 弹刀/格挡状态 |
| 闪避/识破 | 依赖闪避 | 识破/踩踏状态 |
| HP & 架势 | 输出→HP/架势 | FSKDamageResult |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| BlockHPDamageRatio | 0.0 | 0.0-0.1 | 格挡穿透HP | 完美格挡无伤 |
| BlockPostureRatio | 0.7 | 0.5-1.0 | 格挡架势压力大 | 格挡更安全 |
| MikiriMultiplier | 1.5× | 1.2-2.0 | 识破更强 | 识破仅对策 |
| KnockbackScale | 1.0 | 0.5-1.5 | 击退更远 | 击退更近 |

## 验收标准

- [ ] 直接命中 → 全额 HP + 架势伤害
- [ ] 弹刀成功 → HP 伤害 = 0
- [ ] 格挡 → HP 伤害 = HPDamage × BlockHPDamageRatio
- [ ] 识破成功 → HP 伤害 = 0，架势伤害 = 基础 × 1.5
- [ ] AttackID 查找失败 → 使用 DefaultAttackData 且输出 Error 日志
- [ ] 伤害值永不为负
