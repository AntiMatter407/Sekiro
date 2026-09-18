// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/SKCombatTypes.h"
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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
    UPROPERTY(Transient)
    FSKCombatHitRequest HitRequestTemplate; // 发射时签发的来源身份与数值，不随射手后续动作改变
    bool bInitialized = false; // 是否已接受一次有效初始化
    bool bImpactResolved = false; // 是否已经处理首个有效阻挡命中
};
