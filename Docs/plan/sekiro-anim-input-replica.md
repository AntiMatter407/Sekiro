# 只狼动画+输入系统复刻（直接播放方案）

| 状态 | 创建 | 更新 |
|------|------|------|
| ✅ 已完成 | 2026-06-15 | 2026-06-21 |

## 需求描述

将只狼的动画系统和输入系统 1:1 复刻到 UE5 中，采用**直接动画播放**方式（Montage），不构建复杂的 UE 动画蓝图状态机。

**核心原则**：忠实复刻 FromSoftware 原版 TAE（Time Action Event）系统的逻辑——动画内嵌事件驱动的"推送"架构，三层优先级状态机（Locomotion → Combat → Reaction），取消窗口帧级判定。

## 架构

```
Extracted/Sekiro_TAE_Logic.json (512KB)
        │
        ▼  FSATAEImporter → FSATAELogicBuilder（SekiroAssetManager 插件）
        │
┌───────────────────────────────┐
│  USKAnimLogicData (DataAsset) │  ← TAE 数据抽象为 UE 资产
│  · CancelRules                │
│  · AttackHitboxConfigs        │
│  · AnimFrameFlags（帧级）     │
│  · SpEffectConfigs            │
│  · AnimNameMap / CategoryAnimMap │
└───────────────┬───────────────┘
                │  运行时加载
Enhanced Input  │
      │         │
      ▼         ▼
┌──────────────────────┐
│  USKInputHandler     │  ← 输入→意图
└─────────┬────────────┘
          │ 动作意图
          ▼
┌──────────────────────┐
│ USKAnimationController│ ← 意图→动画
│  · 三层优先级状态机   │
│  · CancelWindow 判定  │
│  · Montage 播放       │
│  · 行为标志注入       │
│  · Hitbox 激活        │
└─────────┬────────────┘
          │
          ▼
  AnimInstance / SkeletalMesh
```

## 任务树

<!--
  状态图标：⬜ 未开始  🔄 进行中  ✅ 已完成  ❌ 已取消
  依赖标注：任务后追加 (依赖: 1, 2.1)
-->

### 基础层（已完成）

- ✅ 1. USKInputHandler 输入处理组件（17个 Enhanced Input Action 全覆盖）
- ✅ 2. TAE → DataAsset 管线（FSATAEImporter + FSATAELogicBuilder → USKAnimLogicData）
- ✅ 3. USKAnimationController 动画控制组件（优先级状态机 + CancelWindow + Montage + FrameFlags）
- ✅ 4. 基础移动（五级速度+八方向 Locomotion + Turn 转身）

### 攻击系统

- ✅ 5.1 攻击基础（Combo链/蓄力/方向变体/空中/蹲行）
- ✅ 5.2 Hitbox 激活（UpdateAttackHitbox 从 DataAsset 帧级读取，驱动 ASKWeapon 碰撞体）
- ⬜ 5.3 攻击动画资产验证
  - ⬜ 5.3.1 验证 CategoryAnimMap 攻击类别覆盖全部攻击动画
  - ⬜ 5.3.2 验证 CancelWindow 数据覆盖攻击连段

### 战斗系统

- ✅ 6. 防御系统（6.1 Guard + 6.2 Deflect 预留接口）（Guard 举防 + Deflect 弹刀 + Guard Break 架势崩）

- ✅ 7. 闪避系统（HandleDodge：StepDodge/Quickstep 四方向）
- ⬜ 8. 跳跃系统（Jump 起跳/空中/落地 + 方向变体）
- ✅ 9. 受击与死亡（OnHitReceived/OnDeath/OnResurrection）
- ✅ 10. 特殊动作（Prosthetic/Item/Grapple/CombatArt/Deathblow 处理器）

### 集成层

- ✅ 11. 帧级事件注入（UpdateAttackHitbox + ApplyFrameFlags 每帧查询 DataAsset）
- 🔄 12. 集成测试（USKInputSimulateTool + bridge.py 已就绪）
  - ⬜ 12.1 PIE 全按键功能 + 过渡流畅性 + 优先级打断验证
  - ⬜ 12.2 AIBridge 输入模拟工具（USKInputSimulateTool + bridge.py 命令 + 测试脚本）

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建文档，双组件架构（USKInputHandler + USKAnimationController） |
| 2026-06-15 | Task 1-3 完成 |
| 2026-06-17 | Task 4 完成，Task 5.1 完成，Task 5 递归拆分 |
| 2026-06-21 | 修复 CategoryAnimMap 子类缺失（BuildNameMaps 从 AnimName 提取 Action 子类）；SATAEImporter ID范围对齐；Attack_Dodge_Back→Bwd 统一命名 | 2026-06-21 | 防御系统 6.1 实现：ESKGuardPhase 四阶段状态机 + HandleGuard/OnGuardHit/OnGuardBreak/IsGuarding |
| 2026-06-21 | **sekiro-asset-manager 重构后重置**：TAE管线改为 C++ FSATAEImporter + FSATAELogicBuilder；任务树重组为三层（基础/战斗/集成）；5.2 Hitbox已实现标记完成 |