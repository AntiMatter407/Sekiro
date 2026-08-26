#include "AbilitySystem/SKAbilitySystemComponent.h"
#include "AbilitySystem/SKAttributeProfile.h"
#include "AbilitySystem/SKNumericGameplayEffect.h"
#include "AbilitySystem/Attributes/SKCharacterAttributeSet.h"
#include "Curves/CurveFloat.h"
#include "AbilitySystem/SKResourcePolicy.h"
#include "UObject/UnrealType.h"

/**
 * 游戏线程通过即时 GE 初始化完整显式数值；重复调用不重新应用、不回血。
 * @param Values 必须完整且有限，正上限和资源范围由 IsValid 校验；按值写入效果参数。
 * @return 已就绪或本次初始化成功返回 true；无效配置、缺属性集或重入返回 false。
 */
bool USKAbilitySystemComponent::InitializeFromValues(const FSKAttributeInitialization& Values)
{
    return InitializeInternal(Values, nullptr);
}

/**
 * 游戏线程以数据资产配置初始化属性和初始增益；不加载路径、不授予能力。
 * @param Profile 非空配置资产，仅同步读取，不取得所有权。
 * @return 已就绪或初始化成功返回 true；配置缺失或任何效果失败时保持未就绪。
 */
bool USKAbilitySystemComponent::InitializeFromProfile(const USKAttributeProfile* Profile)
{
    if (!IsInGameThread() || !Profile) return false;
    return InitializeInternal(Profile->Values, Profile);
}

/** 游戏线程查询数值是否完整就绪；不会触发懒初始化。 */
bool USKAbilitySystemComponent::IsAttributesReady() const
{
    if (!ensureMsgf(IsInGameThread(), TEXT("GAS 数值查询仅允许游戏线程"))) return false;
    return bAttributesReady;
}

/** 游戏线程复制常驻属性快照；缺失属性集的字段保持零，不代表系统已就绪。 */
FSKAttributeSnapshot USKAbilitySystemComponent::GetAttributeSnapshot() const
{
    FSKAttributeSnapshot Snapshot;
    if (!ensureMsgf(IsInGameThread(), TEXT("GAS 数值查询仅允许游戏线程"))) return Snapshot;
    const USKCharacterAttributeSet* CharacterSet = GetSet<USKCharacterAttributeSet>();
    if (!CharacterSet) return Snapshot;
    Snapshot.Health = CharacterSet->GetHealth();
    Snapshot.Posture = CharacterSet->GetPosture();
    for (const FGameplayAttribute& Attribute : USKCharacterAttributeSet::GetConfigAttributes())
    {
        FFloatProperty* Field = FindFProperty<FFloatProperty>(FSKAttributeSnapshot::StaticStruct(), *Attribute.GetName());
        if (Field) Field->SetPropertyValue_InContainer(&Snapshot, Attribute.GetNumericValue(CharacterSet));
    }
    return Snapshot;
}

/** 游戏线程标记资源策略为必须；只允许未初始化时调用，不修改现有策略。 */
void USKAbilitySystemComponent::RequireResourcePolicy()
{
    if (IsInGameThread() && !bAttributesReady && !bInitializingAttributes) bResourcePolicyRequired = true;
}

/**
 * 游戏线程绑定唯一资源策略，不持有拥有者的强引用。
 * @param Owner 非空策略拥有者，必须与本 ASC 同属一个 Actor。
 * @param Policy 由 Owner 实现的原生接口，Owner 有效期间必须有效。
 * @return 成功或同一绑定幂等返回 true；冲突、离场或重入返回 false。
 */
bool USKAbilitySystemComponent::RegisterResourcePolicy(UObject* Owner, ISKResourcePolicy* Policy)
{
    if (!IsInGameThread() || bHasEndedPlay || bApplyingNumericEffect || !IsValid(Owner) || !Policy
        || (Owner != GetOwner() && Owner->GetTypedOuter<AActor>() != GetOwner())) return false;
    if (ResourcePolicyOwner.IsValid()) return ResourcePolicyOwner.Get() == Owner && ResourcePolicy == Policy;
    ResourcePolicyOwner = Owner;
    ResourcePolicy = Policy;
    if (bAttributesReady)
    {
        TGuardValue<bool> Guard(bApplyingNumericEffect, true);
        CompleteResourceCommit(nullptr, true);
    }
    return true;
}

