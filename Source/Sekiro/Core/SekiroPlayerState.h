// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SekiroPlayerState.generated.h"

UCLASS()
class SEKIRO_API ASekiroPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ASekiroPlayerState();

protected:
	virtual void BeginPlay() override;
};
