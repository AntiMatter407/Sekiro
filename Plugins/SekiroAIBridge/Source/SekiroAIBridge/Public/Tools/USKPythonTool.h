#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKPythonTool.generated.h"

/**
 * Python 脚本执行工具
 *
 * 通过 IPythonScriptPlugin 执行Python代码或脚本文件。
 * 需要 PythonScriptPlugin 引擎插件已启用。
 * 风险：高（可执行任意Python代码，访问文件系统、网络等）。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKPythonTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("python.execute")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override { return true; }
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

    /** 检查PythonScriptPlugin是否可用 */
    static bool IsPythonAvailable();

private:
    FString ExecuteScript(const FString& Script, FString& OutError);
    FString ExecuteFile(const FString& FilePath, const FString& Args, FString& OutError);
};
