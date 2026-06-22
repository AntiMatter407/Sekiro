# 只狼动画+输入系统复刻 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [需求](../plan/sekiro-anim-input-replica.md) | 🔄 进行中（暂停，等待管线修复） | 2026-06-15 | 2026-06-22 |

## 架构总览

```
Extracted/Sekiro_TAE_Logic.json (28MB, 21148个JT事件)
        │
        ▼  FSATAEImporter::ImportFromFile()
        │
  FSAAnimLogicImportResult (IR)
  ┌─────────────────────────────────┐
  │ AnimLogicMap: AnimID → Logic   │
  │   · CancelWindows [JT=25/26/115/117/118/154]│
  │   · AttackHitboxes [Type=1]     │
  │   · JumpTableFlags [JT=7,51,...]│
  │   · SpEffects [Type=67]         │
  │   · Sound/FFX/Rumble/Camera     │
  └────────────────┬────────────────┘
                   │  FSATAELogicBuilder::BuildDataAsset()
                   ▼
  USKAnimationLogicData (.uasset, DataAsset)
  ┌─────────────────────────────────┐
  │ CancelRules: AnimID→[FSKCancelRule] │
  │ AttackHitboxConfigs: AnimID→[Hitbox]│
  │ AnimFrameFlags: AnimID→FrameData│
  │ SpEffectConfigs                 │
  │ CategoryAnimMap                 │
  │ AnimPrefixMap                   │
  └────────────────┬────────────────┘
                   │  运行时加载
                   ▼
  USKAnimationController::TryPlayAction()
  → CanCancelTo(CurrentAnimID, CurrentFrame, TargetAction)
    └─ CancelRules[AnimID] 中 TargetAction 匹配且帧覆盖?

  ProcessIntents():
    Priority 降序遍历:
      Deathblow(10) > Hit(8) > Dodge(7) > Deflect(6)
      > Guard/Jump(5) > Prosthetic(4) > Item(3)
      > Attack(2) > Quickstep(1) > Locomotion(0)

  USKInputHandler:
      17个 EnhancedInput Action → 消费型/持续型意图
      输入缓冲队列 (6帧)

  SKAnimInstance:
      Speed/Angle / bCanDeflect / bDisableTurning / ...
```

---

## 第一章：管线修复

### 1.1 现状问题

`SATAEImporter::ExtractCancelWindows` 的当前逻辑：

```cpp
// 当前实现：只处理 "CancelStart"，完全丢弃 "CancelEnd"
bool bIsCancelStart = TypeNameLower.Contains("cancelstart")
    || JumpTableID in {1,9,21,25,26,30,105,111,120};

if (bIsCancelStart) {
    // 硬编码 TargetAction
    switch (JumpTableID) {
        case 26: TargetAction = "Attack"; break;  // Generic→Attack
        case 25: TargetAction = "Dodge";  break;
    }
    // 窗口 = 事件自身的 StartFrame ~ EndFrame
    OutWindows.Add({StartFrame, EndFrame, TargetAction, ...});
}

// JT=115/117/118 进入 bIsCancelEnd 分支 → 什么都不做，直接跳过
```

**问题**：
1. JT=1/R1CancelStart 在 TAE 中出现 0 次，JT=9/L1 出现 0 次
2. JT=115/R1CancelEnd 出现 1217 次——完全被丢弃
3. 28 个 JT ID 在 `MapJumpTableToAction` 中未定义

### 1.2 修正方案

`ESKJumpTableAction` 补充高频未映射 JT ID：

| JT ID | 枚举值 | 出现次数 | 含义 |
|-------|--------|---------|------|
| 16 | `SpecialAttackVariant` | 82 | 特殊攻击变体 |
| 24 | `Flag24` | 20 | 未知 |
| 39 | `Flag39` | 107 | 姿态相关 |
| 54 | `Flag54` | 14 | 未知 |
| 56 | `CounterFlag` | 143 | 反击相关 |
| 69 | `Flag69` | 29 | 未知 |
| 72 | `WeaponAction` | 133 | 特定武器动作 |
| 95 | `Flag95` | 28 | 未知 |
| 110 | `Flag110` | 14 | 未知 |
| 125-128 | `Flag125-128` | 1~65 | 未知 |
| 132 | `Flag132` | 4 | 未知 |
| 136 | `Flag136` | 29 | 未知 |
| 140-141 | `Flag140-141` | 13~132 | 与义手相关 |
| 143 | `Flag143` | 71 | 未知 |
| 145-146 | `RumbleCamFlag` | 41~119 | 镜头震动 |
| 148 | `SlowMotionFlag` | 121 | 慢动作/时间控制 |
| 149-151 | `Flag149-151` | 15~77 | 未知 |
| 155-158 | `Flag155-158` | 1~14 | 未知 |

