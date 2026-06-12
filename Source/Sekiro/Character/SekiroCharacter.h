// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "SekiroCharacter.generated.h"

// ============================================================================
// ASekiroCharacter — 只狼玩家角色
// 　　移动系统：行走/奔跑/冲刺/蹲下/闪避方向
// 　　战斗/交互：桩函数，子类覆盖实现
// ============================================================================

class UInputMappingContext;
class UInputAction;
class USpringArmComponent;
class UCameraComponent;
class USekiroAnimInstance;
class USekiroWeaponComponent;

UCLASS(config=Game)
class SEKIRO_API ASekiroCharacter : public ACharacter
{
	GENERATED_BODY()

	friend class USekiroAnimInstance;

	// ── 组件 ──────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USekiroWeaponComponent> WeaponComponent;

	// ── InputMappingContext ───────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	// ── InputAction 引用 ──────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> AttackAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> GuardAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> DodgeAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> UseItemAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> GrappleAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ProstheticAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LockOnAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> HealingGourdAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CycleItemNextAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CycleItemPrevAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> PauseAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MenuAction;

	// ── 移动状态 ──────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	uint32 bIsDodging : 1;

	/**
	 * 闪避方向（-1=后, 0=无/原地, 1=前）
	 * 当前帧由 Move() 沿输入方向计算后写入；
	 * 战斗系统调用 DodgePressed/DodgeReleased 时可利用该值选择动画/行为。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	float DodgeDirection = 0.f;

	/**
	 * 闪避横向方向（-1=左, 0=无, +1=右）
	 * 与 DodgeDirection 同时由 Move() 计算；
	 * 动画蓝图使用该值选择左/右闪避动画。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State", meta = (AllowPrivateAccess = "true"))
	float DodgeDirectionLateral = 0.f;

	/** 允许空中闪避（忍具派生等场景） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dodge", meta = (AllowPrivateAccess = "true"))
	uint32 bAllowAirDodge : 1;

	// ── 视角灵敏度 ────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity", meta = (AllowPrivateAccess = "true"))
	float LookSensitivityYaw = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity", meta = (AllowPrivateAccess = "true"))
	float LookSensitivityPitch = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity", meta = (AllowPrivateAccess = "true"))
	uint32 bInvertPitch : 1;

public:
	ASekiroCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ── 移动 ──────────────────────────────────────────────

	/** 主移动输入（Axis2D）→ 方向 + 速度调节 */
	void Move(const FInputActionValue& Value);

	/** 视角输入（Axis2D）→ 镜头旋转 */
	void Look(const FInputActionValue& Value);

	/** 冲刺开始（按住闪避键 + 有移动输入时） */
	void SprintPressed();
	void SprintReleased();

	/** 蹲下（切换） */
	void CrouchToggle();

	/** 闪避按下 → 触发垫步 */
	void DodgePressed();
	/** 闪避松开 → 结束垫步状态 */
	void DodgeReleased();

	// ── 战斗（桩）──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void AttackPressed();
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void AttackReleased();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void GuardPressed();
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void GuardReleased();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LockOnPressed();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ProstheticPressed();
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ProstheticReleased();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void GrapplePressed();

	// ── 交互（桩）──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void InteractPressed();

	UFUNCTION(BlueprintCallable, Category = "Item")
	void UseItemPressed();

	UFUNCTION(BlueprintCallable, Category = "Item")
	void HealingGourdPressed();

	UFUNCTION(BlueprintCallable, Category = "Item")
	void CycleItemNext();
	UFUNCTION(BlueprintCallable, Category = "Item")
	void CycleItemPrev();

	// ── 系统（桩）──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "System")
	void PausePressed();
	UFUNCTION(BlueprintCallable, Category = "System")
	void MenuPressed();

public:
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};
