// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SekiroPlayerController.generated.h"

UCLASS(config=Game)
class SEKIRO_API ASekiroPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASekiroPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
};
