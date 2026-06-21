#pragma once

#include "CoreMinimal.h"
#include "SAImportData.h"

class UMaterial;
class UTexture2D;
class USkeletalMesh;

/// 材质导入器：从模型 JSON 的 ResolvedMaterials → 创建 UMaterial
///
/// 构建流程（对齐 SekiroMaterialBuilder）：
///   1. 创建 UMaterial（NewObject）
///   2. 根据 ResolvedBlendMode 设置 BlendMode
///   3. 设置 bTwoSided
///   4. 从 ResolvedTextures 查找 UTexture2D
///   5. 创建纹理采样表达式，按语义键设置 SamplerType
///   6. 连接 albedo→BaseColor, normal→Normal, metallic→Metallic, roughness→Roughness
///   7. 材质编译 + 保存
class SAMaterialImporter
{
public:
    /// 从模型数据批量构建材质，并分配到骨骼网格体的材质槽
    /// @param ModelData 模型解析数据（含 Materials 数组）
    /// @param SkeletalMesh 目标骨骼网格体（可选，非空时自动分配材质）
    /// @param BasePackagePath 材质包的基路径（如 "/Game/Sekiro/Characters/Sekiro"）
    /// @return 创建的材质数组
    static TArray<UMaterial*> BuildAll(const FSAModelData& ModelData,
                                       USkeletalMesh* SkeletalMesh,
                                       const FString& BasePackagePath,
                                       const TArray<FString>& TextureSourceDirs = TArray<FString>());

    /// 构建单个材质
    /// @param Mat 材质导入数据
    /// @param PackagePath UE 目标包路径（如 "/Game/Sekiro/Characters/Sekiro/M_BD_M_9000_Body"）
    /// @return 创建的 UMaterial 指针，失败返回 nullptr
    static UMaterial* BuildSingle(const FSAImportMaterial& Mat,
                                  const FString& PackagePath,
                                  const TArray<FString>& TextureSourceDirs = TArray<FString>(),
                                  const FString& TexturePackagePath = TEXT(""));

private:
    // ── 工具函数 ──

    /// 将 ResolvedBlendMode 字符串转为 UE 枚举值
    /// "Opaque"→BLEND_Opaque, "Masked"→BLEND_Masked, "Translucent"→BLEND_Translucent
    static EBlendMode ParseBlendMode(const FString& ResolvedBlendMode,
                                     const FString& MatName,
                                     bool bIsFurHair);

    /// 根据语义键返回采样器类型
    /// albedo→Color, normal→Normal, metallic/roughness→LinearGrayscale
    static TEnumAsByte<EMaterialSamplerType> SamplerTypeForSemantic(const FString& SemanticKey);

    /// 根据语义键返回材质属性
    /// albedo→MP_BaseColor, normal→MP_Normal, metallic→MP_Metallic, roughness→MP_Roughness
    static EMaterialProperty PropertyForSemantic(const FString& SemanticKey);

    /// 清理材质名中的非法字符（替换为下划线）
    static FString SanitizeMaterialName(const FString& RawName);

    /// 从 ResolvedTextures 语义映射加载 UTexture2D
    /// @param ResolvedTextures 语义键 → 纹理文件名（如 "albedo" → "BD_M_9000_Body_a.dds"）
    /// @param SearchPaths 纹理搜索的 UE 包路径列表
    /// @return 语义键 → UTexture2D* 映射
    static TMap<FString, UTexture2D*> LoadTextures(
        const TMap<FString, FString>& ResolvedTextures,
        const TArray<FString>& SearchPaths,
        const TArray<FString>& TextureSourceDirs = TArray<FString>(),
        const FString& TexturePackagePath = TEXT(""));
};
