# 只狼动画+输入系统复刻 / 1. 新建输入处理组件 — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/sekiro-anim-input-replica.md) | 1. 新建输入处理组件 USKInputHandler | ✅ 已完成 | 2026-06-15 |

## 任务描述

创建 USKInputHandler 组件，负责接收 Enhanced Input 事件，转换为动作意图，供 USKAnimationController 消费。

## 架构

```
Enhanced Input
      │
      ▼
ASKCharacter::SetupPlayerInputComponent()
      │  调用 USKInputHandler::SetupInput(EnhancedInputComponent)
      ▼
┌─────────────────────────────────────┐
│  USKInputHandler : UActorComponent  │
│                                     │
│  持有：17 个 UInputAction 引用       │
│       UInputMappingContext 引用      │
│                                     │
│  绑定：每个 Action → 内部回调        │
│                                     │
│  输出（动作意图）：                   │
│    MoveIntent (FVector2D)            │
│    LookIntent (FVector2D)            │
│    bAttackPressed / bAttackHeld     │
│    bGuardHeld                       │
│    bDodgePressed / bDodgeHeld       │
│    bJumpPressed                     │
│    bUseItemPressed                  │
│    bGrapplePressed                  │
│    bProstheticPressed               │
│    bInteractPressed                 │
│    bHealingGourdPressed             │
│    bCycleItemNext / Prev            │
│    bLockOnPressed                   │
│    bCrouchToggled                   │
│    bPausePressed / bMenuPressed     │
│    ... + 长按时间 / 按下次序         │
└─────────────────────────────────────┘
```

## 输入状态机

| 功能 | 逻辑 |
|------|------|
| 短按/长按判定 | Attack、Prosthetic 记录按下时间戳，长按 > 0.3s 触发蓄力 |
| 组合键 | Sprint = Dodge 按住 + MoveIntent 非零 |
| 连段计数 | AttackPressed 在冷却窗口内连续触发 → ComboIndex++ |
| 单次触发消费 | JumpPressed / InteractPressed 等按下即触发，读取后自动清零 |

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| Source/Sekiro/Input/SKInputHandler.h | **新建** | 组件声明 |
| Source/Sekiro/Input/SKInputHandler.cpp | **新建** | 组件实现 |
| Source/Sekiro/Character/SKCharacter.h | 修改 | 移除 17 个 InputAction + MappingContext 成员；添加 USKInputHandler 组件指针；SetupPlayerInputComponent 改为转发 |
| Source/Sekiro/Character/SKCharacter.cpp | 修改 | 创建 USKInputHandler 子对象；输入绑定的所有 BindAction 调用迁移到 USKInputHandler |

## API 设计

### 初始化

```cpp
// 由 ASKCharacter::SetupPlayerInputComponent 调用
void SetupInput(UEnhancedInputComponent* Input);
```

### 消费型意图（读取后自动复位）

```cpp
bool ConsumeAttackPressed();      // 攻击按下
bool ConsumeJumpPressed();        // 跳跃按下
bool ConsumeDodgePressed();       // 闪避按下
bool ConsumeInteractPressed();    // 交互按下
bool ConsumeUseItemPressed();     // 道具使用
bool ConsumeHealingGourdPressed();// 伤药葫芦
bool ConsumeGrapplePressed();     // 钩索
bool ConsumeProstheticPressed();  // 义手忍具
bool ConsumeLockOnPressed();      // 锁定
bool ConsumeCrouchToggled();      // 蹲下切换
bool ConsumeCycleItemNext();      // 切换道具下一个
bool ConsumeCycleItemPrev();      // 切换道具上一个
bool ConsumePausePressed();       // 暂停
bool ConsumeMenuPressed();        // 菜单
```

### 持续型意图

```cpp
FVector2D GetMoveIntent() const;        // 移动方向（归一化）
FVector2D GetLookIntent() const;        // 视角方向
bool IsAttackHeld() const;              // 攻击键按住
bool IsGuardHeld() const;               // 防御键按住
bool IsDodgeHeld() const;               // 闪避键按住
float GetAttackHoldTime() const;        // 攻击长按时间（秒）
float GetProstheticHoldTime() const;    // 义手长按时间（秒）
```

### 连段

```cpp
int32 GetComboIndex() const;            // 当前连段序号
float GetTimeSinceLastAttack() const;   // 距上次攻击时间
```

### TickComponent

每帧：消费型意图自动清零，计算长按持续时间，连段窗口超时复位。

## 迁移映射（ASKCharacter → USKInputHandler）

| ASKCharacter 现有成员 | → USKInputHandler |
|----------------------|-------------------|
| Move(const FInputActionValue&) | 内部回调 + 写入 MoveIntent |
| Look(const FInputActionValue&) | 内部回调 + 写入 LookIntent |
| SprintPressed/Released() | 内部回调 + Dodge 按住状态 |
| CrouchToggle() | 内部回调 + bCrouchToggled 标记 |
| DodgePressed/Released() | 内部回调 + bDodgePressed / bDodgeHeld |
| AttackPressed/Released() | 内部回调 + bAttackPressed / bAttackHeld |
| GuardPressed/Released() | 内部回调 + bGuardHeld |
| 其他桩函数（LockOn/Prosthetic/Grapple/Interact/UseItem/HealingGourd/CycleItem/Pause/Menu） | 内部回调 + 对应意图标记 |
| 所有 UPROPERTY UInputAction* (17个) | 迁移到 USKInputHandler |
| DefaultMappingContext | 迁移到 USKInputHandler |
| bIsDodging / DodgeDirection / DodgeDirectionLateral / bAllowAirDodge | 保留在 ASKCharacter（移动状态，非输入） |
| LookSensitivityYaw/Pitch / bInvertPitch | 保留在 ASKCharacter（视角配置） |

## 负责 Agent

- **Agent**：gameplay-programmer
- **输入**：上述接口设计 + 现有 ASKCharacter 代码
- **依赖**：无

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建 |
