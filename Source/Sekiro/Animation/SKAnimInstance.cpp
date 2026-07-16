#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "Input/SKInputManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimEnums.h"
#include "KismetAnimationLibrary.h"

namespace
{
/**
 * @brief 判断动画步态是否属于有位移的 Walk、Run 或 Sprint。
 *
 * @param InGait ESKAnimGait，待检查的动画步态。
 * @return bool，属于移动步态时返回 true，否则返回 false。
 */
bool SKIsMovingGait(ESKAnimGait InGait)
{
    return InGait == ESKAnimGait::Walk
        || InGait == ESKAnimGait::Run
        || InGait == ESKAnimGait::Sprint;
}

/**
 * @brief 根据移动输入、摇杆阈值和上一帧步态解析蹲姿下的目标步态。
 *
 * @param bHasMovementInput bool，当前是否存在有效移动输入。
 * @param MovementInputAmount float，当前移动输入强度，预期范围为 [0, 1]。
 * @param CurrentGait ESKAnimGait，状态机当前使用的动画步态，用于阈值滞回。
 * @param InputManager const USKInputManager*，输入组件；为空时退化为当前 Walk 或默认 Run。
 * @return ESKAnimGait，解析后的 Idle、Walk 或 Run；蹲姿不会返回 Sprint。
 */
ESKAnimGait SKResolveCrouchGait(
    bool bHasMovementInput,
    float MovementInputAmount,
    ESKAnimGait CurrentGait,
    const USKInputManager* InputManager)
{
    if (!bHasMovementInput)
    {
        return ESKAnimGait::Idle;
    }

    if (!InputManager)
    {
        return CurrentGait == ESKAnimGait::Walk
            ? ESKAnimGait::Walk
            : ESKAnimGait::Run;
    }

    if (InputManager->IsWalkHeld())
    {
        return ESKAnimGait::Walk;
    }

    const float WalkEnterThreshold = InputManager->GetAnalogWalkEnterThreshold();
    const float RunEnterThreshold = InputManager->GetAnalogRunEnterThreshold();
    if (CurrentGait == ESKAnimGait::Walk)
    {
        return MovementInputAmount >= RunEnterThreshold
            ? ESKAnimGait::Run
            : ESKAnimGait::Walk;
    }

    return MovementInputAmount <= WalkEnterThreshold
        ? ESKAnimGait::Walk
        : ESKAnimGait::Run;
}

/**
 * @brief 将移动组件的速度档位转换为动画步态。
 *
 * @param InTier ESKMovementTier，移动组件当前请求的速度档位。
 * @param bWantsMovement bool，角色当前是否仍有移动输入或实际水平位移。
 * @return ESKAnimGait，没有移动意图时返回 Idle，否则返回对应的 Walk、Run 或 Sprint。
 */
ESKAnimGait SKConvertMovementTierToGait(ESKMovementTier InTier, bool bWantsMovement)
{
    if (!bWantsMovement)
    {
        return ESKAnimGait::Idle;
    }

    switch (InTier)
    {
    case ESKMovementTier::Walk:
        return ESKAnimGait::Walk;
    case ESKMovementTier::Sprint:
        return ESKAnimGait::Sprint;
    case ESKMovementTier::Idle:
        return ESKAnimGait::Idle;
    case ESKMovementTier::Run:
    default:
        return ESKAnimGait::Run;
    }
}

/**
 * @brief 获取指定动画步态对应的项目参考速度。
 *
 * @param InGait ESKAnimGait，待查询的动画步态。
 * @param Movement const USKMovementComponent*，项目移动组件；为空时使用当前兼容默认速度。
 * @return float，对应步态的水平参考速度，单位为 cm/s；Idle 返回 0。
 */
float SKGetGaitReferenceSpeed(ESKAnimGait InGait, const USKMovementComponent* Movement)
{
    if (!Movement)
    {
        switch (InGait)
        {
        case ESKAnimGait::Walk:   return 140.f;
        case ESKAnimGait::Sprint: return 853.f;
        case ESKAnimGait::Run:    return 407.f;
        case ESKAnimGait::Idle:
        default:
            return 0.f;
        }
    }

    switch (InGait)
    {
    case ESKAnimGait::Walk:   return Movement->WalkSpeed;
    case ESKAnimGait::Sprint: return Movement->SprintSpeed;
    case ESKAnimGait::Run:    return Movement->RunSpeed;
    case ESKAnimGait::Idle:
    default:
        return 0.f;
    }
}

/**
 * @brief 根据当前速度计算源步态到目标步态的归一化切换进度。
 *
 * @param CurrentGait ESKAnimGait，当前动画步态。
 * @param TargetGait ESKAnimGait，期望切换到的动画步态。
 * @param CurrentSpeed float，角色当前水平速度，单位为 cm/s。
 * @param Movement const USKMovementComponent*，项目移动组件；为空时使用兼容默认速度。
 * @return float，范围为 [0, 1] 的切换进度；1 表示已经到达目标步态速度区间。
 */
float SKCalculateGaitBlendAlpha(
    ESKAnimGait CurrentGait,
    ESKAnimGait TargetGait,
    float CurrentSpeed,
    const USKMovementComponent* Movement)
{
    const float SourceSpeed = SKGetGaitReferenceSpeed(CurrentGait, Movement);
    const float TargetSpeed = SKGetGaitReferenceSpeed(TargetGait, Movement);
    if (FMath::IsNearlyEqual(SourceSpeed, TargetSpeed))
    {
        return 1.f;
    }

    const float LowSpeed = FMath::Min(SourceSpeed, TargetSpeed);
    const float HighSpeed = FMath::Max(SourceSpeed, TargetSpeed);
    const float RangeAlpha = FMath::GetMappedRangeValueClamped(FVector2D(LowSpeed, HighSpeed), FVector2D(0.f, 1.f), CurrentSpeed);
    return TargetSpeed > SourceSpeed ? RangeAlpha : 1.f - RangeAlpha;
}

/**
 * @brief 将局部方向角按 45 度扇区转换为八方向动画枚举。
 *
 * @param InAngle float，角色局部方向角，单位为度，预期范围为 [-180, 180]。
 * @return ESKLocomotionDirection，对应的前、后、左、右或四个斜向枚举。
 */
ESKLocomotionDirection SKConvertAngleToDirection(float InAngle)
{
    if (InAngle > -22.5f && InAngle <= 22.5f) return ESKLocomotionDirection::Fwd;
    if (InAngle > 22.5f && InAngle <= 67.5f) return ESKLocomotionDirection::Fwd_R;
    if (InAngle > 67.5f && InAngle <= 112.5f) return ESKLocomotionDirection::R;
    if (InAngle > 112.5f && InAngle <= 157.5f) return ESKLocomotionDirection::Bwd_R;
    if (InAngle > 157.5f || InAngle <= -157.5f) return ESKLocomotionDirection::Bwd;
    if (InAngle > -157.5f && InAngle <= -112.5f) return ESKLocomotionDirection::Bwd_L;
    if (InAngle > -112.5f && InAngle <= -67.5f) return ESKLocomotionDirection::L;
    return ESKLocomotionDirection::Fwd_L;
}

/**
 * @brief 将相机空间移动输入转换成世界空间的目标移动 Yaw。
 *
 * @param InOwnerCharacter const ASKCharacter*，提供控制器和控制旋转的角色；不能为空。
 * @param InMoveIntent const FVector2D&，屏幕空间移动输入，X 为左右、Y 为前后。
 * @param OutYaw float&，成功时写入目标世界 Yaw，单位为度；失败时保持原值。
 * @return bool，成功获得有效世界方向时返回 true，否则返回 false。
 */
bool SKResolveMoveIntentYaw(const ASKCharacter* InOwnerCharacter, const FVector2D& InMoveIntent, float& OutYaw)
{
    if (!InOwnerCharacter) return false;
    if (InMoveIntent.SizeSquared() < FMath::Square(0.1f)) return false;

    const AController* Controller = InOwnerCharacter->GetController();
    if (!Controller) return false;

    const FVector2D NormalizedIntent = InMoveIntent.GetSafeNormal();
    const FRotator ControlYawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);
    const FVector DesiredDirection = (Forward * NormalizedIntent.Y + Right * NormalizedIntent.X).GetSafeNormal2D();
    if (DesiredDirection.IsNearlyZero()) return false;

