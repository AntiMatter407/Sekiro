#include "USKAIToolRunnerCommandlet.h"
#include "Tools/USKAnimBlueprintTool.h"
#include "SekiroAIBridgeLog.h"
#include "Misc/FileHelper.h"

USKAIToolRunnerCommandlet::USKAIToolRunnerCommandlet()
{
    LogToConsole = true;
}

int32 USKAIToolRunnerCommandlet::Main(const FString& Params)
{
    FString ToolName;
    FString ToolArgs;

    // 解析 -ToolName=xxx -ToolArgs=xxx 或 -ToolArgsFile=xxx
    TArray<FString> Tokens;
    TArray<FString> Switches;
    TMap<FString, FString> SwitchValues;
    ParseCommandLine(*Params, Tokens, Switches, SwitchValues);

    if (!SwitchValues.Contains(TEXT("ToolName")) || (!SwitchValues.Contains(TEXT("ToolArgs")) && !SwitchValues.Contains(TEXT("ToolArgsFile"))))
    {
        UE_LOG(LogSekiroAIBridge, Error, TEXT("缺少参数。用法: -run=SKAIToolRunner -ToolName=anim_blueprint -ToolArgs={\"action\":\"create\",...} 或 -ToolArgsFile=Args.json"));
        return 1;
    }

    ToolName = SwitchValues[TEXT("ToolName")];
    if (SwitchValues.Contains(TEXT("ToolArgsFile")))
    {
        const FString ToolArgsFile = SwitchValues[TEXT("ToolArgsFile")];
        if (!FFileHelper::LoadFileToString(ToolArgs, *ToolArgsFile))
        {
            UE_LOG(LogSekiroAIBridge, Error, TEXT("无法读取 ToolArgsFile: %s"), *ToolArgsFile);
            return 1;
        }
    }
    else
    {
        ToolArgs = SwitchValues[TEXT("ToolArgs")];
    }

    UE_LOG(LogSekiroAIBridge, Display, TEXT("==== AIToolRunner: %s ===="), *ToolName);

    // 实例化工具（绕过注册表，直接在 Commandlet 中使用）
    USKAnimBlueprintTool* Tool = NewObject<USKAnimBlueprintTool>();
    if (!Tool)
    {
        UE_LOG(LogSekiroAIBridge, Error, TEXT("无法创建工具: %s"), *ToolName);
        return 1;
    }

    FString OutError;
    FString Result = Tool->Execute(ToolArgs, OutError);

    if (!OutError.IsEmpty())
    {
        UE_LOG(LogSekiroAIBridge, Error, TEXT("工具执行错误: %s"), *OutError);
        return 1;
    }

    // 输出 JSON 结果到 stdout（供脚本层解析）
    UE_LOG(LogSekiroAIBridge, Display, TEXT("==== RESULT: %s ===="), *Result);

    return 0;
}