/**
 * 游戏线程解绑匹配策略；不广播死亡，不移除其他拥有者。
 * @param Owner 当前注册拥有者；不匹配时无副作用。
 */
void USKAbilitySystemComponent::UnregisterResourcePolicy(UObject* Owner)
{
    if (!IsInGameThread() || ResourcePolicyOwner.Get() != Owner) return;
    ResourcePolicyOwner.Reset();
    ResourcePolicy = nullptr;
}

/** 游戏线程查询 Owner 是否仍为有效注册策略拥有者；空值或离场返回 false，不转移引用。 */
bool USKAbilitySystemComponent::IsResourcePolicyBoundTo(const UObject* Owner) const
{
    return IsInGameThread() && !bHasEndedPlay && ResourcePolicy && ResourcePolicyOwner.IsValid()
        && ResourcePolicyOwner.Get() == Owner;
}

/** 游戏线程查询同步提交边界；包含初始化以及拥有者的状态转换回调。 */
bool USKAbilitySystemComponent::IsResourceCommitActive() const
{
    return !IsInGameThread() || bApplyingNumericEffect || bInitializingAttributes
        || (ResourcePolicyOwner.IsValid() && ResourcePolicy && ResourcePolicy->IsResourcePolicyBusy());
}

/**
 * 游戏线程执行绑定策略拥有者授权的内部资源替换；不向蓝图提供绕过门禁开关。
 * @param PolicyOwner 必须与注册拥有者一致，函数不持有强引用。
 * @param TransitionSerial 非零当前流程编号，由策略验证。
 * @param TargetHealth 生命目标绝对值，有限且属于当前上限范围。
 * @param TargetPosture 躯干目标绝对值，有限且属于当前上限范围。
 * @return 实际快照；授权失败不修改资源，GE 异常报告 ResourceCommitFailed。
 */
FSKNumericResult USKAbilitySystemComponent::RestoreOwnedResources(
    UObject* PolicyOwner, int64 TransitionSerial, float TargetHealth, float TargetPosture)
{
    return ExecuteResourcePair(ESKNumericOperation::RestoreResources, TargetHealth, TargetPosture,
        IsInGameThread() ? GetAvatarActor() : nullptr, PolicyOwner, TransitionSerial);
}

/**
 * 游戏线程提交同一笔最终生命与躯干伤害，独立校验两个通道且只在提交结束评估状态。
 * @param HealthDamage 有限非负最终生命伤害，不再计算护甲。
 * @param PostureDamage 有限非负最终躯干伤害，两通道至少一个为正。
 * @param SourceActor 可空来源，不取得所有权。
 * @return 保留原请求与各通道实际量，局部拒绝不会丢弃另一合法通道。
 */
FSKNumericResult USKAbilitySystemComponent::ApplyResourceImpact(
    float HealthDamage, float PostureDamage, AActor* SourceActor)
{
    return ExecuteResourcePair(ESKNumericOperation::SurvivalImpact, HealthDamage, PostureDamage, SourceActor);
}

/**
 * 游戏线程提交已裁决且已计算护甲的最终生命伤害；不产生死亡或战斗命中事件。
 * @param Amount 正有限生命点数，不能为零。
 * @param SourceActor 可空来源，用于 GAS 上下文及执行记录，不取得所有权。
 * @return 请求及实际扣血记录，失败时 ActualAmount 为零。
 */
FSKNumericResult USKAbilitySystemComponent::ApplyHealthDamage(float Amount, AActor* SourceActor)
{
    return ExecuteNumeric(ESKNumericOperation::Damage, Amount, SourceActor);
}

/**
 * 游戏线程通过临时治疗属性恢复生命；本阶段不判定死亡或复活权限。
 * @param Amount 正有限生命点数。
 * @param SourceActor 可空治疗来源，不取得所有权。
 * @return 实际恢复记录，满血时返回 NoChange。
 */
FSKNumericResult USKAbilitySystemComponent::RestoreHealth(float Amount, AActor* SourceActor)
{
    return ExecuteNumeric(ESKNumericOperation::Healing, Amount, SourceActor);
}

/**
 * 游戏线程增加架势积累；不决定格挡、弹刀或架势崩溃。
 * @param Amount 正有限架势点数。
 * @param SourceActor 可空来源，不取得所有权。
 * @return 实际积累记录，达到上限时返回 NoChange。
 */
