#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * JSON-RPC 2.0 消息结构体
 *
 * 支持三种消息类型：
 * - Request:  客户端->UE（含id，期待响应）
 * - Response: UE->客户端（含id）
 * - Notification: 客户端->UE（无id，不期待响应）或 UE->客户端（推送）
 *
 * 协议层不做业务语义解析，只负责JSON<->结构体转换。
 */
struct SEKIROAIBRIDGE_API FSKJsonRpcMessage
{
    /** JSON-RPC 版本，始终为 "2.0" */
    FString JsonRpc;

    /** 请求ID（String或Number），响应/错误中返回；Notification无此字段 */
    TOptional<FString> Id;

    /** 方法名（Request/Notification） */
    FString Method;

    /** 方法参数（Request/Notification），可为JSON Object或Array */
    TSharedPtr<FJsonValue> Params;

    /** 成功结果（Response） */
    TSharedPtr<FJsonValue> Result;

    /** 错误对象（Response Error） */
    TSharedPtr<FJsonObject> Error;

    // ---- 工厂方法 ----

    /** 从JSON字符串解析 */
    static bool Parse(const FString& JsonString, FSKJsonRpcMessage& OutMessage, FString& OutParseError);

    /** 构建成功响应 */
    static FString BuildResponse(const FString& Id, const TSharedPtr<FJsonValue>& Result);

    /** 构建错误响应，data为可选的附加错误信息 */
    static FString BuildError(const FString& Id, int32 Code, const FString& Message, const FString& Data = FString());

    /** 构建Notification（推送） */
    static FString BuildNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params);

    /** 是否为Notification（无id字段） */
    bool IsNotification() const { return !Id.IsSet(); }

    /** 是否为有效的JSON-RPC 2.0消息 */
    bool IsValid() const { return JsonRpc == TEXT("2.0"); }

    /** 标准 JSON-RPC 错误码 */
    enum EErrorCode
    {
        ParseError     = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams  = -32602,
        InternalError  = -32603,
        ServerError    = -32000,  // 自定义区间起始
    };
};
