# 战斗音效系统

> **状态**: 草稿
> **最后更新**: 2026-05-30
> **支撑的支柱**: 精确节奏、刀刃对抗

## 概述

战斗音效（Battle SFX）系统负责战斗中所有音效的触发和管理——弹刀金属碰撞声、危字警示音、忍杀音效、受击音效、喝药音效、回生音效等。音效是战斗节奏的重要组成部分：弹刀的清脆金属声让玩家感知弹刀成功，危字的低沉嗡鸣给玩家压力。音效通过事件驱动的音频事件系统触发，不与任何具体实现耦合。

## 玩家幻想

弹刀时那一声清脆的"叮"——不只是一种听觉反馈，更是一种节奏锚点。当连续弹刀形成节奏时，声音本身就是战斗的节拍器。危字前的嗡鸣让手心出汗，忍杀的闷响让每次终结都沉重有力。

## 详细设计

### 核心规则

1. 音效通过事件系统触发——游戏系统发送音频事件，音效系统决定播放什么和如何播放
2. 每个音频事件携带位置信息（3D 空间化）或标记为 2D（UI 类音效）
3. 音效优先级管理：高优先级音效可中断低优先级音效
4. 同类型音效有最小重触发间隔（防止音效堆积）
5. 音量基于距离衰减（3D 音效）或恒定（2D 音效）

### 音效事件列表

| 事件 ID | 触发条件 | 类型 | 优先级 | 重触发间隔 |
|---------|---------|------|--------|-----------|
| SFX_Deflect_Success | 玩家弹刀成功 | 3D | High | 50ms |
| SFX_Deflect_Boss | Boss 弹刀玩家攻击 | 3D | High | 50ms |
| SFX_Guard | 玩家格挡（非弹刀） | 3D | Medium | 100ms |
| SFX_Attack_Hit | 玩家攻击命中敌人 | 3D | Medium | 80ms |
| SFX_Attack_BossHit | Boss 攻击命中玩家 | 3D | High | 80ms |
| SFX_Mikiri_Success | 识破成功 | 3D | High | 500ms |
| SFX_Stomp_Success | 踩踏成功 | 3D | High | 500ms |
| SFX_Deathblow_Execution | 忍杀演出开始 | 2D | Critical | 1s |
| SFX_Deathblow_Impact | 忍杀命中的那一击 | 3D | Critical | 1s |
| SFX_Perilous_Warning | 危字出现 | 2D | Critical | 3s |
| SFX_Perilous_Thrust | 突刺危字 | 3D | High | 3s |
| SFX_Perilous_Sweep | 横扫危字 | 3D | High | 3s |
| SFX_Perilous_Grab | 投技危字 | 3D | High | 3s |
| SFX_Healing_Gourd | 喝药 | 2D | Medium | 1s |
| SFX_Resurrection_Trigger | 回生动画开始 | 2D | Critical | 3s |
| SFX_Resurrection_Revive | 回生完成（角色站起） | 2D | Critical | 3s |
| SFX_Death_Player | 玩家真正死亡 | 2D | Critical | — |
| SFX_Posture_Broken | 架势崩坏（敌人或玩家） | 3D | High | 1s |
| SFX_Sword_Clash | 双方同时攻击/弹刀碰撞 | 3D | High | 50ms |
| SFX_Footstep | 移动脚步声 | 3D | Low | 300ms |
| SFX_Dodge | 闪避动作 | 3D | Medium | 200ms |
| SFX_Grapple_Launch | 钩绳发射 | 3D | Medium | 500ms |
| SFX_Grapple_Arrive | 钩绳到达 | 3D | Medium | 500ms |

### 音效参数化

每个音效事件支持参数化以增加变化性，避免听觉疲劳：

| 参数 | 说明 | 范围 |
|------|------|------|
| PitchShift | 随机音高偏移 | 0.95-1.05（±5%） |
| Volume | 音量倍数（基于优先级基础音量） | 0.8-1.0 |
| DistanceAttenuation | 距离衰减曲线 | 线性/对数/自定义曲线 |

### 音效优先级与并发管理

```
音效优先级（Priority）：
  Critical: 最多同时 2 个，可中断所有低优先级音效
  High:     最多同时 4 个，可中断 Medium 和 Low
  Medium:   最多同时 6 个，可中断 Low
  Low:      最多同时 8 个，不可中断其他音效

同类型重触发间隔：
  如果同一事件 ID 在间隔内再次触发 → 忽略新触发
  例外：SFX_Deflect_Success 和 SFX_Sword_Clash 允许 50ms 间隔（连续弹刀场景）
```

### 3D 空间化

| 参数 | 值 |
|------|-----|
| 衰减模型 | 对数衰减（Logarithmic） |
| 最大距离 | 3000 UE 单位 |
| 参考距离 | 200 UE 单位（此距离内音量 100%） |
| 空间化方法 | HRTF（头部相关传输函数）+ 立体声平移（Stereo Panning）备选 |

### 音效与动画同步

