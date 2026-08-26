// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/SKCombatTypes.h"
#include "AttributeSet.h"
#include "Character/SKSurvivalTypes.h"
#include "UnLuaInterface.h"
#include "SKCombatComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class AController;
class ASKAIBattleProjectile;
class UDamageType;
class USKAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSKCombatInputEventSignature, const FSKCombatInputEvent&, InputEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSKCombatAnimationEndedSignature, int32, ActionSerial, bool, bInterrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSKPostureChangedSignature, float, Current, float, Maximum, float, Normalized);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class SEKIRO_API USKCombatComponent : public UActorComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKCombatComponent();

    // ── UnLua 宿主 ─────────────────────────────────────────────

    virtual FString GetModuleName_Implementation() const override;

    /** Lua 可覆盖的战斗逐帧编排入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Combat|Gameplay")
    void HandleCombatTick(float DeltaTime);

    /** 将一次抽象防御结果交给 Lua 战斗规则裁决。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Combat|Posture")
    void HandlePostureImpact(FName ResultName, ESKIncomingAttackType AttackType);

    /** 由攻击者查询本次武器接触使用的抽象攻击类型。 */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Combat|Clash")
    ESKIncomingAttackType ResolveOutgoingAttackType() const;

    /** 将武器与角色的接触交给 Lua 裁决为命中、格挡或弹反。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Combat|Clash")
    ESKWeaponContactResult ResolveIncomingWeaponContact(
        USKCombatComponent* AttackerCombat,
        ESKIncomingAttackType AttackType);

    /** 将一个抽象 AI 攻击请求交给 Lua 战斗规则启动。 */
    UFUNCTION(BlueprintNativeEvent, Category = "Combat|AI")
    bool RequestAIAttack(FName AttackRequest);

    /** 停止 Owner 当前由 AIController 发起的导航移动。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|AI")
    void StopOwnerAIMovement();

    // ── AI 战斗事件 ──────────────────────────────────────────

    /** 发布一条由组件赋予顺序和时间的通用 AI 战斗事件。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|AI")
    bool PublishAICombatEvent(const FSKAICombatEvent& Event);

    /** 按到达顺序消费等待时间最久的 AI 战斗事件。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|AI")
    bool ConsumeAICombatEvent(FSKAICombatEvent& OutEvent);

    /** 清空尚未消费的 AI 战斗事件。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|AI")
    void ClearAICombatEvents();

    /** 查询尚未消费的 AI 战斗事件数量。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|AI")
    int32 GetPendingAICombatEventCount() const;

    // ── AI 弹射物 ────────────────────────────────────────────

    /** 生成并初始化一枚通用 AI 战斗弹射物。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|AI|Projectile")
    bool SpawnAIBattleProjectile(
        TSubclassOf<ASKAIBattleProjectile> ProjectileClass,
        const FVector& SpawnLocation,
        AActor* TargetActor,
        const FVector& TargetLocation,
        float Speed,
        float GravityScale,
        float Damage,
        float LifeSeconds,
        FName EventTag,
        ASKAIBattleProjectile*& OutProjectile);

    // ── 状态与序列号 ──────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    ESKCombatActionState GetCombatActionState() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void SetCombatActionState(ESKCombatActionState NewState);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    ESKCombatPostureState GetCombatPostureState() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void SetCombatPostureState(ESKCombatPostureState NewState);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    ESKAttackSide GetCommittedAttackSide() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void SetCommittedAttackSide(ESKAttackSide NewSide);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    ESKAttackSide GetNextAttackSide() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void SetNextAttackSide(ESKAttackSide NewSide);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsGuardHeld() const;

    /** 查询所属角色当前是否处于 Falling 移动模式。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsOwnerFalling() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsOwnerSprinting() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsOwnerDodgingOrStepActive() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    int32 GetActionSerial() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    int32 BeginCombatAction(ESKCombatActionState NewState);

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    int32 BeginSurvivalPresentation(const FSKSurvivalTransitionToken& Token, ESKCombatActionState NewState);

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void InvalidateCombatAction(int32 ExpectedActionSerial);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsActionSerialValid(int32 ExpectedActionSerial) const;

    // ── 架势 ──────────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "Combat|Posture")
    bool IsCombatAttributesReady() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Posture")
    float GetCurrentPosture() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Posture")
    float GetMaxPosture() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Posture")
    float GetPostureNormalized() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Posture")
    bool IsPostureBroken() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|Posture")
    float GetPostureRecoveryRate() const;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Posture")
    FSKPostureChangedSignature OnPostureChanged; // 架势数值快照变化通知

    // ── Owner 输入桥接 ────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Combat|Input")
    void SetOwnerExternalInputLock(FName Reason, bool bLocked);

    UFUNCTION(BlueprintCallable, Category = "Combat|Input")
    void ClearOwnerGameplayInputForScript();

    // ── Owner 武器碰撞桥接 ────────────────────────────────────

    /** 清空可选命中记录并开启 Owner 当前武器的攻击碰撞。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
    bool ActivateOwnerWeaponHitbox(bool bResetHitActors = true);

    /** 关闭 Owner 当前武器的攻击碰撞。 */
    UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
    bool DeactivateOwnerWeaponHitbox();

    // ── 输入事件 ──────────────────────────────────────────────

    void SubmitCombatInputEvent(const FSKCombatInputEvent& InputEvent);

    UFUNCTION(BlueprintCallable, Category = "Combat|Input")
    bool ConsumeCombatInputEvent(FSKCombatInputEvent& OutInputEvent);

    UFUNCTION(BlueprintCallable, Category = "Combat|Input")
    void ClearCombatInputEvents();

    UPROPERTY(BlueprintAssignable, Category = "Combat|Input")
    FSKCombatInputEventSignature OnCombatInputEvent; // 输入发布通知

    // ── 动态 Montage ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
    bool PlayCombatAnimation(UAnimSequence* Animation, float BlendInTime = 0.05f, float BlendOutTime = 0.05f, float PlayRate = 1.f, int32 LoopCount = 1);

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
    bool PlayCombatAnimationByPath(const FString& AnimationPath, float BlendInTime = 0.05f, float BlendOutTime = 0.05f, float PlayRate = 1.f, int32 LoopCount = 1);

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
    void StopCombatAnimation(float BlendOutTime = 0.05f);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    bool IsCombatAnimationPlaying() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    float GetCombatAnimationPosition() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    float GetActiveSequencePosition() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    float SampleActiveSequenceCurve(FName CurveName) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    float SampleActiveSequenceCurveAtTime(FName CurveName, double EventTimeSeconds) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation")
    bool IsCombatFullBodyActionActive() const;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Animation")
    FSKCombatAnimationEndedSignature OnCombatAnimationEnded; // 当前动作结束通知

    // ── 模拟来袭 ─────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation Test")
    int32 BeginIncomingAttackAnimationTest(ESKIncomingAttackType AttackType, float ActiveDuration);

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation Test")
    void ClearIncomingAttackAnimationTest(int32 ExpectedContextSerial);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation Test")
    bool IsIncomingAttackAnimationActiveAt(double EventTimeSeconds) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Animation Test")
    bool GetIncomingAttackAnimationContext(FSKIncomingAttackAnimationContext& OutContext) const;

    UFUNCTION(BlueprintCallable, Category = "Combat|Animation Test")
    bool TryConsumeIncomingAttackAnimation(int32 ExpectedContextSerial, int32 GuardInputSerial, double EventTimeSeconds, ESKIncomingAttackType& OutAttackType);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // ── GAS 属性桥接 ──────────────────────────────────────
    USKAbilitySystemComponent* ResolveAttributeSystem() const;

    UFUNCTION()
    void HandleGASAttributeChanged(FGameplayAttribute Attribute, float OldValue, float NewValue);

    UFUNCTION()
    void HandleGASAttributesReady();

    // ── AI 事件内部 ──────────────────────────────────────────

    UFUNCTION()
    void HandleOwnerTakeAnyDamage(
        AActor* DamagedActor,
        float Damage,
        const UDamageType* DamageType,
        AController* InstigatedBy,
        AActor* DamageCauser);

    // ── 动画内部 ──────────────────────────────────────────────

    UAnimInstance* ResolveAnimInstance() const;
    bool ResolveSequencePosition(float MontagePosition, float& OutSequencePosition) const;
    float EvaluateSequenceCurve(FName CurveName, float SequencePosition) const;
    void HandleCombatMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 EndedActionSerial);
    void ClearOwnedAnimationState();
    double GetWorldTimeSeconds() const;
    void BroadcastPostureChanged();

    // ── 配置 ──────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Lua", meta = (AllowPrivateAccess = "true"))
    FString LuaModuleName = TEXT("Gameplay.Sekiro.Combat.SKCombatComponent"); // UnLua 模块名

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation", meta = (AllowPrivateAccess = "true"))
    FName CombatSlotName = TEXT("CombatFullBodySlot"); // 战斗组件独占的全身 Slot

    // ── 运行时状态 ────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State", meta = (AllowPrivateAccess = "true"))
    ESKCombatActionState CombatActionState = ESKCombatActionState::Neutral; // 当前动作状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State", meta = (AllowPrivateAccess = "true"))
    ESKCombatPostureState CombatPostureState = ESKCombatPostureState::Normal; // 当前基础战斗姿态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State", meta = (AllowPrivateAccess = "true"))
    ESKAttackSide CommittedAttackSide = ESKAttackSide::None; // 当前动作已提交攻击侧

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State", meta = (AllowPrivateAccess = "true"))
    ESKAttackSide NextAttackSide = ESKAttackSide::Right; // 下一动作候选攻击侧

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|State", meta = (AllowPrivateAccess = "true"))
    bool bGuardHeld = false; // 防御键实时按住状态

    UPROPERTY(Transient)
    FSKSurvivalTransitionToken PresentationToken; // 仅授权当前特殊演出，不是生存状态副本

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> ActiveSequence; // 当前战斗动作源 Sequence

    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> ActiveMontage; // 当前战斗组件拥有的动态 Montage

    UPROPERTY(Transient)
    TArray<FSKAICombatEvent> PendingAICombatEvents; // 等待 ReactionRouter 消费的通用事件

    TArray<FSKCombatInputEvent> PendingInputEvents; // 等待 Lua 消费的有序输入事件
    FSKIncomingAttackAnimationContext IncomingAttackContext; // 当前模拟来袭上下文
    int32 ActionSerial = 0; // 当前动作序列号
    int32 LastAICombatEventSerial = 0; // 最近分配的 AI 战斗事件序列号
    int32 ContextSerial = 0; // 最近分配的来袭上下文序列号
    int32 DeflectGuardInputSerial = 0; // 最近消费来袭的 Guard 输入序列号
    int32 DeflectContextSerial = 0; // 当前弹反关联的来袭序列号
    bool bAICombatEventOverflowLogged = false; // 是否已记录事件队列溢出诊断
    bool bAICombatEventSerialExhaustedLogged = false; // 是否已记录事件序列耗尽诊断
    bool bHasBroadcastPosture = false; // 仅用于通知去重，不作为资源数值来源
    float LastBroadcastPosture = 0.f; // 最近通知的架势值缓存
    float LastBroadcastMaxPosture = 0.f; // 最近通知的架势上限缓存
    double AnimationStartTimeSeconds = 0.0; // 动态 Montage 开始的世界时间
    float ActivePlayRate = 1.f; // 当前动态 Montage 播放倍率
};
