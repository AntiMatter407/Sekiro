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
		UE_LOG(LogTemp, Warning, TEXT("USKWeaponComponent::InitWeapon — DefaultWeaponClass 未设置，请在蓝图侧配置"));
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
		USkeletalMeshComponent* Mesh = Owner->GetMesh();
		if (!Mesh) return;

		// 优先使用 WeaponAttachSocket，不存在则回退到 WeaponAttachBoneFallback（骨骼名称）
		// DoesSocketExist 只检查 Socket 不检查骨骼名，骨骼名直接传 AttachToComponent 也可用
		FName TargetSocket = WeaponAttachSocket;
		if (!Mesh->DoesSocketExist(WeaponAttachSocket))
		{
			TargetSocket = WeaponAttachBoneFallback;
		}

		CurrentWeapon->AttachToComponent(
			Mesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			TargetSocket
		);
	}
}
