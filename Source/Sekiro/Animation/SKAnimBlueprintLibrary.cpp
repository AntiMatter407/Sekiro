#include "Animation/SKAnimBlueprintLibrary.h"

ESKLocomotionDirection USKAnimBlueprintLibrary::GetLocomotionDirectionFromAngle(float Angle, float ForwardAngleRange, float BackwardAngleRange)
{
	const float NormalizedAngle = FMath::FindDeltaAngleDegrees(0.f, Angle);
	const float AbsAngle = FMath::Abs(NormalizedAngle);
	const float ForwardRange = FMath::Clamp(FMath::Abs(ForwardAngleRange), 0.f, 179.f);
	const float BackwardRange = FMath::Clamp(FMath::Abs(BackwardAngleRange), ForwardRange + 1.f, 180.f);

	if (AbsAngle <= ForwardRange) return ESKLocomotionDirection::Fwd;
	if (AbsAngle >= BackwardRange) return ESKLocomotionDirection::Bwd;
	return NormalizedAngle >= 0.f ? ESKLocomotionDirection::R : ESKLocomotionDirection::L;
}
