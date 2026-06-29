# 只狼动画+输入系统复制 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [需求](../plan/sekiro-anim-input-replica.md) | ⏸️ 搁置 | 2026-06-15 | 2026-06-27 |

---

## 一、架构总览（当前状态）

```
Extracted/Sekiro_TAE_Logic.json (28MB, 21148个JT事件)
         │
         ▼ FSATAEImporter::ImportFromFile()
         │
  FSAAnimLogicImportResult (IR)
  ┌──────────────────────────────────┐
  │ AnimLogicMap: AnimID → Logic    │
  │   · CancelWindows [JT=25/26/115/117/118/154]
  │   · AttackHitboxes [Type=1]      │
  │   · JumpTableFlags [JT=7,51,...] │
  │   · SpEffects [Type=67]          │
  │   · Sound/FFX/Rumble/Camera      │
  └──────────────────────────────────┘
                    │ FSATAELogicBuilder::BuildDataAsset()
                    ▼
  USKAnimationLogicData (.uasset, DataAsset)
  ┌──────────────────────────────────┐
  │ CancelRules: AnimID→[FSKCancelRule]
  │ AttackHitboxConfigs: AnimID→[Hitbox]
  │ AnimFrameFlags: AnimID→FrameData │
  │ CategoryAnimMap: Action→AnimIDList
  │ AnimPrefixMap: AnimID→bool       │
  └──────────────────────────────────┘
                    │ 运行时加载
                    ▼
  USKAnimationController:
    ProcessIntents(): Priority降序遍历 8 状态 switch-case
    TryPlayAction(): CanCancelTo → ResolveAnimID → PlayMontageByID
    PlayMontageByID(): PlaySlotAnimationAsDynamicMontage → DefaultSlot
```

**C++ 运行时核心已完成**。当前主要工作是**数据管线 → 运行时的最后集成**。

---

## 二、当前架构关键设计

### 2.1 状态机（ESKCharacterState）

```cpp
enum class ESKCharacterState : uint8 {
    Idle,       // 空闲/Locomotion
    Attack,     // 攻击动作中
    Guard,      // 防御/格挡中
    Dodge,      // 闪避中
    Jump,       // 起跳动画中
    Airborne,   // 空中（Jump后物理上升/下落）
    Hit,        // 受击
    Death       // 死亡
};
```

迁移规则（`TransitionTo` + `CanTransition`）：
- Idle → Attack/Guard/Dodge/Jump（任意输入触发）
- Attack → Attack（连段）/ Guard / Dodge / Airborne
- Guard → Deflect / Hit / Idle（释放L1）
- Dodge → Idle（闪避结束）
- Jump → Airborne（离地）/ Idle（若未离地）
- Airborne → AirAttack / AirDodge / Idle（落地）
- Hit → Idle（硬直结束）/ Death
- Any → Death（HP=0）

### 2.2 优先级系统

```
Deathblow(10) > Death(9) > Hit(8) > Dodge(7) > Deflect(6)
> Jump(5) = Guard(5) > Prosthetic(4) > ItemUse(3) > Attack(2)
> Quickstep(1) > Locomotion(0)
```

处理逻辑（`TryPlayAction`）：
1. 若 `CurrentPriority > NewPriority` 且动画未播完 → 拒绝
2. 若非同动作 → 检查 `CanCancelTo`（CancelWindow 覆盖当前帧）
3. `ResolveAnimID` → 查找 CategoryAnimMap 中的 AnimID
4. `PlayMontageByID` → PlaySlotAnimationAsDynamicMontage

### 2.3 连段推导（DeriveNextAnim）

当前方案为最简单递增：
```cpp
int32 DeriveNextAnim(int32 InCurrentAnimID, FName Action) const {
    int32 JudgeId = InCurrentAnimID % 1000;
    int32 NextAnimID = (InCurrentAnimID / 1000) * 1000 + (JudgeId + 1);
    return AnimLogicData->AnimPrefixMap.Contains(NextAnimID) ? NextAnimID : -1;
}
```

**限制**：只能处理同系列连续递增的连段（如 201000→201001→201002），无法处理原版的复杂连段分支。

---

## 三、数据管线集成方案（第一优先级）

### 3.1 DataTable 导入管道

