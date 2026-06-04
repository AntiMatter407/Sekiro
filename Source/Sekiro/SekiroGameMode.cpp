// Copyright Epic Games, Inc. All Rights Reserved.

#include "SekiroGameMode.h"
#include "SekiroCharacter.h"
#include "UObject/ConstructorHelpers.h"

ASekiroGameMode::ASekiroGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
