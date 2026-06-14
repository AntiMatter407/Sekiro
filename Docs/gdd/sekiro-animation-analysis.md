# 只狼动画系统完整分析

> **状态**: 正式版
> **最后更新**: 2026-06-14
> **数据来源**: Sekiro_TAE_Logic.json (65个TAE文件, 2209个动画, 21,148个Type=0 JumpTable事件)
> **GDD依赖**: animation-system.md, tai-to-ue-anim-blueprint.md, attack-combo-system.md, deflect-system.md, dodge-mikiri-system.md, movement-system.md, input-system.md

---

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────────────┐
│                        ABP_Sekiro (AnimBlueprint)                    │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Layer 3: Reaction (priority=8, 最高优先级)                    │   │
│  │  Deathblow(10) > Death(9) > Hit(8) > Resurrection(10)        │   │
│  │  不可被任何动作打断                                           │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              ▲ 打断                                  │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Layer 2: Combat (priority=2, Montage槽位)                     │   │
│  │  Dodge(7) > Deflect(6) > Guard(5) > Prosthetic(4)            │   │
│  │  > ItemUse(3) > Attack(2) > Quickstep(1)                     │   │
│  │  Transition: InputIntent + CanCancelTo(AnimID,Time,Action)   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              ▲ 叠加                                  │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Layer 1: Locomotion (priority=0, 始终运行)                    │   │
│  │  BlendSpace1D/2D: Speed(0→600) × Direction(-180°→180°)       │   │
│  │  Idle → Walk → Jog → Run → Sprint                            │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.1 FS→UE 范式桥接

```
FS 原始系统 (动画推送)                 UE Animation Blueprint (状态机拉取)
┌──────────────────────┐              ┌──────────────────────┐
│ 动画内置 TAE 事件     │    ──→      │ 状态机 Transition Rule │
│ JumpTable 嵌入帧中    │  导入转换   │ CanCancelTo 查询接口   │
│ "动画知道何时可取消"   │              │ "状态图知道何时切换"   │
└──────────────────────┘              └──────────────────────┘
         │                                      │
         └──────── USKAnimInstance ─────────────┘
              (运行时状态 + 数据查询桥)
```

---

## 2. 输入→动画映射总图

```
                        ┌──────────────────────────┐
                        │     Enhanced Input        │
                        │  (UInputMappingContext)    │
                        └──────────┬───────────────┘
                                   │
            ┌──────────────────────┼──────────────────────┐
            │                      │                      │
            ▼                      ▼                      ▼
    ┌──────────────┐      ┌──────────────┐      ┌──────────────┐
    │  IA_Move     │      │  战斗输入     │      │  系统输入     │
    │  (Axis2D)    │      │  (Digital)   │      │  (Digital)   │
    └──────┬───────┘      └──────┬───────┘      └──────┬───────┘
           │                     │                      │
           ▼                     ▼                      ▼
  ┌─────────────────┐  ┌──────────────────┐  ┌──────────────────┐
  │ Speed/Direction │  │ InputIntent      │  │ IA_Jump/IA_Pause │
  │ → BlendSpace    │  │ → CanCancelTo()  │  │ IA_LockOn/IA_GH  │
  │                 │  │ → TransitionRule │  │                  │
  └────────┬────────┘  └────────┬─────────┘  └────────┬─────────┘
           │                     │                      │
           ▼                     ▼                      ▼
    ┌──────────────────────────────────────────────────────────┐
    │              USKAnimInstance (C++ AnimInstance)           │
    │                                                          │
    │  Speed ──→ BlendSpace X 轴                               │
    │  Angle ──→ BlendSpace Y 轴 (2D)                          │
    │  InputIntent ──→ CanCancelTo(CurrentAnimID, Time, Action)│
    │  bIsDodging / bIsInAir / bIsCrouching ──→ Transition     │
    │  MovementTier ──→ Walk/Jog/Run/Sprint 速度分界            │
    └──────────────────────────────────────────────────────────┘
```

### 2.1 输入动作→动画动作映射表

| 输入 | 触发方式 | InputIntent | 目标Action | 优先级 | 动画类别 |
|------|---------|-------------|-----------|--------|---------|
| IA_Move | Continuous (Axis2D) | — | — | — | Locomotion BlendSpace |
| IA_Attack (Tap) | Tap | `Input.Attack` | `Attack` | 2 | Attack_R1_Combo01→04 |
| IA_Attack (Hold>0.3s) | Hold→Release | `Input.Attack` | `Attack` | 2 | Attack_Charged / Thrust |
| IA_Attack (Jump中) | Tap | `Input.Attack` | `Attack` | 2 | Attack_Jump / Jump_Fwd |
| IA_Deflect (Tap) | Tap (精确时机) | `Input.Deflect` | `Deflect` | 6 | Deflect_Counter / Parry_Deflect_Success |
| IA_Deflect (Hold) | Hold | `Input.Guard` | `Guard` | 5 | Guard_Idle / Guard_Raise |
| IA_Dodge (+Dir) | Tap + Direction | `Input.Dodge` | `Dodge` | 7 | StepDodge / Quickstep |
| IA_Dodge (前+锁定+突刺) | Tap | `Input.Dodge` | `Dodge` | 7 | Mikiri_Counter |
| IA_UseItem | Tap | `Input.Item` | `ItemUse` | 3 | ItemUse 喝药动画 |
| IA_Prosthetic (L2) | Tap | `Input.Prosthetic` | `Prosthetic` | 4 | Prosthetic_* (义手) |
| IA_Jump | Tap | — | — | — | Jump_* / Fall / Land |

