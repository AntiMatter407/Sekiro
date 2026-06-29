# 只狼动画+输入系统复制（TAE 数据驱动方案）

| 状态 | 创建 | 更新 |
|------|------|------|
| ⏸️ 搁置 | 2026-06-15 | 2026-06-27

> 🔄 替代方案：[sekiro-anim-blueprint.md](sekiro-anim-blueprint.md) — 人工识别+AI状态机制作 |

## 需求描述

将只狼的输入和动画切换系统 1:1 复制到 UE5 中，采用 **TAE 数据驱动** 的 Montage 直接播放方案，不构建复杂的 AnimBlueprint 状态机。

核心链路：
```
玩家按键 → USKInputHandler(输入缓冲) → USKAnimationController(ProcessIntents)
  → CanCancelTo(TAE CancelWindow 判定) → ResolveAnimID(CategoryAnimMap) → PlayMontage
  → 每帧: UpdateAttackHitbox(Curve曲线读取)
```

---

## 📊 总体状态

**C++ 运行时核心**：✅ 完成（8状态状态机 + 优先级系统 + Handler全覆盖）
**TAE 管线（AssetManager）**：✅ 完成（CancelWindow提取 + JT映射 + CategoryAnimMap）
**数据管线（Python脚本）**：🔄 进行中（StateAnimMap + DataTable 已生成，待 UE5 导入验证）
**BehaviorParam 数据管线**：🔄 部分完成（JSON已提取，但分类集成被回退到简单方案）
**TAE 曲线写入 AnimSequence**：⏳ 待执行（write_tae_curves.py 已完成，待批量写入验证）
**运行时 PIE 验证**：🔄 基础验证通过，全面验证待执行

---

## ✅ 已完成模块

| 模块 | 完成日期 | 说明 |
|------|---------|------|
| USKInputHandler（输入缓冲） | 6/22 | 17个 EnhancedInput Action + 6帧缓冲队列 + 消费/持续双模式 |
| USKAnimationController（动画控制） | 6/23 | 8状态状态机 + Priority降序遍历 + CancelWindow判定 + Montage播放 |
| ESKCharacterState（状态枚举） | 6/23 | Idle/Attack/Guard/Dodge/Jump/Airborne/Hit/Death |
| Deflect 判定 | 6/22 | IsEnemyAttacking + 6帧窗口 → Deflect + bCounterWindow |
| 转向系统 | 6/22 | 静止时视角旋转>90° + 0.5s冷却 → Turn动画 |
| Jump 系统（起跳/落地/空中攻击/闪避） | 6/23 | 完整 4 分支 |
| Locomotion 分级 | 6/22 | Idle/Walk/Jog/Run/Sprint 五级速度驱动 |
| Sprint 冲刺（Dodge按住） | 6/22 | SKMovementComponent 驱动 |
| Guard 系统（格挡/弹刀/破防） | 6/22 | GuardPhase 三阶段 |
| OnActionMontageEnded 复位 | 6/23 | 修复 Priority 残留 Bug |
| DeriveNextAnim（连段推导） | 6/23 | AnimID % 1000 + 1 简单推导 |
| SATAEImporter（TAE管线） | 6/22 | ExtractCancelWindows修正 + 28个JT映射 + CategoryAnimMap构建 |
| SATAELogicBuilder（DataAsset构建） | 6/22 | USKAnimationLogicData 生成 |
| AIBridge PIE 基础验证 | 6/23 | Move/Attack/Guard/Dodge/Jump 5项输入全部成功 |
| BehaviorParam_PC JSON 提取 | 6/25 | Yapped→JSON，1206条 |
| BehaviorVariationMap JSON | 6/25 | VariationID→Category 映射 |
| TAE 事件提取脚本 | 6/24 | tae_extractor.py, Sekiro_TAE_Logic.json 重新生成 |
| StateAnimMap 生成管线 | 6/27 | build_v2.py → StateAnimMap.json（142 AnimID × 20 事件） |
| 状态推断 + 事件提取 | 6/27 | build_statemap.py |
| DataTable JSON 生成 | 6/27 | build_datatables.py → StateAnimMap_DataTable.json + StateTransitions_DataTable.json |
| 质量检查 | 6/27 | analyze_anims.py, check_ranges.py 通过 |

---

## 🔄 进行中 / ⏳ 待完成

### 第一优先级：数据管线集成到运行时

