// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/SKCombatComponent.h"
#include "AIController.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Character/SKCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Input/SKInputManager.h"
#include "Movement/SKMovementComponent.h"
#include "Weapon/SKWeaponManagerComponent.h"

/**
 * 创建可由 Lua 编排的战斗动作宿主并启用 PrePhysics Tick。
 * 构造阶段不访问 Owner、动画实例或世界；组件只能在游戏线程创建和使用。
 */
USKCombatComponent::USKCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

/**
 * 返回 UnLua 加载当前战斗动作脚本时使用的模块名。
 * 本函数不加载脚本且不修改状态，可在游戏线程的 UnLua 绑定流程中调用。
 *
 * @return 配置的 Lua 模块名；空字符串表示不绑定脚本模块。
 */
FString USKCombatComponent::GetModuleName_Implementation() const
{
    return LuaModuleName;
}

/**
 * 提供没有 Lua 覆盖时的空战斗 Tick 回退，避免 C++ 依赖脚本模块名或手写 Lua 调用。
 * 实际战斗状态机由 UnLua 以同名函数覆盖；仅由组件的 PrePhysics Tick 在游戏线程调用。
 *
 * @param DeltaTime 当前帧步长，单位秒；默认实现不消费该值。
 */
void USKCombatComponent::HandleCombatTick_Implementation(float DeltaTime)
{
    (void)DeltaTime;
}

/**
 * 将命中裁决产生的抽象防御结果交给脚本层处理。
 * 默认实现不解释 ResultName、不计算架势增量且不切换动作状态；UnLua 可用同名方法覆盖。
 * 只能在游戏线程调用。
 *
 * @param ResultName 由 Lua 解释的稳定结果名，例如 DeflectSuccess、Guarded 或 DeflectFailed。
 * @param AttackType 本次来袭的抽象攻击强度类型，不携带资产或数值策略。
 */
void USKCombatComponent::HandlePostureImpact_Implementation(
    FName ResultName,
    ESKIncomingAttackType AttackType)
{
    (void)ResultName;
    (void)AttackType;
}

/**
 * 提供脚本未绑定时的攻击类型回退，使普通攻击仍能进入接触裁决。
 * 默认只根据离散动作状态区分轻攻击与重攻击；Lua 可进一步把蓄力突刺映射为 Thrust。
 * 本函数只读当前战斗状态，不加载资源且不修改组件，必须在游戏线程调用。
 *
 * @return HeavyAttack 返回 Heavy，其他状态返回 Light。
 */
ESKIncomingAttackType USKCombatComponent::ResolveOutgoingAttackType_Implementation() const
{
    return CombatActionState == ESKCombatActionState::HeavyAttack
        ? ESKIncomingAttackType::Heavy
        : ESKIncomingAttackType::Light;
}

/**
 * 提供脚本未绑定时的武器接触回退，避免通用碰撞桥接依赖项目战斗规则。
 * 默认把接触视为普通命中；Lua 覆盖负责检查防御阶段、更新双方架势并播放对应反应。
 * 本函数不应用伤害、不记录去重且不保留攻击者引用，必须在游戏线程调用。
 *
 * @param AttackerCombat 发起攻击的战斗组件，可为空；默认实现不访问该对象。
 * @param AttackType 本次攻击的抽象类型；默认实现不解释该值。
 * @return 脚本未接管时返回 Hit，由武器继续应用基础伤害。
 */
ESKWeaponContactResult USKCombatComponent::ResolveIncomingWeaponContact_Implementation(
    USKCombatComponent* AttackerCombat,
    ESKIncomingAttackType AttackType)
{
    (void)AttackerCombat;
    (void)AttackType;
    return ESKWeaponContactResult::Hit;
}

/**
 * 提供 Lua 战斗规则未绑定时的 AI 攻击安全回退。
 * 默认不解释请求名、不启动动画且不修改战斗状态；只能在游戏线程由行为树或玩法代码调用。
 *
 * @param AttackRequest 脚本层定义的抽象攻击请求名；默认实现不保存该名称。
 * @return 默认返回 false，表示没有脚本规则接管本次请求。
 */
bool USKCombatComponent::RequestAIAttack_Implementation(FName AttackRequest)
{
    (void)AttackRequest;
    return false;
}

/**
 * 停止 Owner 当前由 AIController 发起的路径跟随请求，防止攻击反应或架势崩坏期间继续导航滑行。
 * 玩家控制器和没有 Pawn Owner 的组件会安全忽略；函数不清理行为树、不改变战斗状态。
 * 只能在游戏线程调用。
 */
void USKCombatComponent::StopOwnerAIMovement()
{
    const APawn* OwnerPawn = Cast<APawn>(GetOwner());
    AAIController* AIController = OwnerPawn
        ? Cast<AAIController>(OwnerPawn->GetController())
        : nullptr;
    if (AIController) AIController->StopMovement();
}

