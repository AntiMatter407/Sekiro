// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/SKAIBattleProjectile.h"

#include "Combat/SKCombatComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"

/**
 * 创建无资产依赖的球形碰撞与 ProjectileMovement 默认子对象，并绑定首个阻挡命中回调。
 * 构造阶段不选择射手、目标或伤害参数；蓝图子类可调整组件外观和碰撞尺寸，所有对象只在游戏线程创建。
 */
ASKAIBattleProjectile::ASKAIBattleProjectile()
{
    PrimaryActorTick.bCanEverTick = false;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
    SetRootComponent(CollisionComponent);
    CollisionComponent->InitSphereRadius(8.f);
    CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
    CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    CollisionComponent->SetNotifyRigidBodyCollision(true);
    CollisionComponent->OnComponentHit.AddDynamic(this, &ASKAIBattleProjectile::HandleProjectileHit);

    ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComponent"));
    ProjectileMovementComponent->UpdatedComponent = CollisionComponent;
    ProjectileMovementComponent->InitialSpeed = 2000.f;
    ProjectileMovementComponent->MaxSpeed = 2000.f;
    ProjectileMovementComponent->ProjectileGravityScale = 0.f;
    ProjectileMovementComponent->bRotationFollowsVelocity = true;
    ProjectileMovementComponent->bShouldBounce = false;
    ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;

    InitialLifeSpan = 5.f;
}

/**
 * 使用射手、目标和数值参数初始化一次直射飞行，并锁存后续 ProjectileImpact 所需事件上下文。
 * 有效目标 Actor 只用于记录预期目标；实际初速度朝 InTargetLocation，飞行过程不自动追踪目标。
 * 本函数只能在生成帧的游戏线程调用一次，不生成资产、不选择目标，也不造成即时伤害。
 *
 * @param InShooterActor 发射者 Actor，必须有效；以弱引用保存，并加入移动忽略列表。
 * @param InTargetActor 可选预期目标 Actor，以弱引用保存；实际命中对象仍由碰撞决定。
 * @param InTargetLocation 已解析的世界瞄准位置，单位厘米；必须有限且不能等于出生位置。
 * @param InRelatedActionSerial 发射时射手当前的正动作序列号，必须仍有效；发射成功后允许动作正常结束。
 * @param InEventTag ProjectileImpact 携带的可选中性标签，不由本类解释。
 * @param InDamage 命中时提交的基础伤害，必须为有限非负数。
 * @param InSpeed 初始速度，单位厘米每秒，必须为有限正数。
 * @param InGravityScale 世界重力倍率，必须为有限数，可为零或负数。
 * @param InLifeSeconds 自动销毁时间，单位秒，必须为有限正数。
 * @return 首次初始化且全部输入有效时返回 true；重复调用或任一输入无效时返回 false。
 */
bool ASKAIBattleProjectile::InitializeProjectile(
    AActor* InShooterActor,
    AActor* InTargetActor,
    const FVector& InTargetLocation,
    int32 InRelatedActionSerial,
    FName InEventTag,
    float InDamage,
    float InSpeed,
    float InGravityScale,
    float InLifeSeconds)
{
    const FVector FlightDirection = (InTargetLocation - GetActorLocation()).GetSafeNormal();
    if (bInitialized
        || !IsValid(InShooterActor)
        || InTargetLocation.ContainsNaN()
        || FlightDirection.IsNearlyZero()
        || !FMath::IsFinite(InDamage)
        || InDamage < 0.f
        || !FMath::IsFinite(InSpeed)
        || InSpeed <= UE_SMALL_NUMBER
        || !FMath::IsFinite(InGravityScale)
        || !FMath::IsFinite(InLifeSeconds)
        || InLifeSeconds <= 0.f
        || !CollisionComponent
        || !ProjectileMovementComponent)
    {
        return false;
    }

    USKCombatComponent* SourceCombat = InShooterActor->FindComponentByClass<USKCombatComponent>();
    if (!IsValid(SourceCombat) || !SourceCombat->IsActionSerialValid(InRelatedActionSerial)) return false;
    SetOwner(InShooterActor);
    if (!SourceCombat->RegisterCombatHitSource(this, ESKCombatDamageChannel::Projectile, InDamage, 0.f, HitRequestTemplate)) return false;
    HitRequestTemplate.EventTag = InEventTag;
    ShooterActor = InShooterActor;
    IntendedTargetActor = InTargetActor;
    bInitialized = true;

    SetInstigator(Cast<APawn>(InShooterActor));
    SetActorRotation(FlightDirection.Rotation());
    CollisionComponent->IgnoreActorWhenMoving(InShooterActor, true);
    ProjectileMovementComponent->InitialSpeed = InSpeed;
    ProjectileMovementComponent->MaxSpeed = InSpeed;
    ProjectileMovementComponent->ProjectileGravityScale = InGravityScale;
    ProjectileMovementComponent->Velocity = FlightDirection * InSpeed;
    SetLifeSpan(InLifeSeconds);
    return true;
}

/**
 * 游戏线程离场时释放来源票据，超时销毁同样关闭请求；不改变射手动作或资源。
 * @param EndPlayReason 引擎离场原因，原样转交父类。
 */
void ASKAIBattleProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    AActor* Source = ShooterActor.Get();
    USKCombatComponent* Combat = Source ? Source->FindComponentByClass<USKCombatComponent>() : nullptr;
    if (IsValid(Combat)) Combat->ReleaseCombatHitSource(HitRequestTemplate.HitSourceSerial);
    Super::EndPlay(EndPlayReason);
}

/**
 * 处理首个非射手阻挡命中，将签发的请求交守方统一结算；不独立发布事件或调用标准伤害。
 * 本函数由碰撞组件在游戏线程调用，完成后销毁弹射物并释放票据；失败不会重复处理。
 *
 * @param HitComponent 产生命中的本方碰撞组件，可为空且仅作为委托上下文。
 * @param OtherActor 实际阻挡命中的 Actor，可为空；射手自身会被忽略。
 * @param OtherComponent 实际命中的目标组件，可为空且不保留引用。
 * @param NormalImpulse 物理求解冲量，当前查询型弹射物不使用该值。
 * @param Hit 本次命中的只读几何信息，仅用于世界接触点。
 */
void ASKAIBattleProjectile::HandleProjectileHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    FVector NormalImpulse,
    const FHitResult& Hit)
{
    (void)HitComponent;
    (void)OtherComponent;
    (void)NormalImpulse;

    AActor* SourceActor = ShooterActor.Get();
    if (!bInitialized || bImpactResolved || OtherActor == SourceActor) return;
    bImpactResolved = true;

    USKCombatComponent* TargetCombat = OtherActor
        ? OtherActor->FindComponentByClass<USKCombatComponent>()
        : nullptr;
    if (TargetCombat)
    {
        FSKCombatHitRequest Request = HitRequestTemplate;
        Request.TargetActor = OtherActor;
        Request.ImpactPoint = Hit.ImpactPoint;
        Request.AttackDirection = ProjectileMovementComponent
            ? ProjectileMovementComponent->Velocity.GetSafeNormal()
            : GetActorForwardVector();
        TargetCombat->ResolveCombatHit(Request);
    }

    Destroy();
}