---

## 3. 动画状态机跳转混合总图

### 3.1 完整状态跳转图

```
                              ┌─────────────┐
                              │   Entry     │
                              └──────┬──────┘
                                     │
                                     ▼
                    ┌─────────────────────────────────┐
                    │     Locomotion (Layer 1)         │
                    │  1D/2D BlendSpace, Speed驱动      │
                    │                                  │
                    │  Idle ←→ Walk ←→ Jog ←→ Run     │
                    │    └──── Sprint (Hold)            │
                    │    └──── Turn (L45/L90/L180/R..)  │
                    │    └──── Jump / Fall / Land       │
                    └───────────┬─────────────────────┘
                                │ InputIntent != None
                                │ CanCancelTo() == true
                                ▼
                    ┌─────────────────────────────────┐
                    │     Combat (Layer 2)              │
                    │  Montage Slot, 优先级打断         │
                    │                                  │
                    │  ┌── Attack ──────────────────┐  │
                    │  │ R1_Combo01 → 02 → 03 → 04  │  │
                    │  │   ├─ Deflect (弹刀取消)     │  │
                    │  │   ├─ Dodge (闪避取消)       │  │
                    │  │   └─ GuardBreak (破防)      │  │
                    │  │ Charged / Thrust / Jump    │  │
                    │  │ Dodge_Fwd/L/R/Dash/Back    │  │
                    │  │ Quickstep_Fwd/L/R/Bwd      │  │
                    │  │ Crouch / Sprint_R1         │  │
                    │  └────────────────────────────┘  │
                    │                                  │
                    │  ┌── Defense ─────────────────┐  │
                    │  │ Guard_Idle ←→ Guard_Raise  │  │
                    │  │ Parry → Deflect_Success    │  │
                    │  │   → Deflect_Counter_R1     │  │
                    │  │ Guard_Heavy_Hit            │  │
                    │  │ Guard_Break → Recover      │  │
                    │  │ Mikiri_Counter / StepIn    │  │
                    │  │ Sweep_JumpKick             │  │
                    │  │ Grab_Escape                │  │
                    │  └────────────────────────────┘  │
                    │                                  │
                    │  ┌── Dodge ───────────────────┐  │
                    │  │ StepDodge_Fwd/Bwd/L/R      │  │
                    │  │ StepDodge_Dash             │  │
                    │  │ Quickstep_Fwd/L/R/Bwd      │  │
                    │  └────────────────────────────┘  │
                    │                                  │
                    │  ┌── Other ───────────────────┐  │
                    │  │ Prosthetic (义手13种)       │  │
                    │  │ ItemUse (喝药)              │  │
                    │  │ CombatArt (流派技20种)      │  │
                    │  └────────────────────────────┘  │
                    └───────────┬─────────────────────┘
                                │ 被Hit事件触发
                                │ 优先级: Hit(8) > 所有Combat
                                ▼
                    ┌─────────────────────────────────┐
                    │    Reaction (Layer 3)             │
                    │  最高优先级, 不可取消             │
                    │                                  │
                    │  Hit_Light → Hit_Medium          │
                    │  Hit_Heavy → Hit_KnockBack       │
                    │  Hit_Stagger → Hit_Ground_Recover│
                    │  Hit_Air_Recover                  │
                    │  Death → Death_Immortal_Recover   │
                    │  Resurrection (回生)              │
                    │  Deathblow (忍杀演出, priority=10)│
                    │  PostureBreak (架势崩坏)          │
                    └─────────────────────────────────┘
```

### 3.2 混合(Blend)机制

```
┌─────────────────────────────────────────────────────────────────┐
│                      混合机制全景                                │
│                                                                 │
│  ┌── BlendSpace (Locomotion) ─────────────────────────────┐     │
│  │  Speed 0────150────300────450────600                    │     │
│  │  Idle  Walk   Jog    Run    Sprint                     │     │
│  │  使用线性插值在采样点间平滑过渡                           │     │
│  │  InterpolationSpeed = 5.0 (可调 2-10)                   │     │
│  └────────────────────────────────────────────────────────┘     │
│                                                                 │
│  ┌── Montage BlendIn/BlendOut (Combat/Reaction) ──────────┐     │
│  │  BlendIn:  0.05s (弹刀) / 0.1s (攻击/闪避/其他)        │     │
│  │  BlendOut: 0.1s (标准) / 0.05s (受击打断)              │     │
│  │  BlendOut 期间 AnimNotify 不触发                       │     │
│  └────────────────────────────────────────────────────────┘     │
│                                                                 │
│  ┌── Layered Blend Per Bone (上下半身分离) ───────────────┐     │
│  │  UpperBodySlot: 移动中攻击/防御/使用道具                │     │
│  │  Spine03 及以上 = 上半身动画 (Combat Slot)              │     │
│  │  Spine03 以下 = 下半身动画 (Locomotion BlendSpace)     │     │
│  └────────────────────────────────────────────────────────┘     │
│                                                                 │
│  ┌── Inertialization (UE5.2+ 新型混合) ───────────────────┐     │
│  │  用于状态机跳转时的惯性过渡                               │     │
│  │  替代传统 Blend, 物理更自然                              │     │
│  └────────────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. TAE事件 → 动画帧映射

### 4.1 TAE事件分类体系

```
TAE Event Type 分布 (2209个动画, 总计60,000+事件):
═══════════════════════════════════════════════════════

  Type=0    JumpTable        21,148  (35.2%)  状态机分支/行为控制
  Type=67   AddSpEffect      11,177  (18.6%)  状态效果添加
  Type=128  PlaySound        10,849  (18.1%)  音效触发
  Type=96   SpawnFFX          2,532  ( 4.2%)  视觉特效
  Type=607  FacialExpression  2,184  ( 3.6%)  面部表情
  Type=112  SpawnFFX          2,106  ( 3.5%)  特效变种
  Type=1    AttackBehavior    1,514  ( 2.5%)  攻击盒+伤害
  Type=129  PlaySound         1,343  ( 2.2%)  音效变种
  Type=960  (Behavior)        1,329  ( 2.2%)  行为控制
  Type=16   Blend             1,308  ( 2.2%)  动画混合
  Type=792  (Behavior)        1,242  ( 2.1%)  行为控制
  Type=131  PlaySound         1,163  ( 1.9%)  音效变种
  Type=307  (Behavior)        1,158  ( 1.9%)  行为控制
  Type=2    BulletBehavior      607  ( 1.0%)  射弹/飞行道具
  Type=401  (SpEffect)         597  ( 1.0%)  SpEffect变种
  Type=144  RumbleCam          629  ( 1.0%)  镜头震动
  Type=700  (FootStep)         473  ( 0.8%)  脚步声
  其他      200+                剩余          多种杂项
