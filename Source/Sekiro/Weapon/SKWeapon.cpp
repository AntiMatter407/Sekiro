// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKWeapon.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"

ASKWeapon::ASKWeapon()
{
    PrimaryActorTick.bCanEverTick = false;

    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    SetRootComponent(WeaponMesh);

    // ── 攻击碰撞体 ──────────────────────────────────────────
    AttackHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("AttackHitbox"));
    AttackHitbox->SetupAttachment(WeaponMesh, HitboxSocketName);
    AttackHitbox->SetCapsuleSize(HitboxRadius, HitboxHalfHeight);
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);      // 默认禁用
    AttackHitbox->SetCollisionObjectType(ECC_WorldDynamic);
    AttackHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
    AttackHitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);     // 对Pawn产生Overlap
    AttackHitbox->SetGenerateOverlapEvents(true);
    AttackHitbox->CanCharacterStepUpOn = ECB_No;
    AttackHitbox->OnComponentBeginOverlap.AddDynamic(this, &ASKWeapon::OnHitboxOverlap);
}

void ASKWeapon::ActivateHitbox()
{
    if (!AttackHitbox) return;
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ASKWeapon::DeactivateHitbox()
{
    if (!AttackHitbox) return;
    AttackHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASKWeapon::ClearHitActors()
{
    AlreadyHitActors.Empty();
}

void ASKWeapon::OnHitboxOverlap(UPrimitiveComponent* Overlapped, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
    bool bFromSweep, const FHitResult& SweepResult)
{
    // 忽略自身
    if (!OtherActor || OtherActor == GetOwner()) return;

    // 忽略已命中
    if (AlreadyHitActors.Contains(OtherActor)) return;

    // 应用伤害（基础伤害 100，后续由 BehaviorJudgeID 映射）
    const float BaseDamage = 100.f;
    FPointDamageEvent DamageEvent(BaseDamage, FHitResult(), GetActorForwardVector(), nullptr);
    OtherActor->TakeDamage(BaseDamage, DamageEvent, GetInstigatorController(), GetOwner());

    // 标记已命中
    AlreadyHitActors.Add(OtherActor);
}
