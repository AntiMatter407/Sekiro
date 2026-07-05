# Sekiro AnimBlueprint — 技术方案

> 更新日期：2026-07-02
> 关联文档：
> - [进度文档](../plan/sekiro-anim-blueprint.md)
> - [动画资产清单](../animation_annotation.md)（所有动画资源定义在此）
>
> 架构原则：C++ 只提供通用接口（UPROPERTY/UFUNCTION），ABP 蓝图负责编排

---

## 第一部分：需求

### 1.1 目标

基于已导入 UE5 的 Sekiro 动画资产（覆盖 `a000` ~ `a250`），构建一套完整的角色动画蓝图系统，实现原版只狼的动画行为。资产路径：`/Game/Characters/Sekiro/Animations/Anim_Sekiro_*`，命名格式为 `Anim_Sekiro_{prefix}_{6位ID}`。

需要覆盖的状态：

| 分类 | 状态 | 优先级层 |
|------|------|:--------:|
| 地面 Locomotion | Idle / Walk / Jog / Run / Sprint | Layer 0 |
| 空中 | JumpStart / FreeFall / Land | Layer 0 |
| 蹲姿 | CrouchIdle / CrouchWalk / CrouchRun | Layer 0 |
| 悬挂 | Hang_Idle / Hang_Move | Layer 0 替代 |
| 游泳 | Swim_* / Dive_* | Layer 0 替代 |
| 贴墙 | Wall_* | Layer 0 替代 |
| 战斗 | Guard / Deflect / Attack | Layer 1 (UpperBody) |
| 闪避 | Dodge / Quickstep | Layer 2 (FullBody) |
| 忍义手 | Prosthetic | Layer 1 (UpperBody) |
| 战技 | CombatArt | Layer 1 (UpperBody) |
| 受击 | Hit | Layer 2 (FullBody) |
| 死亡/回生 | Death / Resurrection | Layer 3 (Cinematic) |
| 互动 | Deathblow / Interact / Eavesdrop | Layer 3 (Cinematic) |
| 道具 | ItemUse | Layer 1 (UpperBody) |

---

## 第二部分：基于动画资源的技术方案

### 2.1 动画资源分析

Locomotion 核心动画分布：

| Tier | TAE ID | 方向覆盖 | 原始姿态 | 转向动画 | 停止动画 | 过渡动画 |
|:----:|:------:|:---------:|:--------:|:--------:|:--------:|:--------:|
| **Idle** | 0 | 单方向 | 站立待机 | — | — | — |
| **Walk** | 100-103 | **4方向** (F/B/L/R) | 走路 | 132-133 (TurnL/R) | 300-303 Stop + 10-13 | — |
| **Jog** | 400-403 | **3方向** (F+FL+FR) | 小跑 | 432-433/442-443 (TurnL/R) | — | — |
| **Run** | 500-503 | **4方向** (F/B/L/R) | 跑 | — | 600-603 Stop | — |
| **Sprint** | 1151-1154 | **3方向** (F+FL+FR) + 1200 Loop | 冲刺 | — | 1402-1403 Stop | 1510-1512 Sprint→Idle/Jog/Run |

**关键观察**：
1. Walk 的循环动画是 200-203（WalkAlt），100-103 **不是 Walk 循环而是 Idle 起步到走路的过渡**
2. Run 是完整 4 方向，Jog 和 Sprint **缺少后向和侧向**动画
3. Idle 只有单方向 + 等待循环
3. 转向动画：Walk_TurnL/R (132-133)、Jog_TurnL/R (432-433, 442-443)
4. 停止动画：Walk_Stop (300-303)、Run_Stop (600-603)、Sprint_Stop (1402-1403)
5. 速度降级过渡：Sprint_To_Idle/Jog/Run (1510-1512)

### 2.2 BlendSpace 设计方案

基于上述资源分析，Locomotion **不适合使用单一 2D BlendSpace 覆盖所有 Tier**，因为：

- Jog 和 Sprint 缺少后向/侧向动画，单一 2D BS 会产生空洞
- 每个 Tier 的方向数不一致，强行统一会浪费采样点或产生无效插值

最佳方案是 **每个 Tier 一个独立的 1D Direction BlendSpace**，GroundMoveSM 保留 5 状态机架构：

