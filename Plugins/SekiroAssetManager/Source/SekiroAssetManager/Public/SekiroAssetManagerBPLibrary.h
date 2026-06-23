#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SAAnimationImporter.h"
#include "SekiroAssetManagerBPLibrary.generated.h"

/// 蓝图函数库，提供 Sekiro 资产导入的 UFUNCTION 接口，可被 Python 调用
UCLASS()
class SEKIROASSETMANAGER_API USekiroAssetManagerBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    static FString ImportSkeletalMesh(const FString& JsonPath, const FString& TargetPackagePath, bool& bOutSuccess, FString& OutErrorMessage);
    /// 导入 TAE 逻辑数据（从 Sekiro_TAE_Logic.json 解析为 ABIR）
    /// @param JsonPath TAE JSON 文件绝对路径
    /// @param OutResult 输出的动画逻辑导入结果
    /// @return 是否成功
    UFUNCTION(BlueprintCallable, Category = "Sekiro|TAE")
    static bool ImportTAELogic(const FString& JsonPath, FSAAnimLogicImportResult& OutResult);

    /// 从 TAE 导入结果构建 AnimLogicData DataAsset
    /// @param ImportResult TAE 导入结果
    /// @param PackagePath 目标包路径（如 /Game/Characters/Sekiro）
    /// @param AssetName 资产名（如 "SK_AnimLogicData"）
    /// @return 构建的 DataAsset，失败返回 nullptr
    UFUNCTION(BlueprintCallable, Category = "Sekiro|TAE")
    static USKAnimationLogicData* BuildAnimLogicDataAsset(
        const FSAAnimLogicImportResult& ImportResult,
        const FString& PackagePath,
        const FString& AssetName);

    /// 从 Sekiro_model.json 导入骨架 + 骨骼网格体
    /// @param JsonPath JSON 文件绝对路径
    /// @param TargetPackagePath UE 目标包路径
    /// @param bOutSuccess 是否成功
    /// @param OutErrorMessage 错误信息
    /// @return 导入的 SkeletalMesh 路径（或空字符串）

    /// 给指定动画资产添加 Integer 曲线（用于 FrameFlags/CancelActions/AttackHitbox）
    /// @param AnimPath 动画资产路径
    /// @param CurveName 曲线名
    /// @param KeyTimes 关键帧时间数组（秒）
    /// @param KeyValues 关键帧值数组（float，实际存整数值）
    /// @return 是否成功
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation")
    static bool AddIntegerCurveToAnimation(const FString& AnimPath, const FString& CurveName,
        const TArray<float>& KeyTimes, const TArray<float>& KeyValues);

    /// 从 JSON 导入动画序列
    /// @param JsonPath JSON 文件绝对路径
    /// @param TargetBasePath UE 目标包基础路径（如 /Game/Characters/Sekiro）
    /// @param AssetName 资产名前缀（如 "Sekiro"）
    /// @param SkeletonPath 骨架资产路径（如 /Game/Characters/Sekiro/Sekiro_Skeleton）
    /// @return 成功导入的动画数量
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Asset")
    static int32 ImportAnimations(const FString& JsonPath, const FString& TargetBasePath,
                                   const FString& AssetName, const FString& SkeletonPath);
};
