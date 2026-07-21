// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SKInputRestrictionVolume.generated.h"

class ASKCharacter;
class UBoxComponent;

/** 可放置的输入限制区域，负责向角色输入与武器管理器提交成对进入/离开状态。 */
UCLASS(Blueprintable)
class SEKIRO_API ASKInputRestrictionVolume : public AActor
{
    GENERATED_BODY()

public:
    ASKInputRestrictionVolume();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION()
    void OnRestrictionBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);

    UFUNCTION()
    void OnRestrictionEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,
        int32 OtherBodyIndex);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Restriction")
    TObjectPtr<UBoxComponent> RestrictionBox;           // Pawn Overlap 查询区域

private:
    void SetCharacterRestrictionActive(ASKCharacter* Character, bool bActive); // 向角色管理器提交区域状态

    TSet<TWeakObjectPtr<ASKCharacter>> OverlappingCharacters; // 已计入本区域的角色集合
};
