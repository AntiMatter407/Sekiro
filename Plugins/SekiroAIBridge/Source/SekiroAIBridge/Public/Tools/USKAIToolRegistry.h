#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKAIToolRegistry.generated.h"

/**
 * 工具注册表
 *
 * 管理所有已注册的AI工具实例。
 * 提供工具列表查询（MCP tools/list格式）和按名查找执行。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKAIToolRegistry : public UObject
{
    GENERATED_BODY()

public:
    /** 注册一个工具 */
    void RegisterTool(TScriptInterface<ISKAIToolInterface> Tool);

    /** 注销一个工具 */
    void UnregisterTool(const FName& ToolName);

    /** 列出所有已注册工具的名称 */
    TArray<FName> ListToolNames() const;

    /** 按名称查找工具，返回nullptr表示未找到 */
    TScriptInterface<ISKAIToolInterface> FindTool(const FName& ToolName) const;

    /** 获取所有工具（用于序列化tools/list响应） */
    const TArray<TScriptInterface<ISKAIToolInterface>>& GetAllTools() const { return Tools; }

private:
    UPROPERTY()
    TArray<TScriptInterface<ISKAIToolInterface>> Tools;
};
