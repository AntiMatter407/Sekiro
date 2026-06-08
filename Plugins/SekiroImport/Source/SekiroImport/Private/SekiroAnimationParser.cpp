#include "SekiroAnimationParser.h"
#include "SekiroImportLog.h"
#include "SekiroStreamReader.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"

/// 流式解析阈值：文件超过此大小时使用流式读取
static constexpr int64 STREAM_THRESHOLD = 200LL * 1024 * 1024; // 200MB

// ============================================================================
// 坐标转换
// ============================================================================

FVector FSekiroAnimationParser::SekiroToUnrealPos(const FVector& V)
{
    return V * SekiroToUEScale; // m → cm, Y-up不变
}

FQuat FSekiroAnimationParser::SekiroToUnrealRot(const FQuat& Q)
{
    return Q; // Y-up四元数不变, 旋转由SkeletonBuilder施加
}

// ============================================================================
// LocalTransform解析
// ============================================================================

void FSekiroAnimationParser::ParseBoneLocalTransform(const TSharedPtr<FJsonObject>& TransformObj, FVector& OutPos, FQuat& OutRot, FVector& OutScale)
{
    // Position (P字段, float[3])
    const TArray<TSharedPtr<FJsonValue>>* PArr = nullptr;
    if (TransformObj->TryGetArrayField(TEXT("P"), PArr) && PArr->Num() >= 3)
    {
        OutPos = SekiroToUnrealPos(FVector(
            (float)(*PArr)[0]->AsNumber(),
            (float)(*PArr)[1]->AsNumber(),
            (float)(*PArr)[2]->AsNumber()
        ));
    }

    // Rotation (R字段, quaternion xyzw, float[4])
    const TArray<TSharedPtr<FJsonValue>>* RArr = nullptr;
    if (TransformObj->TryGetArrayField(TEXT("R"), RArr) && RArr->Num() >= 4)
    {
        OutRot = SekiroToUnrealRot(FQuat(
            (float)(*RArr)[0]->AsNumber(),
            (float)(*RArr)[1]->AsNumber(),
            (float)(*RArr)[2]->AsNumber(),
            (float)(*RArr)[3]->AsNumber()
        ));
    }

    // Scale (S字段, float[3])
    const TArray<TSharedPtr<FJsonValue>>* SArr = nullptr;
    if (TransformObj->TryGetArrayField(TEXT("S"), SArr) && SArr->Num() >= 3)
    {
        OutScale = FVector(
            (float)(*SArr)[0]->AsNumber(),
            (float)(*SArr)[1]->AsNumber(),
            (float)(*SArr)[2]->AsNumber()
        );
    }
}

// ============================================================================
// 从动画JSON构建骨骼数组
// ============================================================================

void FSekiroAnimationParser::BuildBonesFromAnimationData(
    const TArray<TSharedPtr<FJsonValue>>& BoneNames,
    const TArray<TSharedPtr<FJsonValue>>& BoneParents,
    const TArray<TSharedPtr<FJsonValue>>& BoneTransforms,
    TArray<FSekiroImportBone>& OutBones)
{
    const int32 BoneCount = BoneNames.Num();
    OutBones.Reserve(BoneCount);

    for (int32 i = 0; i < BoneCount; ++i)
    {
        FSekiroImportBone Bone;
        Bone.Name = FName(*BoneNames[i]->AsString());
        Bone.ParentIndex = (int32)BoneParents[i]->AsNumber();  // -1 = root

        // 解析LocalTransform（已是四元数，直接可用）
        const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
        if (i < BoneTransforms.Num() && BoneTransforms[i]->TryGetObject(TransformObjPtr))
        {
            ParseBoneLocalTransform(*TransformObjPtr, Bone.LocalTranslation, Bone.LocalRotation, Bone.LocalScale);
        }

        OutBones.Add(MoveTemp(Bone));
    }

    UE_LOG(LogSekiroImport, Log, TEXT("从动画JSON构建了 %d 根骨骼（含IK辅助骨骼）"), OutBones.Num());
}

// ============================================================================
// 单帧骨骼变换解析
// ============================================================================

