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
	return TEXT("{\"type\":\"object\",\"properties\":{\"script\":{\"type\":\"string\",\"description\":\"Python code to execute\"},\"file\":{\"type\":\"string\",\"description\":\"Python file path\"}},\"oneOf\":[{\"required\":[\"script\"]},{\"required\":[\"file\"]}]}");
}

FString USKPythonTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Script;
        FString File;
        ArgsObj->TryGetStringField(TEXT("script"), Script);
        ArgsObj->TryGetStringField(TEXT("file"), File);

        if (!Script.IsEmpty())
        {
            FString Preview = Script.Len() > 80 ? Script.Left(77) + TEXT("...") : Script;
            return FString::Printf(TEXT("执行Python代码: %s"), *Preview);
        }
        if (!File.IsEmpty())
        {
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
    ArgsObj->TryGetStringField(TEXT("script"), Script);
    ArgsObj->TryGetStringField(TEXT("file"), File);

    if (!Script.IsEmpty())
    {
        return ExecuteScript(Script, OutError);
    }
    else if (!File.IsEmpty())
    {
        return ExecuteFile(File, OutError);
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

    bool bSuccess = PythonPlugin->ExecPythonCommand(*Script);
    if (!bSuccess)
    {
        OutError = TEXT("Python脚本执行失败");
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("mode"), TEXT("script"));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKPythonTool::ExecuteFile(const FString& FilePath, FString& OutError)
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin)
    {
        OutError = TEXT("PythonScriptPlugin不可用");
        return FString();
    }

    // UE5.2: 使用 ExecPythonCommand 执行文件（无单独的 ExecPythonFile）
    FString Command = FString::Printf(TEXT("exec(open(r'%s', encoding='utf-8').read())"), *FilePath);
    bool bSuccess = PythonPlugin->ExecPythonCommand(*Command);
    if (!bSuccess)
    {
        OutError = FString::Printf(TEXT("Python文件执行失败: %s"), *FilePath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("mode"), TEXT("file"));
    ResultObj->SetStringField(TEXT("file"), FilePath);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}