```
GroundMoveSM (状态机)
│
├── Idle ──── Speed < 30 ──── SequencePlayer(Anim_Sekiro_a000_000000)
│                             仅单方向原地待机
│
├── Walk ──── Speed 30~250 ── 1D BlendSpace(BS_Walk_Direction)
│                             轴：Direction (-180°~180°)
│                             采样：0°=Fwd, 90°=R, -90°=L, 180°=Bwd
│                             资源：200 Fwd, 201 Bwd, 202 L, 203 R (WalkAlt 循环)
│                                    100-103 是 Idle_ToWalk 起步过渡，不在 BS 内
│
├── Jog ───── Speed 250~450 ── 1D BlendSpace(BS_Jog_Direction)
│                             轴：Direction (-180°~180°)
│                             采样：0°=Fwd, -45°=Fwd_L, 45°=Fwd_R
│                             资源：400 Fwd, 401 Fwd_L, 402 Fwd_R
│                             缺少后向/侧向：回退方案 → 后向用 Fwd 镜像或 StateMachine 禁止后向 Jog
│
├── Run ───── Speed 450~600 ── 1D BlendSpace(BS_Run_Direction)
│                             轴：Direction (-180°~180°)
│                             采样：0°=Fwd, 90°=R, -90°=L, 180°=Bwd
│                             资源：500 Fwd, 501 Bwd, 502 L, 503 R
│
└── Sprint ─ Speed 600+ ──── 1D BlendSpace(BS_Sprint_Direction)
                             轴：Direction (-180°~180°)
                             采样：0°=Fwd + Loop, -45°=Fwd_L, 45°=Fwd_R
                             资源：1151 Fwd, 1152 Fwd_L, 1153 Fwd_R, 1200 Loop
                             注意：Sprint_Loop(1200) 可能是循环态 → 可以作为主播放序列
                                   1151-1154 作为起跑过渡
                             缺少后向/侧向：Sprint 状态下禁止后向/侧向移动，强制转向
```

#### 为什么用 1D Direction BS 而不是 2D Speed×Direction

| 方案 | 资产数 | ABP状态数 | 空洞问题 | 维护复杂度 |
|:----:|:------:|:---------:|:--------:|:---------:|
| 1个 2D BS (Speed×Direction) | 1 | 1 | ⚠️ Jog/Sprint 方向不全产生空洞 | 低 |
| 4个 1D Direction BS + 5状态 | 4 | 5 | ✅ 每个 BS 只放实际有的方向 | 中 |
| 2个 2D BS（Walk/Run一组，Jog/Sprint一组） | 2 | 2 | ⚠️ 仍需处理方向缺失 | 中 |

**结论**：4个 1D Direction BS + 5 状态机，因为：
1. 每个 BS 只放实际存在的动画样本点，不会产生空洞
2. Idle 单独用 Sequence Player，性能更好
3. 每个 Tier 的 Transition Rule 独立控制，灵活调整
4. 后期加入 Crouch/Hang/Swim 等状态时，架构一致

### 2.3 整体 ABP 层级架构

```
ABP_Sekiro (AnimGraph)
│
├── [Layer 0]  Locomotion ─── 优先级 0，始终运行，通过 Blend Poses by Bool 切换
│   │
│   ├── bIsInAir=true → AirSM
│   │   ├── JumpStart ── 起跳（触发式，播完过渡到 FreeFall）
│   │   ├── FreeFall ─── 空中循环
│   │   └── Land ─────── 落地（触发式，播完回 GroundSM）
│   │
│   ├── bIsCrouching=true → CrouchSM
│   │   ├── CrouchIdle ── Crouch_Idle(5000)
│   │   ├── CrouchWalk ── 1D BS (CrouchWalk_* 5200-5203)
│   │   └── CrouchRun ─── 1D BS (CrouchRun_* 5500-5503)
│   │
│   └── else → GroundMoveSM（主要状态机）
│       ├── Idle
│       ├── Walk ─── 1D BS: BS_Walk_Direction
│       ├── Jog ──── 1D BS: BS_Jog_Direction
│       ├── Run ──── 1D BS: BS_Run_Direction
│       └── Sprint ─ 1D BS: BS_Sprint_Direction
│
├── [Layer 1]  UpperBody ─── Slot: UBSlot, Blend Per Bone (Spine+)
│   ├── Guard ──── Montage (a050)
│   ├── Deflect ── Montage (a050)
│   ├── Attack ─── Montage (a050/a100)
│   ├── Prosthetic ─ Montage (a070)
│   ├── CombatArt ── Montage (a100-110)
│   └── ItemUse ──── Montage (250000)
│
├── [Layer 2]  FullBody ──── Slot: FBSlot, 全身覆盖
│   ├── Dodge ───── Montage
│   ├── Hit ─────── Montage (100000/190000)
│   └── Roll ────── Montage (206000)
│
└── [Layer 3]  Cinematic ─── Slot: CineSlot, 完全覆盖
    ├── Death ─────── Montage (110000)
    ├── Resurrection ─ Montage
    ├── Deathblow ──── Montage (a200)
    └── Interact ───── Montage (700000)
```