`ExtractCancelWindows` 修正后的逻辑：

```cpp
// 对每个 JumpTable 事件：
switch (JumpTableID) {
    // ======= R1/Attack 取消窗口 =======
    case 115:  // AnimCancelEnd_R1 — "在这个帧区间内，R1可以取消"
        TargetAction = "Attack";
        break;
    case 26:   // GenericCancelStart — "通用取消窗口，通常用于攻击连段"
        TargetAction = "Attack";
        break;

    // ======= L1/Guard 取消窗口 =======
    case 117:  // AnimCancelEnd_L1 — "在这个帧区间内，L1可以取消"
        TargetAction = "Guard";
        break;

    // ======= L2/Prosthetic 取消窗口 =======
    case 118:  // AnimCancelEnd_L2 — "在这个帧区间内，L2可以取消"
        TargetAction = "Prosthetic";
        break;

    // ======= ◻/Dodge 取消窗口 =======
    case 25:   // AnimCancelStart_Dodge — "在这个帧区间内，◻可以取消"
        TargetAction = "Dodge";
        break;

    // ======= ○/Item 取消窗口 =======
    case 154:  // ItemUseWindow — "在这个帧区间内，○可以使用道具"
        TargetAction = "Item";
        break;

    default:
        continue;  // 非取消窗口事件，跳过
}

// 窗口 = 事件自身的 [StartFrame, EndFrame]
OutWindows.Add({StartFrame, EndFrame, TargetAction, CrossfadeDuration});
```

### 1.3 DataAsset 重建

修复管线后，需要重新生成 `.uasset`：

```bash
# bridge.py 或手动运行 Commandlet
"$UE_ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "$PROJECT_PATH" -run=SekiroAnimDataBuild -output=/Game/Characters/Sekiro/DA_Sekiro_AnimLogic
```

验证：

```python
# build_anim_data.py 验证脚本
data = load_asset("DA_Sekiro_AnimLogic")
assert data.CancelRules.Num() > 0
for anim_id, rules in data.CancelRules:
    for rule in rules:
        # 每个 CancelRule 必须有有效的 TargetAction 和帧范围
        assert rule.TargetAction != NAME_None
        assert rule.StartFrame < rule.EndFrame
```

---

## 第二章：运行时修复

### 2.1 输入缓冲

**原版设计**：6帧输入缓冲（60fps 下约 100ms）

```
输入产生 → [Buffer: 6帧] → 状态机采样 → 找到合法转换 → 播放动画
                              │
                      (找不到合法转换 → 丢弃)
```

**实现方案**：

```cpp
// USKInputHandler 新增

// 缓冲队列条目
struct FSKBufferedInput {
    FName Action;           // "Attack", "Guard", "Dodge", "Jump"...
    int32 Priority;         // 对应 ESKActionPriority
    float Timestamp;        // 世界时间戳
    float Lifetime;         // 最大缓冲寿命（秒，默认 6帧=0.1s）
};

// 缓冲队列
TArray<FSKBufferedInput> InputBuffer;

// 输入回调中：添加缓冲条目
void OnAttackStarted(const FInputActionValue& Value) {
    InputBuffer.Add({"Attack", ESKActionPriority::Attack,
                     GetWorld()->GetTimeSeconds(), 0.1f});
}

// Tick 中：消费缓冲
void ProcessInputBuffer() {
    // 1. 按 Priority 降序排序
    InputBuffer.Sort([](auto& A, auto& B) { return A.Priority > B.Priority; });

    // 2. 遍历缓冲条目
    for (auto& Entry : InputBuffer) {
        if (AnimController->TryPlayAction(Entry.Action, Entry.Priority)) {
            // 成功消费 → 移除
            InputBuffer.RemoveAt(0);
            break;  // 一次 Tick 只消费一个最高优先级输入
        }
    }

    // 3. 移除超时条目
    float Now = GetWorld()->GetTimeSeconds();
    InputBuffer.RemoveAll([&](auto& E) { return Now - E.Timestamp > E.Lifetime; });
}
```

### 2.2 ProcessIntents 优先级遍历

**原版设计**：优先级从高到低，每个输入逐项检查是否在当前动画的 CancelWindow 内

**当前代码**（错误）：

```cpp
void USKAnimationController::ProcessIntents() {
    if (InputHandler->ConsumeDodgePressed())   { HandleDodge(); return; }
    if (InputHandler->ConsumeJumpPressed())    { HandleJump(); return; }
    if (InputHandler->IsGuardHeld())           { HandleGuard(); return; }
    if (InputHandler->ConsumeAttackPressed())  { HandleAttack(); return; }
    // Dodge 优先于 Attack — 即使 Attack 在当前帧有 CancelWindow 而 Dodge 没有
}
```

