// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKWeaponComponent.h"
#include "SKWeapon.h"
#include "GameFramework/Character.h"

USKWeaponComponent::USKWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USKWeaponComponent::InitWeapon()
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

	CurrentWeapon = GetWorld()->SpawnActor<ASKWeapon>(DefaultWeaponClass, SpawnParams);
	if (CurrentWeapon)
	{
		CurrentWeapon->AttachToComponent(
			Owner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			AttachSocketName
		);
	}
}
