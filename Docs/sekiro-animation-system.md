# 只狼 动画与状态切换系统解析

> 本文档详细分析原版《只狼：影逝二度》（Sekiro: Shadows Die Twice）根据输入和状态实现动画切换的完整逻辑体系，作为 UE5 迁移项目的参考依据。

---

## 一、系统概览

只狼的动画系统是一个 **多层状态驱动的混合系统**，由以下三层协同工作：

| 层级 | 名称 | 职责 |
|------|------|------|
| L1 | **状态机层** | 定义角色大状态（Idle / Move / Attack / Damage / Dead 等），控制状态进出条件 |
| L2 | **动画混合层** | 在状态内部处理姿势混合（移动方向 BlendSpace、视角偏移、坡度适配） |
| L3 | **动画蒙太奇层** | 播放不可打断/半可打断的固定动画片段（攻击连段、格挡、忍杀） |

切换逻辑的核心公式：

```
下一段动画 = f(当前状态, 输入指令, 上下文标志, 敌人状态, 计时器)
```

---

## 二、核心状态机

### 2.1 顶层状态

```
                    ┌─────────────┐
                    │   Dead      │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
        ┌───────────┤  Idle/Move  │◄──────────┐
        │           └──────┬──────┘           │
        │                  │                  │
        ▼                  ▼                  │
  ┌─────────┐      ┌──────────────┐          │
  │ Attack  │      │ Damage/Guard │          │
  │(连段)   │      │ (受击/格挡)   │          │
  └─────────┘      └──────┬───────┘          │
        │                  │                  │
        ▼                  ▼                  │
  ┌─────────┐      ┌──────────────┐          │
  │ Execute │      │  Deflect     │          │
  │ (忍杀)  │      │ (完美格挡)   │          │
  └─────────┘      └──────┬───────┘          │
                          │                  │
                          ▼                  │
                    ┌──────────────┐         │
                    │  Counter     │         │
                    │ (反斩)       ├─────────┘
                    └──────────────┘
```

### 2.2 状态切换条件

| 源状态 | → 目标状态 | 触发条件 |
|--------|-----------|---------|
| Idle/Move | Attack | R1 按下（R1 → R1 → R1 形成连段）|
| Attack | Attack (下一段) | 前一段攻击的"连段窗口"内 R1 按下 |
| Idle/Move | ChargeAttack | R2 长按 → 蓄力完成 |
| Idle/Move | Guard | L1 按住 |
| Guard | Deflect | L1 在敌人攻击判定帧内按下（精确格挡） |
| Guard | GuardDamage | 格挡时承受攻击（架势条不破）|
| Guard | GuardBreak | 格挡时承受攻击（架势条击破）|
| Deflect | Counter | 完美格挡后 R1 按下（反斩窗口内）|
| Any | Damage | 非格挡状态下被击中 |
| Any | DeathBlow | 敌人架势槽满后 R1（忍杀标识激活时）|
| Attack | JumpCancel | 攻击前摇中 B 键（跳跃取消部分攻击）|
| Idle/Move | Jump | ✗ 键 |
| Idle/Move | Dodge | ◻ 键 + 方向 |
| Jump | AirAttack | 空中 R1 |
| Jump | AirDodge | 空中 ◻ 键 |
| Any | Dead | HP = 0 |

---

## 三、输入系统

### 3.1 输入映射

| 键位 | 指令名称 | 备注 |
|------|---------|------|
| R1 (RB) | `Attack` | 普通攻击 / 突刺攻击 |
| R2 (RT) | `ChargeAttack` | 蓄力攻击（长按）|
| L1 (LB) | `Guard` | 格挡 / 招架 |
| ✗ (A) | `Jump` | 跳跃 / 蹬墙跳 |
| ◻ (X) | `Dodge` | 垫步 / 闪避 |
| ○ (B) | `Interact` | 交互 / 忍杀 / 取消 |
| L3 | `Sprint` | 疾跑 |
| 左摇杆 | `Move` | 移动方向 |
| 右摇杆 | `Camera` | 视角控制 |

### 3.2 输入缓冲（Input Buffer）

只狼使用 **6帧输入缓冲**（60fps 下约 100ms），这是手感的核心机制：