```

### 4.2 JumpTable ID 完整映射表

FS原始数据中实际出现频率最高的JumpTable ID及其含义:

| JumpTableID | 出现次数 | 推断含义 | 映射到ESKJumpTableAction | UE转换方式 |
|-------------|---------|---------|--------------------------|-----------|
| **133** | 1,420 | 禁止特殊动作(枪/义手) | (待添加) | Set bDisableSpecialAction |
| **134** | 1,398 | 禁止道具使用 | (待添加) | Set bDisableItem |
| **7** | 1,378 | 禁止转向 | DisableTurning | Set bDisableTurning=true |
| **87** | 1,333 | 调用攻击行为 | InvokeAttackAction | Trigger AttackBehavior |
| **119** | 1,250 | 启用弹刀模式 | EnableParry | Set bCanDeflect=true |
| **115** | 1,217 | R1取消窗口结束 | AnimCancelEnd_R1 | Close R1 CancelWindow |
| **32** | 1,172 | 进入移动状态 | (待添加) | EnterMovementState |
| **31** | 1,158 | 退出移动状态 | (待添加) | ExitMovementState |
| **118** | 1,129 | L2取消窗口结束 | AnimCancelEnd_L2 | Close L2 CancelWindow |
| **117** | 1,127 | L1取消窗口结束 | AnimCancelEnd_L1 | Close L1 CancelWindow |
| **26** | 1,096 | 取消窗口(通用) | (待添加) | CancelWindow_General |
| **137** | 1,091 | 禁止弹刀 | (待添加) | Set bDisableParry |
| **154** | 1,049 | 开启取消窗口(道具) | (待添加) | CancelStart_Item |
| **11** | 999 | 切换动作层 | (待添加) | SwitchActionLayer |
| **28** | 949 | 设置移动速度正常 | (待添加) | SetMoveSpeedNormal |
| **51** | 641 | 无敌帧开始/结束 | (待添加) | SetInvincibility |
| **63** | 240 | 特殊动作标志 | (待添加) | SpecialActionFlag |
| **50** | 227 | 动作限制 | (待添加) | ActionRestriction |
| **55** | 212 | 硬直标志 | (待添加) | StaggerFlag |
| **65** | 191 | 追踪目标 | (待添加) | TrackTarget |

**关键发现**: 当前`SekiroTAEImporter::MapJumpTableToAction()`只映射了22个ID，数据中实际有70+不同的JumpTableID。高频ID 133/134/32/31/26/137/154/11/28/51占事件的80%，需要优先补充映射。

### 4.3 典型动画的TAE事件帧级分析

#### Attack_R1_Combo 系列 (AnimID 100000-100003)

```
AnimID=100000 (Attack_R1_Combo01)
  帧: 0──────────────────────────────────────────────> 结束帧
  TAE事件:
    Type=0  JumpTable:  [需要从完整数据提取]
    Type=1  AttackBehavior: [攻击盒激活帧范围]
    Type=128 PlaySound: [挥刀音效帧]
    Type=96  SpawnFFX: [火花特效帧]
  
  CancelWindow (推断):
    CancelStart_R1: 帧18→24  (可连段到Combo02)
    CancelStart_L1: 帧12→24  (可取消到防御)
    CancelStart_Dodge: 帧12→20 (可取消到闪避)

AnimID=100001 (Attack_R1_Combo02)  
  同上模式, CancelWindow帧偏移不同

AnimID=100002 (Attack_R1_Combo03)
  同上模式

AnimID=100003 (Attack_R1_Combo04, 最后一段)
  CancelStart_R1: 无 (最后一段不可接Combo)
  CancelStart_L1: 帧18→35
  CancelEnd_General: 帧35
