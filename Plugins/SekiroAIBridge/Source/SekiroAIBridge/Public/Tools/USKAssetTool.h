#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKAssetTool.generated.h"

class FJsonObject;

/**
 * 资产操作工具
 *
 * 支持资产的CRUD操作：创建、删除、复制、重命名、移动、查询。
 * 高风险操作（删除）需要确认。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKAssetTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("asset")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override;
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

private:
    bool IsDangerousAction(const FString& Action) const;

    FString HandleList(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleExists(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCreatePhysicsAsset(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleDelete(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleDuplicate(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleRename(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleSave(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleImportFile(const TSharedPtr<FJsonObject>& Args, FString& OutError);
};
