// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "Movement/SKMovementComponent.h"
#include "SKInputManager.generated.h"

// ============================================================================
// USKInputManager — 输入管理组件
//     接收 Enhanced Input 事件，转换为动作意图
//     供 AnimBlueprint / 战斗系统 / 交互系统消费
// ============================================================================

class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class ACharacter;
class APlayerController;

// ── 输入缓冲条目 ────────────────────────────────────────────
USTRUCT(BlueprintType)
struct SEKIRO_API FSKBufferedInput
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FName Action;                               // 动作名称（"Attack", "Guard", "Dodge" 等）

    UPROPERTY(BlueprintReadOnly)
    int32 Priority = 0;                         // 对应 ESKActionPriority

    UPROPERTY(BlueprintReadOnly)
    float Timestamp = 0.f;                      // 世界时间戳（入队时间）

    UPROPERTY(BlueprintReadOnly)
    float Lifetime = 0.1f;                      // 最大缓冲寿命（秒，默认 0.1s ≈ 6帧@60fps）
};

UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKInputManager : public UActorComponent
{
	GENERATED_BODY()

public:
	USKInputManager();

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

	// ── 输入缓冲 ────────────────────────────────────────────

	/** 获取缓冲队列供消费方读取 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	const TArray<FSKBufferedInput>& GetInputBuffer() const { return InputBuffer; }

	/** 消费缓冲中指定 Action 的最高优先级条目（移除并返回是否存在） */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ConsumeBufferedInput(FName Action);

	/** 清空整个缓冲队列 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void ClearInputBuffer();

	// ── 持续型意图 ──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Input")
	FVector2D GetMoveIntent() const;                 // 移动方向（归一化）

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetMoveInputAmount() const;                // 移动输入强度（0-1，保留摇杆轻推幅度）

	UFUNCTION(BlueprintCallable, Category = "Input")
	FVector2D GetLookIntent() const;                 // 视角方向

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsAttackHeld() const;                       // 攻击键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsGuardHeld() const;                        // 防御键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsDodgeHeld() const;                        // 闪避键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsDodgeActive() const;                      // 闪避动作窗口是否有效

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool IsWalkHeld() const;                         // 步行修饰键按住

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetAttackHoldTime() const;                 // 攻击长按时间（秒）

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetProstheticHoldTime() const;             // 义手长按时间（秒）

	// ── 连段 ────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Input")
	int32 GetComboIndex() const;                     // 当前连段序号

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetTimeSinceLastAttack() const;            // 距上次攻击时间

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── 移动/视角回调 ───────────────────────────────────────

	UFUNCTION()
	void OnMove(const FInputActionValue& Value);     // 主移动输入
	UFUNCTION()
	void OnLook(const FInputActionValue& Value);     // 视角输入

	// ── 跳跃回调 ────────────────────────────────────────────

	UFUNCTION()
	void OnJumpStarted(const FInputActionValue& Value);   // 跳跃按下 → ACharacter::Jump
	UFUNCTION()
	void OnJumpCompleted(const FInputActionValue& Value); // 跳跃松开 → ACharacter::StopJumping

	// ── 闪避/冲刺回调 ──────────────────────────────────────

	UFUNCTION()
	void OnDodgeStarted(const FInputActionValue& Value);   // 闪避键按下开始计时
	UFUNCTION()
	void OnDodgeCompleted(const FInputActionValue& Value); // 闪避键松开，短按生成闪避

	UFUNCTION()
	void OnWalkModifierStarted(const FInputActionValue& Value); // 步行修饰按下
	UFUNCTION()
	void OnWalkModifierCompleted(const FInputActionValue& Value); // 步行修饰松开

	// ── 蹲下回调 ────────────────────────────────────────────

	UFUNCTION()
	void OnCrouchStarted(const FInputActionValue& Value);  // 蹲下切换

	// ── 战斗回调 ────────────────────────────────────────────

	UFUNCTION()
	void OnAttackStarted(const FInputActionValue& Value);   // 攻击按下
	UFUNCTION()
	void OnAttackCompleted(const FInputActionValue& Value); // 攻击松开
	UFUNCTION()
	void OnGuardStarted(const FInputActionValue& Value);    // 防御按下
	UFUNCTION()
	void OnGuardCompleted(const FInputActionValue& Value);  // 防御松开
	UFUNCTION()
	void OnLockOnStarted(const FInputActionValue& Value);   // 锁定按下
	UFUNCTION()
	void OnProstheticStarted(const FInputActionValue& Value);   // 义手按下
	UFUNCTION()
	void OnProstheticCompleted(const FInputActionValue& Value); // 义手松开
	UFUNCTION()
	void OnGrappleStarted(const FInputActionValue& Value);  // 钩索按下

	// ── 交互/道具回调 ──────────────────────────────────────

	UFUNCTION()
	void OnInteractStarted(const FInputActionValue& Value);      // 交互按下
	UFUNCTION()
	void OnUseItemStarted(const FInputActionValue& Value);       // 道具使用
	UFUNCTION()
	void OnHealingGourdStarted(const FInputActionValue& Value);  // 伤药葫芦
	UFUNCTION()
	void OnCycleItemNextStarted(const FInputActionValue& Value); // 切换道具下一个
	UFUNCTION()
	void OnCycleItemPrevStarted(const FInputActionValue& Value); // 切换道具上一个

	// ── 系统回调 ────────────────────────────────────────────

	UFUNCTION()
	void OnPauseStarted(const FInputActionValue& Value); // 暂停
	UFUNCTION()
	void OnMenuStarted(const FInputActionValue& Value);  // 菜单

private:
	// ── 移动档位解析 ──────────────────────────────────────────

	ESKMovementTier ResolveMovementTierFromInput(float InputMagnitude) const; // 根据按键/摇杆推力解析目标移动档位
	void ApplyDesiredMovementTier(float InputMagnitude);                      // 将目标移动档位写入移动组件
	void QueueDodgePressed();                                                 // 短按闪避键时生成一次闪避输入

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
	TObjectPtr<UInputAction> WalkModifierAction;         // 步行修饰

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
	float MoveInputAmount = 0.f;                     // 移动输入强度（0-1，摇杆轻推用于 Walk/Run 迟滞）
	float MoveInputReleaseBufferRemaining = 0.f;     // 移动输入释放缓冲剩余时间（过滤 Enhanced Input 的瞬时 0 值）
	FVector2D LookIntent = FVector2D::ZeroVector;    // 视角方向（原始值）
	bool bAttackHeld = false;                        // 攻击键按住
	bool bGuardHeld = false;                         // 防御键按住
	bool bDodgeHeld = false;                         // 闪避键按住（冲刺用）
	bool bDodgeActive = false;                       // 闪避动作窗口有效
	bool bWalkHeld = false;                          // 步行修饰键按住
	bool bProstheticHeld = false;                    // 义手键按住

	float DodgeHoldTime = 0.f;                       // 闪避键按住时长（短按=闪避，长按=冲刺）
	float DodgeActiveTimeRemaining = 0.f;            // 闪避动作窗口剩余时间

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Input", meta = (AllowPrivateAccess = "true"))
	float SprintHoldThreshold = 0.18f;               // 加速键按住超过该时间才进入 Sprint

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Input", meta = (AllowPrivateAccess = "true"))
	float DodgeActiveDuration = 0.35f;               // 短按闪避后保持闪避状态的默认时长

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Input", meta = (AllowPrivateAccess = "true"))
	float MoveInputReleaseBufferDuration = 0.08f;    // 移动输入释放缓冲时长（防止 Alt+方向键等组合导致动画输入跳变）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Input", meta = (AllowPrivateAccess = "true"))
	float AnalogWalkEnterThreshold = 0.50f;          // 从 Run 回到 Walk 的轻推阈值（低于该值才降档）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Input", meta = (AllowPrivateAccess = "true"))
	float AnalogRunEnterThreshold = 0.62f;           // 从 Walk 进入 Run 的正常推动阈值（高于该值才升档）

	// ── 长按计时 ────────────────────────────────────────────

	float AttackHoldTime = 0.f;                      // 攻击长按累计时间（秒，> 0.3s 视为蓄力）
	float ProstheticHoldTime = 0.f;                  // 义手长按累计时间（秒）

	// ── 连段 ────────────────────────────────────────────────

	int32 ComboIndex = 0;                            // 当前连段序号
	float TimeSinceLastAttack = 0.f;                 // 距上次攻击时间（秒，> 0.5s 超时复位）

	// ── 输入缓冲队列 ────────────────────────────────────────

	TArray<FSKBufferedInput> InputBuffer;            // 输入缓冲队列（最大 6 条，超时移除）

	// ── 角色引用 ────────────────────────────────────────────

	TWeakObjectPtr<ACharacter> OwnerCharacter;       // 所有者角色缓存
};
