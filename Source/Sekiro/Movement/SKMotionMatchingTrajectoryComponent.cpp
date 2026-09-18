#include "Movement/SKMotionMatchingTrajectoryComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "Movement/SKMovementComponent.h"

namespace
{
    constexpr float SKMinPredictionSeconds = 0.f;
    constexpr float SKMaxPredictionSeconds = 10.f;
    constexpr int32 SKMinPredictionSampleRate = 5;
    constexpr int32 SKMaxPredictionSampleRate = 120;
    constexpr int32 SKMinPredictionSamples = 0;
    constexpr int32 SKMaxPredictionSamples = 3600;
    constexpr float SKMaxPredictionAcceleration = 10000.f;
    constexpr float SKMaxFacingTurnRate = 2160.f;
    constexpr float SKTrajectoryTranslationYawOffsetDegrees = -90.f;

    /**
     * 检查向量的三个分量是否均为有限数值，避免 NaN 或无穷值进入轨迹积分。
     * 本函数不归一化或限制向量长度，可在任意线程对值类型调用。
     *
     * @param Value 待检查的向量值。
     * @return 三个分量全部有限时返回 true，否则返回 false。
     */
    bool IsSKFiniteVector(const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    /**
     * 检查轨迹样本的时间、Transform 和速度是否适合交给 Pose Search 消费。
     * 本函数只执行值校验，不修改样本；可在任意线程对已复制样本调用。
     *
     * @param Sample 待检查的轨迹样本。
     * @return 所有必要字段均为有限值时返回 true，否则返回 false。
     */
    bool IsSKFiniteTrajectorySample(const FTransformTrajectorySample& Sample)
    {
        return FMath::IsFinite(Sample.TimeInSeconds)
            && IsSKFiniteVector(Sample.Position)
            && FMath::IsFinite(Sample.Facing.X)
            && FMath::IsFinite(Sample.Facing.Y)
            && FMath::IsFinite(Sample.Facing.Z)
            && FMath::IsFinite(Sample.Facing.W);
    }

    /**
     * 将可能来自蓝图或序列化数据的浮点配置限制为有限范围。
     * 本函数不记录日志，可在任意线程调用。
     *
     * @param Value 待校验的配置值。
     * @param DefaultValue 非有限输入使用的回退值。
     * @param Minimum 允许的闭区间下限。
     * @param Maximum 允许的闭区间上限。
     * @return 有限且限制到指定闭区间的配置值。
     */
    float ClampSKFiniteValue(float Value, float DefaultValue, float Minimum, float Maximum)
    {
        return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, Minimum, Maximum);
    }
}

/**
 * 初始化 Motion Matching 轨迹组件的采样域和 MoveIntent 预测默认值。
 * 构造阶段不访问 Owner 或世界；基类 Tick 继续负责采集真实 Actor Transform 历史。
 *
 * @param ObjectInitializer Unreal 对象初始化器，仅在对象构造阶段有效。
 */
USKMotionMatchingTrajectoryComponent::USKMotionMatchingTrajectoryComponent(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    PredictionSettings.Seconds = 1.5f;
    HistorySettings.Seconds = 0.5f;
    SampleRate = 30;
    MaxSamples = 120;
    bPredictionIncludesHistory = true;
}

/** 清空构造或重建过程中遗留的历史，并按当前上限预留存储空间。 */
void USKMotionMatchingTrajectoryComponent::OnComponentCreated()
{
    FlushHistory();
    SampleHistory.Reserve(FMath::Max(0, MaxSamples));
    Super::OnComponentCreated();
}

/**
 * 在游戏开始时从 Owner 初始化当前世界变换，并让轨迹采样明确晚于 CharacterMovement。
 * 只能在游戏线程调用；正式 ActualState 由移动完成委托采集，Tick 前置关系仅服务旧采样链。
 * 不据此保证 AnimGraph 的整帧调用顺序。本函数不生成未来预测，也不改变角色移动。
 */
void USKMotionMatchingTrajectoryComponent::BeginPlay()
{
    Super::BeginPlay();

    const APawn* OwnerPawn = TryGetOwnerPawn();
    USKMovementComponent* MovementComponent = OwnerPawn
        ? Cast<USKMovementComponent>(OwnerPawn->GetMovementComponent())
        : nullptr;
    if (MovementComponent) AddTickPrerequisiteComponent(MovementComponent);

    // AnimInstance 可能先于 Movement Lua 的首个 Tick 更新。先发布一份静止安全意图，
    // 保证正式查询只等待 CMC 的首个完成样本，而不会因初始化顺序永久停留在无效门禁。
    FSKMotionMatchingIntentInput InitialIntent;
    InitialIntent.DesiredFacingYaw = OwnerPawn
        ? OwnerPawn->GetActorRotation().Yaw
        : 0.f;
    SubmitMotionMatchingIntent(InitialIntent);

    InvalidateMotionMatchingActualState();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        Character->OnCharacterMovementUpdated.AddUniqueDynamic(
            this, &USKMotionMatchingTrajectoryComponent::CaptureCompletedMovement);
    }

    FVector LinearVelocityWS = FVector::ZeroVector;
    PresentWorldTransform = CalcWorldSpacePresentTransform(LinearVelocityWS);
    PresentLinearVelocityLS = PresentWorldTransform.InverseTransformVectorNoScale(LinearVelocityWS);
    FlushHistory();
}

/**
 * 在游戏线程解除移动完成委托并丢弃旧反馈，避免生命周期结束后继续采样。
 * @param EndPlayReason 引擎提供的结束原因，原样传给父类。
 */
void USKMotionMatchingTrajectoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        Character->OnCharacterMovementUpdated.RemoveDynamic(
            this, &USKMotionMatchingTrajectoryComponent::CaptureCompletedMovement);
    }
    InvalidateMotionMatchingActualState();
    Super::EndPlay(EndPlayReason);
}

/** 在 PrePhysics 组件 Tick 中采集当前真实状态并维护历史快照。 */
void USKMotionMatchingTrajectoryComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    TickTrajectory(DeltaTime);
}

/**
 * 校验完整输入并一次性保存规范化意图，不写 ActorYaw、速度或旧轨迹状态，也不触发搜索。
 * 仅游戏线程调用；Free 静止朝向暂取当前 Actor，后续 ActualState 接入时统一替换来源。
 * 比较容差集中于本入口：强度 0.0001、方向分量 0.0001、角度 0.01 度。
 * 容差内保留整个旧快照，避免内容改变但版本不变；持续小变化相对最后发布值累积。
 *
 * @param Input Lua 或 AI 提供的完整值；方向为世界空间，强度无单位，Facing 为度。不保留引用。
 * @return 接受输入（包括无变化）返回 true；非游戏线程、无 Owner、非法枚举、无效 ActorYaw
 * 或版本耗尽返回 false 且保留旧快照。非法移动数值归零，失效 Locked 面向退回 Free。
 */
