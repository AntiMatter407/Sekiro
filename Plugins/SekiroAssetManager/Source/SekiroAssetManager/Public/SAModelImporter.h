#pragma once

#include "CoreMinimal.h"
#include "SAImportData.h"

class USkeletalMesh;
class USkeleton;

/// 模型导入器：解析 Sekiro_model.json → 构建 USkeletalMesh
class SAModelImporter
{
public:
    /// 从 JSON 文件导入骨架和网格体
    /// @param JsonPath Sekiro_model.json 路径
    /// @param TargetPackagePath UE 目标包路径（如 "/Game/Sekiro/Characters/Sekiro"）
    /// @param OutSkeletalMesh 输出的骨骼网格体
    /// @param OutSkeleton 输出的骨架
    /// @return 成功返回 true
    static bool Import(const FString& JsonPath, const FString& TargetPackagePath, const TArray<FString>& TextureSourceDirs,
                       USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton, bool bImportMaterials = true);

    /// 解析 JSON 到内存数据结构（公开，供材质/动画模块读取材质和骨骼数据）
    static bool ParseFromFile(const FString& JsonPath, FSAModelData& OutData);

private:

    /// 解析骨骼数组
    static void ParseBones(const TArray<TSharedPtr<FJsonValue>>& BonesArray,
                           TArray<FSAImportBone>& OutBones);

    /// 解析使用 UE 局部空间声明的可选无蒙皮参考骨骼
    static bool ParseAuxiliaryBones(const TArray<TSharedPtr<FJsonValue>>& BonesArray,
                                    TArray<FSAImportBone>& OutBones);

    /// 解析材质数组（含 ResolvedMaterials）
    static void ParseMaterials(const TArray<TSharedPtr<FJsonValue>>& MatsArray,
                               const TArray<TSharedPtr<FJsonValue>>* ResolvedMatsArray,
                               TArray<FSAImportMaterial>& OutMaterials);

    /// 解析网格体数组
    static void ParseMeshes(const TArray<TSharedPtr<FJsonValue>>& MeshesArray,
                            TArray<FSAImportMeshSection>& OutMeshes);

    /// 解析单个顶点
    static FSAImportVertex ParseVertex(const TSharedPtr<FJsonObject>& VertObj);

    /// 收集所有网格 Section 引用的骨骼名集合
    /// 追加 ModelOnly 骨骼（被网格引用但主骨架中没有的骨骼）
    /// 拓扑追加：父骨骼先于子骨骼加入，父骨骼缺失则挂到 Root(0)
    /// @param MainBones 主骨架骨骼（会被修改，追加新骨骼）
    /// @param FlverBones FLVER 原始骨骼（从中查找 ModelOnly 骨骼的数据）
    /// @param MeshBoneNames 所有网格引用的骨骼名集合
    /// 构建 USkeleton
    static USkeleton* BuildSkeleton(const TArray<FSAImportBone>& Bones,
                                    const TArray<FSAImportBone>& AuxiliaryBones,
                                    const FString& SkeletonName,
                                    const FString& PackagePath);

    /// 构建 USkeletalMesh
    static USkeletalMesh* BuildSkeletalMesh(const FSAModelData& ModelData,
                                            USkeleton* Skeleton,
                                            const FString& PackagePath);
};
