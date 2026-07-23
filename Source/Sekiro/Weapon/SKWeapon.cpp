// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"

/**
 * 创建刀身、刀鞘和攻击碰撞组件，并建立武器内部的默认父子关系。
 * 资产由蓝图配置；构造过程不加载项目资源，只能在游戏线程创建 Actor 时执行。
 */
ASKWeapon::ASKWeapon()
{
    PrimaryActorTick.bCanEverTick = false;

    BladeMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BladeMesh"));
    SetRootComponent(BladeMesh);

    SheathMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SheathMesh"));
    SheathMesh->SetupAttachment(BladeMesh);

    AttackHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("AttackHitbox"));
    AttackHitbox->SetupAttachment(BladeMesh, BladeHitboxSocket);
    AttackHitbox->SetCapsuleSize(HitboxRadius, HitboxHalfHeight);
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttackHitbox->SetCollisionObjectType(ECC_WorldDynamic);
    AttackHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
    AttackHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    AttackHitbox->SetGenerateOverlapEvents(true);
    AttackHitbox->CanCharacterStepUpOn = ECB_No;
    AttackHitbox->OnComponentBeginOverlap.AddDynamic(this, &ASKWeapon::OnHitboxOverlap);
}

/**
 * 保存角色网格与三个外部挂点，并立即按当前展示状态完成首次挂载。
 * 本函数只维护组件挂载，不负责生成武器、播放动画或验证资产内容，必须在游戏线程调用。
 *
 * @param InCharacterMesh 角色的 SkeletalMeshComponent；函数保留 UObject 引用，不接管所有权，不可为空。
 * @param InHandSocket 刀身拔出时优先使用的角色 Socket 或骨骼名称；允许为 None，此时使用回退骨骼。
 * @param InHandBoneFallback 右手挂点不存在时使用的角色骨骼名称；允许为 None，但会导致挂载失败。
 * @param InSheathSocket 刀鞘使用的角色 Socket 或骨骼名称；允许为 None，但会导致挂载失败。
 */
void ASKWeapon::InitializeAttachments(
    USkeletalMeshComponent* InCharacterMesh,
    FName InHandSocket,
    FName InHandBoneFallback,
    FName InSheathSocket)
{
    CharacterMesh = InCharacterMesh;
    HandSocket = InHandSocket;
    HandBoneFallback = InHandBoneFallback;
    SheathSocket = InSheathSocket;

    if (!CharacterMesh) return;

    AttachSheathToBody();
    SetWeaponPresentation(Presentation);
}

/**
 * 将刀身按原版 Dummy 20 的父骨骼相对变换挂到角色右手；优先选择配置挂点，不存在时回退到右手骨骼。
 * 本函数不改变刀鞘位置、不播放动画，必须在游戏线程调用。
 *
 * @return 成功提交组件挂载时返回 true；角色网格、刀身或有效右手挂点缺失时返回 false。
 */
bool ASKWeapon::AttachBladeToHand()
{
    if (!CharacterMesh || !BladeMesh) return false;

    FName TargetSocket = HandSocket;
    const bool bHasHandAttachPoint = !TargetSocket.IsNone()
        && (CharacterMesh->DoesSocketExist(TargetSocket) || CharacterMesh->GetBoneIndex(TargetSocket) != INDEX_NONE);
    if (!bHasHandAttachPoint)
    {
        TargetSocket = HandBoneFallback;
    }
    const bool bHasFallbackAttachPoint = !TargetSocket.IsNone()
        && (CharacterMesh->DoesSocketExist(TargetSocket) || CharacterMesh->GetBoneIndex(TargetSocket) != INDEX_NONE);
    if (!bHasFallbackAttachPoint) return false;

    const bool bAttached = BladeMesh->AttachToComponent(
        CharacterMesh,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale,
        TargetSocket);
    if (bAttached)
    {
        BladeMesh->SetRelativeTransform(BladeHandAttachOffset);
    }
    return bAttached;
}

/**
 * 将刀身按原版 Dummy 147 的父骨骼相对变换直接挂到角色收刀挂点，为后续收刀事件提供原子切换。
 * 原版刀身和刀鞘分别吸附到同一个角色 Dummy，本函数不依赖拆分后刀鞘资产的内部骨骼，必须在游戏线程调用。
 *
 * @return 成功提交组件挂载时返回 true；角色网格、刀身或收刀挂点缺失时返回 false。
 */
bool ASKWeapon::AttachBladeToSheath()
{
    if (!CharacterMesh || !BladeMesh || SheathSocket.IsNone()) return false;
    const bool bHasAttachPoint = CharacterMesh->DoesSocketExist(SheathSocket)
        || CharacterMesh->GetBoneIndex(SheathSocket) != INDEX_NONE;
    if (!bHasAttachPoint) return false;

    const bool bAttached = BladeMesh->AttachToComponent(
        CharacterMesh,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale,
        SheathSocket);
    if (bAttached)
    {
        BladeMesh->SetRelativeTransform(BladeSheathAttachOffset);
    }
    return bAttached;
}

