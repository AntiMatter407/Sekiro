// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SKPlayerState.generated.h"

UCLASS()
class SEKIRO_API ASKPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ASKPlayerState();

protected:
	virtual void BeginPlay() override;
};