FSKNumericResult USKAbilitySystemComponent::ApplyPostureDamage(float Amount, AActor* SourceActor)
{
    return ExecuteNumeric(ESKNumericOperation::PostureDamage, Amount, SourceActor);
}

/**
 * 游戏线程减少架势积累；恢复时机和倍率由 Lua 编排。
 * @param Amount 正有限架势点数。
 * @param SourceActor 可空来源，不取得所有权。
 * @return 实际减少记录，架势已清空时返回 NoChange。
 */
FSKNumericResult USKAbilitySystemComponent::RestorePosture(float Amount, AActor* SourceActor)
{
    return ExecuteNumeric(ESKNumericOperation::PostureRecovery, Amount, SourceActor);
}

/** 游戏线程经恢复 GE 清空架势；不修改崩溃布尔值，已为零返回 NoChange。 */
FSKNumericResult USKAbilitySystemComponent::ResetPosture()
{
    if (!IsInGameThread())
    {
        FSKNumericResult Result;
        Result.Operation = ESKNumericOperation::ResetPosture;
        Result.Code = ESKNumericResultCode::InvalidInput;
        return Result;
    }
    return ExecuteNumeric(ESKNumericOperation::ResetPosture, GetAttributeSnapshot().Posture, GetAvatarActor());
}

/**
 * 游戏线程应用静态属性 Duration/Infinite 增益；禁止持续修改资源、执行器和周期型效果。
 * 该入口只接受本系统常驻统计属性；即时资源通过语义入口执行，叠加规则使用 GE 本身配置。
 * @param EffectClass 非空效果类。
 * @param Level 正有限效果等级。
 * @param SourceActor 可空来源；空时使用 Avatar。
 * @param OutHandle 成功输出活动效果句柄；失败重置为无效句柄。
 * @return 效果确实进入活动集合返回 true；配置错误、重入或免疫拒绝返回 false。
 */
bool USKAbilitySystemComponent::ApplyAttributeEffect(
    TSubclassOf<UGameplayEffect> EffectClass, float Level, AActor* SourceActor,
    FActiveGameplayEffectHandle& OutHandle)
{
    OutHandle = FActiveGameplayEffectHandle();
    if (!IsInGameThread() || bHasEndedPlay || (!bAttributesReady && !bInitializingAttributes)
        || bApplyingNumericEffect || (!bInitializingAttributes && IsResourceCommitActive())
        || !EffectClass || !FMath::IsFinite(Level) || Level <= 0.f) return false;
    const UGameplayEffect* Effect = EffectClass.GetDefaultObject();
    if (!IsAllowedAttributeEffect(Effect)) return false;

    TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
    FGameplayEffectContextHandle Context = MakeEffectContext();
    Context.AddInstigator(SourceActor ? SourceActor : GetAvatarActor(), SourceActor ? SourceActor : GetAvatarActor());
    FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(EffectClass, Level, Context);
    if (!Spec.IsValid()) return false;
    if (!FMath::IsFinite(Spec.Data->Period) || Spec.Data->Period != 0.f
        || !FMath::IsFinite(Spec.Data->GetDuration())) return false;
    Spec.Data->CalculateModifierMagnitudes();
    for (int32 Index = 0; Index < Spec.Data->Modifiers.Num(); ++Index)
    {
        if (!FMath::IsFinite(Spec.Data->GetModifierMagnitude(Index, false))) return false;
    }
    OutHandle = ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    const bool bApplied = OutHandle.IsValid() && GetActiveGameplayEffect(OutHandle) != nullptr;
    if (bAttributesReady) CompleteResourceCommit(nullptr);
    return bApplied;
}

/**
 * 游戏线程按活动效果句柄移除本 ASC 的单个增益，不手工反向运算属性。
 * @param Handle 必须属于本 ASC，过期或无效句柄拒绝。
 * @return 真正移除效果返回 true；重入或句柄无效返回 false。
 */
bool USKAbilitySystemComponent::RemoveAttributeEffect(FActiveGameplayEffectHandle Handle)
{
    if (!IsInGameThread() || bHasEndedPlay || IsResourceCommitActive() || !Handle.IsValid()
        || GetActiveGameplayEffect(Handle) == nullptr) return false;
    TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
    const bool bRemoved = RemoveActiveGameplayEffect(Handle, -1);
    CompleteResourceCommit(nullptr);
    return bRemoved;
}