### 2.4 C++ 接口

已完成，无需修改。

`USKAnimInstance` 已暴露给 ABP 蓝图：

| UPROPERTY | 类型 | 来源 | 用途 |
|-----------|------|------|------|
| Speed | float | Velocity.Size2D() | BlendSpace 状态切换 |
| Angle | float | -180°~180° | 方向混合 |
| MovementTier | ESKMovementTier | Idle/Walk/Jog/Run/Sprint/Crouch | 状态选择 |
| Direction | ESKLocomotionDirection | 8 方向枚举 | 蓝图逻辑 |
| bIsInAir | bool | MovementComponent->IsFalling() | 空中/地面切换 |
| bIsCrouching | bool | Character->bIsCrouched | 蹲姿/站立切换 |
| InputIntent | FName | 当前输入意图 | 触发动作 |

### 2.5 GroundMoveSM 详细设计

#### 2.5.1 Idle 变体

| 变体 | 动画资源 | 切换方式 |
|:----:|:---------|:---------|
| 默认 | `Anim_Sekiro_a000_000000` | 常规待机 |
| 拔刀 | 需确认是否有独立动画 | C++ InputIntent 或战斗标志 |
| 战斗态 | 同上 | bIsLockedOn 或 bInCombat |

#### 2.5.2 Speed 阈值与 Transition Rule

状态切换统一用 Rule Evaluator，基于 Speed 比较。

Phase 1 默认与当前 C++ 运行时 `USKAnimationController::EvaluateLocomotionState` 对齐：Idle `<10`、Walk `<200`、Jog `<400`、Run `<600`、Sprint `>=600`。如后续要调手感，应同步调整 C++、ABP Transition Rule 与 PIE 验证脚本。

| 过渡 | 条件（Speed） | 过渡方式 |
|:----:|:-------------:|:--------:|
| Idle → Walk | Speed >= 10 | 过渡动画(Idle_ToWalk_Fwd/Bwd/L/R, 100-103) 0.15s |
| Walk → Idle | Speed < 10 | 过渡动画(Walk_Stop) 0.15s |
| Walk → Jog | Speed >= 200 | InertialBlend 0.25s |
| Jog → Walk | Speed < 200 | InertialBlend 0.25s |
| Jog → Run | Speed >= 400 | InertialBlend 0.3s |
| Run → Jog | Speed < 400 | InertialBlend 0.3s |
| Run → Sprint | Speed >= 600 | InertialBlend 0.2s |
| Sprint → Run | Speed < 600 | 过渡动画(Sprint_To_Run) 0.2s |
| Sprint → Idle(direct) | Speed < 10 | 过渡动画(Sprint_To_Idle) 0.2s |
| Run → Idle(direct) | Speed < 10 | 过渡动画(Run_Stop) 0.15s |
| Walk → Idle(direct) | Speed < 10 | 过渡动画(Walk_Stop) 0.15s |

#### 2.5.3 过渡动画映射

过渡动画：

| 场景 | 前向 | 后向 | 左 | 右 |
|:----:|:----:|:----:|:--:|:--:|
| Idle→Walk 起步 | Idle_ToWalk_Fwd (100) | Idle_ToWalk_Bwd (101) | Idle_ToWalk_L (102) | Idle_ToWalk_R (103) |
| Walk→Idle 停止 | Walk_Fwd_Stop (300) | Walk_Bwd_Stop (303) | Walk_L_Stop (302) | Walk_R_Stop (301) |
| Run→Idle 停止 | Run_Fwd_Stop (600) | Run_Bwd_Stop (603) | Run_L_Stop (602) | Run_R_Stop (601) |
| Sprint→Idle 停止 | Sprint_Fwd_Stop (1402) | Sprint_Bwd_Stop (1403) | — | — |

降级过渡（Sprint → 低速状态）：

| 过渡 | 动画 |
|:----:|:----|
| Sprint → Idle | `Sprint_To_Idle` (1510) |
| Sprint → Jog | `Sprint_To_Jog` (1511) |
| Sprint → Run | `Sprint_To_Run` (1512) |

#### 2.5.4 转向层

