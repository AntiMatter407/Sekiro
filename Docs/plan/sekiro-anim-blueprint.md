# Sekiro AnimBlueprint 动画蓝图方案 — 计划

> 状态：📋 方案阶段 | 创建：2026-06-28 | 更新：2026-07-02
> 关联：[设计方案](../design/sekiro-anim-blueprint.md) | [动画资产清单](../animation_annotation.md)

## 整体路线

| 阶段 | 内容 | 核心动画 | 状态 |
|:----:|------|:--------:|:----:|
| **1a** | GroundMoveSM 基础状态（Idle/Walk/Run/Sprint） | Anim_Sekiro_a000_000000~000503 | ⬜ 待开始 |
| **1b** | Direction 选择/校正 + Walk/Run/Sprint 采样绑定 | Walk/Run/Sprint 方向动画 | ⬜ 待开始 |
| **1c** | Speed / 输入修饰 Transition Rule | Idle↔Walk↔Run↔Sprint | ⬜ 待开始 |
| **1d** | 停止/降级过渡动画映射 | Walk_Stop/Run_Stop/Sprint_To_* | ⬜ 待开始 |
| **1e** | 转向层 + Idle 变体 | Walk_Turn/Run_Turn | ⬜ 待开始 |
| **1f** | PIE 测试 + 参数调优 | 全 speed 范围 + 过渡效果 | ⬜ 待开始 |
| **2** | AirSM（Jump/Air/Land） | Jump_*/Airborne_*/Land_* (200000) | ⏳ 待排期 |
| **3** | CrouchSM | Crouch_* (5000-5603) | ⏳ 待排期 |
| **4** | Combat Layer（Guard/Deflect/Attack） | a050 战斗动作 | ⏳ 待排期 |
| **5** | Prosthetic + CombatArt | a070-a079, a100-a110 | ⏳ 待排期 |
| **6** | Reaction（Hit/Death/Resurrection） | Hit_*/Death_*/Resurrection_* | ⏳ 待排期 |
| **7** | Interaction（Deathblow/Interact/Eavesdrop） | a200-250 互动 | ⏳ 待排期 |
| **8** | 特殊状态（Hang/Swim/Wall/Roll） | 独立 Locomotion 替代 | ⏳ 待排期 |

## Phase 1 子任务

| # | 任务 | 描述 | 依赖 | 状态 |
|:-:|:----|:-----|:----:|:----:|
| 1.1 | 创建 Walk/Run Direction 选择资产或状态 | BS_Walk_Direction / BS_Run_Direction，Sprint 不做方向混合 | — | ⬜ |
| 1.2 | 配置 BlendSpace 采样点 + 绑定动画 | 每个 BS 按实际资源添加采样点 | 1.1 | ⬜ |
| 1.3 | ABP GroundMoveSM 扩展（添加 Run/Sprint 状态节点） | 当前仅有 Idle/Walk，补充 Run 与 Sprint，Sprint 播前向循环 | — | ⬜ |
| 1.4 | 输入/Speed Transition Rule | 设置 Idle↔Walk↔Run↔Sprint 的触发规则 | 1.3 | ⬜ |
| 1.5 | 停止/降级过渡动画映射 | Walk_Stop/Run_Stop/Sprint_To_* 过渡 Transition | 1.4 | ⬜ |
| 1.6 | 转向层 | Walk_Turn / Run_Turn 急转覆盖 | 1.3 | ⬜ |
| 1.7 | Idle 变体 | 非战斗/拔刀/战斗态 Idle 切换 | — | ⬜ |
| 1.8 | PIE 全链路测试 | 全 Speed 范围 + 方向 + 过渡验证 | 1.4-1.7 | ⬜ |

## 依赖

### 已完成

- [x] C++ USKAnimInstance：Speed / Angle / Direction / MovementTier / bIsInAir / bIsCrouching
- [x] USKMovementComponent：GetMaxSpeed 按 Tier 返回
- [x] 1434 个动画资产已导入 UE5（路径：`/Game/Characters/Sekiro/Animations/Anim_Sekiro_*`）
- [x] animation_annotation.md 动画资产清单（包含所有 TAE ID 到 UE 动画名的映射）
- [x] Docs/design/sekiro-anim-blueprint.md 设计方案（基于实际动画资源完成）
- [x] 方案设计优化：从 5 个 2D BS 降为 4 个 1D Direction BS，节约 1 个资产

### 当前仓库核对（2026-07-02）

- [x] `Content/Characters/Sekiro/ABP_Sekiro.uasset` 已存在
- [x] `Content/Characters/Sekiro/Anim/Sekiro_Walk_2D.uasset`、`Sekiro_Run_2D.uasset` 已存在，属于旧 2D BlendSpace 资产，可作为参考或迁移输入
- [x] Phase 1 所需核心动画资源已存在：Idle、Walk/Run/Sprint 循环、停止、转向、Sprint 降级过渡
- [x] C++ 当前 Locomotion 步态：Walk / Run / Sprint，无 Jog
- [ ] 目标 Direction 选择资产尚未发现：`BS_Walk_Direction` / `BS_Run_Direction`；Sprint 使用前向循环

### 下一步执行入口

1. 使用 AIBridge 在 UE 编辑器内创建 4 个 1D Direction BlendSpace。
2. 按设计文档采样表绑定 `Anim_Sekiro_a000_*` 资源。
3. 将 `ABP_Sekiro` 的 GroundMoveSM 从现有 Idle/Walk 扩展为 Idle/Walk/Run/Sprint。
4. Transition Rule 优先与当前输入逻辑保持一致：`Alt/轻推=Walk`、默认=Run、`Shift/B=Sprint`。

## 变更记录

| 日期 | 变更 |
|:----:|:-----|
| 2026-06-28 | 创建；8 阶段路线 + 依赖检查 |
| 2026-06-30 | 同步更新：10 阶段→8 阶段合并过渡+转向到 Phase 1，对齐新动画命名 |
| 2026-06-30 | **基于动画资源重写**：从 4 个 2D BS（Speed×Direction）改为 4 个 1D Direction BS（因 Jog/Sprint 缺少后向/侧向动画）；1a~1f 细分子任务；更新动画资源路径 |
| 2026-07-02 | 仓库核对：确认 ABP/旧 2D BS/核心动画资产存在；补充当前 C++ Locomotion 阈值与下一步执行入口 |
