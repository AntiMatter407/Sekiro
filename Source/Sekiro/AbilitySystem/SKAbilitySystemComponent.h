#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/SKAttributeTypes.h"
#include "SKAbilitySystemComponent.generated.h"

class USKAttributeProfile;
class USKNumericExecution;
class UCurveFloat;
class ISKResourcePolicy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSKAttributesReadySignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSKAttributeChangedSignature,
    FGameplayAttribute, Attribute, float, OldValue, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSKNumericExecutedSignature,
    const FSKNumericResult&, Result);

/** 角色 GAS 数值入口，不负责动作、生命轮次或死亡流程。 */
UCLASS(ClassGroup = (Abilities), meta = (BlueprintSpawnableComponent))
class SEKIRO_API USKAbilitySystemComponent : public UAbilitySystemComponent
{
    GENERATED_BODY()

    friend class USKNumericExecution;

public:
    // ── 初始化及查询 ──────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    bool InitializeFromValues(const FSKAttributeInitialization& Values);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    bool InitializeFromProfile(const USKAttributeProfile* Profile);

    UFUNCTION(BlueprintPure, Category = "GAS|Attributes")
    bool IsAttributesReady() const;

    UFUNCTION(BlueprintPure, Category = "GAS|Attributes")
    FSKAttributeSnapshot GetAttributeSnapshot() const;

    // ── 原生资源策略 ──────────────────────────────────────
    void RequireResourcePolicy();
    bool RegisterResourcePolicy(UObject* Owner, ISKResourcePolicy* Policy);
    void UnregisterResourcePolicy(UObject* Owner);
    bool IsResourcePolicyBoundTo(const UObject* Owner) const;
    bool IsResourceCommitActive() const;
    FSKNumericResult RestoreOwnedResources(UObject* PolicyOwner, int64 TransitionSerial,
        float TargetHealth, float TargetPosture);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult ApplyResourceImpact(float HealthDamage, float PostureDamage, AActor* SourceActor = nullptr);

    // ── 即时数值请求 ──────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult ApplyHealthDamage(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult RestoreHealth(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult ApplyPostureDamage(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult RestorePosture(float Amount, AActor* SourceActor = nullptr);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    FSKNumericResult ResetPosture();

    // ── 配置效果与护甲 ────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "GAS|Effects")
    bool ApplyAttributeEffect(TSubclassOf<UGameplayEffect> EffectClass, float Level,
        AActor* SourceActor, FActiveGameplayEffectHandle& OutHandle);

    UFUNCTION(BlueprintCallable, Category = "GAS|Effects")
    bool RemoveAttributeEffect(FActiveGameplayEffectHandle Handle);

    UFUNCTION(BlueprintCallable, Category = "GAS|Attributes")
    bool CalculateDamageAfterArmor(float DamageBeforeArmor, const UCurveFloat* ArmorCurve,
        float& OutDamage) const;

    // ── 通知 ──────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category = "GAS|Attributes")
    FSKAttributesReadySignature OnAttributesReady; // 完整初始化完成，只发布一次

    UPROPERTY(BlueprintAssignable, Category = "GAS|Attributes")
    FSKAttributeChangedSignature OnAttributeChanged; // 已提交的常驻属性变化，不包含临时属性

    UPROPERTY(BlueprintAssignable, Category = "GAS|Attributes")
    FSKNumericExecutedSignature OnNumericExecuted; // 一次请求结束后的实际结算记录

protected:
    // ── 生命周期 ──────────────────────────────────────────
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // ── 内部执行 ──────────────────────────────────────────
    bool InitializeInternal(const FSKAttributeInitialization& Values, const USKAttributeProfile* Profile);
    FSKNumericResult ExecuteNumeric(ESKNumericOperation Operation, float Amount, AActor* SourceActor);
    FSKNumericResult ExecuteResourcePair(ESKNumericOperation Operation, float HealthValue, float PostureValue,
        AActor* SourceActor, UObject* RestoreOwner = nullptr, int64 TransitionSerial = 0);
    ESKNumericResultCode CheckPolicy(ESKNumericOperation Operation) const;
    void CompleteResourceCommit(const FSKNumericResult* Result, bool bInitial = false);
    void BindAttributeDelegates();
    void HandleAttributeChanged(const FOnAttributeChangeData& Data);
    bool IsAllowedAttributeEffect(const UGameplayEffect* Effect) const;

    // ── 运行时状态 ────────────────────────────────────────
    UPROPERTY(Transient)
    TArray<FActiveGameplayEffectHandle> InitialEffectHandles; // 本次初始化持有的增益句柄

    TWeakObjectPtr<UObject> ResourcePolicyOwner; // 唯一策略拥有者，不延长生命周期
    ISKResourcePolicy* ResourcePolicy = nullptr; // 仅在弱拥有者有效时使用
    bool bResourcePolicyRequired = false; // 缺失必须策略时拒绝资源写入
    TArray<FOnAttributeChangeData> PendingAttributeChanges; // 只缓存值，不保留 GAS 执行上下文
    TArray<FGameplayAttribute> ObservedAttributes; // 已绑定常驻属性，离场时解除
    int64 LastRequestId = 0; // 结算请求序列
    bool bAttributesReady = false; // 完整配置已提交
    bool bHasEndedPlay = false; // 离场后永久拒绝旧回调再次初始化或写入
    bool bInitializingAttributes = false; // 初始化期间允许内部配置效果
    bool bApplyingNumericEffect = false; // 保护数值提交及同步通知的重入边界
    bool bNumericExecutionObserved = false; // 区分即时效果无有效句柄和执行被拒绝
};