- ⏳ 1. **FSKStateAnimEntry + FSKStateTransition DataTable UE5 导入**
  - 需在 Plugins/SekiroAssetManager 中新建这两个 USTRUCT
  - SAImport Commandlet 新增 DataTable 导入模式
  - 运行时 USKAnimationController 从 DataTable 读取替代硬编码

- ⏳ 2. **TAE 曲线批量写入 UAnimSequence**
  - write_tae_curves.py 已支持单 AnimID 写入（FrameFlags + CancelActions + AttackHitbox）
  - 需要：批量写入脚本 + PIE 中确认曲线数据正确

- ⏳ 3. **BehaviorParam_PC 数据分类集成**
  - 当前方案：DeriveNextAnim 简单递增（AnimID % 1000 + 1）
  - 目标方案：BehaviorParam 驱动连段链 + AnimID 分类修正
  - 需评估是否值得重建（此前已删除 SABehaviorParamImporter）

### 第二优先级：运行时全面验证

- ⏳ 4. **PIE 全面验证脚本**
  - Sprint 过渡验证（Run→Sprint→停止）
  - 转向动画验证（静止转身 >90°触发 Turn）
  - 连段 CancelWindow 验证（攻击窗内 R1→下段，窗外 R1→不触发）
  - Jump 全分支验证（起跳→空中攻击→落地）
  - Guard→Deflect 时间窗口验证

### 第三优先级：打磨和完善

- ⏳ 5. **锁敌系统对接**（IsEnemyAttacking 当前无锁敌目标）
- ⏳ 6. **Deathblow 忍杀标记激活**（bDeathBlowActive = true 的触发条件）
- ⏳ 7. **CombatArt（战技）Handler**

---

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Source/Sekiro/Input/SKInputHandler.h/cpp` | ✅ 已完成 | 输入缓冲队列 |
| `Source/Sekiro/Animation/SKAnimationController.h/cpp` | ✅ 已完成 | ProcessIntents/Deflect/转向/状态机 |
| `Source/Sekiro/Animation/SKAnimInstance.h/cpp` | ✅ 已完成 | 运行时标志 |
| `Plugins/SekiroAssetManager/.../SATAEImporter.cpp` | ✅ 已完成 | ExtractCancelWindows 修正 |
| `Plugins/SekiroAssetManager/.../SATAELogicBuilder.cpp` | ✅ 已完成 | DataAsset 构建 |
| `Plugins/SekiroAssetManager/.../SATAELogicIR.h` | ✅ 已完成 | JT 映射 |
| `Plugins/SekiroAssetManager/.../SAImportCommandlet.cpp` | 🔄 待扩展 | 需新增 DataTable 导入模式 |
| `Script/temp/build_datatables.py` | ✅ 已完成 | DataTable JSON 生成 |
| `Script/temp/build_v2.py` | ✅ 已完成 | StateAnimMap 生成 |
| `Script/write_tae_curves.py` | ✅ 已完成 | TAE 曲线写入（单AnimID） |
| `Output/StateAnimMap_DataTable.json` | ✅ 已生成 | 待 UE5 导入 |
| `Output/StateTransitions_DataTable.json` | ✅ 已生成 | 待 UE5 导入 |

---

## ⏸️ 搁置原因

2026-06-27 决定搁置此方案。改为 **人工识别动画内容 + AI 状态机制作** 的新路径生成动画系统。

核心思路差异：
- 原方案：从 TAE 二进制/HKX Behavior Graph 自动提取状态机 → UE DataTable → 运行时驱动
- 新方案：人工分类整理已导入的动画资源 → AI 辅助设计状态机蓝图 → 直接在 AnimBlueprint 中搭建

搁置前已完成的成果保留：
- C++ 运行时核心（USKAnimationController / USKInputHandler）可用
- TAE 数据管线脚本（tae_extractor.py / build_v2.py 等）可用
- DataTable CSV 已生成，可作为参考

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-27 | 第十三次更新：全面刷新状态——C++核心完成，数据管线待集成，计划重组为三优先级 |
| 2026-06-27 | DataTable 管线：StateAnimMap/StateTransitions JSON 已生成，质量检查通过 |
| 2026-06-25 | commit 64ea0e2: SKAnimationController 大幅精简（-172行），移除 ComboChain/SABehaviorParamImporter |
| 2026-06-24 | TAE 事件数据提取脚本 tae_extractor.py；BehaviorParam 交叉验证 |
| 2026-06-23 | 状态机重构为 8 状态 switch-case |
| 2026-06-22 | 核心运行时完成：输入缓冲 + ProcessIntents + CancelWindow + Montage播放 |