/**
 * 查询当前战斗动作状态，不推进状态机。
 * 仅允许游戏线程读取，返回值由 Lua 或测试接口最近一次写入。
 *
 * @return 当前动作状态枚举。
 */
ESKCombatActionState USKCombatComponent::GetCombatActionState() const
{
    return CombatActionState;
}

/**
 * 写入供 AnimBlueprint 和 Lua 共享的动作状态，不播放或停止动画。
 * 仅允许游戏线程调用。
 *
 * @param NewState 要立即发布的新动作状态。
 */
void USKCombatComponent::SetCombatActionState(ESKCombatActionState NewState)
{
    CombatActionState = NewState;
}

/**
 * 查询 AnimGraph 应持续输出的基础战斗姿态，不推进 Lua 战斗状态机。
 * 该姿态独立于当前离散动作，因此攻击 Montage 覆盖期间仍可保留防御底层 Pose。
 * 仅允许游戏线程读取。
 *
 * @return Lua 最近发布的基础战斗姿态枚举。
 */
ESKCombatPostureState USKCombatComponent::GetCombatPostureState() const
{
    return CombatPostureState;
}

/**
 * 写入供 AnimBlueprint 消费的基础战斗姿态，不修改动作状态，也不播放或停止动画。
 * 仅允许游戏线程由 Lua 战斗状态机调用。
 *
 * @param NewState 要立即发布的新基础战斗姿态。
 */
void USKCombatComponent::SetCombatPostureState(ESKCombatPostureState NewState)
{
    CombatPostureState = NewState;
}

/** 查询当前离散动作已提交的攻击侧；仅允许游戏线程读取。 */
ESKAttackSide USKCombatComponent::GetCommittedAttackSide() const
{
    return CommittedAttackSide;
}

/** 写入当前离散动作稳定攻击侧；仅允许游戏线程调用，不自动修改下一侧。 */
void USKCombatComponent::SetCommittedAttackSide(ESKAttackSide NewSide)
{
    CommittedAttackSide = NewSide;
}

/** 查询后续动作候选攻击侧；仅允许游戏线程读取。 */
ESKAttackSide USKCombatComponent::GetNextAttackSide() const
{
    return NextAttackSide;
}

/** 写入后续动作候选攻击侧；仅允许游戏线程调用，不污染当前动作侧。 */
void USKCombatComponent::SetNextAttackSide(ESKAttackSide NewSide)
{
    NextAttackSide = NewSide;
}

/** 查询输入层最后发布的防御键按住状态；仅允许游戏线程读取。 */
bool USKCombatComponent::IsGuardHeld() const
{
    return bGuardHeld;
}

/**
 * 查询组件所属 Character 当前是否由 CharacterMovement 判定为 Falling。
 * 本函数只做游戏线程只读查询，不修改移动模式，也不推断落地事件。
 *
 * @return 所属角色及移动组件有效且当前处于 Falling 移动模式时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerFalling() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const UCharacterMovementComponent* MovementComponent =
        Character ? Character->GetCharacterMovement() : nullptr;
    return MovementComponent && MovementComponent->IsFalling();
}

/**
 * 查询所属角色移动组件当前发布的速度档位是否为 Sprint。
 * 本函数只提供瞬时事实，不推断战斗状态或决定架势能否恢复；只能在游戏线程读取。
 *
 * @return Owner 使用 USKMovementComponent 且当前档位为 Sprint 时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerSprinting() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const USKMovementComponent* MovementComponent = Character
        ? Cast<USKMovementComponent>(Character->GetCharacterMovement())
        : nullptr;
    return MovementComponent && MovementComponent->IsMovementTierSprint();
}

/**
 * 查询所属角色是否仍处于 Dodge/Step 活动窗口。
 * 同时读取输入组件活动窗口、角色原生 Dodge 快照和战斗动作状态，避免组件间一帧更新差导致误判；
 * 本函数不结束 Dodge、不推进状态机，只能在游戏线程读取。
 *
 * @return 任一现有 Dodge/Step 事实为活动状态时返回 true，否则返回 false。
 */
bool USKCombatComponent::IsOwnerDodgingOrStepActive() const
{
    const ASKCharacter* Character = Cast<ASKCharacter>(GetOwner());
    const USKInputManager* InputManager = Character
        ? Character->FindComponentByClass<USKInputManager>()
        : nullptr;
    return CombatActionState == ESKCombatActionState::Dodging
        || (InputManager && InputManager->IsDodgeActive())
        || (Character && Character->IsDodging());
}

/** 查询当前动作序列号；仅允许游戏线程读取。 */
int32 USKCombatComponent::GetActionSerial() const
{
    return ActionSerial;
}

