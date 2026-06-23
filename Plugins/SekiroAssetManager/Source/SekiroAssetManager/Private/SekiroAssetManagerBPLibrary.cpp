#include "SekiroAssetManagerBPLibrary.h"
#include "SAModelImporter.h"
#include "SATAEImporter.h"
#include "SATAELogicBuilder.h"
#include "SekiroAnimLogicData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/SavePackage.h"

bool USekiroAssetManagerBPLibrary::ImportTAELogic(const FString& JsonPath, FSAAnimLogicImportResult& OutResult)
{
    if (!FPaths::FileExists(JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[TAE Import] JSON not found: %s"), *JsonPath);
        return false;
    }

    OutResult = FSATAEImporter::ImportFromFile(JsonPath);
    if (OutResult.TotalAnims == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[TAE Import] No animations imported"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[TAE Import] Success: %d anims, %d events, %d categories"),
        OutResult.TotalAnims, OutResult.TotalEvents,
        OutResult.MainStateMachine.AnimIDsByCategory.Num());
    return true;
}

USKAnimationLogicData* USekiroAssetManagerBPLibrary::BuildAnimLogicDataAsset(
    const FSAAnimLogicImportResult& ImportResult,
    const FString& PackagePath,
    const FString& AssetName)
{
    if (ImportResult.TotalAnims == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Empty import result"));
        return nullptr;
    }

    // 创建包
    FString FullPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Failed to create package: %s"), *FullPath);
        return nullptr;
    }
    Package->SetFlags(RF_Public | RF_Standalone);

    USKAnimationLogicData* DataAsset = FSATAELogicBuilder::BuildDataAsset(ImportResult, Package);
    if (!DataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] BuildDataAsset failed"));
        return nullptr;
    }

    // 重命名和保存
    DataAsset->Rename(*AssetName, Package);
    FString FilePath = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GLog;
    if (!UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *FilePath, GLog))
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Save failed: %s"), *FilePath);
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildAnimLogicData] Saved: %s"), *FullPath);
    return DataAsset;
}

FString USekiroAssetManagerBPLibrary::ImportSkeletalMesh(
    const FString& JsonPath, const FString& TargetPackagePath,
    bool& bOutSuccess, FString& OutErrorMessage)
{
    bOutSuccess = false;
    OutErrorMessage.Empty();

    USkeletalMesh* Mesh = nullptr;
    USkeleton* Skeleton = nullptr;

    if (!SAModelImporter::Import(JsonPath, TargetPackagePath, TArray<FString>(), Mesh, Skeleton))
    {
        OutErrorMessage = TEXT("SAModelImporter::Import failed");
        return FString();
    }

    bOutSuccess = true;
    return Mesh ? Mesh->GetPathName() : FString();
}
int32 USekiroAssetManagerBPLibrary::ImportAnimations(const FString& JsonPath, const FString& TargetBasePath,
    const FString& AssetName, const FString& SkeletonPath)
{
    UE_LOG(LogTemp, Error, TEXT("[ImportAnimations] Not yet implemented"));
    return 0;
}

bool USekiroAssetManagerBPLibrary::AddIntegerCurveToAnimation(const FString& AnimPath,
    const FString& CurveName, const TArray<float>& KeyTimes, const TArray<float>& KeyValues)
{
    UE_LOG(LogTemp, Log, TEXT("[AddIntegerCurve] 已转移到 AIBridge"));
    return false;
}