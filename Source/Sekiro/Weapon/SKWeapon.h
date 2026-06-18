// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SKWeapon.generated.h"

class UCapsuleComponent;

// ============================================================================
// ASKWeapon — 所有武器的C++基类
//     蓝图子类设置 SkeletalMesh + Hitbox 参数
//     由 USKWeaponComponent 生成并挂载到角色骨骼
// ============================================================================

UCLASS(Blueprintable, BlueprintType)
class SEKIRO_API ASKWeapon : public AActor
{
    GENERATED_BODY()

public:
    ASKWeapon();

    // ── 碰撞体控制 ─────────────────────────────────────────

    /** 激活攻击碰撞体（开始检测 Overlap） */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ActivateHitbox();

    /** 禁用攻击碰撞体 */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void DeactivateHitbox();

    /** 清空当前攻击已命中集合（每次新攻击前调用） */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ClearHitActors();

protected:
    UFUNCTION()
    void OnHitboxOverlap(UPrimitiveComponent* Overlapped, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);

    // ── 组件 ──────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    TObjectPtr<USkeletalMeshComponent> WeaponMesh;

    /** 攻击碰撞胶囊体 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    TObjectPtr<UCapsuleComponent> AttackHitbox;

    // ── 配置 ──────────────────────────────────────────────

    /** 碰撞体附着到的武器 Socket（由 WeaponComponent 指定挂载骨骼） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    FName HitboxSocketName = TEXT("R_Weapon");

    /** 碰撞胶囊体半径 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float HitboxRadius = 12.f;                               // 碰撞体半径

    /** 碰撞胶囊体半高 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float HitboxHalfHeight = 30.f;                           // 碰撞体半高

private:
    /** 当前攻击已命中的目标（防止重复命中） */
    TSet<TWeakObjectPtr<AActor>> AlreadyHitActors;
};