```
输入产生 → [Buffer: 6帧] → 状态机采样 → 找到合法转换 → 播放动画
                              │
                      (若找不到合法转换 → 丢弃)
```

- **缓冲期内**：即便角色处于攻击动画的不可操作帧，输入也被暂存
- **可转换帧到达时**：立即消费缓冲中的输入，无缝衔接下一段动画
- **多输入排队**：同时按下多个键时，按优先级（忍杀 > 闪避 > 跳跃 > 攻击）处理

### 3.3 输入上下文相关（Contextual Input）

同一个键在不同的上下文中产生不同的行为：

```
R1 + 左摇杆推前 + 敌人距离合适 = 突刺攻击
R1 + 左摇杆推后              = 后撤斩
R1 (空中)                     = 空中下劈
R1 (蹬墙)                     = 蹬墙突刺
R1 (敌人构槽满 + 红点)        = 忍杀
R1 (完美格挡后)              = 反斩
```

---

## 四、动画分段结构

### 4.1 攻击动画分段

每段攻击动画被切分为三个时间区间，由 **动画通知（AnimNotifies）** 标记：

```
|←──── StartUp ────→|←── Active ──→|←── Recovery ──→|
                      ↑              ↑
                  判定框激活      判定框结束
    ↑                              ↑
  可取消点(格挡)                 可取消点(垫步)
    ↑
  连段输入窗口开启
```

| 区间 | 名称 | 功能 | 可取消？ |
|------|------|------|---------|
| **StartUp** | 前摇 | 连段输入窗口开启；可被格挡取消 | ✅ 被格挡取消（高风险） |
| **Active** | 判定帧 | 攻击判定框激活；可被敌方格挡/招架 | ❌ 不可取消 |
| **Recovery** | 后摇 | 可通过垫步/跳跃取消后摇 | ✅ 垫步/跳跃取消 |

### 4.2 攻击连段窗口（Combo Window）

```
攻击1 前摇 → 攻击1 判定 → 攻击1 后摇
                          ↑
                    连段窗口：R1按下 → 攻击2
                          ↑
                    (窗口持续约 12-15 帧)
```

- **窗口位置**：攻击动画的约 60%-80% 进度处开启，至动画结束
- **窗口内 R1**：打断当前后摇，直接转入下一段攻击的前摇
- **窗口外 R1**：不产生任何效果（输入被丢弃，因为角色尚在未就绪状态）
- **窗口结束**：动画播完 → 回到 Idle → 此时 R1 是"新的第一段攻击"

### 4.3 特殊窗口

| 窗口 | 持续时间 | 用途 |
|------|---------|------|
| 连段窗口 | 约 12-15 帧 | 衔接攻击连段 |
| 完美格挡窗口 | 约 6-8 帧 | 判定是否为完美格挡（Deflect） |
| 反斩窗口 | 约 20 帧 | 完美格挡后可发动反斩 |
| 垫步无敌帧 | 约 8-10 帧 | 闪避判定无敌 |
| 蓄力取消窗口 | 全程 | 蓄力中可垫步取消 |
| 落地取消窗口 | 约 6 帧 | 落地前输入攻击可触发下劈取消 |

---

## 五、运动系统与动画混合

### 5.1 Locomotion 状态

只狼的 Locomotion 系统在 Idle/Move 状态下使用 **方向混合** 驱动：

```
输入方向向量 → 计算目标朝向偏移角 → BlendSpace 采样 → 输出动画姿势
                                     ↑
                              当前速度（0~1）
```

**BlendSpace 轴：**

| 轴 | 范围 | 说明 |
|----|------|------|
| 水平方向 | -180° ~ +180° | 角色面对方向与输入方向的夹角 |
| 速度 | 0.0 ~ 1.0 | 静止 → 行走 → 疾跑 |

**转向系统：**

- **原地转向**：超过 90° 的转向触发"转向动画"（约 0.3s），期间输入方向被缓存
- **移动中转向**：通过 BlendSpace 平滑过渡，无独立转向动画
- **根运动旋转**：部分攻击动画使用 Root Motion 旋转带动角色朝向

### 5.2 高度适配