对于需要与动画精确同步的音效（如忍杀、回生），通过 AnimNotify 触发：

```
忍杀 Montage:
  [0.0s] AnimNotify_DeathblowStart → SFX_Deathblow_Execution（2D，全屏）
  [0.8s] AnimNotify_DeathblowImpact → SFX_Deathblow_Impact（3D，命中位置）
  [2.0s] 忍杀完成

回生 Montage:
  [0.0s] AnimNotify_ResurrectionStart → SFX_Resurrection_Trigger
  [1.5s] AnimNotify_ResurrectionRise → SFX_Resurrection_Revive
```

### 与其他系统的交互

| 交互系统 | 数据流向 | 说明 |
|----------|---------|------|
| 弹刀系统 | 输入 Deflect/Guard 事件 | 弹刀/格挡音效 |
| 碰撞检测 | 输入 Hit 事件 | 攻击命中音效 |
| 闪避/识破 | 输入 Mikiri/Stomp/Dodge 事件 | 识破/踩踏/闪避音效 |
| 忍杀系统 | 输入 Deathblow 事件 | 忍杀音效 |
| 危字系统 | 输入 Perilous 事件 | 危字警示音 |
| 伤药葫芦 | 输入 Heal 事件 | 喝药音效 |
| 回生系统 | 输入 Resurrection 事件 | 回生音效 |
| 移动系统 | 输入 Footstep 事件 | 脚步声 |
| 动画系统 | 输入 AnimNotify | 动画同步音效 |

## 公式

### 距离衰减

```
AttenuatedVolume = BaseVolume × Curve(Distance / ReferenceDistance)

Curve(t):
  t ≤ 1.0 → 1.0（参考距离内无衰减）
  1.0 < t ≤ MaxDistance/RefDistance → 对数衰减
  t > MaxDistance/RefDistance → 0（超出最大距离无声）
```

### 并发音效上限

```
ActiveSounds_ByPriority[P] ≤ MaxConcurrent[P]
如果超出 → 终止最早播放的该优先级音效
```

## 边界情况

| 场景 | 预期行为 | 理由 |
|------|---------|------|
| 连续弹刀（高频触发） | 允许 50ms 间隔内多次触发弹刀音效，不会丢失 | 高频弹刀是正常玩法 |
| 危字 + 弹刀音效同时 | 危字 (Critical) 不中断弹刀 (High)——不同优先级频道 | 两类信息都重要 |
| 忍杀中其他音效 | 忍杀期间非忍杀音效的音量降低到 20% | 忍杀是焦点时刻 |
| 大量 Hit 事件同时（AOE） | 最多同时播放 4 个命中音效，超出部分丢弃 | 避免音效混乱 |
| 音效资源缺失 | 静默跳过，输出 Warning 日志 | 不崩溃、不阻塞 |
| 暂停/慢动作 | 所有音效暂停/跟随时间膨胀 | 配合游戏暂停 |

## 依赖

| 系统 | 方向 | 依赖性质 |
|------|------|---------|
| 弹刀系统 | 依赖事件 | Deflect/Guard 音频事件 |
| 碰撞检测 | 依赖事件 | Hit 音频事件 |
| 闪避/识破 | 依赖事件 | Mikiri/Stomp/Dodge 音频事件 |
| 忍杀系统 | 依赖事件 | Deathblow 音频事件 |
| 危字系统 | 依赖事件 | Perilous 音频事件 |
| 伤药葫芦 | 依赖事件 | Heal 音频事件 |
| 回生系统 | 依赖事件 | Resurrection 音频事件 |
| 移动系统 | 依赖事件 | Footstep 音频事件 |
| 动画系统 | 依赖 AnimNotify | 动画同步音频事件 |

## 调优旋钮

| 参数 | 建议默认值 | 安全范围 | 增加的效果 | 减少的效果 |
|------|----------|---------|-----------|-----------|
| MaxDistance | 3000 | 2000-5000 | 更远可听到音效 | 更近 |
| ReferenceDistance | 200 | 100-400 | 更大无衰减区域 | 更早开始衰减 |
| PitchVariation | ±5% | ±0-10% | 更多音高变化 | 更一致的音高 |
| MaxConcurrent_Critical | 2 | 1-3 | 更多关键音效同时 | 更少 |
| MaxConcurrent_High | 4 | 2-6 | 更多高优先级音效 | 更少 |
| RetriggerInterval_Deflect | 50ms | 30-80ms | 更长的弹刀音效间隔 | 更密集的弹刀反馈 |

## 验收标准

- [ ] 弹刀成功时播放金属碰撞音效（3D，弹刀位置）
- [ ] 格挡与弹刀音效有可辨识的差异
- [ ] 危字提示出现时播放警示音效（2D）
- [ ] 忍杀演出期间忍杀音效正常触发（AnimNotify 同步）
- [ ] 识破成功播放特殊音效
- [ ] 同类型音效不会因高频触发而堆积
- [ ] 3D 音效有正确的距离衰减
- [ ] 音效资源缺失时静默跳过不崩溃
