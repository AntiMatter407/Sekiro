// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "SKGameState.generated.h"

UCLASS()
class SEKIRO_API ASKGameState : public AGameState
{
	GENERATED_BODY()

public:
	ASKGameState();

protected:
	virtual void BeginPlay() override;
};
