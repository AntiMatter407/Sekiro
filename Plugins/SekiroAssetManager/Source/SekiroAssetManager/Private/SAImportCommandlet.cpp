#include "SAImportCommandlet.h"
#include "SAModelImporter.h"
#include "SAAnimationImporter.h"
#include "SAMaterialImporter.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Parse -Key=Value from command-line string
static FString ParseParam(const FString& Params, const FString& Key)
{
    FString Left, Right;
    if (Params.Split(TEXT(" -") + Key + TEXT("="), &Left, &Right))
    {
        return Right.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Right;
    }
    if (Params.StartsWith(TEXT("-") + Key + TEXT("=")))
    {
        FString Tail = Params.RightChop(Key.Len() + 2);
        return Tail.TrimStartAndEnd().Split(TEXT(" "), &Left, &Right) ? Left : Tail;
    }
    return FString();
}

int32 USAImportCommandlet::Main(const FString& Params)
{
    FString ModelJsonPath    = ParseParam(Params, TEXT("Model"));
    FString AnimJsonPath     = ParseParam(Params, TEXT("Anim"));
    FString MaterialJsonPath = ParseParam(Params, TEXT("Material"));
    FString OutputBasePath   = ParseParam(Params, TEXT("Output"));
    FString AssetNameOverride = ParseParam(Params, TEXT("AssetName"));
    FString SkeletonOverride = ParseParam(Params, TEXT("Skeleton"));

    if (OutputBasePath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Required parameter -Output= not specified"));
        return 1;
    }

    UE_LOG(LogTemp, Display, TEXT("========================================"));
    UE_LOG(LogTemp, Display, TEXT("SAImport Commandlet"));
    UE_LOG(LogTemp, Display, TEXT("  Params:   %s"), *Params);
    UE_LOG(LogTemp, Display, TEXT("  Params:   %s"), *Params);
    UE_LOG(LogTemp, Display, TEXT("  Model:    %s"), *ModelJsonPath);
    UE_LOG(LogTemp, Display, TEXT("  Anim:     %s"), *AnimJsonPath);
    UE_LOG(LogTemp, Display, TEXT("  Material: %s"), *MaterialJsonPath);
    UE_LOG(LogTemp, Display, TEXT("  Output:   %s"), *OutputBasePath);
    UE_LOG(LogTemp, Display, TEXT("========================================"));

    // --- Mode 3: Material Import ---
    if (!MaterialJsonPath.IsEmpty())
    {
        if (!IFileManager::Get().FileExists(*MaterialJsonPath))
        {
            UE_LOG(LogTemp, Error, TEXT("Material JSON not found: %s"), *MaterialJsonPath);
            return 1;
        }

        FSAModelData ModelData;
        if (!SAModelImporter::ParseFromFile(MaterialJsonPath, ModelData))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to parse material JSON"));
            return 1;
        }

        if (ModelData.Materials.Num() == 0)
        {
            UE_LOG(LogTemp, Error, TEXT("No material data in JSON"));
            return 1;
        }

        // Load existing mesh for material slot assignment
        USkeletalMesh* TargetMesh = nullptr;
        FString MeshPath = ParseParam(Params, TEXT("MeshPath"));
        if (!MeshPath.IsEmpty())
        {
            TargetMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
            if (!TargetMesh)
            {
                UE_LOG(LogTemp, Error, TEXT("Mesh not found: %s"), *MeshPath);
                return 1;
            }
        }

        TArray<UMaterial*> Results = SAMaterialImporter::BuildAll(
            ModelData, TargetMesh, OutputBasePath);

        UE_LOG(LogTemp, Display, TEXT("Material import done: %d/%d"),
            Results.Num(), ModelData.Materials.Num());
        return Results.Num() > 0 ? 0 : 1;
    }

    // --- Mode 2: Animation Import ---
    if (!AnimJsonPath.IsEmpty())
    {
        if (!IFileManager::Get().FileExists(*AnimJsonPath))
        {
            UE_LOG(LogTemp, Error, TEXT("Animation JSON not found: %s"), *AnimJsonPath);
            return 1;
        }

        // Parse AssetName from animation JSON for naming (command-line override takes priority)
        FString AnimAssetName = AssetNameOverride;
        {
            FString JsonStr;
            if (AnimAssetName.IsEmpty())
        {
            if (FFileHelper::LoadFileToString(JsonStr, *AnimJsonPath))
            {
                TSharedPtr<FJsonObject> Root;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
                if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
                {
                    Root->TryGetStringField(TEXT("AssetName"), AnimAssetName);
                }
            }
        }
        }

        // Fallback: derive AssetName from OutputBasePath last component
        if (AnimAssetName.IsEmpty())
        {
            AnimAssetName = FPaths::GetCleanFilename(OutputBasePath);
        }
        UE_LOG(LogTemp, Display, TEXT("  AnimAssetName: %s"), *AnimAssetName);

        // Try loading skeleton (use -Skeleton override or derive from AssetName)
    USkeleton* Skeleton = nullptr;
    USkeletalMesh* PreviewMesh = nullptr;

    // Derive skeleton name
    FString SkelName = SkeletonOverride;
    if (SkelName.IsEmpty())
        SkelName = AnimAssetName.IsEmpty() ? TEXT("Sekiro_Skeleton") : AnimAssetName + TEXT("_Skeleton");
    FString ModelName = AnimAssetName.IsEmpty() ? TEXT("Sekiro_Model") : AnimAssetName + TEXT("_Model");
    UE_LOG(LogTemp, Display, TEXT("  Looking for skeleton: %s"), *SkelName);

    // First: load by UE object path
    FString SkeletonPath = FString::Printf(TEXT("%s/%s.%s"), *OutputBasePath, *SkelName, *SkelName);
    Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (Skeleton)
        UE_LOG(LogTemp, Display, TEXT("  Found skeleton at: %s"), *SkeletonPath);
        FString PreviewMeshPath = FString::Printf(TEXT("%s/%s.%s"), *OutputBasePath, *ModelName, *ModelName);
        PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
        if (PreviewMesh) UE_LOG(LogTemp, Display, TEXT("  Preview mesh: %s"), *PreviewMeshPath);

        if (!Skeleton)
        {
            // Second: load from mesh
            FString MeshPath = FString::Printf(TEXT("%s/%s.%s"),
                *OutputBasePath, *ModelName, *ModelName);
            USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
            if (Mesh)
            {
                Skeleton = Mesh->GetSkeleton();
                PreviewMesh = Mesh;
            }
        }

        if (!Skeleton)
        {
            // Last resort: import model
            if (!ModelJsonPath.IsEmpty() && IFileManager::Get().FileExists(*ModelJsonPath))
            {
                USkeletalMesh* TempMesh = nullptr;
                TArray<FString> EmptyTexDirs;
                if (SAModelImporter::Import(ModelJsonPath, OutputBasePath, EmptyTexDirs, TempMesh, Skeleton))
                {
                    PreviewMesh = TempMesh;
                }
            }
        }

        if (!Skeleton)
        {
            UE_LOG(LogTemp, Error, TEXT("No skeleton found. Import model first."));
            return 1;
        }

        // Parse animation JSON
        FSAAnimData AnimData;
        if (!SAAnimationImporter::ParseFromFile(AnimJsonPath, AnimData))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to parse animation JSON: %s"), *AnimJsonPath);
            return 1;
        }

        if (AnimData.Clips.Num() == 0)
        {
            UE_LOG(LogTemp, Error, TEXT("No animation clips in JSON"));
            return 1;
        }

        // Optional name filter
        TArray<FString> AnimNameFilter;
        FString AnimNameStr = ParseParam(Params, TEXT("AnimName"));
        if (!AnimNameStr.IsEmpty())
        {
            AnimNameStr.ParseIntoArray(AnimNameFilter, TEXT(","), true);
            UE_LOG(LogTemp, Display, TEXT("  Filter anims: %d names"), AnimNameFilter.Num());
        }

        TArray<UAnimSequence*> Results = SAAnimationImporter::BuildBatch(
            AnimData, Skeleton, PreviewMesh, OutputBasePath / TEXT("Animations"), AnimAssetName, AnimNameFilter);

        UE_LOG(LogTemp, Display, TEXT("Animation import done: %d/%d"),
            Results.Num(), AnimData.Clips.Num());
        return Results.Num() > 0 ? 0 : 1;
    }

    // --- Mode 1: Model Import ---
    if (ModelJsonPath.IsEmpty() || !IFileManager::Get().FileExists(*ModelJsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Model JSON not found: %s"), *ModelJsonPath);
        return 1;
    }

    USkeletalMesh* Mesh = nullptr;
    USkeleton* Skeleton = nullptr;

    // Read OriginalAssetName from JSON to find asset-specific textures
    // Texture dirs: Output/Textures/{OriginalAssetName}/ + Output/Textures/Shared/
    FString ProjectDir = FPaths::ProjectDir();
    FString AssetTexDir, SharedTexDir;
    {
        FString JsonStr;
        if (FFileHelper::LoadFileToString(JsonStr, *ModelJsonPath))
        {
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
            if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
            {
                FString OrigAssetName;
                if (Root->TryGetStringField(TEXT("OriginalAssetName"), OrigAssetName) && !OrigAssetName.IsEmpty())
                {
                    AssetTexDir = FPaths::ConvertRelativePathToFull(
                        ProjectDir / TEXT("Output") / TEXT("Textures") / *OrigAssetName);
                }
            }
        }
    }
    SharedTexDir = FPaths::ConvertRelativePathToFull(
        ProjectDir / TEXT("Output") / TEXT("Textures") / TEXT("Shared"));

    TArray<FString> TextureSourceDirs;
    if (!AssetTexDir.IsEmpty() && IFileManager::Get().DirectoryExists(*AssetTexDir))
        TextureSourceDirs.Add(AssetTexDir);
    if (IFileManager::Get().DirectoryExists(*SharedTexDir))
        TextureSourceDirs.Add(SharedTexDir);

    if (!SAModelImporter::Import(ModelJsonPath, OutputBasePath, TextureSourceDirs, Mesh, Skeleton))
    {
        UE_LOG(LogTemp, Error, TEXT("Model import failed"));
        return 1;
    }

    UE_LOG(LogTemp, Display, TEXT("Model import done"));
    UE_LOG(LogTemp, Display, TEXT("  Skeleton: %s"), *Skeleton->GetPathName());
    UE_LOG(LogTemp, Display, TEXT("  Mesh:     %s"), *Mesh->GetPathName());
    return 0;
}