# 只狼动画+输入系统复刻 / 2. TAE 数据 → UE DataAsset — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/sekiro-anim-input-replica.md) | 2. TAE 数据 → UE DataAsset | ✅ 已完成 | 2026-06-15 |

## 任务描述

将原版只狼 TAE JSON 数据（2209 动画，60000+ 事件）完整转化为 UE DataAsset，运行时组件只读资产，不碰 JSON。

## 现状分析

**已有**：
- `SekiroTAEImporter` — JSON → IR 管线完整（ParseAnimationEntry / ExtractCancelWindows / BuildTransitions / InferCategoryFromAnimID）
- `USKAnimationLogicData` — DataAsset 骨架（CancelRules / AttackHitboxConfigs / SpEffectConfigs / AnimNameMap / CategoryAnimMap）
- `SekiroAnimNotifies` — 5 种 Notify 类型
- `FSKAnimLogicImportResult` — 完整 IR 结构（FSKAnimationLogicIR / FSKStateMachineIR）

**缺失**：
- **IR → DataAsset 序列化管线** — IR 只在内存，未落地为 .uasset
- **帧级 JumpTable 行为标志** — DataAsset 缺少 bDisableTurning / bEnableParry / bInvincible 等逐帧标志
- **30+ 个高频 JumpTable ID 未映射** — 133/134/31/32/26/137/154/51/28/11 等仍返回 ESKJumpTableAction::None
- **AttackHitbox 多盒支持** — 当前单盒，原版一个动画可有多攻击盒
- **Notify 未自动注入** — Notify 类型已有但管线未注入到动画序列

## 实现方案

### 2.1 完善 DataAsset 结构

```cpp
// 帧级行为标志（精简存储：只在变化点记录）
USTRUCT(BlueprintType)
struct FSKFrameFlags
{
    GENERATED_BODY()

    UPROPERTY() uint32 bDisableTurning : 1;      // JumpTableID 7
    UPROPERTY() uint32 bDisableMovement : 1;     // JumpTableID 89
    UPROPERTY() uint32 bDisableMapHit : 1;       // JumpTableID 19
    UPROPERTY() uint32 bEnableParry : 1;         // JumpTableID 119
    UPROPERTY() uint32 bDisableParry : 1;        // JumpTableID 137
    UPROPERTY() uint32 bDisableSpecial : 1;      // JumpTableID 133（义手/战技）
    UPROPERTY() uint32 bDisableItem : 1;         // JumpTableID 134
    UPROPERTY() uint32 bInvincible : 1;          // JumpTableID 51
    UPROPERTY() uint32 bSetNoGravity : 1;        // JumpTableID 27
    UPROPERTY() uint32 bFlagAsDodging : 1;       // JumpTableID 8
    UPROPERTY() uint32 bInvokeDeath : 1;         // JumpTableID 12
    UPROPERTY() uint32 bLimitMoveSpeedWalk : 1;  // JumpTableID 90
    UPROPERTY() uint32 bLimitMoveSpeedDash : 1;  // JumpTableID 91
    UPROPERTY() uint32 bEnterMovement : 1;       // JumpTableID 32
    UPROPERTY() uint32 bExitMovement : 1;        // JumpTableID 31
    UPROPERTY() uint32 bStaggered : 1;           // JumpTableID 55
};

// 帧级数据（关键帧存储，减少冗余）
USTRUCT(BlueprintType)
struct FSKAnimFrameData
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<int32> KeyFrames;                     // 关键帧号

    UPROPERTY()
    TArray<FSKFrameFlags> Flags;                 // 对应标志（与 KeyFrames 同索引）
};

// 攻击盒列表
USTRUCT(BlueprintType)
struct FSKAttackHitboxList
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FSKAttackHitboxConfig> Hitboxes;
};
```

`USKAnimationLogicData` 修改：

```cpp
// 修改：多盒支持
UPROPERTY()
TMap<int32, FSKAttackHitboxList> AttackHitboxConfigs;

// 新增：帧级 JumpTable 标志
UPROPERTY()
TMap<int32, FSKAnimFrameData> AnimFrameFlags;

// 新增查询接口
UFUNCTION()
bool GetFrameFlags(int32 AnimID, int32 Frame, FSKFrameFlags& OutFlags) const;
```

### 2.2 IR → DataAsset 管线

新建 `FSekiroAnimDataBuilder`（`Plugins/SekiroImport/`）：

```
FSKAnimLogicImportResult (IR)
        │
        ▼  FSekiroAnimDataBuilder::BuildDataAsset(IR, OutDataAsset)
        │
        │  For each (AnimID, AnimLogic) in AnimLogicMap:
        │    ├── CancelWindows → CancelRules (window-based, existing)
        │    ├── AttackHitboxes → AttackHitboxConfigs (multi-hitbox)
        │    ├── SpEffects → SpEffectConfigs (existing)
        │    ├── JumpTable events → AnimFrameFlags (new: frame-level flags)
        │    └── AnimName / InferredCategory → AnimNameMap / CategoryAnimMap
        │
        ▼
USKAnimationLogicData (.uasset)
```

