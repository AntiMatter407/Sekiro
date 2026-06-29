# Sekiro AnimBlueprint — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [计划](../plan/sekiro-anim-blueprint.md) | 📋 Phase 0 | 2026-06-28 | 2026-06-28 |

> 参考实现：[ALS V4](https://github.com/dyanikoglu/ALS-Community) — Community Edition

## 〇、ALS 架构参考

ALS (Advanced Locomotion System) 的核心设计模式：

### 分层状态机
```
ABP_ALS
  ├── [Base Layer]   Locomotion Cycle (BlendSpace)
  ├── [Overlay Layer] UpperBody Overlay (瞄准/攻击)  
  └── [Slot Layer]    Montage (Action/Crouch/Roll)
```

### Locomotion 关键概念

| ALS 概念 | 说明 | 只狼对应 |
|----------|------|---------|
| Gait | Walk / Run / Sprint | Walk / Jog / Run / Sprint |
| Stance | Standing / Crouching | Standing / Crouching |
| MovementDirection | Fwd / Bwd / L / R | 8 方向 |
| RotationMode | VelocityDirection / LookingDirection | 仅 VelocityDirection |
| OverlayState | 上半身动作覆盖 | 忍义手/使用道具 |
| Grounded/Air | 地面/空中 | 地面/空中 |

### ALS BlendSpace 驱动方式

```
UAnimInstance 每帧计算:
  Velocity → Speed (2D)
  Velocity vs ActorRotation → Direction Angle
  
BlendSpace2D:
  X: Speed (0 → 600)
  Y: Direction (-180 → 180)
  采样点: Idle / Walk_Fwd / Walk_L / Walk_R / Walk_Bwd / Jog_Fwd / ...
```

### 关键曲线

ALS 在动画中嵌入曲线控制：
- `Enable_FootIK_L` / `Enable_FootIK_R` — 脚部 IK
- `RotationYawOffset` — 旋转偏移
- `PlayRate` — 播放速率缩放

---

## 一、Phase 1：站立 Locomotion

### 1.1 需标注的动画

从 a000 Locomotion 段（AnimID 0~5600）中提取站立移动相关动画，标注维度：

| AnimID | 动作 | 方向 | Gait | 循环 | 起始帧 | 结束帧 |
|--------|------|------|------|------|--------|--------|

已知 a000 locomotion 段（CSV 已标注，需验证）：
- 0: Idle Stand
- 100~103: Walk Fwd/Fwd_L/Fwd_R/Stop
- 110~113: Walk Bwd/Bwd_L/Bwd_R/Stop  
- 120~123: Walk L/L_Stop/R/R_Stop
- 132~133: Walk LSlow/RSlow
- 200~203: Jog Fwd/Fwd_L/Fwd_R/Stop
- 300~303: ?
- 400~403: Run Fast Fwd/Fwd_L/Fwd_R/Stop
- 500~503: ?
- 600~603: ?
- 1151~1154: Sprint Start
- 1200: Sprint Loop
- 1402~1403: Sprint Stop L/R
- 1510~1512: Sprint Stop to Stand
- 20000: Walk→Idle 过渡
- 20010: Walk→Jog 过渡

### 1.2 BlendSpace 设计

```
BlendSpace1D: Speed (0~600)
  X=0:    Idle (AnimID 0)
  X=150:  Walk_Fwd (AnimID 100)
  X=350:  Jog_Fwd (AnimID 200)
  X=500:  Run_Fast_Fwd (AnimID 400)  
  X=600:  Sprint (AnimID 1200)
```

方向由 `SKAnimInstance::Direction` 驱动，不在 BlendSpace 中混合方向。

### 1.3 实现要点

- `USKAnimInstance::NativeUpdateAnimation` 已计算 Speed/Angle/Direction
- 复用现有 `SekiroAnimBlueprintBuilder::BuildLocomotionBlendSpace` (已实现)
- 需补充：Sprint 进入/退出过渡（当前 BlendSpace 无 Sprint）
- 需补充：Walk↔Idle 过渡动画（AnimID 20000）
- 需补充：Walk↔Jog 过渡动画（AnimID 20010）

### 1.4 过渡规则

```
Idle → Walk:    Speed > 50
Walk → Jog:     Speed > 250  && MovementTier >= Jog
Jog → Run:      Speed > 450  && MovementTier >= Run
Run → Sprint:   Speed > 550  && bSprintRequested
Sprint → Run:   Speed < 500
Run → Jog:      Speed < 400
Jog → Walk:     Speed < 200
Walk → Idle:    Speed < 20
```

---

## 二、后续阶段（概要）

| 阶段 | 内容 | 核心动画段 |
|------|------|-----------|
| Phase 2 | Crouch 蹲姿 | 5000~5699 |
| Phase 3 | Combat 战斗 | a050_0xxx~3xxx |
| Phase 4 | Jump/Air | 200000~ |
| Phase 5 | Reaction | 100000~, 110000~ |
| Phase 6 | Prosthetic | a070~a079 |
| Phase 7 | CombatArt/Grapple | a100~a110, 790xxx |
| Phase 8 | Deathblow | a200~a250 |

---

## 三、数据流

```
Phase N 动画标注 (CSV)
        │
        ▼
  build_abp_data.py      生成 BlendSpace 采样点 / 状态映射
        │
        ▼
  AnimBlueprint Builder   程序化创建 ABP 节点 (C++ / AIBridge)
        │
        ▼
  ABP_Sekiro.uasset      最终 AnimBlueprint 资产
        │
        ▼
  USKAnimationController  运行时驱动 (ProcessIntents → Montage)
```

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-28 | 创建；ALS 参考架构 + Phase 1 Locomotion 设计 |
