// Copyright Epic Games, Inc. All Rights Reserved.

#include "SekiroWeaponComponent.h"
#include "SekiroWeapon.h"
#include "GameFramework/Character.h"

USekiroWeaponComponent::USekiroWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USekiroWeaponComponent::InitWeapon()
{
	if (!DefaultWeaponClass)
	{
		return;
	}

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CurrentWeapon = GetWorld()->SpawnActor<ASekiroWeapon>(DefaultWeaponClass, SpawnParams);
	if (CurrentWeapon)
	{
		CurrentWeapon->AttachToComponent(
			Owner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			AttachSocketName
		);
	}
}