/**
 * 开始一个新的逻辑动作，递增动作序列号并发布状态。
 * 本函数不选择或播放资产；仅允许游戏线程由 Lua 状态机调用。
 *
 * @param NewState 新动作对 AnimBlueprint 可见的状态。
 * @return 新动作的正整数序列号，后续输入和结束回调必须用它校验。
 */
int32 USKCombatComponent::BeginCombatAction(ESKCombatActionState NewState)
{
    ++ActionSerial;
    if (ActionSerial <= 0) ActionSerial = 1;
    CombatActionState = NewState;
    return ActionSerial;
}

/**
 * 使指定当前动作立即失效并清除其输入候选，防止迟到回调推进状态。
 * ExpectedActionSerial 非零且与当前值不匹配时不做任何修改；本函数不停止 Montage。
 * 仅允许游戏线程调用。
 *
 * @param ExpectedActionSerial 调用方认为仍活动的序列号；零表示无条件失效当前动作。
 */
void USKCombatComponent::InvalidateCombatAction(int32 ExpectedActionSerial)
{
    if (ExpectedActionSerial != 0 && ExpectedActionSerial != ActionSerial) return;

    ++ActionSerial;
    if (ActionSerial <= 0) ActionSerial = 1;
    PendingInputEvents.Reset();
    DeflectGuardInputSerial = 0;
    DeflectContextSerial = 0;
}

/**
 * 校验回调或候选是否仍属于当前动作。
 * 仅允许游戏线程读取，不修改序列号。
 *
 * @param ExpectedActionSerial 待验证的正整数动作序列号。
 * @return 与当前序列号完全相等且非零时返回 true。
 */
bool USKCombatComponent::IsActionSerialValid(int32 ExpectedActionSerial) const
{
    return ExpectedActionSerial > 0 && ExpectedActionSerial == ActionSerial;
}

/** 查询当前架势值；仅允许游戏线程读取，不推进恢复或打崩流程。 */
float USKCombatComponent::GetCurrentPosture() const
{
    return CurrentPosture;
}

/** 查询当前架势上限；仅允许游戏线程读取。 */
float USKCombatComponent::GetMaxPosture() const
{
    return MaxPosture;
}

/**
 * 查询当前架势比例，供 Lua 公式、动画和 UI 使用。
 * 当上限为零时稳定返回零，函数不修改任何状态；仅允许游戏线程读取。
 *
 * @return 限制在 [0, 1] 的架势比例，零上限时返回 0。
 */
float USKCombatComponent::GetPostureNormalized() const
{
    return MaxPosture > UE_SMALL_NUMBER
        ? FMath::Clamp(CurrentPosture / MaxPosture, 0.f, 1.f)
        : 0.f;
}

/** 查询当前是否处于架势打崩流程；仅允许游戏线程读取。 */
bool USKCombatComponent::IsPostureBroken() const
{
    return bPostureBroken;
}

/**
 * 写入 Lua 配置提供的架势上限，并把当前值同步限制到新范围。
 * 上限负值按零处理；只有上限或当前值实际变化时才广播一次完整架势快照。
 * 本函数不执行打崩判定且只能在游戏线程调用。
 *
 * @param NewMaxPosture 新架势上限，非有限业务值应由脚本层预先过滤，负值会限制为零。
 */
void USKCombatComponent::SetMaxPosture(float NewMaxPosture)
{
    const float SafeMaxPosture = FMath::Max(0.f, NewMaxPosture);
    const float SafeCurrentPosture = FMath::Clamp(CurrentPosture, 0.f, SafeMaxPosture);
    if (FMath::IsNearlyEqual(MaxPosture, SafeMaxPosture)
        && FMath::IsNearlyEqual(CurrentPosture, SafeCurrentPosture))
    {
        return;
    }

    MaxPosture = SafeMaxPosture;
    CurrentPosture = SafeCurrentPosture;
    BroadcastPostureChanged();
}

/**
 * 写入 Lua 已完成数值策略计算后的当前架势，并限制到 [0, MaxPosture]。
 * 只有限制后的数值实际变化时才广播；本函数不判断结果类型，也不自动进入打崩状态。
 * 只能在游戏线程调用。
 *
 * @param NewCurrentPosture Lua 计算后的目标架势值，超出范围时会被安全限制。
 */
void USKCombatComponent::SetCurrentPosture(float NewCurrentPosture)
{
    const float SafeCurrentPosture = FMath::Clamp(NewCurrentPosture, 0.f, MaxPosture);
    if (FMath::IsNearlyEqual(CurrentPosture, SafeCurrentPosture)) return;

    CurrentPosture = SafeCurrentPosture;
    BroadcastPostureChanged();
}

/**
 * 将当前架势归零并复用统一变化广播。
 * 本函数不改变 bPostureBroken 或动作状态，便于 Lua 明确编排打崩进入与退出顺序；
 * 只能在游戏线程调用。
 */
