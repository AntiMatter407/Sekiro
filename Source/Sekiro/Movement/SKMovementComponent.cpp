#include "Movement/SKMovementComponent.h"

USKMovementComponent::USKMovementComponent()
{
}

float USKMovementComponent::GetMaxSpeed() const
{
	switch (CurrentMovementTier)
	{
	case ESKMovementTier::Walk:   return WalkSpeed;
	case ESKMovementTier::Jog:    return JogSpeed;
	case ESKMovementTier::Run:    return RunSpeed;
	case ESKMovementTier::Sprint: return SprintSpeed;
	case ESKMovementTier::Crouch: return MaxWalkSpeedCrouched;
	default:                          return RunSpeed;
	}
}
