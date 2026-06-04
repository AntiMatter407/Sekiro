# HP & 架势系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗、死亡与回生

## 概述

HP 和架势（Posture）是只狼战斗的两个核心数值槽。HP 代表角色的生命值——归零则死亡。架势代表姿态平衡——架势槽满则姿态崩坏，露出忍杀破绽。两者相互关联：HP 越低，架势恢复越慢。此系统管理两个槽的增减、崩坏判定和自动恢复。

## 玩家幻想

架势槽是只狼战斗的"脉搏"——每一次弹刀都在积累对方的压力，每一次失误都在增加自己的风险。当对方架势槽满的瞬间，玩家应该感受到那种"就是现在！"的紧迫与满足。而自己的架势槽逼近满值时，每一步都走在刀锋上。

## 详细设计

### 核心规则

1. 每个角色拥有 HP 和 Posture 两个属性
2. HP 归零 → 角色进入死亡状态（玩家角色触发回生机会，敌人直接死亡或进入忍杀）
3. 架势槽满 → Posture Break（姿态崩坏）→ 短暂硬直 → 玩家可执行忍杀（对敌人）或陷入危险（对玩家）
4. 架势自动恢复：未受击且未防御时，架势以 PostureRecoveryRate 每秒恢复
5. **HP-架势恢复关联**：HP 百分比越低，架势恢复速率越低（通过 PostureRecoveryScale_HP 曲线控制）
6. 防御/格挡时架势不恢复
7. 架势崩坏后：架势槽清空并短暂暂停恢复

### 状态机

```
Normal
├── 受击 → HP减少/Posture增加
├── 弹刀成功 → Posture增加（自己极少量，对方大量）
├── 格挡 → Posture增加（自己）
├── 未受击 → Posture自动恢复（按 PostureRecoveryRate）
├── Posture满 → PostureBroken
└── HP=0 → Dead

PostureBroken
├──→ 硬直动画（0.5s）
├──→ 敌方可执行忍杀
├──→ 硬直结束后 Posture 清空
└──→ 0.3s 后恢复 Normal 状态

Dead
├──→ 玩家：触发回生机会窗口
└──→ 敌人：进入可忍杀状态
```

### HP 减少来源

| 来源 | 说明 |
|------|------|
| 直接命中 | 未防御时被攻击 → 全额 HP 伤害 |
| 格挡穿透 | 防御但非弹刀时 → BlockHPDamageRatio × HP 伤害 |
| 投技 | 无视防御 → 高额 HP 伤害 |
| 跌落 | 从高处坠落 → 按高度计算 |

### 架势增加来源

| 来源 | 说明 |
|------|------|
| 防御（非弹刀） | 格挡攻击 → BlockPostureDamageSelf |
| 弹刀成功 | 弹刀对方 → 自身 DeflectPostureDamageSelf（通常为0） |
| 弹刀失败（被命中） | 被攻击 → 全额架势伤害 |
| 危字攻击被命中 | 高额架势伤害 |

### 架势减少

| 来源 | 说明 |
|------|------|
| 自动恢复 | 未受击 × RecoveryDelay 后每秒恢复 |
| HP低惩罚 | HP%下降 → 恢复速率乘数下降 |

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 数据配置 | 读取 MaxHP/MaxPosture/RecoveryRate | 初始化和参数 |
| 弹刀系统 | 输入弹刀结果 → 架势增减 | 弹刀成功对敌架势伤害 |
| 伤害计算 | 输入伤害值 → HP/架势减少 | 各来源伤害汇总 |
| 忍杀 | 输出 PostureBroken | 架势崩坏触发忍杀 |
| 回生 | 输出 Death → 触发回生 | HP=0 触发回生窗口 |
| 伤药葫芦 | 输入治疗 → HP 恢复 | 喝药回血 |
| 战斗 HUD | 输出 HP/Posture 当前值 | HUD 显示双槽 |
| Boss AI | 输出 HP 百分比 | Boss 阶段转换判断 |

## 公式

### HP 变化

```
NewHP = Clamp(CurrentHP - HPDamage * DamageMultiplier + HealAmount, 0, MaxHP)
```