void USKCombatComponent::ResetPosture()
{
    SetCurrentPosture(0.f);
}

/**
 * 写入 Lua 编排的架势打崩状态，并在状态真正变化时广播。
 * 本函数不播放动画、不重置架势、不修改动作状态或输入锁；只能在游戏线程调用。
 *
 * @param bNewPostureBroken true 表示进入打崩流程，false 表示流程恢复完成。
 */
void USKCombatComponent::SetPostureBroken(bool bNewPostureBroken)
{
    if (bPostureBroken == bNewPostureBroken) return;

    bPostureBroken = bNewPostureBroken;
    OnPostureBrokenChanged.Broadcast(bPostureBroken);
}

/**
 * 将一个稳定原因名的输入锁请求转发给同 Owner 的输入组件。
 * 本函数不解释原因、不持有锁副本；Reason 为 None 或输入组件缺失时安全忽略。
 * 只能在游戏线程调用。
 *
 * @param Reason 调用系统拥有的稳定锁原因，必须非 None。
 * @param bLocked true 添加该原因，false 仅移除该原因。
 */
void USKCombatComponent::SetOwnerExternalInputLock(FName Reason, bool bLocked)
{
    AActor* Owner = GetOwner();
    USKInputManager* InputManager = Owner ? Owner->FindComponentByClass<USKInputManager>() : nullptr;
    if (InputManager) InputManager->SetExternalInputLock(Reason, bLocked);
}

/**
 * 清除 Owner 输入组件中所有玩法意图，并同步丢弃本组件尚未裁决的战斗输入与 Guard Held 快照。
 * Look、Pause 和 Menu 由输入组件保留；本函数不增删外部锁原因，只能在游戏线程调用。
 */
void USKCombatComponent::ClearOwnerGameplayInputForScript()
{
    AActor* Owner = GetOwner();
    USKInputManager* InputManager = Owner ? Owner->FindComponentByClass<USKInputManager>() : nullptr;
    if (InputManager) InputManager->ClearAllGameplayInputForScript();

    PendingInputEvents.Reset();
    bGuardHeld = false;
}

/**
 * 可选清空 Owner 当前武器的单次攻击去重集合，再开启其 QueryOnly 攻击碰撞。
 * 本函数只桥接组件查找与物理碰撞接口，不判断动作状态或动画窗口；这些业务边界由 Lua 编排。
 * 只能在游戏线程调用，不缓存 WeaponManager 或武器引用。
 *
 * @param bResetHitActors true 表示开启前清空本轮已命中目标，false 保留现有去重集合。
 * @return Owner、WeaponManager 和当前武器均有效，且碰撞开启请求已提交时返回 true；否则返回 false。
 */
bool USKCombatComponent::ActivateOwnerWeaponHitbox(bool bResetHitActors)
{
    AActor* Owner = GetOwner();
    USKWeaponManagerComponent* WeaponManager =
        Owner ? Owner->FindComponentByClass<USKWeaponManagerComponent>() : nullptr;
    if (!WeaponManager) return false;
    if (bResetHitActors && !WeaponManager->ClearWeaponHitActors()) return false;
    return WeaponManager->ActivateWeaponHitbox();
}

/**
 * 关闭 Owner 当前武器的攻击碰撞，供 Lua 在窗口结束、动作切换或异常中断时统一收敛。
 * 本函数不清空已命中集合、不改变武器挂载或展示状态；只能在游戏线程调用且不缓存引用。
 *
 * @return Owner、WeaponManager 和当前武器均有效，且关闭请求已提交时返回 true；否则返回 false。
 */
bool USKCombatComponent::DeactivateOwnerWeaponHitbox()
{
    AActor* Owner = GetOwner();
    USKWeaponManagerComponent* WeaponManager =
        Owner ? Owner->FindComponentByClass<USKWeaponManagerComponent>() : nullptr;
    return WeaponManager && WeaponManager->DeactivateWeaponHitbox();
}

/**
 * 接收输入组件生成的不可变战斗输入事件，更新 Guard Held 快照并按发生顺序入队。
 * 队列最多保留 32 条，溢出时丢弃最旧事件；同时同步广播通知，但不执行动作裁决。
 * 仅允许游戏线程调用，InputEvent 在函数内复制，不保留调用方引用。
 *
 * @param InputEvent 已包含成对 InputSerial、绝对时间和释放时长的事件。
 */
void USKCombatComponent::SubmitCombatInputEvent(const FSKCombatInputEvent& InputEvent)
{
    if (InputEvent.Action == ESKCombatInputAction::Guard)
    {
        bGuardHeld = InputEvent.Phase == ESKCombatInputPhase::Started;
    }

    if (PendingInputEvents.Num() >= 32) PendingInputEvents.RemoveAt(0);
    PendingInputEvents.Add(InputEvent);
    OnCombatInputEvent.Broadcast(InputEvent);
}