```
检测脚下地形高度差：
  高度差 < 15cm  → 正常步态（BlendSpace 自动适配脚部 IK）
  15cm < 高度差 < 50cm → 跨步动画（上/下台阶）
  高度差 > 50cm  → 跳跃/落下过渡
```

### 5.3 跳跃系统

```
跳跃状态机：
  ┌───────┐
  │ Jump  │ ← ✗ 键（地面）
  │ Start │
  └───┬───┘
      │
  ┌───▼────┐     ┌───────────┐
  │  Rising │────►│  Falling  │
  │ (上升)  │     │ (下落)    │
  └───┬────┘     └─────┬─────┘
      │                │
      ▼                ▼
  ┌──────────┐   ┌──────────┐
  │ Air      │   │ Air      │
  │ Attack   │   │ Dodge    │
  │ (R1)     │   │ (◻)      │
  └──────────┘   └────┬─────┘
                      │
              ┌───────▼───────┐
              │  Landing      │
              │ (落地硬直)    │
              └───────┬───────┘
                      │
                  ┌───▼───┐
                  │ Idle  │
                  └───────┘
```

---

## 六、战斗动画切换详解

### 6.1 普通攻击连段

```
[地面, Idle, R1 按下]
  → Anim_Sekiro_Attack_R1_Combo01 播放
    ├── 前摇 8-10帧 (StartUp)
    │     └── 第 4 帧起：连段输入窗口开启 ──→ R1 在此期间按下？→ 标记 combo_ready
    ├── 判定 4-6帧 (Active)
    │     ├── 命中了？→ 敌人受击反应（根据敌人状态选动画）
    │     │     ├── 敌人未格挡 → 敌人播放 HitReaction_轻
    │     │     ├── 敌人格挡 → 播放 GuardImpact（我方有轻微后摇）
    │     │     └── 敌人完美格挡 → 我方被弹开，大后摇（进入 DeflectRecovery）
    │     └── 未命中 → 无反应，继续播放
    └── 后摇 10-12帧 (Recovery)
          ├── combo_ready == true → 第 6 帧转入 Anim_Sekiro_Attack_R1_Combo02
          │     └── 整个过程同上，但 Combo02 结束后：
          │           ├── combo_ready == true → 转入 Combo03（最终段）
          │           └── combo_ready == false → 后摇播完 → 回到 Idle
          ├── ◻ 键按下 → 垫步取消后摇 → Dodge 状态
          └── 后摇播完 → 回到 Idle
```

**连段动画列表：**

| 阶段 | 动画资源 | 帧数 | 说明 |
|------|---------|------|------|
| Combo01 | `Anim_Sekiro_Attack_R1_Combo01` | ~28帧 | 第一段：横斩 |
| Combo02 | `Anim_Sekiro_Attack_R1_Combo02` | ~30帧 | 第二段：横斩 |
| Combo03 | `Anim_Sekiro_Attack_R1_Combo03` | ~32帧 | 第三段：突刺（终结）|

### 6.2 蓄力攻击

```
[R2 按住]
  → 进入 Charge 子状态
  → 播放 Anim_Sekiro_Attack_Charged（蓄力动画，循环播放）
  → 蓄力等级追踪：
      0.5s  → Level 1（十字斩）
      1.5s  → Level 2（满蓄力，突刺）
  → 蓄力过程中：
      ├── ◻ 键 → 垫步取消蓄力
      ├── L1  → 格挡取消蓄力（但有惩罚：取消后不能立即格挡）
      └── 移动 → 缓慢移动（移动速度降低 70%）
  → [R2 松开]
      └── 播放对应的释放动画（Level1 或 Level2）

[蓄力 + 推前左摇杆 + R2 松开] → 突刺蓄力攻击（长位移）
[蓄力 + 空中 + R2 松开]       → 空中蓄力下劈
```

### 6.3 格挡与完美格挡

