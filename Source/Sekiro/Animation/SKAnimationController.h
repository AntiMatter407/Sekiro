// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Movement/SKMovementComponent.h"
#include "SKAnimationController.generated.h"

class USKAnimationLogicData;
class ASKCharacter;
class USKInputHandler;
class USKAnimInstance;
class ASKWeapon;
class UAnimSequence;

// ============================================================================
// USKAnimationController — 动画控制组件
//     消费 USKInputHandler 的动作意图，基于 USKAnimationLogicData 的
//     CancelWindow 数据驱动动画选择与 Montage 播放，实现优先级状态机
// ============================================================================

// ── 动作优先级常量（对齐原版 TAE）────────────────────────────
namespace ESKActionPriority
{
	constexpr int32 Deathblow  = 10;          // 忍杀（不可打断）
	constexpr int32 Death      = 9;           // 死亡（不可打断）
	constexpr int32 Hit        = 8;           // 受击（不可打断，预留）
	constexpr int32 Dodge      = 7;           // 闪避
	constexpr int32 Deflect    = 6;           // 弹刀成功（预留）
	constexpr int32 Guard      = 5;           // 防御
	constexpr int32 Prosthetic = 4;           // 义手
	constexpr int32 ItemUse    = 3;           // 道具
	constexpr int32 Attack     = 2;           // 攻击
	constexpr int32 Quickstep  = 1;           // 垫步（预留）
	constexpr int32 Locomotion = 0;           // 移动（始终可打断）
}

// ── 移动方向枚举（8方向）───────────────────────────────────
UENUM(BlueprintType)
enum class ESKLocomotionDirection : uint8
{
	Fwd   UMETA(DisplayName = "前进"),                                 // -22.5 ~ 22.5
	Fwd_L UMETA(DisplayName = "前左"),                                 // 22.5 ~ 67.5
	L     UMETA(DisplayName = "左"),                                   // 67.5 ~ 112.5
	Bwd_L UMETA(DisplayName = "后左"),                                 // 112.5 ~ 157.5
	Bwd   UMETA(DisplayName = "后退"),                                 // 157.5 ~ -157.5
	Bwd_R UMETA(DisplayName = "后右"),                                 // -157.5 ~ -112.5
	R     UMETA(DisplayName = "右"),                                   // -112.5 ~ -67.5
	Fwd_R UMETA(DisplayName = "前右")                                  // -67.5 ~ -22.5
};

// ── 移动状态结构体 ──────────────────────────────────────────
USTRUCT(BlueprintType)
struct SEKIRO_API FSKLocomotionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	ESKMovementTier Tier = ESKMovementTier::Idle;                     // 当前移动层级

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	ESKLocomotionDirection Direction = ESKLocomotionDirection::Fwd;   // 当前移动方向

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	int32 AnimID = 0;                                                  // 当前动画ID

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsMoving = false;                                            // 是否处于移动中
};

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKAnimationController : public UActorComponent
{
	GENERATED_BODY()

public:
	USKAnimationController();

	// ── 配置 ──────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
	TObjectPtr<USKAnimationLogicData> AnimLogicData;           // TAE DataAsset 引用

	// ── 状态查询 ──────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "Animation")
	FName GetCurrentAction() const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	int32 GetCurrentAnimID() const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	int32 GetCurrentPriority() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;

	// ── 帧级更新 ──────────────────────────────────────────

	UFUNCTION()
	void UpdateFrameState();                              // 更新当前帧/时间

	UFUNCTION()
	void ApplyFrameFlags();                               // 应用 DataAsset 行为标志到角色

	// ── 攻击碰撞体更新 ────────────────────────────────────

	UFUNCTION()
	void UpdateAttackHitbox();                            // 根据当前帧查询数据驱动碰撞体激活/禁用

	// ── 意图处理 ──────────────────────────────────────────

	UFUNCTION()
	void ProcessIntents();                                // 按优先级处理输入意图
	bool TryPlayAction(FName Action, int32 Priority);     // 尝试触发动作（含 CancelWindow 判定）

	// ── 攻击处理（Priority 2）─────────────────────────────

	void HandleAttack();                                  // 攻击主入口
	int32 GetComboAnimID(int32 ComboIdx) const;           // 连段序号 → AnimID
	FName GetMoveDirectionSuffix() const;                 // 移动方向 → 方向后缀名
	void ResetAttackState();                              // 重置攻击状态
	void UpdateChargeState(float DeltaTime);              // 蓄力状态更新
	bool CheckChargeRelease();                            // 检测蓄力按键释放

	// ── 移动层（Locomotion，Priority 0）───────────────────

	void ProcessLocomotion();                             // 移动层主逻辑
	FSKLocomotionState EvaluateLocomotionState(float Speed, float Angle) const;  // 速度+角度 → 状态
	int32 ResolveLocomotionAnimID(const FSKLocomotionState& State) const;         // 状态 → AnimID
	int32 GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To) const;  // 过渡动画
	int32 GetStopAnimID(const FSKLocomotionState& State) const;                   // 停止动画
	int32 GetTurnAnimID(float AngleDelta) const;                                  // 转身动画（正=左转，负=右转）
	void PlayLocomotionMontage(int32 AnimID, bool bLooping);                      // 播放移动动画
	void OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted);         // 过渡结束回调

	// ── 动画播放 ──────────────────────────────────────────

	int32 ResolveAnimID(FName Action);                    // 意图→AnimID
	void PlayMontageByID(int32 AnimID, float Crossfade);  // 播放动画
	void EnsureMontageLoaded(int32 AnimID);               // 按需加载动画

private:
	// ── 工具 ──────────────────────────────────────────────

	USKAnimInstance* GetAnimInstance() const;             // 获取 SKAnimInstance 引用
	ASKWeapon* GetWeapon() const;                         // 获取当前装备的武器引用

	// ── 运行时状态 ────────────────────────────────────────

	FName CurrentAction;                                  // 当前动作名
	int32 CurrentAnimID = 0;                              // 当前动画ID
	int32 CurrentPriority = 0;                            // 当前优先级
	float CurrentAnimTime = 0.f;                          // 当前动画时间（秒）

	// ── 攻击状态 ──────────────────────────────────────────

	struct FAttackState
	{
		int32 ComboIndex = 0;                              // 当前连段序号（0=无连段, 1-4=R1连段）
		float ComboTimeout = 0.f;                          // 连段超时计时（>0.5s 复位）
		bool bIsCharging = false;                          // 是否正在蓄力
		float ChargeTime = 0.f;                            // 蓄力累计时间
		FName LastAttackAction;                            // 上次攻击动作（用于 Combo 推进判定）
		bool bAttackHeldPrev = false;                      // 上一帧攻击按住状态（检测下降沿）
	};
	FAttackState AttackState;                             // 攻击运行时状态

	// ── 移动状态 ──────────────────────────────────────────

	FSKLocomotionState CurrentLocoState;                  // 当前移动状态
	float LastAngle = 0.f;                                // 上一帧角度（转身判定用）
	float TurnCooldown = 0.f;                             // 转身冷却计时（秒）

	// ── 缓存 ──────────────────────────────────────────────

	TMap<int32, TObjectPtr<UAnimSequence>> MontageCache;   // AnimID → AnimSequence 缓存
	TWeakObjectPtr<ASKCharacter> OwnerCharacter;          // 角色引用
	TWeakObjectPtr<USKInputHandler> InputHandler;         // 输入组件引用
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;          // 骨骼网格引用
};