**修正后**：

```cpp
void USKAnimationController::ProcessIntents() {
    struct FIntent {
        FName Action;
        int32 Priority;
        TFunction<bool()> Handler;  // 返回 true=已消费
    };

    // 按 Priority 降序排列
    TArray<FIntent> Intents = {
        {"Deathblow",  10, [&]{ return HandleDeathblow(); }},
        {"Hit",         8, [&]{ return InputHandler->ConsumeInteractPressed(); }},
        {"Dodge",       7, [&]{ return InputHandler->ConsumeDodgePressed()  && HandleDodge(); }},
        {"Deflect",     6, [&]{ return InputHandler->IsGuardHeld() && HandleDeflect(); }},
        {"Guard",       5, [&]{ return InputHandler->IsGuardHeld() && HandleGuard(); }},
        {"Jump",        5, [&]{ return InputHandler->ConsumeJumpPressed() && HandleJump(); }},
        {"Prosthetic",  4, [&]{ return InputHandler->ConsumeProstheticPressed() && HandleProsthetic(); }},
        {"Item",        3, [&]{ return InputHandler->ConsumeUseItemPressed() && HandleItemUse(); }},
        {"Attack",      2, [&]{ return InputHandler->ConsumeAttackPressed() && HandleAttack(); }},
    };

    for (auto& I : Intents) {
        // 低优先级不能打断当前动作
        if (I.Priority <= CurrentPriority && CurrentAnimID > 0) continue;

        // 调用 handler（内部会调用 TryPlayAction → CanCancelTo）
        if (I.Handler()) return;
    }
}

// HandleAttack 内部：
bool USKAnimationController::HandleAttack() {
    // 上下文分支
    if (bInAir)                  return TryPlayAction("Attack_Jump", 2);
    if (CounterWindow)           return TryPlayAction("Attack_Counter", 2);
    if (DeathBlowActive)         return TryPlayAction("Deathblow", 10);

    // 普通攻击连段
    FName ChargeAction = ...; // 蓄力判定
    return TryPlayAction(ChargeAction, 2);
}

// TryPlayAction 内部使用 CanCancelTo：
bool USKAnimationController::TryPlayAction(FName Action, int32 Priority) {
    if (!AnimLogicData) return false;
    if (Priority <= CurrentPriority && CurrentAnimID > 0) return false;

    float Crossfade;
    if (!AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, Action, Crossfade))
        return false;

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0) return false;

    PlayMontageByID(AnimID, Crossfade);
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
    return true;
}
```

### 2.3 Deflect 判定

**原版设计**：
- L1 在敌人攻击判定帧前 **≤6帧** 内按下 → Deflect（完美格挡）
- L1 在敌人攻击判定帧前 **>6帧** 按下 → Guard（普通格挡）

**实现方案**：

```cpp
void USKAnimationController::HandleGuard() {
    // 1. 检查敌人当前动画是否有攻击判定
    ASKCharacter* Target = GetLockOnTarget();
    if (!Target) { HandleGuardOnly(); return; }

    int32 EnemyAnimID = Target->GetAnimController()->GetCurrentAnimID();
    float EnemyTime = Target->GetAnimController()->GetCurrentAnimTime();
    int32 EnemyFrame = FMath::RoundToInt(EnemyTime * 30.0f);

    // 2. 查敌人的攻击框是否有覆盖当前帧的
    TArray<FSKAttackHitboxConfig> ActiveHitboxes;
    AnimLogicData->GetActiveHitboxesAtFrame(EnemyAnimID, EnemyFrame, ActiveHitboxes);

    if (ActiveHitboxes.Num() > 0) {
        // 3. 计算 L1 按下时间与敌人攻击判定帧起始的时间差
        float TimeDiff = EnemyTime - (ActiveHitboxes[0].StartFrame / 30.0f);
        int32 FrameDiff = FMath::RoundToInt(TimeDiff * 30.0f);

        if (FrameDiff <= 6) {
            // ≤6帧 → Deflect（完美格挡）
            TryPlayAction("Deflect", ESKActionPriority::Deflect);
            CounterWindow = true;  // 开启反斩窗口
            return;
        }
    }

    // 普通格挡
    HandleGuardOnly();
}
```

### 2.4 转向系统

```cpp
void USKAnimationController::ProcessLocomotion() {
    // ... 已有的 EvaluateLocomotionState + Tier 过渡逻辑 ...

    // 新增：原地转身判定
    if (Speed < 50.f) {
        float AngleDelta = FMath::FindDeltaAngleDegrees(LastAngle, Angle);
        if (FMath::Abs(AngleDelta) > 90.f && TurnCooldown <= 0.f) {
            int32 TurnID = GetTurnAnimID(AngleDelta);
            if (TurnID > 0) {
                PlayLocomotionMontage(TurnID, false);
                TurnCooldown = 0.5f;  // 0.5s 冷却
                return;
            }
        }
    }

    LastAngle = Angle;
    TurnCooldown -= DeltaTime;
}
```