/**
 * 弹出等待时间最久的输入事件，供 Lua 在战斗组件 Tick 中顺序裁决。
 * 仅允许游戏线程调用；成功时从队列永久移除该事件。
 *
 * @param OutInputEvent 输出事件副本；队列为空时重置为默认值。
 * @return 成功弹出事件时返回 true，队列为空时返回 false。
 */
bool USKCombatComponent::ConsumeCombatInputEvent(FSKCombatInputEvent& OutInputEvent)
{
    OutInputEvent = FSKCombatInputEvent();
    if (PendingInputEvents.IsEmpty()) return false;

    OutInputEvent = PendingInputEvents[0];
    PendingInputEvents.RemoveAt(0);
    return true;
}

/** 清空所有尚未消费的战斗输入事件；仅允许游戏线程调用，不改变 Held 状态。 */
void USKCombatComponent::ClearCombatInputEvents()
{
    PendingInputEvents.Reset();
}

/**
 * 在 CombatFullBodySlot 播放给定 Sequence，并由本组件独占所创建的动态 Montage。
 * 开始前会停止本组件旧 Montage，但不会停止其他组件拥有的动画；不选择资产或编排连段。
 * 仅允许游戏线程调用，Animation 在播放期间由组件强引用持有。
 *
 * @param Animation 要播放的非空动画序列。
 * @param BlendInTime 淡入秒数，负值限制为零。
 * @param BlendOutTime 淡出秒数，负值限制为零。
 * @param PlayRate 播放倍率，绝对值过小时按 1 处理。
 * @param LoopCount 循环次数，小于一时限制为一。
 * @return 动画实例有效且动态 Montage 成功启动时返回 true，否则返回 false。
 */
bool USKCombatComponent::PlayCombatAnimation(
    UAnimSequence* Animation,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount)
{
    UAnimInstance* AnimInstance = ResolveAnimInstance();
    if (!Animation || !AnimInstance || CombatSlotName.IsNone()) return false;

    UAnimMontage* PreviousMontage = ActiveMontage;
    ClearOwnedAnimationState();
    if (PreviousMontage) AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), PreviousMontage);

    ActivePlayRate = FMath::Abs(PlayRate) <= UE_SMALL_NUMBER ? 1.f : PlayRate;
    UAnimMontage* DynamicMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
        Animation,
        CombatSlotName,
        FMath::Max(0.f, BlendInTime),
        FMath::Max(0.f, BlendOutTime),
        ActivePlayRate,
        FMath::Max(1, LoopCount));
    if (!DynamicMontage) return false;

    ActiveSequence = Animation;
    ActiveMontage = DynamicMontage;
    AnimationStartTimeSeconds = GetWorldTimeSeconds();

    FOnMontageEnded EndDelegate;
    EndDelegate.BindUObject(this, &USKCombatComponent::HandleCombatMontageEnded, ActionSerial);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, DynamicMontage);
    return true;
}

/**
 * 从 Lua 配置提供的软对象路径同步加载 UAnimSequence，并交给通用动态 Montage 播放接口。
 * 本函数不缓存路径、不硬编码资产名；加载和播放均只能在游戏线程执行。
 *
 * @param AnimationPath UAnimSequence 的完整对象路径，空字符串或类型不匹配时失败。
 * @param BlendInTime 淡入秒数，负值由通用播放接口限制为零。
 * @param BlendOutTime 淡出秒数，负值由通用播放接口限制为零。
 * @param PlayRate 播放倍率，绝对值过小时由通用播放接口按 1 处理。
 * @param LoopCount 循环次数，小于一时由通用播放接口限制为一。
 * @return 资产成功加载且动态 Montage 成功启动时返回 true，否则返回 false。
 */
bool USKCombatComponent::PlayCombatAnimationByPath(
    const FString& AnimationPath,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount)
{
    if (AnimationPath.IsEmpty()) return false;

    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *AnimationPath);
    return PlayCombatAnimation(Animation, BlendInTime, BlendOutTime, PlayRate, LoopCount);
}

/**
 * 淡出并停止本组件拥有的动态 Montage，并立即清除活动资产引用。
 * 本函数不停止其他系统的 Montage，旧结束回调因 Montage 身份校验而成为幂等空操作。
 * 仅允许游戏线程调用。
 *
 * @param BlendOutTime 淡出秒数，负值限制为零。
 */
void USKCombatComponent::StopCombatAnimation(float BlendOutTime)
{
    UAnimMontage* Montage = ActiveMontage;
    UAnimInstance* AnimInstance = ResolveAnimInstance();
    ClearOwnedAnimationState();
    if (Montage && AnimInstance) AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), Montage);
}