```
[L1 按下]
  → GuardActive = true
  → 播放 Anim_Sekiro_Guard_Idle（举刀姿势）
  → 姿势切换：进入格挡状态（移动速度降低 30%）

[L1 按下 + 敌人攻击判定帧命中]
  → 检测时间差：
      输入时间 - 判定帧起始时间 ≤ 6帧 → ✅ Deflect（完美格挡）
      输入时间 - 判定帧起始时间 > 6帧 → Guard（普通格挡）

  ┌─────────────────────────────────────────────┐
  │ 完美格挡 (Deflect)                          │
  ├─────────────────────────────────────────────┤
  │ • 播放 Anim_Sekiro_Deflect                  │
  │ • 敌人架势条大幅增加（普通格挡的 3倍）       │
  │ • 我方架势条不增加                          │
  │ • 产生大量火花特效 + 特殊音效               │
  │ • 反斩窗口开启（约 20 帧）                  │
  │   └── 窗口内 R1 → CounterAttack（反斩）     │
  │ • 自身不会产生后摇硬直                      │
  └─────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────┐
  │ 普通格挡 (Guard)                            │
  ├─────────────────────────────────────────────┤
  │ • 播放 Anim_Sekiro_Guard Impact（轻/重）     │
  │ • 敌人架势条少量增加                        │
  │ • 我方架势条增加（视敌人攻击强度）           │
  │ • 自身有小幅后摇（约 8 帧）                 │
  │ • 无反斩窗口                                │
  │ • 架势条满 → GuardBreak（破防）             │
  └─────────────────────────────────────────────┘
```

### 6.4 受击硬直

```
[被击中时，Posture_Damage < Max]
  → 根据伤害类型和方向选择受击动画
  → 播放 Anim_Sekiro_Damage_*（硬直动画）
  → 硬直期间输入被抑制（约 10-25 帧，取决于攻击强度）
  → 硬直结束 → 回到 Idle

[被击中时，Posture_Damage == Max]
  → 播放 Anim_Sekiro_GuardBreak
  → 大硬直（约 60 帧 = 1 秒）
  → 可被 R1 忍杀（如果敌人靠近） → 死亡画面
  → 硬直结束 → Posture_Damage 重置为 0 → 回到 Idle
```

---

## 七、上下文标志系统（切换决策的核心）

动画切换的实际决策依赖于一组**标志位（Flag）**，它们在运行时被实时读取和修改：

| 标志位 | 类型 | 说明 | 设置时机 | 消费时机 |
|--------|------|------|---------|---------|
| `CanCombo` | bool | 是否可进入下一段连段攻击 | Combo01 前摇第 4 帧置 true | 转入 Combo02/03 后置 false |
| `IsCharging` | bool | 是否正在蓄力 | R2 按住置 true | R2 松开置 false |
| `GuardActive` | bool | 是否按住格挡 | L1 按下置 true | L1 松开置 false |
| `DeflectWindow` | bool | 当前帧是否可触发完美格挡 | 敌人攻击判定帧前 6 帧置 true | 判定帧结束置 false |
| `CounterWindow` | bool | 完美格挡后是否可反斩 | Deflect 动画第 2 帧置 true | 窗口超时置 false（约 20 帧）|
| `DeathBlowActive` | bool | 是否可执行忍杀 | 敌人架势槽满 + 靠近 + 正面/背面判定 | 忍杀动画结束置 false |
| `IsGrounded` | bool | 是否在地面 | 地面置 true | 离开地面置 false |
| `CanJumpCancel` | bool | 是否可跳跃取消当前动画 | StartUp + Recovery 区间置 true | Active 区间置 false |
| `CanDodgeCancel` | bool | 是否可垫步取消当前动画 | Recovery 区间置 true | StartUp + Active 置 false |
| `ComboID` | int | 当前连段序号（1-3） | 每段攻击开始时递增 | 回到 Idle 重置为 0 |
| `ChargeLevel` | int | 蓄力等级（1-2） | 蓄力 0.5s→1, 1.5s→2 | 蓄力释放后重置 |

### 7.1 决策流程伪代码

```
每帧更新 (60fps)：

1. 采样输入缓冲
2. 更新敌人攻击判定状态（DeflectWindow 等）
3. 更新当前动画进度 → 更新上下文标志位
4. 根据 (当前状态 + 上下文标志) 处理输入：

   if DeathBlowActive && R1_Pressed:
     → PlayExecution(enemy)
     return

   if CounterWindow && R1_Pressed:
     → PlayCounterAttack()
     return

   if CanCombo && R1_Pressed && ComboID < 3:
     → PlayNextCombo()
     return

   if R2_Held && !IsCharging:
     → StartCharge()
     return

   if L1_Pressed && DeflectWindow:
     → PlayDeflect()
     → SetCounterWindow()
     return

   if L1_Pressed:
     → SetGuardActive()
     return

   if Dodge_Pressed && CanDodgeCancel:
     → PlayDodge(direction)
     return

   if Jump_Pressed && CanJumpCancel:
     → PlayJump()
     return

   if NoCombatInput && AnimFinished:
     → TransitionToLocomotion()
     return

   // ... 其他输入处理
```

