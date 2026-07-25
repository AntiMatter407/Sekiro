// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/SKWeaponManagerComponent.h"

#include "Weapon/SKWeapon.h"
#include "Animation/SKAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Character/SKCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

/**
 * 创建原生武器管理组件并声明 PrePhysics Tick 能力，但保持 Tick 默认禁用。
 * 只有进入游戏 BeginPlay 后才按 Lua 开关启用 Tick，避免 CDO、编辑器预览和蓝图重实例化注册 Tick；
 * 构造阶段不加载、生成或引用任何具体项目武器和动画资产，只能在游戏线程创建组件时执行。
 */
USKWeaponManagerComponent::USKWeaponManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

/**
 * 设置后续 Tick 是否调用 Lua 武器管理逻辑；进入游戏生命周期后同步组件 Tick 启用状态。
 * BeginPlay 前仅保存配置，不注册 Tick；函数不会生成或销毁武器，必须在游戏线程调用。
 *
 * @param bNewUseLuaWeaponManagerLogic true 表示启用 Lua Tick，false 表示仅保留原生接口供外部调用。
 */
void USKWeaponManagerComponent::SetUseLuaWeaponManagerLogic(bool bNewUseLuaWeaponManagerLogic)
{
    bUseLuaWeaponManagerLogic = bNewUseLuaWeaponManagerLogic;
    if (HasBegunPlay())
    {
        SetComponentTickEnabled(bUseLuaWeaponManagerLogic);
    }
}

/**
 * 查询当前是否允许调用 Lua 武器管理逻辑；可在游戏线程只读逻辑中调用。
 *
 * @return 启用 Lua Tick 时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::IsUsingLuaWeaponManagerLogic() const
{
    return bUseLuaWeaponManagerLogic;
}

/**
 * 设置 UnLua require 使用的武器管理模块名；不会立即加载或执行模块。
 * 必须在游戏线程调用。
 *
 * @param ModuleName Lua 点分模块名；允许为空，空值会禁用模块调用。
 */
void USKWeaponManagerComponent::SetLuaWeaponManagerModuleName(const FString& ModuleName)
{
    LuaWeaponManagerModuleName = ModuleName;
}

/**
 * 查询当前配置的 Lua 武器管理模块名；不触发 require。
 *
 * @return 当前模块名字符串，未配置时为空。
 */
FString USKWeaponManagerComponent::GetLuaWeaponManagerModuleName() const
{
    return LuaWeaponManagerModuleName;
}

/**
 * 向 UnLua 返回本组件默认绑定的模块名；由 UnLua 在游戏线程查询。
 *
 * @return 当前 Lua 武器管理模块名，允许为空。
 */
FString USKWeaponManagerComponent::GetModuleName_Implementation() const
{
    return LuaWeaponManagerModuleName;
}

/**
 * 提供未绑定 Lua 时的空武器管理 Tick 回退，避免 C++ 手写查找和调用 Lua 模块。
 * 同名 Lua override 负责武器玩法编排；仅由组件 PrePhysics Tick 在游戏线程调用。
 *
 * @param DeltaTime 当前帧步长，单位秒；默认实现不消费该值。
 */
void USKWeaponManagerComponent::HandleWeaponManagerTick_Implementation(float DeltaTime)
{
    (void)DeltaTime;
}

/**
 * 增加一个禁战区域引用计数，仅发布区域状态供 Lua 编排。
 * 本函数不切换武器展示、不播放动画，也不修改角色移动；必须在游戏线程调用。
 */
void USKWeaponManagerComponent::EnterRestrictedZone()
{
    RestrictedZoneCount++;
}

/**
 * 减少一个禁战区域引用计数；计数归零不主动触发拔刀或恢复输入。
 * 多余离开调用会被限制在零并记录告警，必须在游戏线程调用。
 */
void USKWeaponManagerComponent::ExitRestrictedZone()
{
    if (RestrictedZoneCount <= 0)
    {
        RestrictedZoneCount = 0;
        UE_LOG(LogTemp, Warning, TEXT("SKWeaponManager received unmatched restricted-zone exit. Owner=%s"), *GetNameSafe(GetOwner()));
        return;
    }

    RestrictedZoneCount--;
}

/**
 * 查询角色是否位于至少一个禁战区域，不修改武器或动画状态。
 *
 * @return 区域引用计数大于零时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::IsRestrictedZoneActive() const
{
    return RestrictedZoneCount > 0;
}

/**
 * 查询当前成对登记的禁战区域数量，不修改运行时状态。
 *
 * @return 非负区域计数；多个区域重叠时可能大于一。
 */
