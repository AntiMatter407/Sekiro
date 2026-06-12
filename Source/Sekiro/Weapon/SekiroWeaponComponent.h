// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SekiroWeaponComponent.generated.h"

class ASekiroWeapon;
class ACharacter;

// ============================================================================
// USekiroWeaponComponent — 武器管理组件
//     挂在角色上，负责生成武器Actor并挂载到指定骨骼
//     蓝图子类设置 DefaultWeaponClass + AttachSocketName
// ============================================================================

UCLASS(ClassGroup=(Weapon), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USekiroWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USekiroWeaponComponent();

	// ── 配置 ──────────────────────────────────────────────

	/** 默认武器蓝图类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<ASekiroWeapon> DefaultWeaponClass;

	/** 挂载到的骨骼名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName AttachSocketName = TEXT("R_Weapon");

	// ── 运行时 ────────────────────────────────────────────

	/** 当前装备的武器实例 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<ASekiroWeapon> CurrentWeapon;

	/** 生成并挂载武器到Owner角色 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitWeapon();
};