/**
 * 游戏线程读取当前防御方护甲并查询显式曲线；不会提交伤害。
 * @param DamageBeforeArmor 非负有限的护甲前生命伤害，攻击倍率由调用方计算。
 * @param ArmorCurve 非空且至少有一个关键帧的曲线，横轴为护甲，纵轴为 [0,1] 伤害倍率；不持有引用。
 * @param OutDamage 成功输出曲线后的有限伤害；失败输出零。
 * @return 数据齐备且结果有限返回 true，否则 false，不静默选取默认公式。
 */
bool USKAbilitySystemComponent::CalculateDamageAfterArmor(
    float DamageBeforeArmor, const UCurveFloat* ArmorCurve, float& OutDamage) const
{
    OutDamage = 0.f;
    if (!IsInGameThread() || !bAttributesReady || !ArmorCurve
        || !FMath::IsFinite(DamageBeforeArmor) || DamageBeforeArmor < 0.f
        || ArmorCurve->FloatCurve.GetNumKeys() == 0) return false;
    const float Multiplier = ArmorCurve->GetFloatValue(GetAttributeSnapshot().Armor);
    const double Damage = static_cast<double>(DamageBeforeArmor) * Multiplier;
    if (!FMath::IsFinite(Multiplier) || Multiplier < 0.f || Multiplier > 1.f
        || !FMath::IsFinite(Damage) || Damage > MAX_flt) return false;
    OutDamage = static_cast<float>(Damage);
    return true;
}

/**
 * 游戏线程解除数值通知并禁止后续请求；不把卸载等同于死亡。
 * @param EndPlayReason 引擎传入的离场原因，透传父类。
 */
void USKAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bHasEndedPlay = true;
    bAttributesReady = false;
    for (const FGameplayAttribute& Attribute : ObservedAttributes)
    {
        GetGameplayAttributeValueChangeDelegate(Attribute).RemoveAll(this);
    }
    ObservedAttributes.Reset();
    InitialEffectHandles.Reset();
    ResourcePolicyOwner.Reset();
    ResourcePolicy = nullptr;
    PendingAttributeChanges.Reset();
    Super::EndPlay(EndPlayReason);
}

/**
 * 游戏线程执行初始化提交，完成前抑制公开属性通知；失败移除已施加的初始增益并保持未就绪。
 * 重试会覆盖本次写入的基础数值；不宣称具有可回滚的通用 GAS 事务语义。
 * @param Values 已配置的基础数值与资源绝对值。
 * @param Profile 可空附加初始效果配置，不持有引用。
 * @return 完整配置和全部增益应用成功返回 true；否则 false。
 */
