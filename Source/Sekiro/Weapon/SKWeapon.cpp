// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKWeapon.h"
#include "Combat/SKCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"

/**
 * 创建刀身、刀鞘和攻击碰撞组件，并建立武器内部的默认父子关系。
 * 资产由蓝图配置；构造过程不加载项目资源，只能在游戏线程创建 Actor 时执行。
 */
ASKWeapon::ASKWeapon()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

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
    AttackHitbox->SetGenerateOverlapEvents(false);
    AttackHitbox->CanCharacterStepUpOn = ECB_No;
}

/**
 * 在动画攻击窗口内读取当前刀刃线段，并将上一帧到当前帧的运动轨迹交给连续 Sweep。
 * Actor 使用 PostPhysics Tick，以尽量读取角色骨架完成本帧动画更新后的武器姿态；函数只在游戏线程运行。
 *
 * @param DeltaSeconds 本帧时长，单位秒；当前算法不按帧时长缩放刀刃几何，仅传给父类生命周期。
 */
void ASKWeapon::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bBladeSweepActive) return;

    FVector CurrentBladeBase;
    FVector CurrentBladeTip;
    if (!GetBladeSweepSegment(CurrentBladeBase, CurrentBladeTip))
    {
        bHasPreviousBladeSegment = false;
        return;
    }

    if (bHasPreviousBladeSegment)
    {
        SweepBladeSegment(
            PreviousBladeBase,
            PreviousBladeTip,
            CurrentBladeBase,
            CurrentBladeTip);
    }

    PreviousBladeBase = CurrentBladeBase;
    PreviousBladeTip = CurrentBladeTip;
    bHasPreviousBladeSegment = true;
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
 * 开启连续刀刃 Sweep，并立即记录当前刀刃线段作为下一帧轨迹起点。
 * 旧胶囊碰撞始终保持关闭，仅作为蓝图中的尺寸参考；本函数不清空已命中集合，也不计算攻击伤害。
 * 必须在游戏线程调用，无参数且无返回值。
 */
void ASKWeapon::ActivateHitbox()
{
    if (AttackHitbox)
    {
        AttackHitbox->SetGenerateOverlapEvents(false);
        AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    bBladeSweepActive = true;
    bHasPreviousBladeSegment = GetBladeSweepSegment(
        PreviousBladeBase,
        PreviousBladeTip);
    if (bHasPreviousBladeSegment)
    {
        SweepBladeSegment(
            PreviousBladeBase,
            PreviousBladeTip,
            PreviousBladeBase,
            PreviousBladeTip);
    }
    SetActorTickEnabled(true);
}

/**
 * 关闭连续刀刃 Sweep 并丢弃上一帧轨迹，避免下一次攻击跨窗口连接两段无关动作。
 * 本函数不清空命中集合，也不改变武器展示状态；必须在游戏线程调用。
 */
void ASKWeapon::DeactivateHitbox()
{
    bBladeSweepActive = false;
    bHasPreviousBladeSegment = false;
    SetActorTickEnabled(false);
    if (AttackHitbox)
    {
        AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
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
 * 根据 Blade00 与 Blade01 的当前世界坐标推导完整刀刃线段。
 * 两个骨骼只覆盖刀身中段，因此分别按配置比例向刀柄和刀尖延伸；函数只读组件状态且必须在游戏线程调用。
 *
 * @param OutBladeBase 输出补全后的刀柄端世界坐标，仅在返回 true 时有效。
 * @param OutBladeTip 输出补全后的刀尖端世界坐标，仅在返回 true 时有效。
 * @return 网格、骨骼和两点间距有效时返回 true；否则返回 false 且调用方应丢弃历史轨迹。
 */
bool ASKWeapon::GetBladeSweepSegment(
    FVector& OutBladeBase,
    FVector& OutBladeTip) const
{
    if (!BladeMesh
        || BladeMesh->GetBoneIndex(BladeSweepBaseBone) == INDEX_NONE
        || BladeMesh->GetBoneIndex(BladeSweepTipBone) == INDEX_NONE)
    {
        return false;
    }

    const FVector BaseAnchor = BladeMesh->GetSocketLocation(BladeSweepBaseBone);
    const FVector TipAnchor = BladeMesh->GetSocketLocation(BladeSweepTipBone);
    const FVector BoneSpan = TipAnchor - BaseAnchor;
    if (BoneSpan.IsNearlyZero()) return false;

    OutBladeBase = BaseAnchor - BoneSpan * BladeSweepBaseExtension;
    OutBladeTip = TipAnchor + BoneSpan * BladeSweepTipExtension;
    return true;
}

/**
 * 沿完整刀刃均匀取样，并对每个采样点执行从上一帧到当前帧的球形 Sweep。
 * 查询只包含 Pawn，忽略武器自身和持有者；同一帧同一 Actor 只裁决一次，但 Ignored 目标可在后续帧重新判定。
 * 本函数只负责连续空间采样，不决定格挡、弹反、姿势值或具体动画，必须在游戏线程调用。
 *
 * @param InPreviousBladeBase 上一帧补全后的刀柄端世界坐标。
 * @param InPreviousBladeTip 上一帧补全后的刀尖端世界坐标。
 * @param InCurrentBladeBase 当前帧补全后的刀柄端世界坐标。
 * @param InCurrentBladeTip 当前帧补全后的刀尖端世界坐标。
 */
void ASKWeapon::SweepBladeSegment(
    const FVector& InPreviousBladeBase,
    const FVector& InPreviousBladeTip,
    const FVector& InCurrentBladeBase,
    const FVector& InCurrentBladeTip)
{
    UWorld* World = GetWorld();
    if (!World) return;

    FCollisionObjectQueryParams ObjectQueryParams;
    ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SKWeaponBladeSweep), false);
    QueryParams.AddIgnoredActor(this);
    QueryParams.AddIgnoredActor(GetOwner());

    const int32 SampleCount = FMath::Clamp(BladeSweepSampleCount, 2, 16);
    const FCollisionShape SweepShape = FCollisionShape::MakeSphere(
        FMath::Max(BladeSweepRadius, 0.1f));
    TSet<TWeakObjectPtr<AActor>> ProcessedActorsThisFrame;

    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        const float Alpha = static_cast<float>(SampleIndex)
            / static_cast<float>(SampleCount - 1);
        const FVector SweepStart = FMath::Lerp(
            InPreviousBladeBase,
            InPreviousBladeTip,
            Alpha);
        const FVector SweepEnd = FMath::Lerp(
            InCurrentBladeBase,
            InCurrentBladeTip,
            Alpha);

        TArray<FHitResult> SweepHits;
        World->SweepMultiByObjectType(
            SweepHits,
            SweepStart,
            SweepEnd,
            FQuat::Identity,
            ObjectQueryParams,
            SweepShape,
            QueryParams);

        for (const FHitResult& SweepHit : SweepHits)
        {
            AActor* OtherActor = SweepHit.GetActor();
            if (!OtherActor || ProcessedActorsThisFrame.Contains(OtherActor)) continue;

            ProcessedActorsThisFrame.Add(OtherActor);
            ResolveSweepHit(OtherActor, SweepHit);
        }
    }
}

