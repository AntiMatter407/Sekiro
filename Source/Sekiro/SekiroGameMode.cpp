// Copyright Epic Games, Inc. All Rights Reserved.

#include "SekiroGameMode.h"
#include "SekiroCharacter.h"
#include "UObject/ConstructorHelpers.h"

ASekiroGameMode::ASekiroGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/Gameplay/BP_SekiroCharacter"));
	if (PlayerPawnBPClass.Class)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
