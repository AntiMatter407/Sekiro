#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UnLuaInterface.h"
#include "AbilitySystem/SKResourcePolicy.h"
#include "Character/SKSurvivalTypes.h"
#include "Combat/SKCombatTypes.h"
#include "GameplayTagContainer.h"
#include "SKSurvivalComponent.generated.h"

class USKAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSKSurvivalReadySignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSKLifeStateChangedSignature, ESKLifeState, LifeState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSKSurvivalResourceChangedSignature, float, Current, float, Maximum, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSKSurvivalTransitionSignature, const FSKSurvivalTransitionEvent&, Event);

/** 生命/死亡/回生与躯干流程权威；全部可调数值只从 GAS 读取。 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class SEKIRO_API USKSurvivalComponent : public UActorComponent, public IUnLuaInterface, public ISKResourcePolicy
{
    GENERATED_BODY()

public:
    // ── 宿主与原生策略 ──────────────────────────────────────
    USKSurvivalComponent();
    virtual FString GetModuleName_Implementation() const override;

    UFUNCTION(BlueprintCallable, Category = "Survival")
    bool BindAttributeSystem(USKAbilitySystemComponent* AttributeSystem);

    virtual bool IsResourcePolicyBusy() const override;
    virtual ESKNumericResultCode CheckResourceOperation(ESKNumericOperation Operation) const override;
    virtual FName GetResourceRejectionReason(ESKNumericOperation Operation) const override;
    virtual bool CanRestoreOwnedResources(int64 TransitionSerial) const override;
    virtual void ReconcileResourceState(const FSKNumericResult* Result, bool bInitial) override;
    virtual void FlushResourceEvents() override;

    // ── 查询与提交 ──────────────────────────────────────────
    UFUNCTION(BlueprintPure, Category = "Survival")
    bool IsSurvivalReady() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    FSKSurvivalSnapshot GetSurvivalSnapshot() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    ESKLifeState GetLifeState() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    int64 GetLifeSerial() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    FSKSurvivalTransitionToken GetDeathToken() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    FSKSurvivalTransitionToken GetReviveToken() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    FSKSurvivalTransitionToken GetPostureBreakToken() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool IsAlive() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool IsDead() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool IsPostureBroken() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool CanAct() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool CanReceiveDamage() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool CanBeginRevive() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetHealth() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetMaxHealth() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetHealthRatio() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetPosture() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetMaxPosture() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    float GetPostureRatio() const;

    UFUNCTION(BlueprintPure, Category = "Survival")
    bool IsTransitionTokenValid(const FSKSurvivalTransitionToken& Token) const;

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKNumericResult ApplyHealthDamage(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKNumericResult RestoreHealth(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKNumericResult ApplyPostureDamage(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKNumericResult RestorePosture(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalImpactResult ApplySurvivalImpact(float HealthDamage, float PostureDamage, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult FinishDeath(const FSKSurvivalTransitionToken& Token);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult BeginRevive(int64 ExpectedLifeSerial);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult CompleteRevive(const FSKSurvivalTransitionToken& Token, float HealthRatio);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult CancelRevive(const FSKSurvivalTransitionToken& Token);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult ResetBrokenPosture(const FSKSurvivalTransitionToken& Token);

    UFUNCTION(BlueprintCallable, Category = "Survival")
    FSKSurvivalTransitionResult CompletePostureRecovery(const FSKSurvivalTransitionToken& Token);

    // ── Lua 工作流 ──────────────────────────────────────────
    UFUNCTION(BlueprintNativeEvent, Category = "Survival")
    bool ApplyPostureImpact(FName Reason, ESKIncomingAttackType AttackType, AActor* SourceActor);

    UFUNCTION(BlueprintNativeEvent, Category = "Survival")
    void HandleSurvivalTick(float DeltaSeconds);

    // ── 通知 ────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalReadySignature OnSurvivalReady; // 每次绑定首次就绪

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKLifeStateChangedSignature OnLifeStateChanged; // 同步转换完成通知

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalResourceChangedSignature OnHealthChanged; // 完整生命快照变化

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalResourceChangedSignature OnPostureChanged; // 完整躯干快照变化

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnDeathStarted; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnDeathFinished; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnReviveStarted; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnRevived; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnReviveCancelled; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnPostureBroken; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnPostureRecovered; // 带有效身份及最终快照的流程事件

    UPROPERTY(BlueprintAssignable, Category = "Survival")
    FSKSurvivalTransitionSignature OnPostureBreakCancelled; // 带有效身份及最终快照的流程事件

protected:
    // ── 引擎生命周期 ────────────────────────────────────────
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // ── 转换内部 ────────────────────────────────────────────
    bool ResolveRequiredGameplayTags();
    FSKSurvivalTransitionToken MakeToken(ESKSurvivalTransitionKind Kind) const;
    FSKSurvivalTransitionResult MakeResult(ESKSurvivalResultCode Code, const FSKSurvivalTransitionToken& Token) const;
    ESKSurvivalResultCode ValidateTransition(const FSKSurvivalTransitionToken& Token, ESKSurvivalTransitionKind Kind) const;
    void SetLifeState(ESKLifeState NewState);
    void UpdateOwnedTags();
    void QueueTransition(FName EventName, const FSKSurvivalTransitionToken& Token, FName Reason, AActor* SourceActor);
    void PublishEvents();
    FSKNumericResult CommitResources(const FSKSurvivalTransitionToken& Token, float Health, float Posture);
    bool WasResourceCommitSuccessful(const FSKNumericResult& Result, float Health, float Posture) const;

    // ── 配置与状态 ──────────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Survival|Lua")
    FString LuaModuleName = TEXT("Gameplay.Sekiro.Character.SKSurvivalComponent"); // Lua 工作流模块

    UPROPERTY(Transient)
    TObjectPtr<USKAbilitySystemComponent> ASC; // 同角色 ASC，不另存可写属性

    UPROPERTY(Transient)
    TArray<FSKSurvivalTransitionEvent> PendingEvents; // 调用栈结束前统一发布的事件数据

    TArray<FName> PendingEventNames; // 与事件数组对应的语义名称
    FGameplayTag LifeDyingTag; // 从配置字典解析的死亡处理中标签
    FGameplayTag LifeDeadTag; // 从配置字典解析的死亡完成标签
    FGameplayTag LifeRevivingTag; // 从配置字典解析的回生中标签
    FGameplayTag PostureBrokenTag; // 从配置字典解析的躯干崩溃标签
    FGameplayTag DamageImmuneTag; // 从配置字典解析的生命伤害免疫标签
    FGameplayTag PostureImmuneTag; // 从配置字典解析的躯干伤害免疫标签
    FGameplayTag ReviveBlockedTag; // 从配置字典解析的禁止回生标签
    FGameplayTagContainer OwnedStateTags; // 仅追踪本组件添加的 loose tag 贡献
    FSKAttributeSnapshot LastPublishedAttributes; // 仅供通知去重，禁止作为提交来源
    ESKLifeState LifeState = ESKLifeState::Uninitialized; // 唯一生命状态
    int64 LifeSerial = 0; // 生命轮次，成功回生递增
    int64 TransitionSerial = 0; // 死亡/回生过程序号
    int64 BreakSerial = 0; // 躯干崩溃过程序号
    double BreakStartTime = 0.0; // 当前崩溃进入世界时间
    bool bPostureBroken = false; // 唯一躯干流程状态
    bool bBound = false; // 唯一策略注册成功
    bool bReady = false; // 完整初始快照已分类
    bool bEndedPlay = false; // 离场后拒绝所有写入和通知
    bool bTransitionBusy = false; // 同步流程通知重入门禁
    bool bInternalResourceCommit = false; // 原生私有资源恢复窗口
    int64 InternalTransitionSerial = 0; // 当前原生恢复授权编号
    bool bReadyEventPending = false; // 初始化就绪通知
    bool bLifeStateEventPending = false; // 当前调用栈内生命状态变化
    bool bHasPublishedResources = false; // 首次完整资源通知标记
    bool bSerialExhausted = false; // 序号耗尽永久关闭新请求，绝不复用旧令牌
    bool bBypassViolationLogged = false; // 非法旁路生命资源诊断去重
};
