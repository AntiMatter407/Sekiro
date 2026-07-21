#include "SAAnimationImporter.h"
#include "Animation/AnimEnums.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/FeedbackContext.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"

/**
 * 加载已经存在的动画包，或为尚未导入的动画创建新包。
 * 本函数只能在游戏线程调用，因为它会同步加载或创建 UObject；它不会删除磁盘文件，也不会替换包内对象。
 *
 * @param PackagePath 合法的长包名，不包含对象名和扩展名。
 * @return 可用于查找或创建动画资产的包；已有包加载失败时返回 nullptr，避免意外覆盖原文件。
 */
static UPackage* LoadOrCreateAnimationPackage(const FString& PackagePath)
{
    check(IsInGameThread());

    UPackage* Package = FindPackage(nullptr, *PackagePath);
    if (Package)
    {
        Package->FullyLoad();
        return Package;
    }

    if (FPackageName::DoesPackageExist(PackagePath))
    {
        Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
        if (!Package)
        {
            UE_LOG(LogTemp, Error, TEXT("SAAnimationImporter: failed to load existing package without replacing it: %s"),
                *PackagePath);
            return nullptr;
        }
        Package->FullyLoad();
        return Package;
    }

    Package = CreatePackage(*PackagePath);
    if (Package)
    {
        Package->SetFlags(RF_Public | RF_Standalone);
    }
    return Package;
}

FSAAnimBoneTransform SAAnimationImporter::ParseTransform(const TSharedPtr<FJsonObject>& JsonObj)
{
    FSAAnimBoneTransform Result;
    if (!JsonObj.IsValid()) return Result;
    const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* S = nullptr;
    if (JsonObj->TryGetArrayField(TEXT("P"), P) && P && P->Num() >= 3)
    {
        Result.Translation.X = (float)(*P)[0]->AsNumber();
        Result.Translation.Y = (float)(*P)[1]->AsNumber();
        Result.Translation.Z = (float)(*P)[2]->AsNumber();
    }
    if (JsonObj->TryGetArrayField(TEXT("R"), R) && R && R->Num() >= 4)
    {
        Result.Rotation.X = (float)(*R)[0]->AsNumber();
        Result.Rotation.Y = (float)(*R)[1]->AsNumber();
        Result.Rotation.Z = (float)(*R)[2]->AsNumber();
        Result.Rotation.W = (float)(*R)[3]->AsNumber();
    }
    if (JsonObj->TryGetArrayField(TEXT("S"), S) && S && S->Num() >= 3)
    {
        Result.Scale.X = (float)(*S)[0]->AsNumber();
        Result.Scale.Y = (float)(*S)[1]->AsNumber();
        Result.Scale.Z = (float)(*S)[2]->AsNumber();
    }
    return Result;
}

static FSAAnimRootMotionFrame ParseRootMotionFrame(const TSharedPtr<FJsonObject>& JsonObj)
{
    FSAAnimRootMotionFrame Result;
    if (!JsonObj.IsValid()) return Result;

    const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
    if (JsonObj->TryGetArrayField(TEXT("P"), P) && P && P->Num() >= 3)
    {
        Result.Translation.X = (float)(*P)[0]->AsNumber();
        Result.Translation.Y = (float)(*P)[1]->AsNumber();
        Result.Translation.Z = (float)(*P)[2]->AsNumber();
    }

    double Yaw = 0.0;
    if (JsonObj->TryGetNumberField(TEXT("Yaw"), Yaw))
    {
        Result.Yaw = (float)Yaw;
    }
    else if (JsonObj->TryGetNumberField(TEXT("W"), Yaw))
    {
        Result.Yaw = (float)Yaw;
    }

    return Result;
}

static FTransform BuildRootMotionTransformUE(const FSAAnimRootMotionFrame& RootMotionFrame, const FQuat& OrientQ)
{
    const FQuat RootMotionRotationHKX(FVector(0.0f, 1.0f, 0.0f), RootMotionFrame.Yaw);
    FQuat RootMotionRotationUE = OrientQ * RootMotionRotationHKX * OrientQ.Inverse();
    RootMotionRotationUE.Normalize();

    FTransform RootMotionTransform;
    RootMotionTransform.SetTranslation(OrientQ.RotateVector(RootMotionFrame.Translation));
    RootMotionTransform.SetRotation(RootMotionRotationUE);
    RootMotionTransform.SetScale3D(FVector::OneVector);
    return RootMotionTransform;
}