---

## 八、特殊动画切换

### 8.1 忍杀（DeathBlow / Execution）

```
触发条件：敌人架势槽 Max + 红点标识 R1 按下
  → 锁定目标（面向敌人）
  → 播放执行的蒙太奇动画（不可打断）
  → 敌人播放受击动画（同步）
  → 动画结束 → 双方解锁
  → 播放结束特效
  → 回到 Idle / 自动切换下一个敌人
```

忍杀动画根据**位置**和**敌人类型**选择：

| 位置 | 动画 | 说明 |
|------|------|------|
| 正面 | `Anim_Sekiro_Execute_Front` | 正面忍杀 |
| 背面（潜行）| `Anim_Sekiro_Execute_Back` | 潜行忍杀 |
| 空中下劈 | `Anim_Sekiro_Execute_Air` | 空中忍杀（造成巨量架势伤害）|
| Boss 特殊 | `Anim_Sekiro_Execute_Boss/N` | 各 Boss 专属忍杀演出 |

### 8.2 闪避与垫步

```
[◻ 键 + 左摇杆方向]
  ┌────────────────┬──────────────────────────┐
  │ 输入           │ 动画                    │
  ├────────────────┼──────────────────────────┤
  │ ◻ (无方向)    │ Anim_Sekiro_Dodge_Back   │
  │ ◻ + 前        │ Anim_Sekiro_Dodge_Fwd    │
  │ ◻ + 后        │ Anim_Sekiro_Dodge_Back   │
  │ ◻ + 左        │ Anim_Sekiro_Dodge_L      │
  │ ◻ + 右        │ Anim_Sekiro_Dodge_R      │
  │ 疾跑中 ◻      │ Anim_Sekiro_Dodge_Dash   │
  └────────────────┴──────────────────────────┘

  无敌帧：动画的第 3-12 帧（约 8-10 帧，视方向而异）
  垫步后可衔接：
    ├── R1 → 垫步攻击（Attack_Dodge_*）
    ├── R2 → 快速蓄力攻击
    └── L1 → 立即举刀格挡
```

### 8.3 跳跃相关

```
跳跃攻击：空中 R1 → Anim_Sekiro_Attack_Jump
  └── 命中敌人 → 敌人架势条大伤害 + 弹开

跳跃垫步：空中 ◻ → Anim_Sekiro_Dodge_Air
  └── 空中横向位移

踩踏（蹬墙跳）：
  靠近墙壁 + 推前 + ✗ → 蹬墙二段跳
  ✗ + 敌人头上方 → Anim_Sekiro_Jump_Kick（踩踏敌人，架势条大伤害）

落地取消：
  落地前约 6 帧输入攻击 → 空中下劈攻击（自动追踪）
  硬直：落地约 8 帧 Recovery → 可移动
```

### 8.4 Quickstep 与走位动画

```
Quickstep（短距位移，多用于攻击的微调移动）：
  ┌────────────────┬──────────────────────────┐
  │ 输入           │ 动画                    │
  ├────────────────┼──────────────────────────┤
  │ L1+方向+攻击  │ Anim_Sekiro_Attack_Quick_*│
  │ 攻击后快速     │                          │
  │ 短推摇杆       │                          │
  └────────────────┴──────────────────────────┘
```

---

## 九、动画切换规则总结

### 规则优先级（从高到低）

