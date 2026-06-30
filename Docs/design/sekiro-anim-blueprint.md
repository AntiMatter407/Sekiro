# Sekiro AnimBlueprint — 需求 + 技术方案

> 更新日期：2026-06-30
> 关联文档：[Docs/animation_annotation.md](animation_annotation.md)（动画资产清单，随资产导入持续更新）
> 架构原则：C++ 只提供通用接口（UPROPERTY/UFUNCTION），ABP 蓝图负责编排

---

## 第一部分：需求

### 1.1 目标

基于已导入 UE5 的 Sekiro 动画资产（覆盖 `a000` ~ `a250`），构建一套完整的角色动画蓝图系统，实现原版只狼的动画行为：

- **Locomotion**：Idle / Walk / Jog / Run / Sprint / Crouch / 悬挂 / 游泳 / 贴墙
- **Combat**：攻击 / 防御 / 弹刀 / 闪避 / 忍义手 / 战技
- **Reaction**：受击 / 死亡 / 回生
- **Interaction**：忍杀 / 交互 / 道具使用 / 窃听

### 1.2 动画资产映射

动画资产全部在 `Docs/animation_annotation.md` 中按 TAE 原始 ID 分组列出，此处按动画蓝图系统的分类方式汇总：

| 分类 | 来源（TAE 前缀 / 范围） | 方案动画名前缀 | 用途 |
|------|----------------------|--------------|------|
| 站立 Ground | a000: 0, 100-103, 400-403, 500-503, 1151-1154, 1200 | `_Walk_*` / `_Jog_*` / `_Run_*` / `_Sprint_*` | 地面移动循环 |
| 过渡 Transition | a000: 10-13, 300-303, 600-603, 1402-1403, 1510-1512 | `_*_Stop` / `_*_To_*` | 起停/变速过渡 |
| 转向 Turn | a000: 132-133, 432-433, 442-443 | `_TurnL` / `_TurnR` | 急转 |
| 蹲姿 Crouch | a000: 5000-5603 | `_Crouch_*` | 蹲行/蹲跑 |
| 悬挂 Hang | a000: 20000-20433 | `_Hang_*` | 悬挂边缘状态 |
| 贴墙 Wall | a000: 21000 开头 | `_Wall_*` | 贴墙状态 |
| 游泳 Swim | a000: 30000-41000 | `_Swim_*` | 水中移动 |
| 跳跃 Jump | a000: 200000 开头 | `_Jump_*` / `_Airborne_*` / `_Land_*` | 垂直机动 |
| 受击 Hit | a000: 100000 开头 + 190000 开头 | `_Hit_*` / `_Hit_State_*` | 伤害反馈 |
| 死亡/回生 | a000: 110000 开头 | `_Death_*` / `_Resurrection_*` | 死亡循环 |
| 翻滚 Roll | a000: 206000 | `_Roll` | 翻滚规避 |
| 战斗姿态 | a050 | `_Guard_*` / `_Deflect_*` / `_AttackPose_*` | 战斗核心动作 |
| 忍义手 | a070 ~ a079 | `_Prosthetic_*` | 义手技能 |
| 战技 | a100 ~ a110 | `_CombatArt_*` | 流派技能 |
| 互动 Interact | a200 ~ a250 | `_Deathblow_*` / `_MikiriCounter_*` / `_BossSync_*` | 忍杀/识破 |
| 道具道具 | a000: 250000 开头 | `_ItemUse_*` | 使用道具 |
| 窃听 Eavesdrop | a000: 220000 开头 | `_Eavesdrop_*` | 窃听 |

### 1.3 分阶段路线

