#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

struct FReferenceSkeleton;
class USkeletalMesh;
class USkeleton;

/// 骨骼网格体构建器：从FSekiroModelData构建USkeletalMesh
class SEKIROIMPORT_API FSekiroSkeletalMeshBuilder
{
public:
    /// 从模型数据构建骨骼网格体
    /// @param ModelData 模型解析结果
    /// @param Skeleton 已构建的骨架（含完整146骨骼+IK）
    /// @param PackagePath UE内容路径，如 /Game/Characters/Sekiro/Mesh
    /// @return 创建的USkeletalMesh，失败返回nullptr
    static USkeletalMesh* Build(const FSekiroModelData& ModelData, USkeleton* Skeleton, const FString& PackagePath);

private:
    /// 构建 BoneName → SkeletonIndex 查找表
    static TMap<FName, int32> BuildSkeletonBoneMap(const FReferenceSkeleton& RefSkel);

    /// 填充顶点权重，将局部BoneIndices通过BoneIdxToName重映射到骨架索引
    /// @param BoneWorldPositions 骨架所有骨骼的FK参考姿态World位置（用于Fallback最近骨骼查找）
    static void RemapInfluences(TArray<SkeletalMeshImportData::FRawBoneInfluence>& OutInfluences,
        const FSekiroImportMeshSection& Section, int32 GlobalVertOffset,
        const TMap<FName, int32>& BoneNameToSkelIndex, const TMap<int32, FName>& BoneIdxToName,
        const TArray<FVector>& BoneWorldPositions);

    /// 从骨架的ReferenceSkeleton填充ImportData.RefBonesBinary
    static void FillRefBones(const FReferenceSkeleton& RefSkel, TArray<SkeletalMeshImportData::FBone>& OutRefBones);
};
