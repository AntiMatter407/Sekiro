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

static UPackage* CreatePackageForOverwrite(const FString& PackagePath)
{
    FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (FPaths::FileExists(FilePath))
    {
        IFileManager::Get().Delete(*FilePath);
    }
    UPackage* StalePackage = FindPackage(nullptr, *PackagePath);
    if (StalePackage)
    {
        TArray<UObject*> ObjectsInPackage;
        GetObjectsWithOuter(StalePackage, ObjectsInPackage, false);
        for (UObject* Obj : ObjectsInPackage)
        {
            Obj->ClearFlags(RF_Standalone | RF_Public);
            Obj->Rename(nullptr, GetTransientPackage(),
                REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
        }
        ResetLoaders(StalePackage);
        StalePackage->ClearFlags(RF_WasLoaded);
        StalePackage->ClearFlags(RF_Standalone | RF_Public);
        StalePackage->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(),
            GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
    }
    UPackage* Package = CreatePackage(*PackagePath);
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

static FTransform ToHKXTransform(const FSAAnimBoneTransform& B)
{
    return FTransform(B.Rotation, B.Translation, B.Scale);
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

    UPackage* Package = CreatePackageForOverwrite(PackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *PackagePath);
        return nullptr;
    }

    FString AssetName = FString::Printf(TEXT("Anim_%s"), *Clip.Name);
    UAnimSequence* AnimSeq = NewObject<UAnimSequence>(Package, UAnimSequence::StaticClass(),
        FName(*AssetName), RF_Public | RF_Standalone);
    if (!AnimSeq)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UAnimSequence: %s"), *AssetName);
        return nullptr;
    }

    AnimSeq->SetSkeleton(Skeleton);
    AnimSeq->bEnableRootMotion = false;
    AnimSeq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
    AnimSeq->bForceRootLock = false;

    // Debug: sample first frame, first anim bone transform
    if (Clip.FrameData.Num() > 0 && Clip.FrameData[0].Num() > 0)
    {
        const auto& bt0 = Clip.FrameData[0][0];
        UE_LOG(LogTemp, Display, TEXT("Anim Bone0 raw: P=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f,%.3f)"),
            bt0.Translation.X, bt0.Translation.Y, bt0.Translation.Z,
            bt0.Rotation.X, bt0.Rotation.Y, bt0.Rotation.Z, bt0.Rotation.W);
    }

    if (PreviewMesh) AnimSeq->SetPreviewMesh(PreviewMesh);

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

    // Handle ExportRoot/Armature virtual FK (old SekiroImport approach)
    const int32* ERAnimIdx = BoneNameToAnimIdx.Find(FName(TEXT("ExportRoot")));
    const int32* ArAnimIdx = BoneNameToAnimIdx.Find(FName(TEXT("Armature")));
        // Debug: verify bone name matching
    int32 MatchedBones = 0;
    for (int32 i = 0; i < SkeletonBoneCount; ++i)
    {
        if (BoneNameToAnimIdx.Contains(RefSkel.GetBoneName(i)))
            MatchedBones++;
    }
    UE_LOG(LogTemp, Display, TEXT("SAAnimationImporter: Bone match: %d/%d skeleton bones found in anim data"),
        MatchedBones, SkeletonBoneCount);
const bool bNeedVirtualArmature = (ERAnimIdx != nullptr && ArAnimIdx != nullptr);

    const FQuat OrientQ = GetOrientQ();

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

    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        const TArray<FSAAnimBoneTransform>& FrameBones = Clip.FrameData[Frame];

        // Virtual Armature WorldUE (FK: ExportRoot -> Armature -> OrientQ)
        FTransform ArWorldUE = FTransform::Identity;
        if (bNeedVirtualArmature && *ERAnimIdx < FrameBones.Num() && *ArAnimIdx < FrameBones.Num())
        {
            FTransform ERHKX = ToHKXTransform(FrameBones[*ERAnimIdx]);
            FTransform ArHKX = ToHKXTransform(FrameBones[*ArAnimIdx]);
            FTransform ArWorldHKX = ArHKX * ERHKX;
            ArWorldUE.SetRotation(OrientQ * ArWorldHKX.GetRotation());
            ArWorldUE.SetTranslation(OrientQ.RotateVector(ArWorldHKX.GetTranslation()));
            ArWorldUE.SetScale3D(ArWorldHKX.GetScale3D());
        }

        // Pass 1+2: FK + OrientQ -> WorldUE (name-based lookup)
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const FName BoneName = RefSkel.GetBoneName(BoneIdx);
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            const int32* AnimBoneIdxPtr = BoneNameToAnimIdx.Find(BoneName);

            FTransform LocalHKX = FTransform::Identity;
            if (AnimBoneIdxPtr && *AnimBoneIdxPtr < FrameBones.Num())
                LocalHKX = ToHKXTransform(FrameBones[*AnimBoneIdxPtr]);

            FTransform WorldHKX;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ParentWorldHKX;
                ParentWorldHKX.SetTranslation(OrientQInv.RotateVector(WorldUE[ParentIdx].GetTranslation()));
                ParentWorldHKX.SetRotation(OrientQInv * WorldUE[ParentIdx].GetRotation());
                ParentWorldHKX.SetScale3D(WorldUE[ParentIdx].GetScale3D());
                WorldHKX = LocalHKX * ParentWorldHKX;
            }
            else if (bNeedVirtualArmature)
            {
                const FQuat OrientQInv = OrientQ.Inverse();
                FTransform ArWorldHKX;
                ArWorldHKX.SetTranslation(OrientQInv.RotateVector(ArWorldUE.GetTranslation()));
                ArWorldHKX.SetRotation(OrientQInv * ArWorldUE.GetRotation());
                ArWorldHKX.SetScale3D(ArWorldUE.GetScale3D());
                WorldHKX = LocalHKX * ArWorldHKX;
            }
            else
            {
                WorldHKX = LocalHKX;
            }

            WorldUE[BoneIdx].SetTranslation(OrientQ.RotateVector(WorldHKX.GetTranslation()));
            WorldUE[BoneIdx].SetRotation(OrientQ * WorldHKX.GetRotation());
            WorldUE[BoneIdx].SetScale3D(WorldHKX.GetScale3D());
        }

        // Pass 3: Derive LocalUE -> write keys
        for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
        {
            const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
            FTransform LocalUE;
            if (ParentIdx >= 0 && ParentIdx < SkeletonBoneCount)
            {
                LocalUE = WorldUE[BoneIdx].GetRelativeTransform(WorldUE[ParentIdx]);
            }
            else if (bNeedVirtualArmature)
            {
                LocalUE = WorldUE[BoneIdx].GetRelativeTransform(ArWorldUE);
            }
            else
            {
                LocalUE = WorldUE[BoneIdx];
            }

            if (ParentIdx < 0 && Clip.RootMotionFrames.IsValidIndex(Frame))
            {
                const FTransform RootMotionUE = BuildRootMotionTransformUE(Clip.RootMotionFrames[Frame], OrientQ);
                LocalUE = LocalUE * RootMotionUE;
            }

            AllPosKeys[BoneIdx].Add(LocalUE.GetTranslation() * SekiroToUEScale);
            AllRotKeys[BoneIdx].Add(LocalUE.GetRotation());
            AllScaleKeys[BoneIdx].Add(LocalUE.GetScale3D());
        }
    }

    // Write bone curves via IAnimationDataController
    IAnimationDataController& Controller = AnimSeq->GetController();
    Controller.OpenBracket(NSLOCTEXT("SekiroAssetManager", "ImportAnim", "Import Sekiro Animation"));
    Controller.InitializeModel();
    Controller.RemoveAllBoneTracks();
Controller.SetNumberOfFrames(FFrameNumber(NumFrames - 1));
    Controller.SetFrameRate(FrameRate);

    for (int32 BoneIdx = 0; BoneIdx < SkeletonBoneCount; ++BoneIdx)
    {
        const FName BoneName = RefSkel.GetBoneName(BoneIdx);
        Controller.AddBoneCurve(BoneName);
        Controller.SetBoneTrackKeys(BoneName, AllPosKeys[BoneIdx], AllRotKeys[BoneIdx], AllScaleKeys[BoneIdx]);
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
    Controller.CloseBracket();

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

    FAssetRegistryModule::AssetCreated(AnimSeq);

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
