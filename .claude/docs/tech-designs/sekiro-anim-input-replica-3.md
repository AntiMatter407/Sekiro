# 只狼动画+输入系统复刻 / 3. 新建动画控制组件 — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/sekiro-anim-input-replica.md) | 3. 新建动画控制组件 | ✅ 已完成 | 2026-06-15 |

## 任务描述

创建 USKAnimationController 组件，消费 USKInputHandler 的动作意图，基于 USKAnimLogicData DataAsset 的 CancelWindow 数据驱动动画选择与 Montage 播放，实现原版只狼的三层优先级状态机。

## 架构

```
TickComponent (每帧):
  │
  ├── 1. 更新当前动画状态 (CurrentFrame, CurrentAnimTime)
  ├── 2. 应用当前帧行为标志 (DataAsset->GetFrameFlags → 设置 bCanDeflect 等)
  │
  ├── 3. 处理意图（按优先级降序）：
  │      HitPressed?      → TargetAction="Hit",        Priority=8
  │      DodgePressed?    → TargetAction="Dodge",      Priority=7
  │      DeflectTriggered?→ TargetAction="Deflect",    Priority=6
  │      GuardHeld?       → TargetAction="Guard",      Priority=5
  │      ProstheticPressed?→ TargetAction="Prosthetic",Priority=4
  │      UseItemPressed?  → TargetAction="Item",       Priority=3
  │      AttackPressed?   → TargetAction="Attack",     Priority=2
  │
  │      For each intent:
  │        if (TargetPriority <= CurrentPriority) continue;
  │        if (!DataAsset->CanCancelTo(CurrentAnimID, CurrentTime, TargetAction, Crossfade))
  │          continue;
  │        → ResolveAnimID(TargetAction, intent params)
  │        → PlayMontage(AnimID, Crossfade)
  │        → 更新 CurrentAction/CurrentAnimID/CurrentPriority
  │
  └── 4. 无战斗意图 → Locomotion BlendSpace 保持运行
```

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| Source/Sekiro/Animation/SKAnimationController.h | **新建** | 组件声明 |
| Source/Sekiro/Animation/SKAnimationController.cpp | **新建** | 组件实现 |
| Source/Sekiro/Character/SKCharacter.h | 修改 | 添加 USKAnimationController 组件 |

## API 设计

### 优先级常量（对齐原版 TAE）

```cpp
namespace ESKActionPriority
{
    constexpr int32 Deathblow    = 10;   // 忍杀（不可打断）
    constexpr int32 Death        = 9;    // 死亡（不可打断）
    constexpr int32 Hit          = 8;    // 受击（不可打断）
    constexpr int32 Dodge        = 7;    // 闪避
    constexpr int32 Deflect      = 6;    // 弹刀成功
    constexpr int32 Guard        = 5;    // 防御
    constexpr int32 Prosthetic   = 4;    // 义手
    constexpr int32 ItemUse      = 3;    // 道具
    constexpr int32 Attack       = 2;    // 攻击
    constexpr int32 Quickstep    = 1;    // 垫步
    constexpr int32 Locomotion   = 0;    // 移动（始终可打断）
}
```

### 组件声明

```cpp
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKAnimationController : public UActorComponent
{
    GENERATED_BODY()

public:
    USKAnimationController();

    // —— 配置 ——
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
    TObjectPtr<USKAnimLogicData> AnimLogicData;           // TAE DataAsset 引用

    // —— 状态查询 ——
    UFUNCTION(BlueprintCallable)
    FName GetCurrentAction() const;

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentAnimID() const;

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentPriority() const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;

    // —— 帧级更新 ——
    void UpdateFrameState();                              // 更新当前帧/时间
    void ApplyFrameFlags();                               // 应用 DataAsset 行为标志到 ASKCharacter

    // —— 意图处理 ——
    void ProcessIntents();                                // 按优先级处理输入意图
    bool TryPlayAction(FName Action, int32 Priority);     // 尝试触发动作（含 CancelWindow 判定）

    // —— 动画播放 ——
    int32 ResolveAnimID(FName Action);                    // 意图→AnimID
    void PlayMontageByID(int32 AnimID, float Crossfade);  // 播放 Montage

    // —— 缓存 ——
    TMap<int32, TObjectPtr<UAnimMontage>> MontageCache;   // AnimID → Montage
    void EnsureMontageLoaded(int32 AnimID);               // 按需加载 Montage

private:
    // —— 运行时状态 ——
    FName CurrentAction;                                  // 当前动作名
    int32 CurrentAnimID = 0;                              // 当前动画ID
    int32 CurrentPriority = 0;                            // 当前优先级
    float CurrentAnimTime = 0.f;                          // 当前动画时间

    // —— 缓存引用 ——
    TWeakObjectPtr<ASKCharacter> OwnerCharacter;          // 角色引用
    TWeakObjectPtr<USKInputHandler> InputHandler;         // 输入组件引用
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;          // 骨骼网格
};
```

