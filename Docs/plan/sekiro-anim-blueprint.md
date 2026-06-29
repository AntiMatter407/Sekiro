# Sekiro AnimBlueprint 动画蓝图方案

| 状态 | 创建 | 更新 |
|------|------|------|
| 📋 需求阶段 | 2026-06-28 | 2026-06-28 |

> 前置：1434 个动画已全部导入 UE5，覆盖 a000~a250
> 关联：[sekiro-anim-input-replica.md](sekiro-anim-input-replica.md) ⏸️ 搁置
> 参考实现：ALS V4 (Advanced Locomotion System)

## 技术路线

采用 ALS 的分层动画蓝图架构，按阶段实现：

```
Phase 1: ALS Locomotion    → 站立移动 (Idle/Walk/Jog/Run/Sprint)
Phase 2: Crouch            → 蹲姿移动 + 蹲↔站过渡
Phase 3: Combat Base       → Attack/Guard/Deflect/Dodge 状态机
Phase 4: Jump/Air          → 跳跃/空中/落地
Phase 5: Reaction          → Hit/Death/Resurrect/PostureBreak
Phase 6: Prosthetic        → 忍义手 UpperBody 层
Phase 7: CombatArt/Grapple → 战技/钩绳 FullBody 层
Phase 8: Deathblow         → 忍杀特殊交互
```

每个阶段：标注对应动画 → 实现状态机 → 验证

## 动画数据现状

| 前缀 | 数量 | 内容 |
|------|------|------|
| a000 | 689 | Locomotion + 部分战斗 |
| a010 | 21 | 杂项过渡 |
| a050 | 177 | 防御/弹刀/攻击 |
| a070~a079 | ~200 | 忍义手 |
| a100~a110 | ~100 | 战技 |
| a200~a250 | ~200 | Boss互动（忍杀/识破等） |

## 任务

| # | 任务 | 阶段 | 状态 |
|---|------|------|------|
| 1 | **研究 ALS 动画系统架构** | Phase 0 | ⏳ 待开始 |
| 2 | **Phase 1 动画标注：站立移动** | Phase 1 | ⏳ 待开始 |
| 3 | **实现 ALS Locomotion** | Phase 1 | ⏳ 待开始 |
| 4 | Phase 2 动画标注：蹲姿移动 | Phase 2 | ⏳ 待开始 |
| 5 | 实现 Crouch 状态机 | Phase 2 | ⏳ 待开始 |
| 6 | Phase 3 动画标注：战斗 | Phase 3 | ⏳ 待开始 |
| 7 | 实现 Combat 状态机 | Phase 3 | ⏳ 待开始 |
| 8 | Phase 4~8 后续阶段 | — | ⏳ 待开始 |

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-28 | 创建；采用 ALS 分层架构，标注分阶段执行 |
