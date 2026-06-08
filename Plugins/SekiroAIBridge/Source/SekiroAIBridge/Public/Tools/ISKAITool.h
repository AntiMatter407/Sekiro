#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISKAITool.generated.h"

/**
 * AI工具接口
 *
 * 所有AI可调用工具都必须实现此接口。
 * 每个工具对应一个能力领域（Blueprint操作、Python执行、编译等）。
 */
UINTERFACE(MinimalAPI)
class USKAIToolInterface : public UInterface
{
    GENERATED_BODY()
};

class SEKIROAIBRIDGE_API ISKAIToolInterface
{
    GENERATED_BODY()

public:
    /** 工具唯一名称，如 "console.execute"、"python.execute" */
    virtual FName GetToolName() const = 0;

    /** 工具描述（用于MCP tools/list输出） */
    virtual FString GetToolDescription() const = 0;

    /** 工具参数JSON Schema（用于MCP tools/list输出） */
    virtual FString GetInputSchemaJson() const = 0;

    /**
     * 执行工具
     * @param ArgsJson 参数JSON字符串
     * @param OutError  输出错误信息（失败时）
     * @return 执行结果JSON字符串
     */
    virtual FString Execute(const FString& ArgsJson, FString& OutError) = 0;

    /** 是否需要用户确认（高风险操作） */
    virtual bool RequiresConfirmation() const { return false; }

    /** 获取确认对话框的摘要文本（RequiresConfirmation为true时提供） */
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const { return FString(); }
};