bool SAAnimationImporter::ParseFromFile(const FString& JsonPath, FSAAnimData& OutData)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load animation JSON: %s"), *JsonPath);
        return false;
    }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse animation JSON: %s"), *JsonPath);
        return false;
    }
    // Parse BoneNames
    const TArray<TSharedPtr<FJsonValue>>* BoneNamesArr = nullptr;
    if (Root->TryGetArrayField(TEXT("BoneNames"), BoneNamesArr))
    {
        for (const auto& V : *BoneNamesArr)
            OutData.BoneNames.Add(FName(*V->AsString()));
    }
    // Parse BoneParents
    const TArray<TSharedPtr<FJsonValue>>* BoneParentsArr = nullptr;
    if (Root->TryGetArrayField(TEXT("BoneParents"), BoneParentsArr))
    {
        for (const auto& V : *BoneParentsArr)
            OutData.BoneParents.Add((int32)V->AsNumber());
    }
    // Parse BoneLocalTransforms
    const TArray<TSharedPtr<FJsonValue>>* LocalTransforms = nullptr;
    if (Root->TryGetArrayField(TEXT("BoneLocalTransforms"), LocalTransforms))
    {
        for (const auto& V : *LocalTransforms)
        {
            const TSharedPtr<FJsonObject>* Obj = nullptr;
            if (V->TryGetObject(Obj))
                OutData.BoneLocalTransforms.Add(ParseTransform(*Obj));
        }
    }
    // Parse Clips
    const TArray<TSharedPtr<FJsonValue>>* ClipsArr = nullptr;
    if (Root->TryGetArrayField(TEXT("Animations"), ClipsArr))
    {
        for (const auto& ClipVal : *ClipsArr)
        {
            const TSharedPtr<FJsonObject>* ClipObj = nullptr;
            if (!ClipVal->TryGetObject(ClipObj)) continue;
            FSAAnimClip Clip;
            Clip.Name = (*ClipObj)->GetStringField(TEXT("Name"));
            Clip.Duration = (float)(*ClipObj)->GetNumberField(TEXT("Duration"));
            Clip.SampleRate = (float)(*ClipObj)->GetNumberField(TEXT("SampleRate"));
            Clip.FrameCount = (*ClipObj)->GetIntegerField(TEXT("FrameCount"));
            Clip.BoneNames = OutData.BoneNames;  // Use global bone name order for this clip
            bool bJsonHasRootMotion = false;
            if ((*ClipObj)->TryGetBoolField(TEXT("HasRootMotion"), bJsonHasRootMotion))
            {
                Clip.bHasRootMotion = bJsonHasRootMotion;
            }

            const TArray<TSharedPtr<FJsonValue>>* FramesArr = nullptr;
            if ((*ClipObj)->TryGetArrayField(TEXT("Frames"), FramesArr))
            {
                for (const auto& FrameVal : *FramesArr)
                {
                    const TSharedPtr<FJsonObject>* FrameObj = nullptr;
                    if (!FrameVal->TryGetObject(FrameObj) || !FrameObj) continue;
                    const TArray<TSharedPtr<FJsonValue>>* BoneTransformsArr = nullptr;
                    TArray<FSAAnimBoneTransform> Frame;
                    if ((*FrameObj)->TryGetArrayField(TEXT("BoneTransforms"), BoneTransformsArr))
                    {
                        for (const auto& BoneVal : *BoneTransformsArr)
                        {
                            const TSharedPtr<FJsonObject>* BoneObj = nullptr;
                            if (BoneVal->TryGetObject(BoneObj) && BoneObj)
                                Frame.Add(ParseTransform(*BoneObj));
                            else
                                Frame.Add(FSAAnimBoneTransform());
                        }
                    }
                    Clip.FrameData.Add(MoveTemp(Frame));
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* RootMotionFramesArr = nullptr;
            if ((*ClipObj)->TryGetArrayField(TEXT("RootMotionFrames"), RootMotionFramesArr))
            {
                for (const TSharedPtr<FJsonValue>& RootMotionVal : *RootMotionFramesArr)
                {
                    const TSharedPtr<FJsonObject>* RootMotionObj = nullptr;
                    if (RootMotionVal->TryGetObject(RootMotionObj) && RootMotionObj)
                    {
                        Clip.RootMotionFrames.Add(ParseRootMotionFrame(*RootMotionObj));
                    }
                }
            }
            Clip.bHasRootMotion = Clip.bHasRootMotion || Clip.RootMotionFrames.Num() > 0;
            OutData.Clips.Add(Clip);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Parsed %d clips, %d bones from %s"),
        OutData.Clips.Num(), OutData.BoneNames.Num(), *JsonPath);
    return true;
}

/**
 * 将源动画骨骼数据转换为可安全参与 FTransform 运算的 HKX 局部变换。
 * UE 的 GetRelativeTransform 在父旋转未归一化时会直接返回 Identity，因此所有源四元数必须在进入
 * FTransform 之前统一归一化；非有限或近零四元数属于源数据错误，不能静默替换成 Identity。
 *
 * @param BoneTransform 源动画中的 HKX 局部变换。
 * @param ClipName 所属动画名，仅用于错误定位。
 * @param FrameIndex 所属帧索引，仅用于错误定位。
 * @param BoneName 所属骨骼名，仅用于错误定位。
 * @param OutTransform 成功时返回旋转已归一化的 HKX 局部变换。
 * @return 源四元数有效并成功构造变换时返回 true，否则记录错误并返回 false。
 * @note 无 UObject 访问，可在任意线程调用。
 */
static bool TryBuildHKXTransform(
    const FSAAnimBoneTransform& BoneTransform,
    const FString& ClipName,
    int32 FrameIndex,
    const FName& BoneName,
    FTransform& OutTransform)
{
    const double RotationSizeSquared = BoneTransform.Rotation.SizeSquared();
    if (BoneTransform.Rotation.ContainsNaN() || !FMath::IsFinite(RotationSizeSquared))
    {
        UE_LOG(LogTemp, Error,
            TEXT("SAAnimationImporter: non-finite quaternion in clip '%s', frame %d, bone '%s': (%.9f, %.9f, %.9f, %.9f)"),
            *ClipName,
            FrameIndex,
            *BoneName.ToString(),
            BoneTransform.Rotation.X,
            BoneTransform.Rotation.Y,
            BoneTransform.Rotation.Z,
            BoneTransform.Rotation.W);
        return false;
    }

    if (RotationSizeSquared <= UE_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Error,
            TEXT("SAAnimationImporter: near-zero quaternion in clip '%s', frame %d, bone '%s': size squared %.12g"),
            *ClipName,
            FrameIndex,
            *BoneName.ToString(),
            RotationSizeSquared);
        return false;
    }

    FQuat NormalizedRotation = BoneTransform.Rotation;
    NormalizedRotation.Normalize();
    OutTransform = FTransform(NormalizedRotation, BoneTransform.Translation, BoneTransform.Scale);
    return true;
}

/**
 * 将 Skeleton 参考姿势的 UE 局部变换转换为动画导入内部单位。旋转与缩放保持原值，厘米位移转换为米，
 * 使缺少源动画轨道的骨骼可以参与同一套组件空间 FK，并在写入动画轨道时精确还原参考局部姿势。
 * 本函数是无状态纯转换，可在任意线程调用。
 *
 * @param ReferenceLocalPose Skeleton 中以厘米为单位的 UE 局部参考变换，只读且不保留引用。
 * @return 使用米制位移的等价 UE 局部变换。
 */
static FTransform MakeAnimationUnitReferencePose(const FTransform& ReferenceLocalPose)
{
    FTransform AnimationUnitPose = ReferenceLocalPose;
    AnimationUnitPose.SetTranslation(ReferenceLocalPose.GetTranslation() / SekiroToUEScale);
    return AnimationUnitPose;
}

/**
 * 在提交数据模型前验证每根骨骼的三类关键帧数量、有限性和旋转归一化状态。
 * 失败时精确报告动画、帧与骨骼，避免无效变换进入 AnimationDataController。
 *
 * @param ClipName 动画名，仅用于错误定位。
 * @param RefSkel 目标骨架，用于将轨道索引转换为骨骼名。
 * @param NumFrames 期望的关键帧数量。
 * @param AllPosKeys 全部位置关键帧。
 * @param AllRotKeys 全部旋转关键帧。
 * @param AllScaleKeys 全部缩放关键帧。
 * @return 所有轨道均可安全写入时返回 true。
 * @note 只读检查，无 UObject 修改，可在任意线程调用。
 */
static bool ValidateGeneratedAnimationKeys(
    const FString& ClipName,
    const FReferenceSkeleton& RefSkel,
    int32 NumFrames,
    const TArray<TArray<FVector>>& AllPosKeys,
    const TArray<TArray<FQuat>>& AllRotKeys,
    const TArray<TArray<FVector>>& AllScaleKeys)
{
    const int32 BoneCount = RefSkel.GetNum();
    if (AllPosKeys.Num() != BoneCount || AllRotKeys.Num() != BoneCount || AllScaleKeys.Num() != BoneCount)
    {
        UE_LOG(LogTemp, Error, TEXT("SAAnimationImporter: generated track count mismatch for clip '%s'"), *ClipName);
        return false;
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FName BoneName = RefSkel.GetBoneName(BoneIndex);
        if (AllPosKeys[BoneIndex].Num() != NumFrames
            || AllRotKeys[BoneIndex].Num() != NumFrames
            || AllScaleKeys[BoneIndex].Num() != NumFrames)
        {
            UE_LOG(LogTemp, Error,
                TEXT("SAAnimationImporter: key count mismatch in clip '%s', bone '%s': pos=%d rot=%d scale=%d expected=%d"),
                *ClipName,
                *BoneName.ToString(),
                AllPosKeys[BoneIndex].Num(),
                AllRotKeys[BoneIndex].Num(),
                AllScaleKeys[BoneIndex].Num(),
                NumFrames);
            return false;
        }

        for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
        {
            const FVector& Position = AllPosKeys[BoneIndex][FrameIndex];
            const FQuat& Rotation = AllRotKeys[BoneIndex][FrameIndex];
            const FVector& Scale = AllScaleKeys[BoneIndex][FrameIndex];
            if (Position.ContainsNaN() || Rotation.ContainsNaN() || Scale.ContainsNaN() || !Rotation.IsNormalized())
            {
                UE_LOG(LogTemp, Error,
                    TEXT("SAAnimationImporter: invalid generated key in clip '%s', frame %d, bone '%s': P=(%.9g,%.9g,%.9g) R=(%.9g,%.9g,%.9g,%.9g) S=(%.9g,%.9g,%.9g)"),
                    *ClipName,
                    FrameIndex,
                    *BoneName.ToString(),
                    Position.X,
                    Position.Y,
                    Position.Z,
                    Rotation.X,
                    Rotation.Y,
                    Rotation.Z,
                    Rotation.W,
                    Scale.X,
                    Scale.Y,
                    Scale.Z);
                return false;
            }
        }
    }

    return true;
}

/**
 * 验证 AnimationDataController 写入后的原始数据模型与提交的关键帧逐帧一致。
 * 该检查直接覆盖第 53 帧等单帧异常，并在第一次不一致时报告精确骨骼和帧号。
 *
 * @param DataModel 写入后的动画数据模型。
 * @param BoneName 当前骨骼轨道名。
 * @param PosKeys 期望的位置关键帧。
 * @param RotKeys 期望的旋转关键帧。
 * @param ScaleKeys 期望的缩放关键帧。
 * @return 模型中的全部关键帧与输入一致时返回 true。
 * @note 只能在动画模型可读期间调用，不修改 UObject。
 */
static bool ValidateWrittenBoneTrack(
    const IAnimationDataModel& DataModel,
    const FName& BoneName,
    const TArray<FVector>& PosKeys,
    const TArray<FQuat>& RotKeys,
    const TArray<FVector>& ScaleKeys)
{
    constexpr double RoundTripTolerance = 1.0e-3;
    for (int32 FrameIndex = 0; FrameIndex < PosKeys.Num(); ++FrameIndex)
    {
        const FTransform ExpectedTransform(RotKeys[FrameIndex], PosKeys[FrameIndex], ScaleKeys[FrameIndex]);
        const FTransform WrittenTransform = DataModel.GetBoneTrackTransform(BoneName, FFrameNumber(FrameIndex));
        if (!WrittenTransform.Equals(ExpectedTransform, RoundTripTolerance))
        {
            const FQuat& ExpectedRotation = ExpectedTransform.GetRotation();
            const FQuat& WrittenRotation = WrittenTransform.GetRotation();
            UE_LOG(LogTemp, Error,
                TEXT("SAAnimationImporter: data model round-trip mismatch at frame %d, bone '%s': expected R=(%.9g,%.9g,%.9g,%.9g), written R=(%.9g,%.9g,%.9g,%.9g)"),
                FrameIndex,
                *BoneName.ToString(),
                ExpectedRotation.X,
                ExpectedRotation.Y,
                ExpectedRotation.Z,
                ExpectedRotation.W,
                WrittenRotation.X,
                WrittenRotation.Y,
                WrittenRotation.Z,
                WrittenRotation.W);
            return false;
        }
    }

    return true;
}

UAnimSequence* SAAnimationImporter::Build(const FSAAnimClip& Clip, USkeleton* Skeleton,
                                           USkeletalMesh* PreviewMesh, const FString& PackagePath)
{
    if (Clip.FrameCount < 2 || Clip.FrameData.Num() < 2)
    {
        UE_LOG(LogTemp, Error, TEXT("Clip '%s' has insufficient frames (%d)"), *Clip.Name, Clip.FrameCount);
        return nullptr;
    }
    if (!Skeleton)
    {
        UE_LOG(LogTemp, Error, TEXT("Skeleton is null"));
        return nullptr;
    }
    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    const int32 SkeletonBoneCount = RefSkel.GetNum();
    const int32 AnimBoneCount = Clip.BoneNames.Num();
    if (AnimBoneCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Clip '%s' has no bone data"), *Clip.Name);
        return nullptr;
    }
    if (AnimBoneCount > SkeletonBoneCount)
    {
        UE_LOG(LogTemp, Error, TEXT("Anim bone count (%d) > skeleton bone count (%d)"),
            AnimBoneCount, SkeletonBoneCount);
        return nullptr;
    }

    UPackage* Package = LoadOrCreateAnimationPackage(PackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load or create package: %s"), *PackagePath);
        return nullptr;
    }

    FString AssetName = FString::Printf(TEXT("Anim_%s"), *Clip.Name);
    UObject* ExistingAsset = FindObject<UObject>(Package, *AssetName);
    UAnimSequence* AnimSeq = Cast<UAnimSequence>(ExistingAsset);
    const bool bIsReimport = AnimSeq != nullptr;
    if (ExistingAsset && !AnimSeq)
    {
        UE_LOG(LogTemp, Error, TEXT("Existing object is not a UAnimSequence and will not be replaced: %s.%s"),
            *PackagePath, *AssetName);
        return nullptr;
    }

    if (!AnimSeq)
    {
        AnimSeq = NewObject<UAnimSequence>(Package, UAnimSequence::StaticClass(),
            FName(*AssetName), RF_Public | RF_Standalone);
    }
    if (!AnimSeq)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UAnimSequence: %s"), *AssetName);
        return nullptr;
    }

    if (bIsReimport)
    {
        if (AnimSeq->GetSkeleton() != Skeleton)
        {
            UE_LOG(LogTemp, Error, TEXT("Existing animation uses a different skeleton and will not be modified: %s"),
                *AnimSeq->GetPathName());
            return nullptr;
        }
    }
    else
    {
        AnimSeq->SetSkeleton(Skeleton);
        AnimSeq->bEnableRootMotion = Clip.bHasRootMotion;
        AnimSeq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
        AnimSeq->bForceRootLock = false;
        if (PreviewMesh) AnimSeq->SetPreviewMesh(PreviewMesh);
    }

    // Debug: sample first frame, first anim bone transform
    if (Clip.FrameData.Num() > 0 && Clip.FrameData[0].Num() > 0)
    {
        const auto& bt0 = Clip.FrameData[0][0];
        UE_LOG(LogTemp, Display, TEXT("Anim Bone0 raw: P=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f,%.3f)"),
            bt0.Translation.X, bt0.Translation.Y, bt0.Translation.Z,
            bt0.Rotation.X, bt0.Rotation.Y, bt0.Rotation.Z, bt0.Rotation.W);
    }

    TArray<FAnimNotifyEvent> PreservedNotifies;
    TArray<FAnimSyncMarker> PreservedSyncMarkers;
    TArray<FName> PreservedUniqueMarkerNames;
#if WITH_EDITORONLY_DATA
    TArray<FAnimNotifyTrack> PreservedNotifyTracks;
#endif
    int32 PreservedFloatCurveCount = 0;
    int32 PreservedTransformCurveCount = 0;
    int32 PreservedAttributeCount = 0;
    if (bIsReimport)
    {
        const IAnimationDataModel* OriginalDataModel = AnimSeq->GetDataModel();
        if (!OriginalDataModel)
        {
            UE_LOG(LogTemp, Error, TEXT("Existing animation has no data model and will not be modified: %s"),
                *AnimSeq->GetPathName());
            return nullptr;
        }

        // 加载可能已启动读取旧骨轨的异步压缩；取消其结果并等待任务退出后，才允许 Controller 原地改写模型。
        AnimSeq->WaitOnExistingCompression(false);

        PreservedNotifies = AnimSeq->Notifies;
        PreservedSyncMarkers = AnimSeq->AuthoredSyncMarkers;
        PreservedUniqueMarkerNames = AnimSeq->UniqueMarkerNames;
#if WITH_EDITORONLY_DATA
        PreservedNotifyTracks = AnimSeq->AnimNotifyTracks;
#endif
        PreservedFloatCurveCount = OriginalDataModel->GetNumberOfFloatCurves();
        PreservedTransformCurveCount = OriginalDataModel->GetNumberOfTransformCurves();
        PreservedAttributeCount = OriginalDataModel->GetAttributes().Num();

        UE_LOG(LogTemp, Display,
            TEXT("SAAnimationImporter: reimporting in place %s (preserving %d notifies, %d sync markers, %d curves, %d attributes)"),
            *AnimSeq->GetPathName(),
            PreservedNotifies.Num(),
            PreservedSyncMarkers.Num(),
            PreservedFloatCurveCount + PreservedTransformCurveCount,
            PreservedAttributeCount);
    }

    const int32 NumFrames = Clip.FrameData.Num();
    const FFrameRate FrameRate(FMath::RoundToInt(Clip.SampleRate), 1);
    if (Clip.bHasRootMotion && Clip.RootMotionFrames.Num() != NumFrames)
    {
        UE_LOG(LogTemp, Warning, TEXT("Clip '%s' RootMotionFrames mismatch: %d root frames vs %d pose frames"),
            *Clip.Name, Clip.RootMotionFrames.Num(), NumFrames);
    }

    // Build bone name → anim bone index lookup (name-based matching, not index)
    TMap<FName, int32> BoneNameToAnimIdx;
    for (int32 i = 0; i < AnimBoneCount; ++i)
    {
        BoneNameToAnimIdx.Add(Clip.BoneNames[i], i);
    }

    const FName IndependentRootBoneName(TEXT("Root"));
    const int32 IndependentRootBoneIdx = RefSkel.FindBoneIndex(IndependentRootBoneName);
    if (IndependentRootBoneIdx == INDEX_NONE || RefSkel.GetParentIndex(IndependentRootBoneIdx) != INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("Clip '%s' requires an independent top-level Root bone; reimport the model skeleton first"),
            *Clip.Name);
        return nullptr;
    }

    const FTransform& IndependentRootReferencePose = RefSkel.GetRefBonePose()[IndependentRootBoneIdx];
    if (!IndependentRootReferencePose.Equals(FTransform::Identity))
    {
        UE_LOG(LogTemp, Error, TEXT("Clip '%s' requires the independent Root reference pose to be Identity"), *Clip.Name);
        return nullptr;
    }

    // Handle ExportRoot/Armature virtual FK (old SekiroImport approach)
    const int32* ERAnimIdx = BoneNameToAnimIdx.Find(FName(TEXT("ExportRoot")));
    const int32* ArAnimIdx = BoneNameToAnimIdx.Find(FName(TEXT("Armature")));
    // Debug: verify bone name matching. A synthesized Root track is expected to be absent from source data.
    int32 MatchedBones = 0;
    for (int32 i = 0; i < SkeletonBoneCount; ++i)
    {
        if (BoneNameToAnimIdx.Contains(RefSkel.GetBoneName(i)))
            MatchedBones++;
    }
    const bool bRootTrackIsSynthetic = !BoneNameToAnimIdx.Contains(IndependentRootBoneName);
    UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Bone match: %d/%d skeleton bones found in anim data%s"),
        MatchedBones, SkeletonBoneCount, bRootTrackIsSynthetic ? TEXT(" (Root is synthesized)") : TEXT(""));
    const bool bNeedVirtualArmature = (ERAnimIdx != nullptr && ArAnimIdx != nullptr);

    FQuat OrientQ = GetOrientQ();
    OrientQ.Normalize();

    // Per-bone key arrays
    TArray<TArray<FVector>> AllPosKeys;
    TArray<TArray<FQuat>> AllRotKeys;
    TArray<TArray<FVector>> AllScaleKeys;
    AllPosKeys.SetNum(SkeletonBoneCount);
    AllRotKeys.SetNum(SkeletonBoneCount);
    AllScaleKeys.SetNum(SkeletonBoneCount);
    for (int32 i = 0; i < SkeletonBoneCount; ++i)
    {
        AllPosKeys[i].Reserve(NumFrames);
        AllRotKeys[i].Reserve(NumFrames);
        AllScaleKeys[i].Reserve(NumFrames);
    }

    TArray<FTransform> WorldUE;
    WorldUE.SetNum(SkeletonBoneCount);

    TArray<FTransform> LocalUE;
    LocalUE.SetNum(SkeletonBoneCount);

    TArray<FTransform> ReferenceLocalPosesInAnimationUnits;
    ReferenceLocalPosesInAnimationUnits.Reserve(SkeletonBoneCount);
    const TArray<FTransform>& ReferenceLocalPoses = RefSkel.GetRefBonePose();
    for (int32 BoneIndex = 0; BoneIndex < SkeletonBoneCount; ++BoneIndex)
        ReferenceLocalPosesInAnimationUnits.Add(MakeAnimationUnitReferencePose(ReferenceLocalPoses[BoneIndex]));

    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        const TArray<FSAAnimBoneTransform>& FrameBones = Clip.FrameData[Frame];

        // Virtual Armature WorldUE (FK: ExportRoot -> Armature -> OrientQ)
        FTransform ArWorldUE = FTransform::Identity;
        if (bNeedVirtualArmature && *ERAnimIdx < FrameBones.Num() && *ArAnimIdx < FrameBones.Num())
        {
            FTransform ERHKX;
            if (!TryBuildHKXTransform(
                FrameBones[*ERAnimIdx], Clip.Name, Frame, Clip.BoneNames[*ERAnimIdx], ERHKX))
            {
                return nullptr;
            }
            FTransform ArHKX;
            if (!TryBuildHKXTransform(
                FrameBones[*ArAnimIdx], Clip.Name, Frame, Clip.BoneNames[*ArAnimIdx], ArHKX))
            {
                return nullptr;
            }
            FTransform ArWorldHKX = ArHKX * ERHKX;
            ArWorldHKX.NormalizeRotation();
            ArWorldUE.SetRotation(OrientQ * ArWorldHKX.GetRotation());
            ArWorldUE.NormalizeRotation();
            ArWorldUE.SetTranslation(OrientQ.RotateVector(ArWorldHKX.GetTranslation()));
            ArWorldUE.SetScale3D(ArWorldHKX.GetScale3D());
        }

        // Pass 1+2: FK + OrientQ -> WorldUE (name-based lookup)
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            if (BoneIdx == IndependentRootBoneIdx)
            {
                WorldUE[BoneIdx] = FTransform::Identity;
                continue;
            }

            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            const int32* AnimBoneIdxPtr = BoneNameToAnimIdx.Find(BoneName);

            const bool bHasAnimationTransform = AnimBoneIdxPtr && *AnimBoneIdxPtr < FrameBones.Num();
            if (!bHasAnimationTransform)
            {
                LocalUE[BoneIdx] = ReferenceLocalPosesInAnimationUnits[BoneIdx];
                WorldUE[BoneIdx] = ParentIdx >= 0 && ParentIdx < BoneIdx
                    ? LocalUE[BoneIdx] * WorldUE[ParentIdx]
                    : LocalUE[BoneIdx];
                continue;
            }

            FTransform LocalHKX;
            if (!TryBuildHKXTransform(FrameBones[*AnimBoneIdxPtr], Clip.Name, Frame, BoneName, LocalHKX))
            {
                return nullptr;
            }

            FTransform WorldHKX;
            if (ParentIdx == IndependentRootBoneIdx)
            {
                // Root 不参与姿势 FK；原始 Master 仍直接连接旧的虚拟 Armature。
                if (bNeedVirtualArmature)
                {
                    const FQuat OrientQInv = OrientQ.Inverse();
                    FTransform ArWorldHKX;
                    ArWorldHKX.SetTranslation(OrientQInv.RotateVector(ArWorldUE.GetTranslation()));
                    ArWorldHKX.SetRotation(OrientQInv * ArWorldUE.GetRotation());
                    ArWorldHKX.NormalizeRotation();
                    ArWorldHKX.SetScale3D(ArWorldUE.GetScale3D());
                    WorldHKX = LocalHKX * ArWorldHKX;
                    WorldHKX.NormalizeRotation();
                }
                else
                {
                    WorldHKX = LocalHKX;
                }
            }
            else if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ParentWorldHKX;
                ParentWorldHKX.SetTranslation(OrientQInv.RotateVector(WorldUE[ParentIdx].GetTranslation()));
                ParentWorldHKX.SetRotation(OrientQInv * WorldUE[ParentIdx].GetRotation());
                ParentWorldHKX.NormalizeRotation();
                ParentWorldHKX.SetScale3D(WorldUE[ParentIdx].GetScale3D());
                WorldHKX = LocalHKX * ParentWorldHKX;
                WorldHKX.NormalizeRotation();
            }
            else if (bNeedVirtualArmature)
            {
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ArWorldHKX;
                ArWorldHKX.SetTranslation(OrientQInv.RotateVector(ArWorldUE.GetTranslation()));
                ArWorldHKX.SetRotation(OrientQInv * ArWorldUE.GetRotation());
                ArWorldHKX.NormalizeRotation();
                ArWorldHKX.SetScale3D(ArWorldUE.GetScale3D());
                WorldHKX = LocalHKX * ArWorldHKX;
                WorldHKX.NormalizeRotation();
            }
            else
            {
                WorldHKX = LocalHKX;
            }

            WorldUE[BoneIdx].SetTranslation(OrientQ.RotateVector(WorldHKX.GetTranslation()));
            WorldUE[BoneIdx].SetRotation(OrientQ * WorldHKX.GetRotation());
            WorldUE[BoneIdx].NormalizeRotation();
            WorldUE[BoneIdx].SetScale3D(WorldHKX.GetScale3D());
        }

        // Pass 3: Derive LocalUE
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            if (BoneIdx == IndependentRootBoneIdx)
            {
                LocalUE[BoneIdx] = FTransform::Identity;
                continue;
            }

            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32* AnimBoneIdxPtr = BoneNameToAnimIdx.Find(BoneName);
            if (!AnimBoneIdxPtr || *AnimBoneIdxPtr >= FrameBones.Num())
            {
                LocalUE[BoneIdx] = ReferenceLocalPosesInAnimationUnits[BoneIdx];
                continue;
            }

            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                LocalUE[BoneIdx] = WorldUE[BoneIdx].GetRelativeTransform(WorldUE[ParentIdx]);
                LocalUE[BoneIdx].NormalizeRotation();
            }
            else if (bNeedVirtualArmature)
            {
                LocalUE[BoneIdx] = WorldUE[BoneIdx].GetRelativeTransform(ArWorldUE);
                LocalUE[BoneIdx].NormalizeRotation();
            }
            else
            {
                LocalUE[BoneIdx] = WorldUE[BoneIdx];
            }
        }

        if (Clip.RootMotionFrames.IsValidIndex(Frame))
        {
            LocalUE[IndependentRootBoneIdx] = BuildRootMotionTransformUE(Clip.RootMotionFrames[Frame], OrientQ);
        }

        // Write keys
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            AllPosKeys[BoneIdx].Add(LocalUE[BoneIdx].GetTranslation() * SekiroToUEScale);
            AllRotKeys[BoneIdx].Add(LocalUE[BoneIdx].GetRotation());
            AllScaleKeys[BoneIdx].Add(LocalUE[BoneIdx].GetScale3D());
        }
    }

    if (!ValidateGeneratedAnimationKeys(
        Clip.Name,
        RefSkel,
        NumFrames,
        AllPosKeys,
        AllRotKeys,
        AllScaleKeys))
    {
        return nullptr;
    }

    // Write bone curves via IAnimationDataController
    IAnimationDataController& Controller = AnimSeq->GetController();
    Controller.OpenBracket(NSLOCTEXT("SekiroAssetManager", "ImportAnim", "Import Sekiro Animation"), false);
    if (!bIsReimport)
    {
        Controller.InitializeModel();
    }
    Controller.RemoveAllBoneTracks(false);
    Controller.SetFrameRate(FrameRate, false);
    Controller.SetNumberOfFrames(FFrameNumber(NumFrames - 1), false);

    const IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
    ensureAlwaysMsgf(
        DataModel &&
        DataModel->GetFrameRate() == FrameRate &&
        DataModel->GetNumberOfFrames() == NumFrames - 1,
        TEXT("SAAnimationImporter: animation model timing mismatch for '%s' (expected %d/%d fps, %d frames; actual %d/%d fps, %d frames)"),
        *Clip.Name,
        FrameRate.Numerator,
        FrameRate.Denominator,
        NumFrames - 1,
        DataModel ? DataModel->GetFrameRate().Numerator : 0,
        DataModel ? DataModel->GetFrameRate().Denominator : 0,
        DataModel ? DataModel->GetNumberOfFrames() : INDEX_NONE);

    bool bAllTracksWrittenAndVerified = true;
    for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
    {
        const FName BoneName = RefSkel.GetBoneName(BoneIdx);
        if (!Controller.AddBoneCurve(BoneName, false)
            || !Controller.SetBoneTrackKeys(
                BoneName,
                AllPosKeys[BoneIdx],
                AllRotKeys[BoneIdx],
                AllScaleKeys[BoneIdx],
                false)
            || !DataModel
            || !ValidateWrittenBoneTrack(
                *DataModel,
                BoneName,
                AllPosKeys[BoneIdx],
                AllRotKeys[BoneIdx],
                AllScaleKeys[BoneIdx]))
        {
            UE_LOG(LogTemp, Error,
                TEXT("SAAnimationImporter: failed to write or verify track '%s' in clip '%s'"),
                *BoneName.ToString(),
                *Clip.Name);
            bAllTracksWrittenAndVerified = false;
            break;
        }
    }

    if (!bAllTracksWrittenAndVerified)
    {
        Controller.CloseBracket(false);
        return nullptr;
    }

        UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Wrote %d bone tracks"), SkeletonBoneCount);
    Controller.NotifyPopulated();
        // Verify first bone data was written
    UE_LOG(LogTemp, Display, TEXT("Bone[0] keys after write: %d pos, %d rot, %d scale"),
        AllPosKeys[0].Num(), AllRotKeys[0].Num(), AllScaleKeys[0].Num());
    if (AllPosKeys[0].Num() > 0)
        UE_LOG(LogTemp, Display, TEXT("  Pos[0]=(%.1f,%.1f,%.1f) Pos[last]=(%.1f,%.1f,%.1f)"),
            AllPosKeys[0][0].X, AllPosKeys[0][0].Y, AllPosKeys[0][0].Z,
            AllPosKeys[0].Last().X, AllPosKeys[0].Last().Y, AllPosKeys[0].Last().Z);
    // Bracket 关闭时 UAnimSequence::OnModelModified 会更新 RawDataGuid、清除旧压缩数据与平台缓存，
    // 并为新骨骼轨道启动当前平台 DDC 派生，避免原地重导继续使用旧姿势数据。
    Controller.CloseBracket(false);

    if (bIsReimport)
    {
        // SetNumberOfFrames 会自动移动或裁剪事件时间；重导只替换骨骼数据，因此恢复原始事件数组。
        AnimSeq->Notifies = PreservedNotifies;
        AnimSeq->AuthoredSyncMarkers = PreservedSyncMarkers;
        AnimSeq->UniqueMarkerNames = PreservedUniqueMarkerNames;
#if WITH_EDITORONLY_DATA
        AnimSeq->AnimNotifyTracks = PreservedNotifyTracks;
        for (FAnimNotifyTrack& NotifyTrack : AnimSeq->AnimNotifyTracks)
        {
            NotifyTrack.Notifies.Reset();
            NotifyTrack.SyncMarkers.Reset();
        }
        for (FAnimNotifyEvent& Notify : AnimSeq->Notifies)
        {
            if (ensureAlwaysMsgf(
                AnimSeq->AnimNotifyTracks.IsValidIndex(Notify.TrackIndex),
                TEXT("SAAnimationImporter: preserved notify '%s' has invalid track %d in %s"),
                *Notify.NotifyName.ToString(),
                Notify.TrackIndex,
                *AnimSeq->GetPathName()))
            {
                AnimSeq->AnimNotifyTracks[Notify.TrackIndex].Notifies.Add(&Notify);
            }
        }
        for (FAnimSyncMarker& SyncMarker : AnimSeq->AuthoredSyncMarkers)
        {
            if (ensureAlwaysMsgf(
                AnimSeq->AnimNotifyTracks.IsValidIndex(SyncMarker.TrackIndex),
                TEXT("SAAnimationImporter: preserved sync marker '%s' has invalid track %d in %s"),
                *SyncMarker.MarkerName.ToString(),
                SyncMarker.TrackIndex,
                *AnimSeq->GetPathName()))
            {
                AnimSeq->AnimNotifyTracks[SyncMarker.TrackIndex].SyncMarkers.Add(&SyncMarker);
            }
        }
#endif

        const IAnimationDataModel* ReimportedDataModel = AnimSeq->GetDataModel();
        ensureAlwaysMsgf(
            ReimportedDataModel &&
            AnimSeq->Notifies.Num() == PreservedNotifies.Num() &&
            AnimSeq->AuthoredSyncMarkers.Num() == PreservedSyncMarkers.Num() &&
            ReimportedDataModel->GetNumberOfFloatCurves() == PreservedFloatCurveCount &&
            ReimportedDataModel->GetNumberOfTransformCurves() == PreservedTransformCurveCount &&
            ReimportedDataModel->GetAttributes().Num() == PreservedAttributeCount,
            TEXT("SAAnimationImporter: preservation validation failed after in-place reimport: %s"),
            *AnimSeq->GetPathName());
    }

    AnimSeq->MarkPackageDirty();
    

    // Save package
    FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GWarn;
    if (UPackage::SavePackage(Package, AnimSeq, *PackageFileName, SaveArgs))
    {
        UE_LOG(LogTemp, Display, TEXT("Animation saved: %s (%d frames)"), *Clip.Name, NumFrames);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Animation save failed: %s"), *Clip.Name);
        return nullptr;
    }

    if (!bIsReimport)
    {
        FAssetRegistryModule::AssetCreated(AnimSeq);
    }

    UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Built %s (%d frames, %.2fs)"),
        *Clip.Name, NumFrames, Clip.Duration);
    return AnimSeq;
}

