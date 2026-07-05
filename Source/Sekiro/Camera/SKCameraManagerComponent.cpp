#include "Camera/SKCameraManagerComponent.h"

#include "Input/SKInputManager.h"
#include "Movement/SKMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"

USKCameraManagerComponent::USKCameraManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;

    bInvertPitch = false;
}

void USKCameraManagerComponent::SetLockTarget(AActor* NewTarget)
{
    if (NewTarget == GetOwner())
    {
        LockTarget = nullptr;
        return;
    }

    LockTarget = NewTarget;
}

void USKCameraManagerComponent::ClearLockTarget()
{
    LockTarget = nullptr;
}

void USKCameraManagerComponent::ToggleLockTarget(AActor* NewTarget)
{
    if (IsLockedOn())
    {
        ClearLockTarget();
        return;
    }

    SetLockTarget(NewTarget);
}

bool USKCameraManagerComponent::ToggleLockTargetInView()
{
    if (IsLockedOn())
    {
        ClearLockTarget();
        return false;
    }

    AActor* BestTarget = FindBestLockTargetInView();
    if (!BestTarget) return false;

    SetLockTarget(BestTarget);
    return IsLockedOn();
}

AActor* USKCameraManagerComponent::FindBestLockTargetInView() const
{
    if (!OwnerCharacter || !GetWorld()) return nullptr;

    FVector ViewLocation = OwnerCharacter->GetActorLocation();
    FRotator ViewRotation = OwnerCharacter->GetActorRotation();
    const AController* Controller = OwnerCharacter->GetController();
    if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
    }
    else if (Controller)
    {
        ViewRotation = Controller->GetControlRotation();
    }

    const FVector ViewForward = ViewRotation.Vector();
    const float ClampedMaxRange = FMath::Max(1.f, MaxLockOnRange);
    const float ClampedHalfAngle = FMath::Max(1.f, LockOnSearchHalfAngle);
    const float MaxRangeSq = FMath::Square(ClampedMaxRange);
    AActor* BestTarget = nullptr;
    float BestScore = -TNumericLimits<float>::Max();

    for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        ACharacter* Candidate = *It;
        if (!IsValid(Candidate) || Candidate == OwnerCharacter.Get()) continue;

        const FVector ToTarget = Candidate->GetActorLocation() - ViewLocation;
        const float DistanceSq = ToTarget.SizeSquared();
        if (DistanceSq <= KINDA_SMALL_NUMBER || DistanceSq > MaxRangeSq) continue;

        const FVector TargetDirection = ToTarget.GetSafeNormal();
        const float Dot = FMath::Clamp(FVector::DotProduct(ViewForward, TargetDirection), -1.f, 1.f);
        const float Angle = FMath::RadiansToDegrees(FMath::Acos(Dot));
        if (Angle > ClampedHalfAngle) continue;

        const float Distance = FMath::Sqrt(DistanceSq);
        const float AngleScore = 1.f - (Angle / ClampedHalfAngle);
        const float DistanceScore = 1.f - (Distance / ClampedMaxRange);
        const float Score = AngleScore * LockOnAngleScoreWeight + DistanceScore * LockOnDistanceScoreWeight;
        if (Score > BestScore)
        {
            BestScore = Score;
            BestTarget = Candidate;
        }
    }

    return BestTarget;
}

bool USKCameraManagerComponent::IsLockedOn() const
{
    return IsValid(LockTarget);
}

AActor* USKCameraManagerComponent::GetLockTarget() const
{
    return LockTarget;
}

void USKCameraManagerComponent::AddLookInput(FVector2D LookAxis)
{
    LookAxis.X *= LookSensitivityYaw;
    LookAxis.Y *= LookSensitivityPitch;

    if (bInvertPitch)
    {
        LookAxis.Y *= -1.f;
    }

    PendingLookInput += LookAxis;
}

ESKCameraMode USKCameraManagerComponent::GetCameraMode() const
{
    return CameraMode;
}

