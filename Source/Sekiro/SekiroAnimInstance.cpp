#include "SekiroAnimInstance.h"
#include "SekiroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

void USekiroAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	SekiroCharacter = Cast<ASekiroCharacter>(TryGetPawnOwner());
}

void USekiroAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!SekiroCharacter)
	{
		SekiroCharacter = Cast<ASekiroCharacter>(TryGetPawnOwner());
	}

	if (!SekiroCharacter)
	{
		return;
	}

	// Locomotion
	bIsInAir = SekiroCharacter->GetCharacterMovement()->IsFalling();
	bIsCrouching = SekiroCharacter->bIsCrouched;

	// Speed & Angle in local space
	Speed = SekiroCharacter->GetVelocity().Size2D();
	Angle = UKismetAnimationLibrary::CalculateDirection(SekiroCharacter->GetVelocity(), SekiroCharacter->GetActorRotation());

	// Dodge state
	bIsDodging = SekiroCharacter->bIsDodging;
	DodgeDirection = SekiroCharacter->DodgeDirection;
	DodgeDirectionLateral = SekiroCharacter->DodgeDirectionLateral;
}
