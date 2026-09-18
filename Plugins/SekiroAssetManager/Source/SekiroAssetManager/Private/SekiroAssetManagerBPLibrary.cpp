#include "SekiroAssetManagerBPLibrary.h"
#include "SAModelImporter.h"
#include "SATAEImporter.h"
#include "SATAELogicBuilder.h"
#include "SekiroAnimLogicData.h"
#include "Animation/AnimSequence.h"
#include "Animation/IAnimationSequenceCompiler.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "UObject/SavePackage.h"

namespace
{
/**
 * 加载并去重完整动画对象路径，只接受已经完成加载且允许压缩的 UAnimSequence。
 * 本函数不修改动画；成功时 OutAnimSequences 保持输入首次出现顺序，失败原因写入 OutError。
 * 只能由游戏线程的压缩驻留入口调用。
 */
bool LoadUniqueAnimationSequences(
    const TArray<FString>& AnimationObjectPaths,
    TArray<UAnimSequence*>& OutAnimSequences,
    FString& OutError)
{
    if (AnimationObjectPaths.IsEmpty())
    {
        OutError = TEXT("动画对象路径列表为空。");
        return false;
    }

    TSet<FString> UniqueObjectPaths;
    for (const FString& AnimationObjectPath : AnimationObjectPaths)
    {
        if (AnimationObjectPath.IsEmpty())
        {
            OutError = TEXT("动画对象路径不得为空。");
            return false;
        }
        bool bIsAlreadyInSet = false;
        UniqueObjectPaths.Add(AnimationObjectPath, &bIsAlreadyInSet);
        if (!bIsAlreadyInSet)
        {
            UAnimSequence* AnimSequence = LoadObject<UAnimSequence>(nullptr, *AnimationObjectPath);
            if (!AnimSequence)
            {
                OutError = FString::Printf(TEXT("无法加载动画序列：%s"), *AnimationObjectPath);
                return false;
            }
            if (!AnimSequence->CanBeCompressed() || AnimSequence->HasAnyFlags(RF_NeedPostLoad))
            {
                OutError = FString::Printf(
                    TEXT("动画序列尚未完成加载，不能生成当前平台压缩数据：%s"),
                    *AnimationObjectPath);
                return false;
            }
            OutAnimSequences.Add(AnimSequence);
        }
    }
    return true;
}

/**
 * 校验游戏线程与调用方标识，并解析当前运行平台和本轮驻留引用哈希。
 * ResidencyOwner 必须由调用方在获取与释放之间保持一致；输出仅在成功时有效。
 * 本函数只读取模块与线程状态，不请求或释放压缩数据。
 */
bool ResolveCompressionResidencyContext(
    const FString& ResidencyOwner,
    const ITargetPlatform*& OutTargetPlatform,
    uint32& OutReferencerHash,
    FString& OutError)
{
    if (!IsInGameThread())
    {
        OutError = TEXT("动画压缩数据驻留只能在游戏线程请求或释放。");
        return false;
    }
    if (ResidencyOwner.IsEmpty())
    {
        OutError = TEXT("ResidencyOwner 不得为空。");
        return false;
    }

    ITargetPlatformManagerModule* TargetPlatformManager = GetTargetPlatformManager();
    OutTargetPlatform = TargetPlatformManager
        ? TargetPlatformManager->GetRunningTargetPlatform()
        : nullptr;
    if (!OutTargetPlatform)
    {
        OutError = TEXT("无法取得当前运行目标平台。");
        return false;
    }

    OutReferencerHash = GetTypeHash(ResidencyOwner);
    return true;
}
} // namespace

bool USekiroAssetManagerBPLibrary::ImportTAELogic(const FString& JsonPath, FSAAnimLogicImportResult& OutResult)
{
    if (!FPaths::FileExists(JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[TAE Import] JSON not found: %s"), *JsonPath);
        return false;
    }

    OutResult = FSATAEImporter::ImportFromFile(JsonPath);
    if (OutResult.TotalAnims == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[TAE Import] No animations imported"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[TAE Import] Success: %d anims, %d events, %d categories"),
        OutResult.TotalAnims, OutResult.TotalEvents,
        OutResult.MainStateMachine.AnimIDsByCategory.Num());
    return true;
}

USKAnimationLogicData* USekiroAssetManagerBPLibrary::BuildAnimLogicDataAsset(
    const FSAAnimLogicImportResult& ImportResult,
    const FString& PackagePath,
    const FString& AssetName)
{
    if (ImportResult.TotalAnims == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Empty import result"));
        return nullptr;
    }

    // 创建包
    FString FullPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Failed to create package: %s"), *FullPath);
        return nullptr;
    }
    Package->SetFlags(RF_Public | RF_Standalone);

    USKAnimationLogicData* DataAsset = FSATAELogicBuilder::BuildDataAsset(ImportResult, Package);
    if (!DataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] BuildDataAsset failed"));
        return nullptr;
    }

    // 重命名和保存
    DataAsset->Rename(*AssetName, Package);
    FString FilePath = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GLog;
    if (!UPackage::SavePackage(Package, DataAsset, *FilePath, SaveArgs))
    {
        UE_LOG(LogTemp, Error, TEXT("[BuildAnimLogicData] Save failed: %s"), *FilePath);
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("[BuildAnimLogicData] Saved: %s"), *FullPath);
    return DataAsset;
}

