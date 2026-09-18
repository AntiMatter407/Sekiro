#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Templates/SubclassOf.h"

#include "LuaAnimBlueprintFactory.generated.h"

/** 创建附带 Lua 源模块元数据的标准 UAnimBlueprint。 */
UCLASS(HideCategories = Object)
class LUAANIMBLUEPRINTEDITOR_API ULuaAnimBlueprintFactory : public UFactory
{
    GENERATED_BODY()

public:
    ULuaAnimBlueprintFactory();

    UPROPERTY(EditAnywhere, Category = "Lua Anim Blueprint")
    FString LuaModuleName; // 新资产绑定的 Lua require 模块名

    UPROPERTY(EditAnywhere, Category = "Lua Anim Blueprint", meta = (AllowAbstract = ""))
    TSubclassOf<class UAnimInstance> ParentClass; // 新资产的原生 AnimInstance 父类

    UPROPERTY(EditAnywhere, Category = "Lua Anim Blueprint")
    TObjectPtr<class USkeleton> TargetSkeleton = nullptr; // 新资产使用的目标骨架

    virtual FText GetDisplayName() const override;
    virtual FString GetDefaultNewAssetName() const override;
    virtual UObject* FactoryCreateNew(
        UClass* Class,
        UObject* InParent,
        FName Name,
        EObjectFlags Flags,
        UObject* Context,
        FFeedbackContext* Warn,
        FName CallingContext) override;
};
