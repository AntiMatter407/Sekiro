#pragma once

#include "CoreMinimal.h"

/// Sekiro使用米(m), UE使用厘米(cm)
inline constexpr float SekiroToUEScale = 100.0f;

/// 中间骨骼数据（已转换到UE坐标系）
struct FSekiroImportBone
{
    FName Name;
    FName ParentName;                          // 解析阶段的ParentName，Build阶段转为ParentIndex
    int32 ParentIndex = INDEX_NONE;
    FVector LocalTranslation = FVector::ZeroVector;
    FQuat LocalRotation = FQuat::Identity;
    FVector LocalScale = FVector::OneVector;
    FVector WorldTranslation = FVector::ZeroVector; // World位移（UE坐标系）
    FQuat WorldRotation = FQuat::Identity;          // World旋转（UE坐标系）
    FVector WorldScale = FVector::OneVector;         // World缩放
};

/// 中间顶点数据（已转换到UE坐标系）
struct FSekiroImportVertex
{
    FVector3f Position = FVector3f::ZeroVector;
    FVector3f Normal = FVector3f::ZeroVector;
    FVector2f UV = FVector2f::ZeroVector;
    uint16 BoneIndices[4] = { 0, 0, 0, 0 };
    float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // 骨骼影响数（固定4，零权重由循环内 Weight>0 检查过滤）
    int32 GetNumInfluences() const { return 4; }
};

/// 网格体Section（一个材质槽对应一个Section）
struct FSekiroImportMeshSection
{
    FString PartName;
    int32 MaterialIndex = 0;
    TArray<FSekiroImportVertex> Vertices;
    TArray<FIntVector> Triangles;          // 三个顶点索引为一组
    TMap<int32, FName> BoneIdxToName;      // 局部骨骼索引 → 骨骼名（用于重映射到完整骨架）
};

/// 材质描述
struct FSekiroImportMaterial
{
    FString Name;
    FString BlendMode;
    FString ShaderPath;
    FString MTDPath;
    TMap<FString, FString> TextureSlots;   // 参数名 → 贴图路径
    TArray<FString> AvailableTextures;
};

/// 模型解析结果
struct FSekiroModelData
{
    FString SkeletonName;
    TArray<FSekiroImportBone> Bones;
    TArray<FSekiroImportMeshSection> Meshes;
    TArray<FSekiroImportMaterial> Materials;
};

/// 单个动画片段
struct FSekiroAnimationClip
{
    FString Name;
    float Duration = 0.0f;
    int32 FrameCount = 0;
    float SampleRate = 30.0f;
    TArray<FName> BoneNames;               // 按骨架骨骼索引顺序排列的骨骼名（146个）
    TArray<FTransform> ReferenceLocalTransforms; // 参考姿态Local变换 (Y-up HKX空间, cm缩放)
    TArray<TArray<FTransform>> FrameData;  // [FrameIndex][BoneIndex] Y-up HKX空间, cm缩放
};
