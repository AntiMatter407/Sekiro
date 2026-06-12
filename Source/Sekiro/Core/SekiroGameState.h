// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "SekiroGameState.generated.h"

UCLASS()
class SEKIRO_API ASekiroGameState : public AGameState
{
	GENERATED_BODY()

public:
	ASekiroGameState();

protected:
	virtual void BeginPlay() override;
};