```

#### Defense_Guard/Deflect 系列 (AnimID 200000-202700)

```
AnimID=200000 (Guard_Idle, 150帧循环)
  ┌─ 帧0-33 ─────────────────────────────────────┐
  │ JT=87  InvokeAttackAction   (激活攻击判定)    │
  │ JT=119 EnableParry           (启用弹刀模式)    │
  │ JT=31  ExitMovementState     (退出移动)       │
  │ JT=133 DisableSpecialAction  (禁枪/义手)      │
  │ JT=134 DisableItem           (禁道具)         │
  │ JT=7   DisableTurning        (禁转向)         │
  │ JT=28  SetMoveSpeedNormal    (常速移动)       │
  └───────────────────────────────────────────────┘
  标志着: 进入防御姿态 → 启用弹刀 → 禁转向 → 限制移动

AnimID=201010 (Parry_Raise, 100帧, 举刀过程)
  ┌─ 帧0-38 ─────────────────────────────────────┐
  │ JT=87  InvokeAttackAction                    │
  │ JT=115 AnimCancelEnd_R1    (关闭R1取消窗口)   │
  │ JT=154 CancelStart_Item    (开道具取消窗口)   │
  │ JT=117 AnimCancelEnd_L1    (关闭L1取消窗口)   │
  │ JT=137 DisableParry        (禁弹刀!)         │
  │ JT=118 AnimCancelEnd_L2    (关闭L2取消窗口)   │
  │ JT=119 EnableParry         (又启用弹刀!)      │
  │ JT=26  CancelWindow_通用                      │
  └───────────────────────────────────────────────┘
  标志着: 举刀过渡 → 弹刀窗口建立
```

#### Hit 受击系列 (AnimID 250000-250600)

```
AnimID=250000 (Hit_Light_Front, 300帧)
  ┌─ 帧18-63 ────────────────────────────────────┐
  │ JT=87  InvokeAttackAction                    │
  │ JT=115 CancelEnd_R1                          │
  │ JT=154 CancelStart_Item                      │
  │ JT=117 CancelEnd_L1                          │
  │ JT=137 DisableParry                          │
  │ JT=118 CancelEnd_L2                          │
  │ JT=119 EnableParry                           │
  │ JT=26  CancelWindow_通用                      │
  └───────────────────────────────────────────────┘
  标志着: 受击硬直 → 开始关闭各取消窗口 → 63帧后可恢复

  帧 0──18──63──────────────────────────────300
       │   │                                   │
       │   └─ 各CancelEnd事件密集触发          │
       │      JT=137禁弹刀                     │
       │      JT=119又启弹刀 (窗口控制精细)     │
       └──── 受击开始, 短暂无敌/霸体            │
```

#### Locomotion 移动系列 (AnimID 0-999)

```
AnimID=100 (Walk_Fwd, 300帧循环)
  无JumpTable事件 — 纯粹的移动动画
  其他事件: Type=700 (FootStep 脚步声), Type=607 (Facial)

AnimID=400 (Run_Fast_Fwd, 300帧循环) 
  无JumpTable事件 — 奔跑循环
  SetTurnSpeed=0 可能是特征 (区别于AnimID=300 Sprint_Fwd)

AnimID=300 (Sprint_Fwd, 非循环垫步)
  含 JT=28 SetMoveSpeedNormal
  SetTurnSpeed=0 (垫步中不能转向!)
  确认: 不是循环移动动画, 是垫步冲刺
```

---

## 5. 动画跳转混合矩阵

### 5.1 动作取消规则矩阵

行=当前动作, 列=可取消到的目标动作. ✓=可取消, ✗=不可取消, △=条件取消

| 当前\目标 | Attack | Deflect | Guard | Dodge | Item | Prosthetic | Quickstep |
|-----------|--------|---------|-------|-------|------|------------|-----------|
| Idle | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Walk/Run | △¹ | ✓ | ✓ | ✓ | △¹ | △¹ | ✓ |
| Attack_Combo01 | ✓→02² | ✓★ | ✗ | ✓★ | ✗ | ✗ | ✗ |
| Attack_Combo02 | ✓→03² | ✓★ | ✗ | ✓★ | ✗ | ✗ | ✗ |
| Attack_Combo03 | ✓→04² | ✓★ | ✗ | ✓★ | ✗ | ✗ | ✗ |
| Attack_Combo04 | ✗ | ✓★ | ✗ | ✓★ | ✗ | ✗ | ✗ |
| Attack_Charged | ✗ | ✓★ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Guard_Idle | ✓ | ✓ | — | ✓ | ✗ | ✗ | ✓ |
| Deflect_Success | ✓³ | ✓ | ✓ | ✓ | ✗ | ✗ | ✓ |
| Dodge(StepDodge) | ✓⁴ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| Quickstep | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | — |
| ItemUse | ✗ | ✗ | ✗ | ✓⁵ | — | ✗ | ✗ |
| Hit_Light | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Hit_Heavy | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Death | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Deathblow | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |

> ¹ 移动中上半身可攻击/使用道具 (LayeredBlendPerBone)
> ² CancelWindow内按攻击键 → 连段下一段, 窗口外 → 无效
> ³ 弹刀成功后CancelWindow立刻开启 → 可立即反击
> ⁴ 闪避后追击攻击 (Attack_Dodge_Fwd等)
> ⁵ 原版喝药可被闪避取消
> ★ 仅在CancelWindow内可取消

### 5.2 优先级打断表

高优先级动作可以打断低优先级动作, 反之不行:

```
优先级 (数字越大越优先):
═══════════════════════════════════════

  10 ── Deathblow (忍杀演出)
  10 ── Resurrection (回生)
   9 ── Death (死亡)
   8 ── Hit_Light / Hit_Heavy / Knockback (受击)
   7 ── Dodge / EmergencyDodge (闪避)
   6 ── Deflect (弹刀成功)
   5 ── Guard (格挡/防御姿态)
   4 ── Prosthetic (义手 L2)
   3 ── ItemUse (喝药)
   2 ── Attack (所有攻击)
   1 ── Quickstep (垫步)

  打断规则: TargetPrio > CurrentPrio → 允许打断
  相同优先级: 先到先得, 不打断

