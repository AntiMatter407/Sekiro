#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKEditorStateTool.generated.h"

/**
 * 编辑器状态查询工具
 *
 * 只读查询，无副作用，用于验证端到端通信通路。
 * 支持查询：
 * - 当前打开的关卡
 * - 选中的Actor
 * - 项目名称和版本
 * - 编辑器运行时间
 */
UCLASS()
class SEKIROAIBRIDGE_API USKEditorStateTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    // ---- ISKAIToolInterface ----
    virtual FName GetToolName() const override { return FName(TEXT("editor.query")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;

private:
    FString QueryOpenLevel() const;
    FString QuerySelectedActors() const;
    FString QueryProjectInfo() const;
    FString QueryEditorTime() const;
};
