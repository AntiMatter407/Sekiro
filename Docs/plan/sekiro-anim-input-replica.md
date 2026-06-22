# 只狼动画+输入系统复刻（TAE 数据驱动方案）

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 进行中 | 2026-06-15 | 2026-06-22 |

## 需求描述

将只狼的输入和动画切换系统 1:1 复刻到 UE5 中，采用 **TAE 数据驱动** 的 Montage 直接播放方案，不构建复杂的 AnimBlueprint 状态机。

核心链路：
```
玩家按键 → USKInputHandler(输入缓冲) → USKAnimationController(ProcessIntents)
  → CanCancelTo(TAE CancelWindow 判定) → ComboChain(动画选择) → PlayMontage
  → 每帧: ApplyFrameFlags + UpdateAttackHitbox(TAE 帧级数据)
```

### 已完成

| 模块 | 完成日期 | 说明 |
|------|---------|------|
| TAE 管线 ExtractCancelWindows 修正 | 6/22 | JT=115→Attack, JT=117→Guard, JT=118→Prosthetic, JT=25→Dodge, JT=26→Attack, JT=154→Item |
| JT 映射补全（28个） | 6/22 | ESKJumpTableAction 补充高频未映射 JT ID |
| ComboChain DataAsset + 管线 | 6/22 | USKCombatData + SAImport Commandlet + Python 脚本，`DA_Sekiro_Combat.uasset` 已生成 |
| SAImport Commandlet 统一 | 6/22 | ComboChain 导入整合到 SAImport，删除单独的 BehaviorParamCommandlet |
| ProcessIntents 优先级遍历 | 6/22 | Priority 降序遍历 + CanCancelTo 判定 + 上下文分支（忍杀/反斩/空中/蓄力/方向/连段） |
| 输入缓冲队列 | 6/22 | USKInputHandler 新增 FSKBufferedInput 6帧缓冲队列，超时自动移除 |
| 连段系统对接 ComboChain | 6/22 | USKAnimationController 加载 USKCombatData，ResolveAnimID 先查 ComboChain 再回退 CategoryAnimMap |
| Deflect 判定 | 6/22 | IsEnemyAttacking 查敌人攻击框 + 时间差 ≤6帧 → Deflect（开启 bCounterWindow），锁敌待实现 |
| 转向系统 | 6/22 | ProcessLocomotion 原地转身判定（>90° + 冷却 0.5s），调用 GetTurnAnimID |
| EvaluateLocomotionState 编译修复 | 6/22 | `State` 变量未声明导致编译错误 |
| AIBridge 参考文档 + 统一路径 | 6/22 | `Docs/aibridge-reference.md`，bridge.py 移至 `Script/aibridge/` 两工具共享 |
| 输入→JT 映射讲解文档 | 6/22 | `Docs/sekiro-tae-input-mapping.md` |
| 测试方案文档 | 6/22 | `Docs/test-sekiro-input-jt-mapping.md` |
| **义手长按计时修复** | 6/22 | `bProstheticPressed`→`bProstheticHeld`，新增变量全局统一 |
| **Deathblow/Guard/Prosthetic/Item/Grapple/CombatArt 统一走 TryPlayAction** | 6/22 | 修复 6 个 handler 不走 CanCancelTo 窗口判定的问题 |

### 待完成

#### 第一优先级：验证与实测

- 🔄 1. **PIE 动画输出验证**（6/22 初步验证通过，需逐项确认）
  - [x] AIBridge input_simulate 连接成功
  - [x] Move（Walk/Jog/Run）指令成功执行
  - [x] R1 攻击连段（3次连续 R1 输入成功）
  - [x] Guard 动画（按住 + 快速按放）
  - [x] Dodge 闪避
  - [x] Jump 跳跃
  - [ ] 验证 Sprint（Dodge双击→保持移动）
  - [ ] 验证转向动画（look 视角变化）
  - [ ] 验证五级速度切换时的 Tier 过渡动画
  - [ ] 运行时数据采集（GetCurrentAnimID / CurrentAction 日志输出确认动画播了对）

#### 第二优先级：战斗系统完善

- ⬜ 2. **Jump 系统完善**
  - [ ] 起跳/上升/下落/落地四阶段状态机
  - [ ] 空中攻击（HandleAttack 上下文分支已就位但需空中动画）
  - [ ] 空中闪避

- ⬜ 3. **BehaviorParam_PC 完整解析**
  - [ ] 用 Yapped 提取 param 列名（animId, nextBehaviorId 等）
  - [ ] Python 自动生成完整 ComboChain（替代现在的手写 14 条）

#### 第三优先级：集成与工具

- ⬜ 4. **AIBridge 输入模拟测试脚本**
  - [ ] 自动化测试序列脚本（`Script/temp/test_sequence.py`）
  - [ ] PIE 全按键功能 + 过渡流畅性 + 优先级打断验证

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Source/Sekiro/Input/SKInputHandler.h/cpp` | ✅ 已完成 | 输入缓冲队列 |
| `Source/Sekiro/Animation/SKAnimationController.h/cpp` | ✅ 已完成 | ProcessIntents/Deflect/转向/ComboData 引用 |
| `Source/Sekiro/Animation/SKAnimInstance.h/cpp` | ✅ 已完成 | 运行时标志补充 |
| `Plugins/SekiroAssetManager/.../SATAEImporter.cpp` | ✅ 已完成 | ExtractCancelWindows 修正 |
| `Plugins/SekiroAssetManager/.../SATAELogicIR.h` | ✅ 已完成 | JT 映射补全 |
| `Plugins/SekiroAssetManager/.../SAImportCommandlet.cpp` | ✅ 已完成 | ComboChain 导入 |
| `Plugins/SekiroAssetManager/.../SekiroCombatData.h/cpp` | ✅ 已完成 | USKCombatData 新 DataAsset |
| `Plugins/SekiroAssetManager/.../SABehaviorParamImporter.h/cpp` | ✅ 已完成 | ComboChain JSON 导入器 |
| `Docs/aibridge-reference.md` | ✅ 已完成 | AIBridge 完整参考文档 |

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建文档 |
| 2026-06-22 | 第一次更新：整合为单文档，TAE 数据驱动方案 |
| 2026-06-22 | 第二次更新：管线修复 + 全部 5 项运行时修正完成，进入验证阶段 |
| 2026-06-22 | 第三次更新：修复义手长按计时 `bProstheticPressed`→`bProstheticHeld`；HandleDeathblow/Guard/Prosthetic/Item/Grapple/CombatArt 统一走 TryPlayAction 通过 CanCancelTo 窗口判定；HandleDeathblow 新增 `bDeathBlowActive` 守卫 |
| 2026-06-22 | 第四次更新：AIBridge PIE 初步验证——Move/Attack/Guard/Dodge/Jump 基础输入模拟全部成功 |
