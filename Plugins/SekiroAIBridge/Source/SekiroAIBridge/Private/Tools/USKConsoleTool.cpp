#include "Tools/USKConsoleTool.h"
#include "Engine/Engine.h"
#include "Misc/OutputDevice.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

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

    if (!GEngine)
    {
        OutError = TEXT("GEngine 不可用");
        return FString();
    }

    // 捕获控制台输出
    FStringOutputDevice OutputDevice;
    GEngine->Exec(nullptr, *Command, OutputDevice);

    FString Output = FString(OutputDevice);

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
