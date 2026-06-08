#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKCompileTool.generated.h"

/**
 * 编译触发工具
 *
 * 支持：
 * - 编译所有Blueprint
 * - 编译单个Blueprint
 * - 触发LiveCoding编译
 * - 保存所有已修改资产并编译
 */
UCLASS()
class SEKIROAIBRIDGE_API USKCompileTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("compile.run")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;

private:
    FString CompileAllBlueprints(FString& OutError);
    FString CompileSingleBlueprint(const FString& AssetPath, FString& OutError);
    FString CompileLiveCoding(FString& OutError);
    FString SaveAndCompile(const FString& AssetPath, FString& OutError);
};
