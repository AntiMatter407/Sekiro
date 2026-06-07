#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class USkeletalMesh;
class UMaterialInstanceConstant;

/// 材质构建器：从FSekiroImportMaterial创建UMaterialInstanceConstant
class SEKIROIMPORT_API FSekiroMaterialBuilder
{
public:
    /// 为骨骼网格体的所有材质槽创建材质实例
    /// @param ModelData 模型解析结果（含材质列表）
    /// @param SkeletalMesh 目标骨骼网格体（材质槽已创建）
    /// @param BasePath UE内容路径，如 /Game/Characters/Sekiro/Materials
    /// @return 创建的材质实例数组
    static TArray<UMaterialInstanceConstant*> BuildAll(const FSekiroModelData& ModelData, USkeletalMesh* SkeletalMesh, const FString& BasePath);

    /// 创建单个材质实例
    /// @param Material 材质描述
    /// @param PackagePath 材质Package的完整路径
    static UMaterialInstanceConstant* CreateMaterialInstance(const FSekiroImportMaterial& Material, const FString& PackagePath);
};
