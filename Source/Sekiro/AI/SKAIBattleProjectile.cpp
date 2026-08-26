// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/SKAIBattleProjectile.h"

#include "Combat/SKCombatComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
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
 * @param InRelatedActionSerial 发射时的射手动作序列号，可为零，仅用于事件关联。
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

    ShooterActor = InShooterActor;
    IntendedTargetActor = InTargetActor;
    RelatedActionSerial = InRelatedActionSerial;
    ImpactEventTag = InEventTag;
    ProjectileDamage = InDamage;
    bInitialized = true;

    SetOwner(InShooterActor);
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
 * 处理首个非射手阻挡命中，先向被命中 Actor 的战斗组件发布 ProjectileImpact，再同步提交标准点伤害。
 * 该顺序保证同一守方事件队列先收到 ProjectileImpact，随后由 OnTakeAnyDamage 追加 DamageReceived；两者不在 C++ 合并。
 * 本函数由碰撞组件在游戏线程调用，发布或伤害失败均不会重复处理，完成后销毁弹射物。
 *
 * @param HitComponent 产生命中的本方碰撞组件，可为空且仅作为委托上下文。
 * @param OtherActor 实际阻挡命中的 Actor，可为空；射手自身会被忽略。
 * @param OtherComponent 实际命中的目标组件，可为空且不保留引用。
 * @param NormalImpulse 物理求解冲量，当前查询型弹射物不使用该值。
 * @param Hit 本次命中的只读几何信息，用于标准点伤害事件。
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
        FSKAICombatEvent ImpactEvent;
        ImpactEvent.EventType = ESKAICombatEventType::ProjectileImpact;
        ImpactEvent.SourceActor = SourceActor;
        ImpactEvent.TargetActor = OtherActor;
        ImpactEvent.RelatedActionSerial = RelatedActionSerial;
        ImpactEvent.EventTag = ImpactEventTag;
        ImpactEvent.Magnitude = ProjectileDamage;
        TargetCombat->PublishAICombatEvent(ImpactEvent);
    }

    if (OtherActor && ProjectileDamage > 0.f)
    {
        APawn* ShooterPawn = Cast<APawn>(SourceActor);
        AController* InstigatorController = ShooterPawn
            ? ShooterPawn->GetController()
            : GetInstigatorController();
        const FVector ShotDirection = ProjectileMovementComponent
            ? ProjectileMovementComponent->Velocity.GetSafeNormal()
            : GetActorForwardVector();
        FPointDamageEvent DamageEvent(
            ProjectileDamage,
            Hit,
            ShotDirection,
            UDamageType::StaticClass());
        OtherActor->TakeDamage(
            ProjectileDamage,
            DamageEvent,
            InstigatorController,
            SourceActor ? SourceActor : this);
    }

    Destroy();
}
