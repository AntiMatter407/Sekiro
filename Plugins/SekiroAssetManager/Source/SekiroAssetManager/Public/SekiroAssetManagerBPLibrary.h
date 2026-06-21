#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroAssetManagerBPLibrary.generated.h"

/// 蓝图函数库，提供 Sekiro 资产导入的 UFUNCTION 接口，可被 Python 调用
UCLASS()
class SEKIROASSETMANAGER_API USekiroAssetManagerBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /// 从 Sekiro_model.json 导入骨架 + 骨骼网格体
    /// @param JsonPath JSON 文件绝对路径
    /// @param TargetPackagePath UE 目标包路径
    /// @param bOutSuccess 是否成功
    /// @param OutErrorMessage 错误信息
    /// @return 导入的 SkeletalMesh 路径（或空字符串）
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Asset")
    static FString ImportSkeletalMesh(const FString& JsonPath, const FString& TargetPackagePath,
                                       bool& bOutSuccess, FString& OutErrorMessage);
};