**目标**：将 Python 生成的 StateAnimMap 和 StateTransitions 数据导入 UE5 DataTable，替代运行时的硬编码逻辑。

**已生成数据**：
- `Output/StateAnimMap_DataTable.json` — 142 个状态的动画映射
- `Output/StateTransitions_DataTable.json` — 状态迁移规则

**需要新建的 USTRUCT**（在 `Plugins/SekiroAssetManager/Public/`）：

```cpp
// FSKStateAnimEntry — 状态→动画映射行
USTRUCT(BlueprintType)
struct FSKStateAnimEntry : public FTableRowBase {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString State;           // 状态名
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> AnimIDs;   // 该状态的动画ID列表
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FString> TriggerEvents;  // 触发事件名列表
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Category;        // 分类标签
};

// FSKStateTransition — 状态迁移规则行
USTRUCT(BlueprintType)
struct FSKStateTransition : public FTableRowBase {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString EventName;       // 触发事件
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString FromState;       // 源状态
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString ToState;         // 目标状态
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BlendDuration = 0.2f;  // 混合时长
};
```

**SAImport Commandlet 新增模式**：
- 读取 JSON → 调用 `FDataTableImporterTools::CreateDataTable` → 生成 .uasset
- 或者：直接用 Python 调用 UnrealPython API 导入

**运行时集成**：
- `USKAnimationController` 在 `BeginPlay` 加载 DataTable
- `ResolveAnimID` 改为查表替代 `CategoryAnimMap`
- `CanTransition` 改为查表替代硬编码规则

### 3.2 TAE 曲线批量写入

**当前状态**：`Script/write_tae_curves.py` 已支持单 AnimID 写入：
```
FrameFlags → AnimSequence curves (bDisableTurning, bDisableParry, ...)
CancelActions → AnimSequence curves (CanCancelTo_Attack, CanCancelTo_Guard, ...)
AttackHitbox → AnimSequence curves (AttackHitbox 激活帧区间)
```

**需要新建**：
- `Script/write_tae_curves_batch.py` — 批量处理所有已导入的 AnimSequence
- 错误重试 + 进度日志
- 处理边界：无事件动画跳过、部分失败不阻塞整体

**验证方法**：
- PIE 中 `AnimInstance::GetCurveValue("AttackHitbox")` 验证正确帧
- 日志确认 FrameFlags 生效（攻击中 bDisableTurning = true）

### 3.3 BehaviorParam 连段方案评估

**当前**：`DeriveNextAnim` 简单递增（AnimID % 1000 + 1）

**BehaviorParam 方案可通过查表替换**：
- 已提取 BehaviorParam_PC.json（1206条）
- 关键字段：`variationId`, `behaviorJudgeId`, `refType`, `refId`
- 但这些是"行为调度"数据而非"动画连段"数据
- 原版真正的连段是通过 ezState 状态机 + TAE CancelWindow 实现的

**决策点**：行为调度（BehaviorParam）和动画连段是两层。当前 CancelWindow 已从 TAE 正确提取，连段推导用简化公式可行。**完整 BehaviorParam 解析可作为后续优化项，不阻塞当前进度**。

---

## 四、运行时验证方案（第二优先级）

### 4.1 验证脚本设计

使用 AIBridge Python API 编写自动化验证脚本 `Script/temp/test_full_anim.py`：

```python
# 测试序列
tests = [
    # Sprint 测试
    {"name": "Sprint过渡", "steps": [
        ("move", {"y": 1.0}, 0.5),        # 前移加速到Run
        ("dodge_start", {}, 0.1),          # 按住Dodge
        ("wait", {}, 1.0),                 # 等待Sprint
        ("assert_speed_ge", 550),          # 验证Speed>=550
    ]},
    # 转向测试
    {"name": "原地转向", "steps": [
        ("ensure_idle", {}, 0.5),          # 确保静止
        ("look", {"x": 180}, 0.2),         # 快速旋转180°
        ("assert_turn_anim", True),        # 验证触发Turn动画
    ]},
    # 连段测试
    {"name": "连段CancelWindow", "steps": [
        ("attack", {}, 0.1),               # R1攻击
        ("wait", {}, 0.3),                 # 进入CancelWindow
        ("attack", {}, 0.1),               # 窗内R1 → 下段
        ("assert_combo_advanced", True),   # 验证连段推进
    ]},
    # Guard→Deflect 窗口测试
    # Jump全分支测试
]
```