| 当前状态 | 左转 | 右转 | 触发条件 |
|:--------:|:----:|:----:|:--------:|
| Walk | Walk_TurnL (132) | Walk_TurnR (133) | Angle 突变 > 90° |
| Jog | Jog_TurnL (432) / (442) | Jog_TurnR (433) / (443) | Angle 突变 > 90° |

转向动画通过 `Transition Node` 与当前 BlendSpace 交叉淡化 0.15s。

### 2.6 AirSM 详细设计

| 状态 | 动画资源 | 备注 |
|:----:|:---------|:----:|
| JumpStart | `Anim_Sekiro_Jump_*` (200000开头) | 起跳，播完切 FreeFall |
| FreeFall | `Anim_Sekiro_Airborne_*` (200000开头) | 空中循环 |
| Land | `Anim_Sekiro_Land_*` (200000开头) | 落地，播完回 GroundMoveSM |

### 2.7 CrouchSM 详细设计

| 状态 | BlendSpace | 动画资源 |
|:----:|:----------:|:---------|
| CrouchIdle | 无（单动画） | Crouch_Idle (5000) |
| CrouchWalk | 1D Direction BS | CrouchWalk_Fwd/Bwd/L/R (5200-5203) |
| CrouchRun | 1D Direction BS | CrouchRun_Fwd/Bwd/L/R (5500-5503) |

Crouch 过渡动画：Crouch_ToWalk (5010-5013)、Crouch_WalkStart (5100-5103)、Crouch_WalkToIdle (5300-5303)、Crouch_RunStart (5400-5403)、Crouch_Turn (5600-5603)

### 2.8 Combat Layer（Phase 2+ 范围）

所有 Combat 动作通过 `Slot + AnimMontage` 系统播放，`USKAnimationController::TryPlayAction` 触发 Montage。

Layer 1 UpperBody（腰以上覆盖）：
- **Guard**: 防御姿态 Hold，`Anim_Sekiro_Guard_*` (a050 0开头)
- **Deflect**: 弹刀，`Anim_Sekiro_Deflect_*` (a050 2开头)
- **Attack**: 连段攻击，`Anim_Sekiro_AttackPose_*` (a050 3开头)
- **Prosthetic**: 忍义手动画（a070 开头，含多种类）
- **CombatArt**: 战技动画（a100-110，11种）
- **ItemUse**: 使用道具（250000开头）

Layer 2 FullBody（全身覆盖）：
- **Dodge**: 垫步闪避
- **Hit**: 受击反馈 `Anim_Sekiro_Hit_*`
- **Roll**: 翻滚 `Anim_Sekiro_Roll`

Layer 3 Cinematic（完全覆盖）：
- **Death**: 死亡动画 `Anim_Sekiro_Death_*`
- **Resurrection**: 回生动画
- **Deathblow**: 忍杀 `Anim_Sekiro_Deathblow_*`
- **Interact**: 交互 `Anim_Sekiro_Interact_*`

### 2.9 数据流总图

```
NativeUpdateAnimation() (C++ 每帧更新):
  Speed = Velocity.Size2D()
  Angle = CalculateDirection(Velocity, ActorRotation)
  Direction = 量化 8 方向
  MovementTier = MovementComponent->CurrentMovementTier
  bIsInAir = MovementComponent->IsFalling()
  bIsCrouching = Character->bIsCrouched
          │
          ▼
ABP AnimGraph:
  1. bIsInAir → AirSM | bIsCrouching → CrouchSM | else → GroundMoveSM
  2. GroundMoveSM: Speed 阈值选 Idle/Walk/Jog/Run/Sprint 状态
  3. 每个状态内: 1D BlendSpace(Direction) 混合方向
  4. Transition: 停止/转向/降级过渡播过渡动画
  5. Slot系统: UpperBody/FullBody/Cinematic 叠层覆盖
          │
          ▼
  Final Pose → SkeletalMesh
```

---

## 第三部分：实施计划

### 3.1 分阶段路线

