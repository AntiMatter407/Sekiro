# 只狼动画+输入系统复刻 / 4. 基础移动 — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/sekiro-anim-input-replica.md) | 4. 基础移动 | 🔄 进行中 | 2026-06-15 |

## 任务描述

实现 Locomotion 层（Priority=0）：Idle / Walk / Jog / Run / Sprint 五级移动 + Crouch + Turn，采用**离散动画+过渡动画**方式，不依赖 BlendSpace。

## 架构

```
TickComponent (每帧):
  │
  ├── 1. UpdateFrameState()
  ├── 2. ApplyFrameFlags()
  ├── 3. ProcessIntents()        ← 战斗意图（Priority 2-7），已有
  │
  └── 4. ProcessLocomotion()     ← 新增：移动层（Priority 0）
         │
         ├── 读取 Speed/Angle（从 SKAnimInstance）
         ├── 判定 MovementTier (Idle/Walk/Jog/Run/Sprint/Crouch)
         ├── 判定 Direction (Fwd/Bwd/L/R/Fwd_L/Fwd_R/Bwd_L/Bwd_R)
         ├── 确定目标 LocomotionState
         ├── State 变化？
         │   ├── Tier 升降 → 播放过渡动画 → 新循环动画
         │   ├── Direction 变化 → 直接 Crossfade 到新方向循环动画
         │   └── Speed→0 → 播放 Stop 过渡 → Idle
         └── Turn in place（Speed≈0 且 Angle 超阈值）→ 播放 Turn 动画
```

## 速度层级映射

| Tier | Speed 范围 (cm/s) | 循环动画（Fwd） | AnimID |
|------|-------------------|-----------------|--------|
| Idle | 0 ~ 50 | Idle_Default | 000000 |
| Walk | 50 ~ 200 | Walk_Fwd | 000100 |
| Jog | 200 ~ 400 | Jog_Fwd | 000200 |
| Run | 400 ~ 525 | Run_Fast_Fwd | 000400 |
| Sprint | 525+ | Sprint_Fwd | 000300 |

## 方向→AnimID 映射

| Direction | Angle 范围 | Walk | Jog | Run | Sprint |
|-----------|-----------|------|-----|-----|--------|
| Fwd | -22.5° ~ 22.5° | 000100 | 000200 | 000400 | 000300 |
| Fwd_L | 22.5° ~ 67.5° | 000101 | 000201 | 000401 | 000301 |
| L | 67.5° ~ 112.5° | 000120 | — | 000420 | — |
| Fwd_R | -67.5° ~ -22.5° | 000102 | 000202 | 000402 | 000302 |
| R | -112.5° ~ -67.5° | 000122 | — | 000422 | — |
| Bwd | 157.5° ~ -157.5° | 000110 | — | — | — |
| Bwd_L | 112.5° ~ 157.5° | 000111 | — | — | — |
| Bwd_R | -157.5° ~ -112.5° | 000112 | — | — | — |

## 过渡动画

| 过渡 | AnimID | 说明 |
|------|--------|------|
| Walk→Idle | 020000 | Walk_To_Idle |
| Walk→Jog | 020010 | Walk_To_Jog |
| Run→Idle | 023000 | Run_To_Idle |
| Run→Walk | 023200 | Run_To_Walk |
| Run→Jog | 023300 | Run_To_Jog |
| Sprint→Run | 024200 | Sprint_To_Run |
| Sprint→Jog | 024300 | Sprint_To_Jog |
| Sprint→Idle | 000500 | Sprint_To_Idle |
| Walk→Stop | 000103 | Walk_Fwd_Stop |
| Jog→Stop | 000203 | Jog_Fwd_Stop |
| Sprint→Stop | 000303 | Sprint_Fwd_Stop |
| Run→Stop | 000403 | Run_Fast_Fwd_Stop |

## 转身动画

| 角度 | 左转 AnimID | 右转 AnimID |
|------|------------|------------|
| 45° | 005000 (Turn_L45) | 005100 (Turn_R45) |
| 90° | 005010 (Turn_L90) | 005110 (Turn_R90) |
| 135° | 005200 (Turn_L135) | 005300 (Turn_R135) |
| 180° | 005400 (Turn_L180) | 005500 (Turn_R180) |

## 实现方案

### 4.1 SKAnimInstance 清理 BlendSpace

移除 `CachedBlendSpacePlayer`、`FAnimNode_BlendSpacePlayer` 反射 hack、`AnimBlueprintGeneratedClass` include。保留 Speed/Angle 计算。

### 4.2 USKAnimationController 新增 ProcessLocomotion