| 阶段 | 内容 | 核心动画 |
|:----:|------|---------|
| **1** | 站立 Locomotion（Idle / Walk / Jog / Run / Sprint） | Walk_*, Jog_*, Run_*, Sprint_*, Idle_* |
| **2** | 过渡 + 转向 | Walk_Stop, Run_Stop, Sprint_To_*, Walk_Turn, Jog_Turn |
| **3** | Jump / Air / Land | Jump_*, Airborne_*, Land_* |
| **4** | Crouch 蹲姿 | Crouch_* |
| **5** | Combat（Attack / Guard / Deflect / Dodge） | Guard_*, Deflect_*, AttackPose_*, Quickstep_*, StepDodge_* |
| **6** | Prosthetic 忍义手 / CombatArt 战技 | Prosthetic_*, CombatArt_* |
| **7** | Reaction（Hit / Death / Resurrection） | Hit_*, Death_*, Resurrection_* |
| **8** | Interaction（Deathblow / Interact / Eavesdrop） | Deathblow_*, Interact_*, Eavesdrop_* |
| **9** | 特殊状态（Hang / Swim / Wall / Roll） | Hang_*, Swim_*, Wall_*, Roll |
| **10** | ItemUse / 道具 | ItemUse_* |

---

## 第二部分：技术方案

### 2.1 整体架构

```
ABP_Sekiro (AnimGraph)
│
├── [Layer 0]  Locomotion ─── 始终运行，优先级 0
│   ├── GroundMoveSM  ──── 地面移动（站立 + Crouch）
│   ├── AirSM  ──────────── 空中（Jump / FreeFall / Land）
│   ├── HangSM ──────────── 悬挂
│   ├── SwimSM ──────────── 游泳
│   └── WallSM ──────────── 贴墙
│
├── [Layer 1]  UpperBody ── 腰以上覆盖，优先级 2
│   ├── Guard / Deflect
│   ├── Attack
│   ├── Prosthetic
│   ├── CombatArt
│   └── ItemUse
│
├── [Layer 2]  FullBody ─── 全身覆盖，优先级 5
│   ├── Dodge / Quickstep / StepDodge
│   ├── SprintAttack
│   ├── Hit
│   └── Roll
│
└── [Layer 3]  Cinematic ── 完全覆盖，优先级 8
    ├── Death / Resurrection
    ├── Deathblow
    ├── Interact
    └── Eavesdrop
```

### 2.2 C++ 接口

`USKAnimInstance` 已暴露以下变量直接给 ABP 蓝图读取：

| UPROPERTY | 类型 | 说明 |
|-----------|------|------|
| Speed | float | Velocity.Size2D() |
| Angle | float | -180°~180° 移动方向角 |
| MovementTier | ESKMovementTier | Idle / Walk / Jog / Run / Sprint / Crouch |
| Direction | ESKLocomotionDirection | 8 方向：Fwd, Fwd_L, L, Bwd_L, Bwd, Bwd_R, R, Fwd_R |
| bIsInAir | bool | IsFalling() |
| bIsCrouching | bool | bIsCrouched |
| bIsDodging | bool | 闪避中 |
| DodgeDirection | float | 闪避角度 |
| InputIntent | FName | 当前输入意图 |

`USKAnimationController` 提供运行时状态机 + 优先级系统（已实现，不改）。

### 2.3 Phase 1：站立 Locomotion 详细设计

#### 子状态机

```
GroundMoveSM (bIsCrouching=false)
│
├── Idle ──── Speed < 30 ──── SequencePlayer(Idle_Default)
│
├── Walk ──── Speed 30~250 ── 2D BlendSpace
│   ├── X: Speed (±50, 中心 150)
│   └── Y: Direction (-180°~180°)
│       0°=Walk_Fwd, 180°=Walk_Bwd, -90°=Walk_L, 90°=Walk_R
│
├── Jog ───── Speed 250~450 ── 2D BlendSpace
│   ├── X: Speed (±50, 中心 350)
│   └── Y: Direction (-180°~180°)
│       0°=Jog_Fwd, -45°=Jog_Fwd_L, 45°=Jog_Fwd_R
│
├── Run ───── Speed 450~600 ── 2D BlendSpace
│   ├── X: Speed (±50, 中心 500)
│   └── Y: Direction (-180°~180°)
│       0°=Run_Fwd, 180°=Run_Bwd, -90°=Run_L, 90°=Run_R
│
└── Sprint ── Speed 600+ ──── 2D BlendSpace
    ├── X: Speed (±50, 中心 600)
    └── Y: Direction (-180°~180°)
        0°=Sprint_Fwd, -45°=Sprint_Fwd_L, 45°=Sprint_Fwd_R
```

