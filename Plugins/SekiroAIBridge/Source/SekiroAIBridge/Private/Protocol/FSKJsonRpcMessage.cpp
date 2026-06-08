#include "Protocol/FSKJsonRpcMessage.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

// ============================================================================
// 解析
// ============================================================================

bool FSKJsonRpcMessage::Parse(const FString& JsonString, FSKJsonRpcMessage& OutMessage, FString& OutParseError)
{
    // 跳过空行
    FString Trimmed = JsonString.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        OutParseError = FString::Printf(TEXT("JSON解析失败: %s"), *Trimmed.Left(100));
        return false;
    }

    // 解析 jsonrpc 字段
    if (!JsonObject->TryGetStringField(TEXT("jsonrpc"), OutMessage.JsonRpc))
    {
        OutParseError = TEXT("缺少 jsonrpc 字段");
        return false;
    }

    // 解析 id 字段（可选）
    FString IdStr;
    if (JsonObject->TryGetStringField(TEXT("id"), IdStr))
    {
        OutMessage.Id = IdStr;
    }
    else
    {
        // 尝试数字id
        double IdNum;
        if (JsonObject->TryGetNumberField(TEXT("id"), IdNum))
        {
            OutMessage.Id = FString::SanitizeFloat(IdNum, 0);
        }
    }

    // 解析 method 字段（请求/通知必须包含method）
    if (!JsonObject->TryGetStringField(TEXT("method"), OutMessage.Method))
    {
        // 可能是响应消息（有result或error字段），允许method为空
        if (!JsonObject->HasField(TEXT("result")) && !JsonObject->HasField(TEXT("error")))
        {
            OutParseError = TEXT("请求/通知消息缺少 method 字段");
            return false;
        }
    }

    // 解析 params 字段
    TSharedPtr<FJsonValue> ParamsValue = JsonObject->TryGetField(TEXT("params"));
    if (ParamsValue.IsValid())
    {
        OutMessage.Params = ParamsValue;
    }

    // 解析 result 字段（响应消息）
    TSharedPtr<FJsonValue> ResultValue = JsonObject->TryGetField(TEXT("result"));
    if (ResultValue.IsValid())
    {
        OutMessage.Result = ResultValue;
    }

    // 解析 error 字段（错误响应）
    const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObj))
    {
        OutMessage.Error = *ErrorObj;
    }

    return true;
}

// ============================================================================
// 构建响应
// ============================================================================

FString FSKJsonRpcMessage::BuildResponse(const FString& Id, const TSharedPtr<FJsonValue>& Result)
{
    TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    JsonObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    if (!Id.IsEmpty())
    {
        JsonObj->SetStringField(TEXT("id"), Id);
    }

    if (Result.IsValid())
    {
        JsonObj->SetField(TEXT("result"), Result);
    }
    else
    {
        JsonObj->SetObjectField(TEXT("result"), MakeShareable(new FJsonObject()));
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
    Output += TEXT("\n");
    return Output;
}

FString FSKJsonRpcMessage::BuildError(const FString& Id, int32 Code, const FString& Message, const FString& Data)
{
    TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    JsonObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    if (!Id.IsEmpty())
    {
        JsonObj->SetStringField(TEXT("id"), Id);
    }

    TSharedPtr<FJsonObject> ErrorObj = MakeShareable(new FJsonObject());
    ErrorObj->SetNumberField(TEXT("code"), Code);
    ErrorObj->SetStringField(TEXT("message"), Message);
    if (!Data.IsEmpty())
    {
        ErrorObj->SetStringField(TEXT("data"), Data);
    }
    JsonObj->SetObjectField(TEXT("error"), ErrorObj);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
    Output += TEXT("\n");
    return Output;
}

FString FSKJsonRpcMessage::BuildNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject());
    JsonObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    JsonObj->SetStringField(TEXT("method"), Method);

    if (Params.IsValid())
    {
        JsonObj->SetObjectField(TEXT("params"), Params);
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
    Output += TEXT("\n");
    return Output;
}
