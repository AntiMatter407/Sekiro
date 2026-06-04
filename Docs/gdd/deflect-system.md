# 弹刀系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 刀刃对抗、精确节奏

## 概述

弹刀（Deflect/Parry）是只狼战斗的核心交互机制——在敌人攻击命中前的精确时间窗口内按下防御键，将敌人的攻击弹开并对其架势造成伤害。与普通格挡（Block）不同：弹刀成功时自己不受伤势伤害，对方的架势大量增加；格挡仅是减少伤害并承受架势压力。这是区分"会玩"和"不会玩"只狼的核心分水岭。

## 玩家幻想

在刀刃即将触及的瞬间按下防御——火花四溅，敌人武器被弹开，玩家毫发无损且信心倍增。每一次成功的弹刀都是对敌人节奏的完美回应。连续弹刀的叮当声是战斗中最令人满足的音频反馈。

## 详细设计

### 核心规则

1. **防御优先**：按下 IA_Deflect 时，角色立即进入防御姿态
2. **弹刀 vs 格挡判定**：
   - 如果防御键在弹刀窗口内按下（且之前未持续按住）= **弹刀**
   - 如果持续按住防御键 = **格挡**
   - 如果在弹刀窗口外按下 = **格挡**
3. 弹刀窗口 = 攻击命中前 N 帧（N = DeflectWindowFrames，典型 12 帧 / 0.2s）
4. 弹刀窗口由 AnimNotify 在攻击动画中标记——`ANSK_DeflectWindowStart` 到 `ANSK_DeflectWindowEnd`
5. 弹刀成功后：攻击方被短暂弹开（Micro-Stagger），防御方可以立即反击
6. 弹刀火花特效和音效在弹刀碰撞点播放
7. **连续弹刀**：每次成功弹刀重置弹刀窗口计时（后续攻击的弹刀窗口可以略微放宽）

### 判定流程

```
1. 攻击方 攻击盒激活 → 攻击盒开始每帧查询碰撞
2. 防御方 在弹刀窗口内按下 IA_Deflect
3. 攻击盒命中防御方 Hurtbox
4. 检查：防御方是否处于弹刀状态？
   ├── YES → 弹刀成功
   │         ├── 防御方 Posture += DeflectPostureDamageSelf（通常 0）
   │         ├── 攻击方 Posture += DeflectPostureDamage
   │         ├── 攻击方触发 Micro-Stagger（0.15s）
   │         ├── 播放 DeflectSparkVFX + DeflectSparkSFX
   │         └── 防御方 攻击 CancelWindow 立即开启（允许立即反击）
   └── NO（格挡状态）
             ├── 防御方 Posture += BlockPostureDamageSelf
             ├── 防御方 HP += HPDamage * BlockHPDamageRatio
             ├── 播放 BlockSFX（沉闷的格挡声）
             └── 防御方 攻击 CancelWindow 延迟开启
```

### 弹刀状态判定

```
bDeflect = IsDeflectKeyPressedInWindow()   // 在弹刀窗口内按下
           && !bWasHoldingDeflect           // 不是持续按住（去抖）
           && CanDeflect()                  // 可弹刀状态检查
```

`CanDeflect()` 返回 false 的条件：
- 正在播放受击/忍杀/死亡动画
- 正在使用物品
- 处于 PostureBroken 硬直中
- 在空中

### Micro-Stagger

弹刀成功后攻击方进入短暂硬直——不是 PostureBroken，但阻止立即发动下一次攻击。

| 属性 | 值 |
|------|-----|
| 持续时间 | 0.15s |
| 是否可被防御 | 否 |
| 是否可被闪避 | 否 |
| 视觉表现 | 武器被弹开的物理动画 |

### 连续弹刀

```
每弹刀成功次数 → 下次弹刀窗口扩大：
第1次弹刀: DeflectWindowFrames × 1.0
第2次弹刀: DeflectWindowFrames × 1.1
第3次+弹刀: DeflectWindowFrames × 1.2
最多扩大: +20%
```

这给连续弹刀的玩家一点点心理奖励——节奏对了就会越来越顺。

### 弹刀失败保护

如果不在弹刀窗口，且防御键未按住 → 直接受击（无格挡减轻）。这惩罚了"连点"防御键的行为。

```
弹刀尝试失败（连点） = 在窗口外按下 + 立刻松开 + 窗口内未按住
→ 判定为 Fail → 攻击盒正常命中 → 全额 HP + 架势伤害
```

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 输入系统 | 输入 IA_Deflect（含按下时间戳） | 弹刀窗口判定 |
| 碰撞检测 | 输入攻击盒命中结果 + DeflectCheck | 弹刀碰撞检测 |
| HP & 架势 | 输出架势伤害值 | 弹刀成功 → 攻击方架势增加 |
| 动画系统 | 输出弹刀结果 → 触发弹刀动画 | 防御方弹刀动画 |
| 伤害计算 | 输出格挡/弹刀的减伤倍率 | 弹刀成功 = 0 HP伤害 |
| 战斗音效 | 输出弹刀/格挡事件 | 火花声 / 沉闷格挡声 |
| 战斗特效 | 输出弹刀/格挡事件 | 火花粒子 |

