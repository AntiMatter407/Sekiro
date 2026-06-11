#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKEnhancedInputTool.generated.h"

class FJsonObject;

/**
 * Enhanced Input 操作工具
 *
 * 支持创建/配置 UInputAction、UInputMappingContext 资产，
 * 以及按键映射的添加/移除。
 *
 * 风险：中（创建资产无风险，map_key/unmap_key 修改映射配置）。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKEnhancedInputTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("enhanced_input")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override;
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

private:
    FString HandleCreateInputAction(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCreateMappingContext(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleMapKey(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleUnmapKey(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleConfigureTriggers(const TSharedPtr<FJsonObject>& Args, FString& OutError);

    UClass* FindTriggerClass(const FString& TypeName) const;
    UClass* FindModifierClass(const FString& TypeName) const;
    FString InputActionToJson(class UInputAction* IA) const;
    FString MappingContextToJson(class UInputMappingContext* IMC) const;

    FString CurrentAction; // 保存当前 action 用于 ConfirmSummary
};