/**
 * 把一个连续 Sweep 命中的 Actor 交给现有战斗组件裁决为普通命中、格挡或弹反。
 * 非 Ignored 裁决会先向存在的攻守战斗组件发布接触事件，只有普通命中随后应用基础点伤害。
 * 事件发布失败不改变原有裁决、伤害和攻击窗口去重结果；成功裁决的目标加入当前攻击窗口去重集合。
 * 函数不负责攻击窗口、阵营筛选或具体姿势数值规则，必须在游戏线程调用。
 *
 * @param OtherActor Sweep 命中的目标 Actor，可为空；持有者和已结算目标会被忽略，不保留引用。
 * @param SweepResult 本次 Sweep 的命中信息，只在调用期间读取，用于生成点伤害事件。
 */
void ASKWeapon::ResolveSweepHit(
    AActor* OtherActor,
    const FHitResult& SweepResult)
{
    if (!OtherActor || OtherActor == GetOwner()) return;
    if (AlreadyHitActors.Contains(OtherActor)) return;

    AActor* AttackerActor = GetOwner();
    USKCombatComponent* AttackerCombat = AttackerActor
        ? AttackerActor->FindComponentByClass<USKCombatComponent>()
        : nullptr;
    USKCombatComponent* DefenderCombat =
        OtherActor->FindComponentByClass<USKCombatComponent>();

    const ESKIncomingAttackType AttackType = AttackerCombat
        ? AttackerCombat->ResolveOutgoingAttackType()
        : ESKIncomingAttackType::Light;
    ESKWeaponContactResult ContactResult = ESKWeaponContactResult::Hit;
    if (DefenderCombat)
    {
        ContactResult = DefenderCombat->ResolveIncomingWeaponContact(
            AttackerCombat,
            AttackType);
    }

    if (ContactResult == ESKWeaponContactResult::Ignored) return;

    FSKAICombatEvent ContactEvent;
    ContactEvent.EventType = ESKAICombatEventType::WeaponContact;
    ContactEvent.SourceActor = AttackerActor;
    ContactEvent.TargetActor = OtherActor;
    ContactEvent.RelatedActionSerial = AttackerCombat ? AttackerCombat->GetActionSerial() : 0;
    ContactEvent.AttackType = AttackType;
    ContactEvent.ContactResult = ContactResult;
    if (AttackerCombat) AttackerCombat->PublishAICombatEvent(ContactEvent);
    if (DefenderCombat) DefenderCombat->PublishAICombatEvent(ContactEvent);

    if (ContactResult == ESKWeaponContactResult::Hit)
    {
        const float BaseDamage = 100.f;
        FPointDamageEvent DamageEvent(
            BaseDamage,
            SweepResult,
            GetActorForwardVector(),
            nullptr);
        OtherActor->TakeDamage(
            BaseDamage,
            DamageEvent,
            GetInstigatorController(),
            AttackerActor);
    }
    AlreadyHitActors.Add(OtherActor);
}
