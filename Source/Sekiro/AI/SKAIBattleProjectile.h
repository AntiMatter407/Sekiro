// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SKAIBattleProjectile.generated.h"

class UPrimitiveComponent;
class UProjectileMovementComponent;
class USphereComponent;

UCLASS(Blueprintable)
class SEKIRO_API ASKAIBattleProjectile : public AActor
{
    GENERATED_BODY()

public:
    ASKAIBattleProjectile();

    /** 使用一次发射请求初始化飞行与命中上下文。 */
    bool InitializeProjectile(
        AActor* InShooterActor,
        AActor* InTargetActor,
        const FVector& InTargetLocation,
        int32 InRelatedActionSerial,
        FName InEventTag,
        float InDamage,
        float InSpeed,
        float InGravityScale,
        float InLifeSeconds);

protected:
    // ── 弹射物组件 ────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Projectile")
    TObjectPtr<USphereComponent> CollisionComponent; // 提供可由蓝图子类调整的阻挡碰撞体

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Projectile")
    TObjectPtr<UProjectileMovementComponent> ProjectileMovementComponent; // 执行带重力的连续飞行

private:
    // ── 命中处理 ──────────────────────────────────────────────

    UFUNCTION()
    void HandleProjectileHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse,
        const FHitResult& Hit);

    // ── 运行时上下文 ──────────────────────────────────────────

    TWeakObjectPtr<AActor> ShooterActor; // 发射者弱引用
    TWeakObjectPtr<AActor> IntendedTargetActor; // 发射时用于瞄准的目标弱引用
    int32 RelatedActionSerial = 0; // 发射时锁存的射手动作序列号
    FName ImpactEventTag = NAME_None; // 命中事件可选语义标签
    float ProjectileDamage = 0.f; // 标准伤害系统使用的基础伤害
    bool bInitialized = false; // 是否已接受一次有效初始化
    bool bImpactResolved = false; // 是否已经处理首个有效阻挡命中
};
