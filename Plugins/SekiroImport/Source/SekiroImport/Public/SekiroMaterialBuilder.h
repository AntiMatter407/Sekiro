#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class USkeletalMesh;
class UMaterial;

/// 材质构建器：从FSekiroImportMaterial创建独立UMaterial
class SEKIROIMPORT_API FSekiroMaterialBuilder
{
public:
    /// 为骨骼网格体的所有材质槽创建独立材质
    static TArray<UMaterial*> BuildAll(const FSekiroModelData& ModelData, USkeletalMesh* SkeletalMesh, const FString& BasePath);
};