示例:
  Attack(2) 可以被 Hit(8) 打断 ✓
  Attack(2) 可以被 Dodge(7) 打断 ✓  
  Dodge(7) 可以被 Attack(2) 打断 ✗ (低不能打高)
  Hit(8) 可以被任何动作打断 ✗ (Reaction不可取消)
```

### 5.3 CancelWindow 判定公式

```
判定流程:
  1. 玩家按下按键 → 写入 InputIntent
  2. NativeUpdateAnimation 消费 InputIntent
  3. CanCancelTo(TargetAction, OutCrossfade):
     a. PriorityCheck: TargetPrio > CurrentPrio
     b. FrameCheck: CurrentFrame ∈ [CancelWindowStart, CancelWindowEnd]
     c. ActionCheck: TargetAction == CancelWindow.TargetAction
     d. 返回 true + CrossfadeDuration
  4. ABP Transition Rule 检测到 true → 触发状态切换
  5. Montage_Play(TargetMontage, BlendIn=Crossfade)

伪代码:
  bool CanCancelTo(TargetAction, &OutCrossfade):
    if AnimLogicData == null → return false
    if GetActionPriority(TargetAction) <= GetActionPriority(CurrentAction)
       && CurrentAction != None → return false
    
    Rule = AnimLogicData.FindCancelRule(CurrentAnimID)
    if Rule == null → return false
    
    CurrentFrame = CurrentAnimTime * 30  // 转30fps帧号
    if CurrentFrame in [Rule.StartFrame, Rule.EndFrame]:
      if Rule.TargetAction == TargetAction:
        OutCrossfade = Rule.CrossfadeDuration  // 典型0.1s
        return true
    
    return false
```

---

## 6. 每个动画类别详解

### 6.1 Locomotion 移动层

| AnimID范围 | 类别 | 动画数 | 关键特征 |
|-----------|------|--------|---------|
| 0-13 | Idle | 10 | 无JumpTable, 循环, Speed=0 |
| 100-199 | Walk | 16 | FootStep事件, 循环, Speed≈150 |
| 200-299 | Jog | 4 | 循环, Speed≈300 |
| 300-399 | Sprint | 4 | AnimID 300=垫步(含JT=28), AnimID 400才是真奔跑循环 |
| 400-499 | Run_Fast | 8 | 循环, Speed≈450-600 |
| 500-699 | 移动过渡 | 6 | Sprint→Idle, Sprint→Idle_Fast |
| 5000-5999 | Turn | 12 | L45/L90/L135/L180 + R版本, 非循环 |
| 20000-49999 | 移动过渡 | 6 | Walk→Idle/Jog, Run→Idle/Walk/Jog, Sprint→Run/Jog |

**BlendSpace 采样点配置**:
```
1D BlendSpace (当前实现):
  Axis X: Speed 0 → 600
  ┌──────┬──────┬──────┬──────┬──────┐
  │ Idle │ Walk │ Jog  │ Run  │Sprint│
  │  0   │ 150  │ 300  │ 450  │ 600  │
  └──────┴──────┴──────┴──────┴──────┘

待升级 2D BlendSpace:
  Axis X: Speed 0 → 600
  Axis Y: Direction -180° → 180°
  每个速度层有 Fwd/L/R/Bwd 采样点
```

### 6.2 Attack 攻击层

| AnimID范围 | 子类 | 动画数 | Combo链 |
|-----------|------|--------|---------|
| 100000-100003 | R1_Combo | 4 | 01→02→03→04 |
| 100100-100102 | R1_Step | 3 | 01→02→03 |
| 100200-100201 | R1_Dash | 2 | 01→02 |
| 100300-100320 | R1_L (左向) | 6 | Combo01→02, Step01→02, Dash01 |
| 100400-100420 | Charged | 4 | 蓄力→释放 |
| 100500-100610 | Thrust | 3 | 突刺+蓄力突刺 |
| 100800 | GuardBreak | 1 | 破防攻击 |
| 101100-101300 | Sprint_R1/Thrust | 2 | 冲刺攻击 |
| 102000-102940 | Dodge攻击 | 13 | 各方向闪避后追击 |
| 103000-103300 | Jump攻击 | 3 | 跳斩+跳劈+跳蓄力 |
| 104100-104300 | Crouch攻击 | 2 | 蹲斩+蹲蓄力 |
| 110000-192500 | CombatArt | 30 | 各流派技 |

**R1连段跳转流程**:
```
Idle ──[IA_Attack Tap]──→ Combo01 (帧0-35)
  ├─ [帧12-20, IA_Dodge] ──→ Dodge 动画
  ├─ [帧12-24, IA_Deflect] ──→ Deflect 判定
  ├─ [帧18-24, IA_Attack] ──→ Combo02 (帧0-38)
  │   ├─ [帧14-22, IA_Dodge] ──→ Dodge
  │   ├─ [帧14-26, IA_Deflect] ──→ Deflect
  │   └─ [帧20-26, IA_Attack] ──→ Combo03 (帧0-42)
  │       ├─ [帧16-30, IA_Deflect] ──→ Deflect
  │       └─ [帧22-30, IA_Attack] ──→ Combo04 (帧0-50)
  │           ├─ [帧18-35, IA_Deflect] ──→ Deflect
  │           └─ 无连段 (最后一段)
  └─ CancelWindow过期 → 回 Idle