bool USKMotionMatchingTrajectoryComponent::SubmitMotionMatchingIntent(const FSKMotionMatchingIntentInput& Input)
{
    if (!IsInGameThread()) return false;
    const AActor* OwnerActor = GetOwner();
    if (!IsValid(OwnerActor)
        || static_cast<uint8>(Input.RequestedGait) > static_cast<uint8>(ESKMotionMatchingGait::Sprint)
        || static_cast<uint8>(Input.RequestedStance) > static_cast<uint8>(ESKMotionMatchingStance::Crouching)
        || static_cast<uint8>(Input.RotationMode) > static_cast<uint8>(ESKMotionMatchingRotationMode::Locked)) return false;

    const float ActorYaw = OwnerActor->GetActorRotation().Yaw;
    if (!FMath::IsFinite(ActorYaw)) return false;

    constexpr float AmountTolerance = 0.0001f;
    constexpr double DirectionTolerance = 0.0001;
    constexpr float YawTolerance = 0.01f;
    FSKMotionMatchingIntentSnapshot Next;
    Next.Intent = Input;
    Next.Intent.MoveIntentAmount = FMath::IsFinite(Input.MoveIntentAmount)
        ? FMath::Clamp(Input.MoveIntentAmount, 0.f, 1.f) : 0.f;
    FVector Direction = IsSKFiniteVector(Input.MoveIntentWorldDirection)
        ? FVector(Input.MoveIntentWorldDirection.X, Input.MoveIntentWorldDirection.Y, 0.0)
        : FVector::ZeroVector;
    // 先缩放再归一化，避免极大但有限的方向分量在长度平方中溢出。
    const double MaxComponent = FMath::Max(FMath::Abs(Direction.X), FMath::Abs(Direction.Y));
    Direction = MaxComponent > UE_SMALL_NUMBER
        ? (Direction / MaxComponent).GetSafeNormal() : FVector::ZeroVector;
    const float DeadZone = FMath::IsFinite(IntentInputDeadZone)
        ? FMath::Clamp(IntentInputDeadZone, 0.f, 1.f) : 0.01f;
    Next.bHasMoveIntent = Next.Intent.MoveIntentAmount > DeadZone && !Direction.IsNearlyZero();
    Next.Intent.MoveIntentWorldDirection = Next.bHasMoveIntent ? Direction : FVector::ZeroVector;
    if (!Next.bHasMoveIntent) Next.Intent.MoveIntentAmount = 0.f;
    Next.DesiredMoveYaw = Next.bHasMoveIntent
        ? FRotator::NormalizeAxis(Direction.Rotation().Yaw) : SubmittedIntent.DesiredMoveYaw;

    const bool bValidLockedFacing = Input.RotationMode == ESKMotionMatchingRotationMode::Locked
        && Input.bHasFacingTarget && FMath::IsFinite(Input.DesiredFacingYaw);
    Next.Intent.RotationMode = bValidLockedFacing
        ? ESKMotionMatchingRotationMode::Locked : ESKMotionMatchingRotationMode::Free;
    Next.Intent.bHasFacingTarget = bValidLockedFacing;
    Next.Intent.DesiredFacingYaw = FRotator::NormalizeAxis(bValidLockedFacing
        ? Input.DesiredFacingYaw : (Next.bHasMoveIntent ? Next.DesiredMoveYaw : ActorYaw));

    const FSKMotionMatchingIntentInput& Previous = SubmittedIntent.Intent;
    const bool bChanged = SubmittedIntent.IntentRevision == 0
        || Next.bHasMoveIntent != SubmittedIntent.bHasMoveIntent
        || Next.Intent.RequestedGait != Previous.RequestedGait
        || Next.Intent.RequestedStance != Previous.RequestedStance
        || Next.Intent.RotationMode != Previous.RotationMode
        || Next.Intent.bHasFacingTarget != Previous.bHasFacingTarget
        || !Next.Intent.MoveIntentWorldDirection.Equals(Previous.MoveIntentWorldDirection, DirectionTolerance)
        || !FMath::IsNearlyEqual(Next.Intent.MoveIntentAmount, Previous.MoveIntentAmount, AmountTolerance)
        || FMath::Abs(FMath::FindDeltaAngleDegrees(SubmittedIntent.DesiredMoveYaw, Next.DesiredMoveYaw)) > YawTolerance
        || FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.DesiredFacingYaw, Next.Intent.DesiredFacingYaw)) > YawTolerance;
    if (!bChanged) return true;
    if (SubmittedIntent.IntentRevision == MAX_int64) return false;

    Next.IntentRevision = SubmittedIntent.IntentRevision + 1;
    SubmittedIntent = Next;
    return true;
}

/**
 * 返回最近接受的完整意图副本，不读取 MovementComponent，也不修改版本。
 * 仅游戏线程允许访问组件；工作线程必须使用此前复制的值，不能调用此接口。
 *
 * @return 已提交快照；未提交或非游戏线程调用返回默认快照，IntentRevision 为 0。
 */
FSKMotionMatchingIntentSnapshot USKMotionMatchingTrajectoryComponent::GetMotionMatchingIntent() const
{
    if (!IsInGameThread()) return FSKMotionMatchingIntentSnapshot();
    return SubmittedIntent;
}

/**
 * 在游戏线程读取最近完成的实际状态，不重新采样或计算速度。
 * @return 值副本；非游戏线程返回默认无效快照，调用方须检查 bHasCompletedMovement。
 */
FSKMotionMatchingActualState USKMotionMatchingTrajectoryComponent::GetMotionMatchingActualState() const
{
    if (!IsInGameThread()) return FSKMotionMatchingActualState();
    return CompletedActualState;
}

/**
 * 在游戏线程丢弃无法信任的实际反馈与历史连续性，不修改角色或意图。
 * 只用于当前角色状态本身也无法建立新基线的错误路径；正常传送或网络校正应调用 Rebase。
 * 保留样本计数以免后续新样本重用旧编号。
 * 非游戏线程调用不执行任何操作；下一次有效移动回调才能重新发布实际状态。
 */
void USKMotionMatchingTrajectoryComponent::InvalidateMotionMatchingActualState()
{
    if (!IsInGameThread()) return;
    const int64 PreviousSampleId = CompletedActualState.ActualSampleId;
    CompletedActualState = FSKMotionMatchingActualState();
    CompletedActualState.ActualSampleId = PreviousSampleId;
    CompletedActualHistory.Reset();
    FlushHistory();
}

/**
 * 丢弃传送或网络校正之前的连续历史，并以当前角色/CMC 事实发布零位移有效基线。
 * 只能在校正已经应用到 Actor 后于游戏线程调用；不移动角色、不修改 Velocity 或 Intent。
 * 新基线递增 ActualSampleId，实际位移与碰撞裁剪固定为零，避免跨越校正跳变生成伪 RootMotion。
 *
 * @return 当前角色、Movement、World 与有限状态均有效且样本号可递增时返回 true；
 * 否则清空无法信任的状态并返回 false。
 */
bool USKMotionMatchingTrajectoryComponent::RebaseMotionMatchingActualState()
{
    if (!IsInGameThread()) return false;
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const USKMovementComponent* Movement = Character
        ? Cast<USKMovementComponent>(Character->GetCharacterMovement()) : nullptr;
    const UWorld* World = GetWorld();
    if (!IsValid(Character) || !IsValid(Movement) || !World
        || CompletedActualState.ActualSampleId == MAX_int64)
    {
        InvalidateMotionMatchingActualState();
        return false;
    }

    FSKMotionMatchingActualState Baseline;
    Baseline.ActualSampleId = CompletedActualState.ActualSampleId + 1;
    Baseline.SampleTimeSeconds = World->GetTimeSeconds();
    Baseline.ActorTransformWS = Character->GetActorTransform();
    Baseline.ActualVelocityWS = Movement->Velocity;
    Baseline.MovementMode = Movement->MovementMode;
    Baseline.CustomMovementMode = Movement->CustomMovementMode;
    if (!FMath::IsFinite(Baseline.SampleTimeSeconds)
        || Baseline.ActorTransformWS.ContainsNaN()
        || !IsSKFiniteVector(Baseline.ActualVelocityWS))
    {
        InvalidateMotionMatchingActualState();
        return false;
    }

    Baseline.bHasCompletedMovement = true;
    CompletedActualState = Baseline;
    CompletedActualHistory.Reset();
    CompletedActualHistory.Add(Baseline);
    FlushHistory();
    LastActualTranslationDeltaWS = FVector::ZeroVector;
    LastCollisionClippedTranslationWS = FVector::ZeroVector;
    return true;
}

/**
 * 在游戏线程复制正式意图、已完成移动状态和配置，整体构建 Grounded 或 Airborne 查询；
 * 只有空中重力参数读取同角色的 CMC/PhysicsVolume，其余输入不读取旧实时 Movement 字段。
 * 只推进查询/配置版本，不推进动画、移动或搜索请求；返回值独占其样本数组。
 * 历史以 ActualState 时间为零点。Grounded 把 Z 固定为零并按查询意图预测 XY；Airborne 的 XY
 * 保持已完成实际速度，表示上一阶段 RootMotion 经 CMC 碰撞后的水平惯性，Z 再按 GravityZ 积分并受
 * TerminalVelocity 限制。空中输入仍可影响 Facing 和候选语义，但不能凭空生成预测水平加速度。
 * 本函数不把预测写回 CMC。
 *
 * @return 完整值快照；数据缺失、不支持的移动模式或配置非法时 bValid=false、轨迹为空并提供原因。
 * 非游戏线程不访问组件数据，返回 WrongThread；调用方必须检查 bValid，不能沿用旧结果。
 */