/**
 * 将刀鞘按原版 Dummy 147 的父骨骼相对变换挂到角色腰部的配置挂点。
 * 本函数不改变刀身展示状态、不播放动画，必须在游戏线程调用。
 *
 * @return 成功提交组件挂载时返回 true；角色网格、刀鞘或腰部挂点缺失时返回 false。
 */
bool ASKWeapon::AttachSheathToBody()
{
    if (!CharacterMesh || !SheathMesh || SheathSocket.IsNone()) return false;
    const bool bHasAttachPoint = CharacterMesh->DoesSocketExist(SheathSocket)
        || CharacterMesh->GetBoneIndex(SheathSocket) != INDEX_NONE;
    if (!bHasAttachPoint) return false;

    const bool bAttached = SheathMesh->AttachToComponent(
        CharacterMesh,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale,
        SheathSocket);
    if (bAttached)
    {
        SheathMesh->SetRelativeTransform(SheathBodyAttachOffset);
    }
    return bAttached;
}

/**
 * 在拔刀和收刀展示之间切换刀身父组件，并仅在挂载成功后更新状态。
 * 本函数不生成 Actor、不播放动画，也不控制攻击碰撞，必须在游戏线程调用。
 * 换挂前先把刀身子组件刷新到当前骨骼 Pose；成功后输出前后世界 Transform 差值，供动画 IK 连续性验收。
 *
 * @param NewPresentation 目标展示状态；Drawn 挂右手 Dummy 20，Sheathed 挂角色收刀 Dummy 147。
 * @return 目标挂载成功时返回 true；依赖组件或挂点无效时返回 false，原状态保持不变。
 */
bool ASKWeapon::SetWeaponPresentation(ESKWeaponPresentation NewPresentation)
{
    if (BladeMesh) BladeMesh->UpdateComponentToWorld();
    const FTransform BladeTransformBefore = BladeMesh
        ? BladeMesh->GetComponentTransform()
        : FTransform::Identity;
    const bool bAttached = NewPresentation == ESKWeaponPresentation::Drawn
        ? AttachBladeToHand()
        : AttachBladeToSheath();
    if (!bAttached) return false;

    const FTransform BladeTransformAfter = BladeMesh->GetComponentTransform();
    const float LocationDelta = FVector::Distance(
        BladeTransformBefore.GetLocation(),
        BladeTransformAfter.GetLocation());
    const float RotationDelta = FMath::RadiansToDegrees(
        BladeTransformBefore.GetRotation().AngularDistance(
            BladeTransformAfter.GetRotation()));
    UE_LOG(
        LogTemp,
        Display,
        TEXT("SKWeapon presentation=%d attach_delta_cm=%.6f attach_delta_deg=%.6f"),
        static_cast<int32>(NewPresentation),
        LocationDelta,
        RotationDelta);

    Presentation = NewPresentation;
    return true;
}

/**
 * 查询当前已经成功应用的武器展示状态，可在任意只读游戏逻辑中调用。
 *
 * @return 当前拔刀或收刀展示状态；不会触发组件挂载或其他副作用。
 */
ESKWeaponPresentation ASKWeapon::GetWeaponPresentation() const
{
    return Presentation;
}

/**
 * 开启刀身攻击碰撞的查询能力；不清空已命中集合，也不计算攻击伤害。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void ASKWeapon::ActivateHitbox()
{
    if (!AttackHitbox) return;
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

/**
 * 关闭刀身攻击碰撞；不清空命中集合，也不改变武器展示状态。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void ASKWeapon::DeactivateHitbox()
{
    if (!AttackHitbox) return;
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

/**
 * 清空单次攻击的去重集合，使后续碰撞能够再次命中相同 Actor。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void ASKWeapon::ClearHitActors()
{
    AlreadyHitActors.Empty();
}

/**
 * 处理攻击碰撞开始重叠，对有效且未命中过的 Actor 应用一次基础点伤害并记录去重。
 * 委托由游戏线程触发；函数不负责阵营筛选、攻击窗口或展示状态切换。
 *
 * @param Overlapped 触发事件的碰撞组件，仅作为委托上下文，不保留引用。
 * @param OtherActor 被重叠的目标 Actor，可为空；自身拥有者和重复目标会被忽略。
 * @param OtherComp 目标参与重叠的组件，可为空且不保留引用。
 * @param OtherBodyIndex 目标组件的刚体索引，仅作为委托上下文。
 * @param bFromSweep 是否来自 Sweep，仅作为委托上下文。
 * @param SweepResult Sweep 命中信息的只读引用，本实现不保留引用。
 */
void ASKWeapon::OnHitboxOverlap(
    UPrimitiveComponent* Overlapped,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (!OtherActor || OtherActor == GetOwner()) return;
    if (AlreadyHitActors.Contains(OtherActor)) return;

    const float BaseDamage = 100.f;
    FPointDamageEvent DamageEvent(BaseDamage, FHitResult(), GetActorForwardVector(), nullptr);
    OtherActor->TakeDamage(BaseDamage, DamageEvent, GetInstigatorController(), GetOwner());
    AlreadyHitActors.Add(OtherActor);
}