bool USKAbilitySystemComponent::InitializeInternal(
    const FSKAttributeInitialization& Values, const USKAttributeProfile* Profile)
{
    if (!IsInGameThread() || bHasEndedPlay || bInitializingAttributes || bApplyingNumericEffect) return false;
    if (bAttributesReady) return true;
    if (bResourcePolicyRequired && (!ResourcePolicyOwner.IsValid() || !ResourcePolicy)) return false;
    if (!Values.IsValid() || !GetSet<USKCharacterAttributeSet>()
        || !GetOwnerActor() || !GetAvatarActor())
    {
        UE_LOG(LogTemp, Error, TEXT("GAS 属性初始化失败：%s 缺少有效配置、ActorInfo 或 AttributeSet"), *GetNameSafe(GetOwner()));
        return false;
    }
    if (Profile)
    {
        if (!FMath::IsFinite(Profile->EffectLevel) || Profile->EffectLevel <= 0.f) return false;
        for (TSubclassOf<UGameplayEffect> EffectClass : Profile->InitialEffects)
        {
            if (!EffectClass || !IsAllowedAttributeEffect(EffectClass.GetDefaultObject())) return false;
        }
    }

    TGuardValue<bool> InitializingGuard(bInitializingAttributes, true);
    BindAttributeDelegates();
    FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(USKNumericGameplayEffect::StaticClass(), 1.f, MakeEffectContext());
    if (!Spec.IsValid()) return false;
    Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Operation")), static_cast<float>(ESKNumericOperation::Initialize));
    Spec.Data->SetSetByCallerMagnitude(FName(TEXT("InitialHealth")), Values.InitialHealth);
    Spec.Data->SetSetByCallerMagnitude(FName(TEXT("InitialPosture")), Values.InitialPosture);
    for (const FGameplayAttribute& Attribute : USKCharacterAttributeSet::GetConfigAttributes())
    {
        const FFloatProperty* Field = FindFProperty<FFloatProperty>(FSKAttributeInitialization::StaticStruct(), *Attribute.GetName());
        if (!Field) return false;
        Spec.Data->SetSetByCallerMagnitude(FName(*Attribute.GetName()), Field->GetPropertyValue_InContainer(&Values));
    }
    {
        TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
        bNumericExecutionObserved = false;
        ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        if (!bNumericExecutionObserved) return false;
    }

    if (Profile)
    {
        for (TSubclassOf<UGameplayEffect> EffectClass : Profile->InitialEffects)
        {
            FActiveGameplayEffectHandle Handle;
            if (!ApplyAttributeEffect(EffectClass, Profile->EffectLevel, GetAvatarActor(), Handle))
            {
                for (FActiveGameplayEffectHandle AppliedHandle : InitialEffectHandles)
                {
                    RemoveActiveGameplayEffect(AppliedHandle, -1);
                }
                InitialEffectHandles.Reset();
                UE_LOG(LogTemp, Error, TEXT("GAS 初始增益应用失败：%s"), *GetNameSafe(EffectClass.Get()));
                return false;
            }
            InitialEffectHandles.Add(Handle);
        }
    }

    {
        TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
        Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Operation")), static_cast<float>(ESKNumericOperation::InitializeResources));
        bNumericExecutionObserved = false;
        ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        if (!bNumericExecutionObserved)
        {
            for (FActiveGameplayEffectHandle Handle : InitialEffectHandles) RemoveActiveGameplayEffect(Handle, -1);
            InitialEffectHandles.Reset();
            return false;
        }
    }

    const FSKAttributeSnapshot Snapshot = GetAttributeSnapshot();
    bAttributesReady = !bHasEndedPlay && FMath::IsFinite(Snapshot.MaxHealth) && Snapshot.MaxHealth > 0.f
        && FMath::IsFinite(Snapshot.MaxPosture) && Snapshot.MaxPosture > 0.f
        && FMath::IsFinite(Snapshot.Health) && Snapshot.Health >= 0.f && Snapshot.Health <= Snapshot.MaxHealth
        && FMath::IsFinite(Snapshot.Posture) && Snapshot.Posture >= 0.f && Snapshot.Posture <= Snapshot.MaxPosture;
    if (bAttributesReady)
    {
        TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
        CompleteResourceCommit(nullptr, true);
        if (!bHasEndedPlay) OnAttributesReady.Broadcast();
    }
    return bAttributesReady;
}

/**
 * 游戏线程经原生即时 GE 执行单次资源请求，同步通知期间拒绝递归数值写入。
 * @param Operation 资源请求类型；Initialize 仅内部初始化使用，不接受于此。
 * @param Amount 正有限请求量，仅 ResetPosture 允许零。
 * @param SourceActor 可空来源，不取得所有权。
 * @return 完整前后快照和实际变化，失败不修改属性；重入失败不递归发布事件。
 */