TArray<UAnimSequence*> SAAnimationImporter::BuildBatch(const FSAAnimData& AnimData, USkeleton* Skeleton,
                                                        USkeletalMesh* PreviewMesh, const FString& BasePath,
                                                        const FString& AssetName,
                                                        const TArray<FString>& AnimNames)
{
    TArray<UAnimSequence*> Results;
    Results.Reserve(AnimData.Clips.Num());

    TSet<FString, DefaultKeyFuncs<FString>, TInlineSetAllocator<64>> FilterSet;
    if (AnimNames.Num() > 0)
    {
        for (const FString& N : AnimNames)
            FilterSet.Add(N.ToLower());
        UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Filtering %d anims"), FilterSet.Num());
    }

    for (int32 i = 0; i < AnimData.Clips.Num(); ++i)
    {
        const FSAAnimClip& Clip = AnimData.Clips[i];
        if (FilterSet.Num() > 0 && !FilterSet.Contains(Clip.Name.ToLower()))
            continue;

        FString AnimPackagePath;
        FSAAnimClip NormalizedClip = Clip;  // mutable copy for name fix
        if (!AssetName.IsEmpty())
        {
            // Strip existing asset prefix to avoid double prefix like Anim_Sekiro_Sekiro_xxx
            FString Prefix = AssetName + TEXT("_");
            FString CleanName = NormalizedClip.Name.StartsWith(Prefix) ? NormalizedClip.Name.RightChop(Prefix.Len()) : NormalizedClip.Name;
            NormalizedClip.Name = FString::Printf(TEXT("%s_%s"), *AssetName, *CleanName);
            AnimPackagePath = FString::Printf(TEXT("%s/Anim_%s"), *BasePath, *NormalizedClip.Name);
        }
        else
        {
            AnimPackagePath = FString::Printf(TEXT("%s/Anim_%s"), *BasePath, *NormalizedClip.Name);
        }
        UAnimSequence* AnimSeq = Build(NormalizedClip, Skeleton, PreviewMesh, AnimPackagePath);
        if (AnimSeq) Results.Add(AnimSeq);
    }

    UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Batch done %d/%d"), Results.Num(), AnimData.Clips.Num());
    return Results;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSAAnimationMissingTrackReferencePoseTest,
    "Sekiro.AssetManager.Animation.MissingTrackUsesReferencePose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 验证缺失动画轨道时参考局部姿势只执行厘米与米的单位往返，不经过 HKX OrientQ，
 * 从而保持专用参考骨骼的局部旋转、缩放和最终写入位移。
 * 测试仅执行纯变换计算，不创建或保存动画资产。
 *
 * @param Parameters Automation Framework 参数，本测试不使用。
 * @return 始终返回 true 以完成全部断言收集。
 */
bool FSAAnimationMissingTrackReferencePoseTest::RunTest(const FString& Parameters)
{
    const FQuat ReferenceRotation(FVector::UpVector, FMath::DegreesToRadians(37.0));
    const FVector ReferenceTranslation(12.0, -34.0, 56.0);
    const FVector ReferenceScale(1.0, 1.5, 0.75);
    const FTransform ReferencePose(ReferenceRotation, ReferenceTranslation, ReferenceScale);

    const FTransform AnimationUnitPose = MakeAnimationUnitReferencePose(ReferencePose);
    const FVector WrittenTranslation = AnimationUnitPose.GetTranslation() * SekiroToUEScale;

    TestTrue(TEXT("reference translation round-trips through animation meters"),
        WrittenTranslation.Equals(ReferenceTranslation));
    TestTrue(TEXT("reference rotation is not reoriented"),
        AnimationUnitPose.GetRotation().Equals(ReferenceRotation));
    TestTrue(TEXT("reference scale is unchanged"),
        AnimationUnitPose.GetScale3D().Equals(ReferenceScale));
    return true;
}

#endif
