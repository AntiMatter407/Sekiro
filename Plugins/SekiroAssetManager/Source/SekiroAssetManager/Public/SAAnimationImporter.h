#pragma once

#include "CoreMinimal.h"
#include "SAImportData.h"

class UAnimSequence;
class USkeleton;
class USkeletalMesh;

/// Animation Importer: parse Sekiro anim JSON -> build UAnimSequence
/// 3-Pass algorithm (from SekiroAnimationBuilder):
///   Pass 1: FK in HKX -> accumulate from frame LocalHKX * parent WorldHKX
///   Pass 2: OrientQ -> FQuat(FVector(0,0,1),PI)*FQuat(FVector(1,0,0),PI/2) left-multiply rotation
///   Pass 3: Derive LocalUE -> WorldUE[i].GetRelativeTransform(ParentWorldUE)
class SAAnimationImporter
{
public:
    /// Parse animation JSON file to memory data structure
    /// @param JsonPath path to anim JSON file
    /// @param OutData output animation data structure
    /// @return true on success
    static bool ParseFromFile(const FString& JsonPath, FSAAnimData& OutData);

    /// Build single UAnimSequence from animation clip (3-Pass algorithm)
    /// @param Clip animation clip data
    /// @param Skeleton target skeleton
    /// @param PreviewMesh preview skeletal mesh (optional)
    /// @param PackagePath UE package path (e.g. "/Game/Characters/Sekiro/Anim_Sekiro_a000_000000")
    /// @return created UAnimSequence, nullptr on failure
    static UAnimSequence* Build(const FSAAnimClip& Clip, USkeleton* Skeleton,
                                 USkeletalMesh* PreviewMesh, const FString& PackagePath);

    /// Batch build all animation clips
    /// @param AnimData animation data (contains all Clips)
    /// @param Skeleton target skeleton
    /// @param PreviewMesh preview skeletal mesh (optional)
    /// @param BasePath UE base package path (e.g. "/Game/Characters/Sekiro")
    /// @param AssetName asset name for naming prefix (e.g. "Sekiro" -> Anim_Sekiro_xxx)
    /// @param AnimNames optional name filter (empty = build all)
    /// @return array of successfully created UAnimSequence
    static TArray<UAnimSequence*> BuildBatch(const FSAAnimData& AnimData, USkeleton* Skeleton,
                                              USkeletalMesh* PreviewMesh, const FString& BasePath,
                                              const FString& AssetName,
                                              const TArray<FString>& AnimNames = TArray<FString>());

private:
    /// Parse single transform object {"P":[x,y,z],"R":[qx,qy,qz,qw],"S":[sx,sy,sz]}
    static FSAAnimBoneTransform ParseTransform(const TSharedPtr<FJsonObject>& JsonObj);
};