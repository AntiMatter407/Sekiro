#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "SekiroAnimLogicData.h"

void USKAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
    if (OwnerCharacter)
    {
        OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
        AnimController = OwnerCharacter->FindComponentByClass<USKAnimationController>();
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
            AnimController = OwnerCharacter->FindComponentByClass<USKAnimationController>();
        }
    }
    if (!OwnerCharacter) return;

    // ── Locomotion ────────────────────────────────────────────
    bIsInAir = OwnerCharacter->GetCharacterMovement()->IsFalling();
    bIsCrouching = OwnerCharacter->bIsCrouched;

    if (OwnerMovement)
    {
        MovementTier = OwnerMovement->CurrentMovementTier;
    }

    Speed = OwnerCharacter->GetVelocity().Size2D();
    Angle = UKismetAnimationLibrary::CalculateDirection(
        OwnerCharacter->GetVelocity(), OwnerCharacter->GetActorRotation());

    // ── Dodge ─────────────────────────────────────────────────
    bIsDodging = OwnerCharacter->bIsDodging;
    DodgeDirection = OwnerCharacter->DodgeDirection;
    DodgeDirectionLateral = OwnerCharacter->DodgeDirectionLateral;

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
}
