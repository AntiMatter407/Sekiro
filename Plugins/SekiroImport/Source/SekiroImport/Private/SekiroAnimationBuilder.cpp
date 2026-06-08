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

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    const int32 SkeletonBoneCount = RefSkel.GetNum();
    const int32 AnimBoneCount = Clip.BoneNames.Num();

    if (AnimBoneCount == 0)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画 '%s' 没有骨骼数据"), *Clip.Name);
        return nullptr;
    }

    // 动画骨骼数 <= 骨架骨骼数 (骨架可能有额外的Model-Only骨骼)
    if (AnimBoneCount > SkeletonBoneCount)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画骨骼数(%d) > 骨架骨骼数(%d)"), AnimBoneCount, SkeletonBoneCount);
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

    // 构建骨骼名→动画骨骼索引查找表
    TMap<FName, int32> BoneNameToAnimIdx;
    for (int32 i = 0; i < AnimBoneCount; ++i)
    {
        BoneNameToAnimIdx.Add(Clip.BoneNames[i], i);
    }

    // OrientQ = RotZ(180°) * RotX(90°)，与SkeletonBuilder一致
    static const FQuat OrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);

    // 逐骨骼分配键值数组
    TArray<TArray<FVector>> AllPosKeys;
    TArray<TArray<FQuat>> AllRotKeys;
    TArray<TArray<FVector>> AllScaleKeys;
    AllPosKeys.SetNum(SkeletonBoneCount);
    AllRotKeys.SetNum(SkeletonBoneCount);
    AllScaleKeys.SetNum(SkeletonBoneCount);
    for (int32 i = 0; i < SkeletonBoneCount; ++i)
    {
        AllPosKeys[i].Reserve(Clip.FrameData.Num());
        AllRotKeys[i].Reserve(Clip.FrameData.Num());
        AllScaleKeys[i].Reserve(Clip.FrameData.Num());
    }

    // ========================================================================
    // 3-Pass 算法：逐帧计算 LocalUE（对齐Phase 1 ApplyExportRootOrientation）
    //
    //   Pass 1: FK in HKX → WorldHKX[i] = LocalHKX[i] * ParentWorldHKX
    //   Pass 2: OrientQ → WorldUE[i] = OrientQ * WorldHKX[i]
    //   Pass 3: Derive Local → LocalUE[i] = WorldUE[i].GetRelativeTransform(ParentWorldUE)
    // ========================================================================

    TArray<FTransform> WorldUE;       // 每帧临时数组
    WorldUE.SetNum(SkeletonBoneCount);

    for (int32 Frame = 0; Frame < Clip.FrameData.Num(); ++Frame)
    {
        // Pass 1+2: 逐骨骼 (按层级顺序) FK + OrientQ → WorldUE
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            const int32* AnimBoneIdxPtr = BoneNameToAnimIdx.Find(BoneName);

            // 获取该骨骼在此帧的Local HKX变换
            FTransform LocalHKX = FTransform::Identity;
            if (AnimBoneIdxPtr != nullptr && *AnimBoneIdxPtr < Clip.FrameData[Frame].Num())
            {
                LocalHKX = Clip.FrameData[Frame][*AnimBoneIdxPtr];
            }

            // Pass 1: FK World HKX = LocalHKX * ParentWorldHKX
            FTransform WorldHKX;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                // 从ParentWorldUE逆推ParentWorldHKX
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ParentWorldHKX;
                ParentWorldHKX.SetTranslation(OrientQInv.RotateVector(WorldUE[ParentIdx].GetTranslation()));
                ParentWorldHKX.SetRotation(OrientQInv * WorldUE[ParentIdx].GetRotation());
                ParentWorldHKX.SetScale3D(WorldUE[ParentIdx].GetScale3D());
                WorldHKX = LocalHKX * ParentWorldHKX;
            }
            else
            {
                WorldHKX = LocalHKX;
            }

            // Pass 2: OrientQ → World UE
            WorldUE[BoneIdx].SetTranslation(OrientQ.RotateVector(WorldHKX.GetTranslation()));
            WorldUE[BoneIdx].SetRotation(OrientQ * WorldHKX.GetRotation());
            WorldUE[BoneIdx].SetScale3D(WorldHKX.GetScale3D());
        }

        // Pass 3: Derive Local UE → 写入键值数组
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);

            FTransform LocalUE;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                LocalUE = WorldUE[BoneIdx].GetRelativeTransform(WorldUE[ParentIdx]);
            }
            else
            {
                LocalUE = WorldUE[BoneIdx];
            }

            AllPosKeys[BoneIdx].Add(LocalUE.GetTranslation());
            AllRotKeys[BoneIdx].Add(LocalUE.GetRotation());
            AllScaleKeys[BoneIdx].Add(LocalUE.GetScale3D());
        }
    }

    // 通过IAnimationDataController写入动画曲线
    IAnimationDataController& Controller = AnimSeq->GetController();
    Controller.OpenBracket(NSLOCTEXT("SekiroImport", "ImportAnim", "导入Sekiro动画"));

    Controller.InitializeModel();
    Controller.SetNumberOfFrames(NumFrames);
    Controller.SetFrameRate(FrameRate);

    for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
    {
        const FName BoneName = RefSkel.GetBoneName(BoneIdx);
        Controller.AddBoneCurve(BoneName);
        Controller.SetBoneTrackKeys(BoneName, AllPosKeys[BoneIdx], AllRotKeys[BoneIdx], AllScaleKeys[BoneIdx]);
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
            *Clip.Name, Clip.FrameCount, SkeletonBoneCount, *PackageFileName);
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
