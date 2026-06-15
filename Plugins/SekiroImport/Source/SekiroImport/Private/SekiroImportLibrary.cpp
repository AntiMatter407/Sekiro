#include "SekiroImportLibrary.h"
#include "SekiroTAEImporter.h"
#include "SekiroAnimDataBuilder.h"
#include "SekiroAnimLogicData.h"
#include "SekiroImportLog.h"

void USekiroImportLibrary::ImportTAELogic(const FString& JsonPath, FSKAnimLogicImportResult& OutResult)
{
    OutResult = FSekiroTAEImporter::ImportFromFile(JsonPath);
    UE_LOG(LogSekiroImport, Log, TEXT("[ImportLibrary] 导入完成: %d 动画, %d 事件"),
        OutResult.TotalAnims, OutResult.TotalEvents);
}

USKAnimationLogicData* USekiroImportLibrary::BuildAnimLogicDataAsset(
    const FSKAnimLogicImportResult& InResult,
    const FString& PackagePath,
    const FString& AssetName)
{
    if (InResult.AnimLogicMap.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[ImportLibrary] IR 为空，无法构建 DataAsset"));
        return nullptr;
    }

    // 创建可覆盖的 Package
    UPackage* Pkg = CreatePackage(*PackagePath);
    if (!Pkg)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[ImportLibrary] 创建 Package 失败: %s"), *PackagePath);
        return nullptr;
    }

    USKAnimationLogicData* DA = FSekiroAnimDataBuilder::BuildDataAsset(InResult, Pkg);
    if (DA && !AssetName.IsEmpty())
    {
        DA->Rename(*AssetName, Pkg);
    }

    return DA;
}