## 公式

### 弹刀窗口判定

```
bInDeflectWindow = CurrentFrame >= DeflectWindowStart && CurrentFrame <= DeflectWindowEnd
bDeflect = bInDeflectWindow && InputPressed(IA_Deflect) - LastDeflectReleaseTime > DebounceTime
DebounceTime = 0.1s  // 防止连点被误判为弹刀
```

### 架势伤害（弹刀成功）

```
攻击方PostureDamage = DeflectPostureDamage * ConsecutiveDeflectMultiplier
防御方PostureDamage = DeflectPostureDamageSelf（通常为 0）
```

### 连续弹刀倍率

```
ConsecutiveDeflectMultiplier = 1.0 + Clamp(ConsecutiveCount * 0.1, 0, 0.2)
```

**预期输出范围**：1.0× 至 1.2× DeflectPostureDamage

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| 防御键连点（高频 Tap） | 连点期间不触发弹刀——DebounceTime 0.1s 内重复按下视为无效弹刀，回退到格挡 | 防止靠连点混过弹刀 |
| 弹刀窗口内按住不放 | 判定为格挡而非弹刀（因为不是"按下"事件而是"按住"状态） | 弹刀需要精确的时机动作 |
| 两个敌人同时攻击 | 对每个攻击独立判定弹刀窗口；同时命中时取最先触发的弹刀 | 弹刀只能挡一个方向 |
| 弹刀+闪避同时 | 弹刀优先（按键优先级） | 防御优先于闪避 |
| 背后攻击 | 不可弹刀——CanDeflect() 检查攻击方向与角色朝向是否在 ±120° 内 | 背后应无防御能力 |
| 危字•突刺 | 不可弹刀，仅识破可应对 | 只狼原版规则 |
| 危字•横扫 | 不可弹刀，仅踩踏/跳跃可应对 | 同上 |
| Micro-Stagger 期间被再次弹刀 | Micro-Stagger 强制播放完，不会被二次弹刀打断 | 防止弹刀连发崩坏动画 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 输入系统 | 弹刀依赖输入 | IA_Deflect 按键时间和缓冲查询 |
| 碰撞检测 | 弹刀依赖碰撞 | 攻击盒命中结果 + 弹刀检测 |
| HP & 架势 | 弹刀输出→架势 | 弹刀成功 → 架势伤害 |
| 动画系统 | 弹刀输出→动画 | 弹刀/格挡动画触发 |
| 伤害计算 | 弹刀输出→伤害 | 弹刀减免信息 |
| 战斗音效 | 弹刀输出→音效 | 火花声/格挡声 |
| 战斗特效 | 弹刀输出→特效 | 火花粒子 |
| 数据配置 | 弹刀依赖数据配置 | DeflectWindowFrames, DebounceTime 等 |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| DeflectWindowFrames | 12 (0.2s@60fps) | 6-30 | 弹刀更难失败 | 弹刀更严格（接近写实） |
| DebounceTime | 0.1s | 0.05-0.2 | 更长的连点惩罚 | 允许更快重复按键 |
| DeflectPostureDamage | 20 | 10-40 | 弹刀更强力 | 弹刀只是防御手段 |
| MicroStaggerDuration | 0.15s | 0.1-0.25 | 敌人被弹开更久 | 敌人更快恢复 |
| ConsecutiveDeflectBonus | +0.1/次 (max 0.2) | 0-0.3 | 连续弹刀奖励更大 | 更少的连续奖励 |
| BackDeflectAngle | 120° | 90°-150° | 更大角度可弹刀 | 更窄的防御面 |

## 验收标准

- [ ] 弹刀窗口内按下防御 → 弹刀成功（火花特效 + 架势伤害 + 无 HP 伤害）
- [ ] 弹刀窗口外按下 → 普通格挡（架势伤害 + 可能的 HP 穿透）
- [ ] 连点防御键 → 弹刀判定失败，回退到格挡或受击
- [ ] 背后攻击不可弹刀——CanDeflect() 方向检查通过
- [ ] 弹刀成功后攻击方触发 Micro-Stagger（0.15s 无法行动）
- [ ] 连续弹刀后弹刀窗口略微扩大（+10~20%）
- [ ] 危字•突刺和危字•横扫不可弹刀
- [ ] 弹刀火花 VFX 和 SFX 在碰撞点正确触发
- [ ] 弹刀后防御方 CancelWindow 立即开启——可以立刻反击
