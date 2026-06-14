// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SKWeapon.generated.h"

// ============================================================================
// ASKWeapon — 所有武器的C++基类
//     蓝图子类设置 SkeletalMesh + AttachSocketName
//     角色 PossessedBy 时自动生成并挂载到指定骨骼
// ============================================================================

UCLASS(Blueprintable, BlueprintType)
class SEKIRO_API ASKWeapon : public AActor
{
	GENERATED_BODY()

public:
	ASKWeapon();

	// ── 组件 ──────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	// ── 配置（蓝图子类中设置） ──────────────────────────────

	/** 挂载到的角色骨骼Socket名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName AttachSocketName = TEXT("R_Weapon");
};