/** 查询本组件动态 Montage 是否仍在播放或混合；仅允许游戏线程读取。 */
bool USKCombatComponent::IsCombatAnimationPlaying() const
{
    const UAnimInstance* AnimInstance = ResolveAnimInstance();
    return ActiveMontage && AnimInstance && AnimInstance->Montage_IsPlaying(ActiveMontage);
}

/** 查询活动动态 Montage 的 Track 位置；无有效播放时返回零，仅允许游戏线程读取。 */
float USKCombatComponent::GetCombatAnimationPosition() const
{
    const UAnimInstance* AnimInstance = ResolveAnimInstance();
    return ActiveMontage && AnimInstance ? AnimInstance->Montage_GetPosition(ActiveMontage) : 0.f;
}

/** 查询活动 Montage Track 位置映射后的源 Sequence 本地时间；映射失败时返回零。 */
float USKCombatComponent::GetActiveSequencePosition() const
{
    float SequencePosition = 0.f;
    ResolveSequencePosition(GetCombatAnimationPosition(), SequencePosition);
    return SequencePosition;
}

/**
 * 在当前源 Sequence 本地时间直接采样指定曲线，避免读取最终混合 Pose 的残留曲线。
 * 曲线或活动 Sequence 不存在时返回零；仅允许游戏线程读取。
 *
 * @param CurveName Skeleton 中登记的曲线名，None 返回零。
 * @return 指定本地时间的曲线浮点值，查询失败时为零。
 */
float USKCombatComponent::SampleActiveSequenceCurve(FName CurveName) const
{
    return EvaluateSequenceCurve(CurveName, GetActiveSequencePosition());
}

/**
 * 按输入事件的世界绝对时间估算对应 Montage Track 位置，再映射到源 Sequence 采样曲线。
 * 本实现以当前 Montage 位置和世界时间差回溯，受游戏暂停和 Montage 非匀速改变影响时由 Serial 校验兜底；
 * 不推进动画且仅允许游戏线程读取。
 *
 * @param CurveName Skeleton 中登记的曲线名。
 * @param EventTimeSeconds 输入事件发生的游戏世界绝对秒数，可早于当前帧。
 * @return 事件时刻对应的曲线值；无活动动画或无法映射时返回零。
 */
float USKCombatComponent::SampleActiveSequenceCurveAtTime(FName CurveName, double EventTimeSeconds) const
{
    if (!ActiveMontage || !ActiveSequence) return 0.f;

    const double TimeOffset = GetWorldTimeSeconds() - EventTimeSeconds;
    const float EventMontagePosition = GetCombatAnimationPosition() - static_cast<float>(TimeOffset) * ActivePlayRate;
    float SequencePosition = 0.f;
    if (!ResolveSequencePosition(EventMontagePosition, SequencePosition)) return 0.f;
    return EvaluateSequenceCurve(CurveName, SequencePosition);
}

/** 查询战斗全身 Montage 是否处于活动播放或混合状态；仅允许游戏线程读取。 */
bool USKCombatComponent::IsCombatFullBodyActionActive() const
{
    return IsCombatAnimationPlaying();
}

/**
 * 创建或覆盖一个模拟来袭有效区间，用于无 AI、碰撞和伤害条件下测试弹反动画。
 * 新上下文递增 ContextSerial，ActiveDuration 负值限制为零；仅允许游戏线程调用。
 *
 * @param AttackType 由 Lua 映射到 Deflect Type 的抽象攻击类型。
 * @param ActiveDuration 从当前世界时间起算的有效秒数。
 * @return 新上下文的正整数序列号。
 */
int32 USKCombatComponent::BeginIncomingAttackAnimationTest(ESKIncomingAttackType AttackType, float ActiveDuration)
{
    ++ContextSerial;
    if (ContextSerial <= 0) ContextSerial = 1;

    const double Now = GetWorldTimeSeconds();
    IncomingAttackContext.AttackType = AttackType;
    IncomingAttackContext.ContextSerial = ContextSerial;
    IncomingAttackContext.ActiveStartTimeSeconds = Now;
    IncomingAttackContext.ActiveEndTimeSeconds = Now + FMath::Max(0.f, ActiveDuration);
    IncomingAttackContext.bConsumed = false;
    return ContextSerial;
}

/**
 * 清除当前模拟来袭；非零 ExpectedContextSerial 不匹配时忽略，防止旧测试回调清除新上下文。
 * 仅允许游戏线程调用，不中断当前弹反动画。
 *
 * @param ExpectedContextSerial 期望清除的上下文序列号；零表示无条件清除。
 */
void USKCombatComponent::ClearIncomingAttackAnimationTest(int32 ExpectedContextSerial)
{
    if (ExpectedContextSerial != 0 && ExpectedContextSerial != IncomingAttackContext.ContextSerial) return;
    IncomingAttackContext = FSKIncomingAttackAnimationContext();
}

