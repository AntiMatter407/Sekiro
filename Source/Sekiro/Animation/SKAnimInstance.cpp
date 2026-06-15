#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

int32 USKAnimInstance::GetActionPriority(FName Action)
{
	static const TMap<FName, int32> PriorityMap = {
		{TEXT("Deathblow"),     10},
		{TEXT("Resurrection"),  10},
		{TEXT("Death"),          9},
		{TEXT("Hit"),            8},
		{TEXT("Dodge"),          7},
		{TEXT("Deflect"),        6},
		{TEXT("Guard"),          5},
		{TEXT("Prosthetic"),     4},
		{TEXT("ItemUse"),        3},
		{TEXT("Attack"),         2},
		{TEXT("Quickstep"),      1},
	};
	const int32* P = PriorityMap.Find(Action);
	return P ? *P : 0;
}

void USKAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
	if (OwnerCharacter)
	{
		OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
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
		}
	}

	if (!OwnerCharacter)
	{
		return;
	}

	// Locomotion
	bIsInAir = OwnerCharacter->GetCharacterMovement()->IsFalling();
	bIsCrouching = OwnerCharacter->bIsCrouched;

	if (OwnerMovement)
	{
		MovementTier = OwnerMovement->CurrentMovementTier;
	}

	Speed = OwnerCharacter->GetVelocity().Size2D();
	Angle = UKismetAnimationLibrary::CalculateDirection(OwnerCharacter->GetVelocity(), OwnerCharacter->GetActorRotation());

	// Dodge state
	bIsDodging = OwnerCharacter->bIsDodging;
	DodgeDirection = OwnerCharacter->DodgeDirection;
	DodgeDirectionLateral = OwnerCharacter->DodgeDirectionLateral;

	// TAE: 更新当前动画时间
	if (UAnimMontage* Montage = GetCurrentActiveMontage())
	{
		CurrentAnimTime = Montage_GetPosition(Montage);
		CurrentAnimLength = Montage->GetPlayLength();
	}
}

bool USKAnimInstance::CanCancelTo(FName TargetAction, float& OutCrossfade) const
{
	if (!AnimLogicData) return false;

	int32 CurrentPrio = GetActionPriority(CurrentAction);
	int32 TargetPrio = GetActionPriority(TargetAction);
	if (TargetPrio <= CurrentPrio && CurrentAction != NAME_None)
		return false;

	return AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, TargetAction, OutCrossfade);
}

bool USKAnimInstance::GetCurrentHitbox(FSKAttackHitboxConfig& OutConfig) const
{
	if (!AnimLogicData) return false;

	int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.f);
	return AnimLogicData->GetAttackHitboxAtFrame(CurrentAnimID, Frame, OutConfig);
}
