// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SKPlayerController.generated.h"

UCLASS(config=Game)
class SEKIRO_API ASKPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASKPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
};
