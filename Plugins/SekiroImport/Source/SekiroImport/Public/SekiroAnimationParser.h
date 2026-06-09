#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

/// 动画JSON解析器：从Sekiro_animations.json提取骨架数据和动画片段
/// v1: 全量加载+DOM解析，适用于<500MB文件。未来v2将实现流式解析处理1GB+文件。
class SEKIROIMPORT_API FSekiroAnimationParser
{
public:
    /// 解析结果：包含骨架骨骼和动画片段
    struct FParseResult
    {
        TArray<FSekiroImportBone> Bones;          // 从动画JSON提取的完整骨骼(146根，含IK)
        TArray<FSekiroAnimationClip> Clips;        // 所有动画片段
    };

    /// 从文件解析动画数据（包括骨架骨骼和动画片段）
    /// @param FilePath 动画JSON文件路径
    /// @param OutResult 解析结果
    /// @param MaxAnimations 最大解析动画数，0=全部，用于测试和分批导入
    /// @param NamePrefixFilter 动画名称前缀过滤，仅导入以此为前缀的动画（空=全部）
    /// @return 成功返回true
    static bool ParseFromFile(const FString& FilePath, FParseResult& OutResult, int32 MaxAnimations = 0, const FString& NamePrefixFilter = TEXT(""));

    /// 仅解析骨架数据（不解析动画片段），用于先构建骨架
    /// @param FilePath 动画JSON文件路径
    /// @param OutBones 输出的骨骼数组
    /// @return 成功返回true
    static bool ParseSkeletonOnly(const FString& FilePath, TArray<FSekiroImportBone>& OutBones);

private:
    /// 坐标系转换工具
    static FVector SekiroToUnrealPos(const FVector& V);
    static FQuat SekiroToUnrealRot(const FQuat& Q);

    /// 解析单根骨骼的LocalTransform（从BoneLocalTransforms条目）
    static void ParseBoneLocalTransform(const TSharedPtr<FJsonObject>& TransformObj, FVector& OutPos, FQuat& OutRot, FVector& OutScale);

    /// 从BoneNames/BoneParents/BoneLocalTransforms构建骨骼数组
    static void BuildBonesFromAnimationData(
        const TArray<TSharedPtr<FJsonValue>>& BoneNames,
        const TArray<TSharedPtr<FJsonValue>>& BoneParents,
        const TArray<TSharedPtr<FJsonValue>>& BoneTransforms,
        TArray<FSekiroImportBone>& OutBones);

    /// 解析单个动画片段
    /// @param AnimObj 动画JSON对象
    /// @param BoneCount 骨骼数量
    /// @param SkeletonBones 参考姿态骨骼（用于填充Clip的ReferenceLocalTransforms）
    /// @param OutClip 输出的动画片段
    static bool ParseAnimationClip(const TSharedPtr<FJsonObject>& AnimObj, int32 BoneCount, const TArray<FSekiroImportBone>& SkeletonBones, FSekiroAnimationClip& OutClip);

    /// 解析单帧的骨骼变换数组
    static void ParseFrameBoneTransforms(const TArray<TSharedPtr<FJsonValue>>& BoneTransformArray, int32 ExpectedBoneCount, TArray<FTransform>& OutTransforms);
};
