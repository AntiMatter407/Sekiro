#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroLuaAnimBlueprintEditorLibrary.generated.h"

class UAnimBlueprint;
class UBlueprint;

UCLASS()
class SEKIROANIMBLUEPRINTEXTEDITOR_API USekiroLuaAnimBlueprintEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool ConnectLuaAnimBlueprintHostToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool ConnectLuaStateMachineToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName);

    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool BindBlueprintToLuaModule(UBlueprint* Blueprint, const FString& LuaModuleName);
};
