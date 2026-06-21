#include "SekiroAssetManagerBPLibrary.h"
#include "SAModelImporter.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/SavePackage.h"

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