```

### 6.3 Defense 防御/弹刀层

| AnimID范围 | 子类 | 动画数 | 说明 |
|-----------|------|--------|------|
| 200000-200121 | Guard | 4 | Idle/Raise/Lower/Lower_Fast |
| 201000-201610 | Parry/Deflect | 18 | Idle/Deflect/Raise/Lower/Success/Counter/Receive |
| 201200-201321 | Guard受击 | 11 | Heavy_Hit(各方向)/Break_Recover/Counter |
| 202000-202710 | 防御转换 | 10 | Parry↔Guard, Guard→Attack, Guard_Break |
| 205010-206120 | Mikiri/识破 | 6 | Counter/StepIn/Stab/Receive/Recover |
| 210000-216100 | 架势崩坏/横扫/抓取 | 14 | PostureBreak/Sweep/Grab |

**弹刀判定时间线**:
```
攻击方攻击动画:
  帧0────────[弹刀窗口Start]────[命中帧]────[弹刀窗口End]────────→
                  ↑12帧(0.2s)↑
  
防御方:
  IA_Deflect按下 ──→ 进入Parry_Idle
  ┌─ 弹刀窗口内按下 (且非Hold) ──→ Deflect_Success
  │  ├─ 播放火花VFX+SFX
  │  ├─ 攻击方 MicroStagger 0.15s
  │  └─ 防御方CancelWindow立即开启 → 可立刻反击
  │
  └─ 弹刀窗口外按下 或 Hold ──→ Guard (格挡, 有架势伤害)
```

### 6.4 Hit/Death/Resurrection 反应层

| AnimID范围 | 子类 | 动画数 | 优先级 |
|-----------|------|--------|--------|
| 250000-250006 | Hit_Light | 6 | 8 |
| 250010-250016 | Hit_Medium | 6 | 8 |
| 250030-250036 | Hit_Heavy | 6 | 8 |
| 250040-250046 | Hit_KnockBack | 6 | 8 |
| 250100-250110 | Hit_Stagger | 2 | 8 |
| 250200-250600 | Hit特殊 | 5 | 8 |
| 251000-252000 | Death | 12 | 9 |
| 259000-259010 | Resurrection | 2 | 10 |
| 220000-220022 | Deathblow | 13 | 10 |

**受击反应选择逻辑**:
```
伤害到达 → 计算击退值(KnockbackValue)
  ├─ KnockbackValue < 轻击阈值 ──→ Hit_Light (方向: 前/后/左/右)
  ├─ 轻击 < KnockbackValue < 重击 ──→ Hit_Medium
  ├─ KnockbackValue > 重击阈值 ──→ Hit_Heavy / KnockBack
  ├─ 特殊伤害类型 ──→ Hit_HeadShot / Hit_Poison / Hit_Terror
  └─ HP=0 ──→ Death (方向+类型变种)
      └─ 有回生点数 ──→ Resurrection → 恢复战斗
```

### 6.5 Prosthetic 义手层

| AnimID范围 | 义手种类 | 动画数 |
|-----------|---------|--------|
| 700000-700500 | 手里剑 (Shuriken) | 6 |
| 710000-710310 | 斧 (Axe) | 4 |
| 710400-710430 | 枪 (Spear) | 3 |
| 710500-710530 | 喷火筒 (FlameVent) | 3 |
| 710600-710900 | 伞 (Umbrella) | 4 |
| 711000-711020 | 锈丸 (Sabimaru) | 3 |
| 711100-711110 | 口哨 (Whistle) | 2 |
| 711200-711211 | 爆竹 (Firecracker) | 4 |
| 711300-711315 | 雾鸦 (MistRaven) | 5 |
| 711400 | 指哨 (FingerWhistle) | 1 |
| 711500-711510 | 神隐 (DivineAbduction) | 2 |

### 6.6 Grapple 钩绳层

| AnimID | 名称 | 说明 |
|--------|------|------|
| 790000 | Grapple_Start | 钩绳射出 |
| 790010 | Grapple_Fly | 飞行过程 |
| 790020 | Grapple_Land | 落地 |
| 790030-790130 | Grapple_* | 各种落地/攻击变种 |
| 790500 | Grapple_To_Ledge | 到崖边 |

---

## 7. 运行时Tick完整流程

```
每帧执行顺序:

  Character::Tick(DeltaTime)
  │
  ├─ 1. 输入系统: 读取玩家输入 → 写入 InputIntent + 移动向量
  │
  ├─ 2. MovementComponent::TickComponent
  │     └─ 计算 Velocity, Speed, Direction
  │
  ├─ 3. AnimInstance::NativeUpdateAnimation(DeltaTime)
  │     ├─ 从 Character 获取 Speed/Angle/MovementTier
  │     ├─ 通过反射写入 BlendSpace.X = Speed
  │     ├─ 获取 CurrentAnimID, CurrentAnimTime (从当前Montage)
  │     ├─ 读取 InputIntent
  │     ├─ 如果 InputIntent != None:
  │     │   └─ CanCancelTo(InputIntent, Crossfade)
  │     │       ├─ true → 设置 TransitionRequest 标志
  │     │       └─ false → 忽略
  │     └─ 更新 JumpTable 等价标志 (bCanDeflect等)
  │
  ├─ 4. ABP AnimGraph 评估
  │     ├─ Layer 1: BlendSpace 根据 Speed 混合
  │     ├─ Layer 2: Transition Rule 检查 TransitionRequest
  │     │   └─ true → Montage_Play(TargetAnim, Crossfade)
  │     └─ Layer 3: Transition Rule 检查 Hit/Death 标志
  │         └─ true → Montage_Play(ReactionMontage)
  │
  └─ 5. AnimNotify 触发 (在Montage帧边界)
        ├─ CancelWindowNotify → 设置/复位 取消窗口标志
        ├─ AttackHitboxNotify → 生成/销毁 攻击盒
        ├─ SetFlagNotify → 设置 bCanDeflect/bIsDodging
        ├─ SEventNotify → 触发音效
        ├─ FFXNotify → 触发视觉特效
        └─ FootstepNotify → 触发脚步声