int32 USKWeaponManagerComponent::GetRestrictedZoneCount() const
{
    return RestrictedZoneCount;
}

/**
 * 判断所属角色是否已经处于可安全播放收刀或拔刀上半身动画的稳定移动状态。
 * 条件要求角色站立、落地、非 Dodge/Step，且项目 AnimInstance 当前 Gait 为 Idle 或 Walk；
 * 本函数只读状态，不播放动画或切换挂载，可在游戏线程 Lua Tick 中调用。
 *
 * @return 所有稳定条件均满足时返回 true；Owner、移动组件或项目 AnimInstance 缺失时返回 false。
 */
bool USKWeaponManagerComponent::IsOwnerReadyForRestrictedWeaponTransition() const
{
    const ASKCharacter* Character = Cast<ASKCharacter>(GetOwner());
    const UCharacterMovementComponent* MovementComponent = Character
        ? Character->GetCharacterMovement()
        : nullptr;
    const USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
    const USKAnimInstance* AnimInstance = CharacterMesh
        ? Cast<USKAnimInstance>(CharacterMesh->GetAnimInstance())
        : nullptr;
    if (!Character || !MovementComponent || !AnimInstance) return false;

    const bool bStableGait = AnimInstance->Gait == ESKAnimGait::Idle
        || AnimInstance->Gait == ESKAnimGait::Walk;
    return !Character->bIsCrouched
        && !MovementComponent->IsFalling()
        && !Character->IsDodging()
        && !AnimInstance->bIsDodging
        && AnimInstance->Stance == ESKAnimStance::Standing
        && AnimInstance->GroundedEntryState != ESKAnimGroundedEntryState::DodgeStep
        && bStableGait;
}

/**
 * 按 Lua 提供的软类路径生成 ASKWeapon，并用角色网格及三个外部挂点初始化挂载。
 * 函数不提供默认资源或默认挂点，也不播放动画；新实例完全初始化后才替换并销毁旧武器。
 * 必须在游戏线程调用。
 *
 * @param WeaponClassPath ASKWeapon 蓝图生成类或原生类的完整软类路径，不可为空。
 * @param HandSocket 拔刀状态下刀身优先挂载的角色 Socket 或骨骼名，允许为 None。
 * @param HandBoneFallback HandSocket 不存在时使用的角色骨骼名，允许为 None。
 * @param SheathSocket 刀鞘及收刀刀身挂载的角色 Socket 或骨骼名，允许为 None。
 * @return 武器类加载、Actor 生成和初始化均完成时返回 true；任一依赖无效时返回 false，旧武器保持不变。
 */
