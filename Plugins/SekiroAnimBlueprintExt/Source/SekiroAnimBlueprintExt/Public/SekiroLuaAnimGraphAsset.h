#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SekiroLuaAnimTypes.h"
#include "SekiroLuaAnimGraphAsset.generated.h"

UCLASS(BlueprintType)
class SEKIROANIMBLUEPRINTEXT_API USekiroLuaAnimGraphAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TArray<FSekiroLuaAnimState> States;   // 状态列表

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool FindStateCopy(FName StateName, FSekiroLuaAnimState& OutState) const;

    const FSekiroLuaAnimState* FindState(FName StateName) const;
};