```

---

## 8. 数据流总览图

```
┌─────────────┐    ┌──────────────┐    ┌──────────────────┐
│ 输入设备     │    │ Character    │    │ AnimInstance     │
│ (键鼠/手柄)  │    │ Movement     │    │ (C++)           │
└──────┬──────┘    └──────┬───────┘    └────────┬─────────┘
       │                  │                     │
       ▼                  ▼                     ▼
┌─────────────┐    ┌──────────────┐    ┌──────────────────┐
│EnhancedInput│    │ Velocity     │    │ Speed/Angle     │
│MappingCtx   │    │ Speed/Dir    │    │ InputIntent     │
│IA_* Inputs  │    │ MovementTier │    │ CurrentAnimID   │
└──────┬──────┘    └──────┬───────┘    │ CurrentAnimTime  │
       │                  │            └────────┬─────────┘
       │                  │                     │
       └──────────────────┼─────────────────────┘
                          │
                          ▼
              ┌──────────────────────┐
              │  AnimBlueprint (ABP) │
              │                      │
              │  ┌────────────────┐  │
              │  │ State Machine  │  │
              │  │ TransitionRules│  │
              │  └───────┬────────┘  │
              │          │           │
              │  ┌───────▼────────┐  │
              │  │ AnimSequence   │  │
              │  │ Montage        │  │
              │  │ BlendSpace     │  │
              │  └───────┬────────┘  │
              └──────────┼───────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │  AnimNotify 事件触发  │
              │                      │
              │  ┌────────────────┐  │
              │  │ AttackHitbox   │──→ 碰撞检测系统
              │  │ CancelWindow   │──→ CanCancelTo查询
              │  │ SetFlag        │──→ bCanDeflect等
              │  │ PlaySound      │──→ 音效系统
              │  │ SpawnFFX       │──→ 特效系统
              │  └────────────────┘  │
              └──────────────────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │ SK_AnimLogicData     │
              │ (DataAsset)          │
              │                      │
              │ CancelRules Map      │
              │ AttackHitboxConfigs  │
              │ SpEffectConfigs     │
              │ CategoryAnimMap     │
              └──────────────────────┘