FSKMotionMatchingQuerySnapshot USKMotionMatchingTrajectoryComponent::BuildMotionMatchingQuerySnapshot()
{
    FSKMotionMatchingQuerySnapshot Result;
    if (!IsInGameThread())
    {
        Result.InvalidReason = TEXT("WrongThread");
        return Result;
    }
    if (QueryRevision == MAX_int64)
    {
        Result.InvalidReason = TEXT("QueryRevisionExhausted");
        return Result;
    }
    Result.QueryRevision = ++QueryRevision;
    Result.Intent = SubmittedIntent;
    Result.ActualState = CompletedActualState;
    const FSKAnimationRootMotionSpeedProfile Profile = AnimationRootMotionSpeedProfile;
    const FSKTrajectoryPredictionSettings Settings = TrajectoryPredictionSettings;
    if (Result.Intent.IntentRevision == 0)
    {
        Result.InvalidReason = TEXT("MissingIntent");
        return Result;
    }
    if (!Result.ActualState.bHasCompletedMovement)
    {
        Result.InvalidReason = TEXT("MissingActualState");
        return Result;
    }
    const bool bIsGroundedQuery = Result.ActualState.MovementMode == MOVE_Walking
        || Result.ActualState.MovementMode == MOVE_NavWalking;
    const bool bIsAirborneQuery = Result.ActualState.MovementMode == MOVE_Falling;
    if (!bIsGroundedQuery && !bIsAirborneQuery)
    {
        Result.InvalidReason = TEXT("UnsupportedMovementMode");
        return Result;
    }
    if (!FMath::IsFinite(Profile.Walk) || Profile.Walk <= 0.f || Profile.Walk > 10000.f
        || !FMath::IsFinite(Profile.Run) || Profile.Run <= 0.f || Profile.Run > 10000.f
        || !FMath::IsFinite(Profile.Sprint) || Profile.Sprint <= 0.f || Profile.Sprint > 10000.f
        || !FMath::IsFinite(Profile.Crouch) || Profile.Crouch <= 0.f || Profile.Crouch > 10000.f)
    {
        Result.InvalidReason = TEXT("InvalidGaitProfile");
        return Result;
    }
    if (!FMath::IsFinite(PredictionSettings.Seconds) || PredictionSettings.Seconds <= 0.f
        || PredictionSettings.Seconds > SKMaxPredictionSeconds
        || !FMath::IsFinite(HistorySettings.Seconds) || HistorySettings.Seconds < 0.f
        || !FMath::IsFinite(Settings.Acceleration) || Settings.Acceleration < 0.f || Settings.Acceleration > SKMaxPredictionAcceleration
        || !FMath::IsFinite(Settings.Deceleration) || Settings.Deceleration < 0.f || Settings.Deceleration > SKMaxPredictionAcceleration
        || !FMath::IsFinite(Settings.FacingTurnRateDegrees) || Settings.FacingTurnRateDegrees < 0.f
        || Settings.FacingTurnRateDegrees > SKMaxFacingTurnRate
        || SampleRate < 5 || SampleRate > 120 || MaxSamples < 2 || MaxSamples > SKMaxPredictionSamples)
    {
        Result.InvalidReason = TEXT("InvalidPredictionSettings");
        return Result;
    }
    float AirborneGravityZ = 0.f;
    float AirborneTerminalVelocity = 0.f;
    if (bIsAirborneQuery)
    {
        const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
        const UCharacterMovementComponent* CharacterMovement = OwnerCharacter
            ? OwnerCharacter->GetCharacterMovement()
            : nullptr;
        const APhysicsVolume* PhysicsVolume = CharacterMovement
            ? CharacterMovement->GetPhysicsVolume()
            : nullptr;
        if (!CharacterMovement || !PhysicsVolume)
        {
            Result.InvalidReason = TEXT("MissingAirborneMovementContext");
            return Result;
        }
        AirborneGravityZ = CharacterMovement->GetGravityZ();
        AirborneTerminalVelocity = PhysicsVolume->TerminalVelocity;
        if (!FMath::IsFinite(AirborneGravityZ) || AirborneGravityZ > 0.f
            || !FMath::IsFinite(AirborneTerminalVelocity) || AirborneTerminalVelocity <= 0.f)
        {
            Result.InvalidReason = TEXT("InvalidAirbornePredictionSettings");
            return Result;
        }
    }
    const bool bProfileChanged = GaitProfileRevision == 0
        || Profile.Walk != LastQuerySpeedProfile.Walk || Profile.Run != LastQuerySpeedProfile.Run
        || Profile.Sprint != LastQuerySpeedProfile.Sprint || Profile.Crouch != LastQuerySpeedProfile.Crouch;
    if (bProfileChanged)
    {
        if (GaitProfileRevision == MAX_int64)
        {
            Result.InvalidReason = TEXT("GaitProfileRevisionExhausted");
            return Result;
        }
        ++GaitProfileRevision;
        LastQuerySpeedProfile = Profile;
    }
    Result.GaitProfileRevision = GaitProfileRevision;
    float NominalSpeed = Profile.Run;
    switch (Result.Intent.Intent.RequestedGait)
    {
    case ESKMotionMatchingGait::Walk: NominalSpeed = Profile.Walk; break;
    case ESKMotionMatchingGait::Sprint: NominalSpeed = Profile.Sprint; break;
    case ESKMotionMatchingGait::Run: break;
    default:
        Result.InvalidReason = TEXT("InvalidRequestedGait");
        return Result;
    }
    if (Result.Intent.Intent.RequestedStance == ESKMotionMatchingStance::Crouching) NominalSpeed = Profile.Crouch;
    Result.ExpectedSpeed = Result.Intent.bHasMoveIntent ? NominalSpeed * Result.Intent.Intent.MoveIntentAmount : 0.f;
    Result.TargetIntentVelocityWS = Result.Intent.Intent.MoveIntentWorldDirection * Result.ExpectedSpeed;
    Result.QueryOriginWS = Result.ActualState.ActorTransformWS;
    Result.QueryOriginWS.SetScale3D(FVector::OneVector);

    const int32 FutureCount = FMath::CeilToInt(PredictionSettings.Seconds * SampleRate);
    if (FutureCount + 1 > MaxSamples)
    {
        Result.InvalidReason = TEXT("InsufficientSampleBudget");
        return Result;
    }
    FTransformTrajectory Trajectory;
    const int32 HistoryBudget = MaxSamples - FutureCount - 1;
    if (bPredictionIncludesHistory && HistoryBudget > 0)
    {
        for (const FSKMotionMatchingActualState& Sample : CompletedActualHistory)
        {
            const double RelativeTime = Sample.SampleTimeSeconds - Result.ActualState.SampleTimeSeconds;
            if (RelativeTime >= 0.0 || RelativeTime < -HistorySettings.Seconds) continue;
            FTransformTrajectorySample HistorySample;
            HistorySample.TimeInSeconds = static_cast<float>(RelativeTime);
            HistorySample.Position = Result.QueryOriginWS.InverseTransformPosition(Sample.ActorTransformWS.GetLocation());
            HistorySample.Facing = Result.QueryOriginWS.GetRotation().Inverse() * Sample.ActorTransformWS.GetRotation();
            Trajectory.Samples.Add(HistorySample);
        }
        if (Trajectory.Samples.Num() > HistoryBudget)
        {
            Trajectory.Samples.RemoveAt(0, Trajectory.Samples.Num() - HistoryBudget, EAllowShrinking::No);
        }
    }
    FTransformTrajectorySample Present;
    Present.TimeInSeconds = 0.f;
    Present.Position = FVector::ZeroVector;
    Present.Facing = FQuat::Identity;
    Trajectory.Samples.Add(Present);
    FVector VelocityWS = Result.ActualState.ActualVelocityWS;
    if (bIsGroundedQuery) VelocityWS.Z = 0.f;
    FVector PositionWS = Result.QueryOriginWS.GetLocation();
    float FacingYaw = Result.QueryOriginWS.Rotator().Yaw;
    const float TargetYaw = Result.Intent.Intent.RotationMode == ESKMotionMatchingRotationMode::Free
        && !Result.Intent.bHasMoveIntent ? FacingYaw : Result.Intent.Intent.DesiredFacingYaw;
    float PreviousTime = 0.f;
    for (int32 Index = 1; Index <= FutureCount; ++Index)
    {
        const float Time = FMath::Min(static_cast<float>(Index) / SampleRate, PredictionSettings.Seconds);
        const float Step = Time - PreviousTime;
        const FVector PreviousVelocity = VelocityWS;
        if (bIsGroundedQuery)
        {
            const float Rate = Result.Intent.bHasMoveIntent ? Settings.Acceleration : Settings.Deceleration;
            FVector PlanarVelocity = VelocityWS;
            PlanarVelocity.Z = 0.f;
            PlanarVelocity += (Result.TargetIntentVelocityWS - PlanarVelocity).GetClampedToMaxSize(Rate * Step);
            VelocityWS.X = PlanarVelocity.X;
            VelocityWS.Y = PlanarVelocity.Y;
        }
        VelocityWS.Z = bIsAirborneQuery
            ? FMath::Max(VelocityWS.Z + AirborneGravityZ * Step, -AirborneTerminalVelocity)
            : 0.f;
        PositionWS += (PreviousVelocity + VelocityWS) * (0.5f * Step);
        FacingYaw = FMath::FixedTurn(FacingYaw, TargetYaw, Settings.FacingTurnRateDegrees * Step);
        FTransformTrajectorySample Sample;
        Sample.TimeInSeconds = Time;
        Sample.Position = Result.QueryOriginWS.InverseTransformPosition(PositionWS);
        Sample.Facing = Result.QueryOriginWS.GetRotation().Inverse() * FRotator(0.f, FacingYaw, 0.f).Quaternion();
        Trajectory.Samples.Add(Sample);
        PreviousTime = Time;
    }
    const float PredictedFuturePlanarSpeed = VelocityWS.Size2D();
    if (!IsSKFiniteVector(VelocityWS) || !FMath::IsFinite(PredictedFuturePlanarSpeed))
    {
        Result.InvalidReason = TEXT("InvalidPredictedFutureVelocity");
        return Result;
    }
    Result.PredictedFutureVelocityWS = VelocityWS;
    Result.PredictedFuturePlanarSpeed = PredictedFuturePlanarSpeed;
    // 复用现有唯一动画轴适配点，不在输入或世界积分阶段重复旋转。
    TransformTrajectoryToQuerySpace(Trajectory);
    float PreviousSampleTime = -TNumericLimits<float>::Max();
    for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        if (!IsSKFiniteTrajectorySample(Sample) || Sample.TimeInSeconds <= PreviousSampleTime)
        {
            Result.InvalidReason = TEXT("InvalidTrajectorySample");
            return Result;
        }
        PreviousSampleTime = Sample.TimeInSeconds;
    }
    Result.DesiredTrajectory = MoveTemp(Trajectory);
    Result.QueryDomain = bIsAirborneQuery
        ? ESKMotionMatchingQueryDomain::Airborne
        : ESKMotionMatchingQueryDomain::Grounded;
    Result.bValid = true;
    return Result;
}