#### 状态过渡

| 切换 | 条件 | 方式 |
|------|------|------|
| Idle↔Walk | Speed 30 / 20 | InertialBlend 0.2s |
| Walk↔Jog | Speed 250 / 200 | InertialBlend 0.25s |
| Jog↔Run | Speed 450 / 400 | InertialBlend 0.3s |
| Run↔Sprint | Speed 600 / 400 | InertialBlend 0.2s |
| Walk→Idle 停止 | Speed < 20 | Transition（播对应方向 Stop） |
| Run→Idle 停止 | Speed < 20 | Transition（播对应方向 Stop） |
| Sprint→Idle 停止 | Speed < 20 | Transition（播对应方向 Stop） |
| 降级过渡（Run→Jog 等） | Speed 低于阈值 | Transition（播过渡动画） |

#### Idle 变体

| 条件 | 动画 | 说明 |
|------|------|------|
| 非战斗 | `Idle_Default` | 默认站立待机 |
| 拔刀 | `Idle_WeaponOut` | 含 _L/_R/_Shift 重心偏移变体 |
| 战斗态 | `Idle_Combat` | 含 _L/_R/_Shift 变体 |
| 收刀 | `Idle_WeaponSheathe` | 单次过渡，播完回 Default |

#### 过渡动画映射

| 方向 | Walk→Stop | Run→Stop | Sprint→Stop |
|------|-----------|----------|-------------|
| Fwd | `Walk_Fwd_Stop` | `Run_Fwd_Stop` | `Sprint_Fwd_Stop` |
| Bwd | `Walk_Bwd_Stop` | `Run_Bwd_Stop` | `Sprint_Bwd_Stop` |
| L | `Walk_L_Stop` | `Run_L_Stop` | — |
| R | `Walk_R_Stop` | `Run_R_Stop` | — |

| 降级 | 动画 |
|------|------|
| Sprint→Run | `Sprint_To_Run` |
| Sprint→Jog | `Sprint_To_Jog` |
| Sprint→Idle | `Sprint_To_Idle` |
| Run→Jog | `Run_To_Jog` |
| Run→Walk | `Run_To_Walk` |
| Run→Idle | `Run_To_Idle` |
| Walk→Idle(急停转身) | `Walk_Stop_Turn` |

#### 转向

| 当前状态 | 左转 | 右转 | 触发条件 |
|---------|------|------|---------|
| Walk | `Walk_TurnL` | `Walk_TurnR` | Angle 突变 > 90° |
| Jog | `Jog_TurnL` | `Jog_TurnR` | Angle 突变 > 90° |

转向动画与 BlendSpace 交叉淡化 0.15s。

### 2.4 Phase 2：过渡 + 转向（Phase 1 子集）

同 Phase 1 过渡规则实现，以过渡动画和 Transition Node 替代 InertialBlend。

### 2.5 Phase 3：Jump / Air / Land

```
AirSM (bIsInAir=true)
│
├── JumpStart ──── Jump_*(起跳)
├── FreeFall ───── Airborne_*(空中)
└── Land ───────── Land_*(落地)
```

- `bIsInAir` 上升沿：进入 AirSM，播 JumpStart
- `bIsInAir` 为 true 且非上升沿：FreeFall 循环
- `bIsInAir` 下降沿：播 Land，完成后回 GroundMoveSM

### 2.6 Phase 4：Crouch

```
CrouchSM (bIsCrouching=true)
│
├── CrouchIdle ──── Idle 变体 → Crouch_Idle(5000)
├── CrouchWalk ──── 2D BlendSpace(Crouch_WalkLoop_Fwd/Bwd/L/R)
├── CrouchRun ───── 2D BlendSpace(Crouch_RunLoop_Fwd/Bwd/L/R)
├── 过渡 ────────── Crouch_ToWalk_* / Crouch_WalkStart_* / Crouch_RunStart_*
└── 停止 ────────── Crouch_WalkToIdle_*
```

