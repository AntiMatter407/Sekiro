#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

/// Sekiro 使用米(m), UE 使用厘米(cm)
// 使用 SekiroImport 中的 SekiroToUEScale 定义

/// 中间骨骼数据
struct FSAImportBone
{
    FName Name;
    FName ParentName;
    int32 ParentIndex = INDEX_NONE;
    FVector LocalTranslation = FVector::ZeroVector;
    FQuat LocalRotation = FQuat::Identity;
    FVector LocalScale = FVector::OneVector;
    FVector WorldTranslation = FVector::ZeroVector;
    FQuat WorldRotation = FQuat::Identity;
    FVector WorldScale = FVector::OneVector;

    // DSAnimStudio-style Nub flag (FLVER NodeFlags.Disabled)
    // Nub bones get their hierarchy/reference FK from HKX at runtime
    bool bIsNub = false;
    int32 Flags = 0;
};

/// 中间顶点数据
struct FSAImportVertex
{
    FVector3f Position = FVector3f::ZeroVector;
    FVector3f Normal = FVector3f::ZeroVector;
    FVector2f UV = FVector2f::ZeroVector;
    uint16 BoneIndices[4] = { 0, 0, 0, 0 };
    float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    int32 GetNumInfluences() const { return 4; }
};

/// 网格体 Section
struct FSAImportMeshSection
{
    FString PartName;
    int32 MaterialIndex = 0;
    TArray<FSAImportVertex> Vertices;
    TArray<FIntVector> Triangles;
    TMap<int32, FName> BoneIdxToName;
};

/// 材质描述
UENUM()
enum class ESekiroShaderType : uint8
{
    Standard,
    SSS,
    Fur,
    DetailBlend,
    FurCloth,
    DetailBlendCloth,
    FresnelBlend,
    FresnelBlendCloth,
    SSSCloth,
    Skin,
    Eye,
    Cloth,
};

struct FSAImportMaterial
{
    FString Name;
    FString MTDPath;
    ESekiroShaderType ShaderType = ESekiroShaderType::Standard;
    FString ShaderPath;
    FString ResolvedBlendMode;
    float ClipValue = 0.0f;
    bool bTwoSided = false;
    bool bIsFur = false;
    bool bIsHair = false;
    bool bIsCloth = false;
    bool bIsDecal = false;
    FString DrawStep;
    TMap<FString, FString> ResolvedTextures; // 语义键 → 纹理文件名
};

/// 模型解析结果
struct FSAModelData
{
    FString AssetName;
    FString OriginalAssetName;
    FString SkeletonName;
    TArray<FSAImportBone> Bones;          // 主骨架骨骼（来自 HKX 骨架 + 回退时含 FLVER 骨骼）
    TArray<FSAImportBone> FlverBones;     // 可选的 FLVER 原始骨骼树（用于 ModelOnly 追加的数据源）
    TArray<FSAImportBone> AuxiliaryBones; // 可选的无蒙皮参考骨骼，变换使用 UE 局部空间（厘米、四元数）
    TArray<FSAImportMeshSection> Meshes;
    TArray<FSAImportMaterial> Materials;
};

// ============================================================================
// 动画数据结构
// ============================================================================

/// 动画帧的骨骼变换（HKX 空间，Y-up，单位米）
struct FSAAnimBoneTransform
{
    FVector Translation = FVector::ZeroVector;   // 位移 (m)
    FQuat Rotation = FQuat::Identity;             // 旋转 (四元数, xyz,w 顺序)
    FVector Scale = FVector::OneVector;           // 缩放
};

/// 单帧根运动（HKX 空间，Y-up，单位米，Yaw 为绕 HKX Y 轴弧度）
struct FSAAnimRootMotionFrame
{
    FVector Translation = FVector::ZeroVector;    // 根运动位移 (m)
    float Yaw = 0.0f;                             // 根运动水平旋转
};

/// 单个动画片段
struct FSAAnimClip
{
    FString Name;                     // 动画名（原始名，如 "a000_000000"）
    float Duration = 0.0f;            // 时长(秒)
    float SampleRate = 30.0f;         // 帧率
    int32 FrameCount = 0;             // 总帧数
    bool bHasRootMotion = false;      // 是否包含 HKX ReferenceFrame 根运动
    TArray<FName> BoneNames;              // 骨骼名（与 FrameData 骨骼维对齐）
    TArray<TArray<FSAAnimBoneTransform>> FrameData;  // [帧][骨骼] HKX空间变换
    TArray<FSAAnimRootMotionFrame> RootMotionFrames;  // [帧] HKX ReferenceFrame 根运动
};

/// 动画 JSON 解析结果（顶层结构）
struct FSAAnimData
{
    TArray<FSAAnimClip> Clips;
    TArray<FName> BoneNames;          // 全局骨骼名（146 根）
    TArray<int32> BoneParents;        // 全局父索引
    TArray<FSAAnimBoneTransform> BoneLocalTransforms;  // 参考姿势 Local HKX
};

// ============================================================================
// 公共常量
// ============================================================================

/// Sekiro 使用米(m), UE 使用厘米(cm)
inline constexpr float SekiroToUEScale = 100.0f;          // Sekiro 米 → UE 厘米

// ============================================================================
// 公共工具函数
// ============================================================================

/// 从 Havok Y-up (右手系) 到 UE5 Z-up (左手系) 的朝向旋转
inline FQuat GetOrientQ()
{
    return FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);
}

/// Sekiro 位移（米）→ UE 位移（厘米）
inline FVector SekiroToUnreal(const FVector& V) { return V * SekiroToUEScale; }

/// Sekiro 位移（米）→ UE 位移（厘米），FVector3f 重载
inline FVector3f SekiroToUnreal(const FVector3f& V) { return V * SekiroToUEScale; }

/// Sekiro 旋转（四元数）→ UE 旋转（恒等，仅坐标系转换在 GetOrientQ 中处理）
inline FQuat SekiroToUnreal(const FQuat& Q) { return Q; }

/// 从 JSON 数组解析 FVector（至少 3 个元素）
inline FVector ParseVector3(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 3)
        return FVector((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber());
    return FVector::ZeroVector;
}

/// 从 JSON 数组解析 FVector3f（至少 3 个元素）
inline FVector3f ParseVector3f(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 3)
        return FVector3f((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber());
    return FVector3f::ZeroVector;
}

/// 从 JSON 数组解析 FQuat（至少 4 个元素，x,y,z,w 顺序）
inline FQuat ParseQuat(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 4)
        return FQuat((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber(), (float)Arr[3]->AsNumber());
    return FQuat::Identity;
}

/// 从 JSON 数组解析 FVector2f（至少 2 个元素）
inline FVector2f ParseVector2f(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
    if (Arr.Num() >= 2)
        return FVector2f((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber());
    return FVector2f::ZeroVector;
}
