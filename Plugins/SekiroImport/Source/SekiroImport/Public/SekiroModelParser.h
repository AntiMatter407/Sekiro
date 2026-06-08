#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

/// 模型JSON解析器：读取Sekiro_model_hkx.json，输出FSekiroModelData
class SEKIROIMPORT_API FSekiroModelParser
{
public:
    /// 从文件解析模型数据
    /// @param FilePath 模型JSON文件路径
    /// @param OutData 输出解析结果
    /// @return 成功返回true
    static bool ParseFromFile(const FString& FilePath, FSekiroModelData& OutData);

private:
    /// 将Sekiro坐标系(Y-up)转换为UE坐标系(Z-up)
    /// Sekiro: X=右, Y=上, Z=前  →  UE: X=右, Y=前, Z=上
    /// 映射: (x, y, z) → (x, z, -y)
    static FVector SekiroToUnrealVector(const FVector& V);
    static FVector3f SekiroToUnrealVector(const FVector3f& V);
    static FQuat SekiroToUnrealQuat(const FQuat& Q);

    /// 从JSON数组读取FVector (长度=3)
    static FVector ParseVector3(const TArray<TSharedPtr<FJsonValue>>& Arr);
    static FVector3f ParseVector3f(const TArray<TSharedPtr<FJsonValue>>& Arr);

    /// 从JSON数组读取FQuat (xyzw, 长度=4)
    static FQuat ParseQuat(const TArray<TSharedPtr<FJsonValue>>& Arr);

    /// 从JSON数组读取FVector2f (长度=2)
    static FVector2f ParseVector2f(const TArray<TSharedPtr<FJsonValue>>& Arr);

    /// 解析骨骼数组
    static void ParseBones(const TArray<TSharedPtr<FJsonValue>>& BonesArray, TArray<FSekiroImportBone>& OutBones);

    /// 解析材质数组
    static void ParseMaterials(const TArray<TSharedPtr<FJsonValue>>& MatsArray, TArray<FSekiroImportMaterial>& OutMaterials);

    /// 解析网格体数组
    static void ParseMeshes(const TArray<TSharedPtr<FJsonValue>>& MeshesArray, TArray<FSekiroImportMeshSection>& OutMeshes);

    /// 解析单个顶点
    static FSekiroImportVertex ParseVertex(const TSharedPtr<FJsonObject>& VertObj);
};
