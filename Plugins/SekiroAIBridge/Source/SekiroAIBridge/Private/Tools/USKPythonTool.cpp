#include "Tools/USKPythonTool.h"
#include "IPythonScriptPlugin.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Modules/ModuleManager.h"

FString USKPythonTool::GetToolDescription() const
{
    return TEXT("执行Python脚本代码或Python文件。需要PythonScriptPlugin引擎插件已启用。");
}

FString USKPythonTool::GetInputSchemaJson() const
{
	return TEXT("{\"type\":\"object\",\"properties\":{\"script\":{\"type\":\"string\",\"description\":\"Python code to execute\"},\"file\":{\"type\":\"string\",\"description\":\"Python file path\"},\"args\":{\"type\":\"string\",\"description\":\"Optional arguments passed to the script via sys.argv\"}},\"anyOf\":[{\"required\":[\"script\"]},{\"required\":[\"file\"]}]}");
}

FString USKPythonTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Script;
        FString File;
        FString Args;
        ArgsObj->TryGetStringField(TEXT("script"), Script);
        ArgsObj->TryGetStringField(TEXT("file"), File);
        ArgsObj->TryGetStringField(TEXT("args"), Args);

        if (!Script.IsEmpty())
        {
            FString Preview = Script.Len() > 80 ? Script.Left(77) + TEXT("...") : Script;
            return FString::Printf(TEXT("执行Python代码: %s"), *Preview);
        }
        if (!File.IsEmpty())
        {
            if (!Args.IsEmpty())
            {
                FString ArgsPreview = Args.Len() > 60 ? Args.Left(57) + TEXT("...") : Args;
                return FString::Printf(TEXT("执行Python文件: %s (args: %s)"), *File, *ArgsPreview);
            }
            return FString::Printf(TEXT("执行Python文件: %s"), *File);
        }
    }
    return TEXT("执行Python脚本");
}

bool USKPythonTool::IsPythonAvailable()
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin)
    {
        return false;
    }
    // 尝试检查Python是否就绪
    return FModuleManager::Get().IsModuleLoaded("PythonScriptPlugin");
}

FString USKPythonTool::Execute(const FString& ArgsJson, FString& OutError)
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin)
    {
        OutError = TEXT("PythonScriptPlugin不可用。请确认引擎插件已启用。");
        return FString();
    }

    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Script;
    FString File;
    FString Args;
    ArgsObj->TryGetStringField(TEXT("script"), Script);
    ArgsObj->TryGetStringField(TEXT("file"), File);
    ArgsObj->TryGetStringField(TEXT("args"), Args);

    if (!Script.IsEmpty())
    {
        return ExecuteScript(Script, OutError);
    }
    else if (!File.IsEmpty())
    {
        return ExecuteFile(File, Args, OutError);
    }
    else
    {
        OutError = TEXT("需要提供 script 或 file 参数");
        return FString();
    }
}

FString USKPythonTool::ExecuteScript(const FString& Script, FString& OutError)
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin)
    {
        OutError = TEXT("PythonScriptPlugin不可用");
        return FString();
    }

    FPythonCommandEx CmdEx;
    CmdEx.Command = Script;
    CmdEx.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;

    bool bSuccess = PythonPlugin->ExecPythonCommandEx(CmdEx);
    if (!bSuccess)
    {
        OutError = CmdEx.CommandResult;
        return FString();
    }

    // 从 LogOutput 收集 print() 输出（CommandResult 在 ExecuteStatement 模式下始终为 None）
    TArray<FString> OutputLines;
    for (const FPythonLogOutputEntry& Entry : CmdEx.LogOutput)
    {
        if (!Entry.Output.IsEmpty())
        {
            OutputLines.Add(Entry.Output);
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("mode"), TEXT("script"));
    ResultObj->SetStringField(TEXT("command_result"), CmdEx.CommandResult);

    if (OutputLines.Num() > 0)
    {
        ResultObj->SetStringField(TEXT("output"), FString::Join(OutputLines, TEXT("\n")));
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKPythonTool::ExecuteFile(const FString& FilePath, const FString& Args, FString& OutError)
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin)
    {
        OutError = TEXT("PythonScriptPlugin不可用");
        return FString();
    }

    FPythonCommandEx CmdEx;
    if (Args.IsEmpty())
    {
        CmdEx.Command = FString::Printf(TEXT("exec(open(r'%s', encoding='utf-8').read())"), *FilePath);
        CmdEx.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
    }
    else
    {
        CmdEx.Command = FString::Printf(TEXT("\"%s\" %s"), *FilePath, *Args);
        CmdEx.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
        CmdEx.FileExecutionScope = EPythonFileExecutionScope::Private;
    }

    bool bSuccess = PythonPlugin->ExecPythonCommandEx(CmdEx);
    if (!bSuccess)
    {
        OutError = CmdEx.CommandResult;
        return FString();
    }

    // 从 LogOutput 收集 print() 输出
    TArray<FString> OutputLines;
    for (const FPythonLogOutputEntry& Entry : CmdEx.LogOutput)
    {
        if (!Entry.Output.IsEmpty())
        {
            OutputLines.Add(Entry.Output);
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("mode"), TEXT("file"));
    ResultObj->SetStringField(TEXT("file"), FilePath);
    ResultObj->SetStringField(TEXT("command_result"), CmdEx.CommandResult);

    if (OutputLines.Num() > 0)
    {
        ResultObj->SetStringField(TEXT("output"), FString::Join(OutputLines, TEXT("\n")));
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}
