#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class UAnimSequence;
class USkeleton;
class USkeletalMesh;

/// 动画构建器：从FSekiroAnimationClip创建UAnimSequence
class SEKIROIMPORT_API FSekiroAnimationBuilder
{
public:
    /// 从动画片段构建UAnimSequence
    /// @param Clip 动画片段数据
    /// @param Skeleton 目标骨架（动画骨骼直接对应骨架索引）
    /// @param PreviewMesh 预览骨骼网格体（可选）
    /// @param PackagePath UE内容路径，如 /Game/Characters/Sekiro/Anims
    /// @return 创建的UAnimSequence，失败返回nullptr
    static UAnimSequence* Build(const FSekiroAnimationClip& Clip, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, const FString& PackagePath);

    /// 批量构建动画
    /// @param Clips 动画片段数组
    /// @param Skeleton 目标骨架
    /// @param PreviewMesh 预览骨骼网格体（可选）
    /// @param BasePath UE内容基础路径，如 /Game/Characters/Sekiro/Anims
    /// @return 成功创建的UAnimSequence数组
    static TArray<UAnimSequence*> BuildBatch(const TArray<FSekiroAnimationClip>& Clips, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, const FString& BasePath);
};