### 架势变化

```
NewPosture = Clamp(CurrentPosture + PostureDamage - PostureRecovery * DeltaTime, 0, MaxPosture)
```

### 架势恢复速率

```
实际恢复速率 = PostureRecoveryRate * PostureRecoveryScale_HP(CurrentHP / MaxHP)
```

| HP% | 恢复倍率（典型曲线） |
|-----|-------------------|
| 100% | 1.0× |
| 75% | 0.9× |
| 50% | 0.7× |
| 25% | 0.4× |
| 0% | 0.1× |

**预期输出范围**：0.1× PostureRecoveryRate 至 1.0× PostureRecoveryRate

### 架势崩坏判定

```
bPostureBroken = CurrentPosture >= MaxPosture && bAlive
```

### PostureBroken 硬直时间

```
StunDuration = 0.5s（基础值，可通过数据配置调整）
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| HP=0 且 Posture 同时满 | 死亡优先——先触发死亡/回生，架势崩坏不触发 | 死人不应该还能被忍杀 |
| 伤害超过剩余 HP | HP Clamp 到 0，不产生负值 | 防止逻辑错误 |
| 架势伤害超过 MaxPosture | Posture Clamp 到 MaxPosture，触发崩坏 | 不应溢出 |
| 回生后 HP | 恢复到配置的 ResurrectionHP 百分比，架势恢复到 ResurrectionPosture 百分比 | 回生不是满血复活 |
| 恢复延迟内的受击 | 重置恢复计时器（RecoveryDelay 重新计数） | 被击中应延迟恢复 |
| 格挡中架势恢复 | 格挡期间架势不恢复（恢复速率为 0） | 举刀防御时不应恢复 |
| 架势崩坏后立即受击 | 崩坏硬直期间 Posture 已清空，后续受击从 0 开始积累 | 软重置 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 数据配置 | 依赖数据配置 | MaxHP、MaxPosture、恢复参数、曲线 |
| 伤害计算 | 依赖伤害 | 接收 HP 和架势伤害 |
| 弹刀系统 | 依赖弹刀 | 弹刀成功对敌架势伤害值 |
| 忍杀 | 依赖 HP/架势 | PostureBroken → 触发忍杀 |
| 回生系统 | 依赖 HP/架势 | HP=0 → 触发回生 |
| 伤药葫芦 | 依赖 HP | HealAmount → 恢复 HP |
| Boss AI | 依赖 HP/架势 | HP% 用于阶段转换 |
| 战斗 HUD | 输出值 | HP 和 Posture 当前值 |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| MaxHP (玩家) | 100 | 80-200 | 更耐打 | 更脆弱 |
| MaxPosture (玩家) | 100 | 80-200 | 架势更难崩 | 架势更易崩 |
| PostureRecoveryRate (玩家) | 30/s | 10-60 | 恢复更快 → 更简单 | 恢复更慢 → 更难 |
| PostureRecoveryDelay | 1.5s | 0.5-3.0 | 脱离战斗后更久才恢复 | 更快恢复 |
| MaxPosture (Boss) | 150 | 100-300 | Boss 架势更难崩 | Boss 更易崩 |
| PostureBrokenStunDuration | 0.5s | 0.3-1.0 | 崩坏硬直更长 | 更快可追击 |
| HP-DrainRatio | 随 HP% 下降曲线 | — | 战斗节奏更快 | 更鼓励防御 |

## 验收标准

- [ ] HP 归零时角色正确进入死亡状态
- [ ] 架势归零时角色正确进入 PostureBroken 状态（硬直 + 可被忍杀）
- [ ] 架势自动恢复在未受击 × RecoveryDelay 后正确启动
- [ ] HP 50% 时架势恢复速率明显慢于 HP 100% 时（通过日志验证）
- [ ] 格挡时架势不恢复
- [ ] 架势崩坏硬直结束后 Posture 清空为 0
- [ ] 回生后 HP 和 Posture 恢复到配置的百分比值
- [ ] HP 和 Posture 值永远不会超出 [0, Max] 范围
- [ ] Boss HP 下降至阶段阈值时，Boss AI 正确收到阶段转换信号
