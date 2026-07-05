#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/SKAnimDataTypes.h"
#include "SKAnimBlueprintLibrary.generated.h"

UCLASS()
class SEKIRO_API USKAnimBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Locomotion", meta = (DisplayName = "Get Locomotion Direction From Angle"))
	static ESKLocomotionDirection GetLocomotionDirectionFromAngle(float Angle, float ForwardAngleRange = 35.f, float BackwardAngleRange = 135.f);
};
