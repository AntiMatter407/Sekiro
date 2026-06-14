#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "SekiroAnimNotifies.generated.h"

// ============================================================================
// 攻击盒通知 — 映射 TAE Type 1: InvokeAttackBehavior
// ============================================================================
UCLASS(meta = (DisplayName = "SK Attack Hitbox"))
class SEKIROIMPORT_API UAnimNotify_SKAttackHitbox : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    virtual FString GetNotifyName_Implementation() const override { return TEXT("SK Attack Hitbox"); }

    /// BehaviorJudgeID（对应 Param / AtkParam 表）
    UPROPERTY(EditAnywhere, Category = "Attack")
    int32 BehaviorJudgeID = 0;

    /// 攻击类型: 0=Standard, 2=ForwardR1, 62=Plunging, 64=Parry
    UPROPERTY(EditAnywhere, Category = "Attack")
    int32 AttackType = 0;

    /// 来源: 0=Default, 1=Right Hand, 2=Left Hand
    UPROPERTY(EditAnywhere, Category = "Attack")
    int32 Source = 0;
};

// ============================================================================
// 射弹行为通知 — 映射 TAE Type 2: InvokeBulletBehavior
// ============================================================================
UCLASS(meta = (DisplayName = "SK Bullet Behavior"))
class SEKIROIMPORT_API UAnimNotify_SKBulletBehavior : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    virtual FString GetNotifyName_Implementation() const override { return TEXT("SK Bullet Behavior"); }

    UPROPERTY(EditAnywhere, Category = "Bullet")
    int32 DummyPolyID = 0;

    UPROPERTY(EditAnywhere, Category = "Bullet")
    int32 BehaviorJudgeID = 0;
};

// ============================================================================
// 状态效果通知 — 映射 TAE Type 66/67: AddSpEffect
// ============================================================================
UCLASS(meta = (DisplayName = "SK SpEffect"))
class SEKIROIMPORT_API UAnimNotify_SKSpEffect : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    virtual FString GetNotifyName_Implementation() const override
    {
        return FString::Printf(TEXT("SK SpEffect %d"), SpEffectID);
    }

    UPROPERTY(EditAnywhere, Category = "SpEffect")
    int32 SpEffectID = 0;
};

// ============================================================================
// 行为标志通知状态 — 映射 TAE Type 300/301: ChrActionFlag
// 控制 Begin→End 时间段内的行为标记
// ============================================================================
UCLASS(meta = (DisplayName = "SK Behavior Flag"))
class SEKIROIMPORT_API UAnimNotifyState_SKBehaviorFlag : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

    virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    virtual FString GetNotifyName_Implementation() const override
    {
        return FString::Printf(TEXT("SK Flag: %s"), *FlagName.ToString());
    }

    /// 标记名称: "Invincible", "ParryWindow", "CanCancel", "NoMovement" 等
    UPROPERTY(EditAnywhere, Category = "Flag")
    FName FlagName;

    /// 标记值: true=开启, false=关闭
    UPROPERTY(EditAnywhere, Category = "Flag")
    bool bEnable = true;
};

// ============================================================================
// 镜头震动 — 映射 TAE Type 144-147: RumbleCam
// ============================================================================
UCLASS(meta = (DisplayName = "SK Rumble Cam"))
class SEKIROIMPORT_API UAnimNotify_SKRumbleCam : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    virtual FString GetNotifyName_Implementation() const override
    {
        return FString::Printf(TEXT("SK RumbleCam %d"), RumbleCamID);
    }

    UPROPERTY(EditAnywhere, Category = "RumbleCam")
    int32 RumbleCamID = 0;

    UPROPERTY(EditAnywhere, Category = "RumbleCam")
    bool bIsGlobal = true;
};
