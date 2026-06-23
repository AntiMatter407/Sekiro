# 只狼动画+输入系统复刻 — 技术方案

| 进度文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [需求](../plan/sekiro-anim-input-replica.md) | 🔄 进行中 | 2026-06-15 | 2026-06-23 |

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

## 第一优先级：PIE 运行时验证 — Sprint/转向/日志确认

### 1.1 Sprint 验证

**原版行为**：
- 移动中按住 Dodge（Shift/◻）→ 从 Run 切换到 Sprint
- Sprint 中停止移动或离地 → 回到 Run
- Sprint 期间应播放 Sprint_Fwd 动画（Speed ≥ 550）

**当前实现检查**（已就位，无需改代码）：

`SKInputHandler::OnDodgeStarted`：
```cpp
void USKInputHandler::OnDodgeStarted(...) {
    bDodgeHeld = true;                          // 持续按住标记
    // ...
    if (USKMovementComponent* MoveComp = ...) {
        MoveComp->CurrentMovementTier = ESKMovementTier::Sprint;
    }
}
```

`SKInputHandler::TickComponent`（冲刺状态检查）：
```cpp
if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint) {
    if (MoveIntent.Size() < 0.1f || 离地) {
        MoveComp->CurrentMovementTier = ESKMovementTier::Run;
    }
}
```

`EvaluateLocomotionState`（速度分级）：
- Speed < 10 → Idle
- Speed < 200 → Walk
- Speed < 430 → Jog
- Speed < 550 → Run
- **≥ 550 → Sprint**

**验证方法**（AIBridge）：
```python
# 1. 前移 + Sprint
bridge.input_simulate("move", {"x": 0.0, "y": 1.0})   # 前移加速到Run
time.sleep(0.5)
bridge.input_simulate("dodge", {"started": True})      # 按住Dodge → Sprint
time.sleep(1.0)
state = bridge.get_anim_state()                        # 验证Speed≥550 + AnimID匹配Sprint

# 2. Sprint 中停止移动
bridge.input_simulate("move", {"x": 0.0, "y": 0.0})   # 松开移动
time.sleep(0.5)
state = bridge.get_anim_state()                        # 验证回退到 Run/Stop

# 3. Sprint 中离地
bridge.input_simulate("move", {"x": 0.0, "y": 1.0})
bridge.input_simulate("dodge", {"started": True})
time.sleep(0.3)
bridge.input_simulate("jump", {"started": True})       # 跳跃
time.sleep(0.3)
state = bridge.get_anim_state()                        # 验证离地后 Sprint→Run
```

### 1.2 转向动画验证

**原版行为**：
- 静止状态下视角旋转 > 90° → 触发原地转身
- 转身有 0.5s 冷却
- 方向分 L/R（根据 AngleDelta 正负）

**当前实现**（`ProcessLocomotion`）：
```cpp
if (Speed < 50.f && TurnCooldown <= 0.f) {
    float AngleDelta = FMath::FindDeltaAngleDegrees(LastAngle, Angle);
    if (FMath::Abs(AngleDelta) > 90.f) {
        int32 TurnID = GetTurnAnimID(AngleDelta);
        // ...
    }
}
```

**验证方法**：
```python
# 原地静止 → 快速旋转视角
bridge.input_simulate("move", {"x": 0.0, "y": 0.0})   # 确保静止
time.sleep(0.5)
bridge.input_simulate("look", {"x": 180.0, "y": 0.0}) # 快速转180度
time.sleep(0.3)
state = bridge.get_anim_state()                        # 验证触发了Turn动画
```

### 1.3 运行时日志确认

**当前代码已有 Tick 诊断输出**（`TickComponent`）：
```cpp
static int32 TickCounter = 0;
if (++TickCounter % 60 == 0) {
    UE_LOG(LogTemp, Log, TEXT("AnimTick[%s]: Action=%s AnimID=%d Priority=%d ..."),
        ..., *CurrentAction.ToString(), CurrentAnimID, CurrentPriority, ...);
}
```

**验证方法**：
```python
# PIE 中执行一系列操作 → 检查日志输出 → 验证 Action/AnimID 是否与预期一致
bridge.input_simulate("attack", {"started": True})      # R1攻击
time.sleep(0.3)
logs = bridge.get_output_log()                           # 获取最近日志
# 检查: 是否包含 "Action=Attack AnimID=201010" 等
```

### 1.4 CancelWindow 命中验证

**关键验证**：
- 播放攻击动画期间，R1 在 CancelWindow 内按下 → 触发连段下一击
- 播放攻击动画期间，R1 在 CancelWindow 外按下 → 不触发
- Guard 在 CancelWindow 内按下 → 触发 Guard 动画

**验证方法**：
```python
# 在攻击动作中按 R1
bridge.input_simulate("attack", {"started": True})
time.sleep(0.1)                                           # 攻击动作早期（可能在 CancelWindow 内）
bridge.input_simulate("attack", {"started": True})        # 第二下
time.sleep(0.5)                                           # 等动画接近尾声
bridge.input_simulate("attack", {"started": True})        # 第三下或不在窗口内
state = bridge.get_anim_state()
# 验证连段推进或停止
```

---

## 第二章：运行时实现（已就位，作为参考）

### 2.1 输入缓冲（已完成）

```
输入产生 → [Buffer: 6帧] → ProcessIntents 采样 → 找到合法转换 → 播放动画
                              │
                      (找不到合法转换 → 丢弃)
```