/**
 * 判断给定绝对事件时间是否落在当前未消费来袭的闭区间内。
 * 仅允许游戏线程读取，不自动消费上下文。
 *
 * @param EventTimeSeconds Guard Started 事件的世界绝对秒数。
 * @return 上下文有效、未消费且事件时间位于起止边界内时返回 true。
 */
bool USKCombatComponent::IsIncomingAttackAnimationActiveAt(double EventTimeSeconds) const
{
    return IncomingAttackContext.ContextSerial > 0
        && !IncomingAttackContext.bConsumed
        && EventTimeSeconds >= IncomingAttackContext.ActiveStartTimeSeconds
        && EventTimeSeconds <= IncomingAttackContext.ActiveEndTimeSeconds;
}

/**
 * 复制当前模拟来袭上下文供 Lua 调试或裁决。
 * 仅允许游戏线程读取，不消费或延长上下文。
 *
 * @param OutContext 接收上下文副本；不存在时重置为默认值。
 * @return 存在非零 ContextSerial 时返回 true，否则返回 false。
 */
bool USKCombatComponent::GetIncomingAttackAnimationContext(FSKIncomingAttackAnimationContext& OutContext) const
{
    OutContext = IncomingAttackContext;
    return OutContext.ContextSerial > 0;
}

/**
 * 使用新的 Guard Started 输入一次性消费模拟来袭，并锁存弹反关联序列号。
 * 本函数只验证时序和序列号，不选择 Deflect 资产、不推进 Stage，也不播放动画。
 * 仅允许游戏线程调用。
 *
 * @param ExpectedContextSerial Lua 已读取并准备消费的上下文序列号。
 * @param GuardInputSerial 触发裁决的新 Guard Started 输入序列号，必须为正数。
 * @param EventTimeSeconds Guard Started 发生的世界绝对秒数。
 * @param OutAttackType 成功时输出抽象攻击类型；失败时重置为 Light。
 * @return 所有校验通过且上下文首次被消费时返回 true，否则返回 false。
 */
bool USKCombatComponent::TryConsumeIncomingAttackAnimation(
    int32 ExpectedContextSerial,
    int32 GuardInputSerial,
    double EventTimeSeconds,
    ESKIncomingAttackType& OutAttackType)
{
    OutAttackType = ESKIncomingAttackType::Light;
    if (GuardInputSerial <= 0
        || ExpectedContextSerial <= 0
        || ExpectedContextSerial != IncomingAttackContext.ContextSerial
        || !IsIncomingAttackAnimationActiveAt(EventTimeSeconds))
    {
        return false;
    }

    IncomingAttackContext.bConsumed = true;
    DeflectGuardInputSerial = GuardInputSerial;
    DeflectContextSerial = ExpectedContextSerial;
    OutAttackType = IncomingAttackContext.AttackType;
    return true;
}

/**
 * 建立组件 Tick 先后关系并为纯原生 UnLua 组件补发 ReceiveBeginPlay。
 * InputManager 作为前置 Tick，角色 Mesh 以本组件为前置，确保输入发布、战斗状态和动画采集有确定顺序。
 * 仅由 UE 在游戏线程生命周期调用。
 */
void USKCombatComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();

    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (Character)
    {
        if (UActorComponent* InputComponent = Character->FindComponentByClass<USKInputManager>())
        {
            AddTickPrerequisiteComponent(InputComponent);
        }
        if (USkeletalMeshComponent* Mesh = Character->GetMesh())
        {
            Mesh->AddTickPrerequisiteComponent(this);
        }
    }

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

/**
 * 在 Owner 离场前使 Serial 失效、停止自有 Montage 并清空输入和模拟来袭状态。
 * 清理可重复调用，旧 Montage 回调因身份和 Serial 校验不会广播新动作结束事件。
 * 仅由 UE 在游戏线程生命周期调用。
 *
 * @param EndPlayReason UE 提供的离场原因，仅透传给父类。
 */
void USKCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    InvalidateCombatAction(0);
    StopCombatAnimation(0.f);
    PendingInputEvents.Reset();
    IncomingAttackContext = FSKIncomingAttackAnimationContext();
    bGuardHeld = false;
    CombatActionState = ESKCombatActionState::Neutral;
    CombatPostureState = ESKCombatPostureState::Normal;
    Super::EndPlay(EndPlayReason);
}

/**
 * 显式调用 Lua 模块导出的 Tick，使状态机先消费输入，再清理已过期且未消费的模拟来袭。
 * 本函数自身不裁决输入或选择动画；仅由 UE 在游戏线程 PrePhysics 阶段调用。
 *
 * @param DeltaTime 本帧组件步长，当前实现不参与区间计算。
 * @param TickType UE Tick 类型，仅透传父类。
 * @param ThisTickFunction 当前 Tick 函数，仅透传父类，可为空。
 */
void USKCombatComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    HandleCombatTick(DeltaTime);

    if (IncomingAttackContext.ContextSerial > 0
        && !IncomingAttackContext.bConsumed
        && GetWorldTimeSeconds() > IncomingAttackContext.ActiveEndTimeSeconds)
    {
        IncomingAttackContext = FSKIncomingAttackAnimationContext();
    }
}

/**
 * 解析 Owner 角色 Mesh 当前动画实例，不创建或切换 AnimBlueprint。
 * 仅允许游戏线程读取 UObject 状态。
 *
 * @return 有效角色 Mesh 的 AnimInstance；任一环节缺失时返回 nullptr。
 */
UAnimInstance* USKCombatComponent::ResolveAnimInstance() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
    return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

/**
 * 使用动态 Montage 的活动 FAnimSegment 将 Track 时间转换为源 Sequence 本地时间。
 * 只接受当前组件持有的 Sequence，超出所有 Segment 范围时失败；仅允许游戏线程读取。
 *
 * @param MontagePosition 动态 Montage Track 秒数。
 * @param OutSequencePosition 成功时输出源 Sequence 本地秒数，失败时为零。
 * @return 找到匹配 Segment 且其动画引用等于 ActiveSequence 时返回 true。
 */
bool USKCombatComponent::ResolveSequencePosition(float MontagePosition, float& OutSequencePosition) const
{
    OutSequencePosition = 0.f;
    if (!ActiveMontage || !ActiveSequence) return false;

    for (const FSlotAnimationTrack& SlotTrack : ActiveMontage->SlotAnimTracks)
    {
        for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
        {
            if (Segment.GetAnimReference() == ActiveSequence && Segment.IsInRange(MontagePosition))
            {
                OutSequencePosition = Segment.ConvertTrackPosToAnimPos(MontagePosition);
                return true;
            }
        }
    }
    return false;
}

/**
 * 通过 ActiveSequence Skeleton 的 SmartName UID 在指定本地时间直接求曲线值。
 * 本函数不读取最终混合 Pose；仅允许游戏线程读取已加载动画资产。
 *
 * @param CurveName 要查询的曲线稳定名称，None 视为失败。
 * @param SequencePosition 源 Sequence 本地秒数，函数会限制到资产播放范围。
 * @return 曲线存在时的浮点值，否则为零。
 */
float USKCombatComponent::EvaluateSequenceCurve(FName CurveName, float SequencePosition) const
{
    if (!ActiveSequence || CurveName.IsNone()) return 0.f;

    const USkeleton* Skeleton = ActiveSequence->GetSkeleton();
    if (!Skeleton) return 0.f;

    FSmartName SmartCurveName;
    if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, SmartCurveName)) return 0.f;

    const float ClampedPosition = FMath::Clamp(SequencePosition, 0.f, ActiveSequence->GetPlayLength());
    return ActiveSequence->EvaluateCurveData(SmartCurveName.UID, ClampedPosition);
}

/**
 * 接收动态 Montage 结束通知，仅当 Montage 身份和启动时 ActionSerial 仍匹配时广播。
 * 广播前清除自有动画引用，允许监听方安全开始下一 Montage；仅由 UE 在游戏线程调用。
 *
 * @param Montage 已结束的动态 Montage，可为空。
 * @param bInterrupted 是否由停止或抢占导致中断。
 * @param EndedActionSerial 绑定委托时锁存的动作序列号。
 */
void USKCombatComponent::HandleCombatMontageEnded(
    UAnimMontage* Montage,
    bool bInterrupted,
    int32 EndedActionSerial)
{
    if (!Montage || Montage != ActiveMontage || EndedActionSerial != ActionSerial) return;

    ClearOwnedAnimationState();
    OnCombatAnimationEnded.Broadcast(EndedActionSerial, bInterrupted);
}

/** 清除本组件持有的活动动画引用和播放时间快照；仅允许游戏线程调用，不停止 Montage。 */
void USKCombatComponent::ClearOwnedAnimationState()
{
    ActiveSequence = nullptr;
    ActiveMontage = nullptr;
    AnimationStartTimeSeconds = 0.0;
    ActivePlayRate = 1.f;
}

/** 查询当前游戏世界绝对秒数；组件尚无 World 时返回零，仅允许游戏线程读取。 */
double USKCombatComponent::GetWorldTimeSeconds() const
{
    const UWorld* World = GetWorld();
    return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

/**
 * 广播当前架势值、上限和安全归一化比例的同帧一致快照。
 * 仅由已确认状态发生变化的写接口在游戏线程调用，不进行额外去重或状态修改。
 */
void USKCombatComponent::BroadcastPostureChanged()
{
    OnPostureChanged.Broadcast(CurrentPosture, MaxPosture, GetPostureNormalized());
}
