#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroImportLibrary.generated.h"

struct FSKAnimLogicImportResult;
class USKAnimationLogicData;

/// Python 可调用的蓝图函数库：封装 TAE 导入和 DataAsset 构建
UCLASS()
class SEKIROIMPORT_API USekiroImportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /// 从 JSON 文件导入 TAE 逻辑数据 → ABIR（仅 Python 管线调用）
    UFUNCTION(Category = "SekiroImport")
    static void ImportTAELogic(const FString& JsonPath, FSKAnimLogicImportResult& OutResult);

    /// 从 ABIR 构建 USKAnimationLogicData DataAsset（仅 Python 管线调用）
    UFUNCTION(Category = "SekiroImport")
    static USKAnimationLogicData* BuildAnimLogicDataAsset(
        const FSKAnimLogicImportResult& InResult,
        const FString& PackagePath,
        const FString& AssetName);
};
