# 只狼动画+输入系统复刻（TAE 数据驱动方案）

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 进行中 | 2026-06-15 | 2026-06-24 |

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
| **ABP_Sekiro 配置确认** | 6/23 | AnimBlueprint Slot 节点和变量绑定已验证通过 |
| **PIE 动画输出验证** | 6/23 | Move/Attack/Guard/Dodge/Jump/PIE 基础输入模拟全部成功 |

### 待完成

#### 次要任务：运行时验证（按需执行）

- 🔄 1. **运行时数据采集验证**（Sprint 过渡 + 转向 + GetCurrentAnimID/CurrentAction 日志）
  - ✅ 1.1 AIBridge PIE input_simulate 全部 7 项操作（Move/Sprint/Attack/Guard/Dodge/Jump/Look）验证执行成功，bridge 返回 success
  - ✅ 1.2 确认 ABP 中 DefaultSlot 节点存在（AnimGraphNode_Slot_2, SlotName=DefaultSlot）
  - ✅ 1.3 确认 AnimLogicData 加载正常（日志 `AnimLogicData=ok`）
  - ✅ 1.4 Guard 动画触发确认（日志 `Action=Guard AnimID=300000` 连续触发）
  - 🔴 1.5 **卡动画Bug修复：OnActionMontageEnded 复位 CurrentPriority**（PlayMontageByID 播完后复位 Priority/Locomotion）
  - ⬜ 1.6 编译 + PIE 验证修复效果

#### 次要任务：战斗系统完善（按需执行）

- 🔄 2. **Jump 系统完善**
  - ✅ 2.1-2.3 起跳/落地/空中攻击/空中闪避已完成
  - ⬜ 2.4 PIE 验证

- ⬜ 3. **BehaviorParam_PC 完整解析**
  - ⬜ 3.1 用 Yapped 提取 param 列名（animId, nextBehaviorId 等）
  - ⬜ 3.2 Python 自动生成完整 ComboChain（替代当前的手写 14 条）

- 🔄 4. **TAE 事件数据提取与 AnimID 分类修正**
  - ✅ 4.1 Python TAE 解析器 → Script/sekiro_asset_manager/tae_extractor.py，支持从 .tae 二进制提取事件数据并导出为 JSON
  - ✅ 4.2 Sekiro_TAE_Logic.json 已重新生成（64/65 文件成功），包含所有动画的 TAE 事件
  - 🔴 4.3 JumpTableID 参数读取修正：type=0 事件的 param_bytes 字节顺序需要根据 TAE 模板确认（当前 int 解析得到的是 float 值，JumpTableID 可能在其他偏移位置）
  - ⬜ 4.4 基于 TAE 事件特征推断动画分类（替代纯数字范围 InferCategoryFromAnimID）
  - ⬜ 4.5 重新生成 DA_Sekiro_AnimLogic DataAsset

#### 次要任务：集成与工具（按需执行）

- ⬜ 5. **AIBridge 输入模拟测试脚本**
  - ⬜ 4.1 自动化测试序列脚本（`Script/temp/test_sequence.py`）
  - ⬜ 4.2 PIE 全按键功能 + 过渡流畅性 + 优先级打断验证

#### 第一优先级：数据管线完善（参见 [链路文档](../sekiro-input-to-animation-pipeline.md)）

> 阶段1 依赖阶段2 的 BehaviorParam 数据做交叉验证，建议 **2→1→3→4** 顺序执行。

> 以下为当前主要推进任务，按 pipeline 四阶段排列。

- ⬜ 1. **阶段1：BehaviorVariationID ↔ AnimID 映射**（AnimID 编码 + BehaviorParam 交叉验证，方案见 [设计文档](../design/sekiro-anim-state-machine-extraction.md)）
  - ⬜ 1.1 整理 AnimID ↔ behaviorVariationID 编码规则文档
  - ⬜ 1.2 编写交叉查表脚本（TAE BehaviorJudgeID + BehaviorParam → VarID）
  - ⬜ 1.3 输出 AnimToBehaviorMap.json（AnimID → VarID → RefType）