/**
 * 返回供 Motion Matching 节点查询的角色局部轨迹快照。
 * 只能在游戏线程读取组件状态；返回值按值复制，不持有组件内部历史缓冲区引用。
 *
 * @return 按过去、现在、未来排列且时间严格递增的局部轨迹。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::GetMotionMatchingTrajectory() const
{
    return GetTrajectory();
}

/**
 * 更新 MoveIntent 未来积分参数，并把所有字段限制在可预测的有限范围。
 * 只能在游戏线程调用；本函数不清空真实历史、不移动 Actor，也不立即执行预测。
 *
 * @param Settings 新的加速度、减速度、Facing 转速与输入阈值配置，非有限值使用结构默认值。
 */
void USKMotionMatchingTrajectoryComponent::SetTrajectoryPredictionSettings(
    const FSKTrajectoryPredictionSettings& Settings)
{
    const FSKTrajectoryPredictionSettings Defaults;
    TrajectoryPredictionSettings.Acceleration = ClampSKFiniteValue(
        Settings.Acceleration,
        Defaults.Acceleration,
        0.f,
        SKMaxPredictionAcceleration);
    TrajectoryPredictionSettings.Deceleration = ClampSKFiniteValue(
        Settings.Deceleration,
        Defaults.Deceleration,
        0.f,
        SKMaxPredictionAcceleration);
    TrajectoryPredictionSettings.FacingTurnRateDegrees = ClampSKFiniteValue(
        Settings.FacingTurnRateDegrees,
        Defaults.FacingTurnRateDegrees,
        0.f,
        SKMaxFacingTurnRate);
    TrajectoryPredictionSettings.InputThreshold = ClampSKFiniteValue(
        Settings.InputThreshold,
        Defaults.InputThreshold,
        0.f,
        1.f);
}

/**
 * 更新各 Gait 动画族离线统计得到的标称 RootMotion 速度。
 * 只能在游戏线程调用；所有字段均限制为有限的非负速度，不修改 MovementComponent 配置。
 * 该配置在搜索前独立参与 Desired Trajectory 预测，不引用本帧尚未选中的具体动画。
 *
 * @param SpeedProfile Walk、Run、Sprint 与 Crouch 候选动画的标称水平速度，单位 cm/s。
 */
void USKMotionMatchingTrajectoryComponent::SetAnimationRootMotionSpeedProfile(
    const FSKAnimationRootMotionSpeedProfile& SpeedProfile)
{
    const FSKAnimationRootMotionSpeedProfile Defaults;
    AnimationRootMotionSpeedProfile.Walk = ClampSKFiniteValue(
        SpeedProfile.Walk,
        Defaults.Walk,
        0.f,
        10000.f);
    AnimationRootMotionSpeedProfile.Run = ClampSKFiniteValue(
        SpeedProfile.Run,
        Defaults.Run,
        0.f,
        10000.f);
    AnimationRootMotionSpeedProfile.Sprint = ClampSKFiniteValue(
        SpeedProfile.Sprint,
        Defaults.Sprint,
        0.f,
        10000.f);
    AnimationRootMotionSpeedProfile.Crouch = ClampSKFiniteValue(
        SpeedProfile.Crouch,
        Defaults.Crouch,
        0.f,
        10000.f);
}

/** 返回最近一次有限语义意图变化的递增代数。 */
int32 USKMotionMatchingTrajectoryComponent::GetIntentGeneration() const
{
    return IntentGeneration;
}

/** 返回最近一次 Tick 采集并限制到单位圆内的屏幕空间 MoveIntent。 */
FVector2D USKMotionMatchingTrajectoryComponent::GetMoveIntentSnapshot() const
{
    return MoveIntentSnapshot;
}

/** 返回最近一次 Tick 为 Movement Trajectory 采集的有限世界 Yaw。 */
float USKMotionMatchingTrajectoryComponent::GetDesiredMoveYawSnapshot() const
{
    return DesiredMoveYawSnapshot;
}

/** 返回最近一次 Tick 为 Facing Trajectory 采集的有限世界 Yaw。 */
float USKMotionMatchingTrajectoryComponent::GetDesiredFacingYawSnapshot() const
{
    return DesiredFacingYawSnapshot;
}

/** 返回最近一次 RootMotion 经过移动与碰撞求值后的实际世界水平位移。 */
FVector USKMotionMatchingTrajectoryComponent::GetLastActualTranslationDeltaWS() const
{
    return LastActualTranslationDeltaWS;
}

/** 返回最近一次 RootMotion 请求被碰撞、地面或 MovementMode 裁剪的世界水平位移。 */
FVector USKMotionMatchingTrajectoryComponent::GetLastCollisionClippedTranslationWS() const
{
    return LastCollisionClippedTranslationWS;
}

