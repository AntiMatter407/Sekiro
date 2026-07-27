#pragma once

#include "CoreMinimal.h"
#include "Character/SKCharacter.h"
#include "SKAICharacter.generated.h"

UCLASS(config = Game, Blueprintable)
class SEKIRO_API ASKAICharacter : public ASKCharacter
{
	GENERATED_BODY()

public:
	ASKAICharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
