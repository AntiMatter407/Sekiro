#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "USKAIToolRunnerCommandlet.generated.h"

/**
 * 通用 AIBridge 工具调用入口（Commandlet）
 *
 * 接受 -ToolName 和 -ToolArgs JSON 参数，调用对应的 AIBridge 工具。
 * C++ 层只提供通用 CLI 接口，具体工作流由脚本层编排。
 *
 * 用法:
 *   UnrealEditor-Cmd.exe Project.uproject -run=AIToolRunner \
 *     -ToolName=anim_blueprint -ToolArgs={"action":"create","path":"/Game/...",...}
 */
UCLASS()
class USKAIToolRunnerCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    USKAIToolRunnerCommandlet();

    virtual int32 Main(const FString& Params) override;
};
