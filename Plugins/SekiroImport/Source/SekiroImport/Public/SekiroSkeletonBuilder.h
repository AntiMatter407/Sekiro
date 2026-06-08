#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class USkeleton;
class UPackage;

/// 骨架构建器：从骨骼数据创建UE USkeleton资产
class SEKIROIMPORT_API FSekiroSkeletonBuilder
{
public:
    /// 从中间骨骼数据构建USkeleton
    /// @param Bones 骨骼数组（必须按父子层级顺序排列，父骨骼在子骨骼之前）
    /// @param SkeletonName 骨架名称
    /// @param PackagePath UE内容路径，如 /Game/Characters/Sekiro/Skeleton
    /// @return 创建的USkeleton，失败返回nullptr
    static USkeleton* Build(const TArray<FSekiroImportBone>& Bones, const FString& SkeletonName, const FString& PackagePath);

    /// 从World变换推导Local变换（仅处理有World变换数据的骨骼）
    static void DeriveLocalFromWorld(TArray<FSekiroImportBone>& Bones);

    /// 将模型JSON的WorldPos合并到动画骨骼中，使参考姿态匹配模型的绑定姿态
    /// @param AnimBones 动画JSON的146骨骼（含LocalPos，需按层级顺序排列）
    /// @param ModelBones 模型JSON的93骨骼（含WorldPos）
    static void MergeModelWorldTransforms(TArray<FSekiroImportBone>& AnimBones, const TArray<FSekiroImportBone>& ModelBones);

    /// 将仅存在于模型中的骨骼（Model-Only）追加到动画骨骼列表末尾
    /// 模型中有但动画中没有的骨骼（如オブジェクト002），需要加入骨架以供蒙皮引用
    /// @param AnimBones 动画骨骼列表（已合并ModelWorldTransform），追加到此数组
    /// @param ModelBones 模型JSON的全部骨骼（含WorldPos和ParentName）
    /// @param MeshBoneNames 所有网格BoneIdxToName中引用的骨骼名集合（用于判断零位骨骼是否被使用）
    static void AppendModelOnlyBones(TArray<FSekiroImportBone>& AnimBones, const TArray<FSekiroImportBone>& ModelBones, const TSet<FName>& MeshBoneNames);

    /// 施加ExportRoot旋转 (RotZ(180)*RotX(90)) + 插入ExportRoot虚拟根骨骼
    /// 对应Blender管线 ExportRoot(Z=180) + Armature(X=90) 父级变换。
    /// 插入ExportRoot避免将OrientQ烘焙到Master的LocalQuat导致渲染异常。
    /// @param Bones 骨骼数组（含World变换，Y-up cm空间，将插入ExportRoot并转换为UE5空间）
    /// @post Bones[0] = ExportRoot, Bones.Num() = 输入 + 1
    static void ApplyExportRootOrientation(TArray<FSekiroImportBone>& Bones);

private:
    /// 验证骨骼层级顺序是否正确（父骨骼必须在子骨骼之前）
    static bool ValidateHierarchyOrder(const TArray<FSekiroImportBone>& Bones);
};
