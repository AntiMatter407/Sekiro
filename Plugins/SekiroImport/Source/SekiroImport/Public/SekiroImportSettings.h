#pragma once

#include "CoreMinimal.h"
#include "SekiroImportSettings.generated.h"

/// 覆盖模式
UENUM()
enum class ESekiroOverwriteMode : uint8
{
    Overwrite  UMETA(DisplayName="覆盖已有资产"),
    Skip       UMETA(DisplayName="跳过已有资产"),
};

/// Sekiro导入配置（持久化到 DefaultSekiroImportSettings.ini）
UCLASS(config=Editor, defaultconfig)
class SEKIROIMPORT_API USekiroImportSettings : public UObject
{
    GENERATED_BODY()

public:
    /// 模型JSON文件路径
    UPROPERTY(config, EditAnywhere, Category="Source")
    FString ModelJsonPath;

    /// 动画JSON文件路径
    UPROPERTY(config, EditAnywhere, Category="Source")
    FString AnimationJsonPath;

    /// 输出内容根路径，如 /Game/Characters/Sekiro
    UPROPERTY(config, EditAnywhere, Category="Target")
    FString OutputBasePath = TEXT("/Game/Characters/Sekiro");

    /// 骨架资产名称
    UPROPERTY(config, EditAnywhere, Category="Target")
    FString SkeletonName = TEXT("Sekiro_Skeleton");

    /// 是否导入骨架
    UPROPERTY(config, EditAnywhere, Category="Options")
    bool bImportSkeleton = true;

    /// 是否导入骨骼网格体
    UPROPERTY(config, EditAnywhere, Category="Options")
    bool bImportSkeletalMesh = true;

    /// 是否导入材质
    UPROPERTY(config, EditAnywhere, Category="Options")
    bool bImportMaterials = true;

    /// 是否导入动画
    UPROPERTY(config, EditAnywhere, Category="Options")
    bool bImportAnimations = true;

    /// 最大导入动画数（0=全部）
    UPROPERTY(config, EditAnywhere, Category="Options", meta=(ClampMin="0"))
    int32 MaxAnimations = 0;

    /// 动画名称前缀过滤（仅导入以此为前缀的动画，空=全部）
    UPROPERTY(config, EditAnywhere, Category="Options|Animation")
    FString AnimationPrefixFilter;

    /// 已有资产处理方式
    UPROPERTY(config, EditAnywhere, Category="Options")
    ESekiroOverwriteMode OverwriteMode = ESekiroOverwriteMode::Overwrite;
};