```

---

## 9. 关键发现与待办事项

### 9.1 JumpTable ID 完整映射表 (共55个已知ID)

TAE数据中JumpTable事件的参数结构统一为: `{JumpTableID, ArgA, ArgB, ArgC, ArgD, StateInfo}`。
ArgA/B/C/D的具体含义因ID而异（速度值、角度、帧数、布尔标志等）。

当前 `SekiroTAEImporter::MapJumpTableToAction()` 只映射了 **22个** ID，但有 **55个** 不同的JumpTableID出现在数据中。

| 优先级 | JumpTableID | 频率 | TypeName | 推测功能 | 典型参数特征 |
|--------|------------|------|----------|---------|-------------|
| **P0** | **133** | 1,420 | JumpTable | 禁止特殊动作(义手/战技) | 标准参数 |
| **P0** | **134** | 1,398 | JumpTable | 禁止道具使用 | 标准参数 |
| **P0** | **7** | 1,378 | JumpTable | 禁止转向 (DisableTurning) | ✅已映射 |
| **P0** | **87** | 1,333 | JumpTable | 调用攻击行为 (InvokeAttack) | ✅已映射 |
| **P0** | **119** | 1,250 | JumpTable | 启用弹刀 (EnableParry) | ✅已映射 |
| **P0** | **115** | 1,217 | JumpTable | R1取消窗口结束 (CancelEnd_R1) | ✅已映射 |
| **P0** | **32** | 1,172 | JumpTable | 进入移动状态 | 标准参数 |
| **P0** | **31** | 1,158 | JumpTable | 退出移动状态(锁定步伐) | 标准参数 |
| **P0** | **118** | 1,129 | JumpTable | L2取消窗口结束 (CancelEnd_L2) | ✅已映射 |
| **P0** | **117** | 1,127 | JumpTable | L1取消窗口结束 (CancelEnd_L1) | ✅已映射 |
| **P0** | **26** | 1,096 | JumpTable | 通用取消窗口(非特定武器) | 标准参数 |
| **P1** | **137** | 1,091 | JumpTable | 禁止弹刀(与119互斥) | 标准参数 |
| **P1** | **154** | 1,049 | JumpTable | 道具使用开启窗口 | 标准参数 |
| **P1** | **11** | 999 | JumpTable | 切换动作层(HKS层控制) | 标准参数 |
| **P1** | **28** | 949 | JumpTable | 设置移动速度正常(1.0x) | 标准参数 |
| **P1** | **51** | 641 | JumpTable | 无敌帧(i-Frame)开关 | 标准参数 |
| P2 | 63 | 240 | JumpTable | 特殊动作标志 | 标准参数 |
| P2 | 50 | 227 | JumpTable | 动作限制(禁移动+转向) | 标准参数 |
| P2 | 55 | 212 | JumpTable | 硬直标志(Stagger) | 标准参数 |
| P2 | 65 | 191 | JumpTable | 追踪目标(LookAt) | 标准参数 |
| P2 | 56 | 143 | JumpTable | 受击硬直恢复标志 | 标准参数 |
| P2 | 3 | 139 | JumpTable | 转向速度设置 | ArgB=90(角度?) |
| P2 | 72 | 133 | JumpTable | 移动速度倍率 | ArgA=0.09(倍率?) |
| P2 | 141 | 132 | JumpTable | 取消窗口标记(变种) | 标准参数 |
| P3 | 148 | 121 | JumpTable | 高度修正相关 | 标准参数 |
| P3 | 145 | 119 | JumpTable | 弹刀窗口变种 | 标准参数 |
| P3 | 27 | 113 | JumpTable | 设置无重力(SetNoGravity) | ✅已映射 |
| P3 | 39 | 107 | JumpTable | 锁定目标更新 | 标准参数 |
| P3 | 16 | 82 | JumpTable | 动画混合标志 | 标准参数 |
| P3 | 19 | 77 | JumpTable | 禁地图碰撞(DisableMapHit) | ✅已映射 |
| P3 | 151 | 77 | JumpTable | 受伤判定标志 | 标准参数 |
| P4 | 143 | 71 | JumpTable | 体力消耗相关 | 标准参数 |
| P4 | 128 | 65 | JumpTable | 特殊效果触发 | 标准参数 |
| P4 | 25 | 64 | JumpTable | Dodge取消窗口开始 | ✅已映射 |
| P4 | 127 | 55 | JumpTable | 霸体/钢体标志 | 标准参数 |
| P4 | 146 | 41 | JumpTable | 投技相关标志 | 标准参数 |
| P4 | 5 | 40 | JumpTable | 启用弹刀(早期版本?) | ✅已映射为EnableParry |
| P4 | 150 | 38 | JumpTable | 角度/朝向修正 | 标准参数 |
| P5 | 136 | 29 | JumpTable | 移动输入禁用 | 标准参数 |
| P5 | 69 | 29 | JumpTable | 特殊动画层控制 | 标准参数 |
| P5 | 95 | 28 | JumpTable | 动作缓存相关 | 标准参数 |
| P5 | 126 | 26 | JumpTable | 行为标记(BehaviorFlag) | 标准参数 |
| P5 | 24 | 20 | JumpTable | 防御相关标志 | 标准参数 |
| P5 | 90 | 16 | JumpTable | 限制移动速度=步行 | ✅已映射 |
| P6 | 149 | 15 | JumpTable | 特效触发相关 | 标准参数 |
| P6 | 155 | 14 | JumpTable | 弹反窗口变种 | 标准参数 |
| P6 | 110 | 14 | JumpTable | 投技被抓住标志 | 标准参数 |
| P6 | 54 | 14 | JumpTable | 防御架势崩坏 | 标准参数 |
| P6 | 140 | 13 | JumpTable | 忍杀相关标志 | 标准参数 |
| P6 | 157 | 13 | JumpTable | 防御状态结束 | 标准参数 |
| P7 | 12 | 12 | JumpTable | 调用死亡(InvokeDeath) | ✅已映射 |
| P7 | 132 | 4 | JumpTable | 特殊音效触发 | 标准参数 |
| P7 | 113 | 3 | JumpTable | 设置高度修正 | ✅已映射 |
| P7 | 156 | 2 | JumpTable | 特殊触发(稀有) | 标准参数 |
| P7 | 125 | 1 | JumpTable | 特殊动作(极稀有) | 标准参数 |
| P7 | 158 | 1 | JumpTable | 特殊触发(极稀有) | 标准参数 |

> **注**: "标准参数" = `{ArgA: 0, ArgB: -1, ArgC: 6, ArgD: 0, StateInfo: 0}`，这是大部分JumpTable事件的默认参数模式。
> ArgC=6可能表示"应用于当前角色"的作用域，ArgB=-1可能表示"无效/默认"。

### 9.2 已确认的关键AnimID映射修正

| AnimID | 旧名称 | 正确名称 | 证据 |
|--------|--------|---------|------|
| 300 | Sprint_Fwd (❌) | **StepDodge_Dash** (垫步非循环) | 含JT=28 + SetTurnSpeed=0, 非循环移动 |
| 400 | Run_Fast_Fwd (✓) | **Run_Fast_Fwd** (真奔跑循环) | 循环动画, 无JT事件 |

### 9.3 动画系统健康状态

| 模块 | 状态 | 完成度 |
|------|------|--------|
| Locomotion BlendSpace | ⚠️ 1D完成, 待升2D | 60% |
| Combat 状态机 | ❌ 未开始 | 0% |
| Defense 状态机 | ❌ 未开始 | 0% |
| Reaction 状态机 | ❌ 未开始 | 0% |
| AnimNotify注入 | ❌ 未开始 | 0% |
| DataAsset 完善 | ⚠️ 结构定义完成 | 20% |
| AnimID 映射 | ⚠️ 200+已命名, 待扩展 | 15% |
| JumpTable 映射 | ⚠️ 22/70+ 已映射 | 31% |
