// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "SKInputHandler.generated.h"

// ============================================================================
// USKInputHandler — 输入处理组件
//     接收 Enhanced Input 事件，转换为动作意图
//     供 USKAnimationController / 战斗系统 / 交互系统消费
// ============================================================================

class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class ACharacter;
class APlayerController;

UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKInputHandler : public UActorComponent
{
	GENERATED_BODY()

public:
	USKInputHandler();

	// ── 初始化 ──────────────────────────────────────────────

	/** 绑定所有 InputAction 到内部回调 */
	void SetupInput(UEnhancedInputComponent* Input);

	/** 添加 MappingContext 到 Enhanced Input 子系统 */
	void AddMappingContext(APlayerController* PC);

	// ── 消费型意图（读取后自动清零）─────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeAttackPressed();                     // 攻击按下

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeJumpPressed();                       // 跳跃按下

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeDodgePressed();                      // 闪避按下

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeInteractPressed();                   // 交互按下

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeUseItemPressed();                    // 道具使用

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeHealingGourdPressed();               // 伤药葫芦

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeGrapplePressed();                    // 钩索

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeProstheticPressed();                 // 义手忍具

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeLockOnPressed();                     // 锁定

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeCrouchToggled();                     // 蹲下切换

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeCycleItemNext();                     // 切换道具下一个

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeCycleItemPrev();                     // 切换道具上一个

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumePausePressed();                      // 暂停

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeMenuPressed();                       // 菜单

	// ── 持续型意图 ──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Input")
	FVector2D GetMoveIntent() const;                 // 移动方向（归一化）

	UFUNCTION(BlueprintCallable, Category = "Input")
	FVector2D GetLookIntent() const;                 // 视角方向

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsAttackHeld() const;                       // 攻击键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsGuardHeld() const;                        // 防御键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsDodgeHeld() const;                        // 闪避键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetAttackHoldTime() const;                 // 攻击长按时间（秒）

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetProstheticHoldTime() const;             // 义手长按时间（秒）

	// ── 连段 ────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Input")
	int32 GetComboIndex() const;                     // 当前连段序号

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetTimeSinceLastAttack() const;            // 距上次攻击时间

	// ── 视角配置（从 ASKCharacter 迁移）─────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
	float LookSensitivityYaw = 1.0f;                  // 视角 Yaw 灵敏度

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
	float LookSensitivityPitch = 1.0f;                // 视角 Pitch 灵敏度

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
	bool bInvertPitch = false;                        // Pitch 反转

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── 移动/视角回调 ───────────────────────────────────────

	void OnMove(const FInputActionValue& Value);     // 主移动输入
	void OnLook(const FInputActionValue& Value);     // 视角输入

	// ── 跳跃回调 ────────────────────────────────────────────

	void OnJumpStarted(const FInputActionValue& Value);   // 跳跃按下 → ACharacter::Jump
	void OnJumpCompleted(const FInputActionValue& Value); // 跳跃松开 → ACharacter::StopJumping

	// ── 闪避/冲刺回调 ──────────────────────────────────────

	void OnDodgeStarted(const FInputActionValue& Value);   // 闪避按下 → 垫步 + 冲刺
	void OnDodgeCompleted(const FInputActionValue& Value); // 闪避松开

	// ── 蹲下回调 ────────────────────────────────────────────

	void OnCrouchStarted(const FInputActionValue& Value);  // 蹲下切换

	// ── 战斗回调 ────────────────────────────────────────────

	void OnAttackStarted(const FInputActionValue& Value);   // 攻击按下
	void OnAttackCompleted(const FInputActionValue& Value); // 攻击松开
	void OnGuardStarted(const FInputActionValue& Value);    // 防御按下
	void OnGuardCompleted(const FInputActionValue& Value);  // 防御松开
	void OnLockOnStarted(const FInputActionValue& Value);   // 锁定按下
	void OnProstheticStarted(const FInputActionValue& Value);   // 义手按下
	void OnProstheticCompleted(const FInputActionValue& Value); // 义手松开
	void OnGrappleStarted(const FInputActionValue& Value);  // 钩索按下

	// ── 交互/道具回调 ──────────────────────────────────────

	void OnInteractStarted(const FInputActionValue& Value);      // 交互按下
	void OnUseItemStarted(const FInputActionValue& Value);       // 道具使用
	void OnHealingGourdStarted(const FInputActionValue& Value);  // 伤药葫芦
	void OnCycleItemNextStarted(const FInputActionValue& Value); // 切换道具下一个
	void OnCycleItemPrevStarted(const FInputActionValue& Value); // 切换道具上一个

	// ── 系统回调 ────────────────────────────────────────────

	void OnPauseStarted(const FInputActionValue& Value); // 暂停
	void OnMenuStarted(const FInputActionValue& Value);  // 菜单

private:
	// ── InputAction 引用（17 个，从 ASKCharacter 迁移）─────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;                 // 移动

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookAction;                 // 视角

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> JumpAction;                 // 跳跃

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> AttackAction;               // 攻击

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> GuardAction;                // 防御

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> DodgeAction;                // 闪避

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> InteractAction;             // 交互

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> UseItemAction;              // 道具使用

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> GrappleAction;              // 钩索

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ProstheticAction;           // 义手忍具

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LockOnAction;               // 锁定

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CrouchAction;               // 蹲下

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> HealingGourdAction;         // 伤药葫芦

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CycleItemNextAction;        // 切换道具下一个

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CycleItemPrevAction;        // 切换道具上一个

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> PauseAction;                // 暂停

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MenuAction;                 // 菜单

	// ── InputMappingContext ────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext; // 默认映射上下文

	// ── 消费型意图标记（帧末清零）───────────────────────────

	bool bAttackPressed = false;                     // 攻击按下标记
	bool bJumpPressed = false;                       // 跳跃按下标记
	bool bDodgePressed = false;                      // 闪避按下标记
	bool bInteractPressed = false;                   // 交互按下标记
	bool bUseItemPressed = false;                    // 道具使用标记
	bool bHealingGourdPressed = false;               // 伤药葫芦标记
	bool bGrapplePressed = false;                    // 钩索标记
	bool bProstheticPressed = false;                 // 义手标记
	bool bLockOnPressed = false;                     // 锁定标记
	bool bCrouchToggled = false;                     // 蹲下切换标记
	bool bCycleItemNext = false;                     // 下一道具标记
	bool bCycleItemPrev = false;                     // 上一道具标记
	bool bPausePressed = false;                      // 暂停标记
	bool bMenuPressed = false;                       // 菜单标记

	// ── 持续型意图 ──────────────────────────────────────────

	FVector2D MoveIntent = FVector2D::ZeroVector;    // 移动方向（归一化，X=右, Y=前）
	FVector2D LookIntent = FVector2D::ZeroVector;    // 视角方向（原始值）
	bool bAttackHeld = false;                        // 攻击键按住
	bool bGuardHeld = false;                         // 防御键按住
	bool bDodgeHeld = false;                         // 闪避键按住（冲刺用）

	// ── 长按计时 ────────────────────────────────────────────

	float AttackHoldTime = 0.f;                      // 攻击长按累计时间（秒，> 0.3s 视为蓄力）
	float ProstheticHoldTime = 0.f;                  // 义手长按累计时间（秒）

	// ── 连段 ────────────────────────────────────────────────

	int32 ComboIndex = 0;                            // 当前连段序号
	float TimeSinceLastAttack = 0.f;                 // 距上次攻击时间（秒，> 0.5s 超时复位）

	// ── 角色引用 ────────────────────────────────────────────

	TWeakObjectPtr<ACharacter> OwnerCharacter;       // 所有者角色缓存
};