bool USKWeaponManagerComponent::SpawnWeaponByClassPath(
    const FString& WeaponClassPath,
    FName HandSocket,
    FName HandBoneFallback,
    FName SheathSocket)
{
    if (WeaponClassPath.IsEmpty()) return false;

    UClass* WeaponClass = FSoftClassPath(WeaponClassPath).TryLoadClass<ASKWeapon>();
    USkeletalMeshComponent* CharacterMesh = ResolveCharacterMesh();
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    UWorld* World = GetWorld();
    if (!WeaponClass || !CharacterMesh || !OwnerCharacter || !World)
    {
        UE_LOG(LogTemp, Warning, TEXT("SKWeaponManager failed to spawn weapon. Class=%s"), *WeaponClassPath);
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = OwnerCharacter;
    SpawnParameters.Instigator = OwnerCharacter;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ASKWeapon* SpawnedWeapon = World->SpawnActor<ASKWeapon>(WeaponClass, SpawnParameters);
    if (!SpawnedWeapon) return false;

    SpawnedWeapon->InitializeAttachments(CharacterMesh, HandSocket, HandBoneFallback, SheathSocket);

    ASKWeapon* PreviousWeapon = CurrentWeapon.Get();
    CurrentWeapon = SpawnedWeapon;
    if (PreviousWeapon && PreviousWeapon != SpawnedWeapon)
    {
        PreviousWeapon->DeactivateHitbox();
        PreviousWeapon->Destroy();
    }
    return true;
}

/**
 * 关闭攻击碰撞并销毁当前管理的武器 Actor；重复调用安全，不影响角色动画。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void USKWeaponManagerComponent::DestroyCurrentWeapon()
{
    if (!CurrentWeapon) return;

    CurrentWeapon->DeactivateHitbox();
    CurrentWeapon->Destroy();
    CurrentWeapon = nullptr;
}

/**
 * 返回当前管理的武器实例，不转移所有权且不触发资源加载。
 *
 * @return 当前有效武器；尚未生成或已经销毁时返回 nullptr。
 */
ASKWeapon* USKWeaponManagerComponent::GetCurrentWeapon() const
{
    return CurrentWeapon;
}

/**
 * 按 Lua 提供的 Drawn 或 Sheathed 名称原子切换刀身挂载；不播放动画，也不推断切换帧。
 * 必须在游戏线程调用。
 *
 * @param PresentationName 目标展示名，不区分大小写，仅接受 Drawn 或 Sheathed。
 * @return 名称有效且当前武器成功完成挂载时返回 true；否则返回 false。
 */
bool USKWeaponManagerComponent::SetWeaponPresentationByName(FName PresentationName)
{
    if (!CurrentWeapon) return false;

    const FString NormalizedName = PresentationName.ToString().ToLower();
    if (NormalizedName == TEXT("drawn"))
    {
        return CurrentWeapon->SetWeaponPresentation(ESKWeaponPresentation::Drawn);
    }
    if (NormalizedName == TEXT("sheathed"))
    {
        return CurrentWeapon->SetWeaponPresentation(ESKWeaponPresentation::Sheathed);
    }

    UE_LOG(LogTemp, Warning, TEXT("SKWeaponManager unsupported presentation: %s"), *PresentationName.ToString());
    return false;
}

/**
 * 将 AnimNotify 提供的通用事件转发给可由 Blueprint 或 UnLua 覆盖的反射入口。
 * 函数不解释事件名、不切换武器状态，也不保留动画引用；未启用脚本逻辑时保持当前状态。
 * 必须在游戏线程调用。
 *
 * @param EventName 由动画资产配置的非空事件语义名称，转换为稳定字符串后传给 Lua。
 * @param Animation 触发通知的动画资源，允许为空且不转移所有权。
 * @return 覆盖入口成功处理事件时返回 true；脚本逻辑被禁用、事件名为空或未处理时返回 false。
 */
bool USKWeaponManagerComponent::DispatchWeaponAnimationEvent(
    FName EventName,
    UAnimSequenceBase* Animation)
{
    if (!bUseLuaWeaponManagerLogic || EventName.IsNone())
    {
        return false;
    }

    return HandleWeaponAnimationEvent(EventName.ToString(), Animation);
}

/**
 * 提供未被 Blueprint 或 UnLua 覆盖时的武器动画通知回退。
 * 默认实现不解释事件、不改变展示状态，也不保留动画引用；仅由游戏线程上的通知转发入口调用。
 *
 * @param EventName 动画资产配置的事件语义名称；默认实现不消费。
 * @param Animation 触发通知的动画资源，允许为空且不转移所有权；默认实现不消费。
 * @return 默认返回 false，表示没有原生业务处理该事件。
 */
bool USKWeaponManagerComponent::HandleWeaponAnimationEvent_Implementation(
    const FString& EventName,
    UAnimSequenceBase* Animation)
{
    (void)EventName;
    (void)Animation;
    return false;
}

/**
 * 同步加载 Lua 提供的动画软路径，并以单节点模式在角色网格上播放。
 * 非循环动画结束时可保持末帧或恢复动画蓝图；函数不会切换武器挂载，事件时刻由 Lua 决定。
 * 必须在游戏线程调用。
 *
 * @param AnimationPath UAnimSequence 的完整软对象路径，不可为空。
 * @param bLooping true 时循环播放并忽略末帧保持选项，直至显式恢复动画蓝图。
 * @param bHoldLastFrame 非循环动画结束后为 true 时冻结末帧，为 false 时自动恢复动画蓝图。
 * @return 动画加载成功且已提交至角色网格播放时返回 true；路径或角色网格无效时返回 false。
 */
bool USKWeaponManagerComponent::PlayCharacterAnimationByPath(
    const FString& AnimationPath,
    bool bLooping,
    bool bHoldLastFrame)
{
    UAnimSequence* Animation = LoadAnimation(AnimationPath);
    USkeletalMeshComponent* CharacterMesh = ResolveCharacterMesh();
    UWorld* World = GetWorld();
    if (!Animation || !CharacterMesh || !World) return false;

    FTimerManager& TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(AnimationFinishTimer);

    CurrentPreviewAnimation = Animation;
    bHoldCurrentAnimationLastFrame = !bLooping && bHoldLastFrame;
    CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    CharacterMesh->PlayAnimation(Animation, bLooping);

    if (!bLooping)
    {
        TimerManager.SetTimer(
            AnimationFinishTimer,
            this,
            &USKWeaponManagerComponent::FinishCharacterAnimationPreview,
            FMath::Max(Animation->GetPlayLength(), 0.01f),
            false);
    }
    return true;
}

/**
 * 查询角色是否仍由本组件置于单节点动画模式；保持末帧时也视为活动预览。
 * 本函数不推进动画或修改状态。
 *
 * @return 已缓存角色网格且其动画模式为 AnimationSingleNode 时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::IsCharacterAnimationPreviewActive() const
{
    const USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    return CharacterMesh && CharacterMesh->GetAnimationMode() == EAnimationMode::AnimationSingleNode;
}

/**
 * 查询当前单节点动画的播放位置，供 Lua 依据原版事件边界切换武器挂载。
 * 本函数只读动画实例且应在游戏线程调用。
 *
 * @return 当前播放位置，单位为秒；不存在单节点实例时返回 0。
 */
float USKWeaponManagerComponent::GetCharacterAnimationPosition() const
{
    const USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    const UAnimSingleNodeInstance* SingleNodeInstance = CharacterMesh
        ? CharacterMesh->GetSingleNodeInstance()
        : nullptr;
    return SingleNodeInstance ? SingleNodeInstance->GetCurrentTime() : 0.f;
}

/**
 * 结束单节点预览并恢复首次缓存的 AnimInstance 类，同时清除动画结束定时器。
 * 函数不切换武器展示状态，重复调用安全，必须在游戏线程调用。
 */
void USKWeaponManagerComponent::RestoreCharacterAnimationBlueprint()
{
    UWorld* World = GetWorld();
    if (World)
    {
        World->GetTimerManager().ClearTimer(AnimationFinishTimer);
    }

    USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    CurrentPreviewAnimation = nullptr;
    bHoldCurrentAnimationLastFrame = false;
    if (!CharacterMesh) return;

    CharacterMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    if (CachedAnimInstanceClass)
    {
        CharacterMesh->SetAnimInstanceClass(CachedAnimInstanceClass);
    }
}

/**
 * 按名称写入角色当前 AnimInstance 上的浮点反射属性，供 Lua 在播放动作前发布动画图控制量。
 * 本函数只接受当前生成类真实存在的 float 属性，不创建字段、不解释属性业务含义，必须在游戏线程调用。
 *
 * @param PropertyName AnimInstance 生成类上的非空浮点属性名。
 * @param Value 要立即写入实例的浮点值；函数不做范围限制。
 * @return 找到有效 AnimInstance 与同名 float 属性并完成写入时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::SetCharacterAnimFloatPropertyByName(
    FName PropertyName,
    float Value)
{
    USkeletalMeshComponent* CharacterMesh = ResolveCharacterMesh();
    UAnimInstance* AnimInstance = CharacterMesh ? CharacterMesh->GetAnimInstance() : nullptr;
    FFloatProperty* FloatProperty = AnimInstance && !PropertyName.IsNone()
        ? FindFProperty<FFloatProperty>(AnimInstance->GetClass(), PropertyName)
        : nullptr;
    if (!FloatProperty)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("SKWeaponManager AnimInstance float property missing. Property=%s AnimInstance=%s"),
            *PropertyName.ToString(),
            *GetNameSafe(AnimInstance));
        return false;
    }

    FloatProperty->SetPropertyValue_InContainer(AnimInstance, Value);
    return true;
}

/**
 * 按 Lua 提供的动画软路径和 Slot 名创建动态 Montage，在现有 AnimBlueprint 上播放上半身动作。
 * 函数会恢复可能存在的 SingleNode 预览模式，并禁用该 Montage 实例的 Root Motion；
 * 下半身混合范围由 AnimBlueprint 中对应 Slot 的骨骼分层决定，本函数不切换武器展示。
 * 必须在游戏线程调用。
 *
 * @param AnimationPath UAnimSequence 的完整软对象路径，不可为空。
 * @param SlotName AnimBlueprint 中由 Lua 选择的 Slot 名，不可为 None。
 * @param BlendInTime 淡入时长，单位秒；负值限制为零。
 * @param BlendOutTime 淡出时长，单位秒；负值限制为零。
 * @param PlayRate 播放倍率；绝对值过小时按 1 处理，允许负值反向播放。
 * @param LoopCount 播放循环次数；小于一时限制为一。
 * @return 动画与 AnimInstance 有效且动态 Montage 成功开始时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::PlayCharacterSlotAnimationByPath(
    const FString& AnimationPath,
    FName SlotName,
    float BlendInTime,
    float BlendOutTime,
    float PlayRate,
    int32 LoopCount)
{
    UAnimSequence* Animation = LoadAnimation(AnimationPath);
    USkeletalMeshComponent* CharacterMesh = ResolveCharacterMesh();
    if (!Animation || !CharacterMesh || SlotName.IsNone()) return false;

    if (CharacterMesh->GetAnimationMode() != EAnimationMode::AnimationBlueprint)
    {
        RestoreCharacterAnimationBlueprint();
    }

    UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance();
    if (!AnimInstance) return false;

    const float ResolvedPlayRate = FMath::Abs(PlayRate) <= UE_SMALL_NUMBER ? 1.f : PlayRate;
    UAnimMontage* DynamicMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
        Animation,
        SlotName,
        FMath::Max(0.f, BlendInTime),
        FMath::Max(0.f, BlendOutTime),
        ResolvedPlayRate,
        FMath::Max(1, LoopCount));
    if (!DynamicMontage) return false;

    ActiveSlotMontage = DynamicMontage;
    return true;
}

/**
 * 淡出并停止本组件最近启动的动态 Slot Montage，并清除弱引用。
 * 函数不停止其他系统的 Montage、不恢复 SingleNode 模式，也不切换武器展示；必须在游戏线程调用。
 *
 * @param BlendOutTime 停止淡出时长，单位秒；负值限制为零。
 */
void USKWeaponManagerComponent::StopCharacterSlotAnimation(float BlendOutTime)
{
    UAnimMontage* Montage = ActiveSlotMontage.Get();
    USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    UAnimInstance* AnimInstance = CharacterMesh ? CharacterMesh->GetAnimInstance() : nullptr;
    if (Montage && AnimInstance)
    {
        AnimInstance->Montage_Stop(FMath::Max(0.f, BlendOutTime), Montage);
    }
    ActiveSlotMontage = nullptr;
}

/**
 * 查询本组件最近启动的动态 Slot Montage 是否仍在播放或混合。
 * 本函数只读 AnimInstance，不推进动画，适合 Lua Tick 的结束检测。
 *
 * @return Montage 与 AnimInstance 均有效且 Montage_IsPlaying 返回 true 时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::IsCharacterSlotAnimationPlaying() const
{
    const UAnimMontage* Montage = ActiveSlotMontage.Get();
    const USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    const UAnimInstance* AnimInstance = CharacterMesh ? CharacterMesh->GetAnimInstance() : nullptr;
    return Montage && AnimInstance && AnimInstance->Montage_IsPlaying(Montage);
}

/**
 * 查询本组件最近启动的动态 Slot Montage 当前播放位置。
 * 本函数只读 AnimInstance，不推进动画或触发 Notify。
 *
 * @return 当前 Montage 位置，单位秒；Montage 或 AnimInstance 无效时返回零。
 */
float USKWeaponManagerComponent::GetCharacterSlotAnimationPosition() const
{
    const UAnimMontage* Montage = ActiveSlotMontage.Get();
    const USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    const UAnimInstance* AnimInstance = CharacterMesh ? CharacterMesh->GetAnimInstance() : nullptr;
    return Montage && AnimInstance ? AnimInstance->Montage_GetPosition(Montage) : 0.f;
}

/**
 * 开启当前武器的攻击碰撞；不清空命中记录，也不控制动画或挂载。
 * 必须在游戏线程调用。
 *
 * @return 存在当前武器并已调用其碰撞接口时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::ActivateWeaponHitbox()
{
    if (!CurrentWeapon) return false;
    CurrentWeapon->ActivateHitbox();
    return true;
}

/**
 * 关闭当前武器的攻击碰撞；不清空命中记录，也不控制动画或挂载。
 * 必须在游戏线程调用。
 *
 * @return 存在当前武器并已调用其碰撞接口时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::DeactivateWeaponHitbox()
{
    if (!CurrentWeapon) return false;
    CurrentWeapon->DeactivateHitbox();
    return true;
}

/**
 * 清空当前武器本轮攻击的目标去重记录；不启用或关闭碰撞。
 * 必须在游戏线程调用。
 *
 * @return 存在当前武器并已清空记录时返回 true，否则返回 false。
 */
bool USKWeaponManagerComponent::ClearWeaponHitActors()
{
    if (!CurrentWeapon) return false;
    CurrentWeapon->ClearHitActors();
    return true;
}

/**
 * 缓存角色网格，为纯原生 UnLua 组件补发一次 ReceiveBeginPlay，并在初始化完成后按 Lua 开关启用 Tick。
 * 蓝图生成类或非原生类沿用引擎派发以避免重复；本函数不自动生成武器。
 * 构造、CDO、编辑器预览和蓝图编译阶段保持 Tick 禁用；仅由引擎在游戏线程调用。
 */
void USKWeaponManagerComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();
    ResolveCharacterMesh();

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();

    SetComponentTickEnabled(bUseLuaWeaponManagerLogic);
}