---

## 第三章：资产验证与测试

### 3.1 移动动画验证

验证 `CategoryAnimMap` 中的 Locomotion 类别覆盖：

| 类别 | 验证方法 | 预期 |
|------|---------|------|
| `Locomotion_Idle` | 查询 CategoryAnimMap | ≥1 个 AnimID |
| `Locomotion_Walk_Fwd/Bwd/L/R` | 同上 | 每个方向 ≥1 |
| `Locomotion_Jog_Fwd` | 同上 | ≥1（只有 Fwd，原版只狼 Jog 无侧向/后退）|
| `Locomotion_Run_Fwd/Bwd/L/R` | 同上 | 每个方向 ≥1 |
| `Locomotion_Sprint_Fwd` | 同上 | ≥1 |
| `Locomotion_Transition_*_to_*` | 同上 | 过渡条目数 |
| `Locomotion_Stop_*` | 同上 | 每个 Tier 有 Stop |
| `Locomotion_Turn_L/R` | 同上 | ≥1 每个方向 |

### 3.2 PIE 测试

| 测试项 | 方法 | 通过标准 |
|--------|------|---------|
| 五级速度切换 | 移动摇杆，观察速度 | Idle→Walk→Jog→Run→Sprint 逐级上升 |
| 八方向移动 | 推不同方向 | 角色朝八个方向正常行走 |
| Tier 过渡动画 | 加速/减速 | Walk→Jog→Run 触发过渡动画 |
| Stop 过渡 | 停止移动 | 从各速度级停止时播 Stop 动画 |
| 原地转身 | 不移动+转视角 | 角度 >90° 触发 Turn 动画 |
| 战斗打断移动 | 攻击/闪避 | 优先级自动切换，战斗结束回移动 |

---

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Plugins/SekiroAssetManager/.../SATAELogicIR.h` | **修改** | ESKJumpTableAction 补充 28 个枚举值 |
| `Plugins/SekiroAssetManager/.../SATAEImporter.h` | **修改** | MapJumpTableToAction 补充映射 |
| `Plugins/SekiroAssetManager/.../SATAEImporter.cpp` | **修改** | ExtractCancelWindows / ExtractFrameFlags 修正 |
| `Plugins/SekiroAssetManager/.../SATAELogicBuilder.cpp` | **修改** | BuildCancelRules 对齐新映射 |
| `Source/Sekiro/Input/SKInputHandler.h` | **修改** | 新增输入缓冲队列结构 |
| `Source/Sekiro/Input/SKInputHandler.cpp` | **修改** | 实现输入缓冲 + 优先级排序 + 超时丢弃 |
| `Source/Sekiro/Animation/SKAnimationController.h` | **修改** | 新增 Deflect 判定 / 转向调用 |
| `Source/Sekiro/Animation/SKAnimationController.cpp` | **修改** | ProcessIntents 重写 / HandleDeflect / HandleAttack 上下文分支 / ProcessLocomotion 转向 |
| `Source/Sekiro/Animation/SKAnimInstance.h` | **修改** | 新增帧级标志 |
| `Script/build_anim_data.py` | **修改** | 重建 DataAsset + 验证脚本 |

## 数据流

```
玩家按键
    ↓
USKInputHandler (输入缓冲队列, 6帧)
    ↓  消费意图 (ConsumeAttackPressed / IsGuardHeld / ...)
USKAnimationController::ProcessIntents()
    ↓  Priority 降序遍历
HandleAttack / HandleGuard / HandleDodge / ...
    ↓  TryPlayAction(Action, Priority)
CanCancelTo(CurrentAnimID, CurrentFrame, TargetAction)
    ↓  查 DataAsset.CancelRules[CurrentAnimID]
TAE JT=115/117/118/25/26/154 的帧窗口判定
    ↓  命中窗口?
PlayMontageByID(TargetAnimID) → 动画播放
    ↓  每帧
UpdateFrameState() → ApplyFrameFlags() → UpdateAttackHitbox()
```

## 依赖与风险

| 依赖 | 说明 |
|------|------|
| DataAsset 重建 | 管线修复后需在 UE 编辑器中重新运行 Commandlet 生成 `.uasset` |
| 动画资产加载 | `EnsureMontageLoaded` 依赖 `/Game/Characters/Sekiro/Animations/` 下的命名规则 |
| 敌人攻击系统 | Deflect 判定依赖敌人端的攻击判定框数据（当前无敌人 AI）|

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-22 | 创建：整合为单文档三章结构，覆盖管线修复+运行时修复+资产验证 |