### 2.3 补充 JumpTable ID 映射

`ESKJumpTableAction` 新增枚举值：

| JumpTableID | 枚举值 | 出现次数 | 含义 |
|-------------|--------|---------|------|
| 3 | `SetTurnSpeed` | 139 | 转向速度设置 |
| 11 | `SwitchHKS`:ayer | 999 | HKS 动画层切换 |
| 26 | `GenericCancelStart` | 1,096 | 通用取消窗口（不限定武器类型） |
| 28 | `SetMoveSpeedNormal` | 949 | 恢复正常移速 |
| 31 | `ExitMovement` | 1,158 | 退出移动/锁定步伐 |
| 32 | `EnterMovement` | 1,172 | 进入移动状态 |
| 50 | `ActionRestriction` | 227 | 动作限制（禁移动+转向） |
| 51 | `InvincibilityFrame` | 641 | 无敌帧 |
| 55 | `StaggerFlag` | 212 | 硬直标志 |
| 63 | `SpecialActionFlag` | 240 | 特殊动作标志 |
| 65 | `LookAtTarget` | 191 | 追踪目标 |
| 133 | `DisableSpecial` | 1,420 | 禁止义手/战技 |
| 134 | `DisableItem` | 1,398 | 禁止道具 |
| 137 | `DisableParry` | 1,091 | 禁止弹刀（与 119 EnableParry 互斥） |
| 154 | `ItemUseWindow` | 1,049 | 道具使用开启窗口 |

`MapJumpTableToAction()` 补全映射，`ExtractCancelWindows()` 扩展识别 GenericCancel(26) 和 ItemUseWindow(154)，新增 `ExtractFrameFlags()` 解析 JumpTable 事件提取帧级标志。

### 2.4 验证覆盖

Python 管线验证脚本：
- 动画覆盖率 = 100%（2209/2209 AnimID 在 DataAsset 中）
- CancelRules 条目数 > 0
- AnimFrameFlags 条目数 > 0
- 高频 JumpTable ID 无 None 漏网
- SpEffectConfigs / AttackHitboxConfigs 条目数验证

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Plugins/SekiroImport/Public/SekiroAnimLogicData.h` | **修改** | 新增 FSKFrameFlags / FSKAnimFrameData / FSKAttackHitboxList；USKAnimationLogicData 新增 AnimFrameFlags + GetFrameFlags()；修改 AttackHitboxConfigs 为多盒 |
| `Plugins/SekiroImport/Private/SekiroAnimLogicData.cpp` | **修改** | 新增 GetFrameFlags() 帧级查询实现 |
| `Plugins/SekiroImport/Public/SekiroAnimLogicIR.h` | **修改** | ESKJumpTableAction 新增 15+ 枚举值；FSKAnimationLogicIR 新增 JumpTableFlags 帧级标志数组 |
| `Plugins/SekiroImport/Private/SekiroTAEImporter.cpp` | **修改** | MapJumpTableToAction 补全 ID；ExtractCancelWindows 扩展识别；新增 ExtractFrameFlags() |
| `Plugins/SekiroImport/Public/SekiroAnimDataBuilder.h` | **新建** | IR → DataAsset 构建器声明 |
| `Plugins/SekiroImport/Private/SekiroAnimDataBuilder.cpp` | **新建** | IR → DataAsset 序列化实现 |
| `Script/build_anim_data.py` | **新建** | Python 管线脚本：调用管线产出 DataAsset 并验证 |

## 数据流

```
Sekiro_TAE_Logic.json (512KB)
        │
        ▼  FSekiroTAEImporter::ImportFromFile()
        │
FSKAnimLogicImportResult (IR, 内存中)
        │
        ▼  FSekiroAnimDataBuilder::BuildDataAsset()
        │
USKAnimationLogicData (.uasset)  ← 运行时直接加载
        │
        ├── CancelRules: AnimID → [FSKCancelRule]
        ├── AnimFrameFlags: AnimID → FSKAnimFrameData
        ├── AttackHitboxConfigs: AnimID → FSKAttackHitboxList
        ├── SpEffectConfigs: AnimID → [FSKSpEffectConfig]
        ├── AnimNameMap: AnimID → "Sekiro_Attack_R1_Combo01"
        └── CategoryAnimMap: "Attack" → [100000, 100001, ...]
```

## 负责 Agent

- **Agent**：plugin-programmer
- **输入**：上述接口设计 + 现有 IR/DataAsset/TAEImporter 代码
- **依赖**：无

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建 |
