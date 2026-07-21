#include "Tools/USKConsoleTool.h"
#include "SekiroAIBridgeLog.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    constexpr int32 RequiredSettledTickCount = 2;
    bool bSafeEditorExitScheduled = false;

    /**
     * 判断命令是否必须走编辑器安全退出流程。
     *
     * @param Command 待执行的完整控制台命令。
     * @return 首个命令词是否为 QUIT_EDITOR、EXIT 或 QUIT（忽略大小写）。
     * @note 纯字符串判断，无线程限制。
     */
    bool IsEditorExitCommand(const FString& Command)
    {
        const TCHAR* CommandCursor = *Command;
        if (FParse::Command(&CommandCursor, TEXT("QUIT_EDITOR")))
        {
            return true;
        }

        CommandCursor = *Command;
        if (FParse::Command(&CommandCursor, TEXT("EXIT")))
        {
            return true;
        }

        CommandCursor = *Command;
        return FParse::Command(&CommandCursor, TEXT("QUIT"));
    }

    /**
     * 安排编辑器安全退出：先请求关闭全部资产编辑器，等待资产编辑器注册表连续两个后续帧为空，
     * 再向引擎延迟命令队列提交 QUIT_EDITOR。这样动画编辑器等 Toolkit 会在插件 RPC 调用栈返回后完成析构。
     *
     * @param OutError 无法获得编辑器或资产编辑器子系统时返回错误原因。
     * @return 是否成功安排退出；重复请求视为成功。
     * @note 只能在游戏线程调用；Ticker 回调同样在游戏线程运行。
     */
    bool ScheduleSafeEditorExit(FString& OutError)
    {
        check(IsInGameThread());

        if (bSafeEditorExitScheduled)
        {
            return true;
        }

        if (!GEditor)
        {
            OutError = TEXT("GEditor 不可用，无法安全关闭资产编辑器");
            return false;
        }

        UAssetEditorSubsystem* AssetEditorSubsystem =
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!AssetEditorSubsystem)
        {
            OutError = TEXT("AssetEditorSubsystem 不可用，无法安全退出编辑器");
            return false;
        }

        const bool bAllEditorsAcceptedClose = AssetEditorSubsystem->CloseAllAssetEditors();
        bSafeEditorExitScheduled = true;

        TSharedRef<int32> SettledTickCount = MakeShared<int32>(0);
        FTSTicker::GetCoreTicker().AddTicker(
            TEXT("SekiroAIBridge.SafeEditorExit"),
            0.0f,
            [SettledTickCount](float)
            {
                check(IsInGameThread());

                if (GEditor)
                {
                    UAssetEditorSubsystem* CurrentAssetEditorSubsystem =
                        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
                    if (CurrentAssetEditorSubsystem
                        && CurrentAssetEditorSubsystem->GetAllEditedAssets().Num() > 0)
                    {
                        CurrentAssetEditorSubsystem->CloseAllAssetEditors();
                        *SettledTickCount = 0;
                        return true;
                    }
                }

                ++(*SettledTickCount);
                if (*SettledTickCount < RequiredSettledTickCount)
                {
                    return true;
                }

                if (GEngine)
                {
                    GEngine->DeferredCommands.AddUnique(TEXT("QUIT_EDITOR"));
                }
                else
                {
                    FPlatformMisc::RequestExit(false);
                }

                return false;
            });

        UE_LOG(
            LogSekiroAIBridge,
            Log,
            TEXT("已安排安全退出编辑器，资产编辑器关闭请求结果: %s"),
            bAllEditorsAcceptedClose ? TEXT("全部接受") : TEXT("等待后续帧完成"));
        return true;
    }
}

FString USKConsoleTool::GetToolDescription() const
{
    return TEXT("执行UE控制台命令并返回输出。例如：stat fps、obj list、memreport 等。");
}

FString USKConsoleTool::GetInputSchemaJson() const
{
    return TEXT("{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Console command to execute\"}},\"required\":[\"command\"]}");
}

FString USKConsoleTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Command = ArgsObj->GetStringField(TEXT("command"));
        return FString::Printf(TEXT("执行控制台命令: %s"), *Command);
    }
    return TEXT("执行控制台命令");
}

FString USKConsoleTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Command;
    if (!ArgsObj->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
    {
        OutError = TEXT("缺少 command 参数");
        return FString();
    }

    FString Output;
    if (IsEditorExitCommand(Command))
    {
        if (!ScheduleSafeEditorExit(OutError))
        {
            return FString();
        }

        Output = TEXT("已关闭资产编辑器，并安排在后续帧安全退出编辑器");
    }
    else if (!GEngine)
    {
        OutError = TEXT("GEngine 不可用");
        return FString();
    }
    else
    {
        // 捕获控制台输出
        FStringOutputDevice OutputDevice;
        GEngine->Exec(nullptr, *Command, OutputDevice);
        Output = FString(OutputDevice);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("command"), Command);
    ResultObj->SetStringField(TEXT("output"), Output.IsEmpty() ? TEXT("(无输出)") : Output);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString ResultJson;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResultJson);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return ResultJson;
}