FSKNumericResult USKAbilitySystemComponent::ExecuteNumeric(
    ESKNumericOperation Operation, float Amount, AActor* SourceActor)
{
    FSKNumericResult Result;
    if (!IsInGameThread())
    {
        Result.Code = ESKNumericResultCode::InvalidInput;
        return Result;
    }
    Result.Operation = Operation;
    Result.RequestedAmount = Amount;
    Result.SourceActor = SourceActor;
    Result.TargetActor = GetAvatarActor();
    Result.Before = GetAttributeSnapshot();
    Result.After = Result.Before;
    if (IsResourceCommitActive())
    {
        Result.Code = ESKNumericResultCode::Reentrant;
        return Result;
    }
    TGuardValue<bool> ApplyingGuard(bApplyingNumericEffect, true);
    if (LastRequestId < MAX_int64) Result.RequestId = ++LastRequestId;
    if (bHasEndedPlay || !bAttributesReady)
    {
        Result.Code = ESKNumericResultCode::NotReady;
    }
    else if (Result.RequestId == 0 || !FMath::IsFinite(Amount) || Amount < 0.f
        || (Amount == 0.f && Operation != ESKNumericOperation::ResetPosture))
    {
        Result.Code = ESKNumericResultCode::InvalidInput;
    }
    else if (CheckPolicy(Operation) != ESKNumericResultCode::Applied)
    {
        Result.Code = CheckPolicy(Operation);
        Result.RejectionReason = ResourcePolicyOwner.IsValid() && ResourcePolicy
            ? ResourcePolicy->GetResourceRejectionReason(Operation) : FName(TEXT("PolicyMissing"));
    }
    else
    {
        FGameplayEffectContextHandle Context = MakeEffectContext();
        Context.AddInstigator(SourceActor ? SourceActor : GetAvatarActor(), SourceActor ? SourceActor : GetAvatarActor());
        FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(USKNumericGameplayEffect::StaticClass(), 1.f, Context);
        bNumericExecutionObserved = false;
        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Operation")), static_cast<float>(Operation));
            Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Amount")), Amount);
            ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }
        Result.After = GetAttributeSnapshot();
        const bool bHealthOperation = Operation == ESKNumericOperation::Damage || Operation == ESKNumericOperation::Healing;
        Result.ActualAmount = FMath::Abs(bHealthOperation
            ? Result.After.Health - Result.Before.Health : Result.After.Posture - Result.Before.Posture);
        Result.Code = !bNumericExecutionObserved ? ESKNumericResultCode::EffectRejected
            : (Result.ActualAmount > 0.f ? ESKNumericResultCode::Applied : ESKNumericResultCode::NoChange);
    }
    CompleteResourceCommit(&Result);
    if (!bHasEndedPlay) OnNumericExecuted.Broadcast(Result);
    return Result;
}

/**
 * 游戏线程执行双资源原生 GE，保留异常后的真实快照，不模拟通用事务回滚。
 * @param Operation 仅接受 SurvivalImpact 或 RestoreResources。
 * @param HealthValue 伤害量或目标生命，有限非负。
 * @param PostureValue 伤害量或目标躯干，有限非负。
 * @param SourceActor 可空上下文来源。
 * @param RestoreOwner 内部替换时的策略拥有者；普通伤害必须为空。
 * @param TransitionSerial 内部授权编号，普通伤害为零。
 * @return 各通道实际值及拒绝原因；状态总是按实际最终结果收敛。
 */