### Tick 主循环

```cpp
void USKAnimationController::TickComponent(float DeltaTime, ...)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // 1. 更新当前动画帧
    UpdateFrameState();

    // 2. 应用当前帧行为标志（bCanDeflect / bDisableMovement 等）
    ApplyFrameFlags();

    // 3. 按优先级处理输入意图
    ProcessIntents();
}
```

### ProcessIntents 逻辑

```cpp
void USKAnimationController::ProcessIntents()
{
    if (!InputHandler.IsValid()) return;

    // 按优先级降序遍历意图
    if (InputHandler->ConsumeDodgePressed())
        TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge);

    if (InputHandler->ConsumeAttackPressed())
    {
        FName AttackAction = InputHandler->GetAttackHoldTime() > 0.3f
            ? TEXT("Attack_Charged") : TEXT("Attack");
        TryPlayAction(AttackAction, ESKActionPriority::Attack);
    }

    if (InputHandler->ConsumeProstheticPressed())
        TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic);

    if (InputHandler->ConsumeUseItemPressed())
        TryPlayAction(TEXT("Item"), ESKActionPriority::ItemUse);

    if (InputHandler->ConsumeHealingGourdPressed())
        TryPlayAction(TEXT("Item"), ESKActionPriority::ItemUse);

    if (InputHandler->IsGuardHeld())
        TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard);
    // ... 更多意图
}
```

### TryPlayAction 核心判定

```cpp
bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
    if (!AnimLogicData) return false;

    // 优先级判定
    if (Priority <= CurrentPriority) return false;

    // CancelWindow 判定（帧级，30fps 查询 DataAsset）
    float Crossfade = 0.1f;
    if (!AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, Action, Crossfade))
        return false;

    // 解析 AnimID
    int32 TargetAnimID = ResolveAnimID(Action);
    if (TargetAnimID <= 0) return false;

    // 播放 Montage
    PlayMontageByID(TargetAnimID, Crossfade);

    // 更新状态
    CurrentAction = Action;
    CurrentAnimID = TargetAnimID;
    CurrentPriority = Priority;
    CurrentAnimTime = 0.f;

    return true;
}
```

### ResolveAnimID — 意图到 AnimID 映射（后续任务填充）

当前骨架版本返回默认 AnimID，各系统 Task 4-10 将逐步填充具体映射逻辑。

```cpp
int32 USKAnimationController::ResolveAnimID(FName Action)
{
    // 当前骨架：从 DataAsset 的 CategoryAnimMap 取第一个匹配类别动画
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
    if (List && List->IDs.Num() > 0)
        return List->IDs[0];
    return -1;
}
```

### 数据流

```
USKInputHandler (意图)
        │
        ▼  ProcessIntents()
        │
        ├── 意图 → TargetAction + Priority
        ├── CanCancelTo(AnimID, Time, TargetAction) ← USKAnimLogicData (DataAsset)
        ├── 可取消？→ ResolveAnimID(TargetAction)
        └── PlayMontage(AnimID, Crossfade)
                │
                ▼
        UAnimInstance::Montage_Play()
                │
                ▼
        ApplyFrameFlags() → GetFrameFlags(AnimID, Frame)
                │
                ▼
        ASKCharacter: bCanDeflect / bDisableMovement / ...
```

## 负责 Agent

- **Agent**：gameplay-programmer
- **输入**：上述接口设计 + 现有 USKInputHandler / USKAnimLogicData / ASKCharacter
- **依赖**：Task 1（USKInputHandler）✅ + Task 2（DataAsset）✅

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建 |