/**
 * 使用组件默认时间域生成轨迹，默认是否包含历史由基类开关决定。
 * 只能在游戏线程读取 Owner、MovementComponent 和历史缓冲；不修改角色运动状态。
 *
 * @return 角色局部、时间严格递增的历史、现在与未来轨迹。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::GetTrajectory() const
{
    return GetTrajectoryWithSettings(PredictionSettings, bPredictionIncludesHistory);
}

/**
 * 使用临时时间域生成 MoveIntent 驱动的未来轨迹，并按请求拼接基类真实历史。
 * 只能在游戏线程调用；Settings.Seconds 的单位为秒，非法值会回退并限制到 0..10 秒。
 * 本函数只读取输入、移动档位和锁定快照，不写 Actor Transform、不施加速度。
 *
 * @param Settings 本次查询的未来时间域；仅 Seconds 参与覆盖。
 * @param bIncludeHistory true 时包含基类从真实 Actor Transform 采集的过去样本。
 * @return 角色局部、包含唯一零时刻且采样时间严格递增的轨迹。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::GetTrajectoryWithSettings(
    const FMotionTrajectorySettings& Settings,
    bool bIncludeHistory) const
{
    FTransformTrajectory Prediction;
    const APawn* OwnerPawn = TryGetOwnerPawn();
    const USKMovementComponent* MovementComponent = OwnerPawn
        ? Cast<USKMovementComponent>(OwnerPawn->GetMovementComponent())
        : nullptr;

    const float DefaultSeconds = ClampSKFiniteValue(
        PredictionSettings.Seconds,
        1.5f,
        SKMinPredictionSeconds,
        SKMaxPredictionSeconds);
    const float PredictionSeconds = ClampSKFiniteValue(
        Settings.Seconds,
        DefaultSeconds,
        SKMinPredictionSeconds,
        SKMaxPredictionSeconds);
    const int32 PredictionSampleRate = FMath::Clamp(
        SampleRate,
        SKMinPredictionSampleRate,
        SKMaxPredictionSampleRate);
    const int32 PredictionMaxSamples = FMath::Clamp(
        MaxSamples,
        SKMinPredictionSamples,
        SKMaxPredictionSamples);

    if (MovementComponent && PredictionSeconds > 0.f)
    {
        PredictTrajectory(
            MovementComponent,
            PredictionSeconds,
            PredictionSampleRate,
            PredictionMaxSamples,
            Prediction);
    }

    const FTransformTrajectory CombinedTrajectory = CombineHistoryPresentPrediction(
        bIncludeHistory,
        Prediction);
    return SanitizeTrajectoryRange(CombinedTrajectory);
}

/**
 * 在角色移动完成委托中发布实际状态；只允许游戏线程，不驱动移动或修改查询。
 * 速度直接读取 CMC；位移使用该步骤起止位置，不拿动画请求替代实际结果。
 * 时间倒退或步骤起点与上次终点不连续时从当前角色事实重建零位移基线；非法数值才清空状态。
 *
 * @param DeltaSeconds 本移动步骤时长，单位秒，必须有限且大于零。
 * @param OldLocation 本步骤起始世界位置，单位 cm。
 * @param OldVelocity 本步骤起始世界速度，当前无需使用，不作为结果速度。
 */