    OutYaw = DesiredDirection.Rotation().Yaw;
    return true;
}
}

/**
 * 初始化项目动画实例的 UE Root Motion 基线。
 * 构造阶段不访问 Pawn 或世界，只设置动画实例自身状态；必须在游戏线程创建 UObject。
 */
USKAnimInstance::USKAnimInstance()
{
    RootMotionMode = ERootMotionMode::RootMotionFromEverything;
    bActorYawOwnedByRootMotion = false;
}

/**
 * 初始化动画实例并缓存所属角色、移动组件、相机组件和输入组件。
 * 由 UE 动画生命周期在游戏线程调用；会更新本实例持有的 UObject 引用，不执行 Pose 求值。
 */
void USKAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    RootMotionMode = ERootMotionMode::RootMotionFromEverything;
    CacheOwnerReferences();
}

/**
 * 采集角色当前帧的移动、输入、相机、步态和转向数据，供 AnimBlueprint 只读消费。
 * 由 UE 动画生命周期在游戏线程调用；不会主动更新脚本状态机，也不直接修改角色移动。
 *
 * @param DeltaSeconds 当前动画更新步长，单位为秒；非正值仍会刷新瞬时采集数据。
 */
void USKAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwnerCharacter)
    {
        CacheOwnerReferences();
    }
    if (!OwnerCharacter) return;

    const bool bWasInAir = bIsInAir;
    const bool bWasCrouching = bIsCrouching;
    const ESKAnimRotationMode PreviousRotationMode = RotationMode;

    // ── Locomotion ────────────────────────────────────────────
    UCharacterMovementComponent* CharacterMovement = OwnerCharacter->GetCharacterMovement();
    bIsInAir = CharacterMovement ? CharacterMovement->IsFalling() : false;
    bIsCrouching = OwnerCharacter->bIsCrouched;

    if (OwnerMovement)
    {
        MovementTier = OwnerMovement->CurrentMovementTier;
    }

    Velocity = OwnerCharacter->GetVelocity();
    VerticalVelocity = Velocity.Z;
    Speed = Velocity.Size2D();
    Acceleration = CharacterMovement ? CharacterMovement->GetCurrentAcceleration() : FVector::ZeroVector;
    AccelerationAmount = Acceleration.Size2D();
    const FVector2D MoveIntent = OwnerInputManager ? OwnerInputManager->GetMoveIntent() : FVector2D::ZeroVector;
    MoveInputX = MoveIntent.X;
    MoveInputY = MoveIntent.Y;
    MovementInputAmount = OwnerInputManager ? OwnerInputManager->GetMoveInputAmount() : 0.f;
    bIsMoving = Speed > 3.f;
    bHasMovementInput = MovementInputAmount > 0.1f;
    bIsAccelerating = AccelerationAmount > 3.f;

    const FRotator ActorRotation = OwnerCharacter->GetActorRotation();
    Angle = UKismetAnimationLibrary::CalculateDirection(Velocity, ActorRotation);
    Direction = SKConvertAngleToDirection(Angle);

    if (OwnerCameraManager)
    {
        bIsLockedOn = OwnerCameraManager->IsLockedOn();
        bIsSprintCameraAligning = OwnerCameraManager->IsSprintCameraAligning();
        CameraMode = OwnerCameraManager->GetCameraMode();
    }
    else
    {
        bIsLockedOn = false;
        bIsSprintCameraAligning = false;
        CameraMode = ESKCameraMode::Free;
    }

    ActorYaw = ActorRotation.Yaw;
    bHasDesiredMoveYaw = SKResolveMoveIntentYaw(OwnerCharacter, MoveIntent, DesiredMoveYaw);
    if (bHasDesiredMoveYaw)
    {
        MoveDirectionAngle = FMath::FindDeltaAngleDegrees(ActorYaw, DesiredMoveYaw);
    }
    else if (!Velocity.IsNearlyZero())
    {
        DesiredMoveYaw = ActorYaw;
        MoveDirectionAngle = Angle;
    }
    else
    {
        DesiredMoveYaw = ActorYaw;
        MoveDirectionAngle = 0.f;
    }

    DirectionDelta = FMath::FindDeltaAngleDegrees(Angle, MoveDirectionAngle);
    AimYawDelta = FMath::FindDeltaAngleDegrees(ActorYaw, OwnerCharacter->GetControlRotation().Yaw);
    RootYawOffset = 0.f;

    if (bIsInAir)
    {
        MovementState = ESKAnimMovementState::InAir;
    }
    else if (bIsCrouching)
    {
        MovementState = ESKAnimMovementState::Crouching;
    }
    else
    {
        MovementState = ESKAnimMovementState::Grounded;
    }

    if (!bWasInAir && bIsInAir)
    {
        bJumpStartedCrouched = bWasCrouching;
    }

    Stance = bIsCrouching ? ESKAnimStance::Crouching : ESKAnimStance::Standing;

    if (CameraMode == ESKCameraMode::SprintAlign)
    {
        RotationMode = ESKAnimRotationMode::SprintAlign;
    }
    else if (CameraMode == ESKCameraMode::LockOn)
    {
        RotationMode = ESKAnimRotationMode::LookingDirection;
    }
    else
    {
        RotationMode = ESKAnimRotationMode::VelocityDirection;
    }

    DesiredGait = bIsCrouching || MovementTier == ESKMovementTier::Crouch
        ? SKResolveCrouchGait(bHasMovementInput, MovementInputAmount, Gait, OwnerInputManager)
        : SKConvertMovementTierToGait(MovementTier, bHasMovementInput || bIsMoving);
    if (!SKIsMovingGait(DesiredGait))
    {
        Gait = ESKAnimGait::Idle;
        GaitBlendAlpha = 1.f;
        bIsGaitChanging = false;
    }
    else if (!SKIsMovingGait(Gait))
    {
        Gait = DesiredGait;
        GaitBlendAlpha = 1.f;
        bIsGaitChanging = false;
    }
    else if (Gait != DesiredGait)
    {
        GaitBlendAlpha = SKCalculateGaitBlendAlpha(Gait, DesiredGait, Speed, OwnerMovement);
        bIsGaitChanging = GaitBlendAlpha < 0.98f;
        if (!bIsGaitChanging)
        {
            Gait = DesiredGait;
            GaitBlendAlpha = 1.f;
        }
    }
    else
    {
        GaitBlendAlpha = 1.f;
        bIsGaitChanging = false;
    }

    // ── Dodge ─────────────────────────────────────────────────
    bIsDodging = OwnerInputManager ? OwnerInputManager->IsDodgeActive() : OwnerCharacter->bIsDodging;
    DodgeDirection = OwnerCharacter->DodgeDirection;
    DodgeDirectionLateral = OwnerCharacter->DodgeDirectionLateral;

    const float TurnEnterAngle = 60.f;
    const float AbsDirectionDelta = FMath::Abs(DirectionDelta);
    const bool bChangedBetweenLockAndFree =
        (PreviousRotationMode == ESKAnimRotationMode::VelocityDirection && RotationMode == ESKAnimRotationMode::LookingDirection)
        || (PreviousRotationMode == ESKAnimRotationMode::LookingDirection && RotationMode == ESKAnimRotationMode::VelocityDirection);
    if (bChangedBetweenLockAndFree
        && !bIsInAir
        && !bIsDodging
        && DesiredGait != ESKAnimGait::Sprint
        && (bIsMoving || bHasMovementInput))
    {
        const float CandidateTurnAngle = RotationMode == ESKAnimRotationMode::LookingDirection
            ? AimYawDelta
            : MoveDirectionAngle;
        if (FMath::Abs(CandidateTurnAngle) >= RotationModeTurnEnterAngle)
        {
            RotationModeTransitionAngle = CandidateTurnAngle;
            RotationModeTurnTimeRemaining = RotationModeTurnDuration;
        }
    }

    if (RotationModeTurnTimeRemaining > 0.f)
    {
        RotationModeTurnTimeRemaining = FMath::Max(0.f, RotationModeTurnTimeRemaining - DeltaSeconds);
    }

    bShouldRotationModeTurn = RotationModeTurnTimeRemaining > 0.f
        && !bIsInAir
        && !bIsDodging
        && DesiredGait != ESKAnimGait::Sprint;

    const float EffectiveTurnAngle = bShouldRotationModeTurn
        ? RotationModeTransitionAngle
        : (bIsMoving ? DirectionDelta : MoveDirectionAngle);
    TurnAngle = EffectiveTurnAngle;
    TurnDirection = FMath::Abs(EffectiveTurnAngle) < 5.f
        ? ESKAnimTurnDirection::None
        : (EffectiveTurnAngle >= 0.f ? ESKAnimTurnDirection::Right : ESKAnimTurnDirection::Left);

    bShouldTurnInPlace = !bIsMoving && bIsLockedOn && FMath::Abs(AimYawDelta) > 60.f;
    bShouldPivot = false;
    bShouldTurn = bIsMoving && bHasMovementInput && AbsDirectionDelta >= TurnEnterAngle && DesiredGait != ESKAnimGait::Sprint;

    if (bIsDodging && !bIsInAir)
    {
        GroundedEntryState = ESKAnimGroundedEntryState::DodgeStep;
    }
    else if (bShouldRotationModeTurn)
    {
        GroundedEntryState = ESKAnimGroundedEntryState::Turn;
    }
    else if (!bIsMoving && !bHasMovementInput)
    {
        GroundedEntryState = bShouldTurnInPlace ? ESKAnimGroundedEntryState::TurnInPlace : ESKAnimGroundedEntryState::Idle;
    }
    else if (!bHasMovementInput)
    {
        GroundedEntryState = ESKAnimGroundedEntryState::Stop;
    }
    else if (!bIsMoving || Speed < 30.f)
    {
        GroundedEntryState = ESKAnimGroundedEntryState::Start;
    }
    else
    {
        GroundedEntryState = ESKAnimGroundedEntryState::Cycle;
    }

}

/**
 * 查询项目动画图是否声明接管角色世界 Yaw。
 * 本函数只读取动画实例字段，可在游戏线程的相机和移动更新中调用，不修改任何状态。
 *
 * @return 动画图已接管角色世界 Yaw 时返回 true；默认返回 false。
 */
bool USKAnimInstance::IsActorYawOwnedByRootMotion() const
{
    return bActorYawOwnedByRootMotion;
}

/**
 * 从当前动画 Pawn 缓存角色以及动画数据采集所需的项目组件。
 * 由动画生命周期在游戏线程调用；成功时替换缓存引用，Pawn 不是 ASKCharacter 时清空全部缓存。
 */
void USKAnimInstance::CacheOwnerReferences()
{
    OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
    if (!OwnerCharacter)
    {
        OwnerMovement = nullptr;
        OwnerCameraManager = nullptr;
        OwnerInputManager = nullptr;
        return;
    }

    OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
    OwnerCameraManager = OwnerCharacter->FindComponentByClass<USKCameraManagerComponent>();
    OwnerInputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
}