| 优先级 | 切换 | 条件 |
|--------|------|------|
| 1 | 任何状态 → Dead | HP == 0 |
| 2 | 任何状态 → Execution | DeathBlowActive && R1 |
| 3 | 任何状态 → GuardBreak | GuardActive && 架势槽满 |
| 4 | 任何状态 → Deflect | L1 按下 && DeflectWindow |
| 5 | Attack → CounterAttack | CounterWindow && R1 |
| 6 | Attack → Attack(Combo) | CanCombo && R1 |
| 7 | Attack → Dodge | CanDodgeCancel && ◻ |
| 8 | Any → Guard | L1 按下 |
| 9 | Any → Damage | 被击中 && !GuardActive |
| 10 | Attack → Jump | CanJumpCancel && ✗ |
| 11 | Any → Jump | ✗ 按下 |
| 12 | Any → Dodge | ◻ 按下 |
| 13 | Any → Attack | R1 按下 |
| 14 | Any → Idle/Move | 无输入 && 动画播完 |

### 关键设计原则

1. **优先级切断**：高优事件（如忍杀）可以打断任何正在播放的动画
2. **窗口机制**：绝大部分切换不是"立即发生"的，而是依赖于动画中的特定窗口
3. **输入缓冲**：6帧缓冲防止"帧完美"要求，同时保持操作精确感
4. **上下文决策**：同一输入在不同状态下产生完全不同的行为（如 R1 = 攻击 / 反斩 / 忍杀）
5. **根运动（Root Motion）**：战斗动画使用根运动驱动角色位移，Locomotion 使用速度向量驱动

---

## 十、UE5 迁移要点

| 原版机制 | UE5 实现方式 |
|---------|-------------|
| 状态机 | `UAnimInstance` + 状态机（AnimBlueprint 中的 StateMachine）|
| 动画分段 + 窗口 | `UAnimNotify` + `UAnimNotifyState` |
| 输入缓冲（6帧） | 自定义 `UInputBufferComponent` |
| 上下文标志 | `UCharacterStateComponent` 维护一组 Flag |
| 连段系统 | `ComboSystem`（Montage Section 跳转或独立 Montage 拼接）|
| 姿态/架势 | `UPostureComponent`（PostureDamage / MaxPosture）|
| 根运动 | 勾选 EnableRootMotion 或使用 `UAnimMontage` 根运动设置 |
| BlendSpace | `UBlendSpace` (1D: 速度, 2D: 方向+速度) |
| 多层混合 | `ULayeredBoneBlend` 或 `UBlendSpace` 层叠 |

---

## 附录：动画资源映射表

以下是原版只狼的主要动画列表，已在项目中导入：

| 资产路径 | 用途 |
|---------|------|
| `Anim_Sekiro_Attack_R1_Combo01/02/03` | 地面攻击三段连击 |
| `Anim_Sekiro_Attack_Charged` | 蓄力攻击 |
| `Anim_Sekiro_Attack_Charged_Dash/L` | 蓄力突刺/蓄力横斩 |
| `Anim_Sekiro_Attack_Charged_Step` | 蓄力步进 |
| `Anim_Sekiro_Attack_Jump` | 跳跃攻击 |
| `Anim_Sekiro_Attack_Jump_Charged` | 空中蓄力下劈 |
| `Anim_Sekiro_Attack_Jump_Fwd` | 前跳攻击 |
| `Anim_Sekiro_Attack_Crouch` | 下蹲攻击 |
| `Anim_Sekiro_Attack_Crouch_Charged` | 下蹲蓄力攻击 |
| `Anim_Sekiro_Attack_Dodge_Back/Fwd/L/R` | 垫步攻击（各方向）|
| `Anim_Sekiro_Attack_Dodge_Dash` | 疾跑垫步攻击 |
| `Anim_Sekiro_Attack_Dodge_Charged` | 垫步蓄力攻击 |
| `Anim_Sekiro_Attack_GuardBreak` | 破防反击 |
| `Anim_Sekiro_Attack_Quickstep_Bwd/Fwd/L/R` | Quickstep 攻击 |
| `Anim_Sekiro_Guard_Idle` | 持刀格挡姿势 |
| `Anim_Sekiro_Deflect` | 完美格挡 |
| `Anim_Sekiro_GuardBreak` | 破防 |
| `Anim_Sekiro_Dodge_*` | 各方向垫步 |
| `Anim_Sekiro_Jump_*` | 跳跃相关动画 |
| `Anim_Sekiro_Damage_*` | 受击硬直 |
| `Anim_Sekiro_Execute_Front/Back/Air` | 忍杀动画 |