USKInputHandler 中缓冲队列的结构和工作逻辑已在 6/22 实现，无需修改。

### 2.2 ProcessIntents 优先级遍历（已完成）

Priority 降序遍历，高优先级意图优先被消费，低优先级不能打断当前动作。

当前意图优先级表：

| 意图 | 优先级 | 触发方式 |
|------|--------|---------|
| Deathblow | 10 | bDeathBlowActive + ConsumeBufferedInput("Attack") |
| Dodge | 7 | ConsumeBufferedInput("Dodge") → HandleDodge |
| Jump | 5 | ConsumeBufferedInput("Jump") → HandleJump |
| Guard | 5 | IsGuardHeld() → HandleGuard |
| Prosthetic | 4 | ConsumeBufferedInput("Prosthetic") → HandleProsthetic |
| ItemUse | 3 | ConsumeBufferedInput("Item") → HandleItemUse |
| Grapple | 2 | ConsumeBufferedInput("Grapple") → HandleGrapple |
| Attack | 2 | HandleAttack（内部消费缓冲） |

### 2.3 Deflect 判定（已完成）

```
L1 按下 → 检查敌人当前动画是否有攻击判定
  → 有且时间差 ≤6帧 → Deflect（完美格挡，开启 bCounterWindow）
  → 有且时间差 >6帧 → Guard（普通格挡）
  → 无 → Guard（普通格挡）
```

### 2.4 转向系统（已完成）

```
静止 + 角度变化 >90° + 冷却 0.5s → 播 Turn 动画
  → AngleDelta > 0 → Locomotion_Turn_R
  → AngleDelta < 0 → Locomotion_Turn_L
```

---

## 第三章：验证方案（当前重点）

### 3.1 测试环境

- AIBridge PIE 模式下运行
- 日志通过 `bridge.get_output_log()` 采集
- 动画状态通过 `bridge.get_anim_state()` 获取
- 验证脚本放在 `Script/temp/test_sprint_turn_log.py`

### 3.2 验证序列

| 序号 | 测试项 | 输入序列 | 预期输出 | 通过标准 |
|------|--------|---------|---------|---------|
| 1 | Run→Sprint 过渡 | 前移 → 加速到 Run → 按住 Dodge | Speed ≥ 550, Tier=Sprint, Action=Locomotion | 日志输出 Speed≥550 |
| 2 | Sprint→Run 回退 | Sprint 中松开移动 | Speed < 550, Tier=Run | 日志输出 Speed 下降 |
| 3 | Sprint→Jump→Run | Sprint 中跳跃再落地 | Sprint→离地→Run | Tier 变化符合预期 |
| 4 | 原地转身 | 静止 + 快速旋转视角 | 触发 Turn 动画 (TurnID > 0) | 日志输出 Turn 动作 |
| 5 | 转身冷却 | 连续快速旋转 → 第二次不触发 | 第一次 Turn，第二次不触发 | 0.5s 内第二次无 Turn |
| 6 | 连段(CancelWindow) | 攻击 → 窗口内R1 → 窗口外R1 | 第一击 → 第二击 → 不触发 | AnimID 按连段推进 |
| 7 | Guard 取消 | 攻击中按 Guard | 触发 Guard 或 Deflect | Action=Guard/Deflect |

### 3.3 失败处理

如果验证结果不符合预期：
1. 分析日志确定原因（Missing CategoryAnimMap / CancelWindow 不匹配 / BuildAnimAssetPath 错误）
2. 如果是 DataAsset 数据问题 → 重新运行 SAImport Commandlet
3. 如果是 C++ 逻辑问题 → 按照 bug-fix-workflow.md 先分析后修复

---

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Script/temp/test_sprint_turn_log.py` | **新建** | Sprint/转向/运行时日志验证脚本 |
| `Docs/plan/sekiro-anim-input-replica.md` | **更新** | 任务状态同步 |

**无需修改任何 C++ 源文件**。本次验证纯 AIBridge + Python 脚本操作。

## 数据流

```
AIBridge input_simulate
    ↓
USKInputHandler (OnMove/OnDodgeStarted/OnAttackStarted)
    ↓
USKMovementComponent (CurrentMovementTier + GetMaxSpeed)
    ↓
USKAnimInstance (Speed/Angle 计算)
    ↓
USKAnimationController::ProcessLocomotion
    ↓
PlaySlotAnimationAsDynamicMontage → ABP_Sekiro Default Slot
    ↓
TickComponent 日志输出 (每60帧)
    ↓
bridge.get_output_log() 采集验证
```

## 依赖与风险

| 依赖 | 说明 |
|------|------|
| AIBridge 连接 | 需要编辑器在 PIE 模式下运行，bridge.py 能正常连接 |
| DataAsset 数据 | CategoryAnimMap 中 Sprint/Turn 类别必须有动画ID（否则播不出） |
| 无 C++ 修改必要 | 如果验证失败，需要排查的是数据层（DataAsset）而非逻辑层 |

## Agent 派发

本次全部直接在主 Agent 中完成，不需要 C++ Agent：
- Python 验证脚本不需要 C++ 修改
- 不需要编译

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-23 | 创建：聚焦第一优先级 Sprint/转向/运行时日志确认，纯验证不修改 C++ |
| 2026-06-23 | 新增 2.5 Priority 复位：Guard 播完后 CurrentPriority 残留导致阻塞后续动作，OnActionMontageEnded 复位 CurrentPriority/Locomotion |
