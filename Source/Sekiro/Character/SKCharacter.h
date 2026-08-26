// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "SKCharacter.generated.h"

// ============================================================================
// ASKCharacter — 只狼玩家角色
//     输入状态委托给 USKInputManager 组件
//     速度与角色朝向策略委托给 USKMovementComponent 的 Lua 模块
//     动画参数由 USKAnimInstance 读取并交给 AnimBlueprint 编排
// ============================================================================

class USpringArmComponent;
class UCameraComponent;
class USKWeaponManagerComponent;
class USKInputManager;
class USKCameraManagerComponent;
class USKLockOnIndicatorComponent;
class USKCombatComponent;
class USKMovementComponent;
class USKAbilitySystemComponent;
class USKCharacterAttributeSet;
class USKSurvivalComponent;

UCLASS(config=Game)
class SEKIRO_API ASKCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

	friend class USKAnimInstance;

public:
	ASKCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    UFUNCTION(BlueprintPure, Category = "Survival")
    USKSurvivalComponent* GetSurvivalComponent() const;

    // ── GAS 数值基础 ──────────────────────────────────────
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    UFUNCTION(BlueprintPure, Category = "GAS")
    USKAbilitySystemComponent* GetSKAbilitySystemComponent() const;

    UFUNCTION(BlueprintPure, Category = "GAS")
    FString GetAttributeConfigModule() const;

	// ── 组件访问 ──────────────────────────────────────────

	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	FORCEINLINE USKInputManager* GetInputManager() const { return InputManager; }

	/**
	 * 返回角色持有的战斗动作宿主；不转移所有权。
	 * 该内联接口同时供原生代码、蓝图和 Lua 行为树任务读取，角色构造完成后应始终非空。
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	FORCEINLINE USKCombatComponent* GetCombatComponent() const { return CombatComponent; }

	FORCEINLINE USKCameraManagerComponent* GetCameraManager() const { return CameraManager; }
	FORCEINLINE USKLockOnIndicatorComponent* GetLockOnIndicator() const { return LockOnIndicator; }
	/** 返回角色持有的原生武器管理组件；不转移所有权，角色构造完成后应始终非空。 */
	FORCEINLINE USKWeaponManagerComponent* GetWeaponManager() const { return WeaponManager; }

	// ── 闪避状态接口（由 USKInputManager 调用）────────────

	bool IsDodging() const { return bIsDodging; }        // 闪避状态查询
	bool CanAirDodge() const { return bAllowAirDodge; }    // 空中闪避许可
	void SetDodging(bool bActive);                        // 设置闪避激活状态
	void SetDodgeDirection(float Fwd, float Lateral);     // 设置闪避方向

protected:
	struct FSKMovementComponentOverrideTag
	{
	};

	/** 供派生角色保留其已配置移动组件类型的构造入口。 */
	ASKCharacter(const FObjectInitializer& ObjectInitializer, FSKMovementComponentOverrideTag OverrideTag);

    virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ── 组件 ──────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Survival")
    TObjectPtr<USKSurvivalComponent> SurvivalComponent; // 唯一生命与躯干流程组件

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
    TObjectPtr<USKAbilitySystemComponent> AbilitySystemComponent; // 角色自身作为 Owner 与 Avatar 的 ASC

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
    TObjectPtr<USKCharacterAttributeSet> CharacterAttributes; // 生命、战斗和躯干的唯一属性存储

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS", meta = (ExposeOnSpawn = "true"))
    FString AttributeConfigModule; // 统一角色属性的 Lua 模块；空值由 Lua 按主角/AI 类型选择，显式配置错误不回退

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;           // 相机摇臂

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;            // 跟随相机

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USKWeaponManagerComponent> WeaponManager; // 武器管理组件

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USKInputManager> InputManager;             // 输入处理组件

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USKCombatComponent> CombatComponent;       // 战斗动画动作宿主

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USKCameraManagerComponent> CameraManager;  // 摄像机与朝向管理组件

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USKLockOnIndicatorComponent> LockOnIndicator; // 锁定目标 UI 组件

	// ── 闪避状态（供 USKAnimInstance 查询）───────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	uint32 bIsDodging : 1;                                // 是否正在闪避

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	float DodgeDirection = 0.f;                           // 闪避前后方向（-1=后, 1=前）

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	float DodgeDirectionLateral = 0.f;                    // 闪避横向方向（-1=左, 1=右）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dodge", meta = (AllowPrivateAccess = "true"))
	uint32 bAllowAirDodge : 1;                            // 允许空中闪避

};
