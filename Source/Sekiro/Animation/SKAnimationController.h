// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Movement/SKMovementComponent.h"
#include "Engine/DataTable.h"
#include "Animation/SKAnimDataTypes.h"
#include "SKAnimationController.generated.h"

class USKAnimationLogicData;
class ASKCharacter;
class USKInputHandler;
class USKAnimInstance;
class ASKWeapon;
class UAnimSequence;

// ── 角色行为状态 ──────────────────────────────────────────────
UENUM(BlueprintType)
enum class ESKCharacterState : uint8
{
    Idle,               // 空闲/移动/Locomotion
    Attack,             // 攻击动作中
    Guard,              // 防御中
    Dodge,              // 闪避中
    Jump,               // 起跳动画中
    Airborne,           // 空中（Jump后物理上升/下落中）
    Hit,                // 受击
    Death               // 死亡
};

namespace ESKActionPriority
{
    constexpr int32 Deathblow  = 10;
    constexpr int32 Death      = 9;
    constexpr int32 Hit        = 8;
    constexpr int32 Dodge      = 7;
    constexpr int32 Deflect    = 6;
    constexpr int32 Jump       = 5;
    constexpr int32 Guard      = 5;
    constexpr int32 Prosthetic = 4;
    constexpr int32 ItemUse    = 3;
    constexpr int32 Attack     = 2;
    constexpr int32 Quickstep  = 1;
    constexpr int32 Locomotion = 0;
}

UENUM(BlueprintType)
enum class ESKLocomotionDirection : uint8
{
    Fwd, Fwd_L, L, Bwd_L, Bwd, Bwd_R, R, Fwd_R
};

UENUM(BlueprintType)
enum class ESKGuardPhase : uint8
{
    NotGuarding, Idle, HitReaction, Broken
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKGuardState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) ESKGuardPhase Phase = ESKGuardPhase::NotGuarding;
    UPROPERTY(BlueprintReadOnly) bool bIsDeflecting = false;
    UPROPERTY(BlueprintReadOnly) float GuardHoldTime = 0.f;
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKLocomotionState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) ESKMovementTier Tier = ESKMovementTier::Idle;
    UPROPERTY(BlueprintReadOnly) ESKLocomotionDirection Direction = ESKLocomotionDirection::Fwd;
    UPROPERTY(BlueprintReadOnly) int32 AnimID = 0;
    UPROPERTY(BlueprintReadOnly) bool bIsMoving = false;
};

UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKAnimationController : public UActorComponent
{
    GENERATED_BODY()
public:
    USKAnimationController(const FObjectInitializer& ObjectInitializer);

    // ── 数据资产 ──────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
    TObjectPtr<USKAnimationLogicData> AnimLogicData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
    FString AnimAssetName = TEXT("Sekiro");

    // ── DataTable（从 c0000.hkx Behavior Graph 提取） ─────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data|StateMachine")
    TObjectPtr<UDataTable> StateAnimTable;          // State → AnimID 映射表（FSKStateAnimRow）

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data|StateMachine")
    TObjectPtr<UDataTable> TransitionTable;          // Event → State 转换表（FSKStateTransitionRow）

    // ── 查询接口 ──────────────────────────────────────────────

    UFUNCTION(BlueprintCallable) FName GetCurrentAction() const;
    UFUNCTION(BlueprintCallable) int32 GetCurrentAnimID() const;
    UFUNCTION(BlueprintCallable) ESKCharacterState GetCharacterState() const { return CurrentState; }
    UFUNCTION(BlueprintCallable) float GetCurrentAnimTime() const { return CurrentAnimTime; }
    UFUNCTION(BlueprintCallable) bool IsGuarding() const { return CurrentState == ESKCharacterState::Guard; }

    UFUNCTION(BlueprintCallable)
    int32 DeriveNextAnim(int32 CurrentAnimID, FName Action) const;

    UFUNCTION(BlueprintCallable)
    bool IsEnemyAttacking(int32& OutBehaviorJudgeID, int32& OutStartFrame) const;

    UFUNCTION(BlueprintCallable)
    ASKCharacter* GetLockOnTarget() const;

    // ── 帧级曲线查询（从 USKAnimInstance 迁移） ───────────────

    UFUNCTION(BlueprintCallable, Category = "Cancel")
    bool CanCancelTo(FName TargetAction, float& OutCrossfade) const;

    UFUNCTION(BlueprintCallable, Category = "Attack")
    bool IsHitboxActive() const;

    UFUNCTION(BlueprintCallable, Category = "Cancel")
    static int32 GetActionPriority(FName Action);

    // ── 状态机（从 USKAnimInstance 迁移） ─────────────────────

    UFUNCTION(BlueprintCallable, Category = "State Machine")
    bool SendStateEvent(FName EventName);

    UFUNCTION(BlueprintCallable, Category = "State Machine")
    void ForceState(FName NewState);

    UFUNCTION(BlueprintCallable, Category = "State Machine")
    FName EvaluateLocomotionState(float MoveSpeed) const;

    UFUNCTION(BlueprintPure, Category = "State Machine")
    bool IsInState(FName State) const { return CurrentAction == State; }

    UFUNCTION(BlueprintPure, Category = "State Machine")
    FName GetPreviousAction() const { return PreviousAction; }

    UFUNCTION(BlueprintPure, Category = "State Machine")
    float GetTimeInState() const { return TimeInState; }

    // ── 外部事件 ──────────────────────────────────────────────

    void OnGuardHit(int32 AnimID);
    void OnGuardBreak();
    void OnHitReceived(int32 AnimID);
    void OnDeath(int32 AnimID);
    void OnResurrection();
    void SetDeathBlowActive() { bDeathBlowActive = true; }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;

    void UpdateFrameState();
    void UpdateFrameFlags();                        // 从动画曲线读取 FrameFlags
    void UpdateAttackHitbox();
    void UpdateChargeState(float DeltaTime);
    void UpdateContextFlags();
    void ProcessIntents();
    bool TryPlayAction(FName Action, int32 Priority);

    // ── 状态迁移辅助 ──────────────────────────────────────────
    void TransitionTo(ESKCharacterState NewState, FName Action, int32 Priority);
    bool CanTransition(FName Action, int32& OutPriority);

    // ── 动作处理 ──────────────────────────────────────────────
    void HandleAttack();
    int32 GetComboAnimID(int32 ComboIdx) const;
    FName GetMoveDirectionSuffix() const;
    void ResetAttackState();
    bool CheckChargeRelease();
    void HandleGuard();
    void HandleDodge();
    void HandleJump();
    bool HandleAirAttack();
    bool HandleAirDodge();
    void HandleProsthetic();
    void HandleItemUse();
    void HandleGrapple();
    void HandleCombatArt();
    bool HandleDeathblow();

    // ── Locomotion ────────────────────────────────────────────
    // Locomotion 动画完全由 AnimBlueprint 驱动，C++ 只更新参数
    void ProcessLocomotion();

    int32 ResolveAnimID(FName Action);
    int32 ResolveAnimID(FName Action, int32 FromAnimID);
    void PlayMontageByID(int32 AnimID, float Crossfade);
    void OnActionMontageEnded(UAnimMontage* Montage, bool bInterrupted);
    void OnAnyMontageEnded(UAnimMontage* Montage, bool bInterrupted);
    void EnsureMontageLoaded(int32 AnimID);

private:
    USKAnimInstance* GetAnimInstance() const;
    ASKWeapon* GetWeapon() const;

    // ── 状态机初始化 ──────────────────────────────────────────
    void InitStateMap();                            // 初始化硬编码过渡 + AnimID 映射
    void LoadTransitionsFromDataTable();            // DataTable 覆盖过渡规则
    void LoadStateAnimMapFromDataTable();           // DataTable 覆盖 AnimID 映射
    void OnStateChanged(FName OldState, FName NewState);

    // ── 状态 ──────────────────────────────────────────────────
    ESKCharacterState CurrentState = ESKCharacterState::Idle;
    FName CurrentAction;
    int32 CurrentAnimID = 0;
    int32 CurrentPriority = 0;
    float CurrentAnimTime = 0.f;

    FName PreviousAction;
    float TimeInState = 0.f;

    struct FAttackState
    {
        int32 ComboIndex = 0;
        float ComboTimeout = 0.f;
        bool bIsCharging = false;
        float ChargeTime = 0.f;
        FName LastAttackAction;
        bool bAttackHeldPrev = false;
    };
    FAttackState AttackState;

    FSKGuardState GuardState;
    bool bWasInAir = false;

    // ── 上下文标志 ────────────────────────────────────────────
    bool bCounterWindow = false;
    bool bDeathBlowActive = false;
    bool bIsInAir = false;

    // ── 帧级标志（从 FrameFlags 曲线读取） ────────────────────
    bool bDisableTurning = false;
    bool bDisableMovement = false;
    bool bCanDeflect = false;
    bool bInvincible = false;

    // ── 状态机数据 ────────────────────────────────────────────

    // EventName → TargetState（通配转换，任意状态均可触发）
    TMap<FName, FName> TransitionMap;

    // State → 主要 AnimID 字符串（例：Sprint → "a000_001151"）
    TMap<FName, FName> StateAnimMap;

    TMap<int32, TObjectPtr<UAnimSequence>> MontageCache;
    TWeakObjectPtr<ASKCharacter> OwnerCharacter;
    TWeakObjectPtr<USKInputHandler> InputHandler;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
};
