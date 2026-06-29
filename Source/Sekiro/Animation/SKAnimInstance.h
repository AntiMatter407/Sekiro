#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/SKMovementComponent.h"
#include "Animation/SKAnimationController.h"
#include "SKAnimInstance.generated.h"

class ASKCharacter;

// ============================================================================
// USKAnimInstance — 精简后的动画实例
// 仅负责更新 Blueprint 可见变量（Speed/Angle/Direction/FrameFlags）。
// 状态机、DataTable、曲线查询逻辑已迁移至 USKAnimationController。
// ============================================================================

UCLASS()
class SEKIRO_API USKAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // ── Locomotion（Blueprint 读取） ────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Speed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Angle = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ESKMovementTier MovementTier = ESKMovementTier::Run;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ESKLocomotionDirection Direction = ESKLocomotionDirection::Fwd;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bIsInAir : 1;

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bIsCrouching : 1;

    // ── Dodge（Blueprint 读取） ─────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    uint32 bIsDodging : 1;

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    float DodgeDirection = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    float DodgeDirectionLateral = 0.f;

    // ── 帧级标志（从 FrameFlags 曲线读取，Blueprint 消费） ─────

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    uint32 bCanDeflect : 1;

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    uint32 bDisableTurning : 1;

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    uint32 bDisableMovement : 1;

    UPROPERTY(BlueprintReadOnly, Category = "Flags")
    uint32 bInvincible : 1;

    // ── 输入意图（Blueprint 读取） ─────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    FName InputIntent;

    // ── Controller 引用 ────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Controller")
    TObjectPtr<USKAnimationController> AnimController;

protected:
    UPROPERTY()
    TObjectPtr<ASKCharacter> OwnerCharacter;

    UPROPERTY()
    TObjectPtr<USKMovementComponent> OwnerMovement;
};
