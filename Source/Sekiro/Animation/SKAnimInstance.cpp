#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "Input/SKInputManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimEnums.h"
#include "KismetAnimationLibrary.h"
#include "SekiroAnimLogicData.h"

namespace
{
bool SKIsMovingGait(ESKAnimGait InGait)
{
    return InGait == ESKAnimGait::Walk
        || InGait == ESKAnimGait::Run
        || InGait == ESKAnimGait::Sprint;
}

ESKAnimGait SKConvertMovementTierToGait(ESKMovementTier InTier, bool bWantsMovement)
{
    if (!bWantsMovement)
    {
        return ESKAnimGait::Idle;
    }

    switch (InTier)
    {
    case ESKMovementTier::Walk:
    case ESKMovementTier::Crouch:
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
}

USKAnimInstance::USKAnimInstance()
{
    bAutoUpdateLuaDrivenAnimation = false;
    DefaultLuaAnimModuleName = TEXT("Animation.Sekiro.ABP_Sekiro");
    RootMotionMode = ERootMotionMode::RootMotionFromEverything;
}

void USKAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    RootMotionMode = ERootMotionMode::RootMotionFromEverything;
    OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
    if (OwnerCharacter)
    {
        OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
        OwnerCameraManager = OwnerCharacter->FindComponentByClass<USKCameraManagerComponent>();
        OwnerInputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
    }
}

void USKAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
        if (OwnerCharacter)
        {
            OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
            OwnerCameraManager = OwnerCharacter->FindComponentByClass<USKCameraManagerComponent>();
            OwnerInputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
        }
    }
    if (!OwnerCharacter) return;

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

    Angle = UKismetAnimationLibrary::CalculateDirection(
        Velocity, OwnerCharacter->GetActorRotation());
    Direction = SKConvertAngleToDirection(Angle);

    if (OwnerCameraManager)
    {
        bIsLockedOn = OwnerCameraManager->IsLockedOn();
        bIsSprintCameraAligning = OwnerCameraManager->IsSprintCameraAligning();
        CameraMode = OwnerCameraManager->GetCameraMode();
        MoveDirectionAngle = OwnerCameraManager->GetMoveDirectionAngle();
    }
    else
    {
        bIsLockedOn = false;
        bIsSprintCameraAligning = false;
        CameraMode = ESKCameraMode::Free;
        MoveDirectionAngle = Angle;
    }

    DirectionDelta = FMath::FindDeltaAngleDegrees(Angle, MoveDirectionAngle);
    AimYawDelta = FMath::FindDeltaAngleDegrees(OwnerCharacter->GetActorRotation().Yaw, OwnerCharacter->GetControlRotation().Yaw);
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

    DesiredGait = SKConvertMovementTierToGait(MovementTier, bHasMovementInput || bIsMoving);
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

    // ── FrameFlags 从动画曲线读取（供 ABP 消费） ──────────────
    UAnimMontage* CurrentMontage = GetCurrentActiveMontage();
    if (CurrentMontage)
    {
        float CurveValue = 0.f;
        if (GetCurveValue(TEXT("FrameFlags"), CurveValue))
        {
            int32 Mask = FMath::RoundToInt(CurveValue);
            bDisableTurning  = (Mask & (1 << (uint8)ESKFrameFlag::DisableTurning)) != 0;
            bDisableMovement = (Mask & (1 << (uint8)ESKFrameFlag::DisableMovement)) != 0
                            || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedWalk)) != 0
                            || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedDash)) != 0;
            bCanDeflect      = (Mask & (1 << (uint8)ESKFrameFlag::EnableParry)) != 0
                            && (Mask & (1 << (uint8)ESKFrameFlag::DisableParry)) == 0;
            bInvincible      = (Mask & (1 << (uint8)ESKFrameFlag::Invincible)) != 0;
        }
    }

    UpdateLuaDrivenAnimation(DeltaSeconds);
}