### 4.2 验证通过标准

| 测试项 | 标准 |
|--------|------|
| Sprint过渡 | Speed≥550 + Tier=Sprint |
| 原地转向 | TurnID>0 + 0.5s冷却第二次不触发 |
| 连段窗口 | CancelWindow内R1→第二段触发，窗外R1→不触发 |
| Jump分支 | 起跳→Airborne→空中攻击可选→落地→Idle |
| Guard窗口 | L1按下→检测敌人攻击→弹反触发 |
| 动画不卡 | OnActionMontageEnded后 Locomotion 恢复 |

---

## 五、打磨和完善（第三优先级）

### 5.1 锁敌系统
- `IsEnemyAttacking` 需要锁敌目标
- 当前实现依赖 `GetLockOnTarget()` 返回 `ASKCharacter*`
- 需配合锁敌系统（EnhancedInput LockOn Action）激活

### 5.2 Deathblow 激活
- `bDeathBlowActive = true` 需在敌人架势条满时触发
- 需配合 `USKPostureComponent`（未实现）

### 5.3 CombatArt 战技
- 当前 `HandleCombatArt` 为空壳
- 需配合义手/战技资源导入和 DataTable

---

## 六、数据流总览

```
┌──────────────┐    ┌────────────────┐    ┌──────────────────────┐
│ TAE二进制    │ →  │ SekiroTAE      │ →  │ Sekiro_TAE_Logic     │
│ .tae (65个)  │    │ Extractor (.cs) │    │ .json (28MB)         │
└──────────────┘    └────────────────┘    └──────────┬───────────┘
                                                     │
                    ┌────────────────────────────────┤
                    ▼                                ▼
           ┌──────────────┐                  ┌──────────────────┐
           │ SAImport     │                  │ Python 分析脚本  │
           │ Commandlet   │                  │ build_v2.py      │
           │ (SATAEImp)   │                  │ build_statemap.py │
           └──────┬───────┘                  └────────┬─────────┘
                  ▼                                   ▼
    ┌─────────────────────┐              ┌──────────────────────┐
    │ USKAnimLogicData    │              │ StateAnimMap_        │
    │ (DataAsset .uasset) │              │ DataTable.json       │
    │ · CancelRules       │              │ StateTransitions_    │
    │ · CategoryAnimMap   │              │ DataTable.json       │
    │ · AnimPrefixMap     │              └──────────┬───────────┘
    └──────────┬──────────┘                         │
               │                    ┌───────────────┘
               ▼                    ▼
    ┌──────────────────────────────────────┐
    │ USKAnimationController 运行时        │
    │ · ProcessIntents (Priority遍历)      │
    │ · TryPlayAction (CancelWindow判定)   │
    │ · PlayMontageByID (DefaultSlot)      │
    └──────────────────────────────────────┘
```

---

## 七、Agent 派发

| 任务 | Agent | 说明 |
|------|-------|------|
| DataTable USTRUCT 新建 | plugin-programmer | 在 Plugins/SekiroAssetManager 中添加 |
| SAImport DataTable 导入模式 | plugin-programmer | Commandlet 扩展 |
| TAE 曲线批量写入脚本 | 主 Agent (Python) | Script/temp/ |
| PIE 验证脚本 | 主 Agent (Python) + AIBridge | Script/temp/ |
| 锁敌/Deathblow/CombatArt | gameplay-programmer | Source/Sekiro/ |

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-27 | 全面重写：反映 C++ 核心完成现状，聚焦数据管线集成三优先级 |
| 2026-06-23 | 创建：聚焦第一优先级 Sprint/转向/运行时日志确认，纯验证不修改 C++ |


---

## ⏸️ 搁置 (2026-06-27)

此方案已搁置。新方向：**人工识别动画内容 + AI 状态机制作**。

保留资产：
- C++ 运行时：USKAnimationController / USKInputHandler 继续使用
- TAE 数据管线：可作为动画分类参考
- DataTable：DT_StateAnimMap / DT_StateTransitions 可作为审阅材料