bool USKCameraManagerComponent::IsSprintCameraAligning() const
{
    return CameraMode == ESKCameraMode::SprintAlign;
}

float USKCameraManagerComponent::GetMoveDirectionAngle() const
{
    return MoveDirectionAngle;
}

void USKCameraManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    RefreshCachedComponents();
}

void USKCameraManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    RefreshCachedComponents();
    if (!OwnerCharacter)
    {
        PendingLookInput = FVector2D::ZeroVector;
        return;
    }

    ValidateLockTarget();
    CameraMode = ResolveCameraMode();
    UpdateMovementRotationSettings();
    UpdateMoveDirectionAngle();

    if (CameraMode == ESKCameraMode::SprintAlign)
    {
        UpdateSprintAlignMode(DeltaTime);
    }
    else if (CameraMode == ESKCameraMode::LockOn)
    {
        UpdateLockOnMode(DeltaTime);
    }
    else
    {
        UpdateFreeMode(DeltaTime);
    }

    PendingLookInput = FVector2D::ZeroVector;
}

void USKCameraManagerComponent::RefreshCachedComponents()
{
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(GetOwner());
    }
    if (!OwnerCharacter) return;

    if (!InputManager)
    {
        InputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
    }
    if (!MovementComponent)
    {
        MovementComponent = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
    }
}

void USKCameraManagerComponent::ValidateLockTarget()
{
    if (!LockTarget) return;

    if (!IsValid(LockTarget))
    {
        ClearLockTarget();
        return;
    }

    if (!OwnerCharacter) return;

    const float BreakDistance = MaxLockOnRange * LockOnBreakDistanceMultiplier;
    const float BreakDistanceSq = FMath::Square(FMath::Max(1.f, BreakDistance));
    const float TargetDistanceSq = FVector::DistSquared(OwnerCharacter->GetActorLocation(), LockTarget->GetActorLocation());
    if (TargetDistanceSq > BreakDistanceSq)
    {
        ClearLockTarget();
    }
}

ESKCameraMode USKCameraManagerComponent::ResolveCameraMode() const
{
    if (MovementComponent && MovementComponent->CurrentMovementTier == ESKMovementTier::Sprint)
    {
        return ESKCameraMode::SprintAlign;
    }

    if (IsLockedOn())
    {
        return ESKCameraMode::LockOn;
    }

    return ESKCameraMode::Free;
}

void USKCameraManagerComponent::UpdateMovementRotationSettings()
{
    if (!OwnerCharacter) return;

    UCharacterMovementComponent* CharacterMovement = OwnerCharacter->GetCharacterMovement();
    if (!CharacterMovement) return;

    if (CameraMode == ESKCameraMode::LockOn)
    {
        CharacterMovement->bOrientRotationToMovement = false;
        CharacterMovement->bUseControllerDesiredRotation = true;
    }
    else if (CameraMode == ESKCameraMode::SprintAlign)
    {
        CharacterMovement->bOrientRotationToMovement = false;
        CharacterMovement->bUseControllerDesiredRotation = false;
    }
    else
    {
        CharacterMovement->bOrientRotationToMovement = true;
        CharacterMovement->bUseControllerDesiredRotation = false;
    }
}

void USKCameraManagerComponent::UpdateMoveDirectionAngle()
{
    if (!OwnerCharacter)
    {
        MoveDirectionAngle = 0.f;
        return;
    }

    float TargetYaw = 0.f;
    if (GetDesiredMoveYaw(TargetYaw))
    {
        MoveDirectionAngle = FMath::FindDeltaAngleDegrees(OwnerCharacter->GetActorRotation().Yaw, TargetYaw);
        return;
    }

    const FVector Velocity = OwnerCharacter->GetVelocity();
    if (!Velocity.IsNearlyZero())
    {
        const float VelocityYaw = Velocity.Rotation().Yaw;
        MoveDirectionAngle = FMath::FindDeltaAngleDegrees(OwnerCharacter->GetActorRotation().Yaw, VelocityYaw);
        return;
    }

    MoveDirectionAngle = 0.f;
}