bIsCrouching 变化时 Transition 0.15s，各方向 4 方向 BlendSpace。

### 2.7 Phase 5：Combat

```
Layer 1 (UpperBody) — Slot: UpperBodySlot, BlendPerBone(Spine+)
│
├── Guard ──── Hold 防御，使用 Guard_*
├── Deflect ── 弹刀窗口内触发，使用 Deflect_*
├── Attack ─── 连段攻击，使用 AttackPose_*
│
Layer 2 (FullBody)
├── Dodge ──── Quickstep_Fwd/Bwd/L/R + StepDodge_Fwd/Bwd/L/R/Dash
└── SprintAttack ── Sprint_R1 / Sprint_Thrust
```

动作通过 `USKAnimationController::TryPlayAction` 触发 Montage，由 `Montage_Play` 播放在对应 Slot。

### 2.8 Phase 6~10 概要

后续各阶段复用 Layer 结构，优先级叠层机制一致：
- **Prosthetic / CombatArt** → UpperBody Slot
- **Hit / Roll** → FullBody Slot
- **Death / Deathblow / Interact** → Cinematic Slot
- **Hang / Swim / Wall** → 替代 Locomotion 层整层

### 2.9 数据流总图

```
每帧 NativeUpdateAnimation():
  Speed = Velocity.Size2D()
  Angle = CalculateDirection(Velocity, ActorRotation)
  Direction = 量化 8 方向 (基于移动输入)
  MovementTier = MovementComponent->CurrentMovementTier
  bIsInAir = MovementComponent->IsFalling()
  bIsCrouching = Character->bIsCrouched
          │
          ▼
ABP AnimGraph:
  1. StateMachine 选择子状态（Idle/Walk/Jog/Run/Sprint/Crouch/Air）
  2. BlendSpace 根据 Speed + Direction 插值
  3. Transition 处理停止/转向/速度变化
  4. Slot 系统处理上层覆盖（Combat/Action/Cinematic）
          │
          ▼
  Final Pose → Mesh
```

---

## 第三部分：实施计划

### 3.1 Phase 1 实施子任务

| # | 任务 | 预计工作量 |
|---|------|-----------|
| 1.1 | 在 ABP_Sekiro 中创建 GroundMoveSM 5 个子状态 | 蓝图 1h |
| 1.2 | 创建 5 个 2D BlendSpace 资产（Walk/Jog/Run/Sprint/Idle） | 蓝图 1.5h |
| 1.3 | BlendsSpace 采样点配置 + 动画绑定 | 蓝图 0.5h |
| 1.4 | Speed 阈值 Transition Rule：Idle↔Walk↔Jog↔Run↔Sprint | 蓝图 1.5h |
| 1.5 | 过渡动画映射：Stop 方向动画 + Speed 降级过渡 | 蓝图 1h |
| 1.6 | 转向层：Walk_Turn / Jog_Turn | 蓝图 0.5h |
| 1.7 | Idle 变体切换（非战斗/拔刀/战斗态） | 蓝图 0.5h |
| 1.8 | PIE 全链路测试 | 测试 1h |

### 3.2 涉及文件

| 文件 | 操作 |
|------|------|
| `Docs/animation_annotation.md` | ✅ 已有，持续更新 |
| `Docs/design/sekiro-anim-blueprint.md` | ✅ 本文 |
| `Content/Characters/Sekiro/ABP_Sekiro.uasset` | ✅ 主要修改 |
| `Content/Characters/Sekiro/SK_Locomotion_BS` | 可能需要重建为多个 2D BS |
| `Source/Sekiro/Animation/SKAnimInstance.h/.cpp` ⬜ 无需修改 |
| `Source/Sekiro/Movement/SKMovementComponent.h/.cpp` ⬜ 无需修改 |

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-28 | 创建；ALS 参考架构 + Phase 1 Locomotion 设计 |
| 2026-06-30 | 重写；对齐最新 animation_annotation.md 动画资产清单，统一命名，10 阶段路线 |
