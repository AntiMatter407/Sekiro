// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/SKCombatComponent.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Input/SKInputManager.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    /**
     * 显式 require 战斗 Lua 模块，并验证其返回值是可调用函数表。
     * 本函数只操作传入 LuaEnv 的主状态，不缓存栈引用；必须在游戏线程调用。
     *
     * @param LuaEnv 当前战斗组件所属的 UnLua 环境，可为空。
     * @param LuaModuleName 要加载的模块稳定名称，不是文件系统路径。
     * @param bOutSucceeded 成功得到模块表时输出 true，其他情况输出 false。
     * @return require 调用的返回值容器；失败时可能为空，所有权由调用方栈对象管理。
     */
    static UnLua::FLuaRetValues RequireSKCombatLuaModule(
        UnLua::FLuaEnv* LuaEnv,
        const FString& LuaModuleName,
        bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKCombatComponent Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKCombatComponent Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }
}

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
    TryCallLuaCombatTick(DeltaTime);

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
 * 显式 require 配置的战斗 Lua 模块并调用其 Tick(self, DeltaTime) 导出函数。
 * 该路径不依赖原生 UActorComponent 的 ReceiveTick 派发，因此纯原生组件也能稳定运行 Lua 状态机；
 * 本函数不解释 Lua 返回值，只以是否找到并完成调用作为结果，且仅允许游戏线程调用。
 *
 * @param DeltaTime 当前组件 Tick 步长，单位秒，原样传给 Lua。
 * @return Lua 环境、模块表和 Tick 函数均有效且调用完成时返回 true，否则返回 false。
 */
bool USKCombatComponent::TryCallLuaCombatTick(float DeltaTime)
{
    if (LuaModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKCombatLuaModule(LuaEnv, LuaModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
    FunctionReturnValues.Pop();
    return true;
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
