#include "Movement/SekiroMovementComponent.h"

USekiroMovementComponent::USekiroMovementComponent()
{
}

float USekiroMovementComponent::GetMaxSpeed() const
{
	switch (CurrentMovementTier)
	{
	case ESekiroMovementTier::Walk:   return WalkSpeed;
	case ESekiroMovementTier::Jog:    return JogSpeed;
	case ESekiroMovementTier::Run:    return RunSpeed;
	case ESekiroMovementTier::Sprint: return SprintSpeed;
	case ESekiroMovementTier::Crouch: return MaxWalkSpeedCrouched;
	default:                          return RunSpeed;
	}
}