- ⬜ 2. **阶段2：提取 BehaviorParam_PC**（Yabber 导出 → behaviorParamID → {RefType, Category, RefID} 权威分类）
  - ⬜ 2.1 Yabber 导出 BehaviorParam_PC.param → XML
  - ⬜ 2.2 Python 解析 XML → 构建 `{VariationID*1000 + BehaviorJudgeID → {RefType, Category}}` 查表
  - ⬜ 2.3 对 262 个有 Type=1/2/5 的动画，查表获取 RefType 权威分类
  - ⬜ 2.4 输出 AnimID → 权威分类 JSON

- ⬜ 3. **阶段3：完善 TAE 事件解析**（修复 JumpTableID 读取 + 全事件类型覆盖）
  - ⬜ 3.1 修复 Type=0 事件 param_bytes 字节顺序（当前 int 读到 float 值）
  - ⬜ 3.2 验证 Type=1/2/5 事件的 BehaviorJudgeID 完整性
  - ⬜ 3.3 重新运行 SekiroTAEExtractor → 生成修正版 Sekiro_TAE_Logic.json

- 🔄 4. **阶段4：TAE 曲线写入 UAnimSequence**（FrameFlags + CancelActions + AttackHitbox）
  - ✅ 4.1 write_tae_curves.py 已完成（支持 --animid 单动画写入）
  - ⬜ 4.2 批量写入验证（选取 20 个代表性动画，PIE 中确认曲线数据正确）
  - ⬜ 4.3 自动化全量写入脚本（按 AnimID 范围分批，错误重试）

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
| `Docs/sekiro-input-to-animation-pipeline.md` | ✅ 已完成 | 输入->动画 ID 链路参考文档 |

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-24 | 第十二次更新：阶段1 方案设计完成（[设计文档](../design/sekiro-anim-state-machine-extraction.md)）——放弃完整状态机提取，改用 AnimID 编码 + BehaviorParam 交叉验证 |
| 2026-06-24 | 第十一次更新：调整优先级——数据管线四阶段提升为第一优先级，原验证/战斗/集成降为次要任务 |
| 2026-06-24 | 第十次更新：新增第四优先级 4 阶段数据管线任务（状态机/BehaviorParam/TAE/曲线写入）；新增参考文档 Docs/sekiro-input-to-animation-pipeline.md |
| 2026-06-24 | 第九次更新：修复 AIBridge 输入模拟脉冲释放；添加 TAE 事件数据提取（tae_extractor.py）；新增任务 4.1-4.5 AnimID 分类修正 |
| 2026-06-15 | 创建文档 |
| 2026-06-22 | 第一次更新：整合为单文档，TAE 数据驱动方案 |
| 2026-06-22 | 第二次更新：管线修复 + 全部 5 项运行时修正完成，进入验证阶段 |
| 2026-06-22 | 第三次更新：修复义手长按计时；全部 handler 统一走 TryPlayAction |
| 2026-06-22 | 第四次更新：AIBridge PIE 初步验证——Move/Attack/Guard/Dodge/Jump 基础输入模拟全部成功 |
| 2026-06-23 | 第五次更新：确认 ABP+基础 PIE 验证完成，下一步 Sprint/转向/运行时日志确认 |
| 2026-06-23 | 第六次更新：PIE 7 项操作全部执行成功；发现 Guard 播完后 CurrentPriority=5 残留导致后续动作被阻塞的Bug；新增 OnActionMontageEnded 回调复位 CurrentPriority；编译通过待 PIE 验证 |
| 2026-06-23 | 第七次更新：ComboChain 删除→DeriveNextAnim 推导；SABehaviorParamImporter/SekiroCombatData 删除；SAImport Mode4 删除。Jump 系统完善 |
| 2026-06-23 | 第八次更新：状态机重构。新增 ESKCharacterState 枚举(8状态)，ProcessIntents 改为 switch-case 状态迁移，OnActionMontageEnded 统一处理动画结束迁移，落地检测自动回 Idle，CanTransition+TransitionTo 统一迁移规则 |
| 2026-06-24 | 第九次更新：修复 AIBridge 输入模拟脉冲释放（SchedulePulseRelease）；新增 TAE 事件数据提取脚本 tae_extractor.py；Sekiro_TAE_Logic.json 已重新生成（64/65 成功）；新增任务 4.1-4.5 TAE 事件驱动 AnimID 分类 |
