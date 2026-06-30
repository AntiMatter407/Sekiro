# Sekiro AnimBlueprint 动画蓝图方案 — 计划

> 状态：📋 方案阶段 | 创建：2026-06-28 | 更新：2026-06-30
> 关联：[设计方案](../design/sekiro-anim-blueprint.md) | [动画资产清单](../animation_annotation.md)

## 整体路线

| 阶段 | 内容 | 核心动画 | 状态 |
|:----:|------|---------|:----:|
| **1** | 站立 Locomotion（Idle/Walk/Jog/Run/Sprint + 过渡 + 转向） | Walk_*, Jog_*, Run_*, Sprint_*, Idle_* | ⬜ 待开始 |
| **2** | Jump / Air / Land | Jump_*, Airborne_*, Land_* | ⏳ 待排期 |
| **3** | Crouch 蹲姿 | Crouch_* | ⏳ 待排期 |
| **4** | Combat（Attack / Guard / Deflect / Dodge） | Guard_*, Deflect_*, AttackPose_*, Quickstep_* | ⏳ 待排期 |
| **5** | Prosthetic 忍义手 + CombatArt 战技 | Prosthetic_*, CombatArt_* | ⏳ 待排期 |
| **6** | Reaction（Hit / Death / Resurrection） | Hit_*, Death_*, Resurrection_* | ⏳ 待排期 |
| **7** | Interaction（Deathblow / Interact / Eavesdrop / ItemUse） | Deathblow_*, Interact_*, ItemUse_* | ⏳ 待排期 |
| **8** | 特殊状态（Hang / Swim / Wall / Roll） | Hang_*, Swim_*, Wall_*, Roll | ⏳ 待排期 |

## Phase 1 子任务

| # | 任务 | 描述 | 状态 |
|---|------|------|:----:|
| 1.1 | 创建 GroundMoveSM 子状态 | Idle/Walk/Jog/Run/Sprint 5 个状态节点 | ⬜ |
| 1.2 | 创建 2D BlendSpace 资产 | Walk/Jog/Run/Sprint 各一个 2D BS（Speed×Direction） | ⬜ |
| 1.3 | 采样点动画绑定 | 各方向动画分配到对应 BlendSpace | ⬜ |
| 1.4 | Speed 阈值过渡规则 | Idle↔Walk↔Jog↔Run↔Sprint Transition Rule | ⬜ |
| 1.5 | 过渡动画映射 | Walk_Stop / Run_Stop / Sprint_To_* 等降级过渡 | ⬜ |
| 1.6 | 转向层 | Walk_Turn / Jog_Turn 急转覆盖 | ⬜ |
| 1.7 | Idle 变体切换 | 非战斗/拔刀/战斗态 Idle 切换 | ⬜ |
| 1.8 | PIE 测试 | 全 Speed 范围 + 方向 + 过渡验证 | ⬜ |

## 依赖

### 已完成

- [x] C++ USKAnimInstance：Speed / Angle / Direction / MovementTier / bIsInAir / bIsCrouching
- [x] USKMovementComponent：GetMaxSpeed 按 Tier 返回
- [x] 1434 个动画资产已导入 UE5
- [x] animation_annotation.md 动画资产清单 + 方案动画名映射
- [x] Docs/design/sekiro-anim-blueprint.md 设计方案

### 待确认

- ABP_Sekiro.uasset 当前状态（1D BlendSpace / ABP 层级结构）

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-28 | 创建；8 阶段路线 + 依赖检查 |
| 2026-06-30 | 同步更新：10 阶段→8 阶段合并过渡+转向到 Phase 1，对齐新动画命名 |
