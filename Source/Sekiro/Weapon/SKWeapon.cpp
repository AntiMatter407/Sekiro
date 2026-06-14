// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKWeapon.h"

ASKWeapon::ASKWeapon()
{
	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
}