/**
 * 第一时间禁用组件 Tick，再停止动态 Slot Montage、清理动画定时器、恢复角色动画蓝图并销毁管理的武器。
 * 禁用动作确保 EndPlay 清理期间不会再次进入 Lua；函数避免结束游戏时遗留 Actor。
 * 仅由引擎在游戏线程调用。
 *
 * @param EndPlayReason 引擎提供的结束原因，仅转发给父类且不保留引用。
 */
void USKWeaponManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SetComponentTickEnabled(false);
    StopCharacterSlotAnimation(0.f);
    RestoreCharacterAnimationBlueprint();
    DestroyCurrentWeapon();
    Super::EndPlay(EndPlayReason);
}

/**
 * 将本帧 DeltaTime 显式交给 Lua 武器模块 Tick；Lua 失败时保持现状且不生成默认武器。
 * 仅由引擎在游戏线程调用。
 *
 * @param DeltaTime 本帧游戏时间增量，单位秒。
 * @param TickType 引擎 Tick 类型，仅转发给父类。
 * @param ThisTickFunction 当前 Tick 函数上下文，可为空且不保留引用。
 */
void USKWeaponManagerComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bUseLuaWeaponManagerLogic)
    {
        HandleWeaponManagerTick(DeltaTime);
    }
}

