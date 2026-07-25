// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/SKCombatTypes.h"
#include "UnLuaInterface.h"
#include "SKCombatComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSKCombatInputEventSignature, const FSKCombatInputEvent&, InputEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSKCombatAnimationEndedSignature, int32, ActionSerial, bool, bInterrupted);

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
    int32 GetActionSerial() const;

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    int32 BeginCombatAction(ESKCombatActionState NewState);

    UFUNCTION(BlueprintCallable, Category = "Combat|State")
    void InvalidateCombatAction(int32 ExpectedActionSerial);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|State")
    bool IsActionSerialValid(int32 ExpectedActionSerial) const;

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
    // ── 动画内部 ──────────────────────────────────────────────

    UAnimInstance* ResolveAnimInstance() const;
    bool ResolveSequencePosition(float MontagePosition, float& OutSequencePosition) const;
    float EvaluateSequenceCurve(FName CurveName, float SequencePosition) const;
    void HandleCombatMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 EndedActionSerial);
    void ClearOwnedAnimationState();
    double GetWorldTimeSeconds() const;

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
    TObjectPtr<UAnimSequence> ActiveSequence; // 当前战斗动作源 Sequence

    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> ActiveMontage; // 当前战斗组件拥有的动态 Montage

    TArray<FSKCombatInputEvent> PendingInputEvents; // 等待 Lua 消费的有序输入事件
    FSKIncomingAttackAnimationContext IncomingAttackContext; // 当前模拟来袭上下文
    int32 ActionSerial = 0; // 当前动作序列号
    int32 ContextSerial = 0; // 最近分配的来袭上下文序列号
    int32 DeflectGuardInputSerial = 0; // 最近消费来袭的 Guard 输入序列号
    int32 DeflectContextSerial = 0; // 当前弹反关联的来袭序列号
    double AnimationStartTimeSeconds = 0.0; // 动态 Montage 开始的世界时间
    float ActivePlayRate = 1.f; // 当前动态 Montage 播放倍率
};