FSKNumericResult USKAbilitySystemComponent::ExecuteResourcePair(
    ESKNumericOperation Operation, float HealthValue, float PostureValue,
    AActor* SourceActor, UObject* RestoreOwner, int64 TransitionSerial)
{
    FSKNumericResult Result;
    Result.Operation = Operation;
    if (!IsInGameThread()) { Result.Code = ESKNumericResultCode::InvalidInput; return Result; }
    Result.SourceActor = SourceActor;
    Result.TargetActor = GetAvatarActor();
    Result.Before = GetAttributeSnapshot();
    Result.After = Result.Before;
    Result.RequestedHealthDamage = HealthValue;
    Result.RequestedPostureDamage = PostureValue;
    const bool bRestore = Operation == ESKNumericOperation::RestoreResources;
    if (bApplyingNumericEffect || bInitializingAttributes
        || (!bRestore && IsResourceCommitActive()))
    {
        Result.Code = ESKNumericResultCode::Reentrant;
        return Result;
    }
    TGuardValue<bool> Guard(bApplyingNumericEffect, true);
    if (LastRequestId < MAX_int64) Result.RequestId = ++LastRequestId;
    if (!bAttributesReady || bHasEndedPlay) Result.Code = ESKNumericResultCode::NotReady;
    else if (Result.RequestId == 0 || !FMath::IsFinite(HealthValue) || !FMath::IsFinite(PostureValue)
        || HealthValue < 0.f || PostureValue < 0.f || (!bRestore && HealthValue == 0.f && PostureValue == 0.f))
        Result.Code = ESKNumericResultCode::InvalidInput;
    else if (bRestore && (!ResourcePolicyOwner.IsValid() || ResourcePolicyOwner.Get() != RestoreOwner
        || !ResourcePolicy || !ResourcePolicy->CanRestoreOwnedResources(TransitionSerial)))
        Result.Code = ESKNumericResultCode::PolicyRejected;
    else if (bRestore && (HealthValue > Result.Before.MaxHealth || PostureValue > Result.Before.MaxPosture))
        Result.Code = ESKNumericResultCode::InvalidInput;
    else
    {
        Result.HealthChannelCode = bRestore || HealthValue == 0.f ? ESKNumericResultCode::Applied : CheckPolicy(ESKNumericOperation::Damage);
        Result.PostureChannelCode = bRestore || PostureValue == 0.f ? ESKNumericResultCode::Applied : CheckPolicy(ESKNumericOperation::PostureDamage);
        if (Result.HealthChannelCode != ESKNumericResultCode::Applied)
            Result.HealthRejectionReason = ResourcePolicyOwner.IsValid() && ResourcePolicy
                ? ResourcePolicy->GetResourceRejectionReason(ESKNumericOperation::Damage) : FName(TEXT("PolicyMissing"));
        if (Result.PostureChannelCode != ESKNumericResultCode::Applied)
            Result.PostureRejectionReason = ResourcePolicyOwner.IsValid() && ResourcePolicy
                ? ResourcePolicy->GetResourceRejectionReason(ESKNumericOperation::PostureDamage) : FName(TEXT("PolicyMissing"));
        const float AcceptedHealth = Result.HealthChannelCode == ESKNumericResultCode::Applied ? HealthValue : 0.f;
        const float AcceptedPosture = Result.PostureChannelCode == ESKNumericResultCode::Applied ? PostureValue : 0.f;
        if (!bRestore && AcceptedHealth == 0.f && AcceptedPosture == 0.f)
            Result.Code = ESKNumericResultCode::PolicyRejected;
        else
        {
            FGameplayEffectContextHandle Context = MakeEffectContext();
            Context.AddInstigator(SourceActor ? SourceActor : GetAvatarActor(), SourceActor ? SourceActor : GetAvatarActor());
            FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(USKNumericGameplayEffect::StaticClass(), 1.f, Context);
            bNumericExecutionObserved = false;
            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Operation")), static_cast<float>(Operation));
                Spec.Data->SetSetByCallerMagnitude(FName(bRestore ? TEXT("TargetHealth") : TEXT("HealthAmount")), AcceptedHealth);
                Spec.Data->SetSetByCallerMagnitude(FName(bRestore ? TEXT("TargetPosture") : TEXT("PostureAmount")), AcceptedPosture);
                ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
            }
            Result.After = GetAttributeSnapshot();
            Result.ActualHealthDamage = FMath::Max(0.f, Result.Before.Health - Result.After.Health);
            Result.ActualPostureDamage = FMath::Max(0.f, Result.After.Posture - Result.Before.Posture);
            const double TotalChange = static_cast<double>(FMath::Abs(Result.After.Health - Result.Before.Health))
                + FMath::Abs(Result.After.Posture - Result.Before.Posture);
            Result.ActualAmount = static_cast<float>(FMath::Min(TotalChange, static_cast<double>(MAX_flt)));
            const bool bTargetsMatch = !bRestore || (FMath::IsNearlyEqual(Result.After.Health, HealthValue)
                && FMath::IsNearlyEqual(Result.After.Posture, PostureValue));
            const float ExpectedHealth = FMath::Max(0.f, Result.Before.Health - AcceptedHealth);
            const float ExpectedPosture = static_cast<float>(FMath::Min(
                static_cast<double>(Result.Before.MaxPosture), static_cast<double>(Result.Before.Posture) + AcceptedPosture));
            const bool bImpactMatches = bRestore || (FMath::IsNearlyEqual(Result.After.Health, ExpectedHealth)
                && FMath::IsNearlyEqual(Result.After.Posture, ExpectedPosture));
            Result.Code = !bNumericExecutionObserved || !bTargetsMatch || !bImpactMatches ? ESKNumericResultCode::ResourceCommitFailed
                : (Result.ActualAmount > 0.f ? ESKNumericResultCode::Applied : ESKNumericResultCode::NoChange);
        }
    }
    CompleteResourceCommit(&Result);
    if (!bHasEndedPlay) OnNumericExecuted.Broadcast(Result);
    return Result;
}

/**
 * 游戏线程只读校验当前资源策略；无必需策略的独立数值宿主允许操作。
 * @param Operation 资源语义，不含内部恢复。
 * @return Applied 允许；其余为失败关闭原因，不写入状态。
 */
