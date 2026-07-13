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

    /** 作用：按名称查找状态并复制到输出参数。@param StateName FName，目标状态名称。@param OutState FSekiroLuaAnimState&，找到时接收状态副本。@return bool，找到状态时为 true。 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Sekiro|Lua Animation")
    bool FindStateCopy(FName StateName, FSekiroLuaAnimState& OutState) const;

    /** 作用：按名称查找数据资产中的状态。@param StateName FName，目标状态名称。@return const FSekiroLuaAnimState*，状态地址，未找到返回 nullptr；仅限当前资产生命周期内使用。 */
    const FSekiroLuaAnimState* FindState(FName StateName) const;
};