```cpp
// ── 移动层数据结构 ────────────────────────────────────
struct FSKLocomotionState
{
    ESKMovementTier Tier = ESKMovementTier::Run;
    FSKLocomotionDirection Direction = FSKLocomotionDirection::Fwd;
    int32 AnimID = 0;
    bool bIsMoving = false;
};

// 新增方法
void ProcessLocomotion();                              // 移动层主逻辑
FSKLocomotionState EvaluateLocomotionState();          // 速度→状态
int32 ResolveLocomotionAnimID(const FSKLocomotionState& State);  // 状态→AnimID
int32 GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To);  // 过渡动画
int32 GetTurnAnimID(float Angle);                      // 转身动画
void PlayLocomotionMontage(int32 AnimID, bool bLooping);  // 播放移动 Montage

// 新增成员
FSKLocomotionState CurrentLocoState;                   // 当前移动状态
float LastAngle = 0.f;                                 // 上一帧角度（转身判定用）
float TurnCooldown = 0.f;                              // 转身冷却
```

### 4.3 ProcessLocomotion 核心逻辑

```cpp
void USKAnimationController::ProcessLocomotion()
{
    // 仅当无战斗动作或当前在移动层时处理
    if (CurrentPriority > ESKActionPriority::Locomotion) return;

    // 1. 读取当前速度/角度
    float Speed = 0.f, Angle = 0.f;
    if (USKAnimInstance* AI = GetAnimInstance())
    {
        Speed = AI->Speed;
        Angle = AI->Angle;
    }

    // 2. 转身判定（原地 + 角度变化 > 阈值）
    if (Speed < 50.f && CurrentLocoState.bIsMoving)
    {
        // 刚停下 → 播放 Stop 过渡
        int32 StopAnim = GetStopAnimID(CurrentLocoState);
        if (StopAnim > 0)
            PlayLocomotionMontage(StopAnim, false);
        CurrentLocoState.bIsMoving = false;
        return;  // 等待 Stop 播完再切 Idle（通过 Montage 结束回调或下次 Tick）
    }

    // 3. 计算目标状态
    FSKLocomotionState Target = EvaluateLocomotionState(Speed, Angle);

    // 4. 同一循环动画，无需切换
    if (Target.AnimID == CurrentLocoState.AnimID) return;

    // 5. Tier 变化 → 过渡动画 → 新循环
    int32 TransID = GetTransitionAnimID(CurrentLocoState, Target);
    if (TransID > 0)
    {
        PlayLocomotionMontage(TransID, false);  // 非循环过渡
        // 过渡播完后切目标循环（由 Montage 结束事件触发 PlayLocomotionMontage(Target.AnimID, true)）
    }
    else
    {
        // 同 Tier 方向切换：直接 Crossfade
        PlayLocomotionMontage(Target.AnimID, true);
    }

    CurrentLocoState = Target;
}
```

### 4.4 过渡动画衔接

播放过渡 Montage 后，通过 `FOnMontageEnded` 委托自动衔接目标循环动画：

```cpp
void USKAnimationController::OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (bInterrupted) return;

    // 过渡结束 → 播放目标循环动画
    FSKLocomotionState Target = EvaluateLocomotionState(/* current Speed/Angle */);
    PlayLocomotionMontage(Target.AnimID, true);
    CurrentLocoState = Target;
}
```

### 4.5 Montage 循环

Montage 播放时通过 Section 实现循环：

```cpp
void USKAnimationController::PlayLocomotionMontage(int32 AnimID, bool bLooping)
{
    UAnimMontage* Montage = LoadMontage(AnimID);
    if (!Montage) return;

    if (bLooping)
    {
        // 设置 Default 段为循环
        AnimInst->Montage_Play(Montage);
        AnimInst->Montage_SetNextSection(TEXT("Default"), TEXT("Default"));
    }
    else
    {
        AnimInst->Montage_Play(Montage);
        // 绑定结束回调，衔接后续循环
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &USKAnimationController::OnLocoTransitionEnded);
        AnimInst->Montage_SetEndDelegate(EndDelegate, Montage);
    }
}
```

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| Source/Sekiro/Animation/SKAnimInstance.h | **修改** | 移除 CachedBlendSpacePlayer，清理 BlendSpace 残留 |
| Source/Sekiro/Animation/SKAnimInstance.cpp | **修改** | 移除 BlendSpace X 轴写入，保留 Speed/Angle |
| Source/Sekiro/Animation/SKAnimationController.h | **修改** | 新增 ProcessLocomotion + 移动状态结构 |
| Source/Sekiro/Animation/SKAnimationController.cpp | **修改** | 实现移动层逻辑 |

## 负责 Agent

- **Agent**：gameplay-programmer
- **输入**：上述接口设计 + 现有 USKAnimationController / SKAnimInstance / SKAnimationNameMap
- **依赖**：Task 1 ✅ + Task 2 ✅ + Task 3 ✅

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建 |