void USKCameraManagerComponent::UpdateFreeMode(float DeltaTime)
{
    ApplyPendingLookInput();
}

void USKCameraManagerComponent::UpdateSprintAlignMode(float DeltaTime)
{
    float TargetYaw = 0.f;
    if (!GetDesiredMoveYaw(TargetYaw))
    {
        TargetYaw = OwnerCharacter->GetActorRotation().Yaw;
    }

    ApplyActorYaw(TargetYaw, SprintActorInterpSpeed, DeltaTime);

    float CameraTargetYaw = 0.f;
    if (IsLockedOn() && GetLockTargetYaw(CameraTargetYaw))
    {
        ApplyControllerYaw(CameraTargetYaw, LockOnCameraYawInterpSpeed, DeltaTime);
        return;
    }

    ApplyControllerYaw(OwnerCharacter->GetActorRotation().Yaw, SprintCameraYawInterpSpeed, DeltaTime);
}

void USKCameraManagerComponent::UpdateLockOnMode(float DeltaTime)
{
    float TargetYaw = 0.f;
    if (!GetLockTargetYaw(TargetYaw)) return;

    ApplyActorYaw(TargetYaw, LockOnActorInterpSpeed, DeltaTime);
    ApplyControllerYaw(TargetYaw, LockOnCameraYawInterpSpeed, DeltaTime);
}

bool USKCameraManagerComponent::GetDesiredMoveYaw(float& OutYaw) const
{
    if (!OwnerCharacter || !InputManager) return false;

    const FVector2D MoveIntent = InputManager->GetMoveIntent();
    if (MoveIntent.Size() < MinMoveInputForFacing) return false;

    const AController* Controller = OwnerCharacter->GetController();
    if (!Controller) return false;

    const FRotator ControlYawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);
    const FVector DesiredDirection = (Forward * MoveIntent.Y + Right * MoveIntent.X).GetSafeNormal();
    if (DesiredDirection.IsNearlyZero()) return false;

    OutYaw = DesiredDirection.Rotation().Yaw;
    return true;
}

bool USKCameraManagerComponent::GetLockTargetYaw(float& OutYaw) const
{
    if (!OwnerCharacter || !IsLockedOn()) return false;

    const FVector ToTarget = LockTarget->GetActorLocation() - OwnerCharacter->GetActorLocation();
    const FVector FlatDirection(ToTarget.X, ToTarget.Y, 0.f);
    if (FlatDirection.IsNearlyZero()) return false;

    OutYaw = FlatDirection.Rotation().Yaw;
    return true;
}

void USKCameraManagerComponent::ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    if (!OwnerCharacter) return;

    const FRotator CurrentRotation = OwnerCharacter->GetActorRotation();
    const float NewYaw = FMath::FInterpTo(CurrentRotation.Yaw, TargetYaw, DeltaTime, InterpSpeed);
    OwnerCharacter->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

void USKCameraManagerComponent::ApplyControllerYaw(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    if (!OwnerCharacter) return;

    AController* Controller = OwnerCharacter->GetController();
    if (!Controller) return;

    const FRotator CurrentRotation = Controller->GetControlRotation();
    const float NewYaw = FMath::FInterpTo(CurrentRotation.Yaw, TargetYaw, DeltaTime, InterpSpeed);
    Controller->SetControlRotation(FRotator(CurrentRotation.Pitch, NewYaw, CurrentRotation.Roll));
}

void USKCameraManagerComponent::ApplyPendingLookInput()
{
    if (!OwnerCharacter || PendingLookInput.IsNearlyZero()) return;

    OwnerCharacter->AddControllerYawInput(PendingLookInput.X);
    OwnerCharacter->AddControllerPitchInput(PendingLookInput.Y);
}