void FSekiroAnimationParser::ParseFrameBoneTransforms(const TArray<TSharedPtr<FJsonValue>>& BoneTransformArray, int32 ExpectedBoneCount, TArray<FTransform>& OutTransforms)
{
    OutTransforms.Reserve(FMath::Min(BoneTransformArray.Num(), ExpectedBoneCount));

    for (int32 i = 0; i < BoneTransformArray.Num() && i < ExpectedBoneCount; ++i)
    {
        const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
        if (!BoneTransformArray[i]->TryGetObject(TransformObjPtr))
        {
            OutTransforms.Add(FTransform::Identity);
            continue;
        }

        FVector Pos = FVector::ZeroVector;
        FQuat Rot = FQuat::Identity;
        FVector Scale = FVector::OneVector;
        ParseBoneLocalTransform(*TransformObjPtr, Pos, Rot, Scale);

        OutTransforms.Add(FTransform(Rot, Pos, Scale));
    }

    // 补齐缺失的骨骼（使用Identity）
    while (OutTransforms.Num() < ExpectedBoneCount)
    {
        OutTransforms.Add(FTransform::Identity);
    }
}

// ============================================================================
// 单个动画片段解析
// ============================================================================

bool FSekiroAnimationParser::ParseAnimationClip(const TSharedPtr<FJsonObject>& AnimObj, int32 BoneCount, const TArray<FSekiroImportBone>& SkeletonBones, FSekiroAnimationClip& OutClip)
{
    OutClip.Name = AnimObj->GetStringField(TEXT("Name"));
    OutClip.Duration = (float)AnimObj->GetNumberField(TEXT("Duration"));
    OutClip.FrameCount = (int32)AnimObj->GetNumberField(TEXT("FrameCount"));
    OutClip.SampleRate = (float)AnimObj->GetNumberField(TEXT("SampleRate"));

    // BoneNames直接对应骨架骨骼名，无需过滤
    OutClip.BoneNames.Reserve(BoneCount);
    for (int32 b = 0; b < BoneCount; ++b)
    {
        OutClip.BoneNames.Add(FName());  // 将由调用者填充（或从ParseResult.Bones获取）
    }

    // 复制参考姿态Local变换到Clip（Y-up HKX空间, cm缩放）
    OutClip.ReferenceLocalTransforms.Reserve(FMath::Min(BoneCount, SkeletonBones.Num()));
    for (int32 b = 0; b < FMath::Min(BoneCount, SkeletonBones.Num()); ++b)
    {
        OutClip.ReferenceLocalTransforms.Add(FTransform(SkeletonBones[b].LocalRotation, SkeletonBones[b].LocalTranslation, SkeletonBones[b].LocalScale));
    }

    // 解析帧数据
    const TArray<TSharedPtr<FJsonValue>>* FramesArray = nullptr;
    if (!AnimObj->TryGetArrayField(TEXT("Frames"), FramesArray))
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("动画 '%s' 没有Frames数组"), *OutClip.Name);
        return false;
    }

    OutClip.FrameData.Reserve(FramesArray->Num());

    for (const TSharedPtr<FJsonValue>& FrameVal : *FramesArray)
    {
        const TSharedPtr<FJsonObject>* FrameObjPtr = nullptr;
        if (!FrameVal->TryGetObject(FrameObjPtr))
        {
            continue;
        }

        TArray<FTransform> FrameTransforms;
        const TArray<TSharedPtr<FJsonValue>>* BoneTransforms = nullptr;
        if ((*FrameObjPtr)->TryGetArrayField(TEXT("BoneTransforms"), BoneTransforms))
        {
            ParseFrameBoneTransforms(*BoneTransforms, BoneCount, FrameTransforms);
        }

        OutClip.FrameData.Add(MoveTemp(FrameTransforms));
    }

    UE_LOG(LogSekiroImport, Verbose, TEXT("  动画 '%s': %.2f秒, %d帧 @%.0fFPS"),
        *OutClip.Name, OutClip.Duration, OutClip.FrameCount, OutClip.SampleRate);

    return true;
}

// ============================================================================
// 主解析入口（骨架+动画）
// ============================================================================

