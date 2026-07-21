// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SKWeapon.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class ESKWeaponPresentation : uint8
{
    Drawn UMETA(DisplayName = "Drawn"),
    Sheathed UMETA(DisplayName = "Sheathed")
};

/** 由单个 Actor 管理刀身、刀鞘与攻击碰撞的武器基类。 */
UCLASS(Blueprintable, BlueprintType)
class SEKIRO_API ASKWeapon : public AActor
{
    GENERATED_BODY()

public:
    ASKWeapon();

    // ── 挂载与展示 ────────────────────────────────────────────────────────────

    /** 初始化刀身与刀鞘在角色骨架上的挂载信息。 */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void InitializeAttachments(
        USkeletalMeshComponent* InCharacterMesh,
        FName InHandSocket,
        FName InHandBoneFallback,
        FName InSheathSocket);

    /** 将刀身切换到右手挂点。 */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    bool AttachBladeToHand();

    /** 将刀身切换到角色的原版收刀挂点。 */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    bool AttachBladeToSheath();

    /** 将刀鞘挂到角色腰部。 */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    bool AttachSheathToBody();

    /** 切换拔刀或收刀展示状态。 */
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    bool SetWeaponPresentation(ESKWeaponPresentation NewPresentation);

    /** 获取当前武器展示状态。 */
    UFUNCTION(BlueprintPure, Category = "Weapon")
    ESKWeaponPresentation GetWeaponPresentation() const;

    // ── 碰撞体控制 ────────────────────────────────────────────────────────────

    /** 激活攻击碰撞体。 */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ActivateHitbox();

    /** 禁用攻击碰撞体。 */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void DeactivateHitbox();

    /** 清空当前攻击已命中的目标集合。 */
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ClearHitActors();

protected:
    // ── 碰撞回调 ──────────────────────────────────────────────────────────────

    UFUNCTION()
    void OnHitboxOverlap(
        UPrimitiveComponent* Overlapped,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    // ── 组件 ──────────────────────────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    TObjectPtr<USkeletalMeshComponent> BladeMesh;             // 刀身网格，资源由武器蓝图配置

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    TObjectPtr<USkeletalMeshComponent> SheathMesh;            // 刀鞘网格，资源由武器蓝图配置

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    TObjectPtr<UCapsuleComponent> AttackHitbox;               // 跟随刀身 Blade01 骨骼的攻击碰撞体

    // ── 配置 ──────────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    FName BladeHitboxSocket = TEXT("Blade01");               // 刀身内部的攻击碰撞挂点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
    FName BladeSheathSocket = TEXT("Sheath01");              // 兼容已有蓝图序列化；原版收刀挂载不再依赖该内部骨骼

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Attachment")
    FTransform BladeHandAttachOffset = FTransform::Identity; // 新 R_WeaponSocket 已包含刀身对齐变换

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Attachment")
    FTransform SheathBodyAttachOffset = FTransform::Identity; // 新 SheathSocket 已包含刀鞘对齐变换

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Attachment")
    FTransform BladeSheathAttachOffset = FTransform::Identity; // 收刀刀身与刀鞘直接共用 SheathSocket

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float HitboxRadius = 12.f;                                // 攻击碰撞体半径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float HitboxHalfHeight = 30.f;                            // 攻击碰撞体半高

private:
    // ── 运行时状态 ────────────────────────────────────────────────────────────

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> CharacterMesh;         // 武器当前绑定的角色骨架网格

    FName HandSocket = TEXT("R_WeaponSocket");               // 优先使用的右手武器挂点
    FName HandBoneFallback = TEXT("R_Hand");                 // 右手挂点不存在时使用的骨骼
    FName SheathSocket = TEXT("SheathSocket");               // 角色腰部刀鞘挂点
    ESKWeaponPresentation Presentation = ESKWeaponPresentation::Drawn; // 当前展示状态
    TSet<TWeakObjectPtr<AActor>> AlreadyHitActors;             // 单次攻击已命中的目标集合
};
