// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "SKAnimNotify_WeaponEvent.generated.h"

/** 将动画时间轴上的通用武器事件转发给角色 WeaponManager。 */
UCLASS(meta=(DisplayName="SK Weapon Event"))
class SEKIRO_API USKAnimNotify_WeaponEvent : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(
        USkeletalMeshComponent* MeshComp,
        UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Event")
    FName EventName = NAME_None;                        // 交给 Lua 解释的通用事件名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Event", meta = (ClampMin = "0"))
    int32 SourceFrame = 0;                              // 事件在原版动画中的来源帧

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Event", meta = (ClampMin = "0.001"))
    float SourceFrameRate = 30.f;                       // 原版来源动画帧率
};
