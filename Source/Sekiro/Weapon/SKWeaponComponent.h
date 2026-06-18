// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SKWeaponComponent.generated.h"

class ASKWeapon;
class ACharacter;

// ============================================================================
// USKWeaponComponent — 武器管理组件
//     挂在角色上，负责生成武器Actor并挂载到指定骨骼
//     蓝图子类设置 DefaultWeaponClass + WeaponAttachSocket + WeaponAttachBoneFallback
// ============================================================================

UCLASS(ClassGroup=(Weapon), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USKWeaponComponent();

	// ── 配置 ──────────────────────────────────────────────

	/** 默认武器蓝图类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<ASKWeapon> DefaultWeaponClass;

	/** 武器挂载 Socket */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName WeaponAttachSocket = TEXT("R_Weapon");

	/** Socket 不存在时的回退骨骼名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName WeaponAttachBoneFallback = TEXT("hand_r");

	// ── 运行时 ────────────────────────────────────────────

	/** 当前装备的武器实例 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<ASKWeapon> CurrentWeapon;

	/** 生成并挂载武器到Owner角色 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitWeapon();
};
