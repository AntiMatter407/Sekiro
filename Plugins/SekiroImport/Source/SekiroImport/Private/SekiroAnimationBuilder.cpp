#include "SekiroAnimationBuilder.h"
#include "SekiroImport.h"
#include "SekiroImportLog.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "ReferenceSkeleton.h"

// ============================================================================
// 单个动画构建
// ============================================================================

UAnimSequence* FSekiroAnimationBuilder::Build(const FSekiroAnimationClip& Clip, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, const FString& PackagePath)
{
    if (Clip.FrameCount < 2)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画 '%s' 帧数不足 (FrameCount=%d)"), *Clip.Name, Clip.FrameCount);
        return nullptr;
    }

    if (!Skeleton)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("骨架为空，无法构建动画 '%s'"), *Clip.Name);
        return nullptr;
    }

    const int32 NumBones = Clip.BoneNames.Num();
    if (NumBones == 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画 '%s' 没有骨骼数据"), *Clip.Name);
        return nullptr;
    }

    // 验证骨骼数量与骨架一致
    if (NumBones != Skeleton->GetReferenceSkeleton().GetNum())
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画骨骼数(%d)与骨架骨骼数(%d)不匹配"),
            NumBones, Skeleton->GetReferenceSkeleton().GetNum());
        return nullptr;
    }

    // 创建Package
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackagePath);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建动画Package: %s"), *PackagePath);
        return nullptr;
    }

    // 创建UAnimSequence
    FString AssetName = FString::Printf(TEXT("Anim_%s"), *Clip.Name);
    UAnimSequence* AnimSeq = NewObject<UAnimSequence>(Package, UAnimSequence::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    if (!AnimSeq)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法创建UAnimSequence: %s"), *AssetName);
        return nullptr;
    }

    AnimSeq->SetSkeleton(Skeleton);
    if (PreviewMesh)
    {
        AnimSeq->SetPreviewMesh(PreviewMesh);
    }

    // UE中NumFrames = 采样数 - 1（区间数）
    const int32 NumFrames = Clip.FrameCount - 1;
    const FFrameRate FrameRate((int32)Clip.SampleRate, 1);

    // 使用IAnimationDataController填充动画数据
    IAnimationDataController& Controller = AnimSeq->GetController();
    Controller.OpenBracket(NSLOCTEXT("SekiroImport", "ImportAnim", "导入Sekiro动画"));

    Controller.InitializeModel();
    Controller.SetNumberOfFrames(NumFrames);
    Controller.SetFrameRate(FrameRate);

    // 逐骨骼填充轨道（全部146骨骼，含IK）
    for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
    {
        const FName BoneName = Clip.BoneNames[BoneIdx];
        if (BoneName.IsNone()) continue;

        TArray<FVector> PosKeys;
        TArray<FQuat> RotKeys;
        TArray<FVector> ScaleKeys;

        PosKeys.Reserve(Clip.FrameData.Num());
        RotKeys.Reserve(Clip.FrameData.Num());
        ScaleKeys.Reserve(Clip.FrameData.Num());

        for (int32 Frame = 0; Frame < Clip.FrameData.Num(); ++Frame)
        {
            if (BoneIdx < Clip.FrameData[Frame].Num())
            {
                const FTransform& Transform = Clip.FrameData[Frame][BoneIdx];
                PosKeys.Add(Transform.GetTranslation());
                RotKeys.Add(Transform.GetRotation());
                ScaleKeys.Add(Transform.GetScale3D());
            }
            else
            {
                // 防御性：使用Identity
                PosKeys.Add(FVector::ZeroVector);
                RotKeys.Add(FQuat::Identity);
                ScaleKeys.Add(FVector::OneVector);
            }
        }

        Controller.AddBoneCurve(BoneName);
        Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
    }

    Controller.NotifyPopulated();
    Controller.CloseBracket();

    AnimSeq->MarkPackageDirty();

    // 保存
    FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;

    if (UPackage::SavePackage(Package, AnimSeq, *PackageFileName, SaveArgs))
    {
        UE_LOG(LogSekiroImport, Log, TEXT("动画构建成功: '%s' (%d帧, %d骨骼) → %s"),
            *Clip.Name, Clip.FrameCount, NumBones, *PackageFileName);
    }
    else
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画保存失败: '%s'"), *Clip.Name);
    }

    return AnimSeq;
}

// ============================================================================
// 批量构建
// ============================================================================

TArray<UAnimSequence*> FSekiroAnimationBuilder::BuildBatch(const TArray<FSekiroAnimationClip>& Clips, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, const FString& BasePath)
{
    TArray<UAnimSequence*> Results;
    Results.Reserve(Clips.Num());

    UE_LOG(LogSekiroImport, Log, TEXT("开始批量构建 %d 个动画 → %s/"), Clips.Num(), *BasePath);

    for (int32 i = 0; i < Clips.Num(); ++i)
    {
        const FSekiroAnimationClip& Clip = Clips[i];
        FString AnimPackagePath = FString::Printf(TEXT("%s/Anim_%s"), *BasePath, *Clip.Name);

        UE_LOG(LogSekiroImport, Verbose, TEXT("  [%d/%d] %s (%.2f秒, %d帧)"),
            i + 1, Clips.Num(), *Clip.Name, Clip.Duration, Clip.FrameCount);

        UAnimSequence* AnimSeq = Build(Clip, Skeleton, PreviewMesh, AnimPackagePath);
        if (AnimSeq)
        {
            Results.Add(AnimSeq);
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("批量动画构建完成: %d/%d 成功"), Results.Num(), Clips.Num());
    return Results;
}
