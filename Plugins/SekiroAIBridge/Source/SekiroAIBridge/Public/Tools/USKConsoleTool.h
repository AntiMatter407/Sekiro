#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKConsoleTool.generated.h"

/**
 * 控制台命令执行工具
 *
 * 通过 GEngine->Exec() 执行UE控制台命令并返回输出。
 * 风险：高（可执行任意控制台命令，包括文件操作等）。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKConsoleTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("console.execute")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override { return true; }
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;
};
