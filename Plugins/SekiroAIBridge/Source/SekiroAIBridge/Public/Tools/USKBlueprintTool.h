#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKBlueprintTool.generated.h"

class FJsonObject;
class UBlueprint;

/**
 * Blueprint 操作工具
 *
 * 支持创建、修改、查询Blueprint资产。
 * 操作包括：
 * - 创建Blueprint（指定父类和资产路径）
 * - 添加变量
 * - 添加函数
 * - 添加组件
 * - 设置CDO默认值
 * - 查询Blueprint结构
 * - 编译
 *
 * 风险：高（可修改Blueprint逻辑）。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKBlueprintTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("blueprint")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override;
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

private:
    FString HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddVariable(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddFunction(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddComponent(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleSetProperty(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCompile(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddInterface(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddNode(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleLayout(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleSetupMaterial(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAssignMaterialSlot(const TSharedPtr<FJsonObject>& Args, FString& OutError);

    UBlueprint* LoadBlueprint(const FString& AssetPath, FString& OutError);
    FString BlueprintToJson(UBlueprint* BP) const;

    // 按类名查找 UClass，支持裸类名（带/不带 A/U/I/F/E/T/S 前缀）和全路径
    UClass* ResolveClassByName(const FString& InClassName);
};