void USKMotionMatchingTrajectoryComponent::CaptureCompletedMovement(
    float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
    if (!IsInGameThread()) return;
    static_cast<void>(OldVelocity);
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const USKMovementComponent* Movement = Character
        ? Cast<USKMovementComponent>(Character->GetCharacterMovement()) : nullptr;
    const UWorld* World = GetWorld();
    if (!IsValid(Character) || !IsValid(Movement) || !World
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
    {
        InvalidateMotionMatchingActualState();
        return;
    }

    FSKMotionMatchingActualState Next;
    Next.SampleTimeSeconds = World->GetTimeSeconds();
    Next.ActorTransformWS = Character->GetActorTransform();
    Next.ActualVelocityWS = Movement->Velocity;
    Next.ActualTranslationDeltaWS = Next.ActorTransformWS.GetLocation() - OldLocation;
    Next.CollisionClippedTranslationWS = Movement->GetLastRootMotionCollisionClippedTranslationWS();
    Next.MovementMode = Movement->MovementMode;
    Next.CustomMovementMode = Movement->CustomMovementMode;
    const bool bDiscontinuous = CompletedActualState.bHasCompletedMovement
        && (Next.SampleTimeSeconds < CompletedActualState.SampleTimeSeconds
            || !OldLocation.Equals(CompletedActualState.ActorTransformWS.GetLocation(), 0.1));
    if (!FMath::IsFinite(Next.SampleTimeSeconds)
        || Next.ActorTransformWS.ContainsNaN() || !IsSKFiniteVector(OldLocation)
        || !IsSKFiniteVector(Next.ActualVelocityWS) || !IsSKFiniteVector(Next.ActualTranslationDeltaWS)
        || !IsSKFiniteVector(Next.CollisionClippedTranslationWS)
        || CompletedActualState.ActualSampleId == MAX_int64)
    {
        InvalidateMotionMatchingActualState();
        return;
    }
    if (bDiscontinuous)
    {
        RebaseMotionMatchingActualState();
        return;
    }

    Next.ActualSampleId = CompletedActualState.ActualSampleId + 1;
    Next.bHasCompletedMovement = true;
    CompletedActualState = Next;
    // 同一世界时间内可能完成多次移动：保留最后结果，避免历史查询时间重复。
    if (!CompletedActualHistory.IsEmpty() && CompletedActualHistory.Last().SampleTimeSeconds == Next.SampleTimeSeconds)
    {
        CompletedActualHistory.Last() = Next;
    }
    else
    {
        CompletedActualHistory.Add(Next);
    }
    const int32 HistoryLimit = FMath::Clamp(MaxSamples, 1, SKMaxPredictionSamples);
    if (CompletedActualHistory.Num() > HistoryLimit)
    {
        CompletedActualHistory.RemoveAt(0, CompletedActualHistory.Num() - HistoryLimit, EAllowShrinking::No);
    }
}

/**
 * 返回组件 Owner 对应的 Pawn；只读且不转移所有权。
 * 只能在游戏线程调用；Owner 不是 Pawn 或不存在时返回 nullptr。
 */
const APawn* USKMotionMatchingTrajectoryComponent::TryGetOwnerPawn() const
{
    return Cast<APawn>(GetOwner());
}

/**
 * 采集当前世界状态并把线速度转换到角色局部空间，随后更新历史。
 * 只能由组件 Tick 在游戏线程调用；本函数不推进角色移动，也不生成未来预测。
 *
 * @param DeltaTime 当前帧间隔，单位秒；仅用于组件生命周期契约，采样读取真实状态。
 */
void USKMotionMatchingTrajectoryComponent::TickTrajectory(float DeltaTime)
{
    static_cast<void>(DeltaTime);
    const APawn* OwnerPawn = TryGetOwnerPawn();
    const USKMovementComponent* MovementComponent = OwnerPawn
        ? Cast<USKMovementComponent>(OwnerPawn->GetMovementComponent())
        : nullptr;
    RefreshIntentSnapshot(MovementComponent);
    LastActualTranslationDeltaWS = MovementComponent
        ? MovementComponent->GetLastRootMotionActualTranslationWS()
        : FVector::ZeroVector;
    LastCollisionClippedTranslationWS = MovementComponent
        ? MovementComponent->GetLastRootMotionCollisionClippedTranslationWS()
        : FVector::ZeroVector;

    FVector LinearVelocityWS = FVector::ZeroVector;
    PresentWorldTransform = CalcWorldSpacePresentTransform(LinearVelocityWS);
    PresentLinearVelocityLS = PresentWorldTransform.InverseTransformVectorNoScale(LinearVelocityWS);
    PresentLinearVelocityLS.Z = 0.f;
    if (!IsSKFiniteVector(PresentLinearVelocityLS)) PresentLinearVelocityLS = FVector::ZeroVector;

    TickHistoryEvictionPolicy();
}

/**
 * 从 MovementComponent 采集有限的移动与 Facing 语义，并在语义变化时递增 IntentGeneration。
 * 只能在游戏线程由组件 Tick 调用；本函数只发布查询快照，不施加移动或旋转。
 *
 * @param MovementComponent 当前 Pawn 的项目移动组件；为空时发布静止安全值。
 */
void USKMotionMatchingTrajectoryComponent::RefreshIntentSnapshot(
    const USKMovementComponent* MovementComponent)
{
    const APawn* OwnerPawn = TryGetOwnerPawn();
    const float OwnerYawWS = OwnerPawn && FMath::IsFinite(OwnerPawn->GetActorRotation().Yaw)
        ? OwnerPawn->GetActorRotation().Yaw
        : 0.f;

    FVector2D NewMoveIntent = FVector2D::ZeroVector;
    float NewInputAmount = 0.f;
    float NewMoveYawWS = OwnerYawWS;
    float NewFacingYawWS = OwnerYawWS;
    ESKMovementTier NewRequestedGait = ESKMovementTier::Idle;
    bool bNewHasMoveIntent = false;
    bool bNewLockedOn = false;

    if (MovementComponent)
    {
        NewMoveIntent.X = FMath::IsFinite(MovementComponent->GetMoveInputX())
            ? MovementComponent->GetMoveInputX()
            : 0.f;
        NewMoveIntent.Y = FMath::IsFinite(MovementComponent->GetMoveInputY())
            ? MovementComponent->GetMoveInputY()
            : 0.f;
        NewMoveIntent = NewMoveIntent.GetClampedToMaxSize(1.f);
        NewInputAmount = ClampSKFiniteValue(
            MovementComponent->GetMoveInputAmount(),
            0.f,
            0.f,
            1.f);
        const float InputThreshold = ClampSKFiniteValue(
            TrajectoryPredictionSettings.InputThreshold,
            FSKTrajectoryPredictionSettings().InputThreshold,
            0.f,
            1.f);
        bNewHasMoveIntent = NewInputAmount > InputThreshold
            && NewMoveIntent.SizeSquared() > FMath::Square(InputThreshold);
        NewRequestedGait = MovementComponent->CurrentMovementTier;

        if (bNewHasMoveIntent && MovementComponent->HasDesiredMoveYawSnapshot())
        {
            const float SnapshotYaw = MovementComponent->GetDesiredMoveYawSnapshot();
            NewMoveYawWS = FMath::IsFinite(SnapshotYaw) ? SnapshotYaw : OwnerYawWS;
        }
        else if (bNewHasMoveIntent)
        {
            const float ControllerYawWS = MovementComponent->GetControllerYawOrFallback(OwnerYawWS);
            const float InputYawOffset = FMath::RadiansToDegrees(
                FMath::Atan2(NewMoveIntent.X, NewMoveIntent.Y));
            NewMoveYawWS = FMath::UnwindDegrees(
                (FMath::IsFinite(ControllerYawWS) ? ControllerYawWS : OwnerYawWS)
                + InputYawOffset);
        }

        bNewLockedOn = MovementComponent->IsLockedOn();
        if (bNewLockedOn)
        {
            const float LockYawWS = MovementComponent->HasLockTargetYaw()
                ? MovementComponent->GetLockTargetYawOrFallback(OwnerYawWS)
                : OwnerYawWS;
            NewFacingYawWS = FMath::IsFinite(LockYawWS) ? LockYawWS : OwnerYawWS;
        }
        else if (bNewHasMoveIntent)
        {
            NewFacingYawWS = NewMoveYawWS;
        }
    }

    const bool bIntentChanged = !MoveIntentSnapshot.Equals(NewMoveIntent, KINDA_SMALL_NUMBER)
        || !FMath::IsNearlyEqual(MoveInputAmountSnapshot, NewInputAmount, KINDA_SMALL_NUMBER)
        || !FMath::IsNearlyEqual(
            FMath::FindDeltaAngleDegrees(DesiredMoveYawSnapshot, NewMoveYawWS),
            0.f,
            KINDA_SMALL_NUMBER)
        || !FMath::IsNearlyEqual(
            FMath::FindDeltaAngleDegrees(DesiredFacingYawSnapshot, NewFacingYawWS),
            0.f,
            KINDA_SMALL_NUMBER)
        || RequestedGaitSnapshot != NewRequestedGait
        || bHasMoveIntentSnapshot != bNewHasMoveIntent
        || bLockedOnSnapshot != bNewLockedOn;

    MoveIntentSnapshot = NewMoveIntent;
    MoveInputAmountSnapshot = NewInputAmount;
    DesiredMoveYawSnapshot = FMath::UnwindDegrees(NewMoveYawWS);
    DesiredFacingYawSnapshot = FMath::UnwindDegrees(NewFacingYawWS);
    RequestedGaitSnapshot = NewRequestedGait;
    bHasMoveIntentSnapshot = bNewHasMoveIntent;
    bLockedOnSnapshot = bNewLockedOn;
    if (bIntentChanged)
    {
        IntentGeneration = IntentGeneration == MAX_int32 ? 1 : IntentGeneration + 1;
    }
}

/**
 * 保存当前世界变换并按历史时间域及样本上限删除旧快照。
 * 只能在游戏线程调用；历史以世界空间保存，查询时才转换到当前角色局部空间。
 */
void USKMotionMatchingTrajectoryComponent::TickHistoryEvictionPolicy()
{
    const UWorld* World = GetWorld();
    if (!World || !PresentWorldTransform.IsValid()) return;

    const float WorldGameTime = static_cast<float>(World->GetTimeSeconds());
    const float HistorySeconds = ClampSKFiniteValue(
        HistorySettings.Seconds,
        0.5f,
        SKMinPredictionSeconds,
        SKMaxPredictionSeconds);
    const int32 HistoryMaxSamples = FMath::Clamp(
        MaxSamples,
        SKMinPredictionSamples,
        SKMaxPredictionSamples);
    if (HistoryMaxSamples <= 0)
    {
        SampleHistory.Reset();
        return;
    }

    const float EarliestGameTime = WorldGameTime - HistorySeconds;
    int32 RemoveCount = 0;
    while (RemoveCount < SampleHistory.Num()
        && SampleHistory[RemoveCount].WorldGameTime < EarliestGameTime)
    {
        ++RemoveCount;
    }
    if (RemoveCount > 0) SampleHistory.RemoveAt(0, RemoveCount, EAllowShrinking::No);

    const int32 OverflowCount = SampleHistory.Num() - HistoryMaxSamples + 1;
    if (OverflowCount > 0) SampleHistory.RemoveAt(0, OverflowCount, EAllowShrinking::No);

    FSKTrajectoryHistorySample HistorySample;
    HistorySample.WorldTransform = PresentWorldTransform;
    HistorySample.WorldGameTime = WorldGameTime;
    HistorySample.ActualTranslationDeltaWS = LastActualTranslationDeltaWS;
    HistorySample.CollisionClippedTranslationWS = LastCollisionClippedTranslationWS;
    HistorySample.bWasCollisionClipped = !LastCollisionClippedTranslationWS.IsNearlyZero(0.1f);
    SampleHistory.Add(HistorySample);
}

/** 清空运行时历史快照；不改变预测参数和组件 Tick 状态。 */
void USKMotionMatchingTrajectoryComponent::FlushHistory()
{
    SampleHistory.Reset();
}

/**
 * 把保留的世界空间历史转换为相对当前角色的局部轨迹。
 * 只能在游戏线程读取组件快照；返回副本不持有内部数组引用。
 *
 * @return 仅包含负时间历史样本的轨迹，按时间从旧到新排列。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::GetHistory() const
{
    FTransformTrajectory History;
    const UWorld* World = GetWorld();
    if (!World || !PresentWorldTransform.IsValid()) return History;

    const float WorldGameTime = static_cast<float>(World->GetTimeSeconds());
    History.Samples.Reserve(SampleHistory.Num());
    for (const FSKTrajectoryHistorySample& HistorySample : SampleHistory)
    {
        const float TimeInSeconds = HistorySample.WorldGameTime - WorldGameTime;
        if (TimeInSeconds >= -UE_SMALL_NUMBER || !HistorySample.WorldTransform.IsValid()) continue;

        const FTransform RelativeTransform = HistorySample.WorldTransform.GetRelativeTransform(
            PresentWorldTransform);
        FTransformTrajectorySample TrajectorySample;
        TrajectorySample.SetTransform(RelativeTransform);
        TrajectorySample.Facing.Normalize();
        TrajectorySample.TimeInSeconds = TimeInSeconds;
        History.Samples.Add(TrajectorySample);
    }
    return History;
}

/**
 * 按过去、现在、未来的顺序组合轨迹值，不修改历史或预测输入。
 *
 * @param bIncludeHistory 是否附加组件保存的真实历史。
 * @param Prediction 正时间未来样本，不保留引用。
 * @return 包含一个零时刻占位样本的组合轨迹。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::CombineHistoryPresentPrediction(
    bool bIncludeHistory,
    const FTransformTrajectory& Prediction) const
{
    FTransformTrajectory Trajectory;
    const FTransformTrajectory History = bIncludeHistory ? GetHistory() : FTransformTrajectory();
    Trajectory.Samples.Reserve(History.Samples.Num() + Prediction.Samples.Num() + 1);
    Trajectory.Samples.Append(History.Samples);
    Trajectory.Samples.Add(FTransformTrajectorySample());
    Trajectory.Samples.Append(Prediction.Samples);
    return Trajectory;
}

/**
 * 采集当前帧真实 Actor 世界 Transform 和 MovementComponent 实际速度。
 * 只能在游戏线程调用；本实现不自行积分或改写角色。
 *
 * @param OutLinearVelocityWS 输出有限的水平世界速度，Owner 无效时归零。
 * @return 当前有效世界变换；Owner 或 Transform 无效时返回单位变换。
 */
FTransform USKMotionMatchingTrajectoryComponent::CalcWorldSpacePresentTransform(
    FVector& OutLinearVelocityWS) const
{
    OutLinearVelocityWS = FVector::ZeroVector;
    const APawn* OwnerPawn = TryGetOwnerPawn();
    if (!OwnerPawn) return FTransform::Identity;

    const FTransform OwnerTransform = OwnerPawn->GetActorTransform();
    if (!OwnerTransform.IsValid()) return FTransform::Identity;

    FVector HorizontalVelocity = OwnerPawn->GetVelocity();
    HorizontalVelocity.Z = 0.f;
    if (IsSKFiniteVector(HorizontalVelocity))
    {
        OutLinearVelocityWS = HorizontalVelocity;
    }

    return OwnerTransform;
}

/**
 * 从当前局部速度开始，以固定采样步长预测未来位移、速度和 Facing。
 * 只能在游戏线程读取 MovementComponent 快照；输出数组由本函数追加，调用方应传入空范围。
 * 本函数不调用 AddMovementInput，不写 CharacterMovement，也不改变 Actor Transform。
 *
 * @param MovementComponent 有效的项目移动组件，只读且不被持有。
 * @param PredictionSeconds 未来时间域，单位秒，调用方已限制到 0..10。
 * @param PredictionSampleRate 每秒采样数，调用方已限制到 5..120。
 * @param PredictionMaxSamples 最多生成的未来样本数，调用方已限制到 1..1200。
 * @param OutPrediction 输出角色局部未来轨迹，不包含历史和零时刻样本。
 */
void USKMotionMatchingTrajectoryComponent::PredictTrajectory(
    const USKMovementComponent* MovementComponent,
    float PredictionSeconds,
    int32 PredictionSampleRate,
    int32 PredictionMaxSamples,
    FTransformTrajectory& OutPrediction) const
{
    if (!MovementComponent || PredictionSeconds <= 0.f
        || PredictionSampleRate <= 0 || PredictionMaxSamples <= 0) return;

    bool bHasMoveInput = false;
    float MoveYawWS = 0.f;
    const FVector TargetVelocityLS = ResolveTargetVelocityLS(
        MovementComponent,
        bHasMoveInput,
        MoveYawWS);
    const float TargetFacingYawLS = ResolveFacingYawLS(
        MovementComponent,
        bHasMoveInput,
        MoveYawWS);
    const FSKTrajectoryPredictionSettings Defaults;
    const float Acceleration = ClampSKFiniteValue(
        TrajectoryPredictionSettings.Acceleration,
        Defaults.Acceleration,
        0.f,
        SKMaxPredictionAcceleration);
    const float Deceleration = ClampSKFiniteValue(
        TrajectoryPredictionSettings.Deceleration,
        Defaults.Deceleration,
        0.f,
        SKMaxPredictionAcceleration);
    const float FacingTurnRateDegrees = ClampSKFiniteValue(
        TrajectoryPredictionSettings.FacingTurnRateDegrees,
        Defaults.FacingTurnRateDegrees,
        0.f,
        SKMaxFacingTurnRate);

    FVector CurrentVelocityLS = IsSKFiniteVector(PresentLinearVelocityLS)
        ? PresentLinearVelocityLS
        : FVector::ZeroVector;
    CurrentVelocityLS.Z = 0.f;
    FVector CurrentLocationLS = FVector::ZeroVector;
    float CurrentFacingYawLS = 0.f;
    float AccumulatedSeconds = 0.f;
    const float FixedDeltaSeconds = 1.f / static_cast<float>(PredictionSampleRate);
    OutPrediction.Samples.Reserve(PredictionMaxSamples);

    for (int32 SampleIndex = 0; SampleIndex < PredictionMaxSamples; ++SampleIndex)
    {
        const float StepSeconds = FixedDeltaSeconds;
        const float NextAccumulatedSeconds = static_cast<float>(SampleIndex + 1) * FixedDeltaSeconds;
        if (NextAccumulatedSeconds > PredictionSeconds + UE_SMALL_NUMBER) break;

        const FVector PreviousVelocityLS = CurrentVelocityLS;
        const FVector DesiredVelocityLS = bHasMoveInput ? TargetVelocityLS : FVector::ZeroVector;
        const float VelocityChangeLimit = (bHasMoveInput ? Acceleration : Deceleration) * StepSeconds;
        const FVector VelocityDelta = DesiredVelocityLS - CurrentVelocityLS;
        CurrentVelocityLS += VelocityDelta.GetClampedToMaxSize(VelocityChangeLimit);
        if (!bHasMoveInput && CurrentVelocityLS.SizeSquared2D() <= 1.f)
        {
            CurrentVelocityLS = FVector::ZeroVector;
        }

        CurrentLocationLS += (PreviousVelocityLS + CurrentVelocityLS) * (0.5f * StepSeconds);
        CurrentLocationLS.Z = 0.f;
        CurrentFacingYawLS = FMath::FixedTurn(
            CurrentFacingYawLS,
            TargetFacingYawLS,
            FacingTurnRateDegrees * StepSeconds);
        AccumulatedSeconds = NextAccumulatedSeconds;

        FTransformTrajectorySample FutureSample;
        FutureSample.TimeInSeconds = AccumulatedSeconds;
        FutureSample.Facing = FRotator(0.f, CurrentFacingYawLS, 0.f).Quaternion();
        FutureSample.Position = CurrentLocationLS;
        OutPrediction.Samples.Add(FutureSample);
    }
}

/**
 * 根据屏幕 MoveIntent、输入强度、目标移动 Yaw 和当前速度档位解析局部目标速度。
 * 只能在游戏线程读取 MovementComponent；输出参数始终初始化，不保留组件引用。
 *
 * @param MovementComponent 有效的项目移动组件，只读。
 * @param bOutHasMoveInput 输出是否存在超过阈值的有效移动输入。
 * @param OutMoveYawWS 输出移动方向的世界 Yaw，单位度；无输入时为 Owner Yaw。
 * @return 角色局部水平目标速度，单位 cm/s；输入或数据无效时返回零向量。
 */
FVector USKMotionMatchingTrajectoryComponent::ResolveTargetVelocityLS(
    const USKMovementComponent* MovementComponent,
    bool& bOutHasMoveInput,
    float& OutMoveYawWS) const
{
    bOutHasMoveInput = false;
    OutMoveYawWS = 0.f;
    const APawn* OwnerPawn = TryGetOwnerPawn();
    if (!MovementComponent || !OwnerPawn) return FVector::ZeroVector;

    const float OwnerYawWS = FMath::IsFinite(OwnerPawn->GetActorRotation().Yaw)
        ? OwnerPawn->GetActorRotation().Yaw
        : 0.f;
    OutMoveYawWS = OwnerYawWS;
    const float InputX = FMath::IsFinite(MovementComponent->GetMoveInputX())
        ? MovementComponent->GetMoveInputX()
        : 0.f;
    const float InputY = FMath::IsFinite(MovementComponent->GetMoveInputY())
        ? MovementComponent->GetMoveInputY()
        : 0.f;
    const float InputAmount = ClampSKFiniteValue(
        MovementComponent->GetMoveInputAmount(),
        0.f,
        0.f,
        1.f);
    const float InputThreshold = ClampSKFiniteValue(
        TrajectoryPredictionSettings.InputThreshold,
        FSKTrajectoryPredictionSettings().InputThreshold,
        0.f,
        1.f);
    const FVector2D MoveIntent(InputX, InputY);
    if (InputAmount <= InputThreshold || MoveIntent.SizeSquared() <= FMath::Square(InputThreshold))
    {
        return FVector::ZeroVector;
    }

    if (MovementComponent->HasDesiredMoveYawSnapshot())
    {
        const float SnapshotYaw = MovementComponent->GetDesiredMoveYawSnapshot();
        OutMoveYawWS = FMath::IsFinite(SnapshotYaw) ? SnapshotYaw : OwnerYawWS;
    }
    else
    {
        const float ControllerYawWS = MovementComponent->GetControllerYawOrFallback(OwnerYawWS);
        const float InputYawOffset = FMath::RadiansToDegrees(FMath::Atan2(InputX, InputY));
        OutMoveYawWS = FMath::UnwindDegrees(
            (FMath::IsFinite(ControllerYawWS) ? ControllerYawWS : OwnerYawWS) + InputYawOffset);
    }

    const float TierSpeed = ResolveGaitProfileSpeed(MovementComponent);
    if (TierSpeed <= 0.f) return FVector::ZeroVector;

    const FVector TargetDirectionWS = FRotator(0.f, OutMoveYawWS, 0.f).Vector();
    FVector TargetDirectionLS = OwnerPawn->GetActorTransform().InverseTransformVectorNoScale(TargetDirectionWS);
    TargetDirectionLS.Z = 0.f;
    TargetDirectionLS = TargetDirectionLS.GetSafeNormal();
    if (!IsSKFiniteVector(TargetDirectionLS)) return FVector::ZeroVector;

    bOutHasMoveInput = true;
    return TargetDirectionLS * TierSpeed * InputAmount;
}

/**
 * 解析预测 Facing 相对当前 Actor 的局部 Yaw，锁定时始终优先面向锁定目标。
 * 只能在游戏线程读取锁定快照；Facing 与目标移动速度分别计算，因此允许侧移和后退。
 *
 * @param MovementComponent 有效的项目移动组件，只读。
 * @param bHasMoveInput 当前是否存在有效移动输入。
 * @param MoveYawWS 有输入时的世界移动方向 Yaw，单位度。
 * @return 相对当前 Actor 的目标局部 Yaw，单位度且位于 -180..180。
 */
float USKMotionMatchingTrajectoryComponent::ResolveFacingYawLS(
    const USKMovementComponent* MovementComponent,
    bool bHasMoveInput,
    float MoveYawWS) const
{
    const APawn* OwnerPawn = TryGetOwnerPawn();
    if (!MovementComponent || !OwnerPawn) return 0.f;

    const float OwnerYawWS = FMath::IsFinite(OwnerPawn->GetActorRotation().Yaw)
        ? OwnerPawn->GetActorRotation().Yaw
        : 0.f;
    float FacingYawWS = bHasMoveInput && FMath::IsFinite(MoveYawWS) ? MoveYawWS : OwnerYawWS;
    if (MovementComponent->IsLockedOn())
    {
        const float LockYawWS = MovementComponent->HasLockTargetYaw()
            ? MovementComponent->GetLockTargetYawOrFallback(OwnerYawWS)
            : OwnerYawWS;
        FacingYawWS = FMath::IsFinite(LockYawWS) ? LockYawWS : OwnerYawWS;
    }

    return FMath::FindDeltaAngleDegrees(OwnerYawWS, FacingYawWS);
}

/**
 * 把 Lua 发布的当前 MovementTier 映射到对应 Gait 动画族的离线 RootMotion 标称速度。
 * 只能在游戏线程读取 MovementComponent；本函数不修改 MovementComponent 速度或档位。
 * 返回值来自独立配置，不读取 Motion Matching 本帧尚未选中的动画或数据库结果。
 *
 * @param MovementComponent 有效的项目移动组件，只读。
 * @return 非负且有限的目标速度，单位 cm/s；Idle 或非法数据返回 0。
 */
float USKMotionMatchingTrajectoryComponent::ResolveGaitProfileSpeed(
    const USKMovementComponent* MovementComponent) const
{
    if (!MovementComponent) return 0.f;

    float TierSpeed = 0.f;
    switch (MovementComponent->CurrentMovementTier)
    {
    case ESKMovementTier::Walk:
        TierSpeed = AnimationRootMotionSpeedProfile.Walk;
        break;
    case ESKMovementTier::Crouch:
        TierSpeed = AnimationRootMotionSpeedProfile.Crouch;
        break;
    case ESKMovementTier::Run:
        TierSpeed = AnimationRootMotionSpeedProfile.Run;
        break;
    case ESKMovementTier::Sprint:
        TierSpeed = AnimationRootMotionSpeedProfile.Sprint;
        break;
    case ESKMovementTier::Idle:
    default:
        break;
    }

    return ClampSKFiniteValue(TierSpeed, 0.f, 0.f, 10000.f);
}

/**
 * 过滤组合轨迹中的非法或乱序样本，并强制插入唯一的零时刻当前样本。
 * 可在游戏线程对组件快照调用；函数只复制值，不修改基类历史缓冲或输入范围。
 *
 * @param CombinedTrajectory 基类按历史、现在、未来拼接的角色局部轨迹。
 * @return 仅含有限值且 TimeInSeconds 严格递增的角色局部轨迹。
 */
FTransformTrajectory USKMotionMatchingTrajectoryComponent::SanitizeTrajectoryRange(
    const FTransformTrajectory& CombinedTrajectory) const
{
    FTransformTrajectory SanitizedTrajectory;
    SanitizedTrajectory.Samples.Reserve(CombinedTrajectory.Samples.Num() + 1);
    float PreviousSeconds = -TNumericLimits<float>::Max();

    for (const FTransformTrajectorySample& Sample : CombinedTrajectory.Samples)
    {
        if (Sample.TimeInSeconds >= 0.f) break;
        if (!IsSKFiniteTrajectorySample(Sample)
            || Sample.TimeInSeconds <= PreviousSeconds) continue;

        SanitizedTrajectory.Samples.Add(Sample);
        PreviousSeconds = Sample.TimeInSeconds;
    }

    FTransformTrajectorySample PresentSample;
    PresentSample.TimeInSeconds = 0.f;
    SanitizedTrajectory.Samples.Add(PresentSample);
    PreviousSeconds = 0.f;

    for (const FTransformTrajectorySample& Sample : CombinedTrajectory.Samples)
    {
        if (Sample.TimeInSeconds <= 0.f) continue;
        if (!IsSKFiniteTrajectorySample(Sample)
            || Sample.TimeInSeconds <= PreviousSeconds) continue;

        SanitizedTrajectory.Samples.Add(Sample);
        PreviousSeconds = Sample.TimeInSeconds;
    }

    TransformTrajectoryToQuerySpace(SanitizedTrajectory);
    return SanitizedTrajectory;
}

/**
 * 将局部轨迹的位置和线速度旋转到动画数据库使用的根位移坐标空间。
 * 可在游戏线程对当前函数独占的值快照调用；只修改水平 Translation 与 LinearVelocity，
 * 样本时间、Facing Rotation、Scale 和垂直分量保持不变。
 *
 * @param Trajectory 已完成合法性过滤、即将返回给 AnimGraph 的轨迹快照。
 */
void USKMotionMatchingTrajectoryComponent::TransformTrajectoryToQuerySpace(
    FTransformTrajectory& Trajectory) const
{
    // 只狼源动画的根位移前向轴是本地 -Y；查询位置/速度从 UE 角色 +X 前向旋转到该轴。
    const FQuat QueryRotation(
        FVector::UpVector,
        FMath::DegreesToRadians(SKTrajectoryTranslationYawOffsetDegrees));
    for (FTransformTrajectorySample& Sample : Trajectory.Samples)
    {
        const float PositionZ = Sample.Position.Z;
        Sample.Position.Z = 0.f;
        Sample.Position = QueryRotation.RotateVector(Sample.Position);
        Sample.Position.Z = PositionZ;
    }
}