/**
 * 解析 Owner 的角色网格，并在首次处于动画蓝图模式时缓存其 AnimInstance 类。
 * 必须在游戏线程调用；函数保留弱网格引用和动画类引用，不接管组件所有权。
 *
 * @return 可用角色网格；Owner 不是角色或网格无效时返回 nullptr。
 */
USkeletalMeshComponent* USKWeaponManagerComponent::ResolveCharacterMesh()
{
    if (CachedCharacterMesh.IsValid()) return CachedCharacterMesh.Get();

    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* CharacterMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
    if (!CharacterMesh) return nullptr;

    CachedCharacterMesh = CharacterMesh;
    if (CharacterMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint)
    {
        CachedAnimInstanceClass = CharacterMesh->GetAnimClass();
    }
    return CharacterMesh;
}

/**
 * 同步加载 Lua 提供的动画软对象路径并校验 UAnimSequence 类型。
 * 必须在游戏线程调用，不缓存失败路径。
 *
 * @param AnimationPath 完整动画软对象路径，不可为空。
 * @return 成功加载的动画对象；路径为空、资源不存在或类型不匹配时返回 nullptr。
 */
UAnimSequence* USKWeaponManagerComponent::LoadAnimation(const FString& AnimationPath) const
{
    if (AnimationPath.IsEmpty()) return nullptr;

    UObject* LoadedObject = FSoftObjectPath(AnimationPath).TryLoad();
    UAnimSequence* Animation = Cast<UAnimSequence>(LoadedObject);
    if (!Animation)
    {
        UE_LOG(LogTemp, Warning, TEXT("SKWeaponManager failed to load animation: %s"), *AnimationPath);
    }
    return Animation;
}

/**
 * 处理非循环单节点动画结束：按调用参数冻结末帧，或恢复角色动画蓝图。
 * 定时器仅在游戏线程触发；函数不改变武器挂载状态。
 */
void USKWeaponManagerComponent::FinishCharacterAnimationPreview()
{
    if (!bHoldCurrentAnimationLastFrame)
    {
        RestoreCharacterAnimationBlueprint();
        return;
    }

    USkeletalMeshComponent* CharacterMesh = CachedCharacterMesh.Get();
    UAnimSequence* Animation = CurrentPreviewAnimation.Get();
    UAnimSingleNodeInstance* SingleNodeInstance = CharacterMesh
        ? CharacterMesh->GetSingleNodeInstance()
        : nullptr;
    if (!SingleNodeInstance || !Animation) return;

    SingleNodeInstance->SetPosition(Animation->GetPlayLength(), false);
    SingleNodeInstance->SetPlaying(false);
}
