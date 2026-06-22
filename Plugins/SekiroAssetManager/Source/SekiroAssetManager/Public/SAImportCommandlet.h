#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SAImportCommandlet.generated.h"

/// SekiroAssetManager 统一导入 Commandlet
/// 用法:
///   模型:   -run=SAImport -Model=<json> -Output=<UE路径>
///   动画:   -run=SAImport -Anim=<json> -Output=<UE路径> [-Skeleton=<路径>]
///   材质:   -run=SAImport -Material=<json> -Output=<UE路径> -MeshPath=<路径>
///   战斗数据: -run=SAImport -Combo=<ComboChain.json> -Output=<UE资产包路径>
UCLASS()
class SEKIROASSETMANAGER_API USAImportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};