| 阶段 | 内容 | 核心动画资源 | 状态 |
|:----:|------|:------------:|:----:|
| **1a** | GroundMoveSM 基础状态 | Idle/Walk/Jog/Run/Sprint 循环 | ⬜ |
| **1b** | BlendSpace 创建 + 采样绑定 | 各方向循环动画绑定到 1D BS | ⬜ |
| **1c** | Speed 阈值 Transition Rule | Idle↔Walk↔Jog↔Run↔Sprint | ⬜ |
| **1d** | 停止/降级过渡动画映射 | Walk_Stop/Run_Stop/Sprint_To_* | ⬜ |
| **1e** | 转向层 + Idle 变体 | Walk_Turn/Jog_Turn + 战斗态 Idle | ⬜ |
| **1f** | PIE 测试 + 参数调优 | 全 speed 范围 + 过渡效果 | ⬜ |
| **2** | AirSM (Jump/Air/Land) | Jump_*/Airborne_*/Land_* (200000) | ⏳ |
| **3** | CrouchSM | Crouch_* (5000-5603) | ⏳ |
| **4** | Combat Layer | Guard/Deflect/Attack (a050) | ⏳ |
| **5** | Prosthetic + CombatArt | a070-a079, a100-a110 | ⏳ |
| **6** | Reaction (Hit/Death/Resurrection) | Hit_*/Death_*/Resurrection_* | ⏳ |
| **7** | Interaction (Deathblow/Interact/Eavesdrop) | a200-250 互动 | ⏳ |
| **8** | 特殊状态 (Hang/Swim/Wall/Roll) | 独立 Locomotion 替代 | ⏳ |

### 3.2 涉及文件

| 文件 | 操作 | 说明 |
|:----|:----:|:-----|
| `Content/Characters/Sekiro/ABP_Sekiro.uasset` | 修改 | 全部层级 + 状态机结构 |
| `Content/Characters/Sekiro/Anim/Sekiro_Walk_2D.uasset` | 参考/迁移 | 旧 2D Walk BlendSpace，Phase 1 可参考采样绑定 |
| `Content/Characters/Sekiro/Anim/Sekiro_Run_2D.uasset` | 参考/迁移 | 旧 2D Run BlendSpace，Phase 1 可参考采样绑定 |
| `Content/Characters/Sekiro/Anim/BS_Walk_Direction` | 新建 | Walk 1D Direction BlendSpace |
| `Content/Characters/Sekiro/Anim/BS_Jog_Direction` | 新建 | Jog 1D Direction BlendSpace |
| `Content/Characters/Sekiro/Anim/BS_Run_Direction` | 新建 | Run 1D Direction BlendSpace |
| `Content/Characters/Sekiro/Anim/BS_Sprint_Direction` | 新建 | Sprint 1D Direction BlendSpace |
| `Content/Characters/Sekiro/Anim/BS_CrouchWalk_Direction` | 新建 | CrouchWalk 1D Direction BS（Phase 3） |
| `Content/Characters/Sekiro/Anim/BS_CrouchRun_Direction` | 新建 | CrouchRun 1D Direction BS（Phase 3） |
| `Source/Sekiro/Animation/SKAnimInstance.h/.cpp` | ⬜ 无需修改 | C++ 接口已完备 |
| `Source/Sekiro/Animation/SKAnimationController.h/.cpp` | ⬜ 无需修改 | 状态机已完备 |

### 3.3 Agent 派发

所有 ABP 操作通过 `aibridge → anim_blueprint` 系列命令在 UE 编辑器中进行，不需要修改 C++ 代码。

| 步骤 | 任务 | 负责 |
|:----:|:----|:----:|
| 1 | 创建 4 个 1D BlendSpace 资产 | aibridge asset create |
| 2 | 逐个配置 BlendSpace 采样点和绑定动画 | aibridge blueprint |
| 3 | ABP GroundMoveSM 扩展（添加 Jog/Run/Sprint 状态） | aibridge anim_blueprint |
| 4 | 设置 Transition Rule（Speed 阈值） | aibridge anim_blueprint |
| 5 | 添加停止/过渡动画 Transition | aibridge anim_blueprint |
| 6 | 添加转向层 + Idle 变体 | aibridge anim_blueprint |
| 7 | PIE 测试 + 参数调优 | aibridge pie |

---

## 变更记录

| 日期 | 变更 |
|:----:|:-----|
| 2026-06-28 | 创建；ALS 参考架构 + Phase 1 Locomotion 设计 |
| 2026-06-30 | 重写 Phase 1（10阶段→8阶段）；对齐最新 animation_annotation.md |
| 2026-06-30 | **基于动画资源重写**：4个1D Direction BS 替代 5个2D BS；完整映射实际动画 ID 到每个采样点；Locomotion 数据驱动设计；新增 AirSM/CrouchSM 资源映射 |
| 2026-07-02 | 对齐当前仓库状态：补充旧 2D BlendSpace 资产；将 GroundMoveSM 阈值统一到现有 C++ 运行时 `10/200/400/600` |
