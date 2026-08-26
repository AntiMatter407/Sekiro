#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/SKAttributeTypes.h"
#include "SKAttributeProfile.generated.h"

class UGameplayEffect;

/** 角色初始属性配置；不包含资产路径、死亡流程或动作参数。 */
UCLASS(BlueprintType)
class SEKIRO_API USKAttributeProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    // ── 初始配置 ──────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
    FSKAttributeInitialization Values; // 初始属性和资源绝对值

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
    TArray<TSubclassOf<UGameplayEffect>> InitialEffects; // 初始装备或增益，限定持续或无限的静态属性修改器

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
    float EffectLevel = 1.f; // 初始增益的效果等级
};