FString USekiroAssetManagerBPLibrary::ImportSkeletalMesh(
    const FString& JsonPath, const FString& TargetPackagePath,
    bool& bOutSuccess, FString& OutErrorMessage)
{
    bOutSuccess = false;
    OutErrorMessage.Empty();

    USkeletalMesh* Mesh = nullptr;
    USkeleton* Skeleton = nullptr;

    if (!SAModelImporter::Import(JsonPath, TargetPackagePath, TArray<FString>(), Mesh, Skeleton))
    {
        OutErrorMessage = TEXT("SAModelImporter::Import failed");
        return FString();
    }

    bOutSuccess = true;
    return Mesh ? Mesh->GetPathName() : FString();
}
int32 USekiroAssetManagerBPLibrary::ImportAnimations(const FString& JsonPath, const FString& TargetBasePath,
    const FString& AssetName, const FString& SkeletonPath)
{
    if (!FPaths::FileExists(JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[ImportAnimations] JSON not found: %s"), *JsonPath);
        return 0;
    }

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        UE_LOG(LogTemp, Error, TEXT("[ImportAnimations] Skeleton not found: %s"), *SkeletonPath);
        return 0;
    }

    FSAAnimData AnimData;
    if (!SAAnimationImporter::ParseFromFile(JsonPath, AnimData))
    {
        UE_LOG(LogTemp, Error, TEXT("[ImportAnimations] ParseFromFile failed"));
        return 0;
    }

    TArray<UAnimSequence*> Results = SAAnimationImporter::BuildBatch(
        AnimData, Skeleton, nullptr, TargetBasePath, AssetName);

    UE_LOG(LogTemp, Display, TEXT("[ImportAnimations] Imported %d/%d animations"), Results.Num(), AnimData.Clips.Num());
    return Results.Num();
}

bool USekiroAssetManagerBPLibrary::AddIntegerCurveToAnimation(const FString& AnimPath,
    const FString& CurveName, const TArray<float>& KeyTimes, const TArray<float>& KeyValues)
{
    UE_LOG(LogTemp, Log, TEXT("[AddIntegerCurve] 已转移到 AIBridge"));
    return false;
}

/**
 * 为指定动画请求当前运行平台的压缩数据驻留，并同步等待相关动画压缩任务完成。
 * AnimationObjectPaths 是待采样动画的完整 UObject 路径；ResidencyOwner 是本轮调用的唯一所有者标识；
 * bSuccess 和 OutError 返回完整执行结果。本函数只能在编辑器游戏线程调用，成功后必须使用相同参数释放。
 */
void USekiroAssetManagerBPLibrary::AcquireAnimationCompressionResidencyDetailed(
    const TArray<FString>& AnimationObjectPaths,
    const FString& ResidencyOwner,
    bool& bSuccess,
    FString& OutError)
{
    bSuccess = false;
    OutError.Empty();

    const ITargetPlatform* TargetPlatform = nullptr;
    uint32 ReferencerHash = 0;
    if (!ResolveCompressionResidencyContext(
        ResidencyOwner,
        TargetPlatform,
        ReferencerHash,
        OutError))
    {
        return;
    }

    TArray<UAnimSequence*> AnimSequences;
    if (!LoadUniqueAnimationSequences(AnimationObjectPaths, AnimSequences, OutError))
    {
        return;
    }

    for (UAnimSequence* AnimSequence : AnimSequences)
    {
        AnimSequence->RequestResidency(TargetPlatform, ReferencerHash);
    }
    UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(MakeArrayView(AnimSequences));

    for (UAnimSequence* AnimSequence : AnimSequences)
    {
        if (!AnimSequence->HasCompressedDataForPlatform(TargetPlatform))
        {
            OutError = FString::Printf(
                TEXT("动画序列缺少当前平台压缩数据：%s"),
                *AnimSequence->GetPathName());
            for (UAnimSequence* RequestedSequence : AnimSequences)
            {
                if (RequestedSequence->HasResidency(ReferencerHash))
                {
                    RequestedSequence->ReleaseResidency(TargetPlatform, ReferencerHash);
                }
            }
            return;
        }
    }

    bSuccess = true;
}

/**
 * 释放 AcquireAnimationCompressionResidencyDetailed 取得的当前平台压缩数据驻留。
 * AnimationObjectPaths 与 ResidencyOwner 必须和获取时一致；bSuccess 和 OutError 返回释放结果。
 * 本函数只能在编辑器游戏线程调用，并允许对已释放的路径执行幂等清理。
 */
void USekiroAssetManagerBPLibrary::ReleaseAnimationCompressionResidencyDetailed(
    const TArray<FString>& AnimationObjectPaths,
    const FString& ResidencyOwner,
    bool& bSuccess,
    FString& OutError)
{
    bSuccess = false;
    OutError.Empty();

    const ITargetPlatform* TargetPlatform = nullptr;
    uint32 ReferencerHash = 0;
    if (!ResolveCompressionResidencyContext(
        ResidencyOwner,
        TargetPlatform,
        ReferencerHash,
        OutError))
    {
        return;
    }

    TArray<UAnimSequence*> AnimSequences;
    if (!LoadUniqueAnimationSequences(AnimationObjectPaths, AnimSequences, OutError))
    {
        return;
    }

    for (UAnimSequence* AnimSequence : AnimSequences)
    {
        if (AnimSequence->HasResidency(ReferencerHash))
        {
            AnimSequence->ReleaseResidency(TargetPlatform, ReferencerHash);
        }
    }
    bSuccess = true;
}
