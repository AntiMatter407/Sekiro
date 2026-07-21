// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/SKInputRestrictionVolume.h"

#include "Character/SKCharacter.h"
#include "Input/SKInputManager.h"
#include "Weapon/SKWeaponManagerComponent.h"
#include "Components/BoxComponent.h"

/**
 * 创建可放置的禁战输入区域并配置仅查询 Pawn Overlap 的 Box 根组件。
 * 构造过程不加载资产、不播放动画，也不持有角色强引用；只能在游戏线程创建 Actor 时执行。
 */
ASKInputRestrictionVolume::ASKInputRestrictionVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    RestrictionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("RestrictionBox"));
    SetRootComponent(RestrictionBox);
    RestrictionBox->SetBoxExtent(FVector(100.f, 100.f, 100.f));
    RestrictionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    RestrictionBox->SetCollisionObjectType(ECC_WorldDynamic);
    RestrictionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    RestrictionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    RestrictionBox->SetGenerateOverlapEvents(true);
    RestrictionBox->CanCharacterStepUpOn = ECB_No;
}

void ASKInputRestrictionVolume::BeginPlay()
{
    Super::BeginPlay();
    if (RestrictionBox)
    {
        RestrictionBox->OnComponentBeginOverlap.AddDynamic(
            this,
            &ASKInputRestrictionVolume::OnRestrictionBeginOverlap);
        RestrictionBox->OnComponentEndOverlap.AddDynamic(
            this,
            &ASKInputRestrictionVolume::OnRestrictionEndOverlap);
    }
}

/**
 * 在区域退出游戏生命周期时为仍登记的角色补发离开，保证多区域引用计数不会泄漏。
 * 仅由引擎在游戏线程调用；函数不会销毁角色或管理器。
 *
 * @param EndPlayReason 引擎提供的结束原因，仅转发给父类且不保留引用。
 */
void ASKInputRestrictionVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (RestrictionBox)
    {
        RestrictionBox->OnComponentBeginOverlap.RemoveAll(this);
        RestrictionBox->OnComponentEndOverlap.RemoveAll(this);
    }
    for (const TWeakObjectPtr<ASKCharacter>& CharacterReference : OverlappingCharacters)
    {
        ASKCharacter* Character = CharacterReference.Get();
        if (Character) SetCharacterRestrictionActive(Character, false);
    }
    OverlappingCharacters.Reset();

    Super::EndPlay(EndPlayReason);
}

/**
 * 处理 Box 开始重叠，并只为首次进入本区域的 ASKCharacter 增加一次限制计数。
 * 多个角色组件同时重叠时由集合去重；函数不解释移动状态或播放武器动画。
 * 仅由碰撞系统在游戏线程调用。
 *
 * @param OverlappedComponent 触发事件的区域组件，不保留引用。
 * @param OtherActor 进入区域的 Actor；只有 ASKCharacter 会被处理。
 * @param OtherComponent 角色参与重叠的组件，可为空且不保留引用。
 * @param OtherBodyIndex 角色组件刚体索引，仅作为碰撞上下文。
 * @param bFromSweep 是否来自 Sweep，仅作为碰撞上下文。
 * @param SweepResult Sweep 命中信息的只读引用，不保留引用。
 */
void ASKInputRestrictionVolume::OnRestrictionBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    ASKCharacter* Character = Cast<ASKCharacter>(OtherActor);
    if (!Character || OverlappingCharacters.Contains(Character)) return;

    OverlappingCharacters.Add(Character);
    SetCharacterRestrictionActive(Character, true);
}

/**
 * 处理 Box 结束重叠，并在角色所有组件均离开本区域后减少一次限制计数。
 * 未登记角色和仍有其他组件重叠的角色会被忽略，避免计数提前归零；仅在游戏线程调用。
 *
 * @param OverlappedComponent 触发事件的区域组件，不保留引用。
 * @param OtherActor 离开区域的 Actor；只有已登记 ASKCharacter 会被处理。
 * @param OtherComponent 结束重叠的角色组件，可为空且不保留引用。
 * @param OtherBodyIndex 角色组件刚体索引，仅作为碰撞上下文。
 */
void ASKInputRestrictionVolume::OnRestrictionEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,
    int32 OtherBodyIndex)
{
    ASKCharacter* Character = Cast<ASKCharacter>(OtherActor);
    if (!Character || !OverlappingCharacters.Contains(Character)) return;
    if (RestrictionBox && RestrictionBox->IsOverlappingActor(Character)) return;

    OverlappingCharacters.Remove(Character);
    SetCharacterRestrictionActive(Character, false);
}

/**
 * 将本区域的进入或离开语义同步提交给角色的 InputManager 与 WeaponManager。
 * 函数只修改管理器计数，不播放动画、不选择武器状态；必须在游戏线程调用。
 *
 * @param Character 目标项目角色，不可为空且不转移所有权。
 * @param bActive true 表示进入区域并增加计数，false 表示离开区域并减少计数。
 */
void ASKInputRestrictionVolume::SetCharacterRestrictionActive(ASKCharacter* Character, bool bActive)
{
    if (!Character) return;

    USKInputManager* InputManager = Character->GetInputManager();
    USKWeaponManagerComponent* WeaponManager = Character->GetWeaponManager();
    if (bActive)
    {
        if (InputManager) InputManager->EnterRestrictedZone();
        if (WeaponManager) WeaponManager->EnterRestrictedZone();
        return;
    }

    if (InputManager) InputManager->ExitRestrictedZone();
    if (WeaponManager) WeaponManager->ExitRestrictedZone();
}