bool FSekiroAnimationParser::ParseFromFile(const FString& FilePath, FParseResult& OutResult, int32 MaxAnimations)
{
    UE_LOG(LogSekiroImport, Log, TEXT("开始解析动画JSON: %s"), *FilePath);

    // 检查文件大小，决定使用全量加载还是流式解析
    int64 FileSize = IFileManager::Get().FileSize(*FilePath);
    float FileSizeMB = FileSize / (1024.0f * 1024.0f);

    if (FileSize >= STREAM_THRESHOLD)
    {
        UE_LOG(LogSekiroImport, Log, TEXT("文件较大(%.1f MB), 使用流式解析 (内存峰值~100MB)"), FileSizeMB);

        if (MaxAnimations < 0)
        {
            // 仅解析骨架，跳过动画
            return FSekiroStreamReader::ParseSkeleton(FilePath, OutResult.Bones);
        }

        // 流式解析：先提取骨架，再逐个处理动画
        FSekiroStreamReader::FOnAnimationParsed Callback;
        Callback.BindLambda([&OutResult](const FSekiroAnimationClip& Clip) -> bool {
            OutResult.Clips.Add(Clip);
            return true;
        });

        int32 AnimCount = FSekiroStreamReader::ParseAll(FilePath, OutResult.Bones, Callback, MaxAnimations);

        UE_LOG(LogSekiroImport, Log, TEXT("流式解析完成: %d骨骼, %d动画"), OutResult.Bones.Num(), AnimCount);
        return OutResult.Bones.Num() > 0;
    }

    // 小文件：全量加载（快速路径）
    UE_LOG(LogSekiroImport, Log, TEXT("文件较小(%.1f MB), 使用全量加载"), FileSizeMB);

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("无法读取动画JSON文件: %s"), *FilePath);
        return false;
    }

    // 解析JSON
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(JsonReader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画JSON解析失败"));
        return false;
    }

    // --- 骨架数据 ---
    int32 BoneCount = (int32)RootObject->GetNumberField(TEXT("BoneCount"));

    const TArray<TSharedPtr<FJsonValue>>* BoneNamesArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* BoneParentsArray = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* BoneTransformsArray = nullptr;

    RootObject->TryGetArrayField(TEXT("BoneNames"), BoneNamesArray);
    RootObject->TryGetArrayField(TEXT("BoneParents"), BoneParentsArray);
    RootObject->TryGetArrayField(TEXT("BoneLocalTransforms"), BoneTransformsArray);

    if (!BoneNamesArray || !BoneParentsArray || !BoneTransformsArray)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("动画JSON缺少骨架数据字段"));
        return false;
    }

    BuildBonesFromAnimationData(*BoneNamesArray, *BoneParentsArray, *BoneTransformsArray, OutResult.Bones);

    // --- 动画片段 ---
    int32 AnimationCount = (int32)RootObject->GetNumberField(TEXT("AnimationCount"));
    const TArray<TSharedPtr<FJsonValue>>* AnimationsArray = nullptr;

    if (!RootObject->TryGetArrayField(TEXT("Animations"), AnimationsArray))
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("动画JSON没有Animations数组"));
        return true;  // 骨架数据已成功解析
    }

    // MaxAnimations < 0: 跳过动画解析（仅骨架模式）
    // MaxAnimations = 0: 解析全部动画
    // MaxAnimations > 0: 限制数量
    int32 NumToParse;
    if (MaxAnimations < 0)
    {
        NumToParse = 0;
    }
    else if (MaxAnimations == 0)
    {
        NumToParse = AnimationsArray->Num();
    }
    else
    {
        NumToParse = FMath::Min(MaxAnimations, AnimationsArray->Num());
    }
    OutResult.Clips.Reserve(NumToParse);

    for (int32 i = 0; i < NumToParse; ++i)
    {
        const TSharedPtr<FJsonObject>* AnimObjPtr = nullptr;
        if (!(*AnimationsArray)[i]->TryGetObject(AnimObjPtr))
        {
            continue;
        }

        FSekiroAnimationClip Clip;
        if (ParseAnimationClip(*AnimObjPtr, BoneCount, OutResult.Bones, Clip))
        {
            // 填充BoneNames（从骨架数据复制）
            Clip.BoneNames.Reset(BoneCount);
            for (int32 b = 0; b < BoneCount; ++b)
            {
                Clip.BoneNames.Add(OutResult.Bones[b].Name);
            }

            OutResult.Clips.Add(MoveTemp(Clip));
        }
    }

    // 释放JSON字符串内存
    JsonString.Empty();

    UE_LOG(LogSekiroImport, Log, TEXT("动画JSON解析完成: %d 根骨骼, %d 个动画片段"),
        OutResult.Bones.Num(), OutResult.Clips.Num());

    return true;
}

// ============================================================================
// 仅解析骨架
// ============================================================================

bool FSekiroAnimationParser::ParseSkeletonOnly(const FString& FilePath, TArray<FSekiroImportBone>& OutBones)
{
    // 大文件直接用流式读取器提取骨架，避免加载整个文件
    int64 FileSize = IFileManager::Get().FileSize(*FilePath);
    if (FileSize >= STREAM_THRESHOLD)
    {
        UE_LOG(LogSekiroImport, Log, TEXT("大文件(%.1f MB), 使用流式提取骨架"), FileSize / (1024.0f * 1024.0f));
        return FSekiroStreamReader::ParseSkeleton(FilePath, OutBones);
    }

    // 小文件：全量加载后提取骨架
    FParseResult Result;
    if (ParseFromFile(FilePath, Result, -1))
    {
        OutBones = MoveTemp(Result.Bones);
        return true;
    }
    return false;
}
