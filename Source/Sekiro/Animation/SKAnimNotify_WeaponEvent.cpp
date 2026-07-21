// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SKAnimNotify_WeaponEvent.h"

#include "Weapon/SKWeaponManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

/**
 * 将当前动画通知的事件名和动画对象转发给网格 Owner 上的 WeaponManager。
 * 本函数不解释事件语义、不切换武器挂载，也不使用溯源帧字段计算触发时间；
 * SourceFrame 与 SourceFrameRate 仅供资产审计和原版数据溯源。由动画系统在游戏线程调用。
 *
 * @param MeshComp 触发通知的角色骨骼网格，不可为空且不转移所有权。
 * @param Animation 触发通知的动画资源，可为空；仅在本次调用中转发给 Lua，不保留引用。
 * @param EventReference 引擎提供的通知上下文，只转发给父类且不保留引用。
 */
void USKAnimNotify_WeaponEvent::Notify(
    USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);
    AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
    USKWeaponManagerComponent* WeaponManager = Owner
        ? Owner->FindComponentByClass<USKWeaponManagerComponent>()
        : nullptr;
    if (!MeshComp || EventName.IsNone() || !WeaponManager)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("SKWeaponNotify dispatch failed. Animation=%s Event=%s Owner=%s ManagerFound=%s"),
            *GetNameSafe(Animation),
            *EventName.ToString(),
            *GetNameSafe(Owner),
            WeaponManager ? TEXT("true") : TEXT("false"));
        return;
    }

    WeaponManager->DispatchWeaponAnimationEvent(EventName, Animation);
}
