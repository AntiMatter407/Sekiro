// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Movement/SKMovementComponent.h"
#include "SKAnimationController.generated.h"

class USKAnimationLogicData;
class USKCombatData;
class ASKCharacter;
class USKInputHandler;
class USKAnimInstance;
class ASKWeapon;
class UAnimSequence;

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

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
    TObjectPtr<USKAnimationLogicData> AnimLogicData;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Data")
    TObjectPtr<USKCombatData> CombatData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
    FString AnimAssetName = TEXT("Sekiro");

    UFUNCTION(BlueprintCallable) FName GetCurrentAction() const;
    UFUNCTION(BlueprintCallable) int32 GetCurrentAnimID() const;
    UFUNCTION(BlueprintCallable) int32 GetCurrentPriority() const;
    UFUNCTION(BlueprintCallable) float GetCurrentAnimTime() const { return CurrentAnimTime; }
    UFUNCTION(BlueprintCallable) bool IsGuarding() const { return GuardState.Phase != ESKGuardPhase::NotGuarding; }
    UFUNCTION(BlueprintCallable)
    int32 GetComboNextAnim(int32 CurrentAnimID, FName Action) const;

    /** 查询敌人是否正在攻击（当前帧有活跃攻击框） */
    UFUNCTION(BlueprintCallable)
    bool IsEnemyAttacking(int32& OutBehaviorJudgeID, int32& OutStartFrame) const;

    /** 获取锁定目标（暂未实现，返回 nullptr） */
    UFUNCTION(BlueprintCallable)
    ASKCharacter* GetLockOnTarget() const;

    void OnGuardHit(int32 AnimID);
    void OnGuardBreak();
    void OnHitReceived(int32 AnimID);
    void OnDeath(int32 AnimID);
    void OnResurrection();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;

    void UpdateFrameState();
    void ApplyFrameFlags();
    void UpdateAttackHitbox();
    void UpdateChargeState(float DeltaTime);
    void UpdateContextFlags();
    void ProcessIntents();
    bool TryPlayAction(FName Action, int32 Priority);

    void HandleAttack();
    int32 GetComboAnimID(int32 ComboIdx) const;
    FName GetMoveDirectionSuffix() const;
    void ResetAttackState();
    bool CheckChargeRelease();

    void HandleGuard();
    void HandleDodge();
    void HandleJump();
    void HandleProsthetic();
    void HandleItemUse();
    void HandleGrapple();
    void HandleCombatArt();
    bool HandleDeathblow();

    void ProcessLocomotion();
    FSKLocomotionState EvaluateLocomotionState(float Speed, float Angle) const;
    int32 ResolveLocomotionAnimID(const FSKLocomotionState& State) const;
    int32 GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To) const;
    int32 GetStopAnimID(const FSKLocomotionState& State) const;
    int32 GetTurnAnimID(float AngleDelta) const;
    void PlayLocomotionMontage(int32 AnimID, bool bLooping);
    void OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted);

    int32 ResolveAnimID(FName Action);                         // 原有：从当前 AnimID 派生
    int32 ResolveAnimID(FName Action, int32 FromAnimID);       // 新增：从指定 AnimID 派生
    void PlayMontageByID(int32 AnimID, float Crossfade);
    void EnsureMontageLoaded(int32 AnimID);

private:
    USKAnimInstance* GetAnimInstance() const;
    ASKWeapon* GetWeapon() const;

    FName CurrentAction;
    int32 CurrentAnimID = 0;
    int32 CurrentPriority = 0;
    float CurrentAnimTime = 0.f;

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

    // ── 上下文标志（用于上下文输入分支）──
    bool bCounterWindow = false;             // 完美格挡后反斩窗口
    bool bDeathBlowActive = false;           // 忍杀标识激活
    bool bIsInAir = false;                   // 是否在空中（从AnimInstance同步）

    FSKLocomotionState CurrentLocoState = FSKLocomotionState();
    float LastAngle = 0.f;
    float TurnCooldown = 0.f;

    TMap<int32, TObjectPtr<UAnimSequence>> MontageCache;
    TWeakObjectPtr<ASKCharacter> OwnerCharacter;
    TWeakObjectPtr<USKInputHandler> InputHandler;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
};