ESKNumericResultCode USKAbilitySystemComponent::CheckPolicy(ESKNumericOperation Operation) const
{
    if (!ResourcePolicyOwner.IsValid() || !ResourcePolicy)
        return bResourcePolicyRequired ? ESKNumericResultCode::PolicyRejected : ESKNumericResultCode::Applied;
    return ResourcePolicy->CheckResourceOperation(Operation);
}

/**
 * 游戏线程在一次受控提交结束后先收敛策略状态，再发布属性与语义事件。
 * @param Result 可空最终结算，空表示统计属性变化。
 * @param bInitial true 表示初始就绪；不补发历史死亡。
 */
void USKAbilitySystemComponent::CompleteResourceCommit(const FSKNumericResult* Result, bool bInitial)
{
    if (bHasEndedPlay || !bAttributesReady) return;
    if (ResourcePolicyOwner.IsValid() && ResourcePolicy) ResourcePolicy->ReconcileResourceState(Result, bInitial);
    TArray<FOnAttributeChangeData> Changes = MoveTemp(PendingAttributeChanges);
    PendingAttributeChanges.Reset();
    for (const FOnAttributeChangeData& Change : Changes)
    {
        if (bHasEndedPlay) break;
        OnAttributeChanged.Broadcast(Change.Attribute, Change.OldValue, Change.NewValue);
    }
    if (!bHasEndedPlay && ResourcePolicyOwner.IsValid() && ResourcePolicy) ResourcePolicy->FlushResourceEvents();
}

/** 游戏线程幂等绑定全部常驻属性；meta 属性不向外部发布。 */
void USKAbilitySystemComponent::BindAttributeDelegates()
{
    if (!ObservedAttributes.IsEmpty()) return;
    ObservedAttributes = USKCharacterAttributeSet::GetConfigAttributes();
    ObservedAttributes.Add(USKCharacterAttributeSet::GetHealthAttribute());
    ObservedAttributes.Add(USKCharacterAttributeSet::GetPostureAttribute());
    for (const FGameplayAttribute& Attribute : ObservedAttributes)
        GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this, &USKAbilitySystemComponent::HandleAttributeChanged);
}

/**
 * 游戏线程只缓存变化值；受控提交内推迟生命周期评估，外部属性变更立即关闭门禁。
 * @param Data GAS 修改后的数值，不保留 GEModData 指针。
 */
void USKAbilitySystemComponent::HandleAttributeChanged(const FOnAttributeChangeData& Data)
{
    if (!bAttributesReady || bHasEndedPlay) return;
    FOnAttributeChangeData Copy;
    Copy.Attribute = Data.Attribute;
    Copy.OldValue = Data.OldValue;
    Copy.NewValue = Data.NewValue;
    PendingAttributeChanges.Add(Copy);
    if (!bApplyingNumericEffect && !bInitializingAttributes)
    {
        TGuardValue<bool> Guard(bApplyingNumericEffect, true);
        CompleteResourceCommit(nullptr);
    }
}

/**
 * 游戏线程校验增益模板边界；拒绝资源持续修改、执行器与周期执行。
 * @param Effect 非空 GE 类默认对象，只读。
 * @return 只有生命上限/战斗统计属性的持续或无限效果返回 true；不允许普通 Override 竞争。
 */
bool USKAbilitySystemComponent::IsAllowedAttributeEffect(const UGameplayEffect* Effect) const
{
    if (!Effect || Effect->DurationPolicy == EGameplayEffectDurationType::Instant
        || !Effect->Executions.IsEmpty() || Effect->Period.GetValueAtLevel(1.f) != 0.f
        || !Effect->ConditionalGameplayEffects.IsEmpty() || !Effect->OverflowEffects.IsEmpty()
        || !Effect->PrematureExpirationEffectClasses.IsEmpty() || !Effect->RoutineExpirationEffectClasses.IsEmpty()
        || !Effect->GrantedAbilities.IsEmpty()) return false;
    for (const FGameplayModifierInfo& Modifier : Effect->Modifiers)
    {
        const FGameplayAttribute& Attribute = Modifier.Attribute;
        const bool bAllowed = USKCharacterAttributeSet::GetConfigAttributes().Contains(Attribute);
        if (!bAllowed || Modifier.ModifierOp == EGameplayModOp::Override) return false;
    }
    return true;
}
