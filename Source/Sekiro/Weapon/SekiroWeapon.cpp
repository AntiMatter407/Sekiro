// Copyright Epic Games, Inc. All Rights Reserved.

#include "SekiroWeapon.h"

ASekiroWeapon::ASekiroWeapon()
{
	PrimaryActorTick.bCanEverTick = false;

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
}
