#pragma once

#include "CoreMinimal.h"
#include "SekiroImportData.h"

class USkeleton;
class USkeletalMesh;
class UMaterial;
class UAnimSequence;
class USekiroImportSettings;

/// 导入流水线编排器：协调 Parse → Build → Save 全流程
class SEKIROIMPORT_API FSekiroImportPipeline
{
public:
    /// 导入结果
    struct FImportResult
    {
        USkeleton* Skeleton = nullptr;
        USkeletalMesh* SkeletalMesh = nullptr;
        TArray<UMaterial*> Materials;
        TArray<UAnimSequence*> Animations;
        TArray<FString> Errors;
        bool bSuccess = false;
    };

    /// 执行完整导入流程
    /// @param Settings 导入设置（文件路径、输出路径、选项）
    /// @return 导入结果
    static FImportResult Run(const USekiroImportSettings& Settings);

    /// 导入进度委托
    DECLARE_DELEGATE_OneParam(FOnProgress, const FString& /*StatusMessage*/);
    static FOnProgress OnProgress;
};
