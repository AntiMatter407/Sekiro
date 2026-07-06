#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SekiroAnimBlueprintInstance.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroAnimBlueprintInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    USekiroAnimBlueprintInstance();
};
