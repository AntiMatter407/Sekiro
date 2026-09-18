#include "Subsystem/USKAIBridgeSubsystem.h"
#include "Server/FSKAIBridgeServer.h"
#include "Protocol/FSKJsonRpcMessage.h"
#include "Tools/USKAIToolRegistry.h"
#include "Tools/USKEditorStateTool.h"
#include "Tools/USKConsoleTool.h"
#include "Tools/USKAssetTool.h"
#include "Tools/USKPythonTool.h"
#include "Tools/USKCompileTool.h"
#include "Tools/USKBlueprintTool.h"
#include "Tools/USKEnhancedInputTool.h"
#include "Tools/USKAnimBlueprintTool.h"
#include "Tools/USKPIEControlTool.h"
#include "Tools/USKInputSimulateTool.h"
#include "Security/FSKAccessControl.h"
#include "Settings/USKAIBridgeSettings.h"
#include "SekiroAIBridgeLog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/CoreDelegates.h"

// ============================================================================
// 生命周期
// ============================================================================

void USKAIBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogSekiroAIBridge, Log, TEXT("========== AI桥接子系统初始化 =========="));

    USKAIBridgeSettings* Settings = USKAIBridgeSettings::Get();
    ServerPort = Settings->ServerPort;

    AccessControl = new FSKAccessControl();
    ToolRegistry = NewObject<USKAIToolRegistry>(this);
    RegisterAllTools();
    EndFrameDelegateHandle = FCoreDelegates::OnEndFrame.AddUObject(
        this, &USKAIBridgeSubsystem::ProcessPendingToolsCalls);

    if (!Settings->bAutoStartServer)
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("自动启动已禁用，跳过服务端创建"));
        return;
    }

    Server = new FSKAIBridgeServer(ServerPort);
    Server->OnMessageReceived.BindUObject(this, &USKAIBridgeSubsystem::OnMessageReceived);

    // 客户端断开时重置认证状态（防止认证跨连接泄漏）
    Server->OnClientDisconnected.BindUObject(this, &USKAIBridgeSubsystem::ResetClientAuth);

    if (Server->Start())
    {
        bAuthenticated = Settings->PreSharedKey.IsEmpty();
        UE_LOG(LogSekiroAIBridge, Log, TEXT("AI桥接服务端已启动: %s:%d（%s认证）"),
            *Settings->BindAddress, ServerPort,
            bAuthenticated ? TEXT("无") : TEXT("PSK"));
        UE_LOG(LogSekiroAIBridge, Log, TEXT("已注册 %d 个工具: %s"),
            ToolRegistry->GetAllTools().Num(),
            *FString::JoinBy(ToolRegistry->ListToolNames(), TEXT(", "), [](const FName& N) { return N.ToString(); }));

        if (Settings->bReadOnlyMode)
        {
            UE_LOG(LogSekiroAIBridge, Warning, TEXT("只读模式已启用，所有高风险操作将被拒绝"));
        }
    }
    else
    {
        UE_LOG(LogSekiroAIBridge, Error, TEXT("AI桥接服务端启动失败"));
    }
}

void USKAIBridgeSubsystem::Deinitialize()
{
    UE_LOG(LogSekiroAIBridge, Log, TEXT("========== AI桥接子系统关闭 =========="));

    if (EndFrameDelegateHandle.IsValid())
    {
        FCoreDelegates::OnEndFrame.Remove(EndFrameDelegateHandle);
        EndFrameDelegateHandle.Reset();
    }
    PendingToolCalls.Reset();

    if (Server)
    {
        Server->Shutdown();
        Server->OnMessageReceived.Unbind();
        Server->OnClientDisconnected.Unbind();
        delete Server;
        Server = nullptr;
    }

    if (AccessControl)
    {
        delete AccessControl;
        AccessControl = nullptr;
    }

    Super::Deinitialize();
}

// ============================================================================
// 工具注册
// ============================================================================

void USKAIBridgeSubsystem::RegisterAllTools()
{
    ToolRegistry->RegisterTool(NewObject<USKEditorStateTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKConsoleTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKAssetTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKPythonTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKCompileTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKBlueprintTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKEnhancedInputTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKAnimBlueprintTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKPIEControlTool>(this));
    ToolRegistry->RegisterTool(NewObject<USKInputSimulateTool>(this));

    UE_LOG(LogSekiroAIBridge, Log, TEXT("已注册 %d 个AI工具"), ToolRegistry->GetAllTools().Num());
}

// ============================================================================
// PSK认证
// ============================================================================

bool USKAIBridgeSubsystem::Authenticate(const TSharedPtr<FJsonObject>& AuthParams)
{
    USKAIBridgeSettings* Settings = USKAIBridgeSettings::Get();

    if (Settings->PreSharedKey.IsEmpty())
    {
        bAuthenticated = true;
        return true;
    }

    FString Key;
    if (AuthParams->TryGetStringField(TEXT("psk"), Key))
    {
        bool bMatch = (Key == Settings->PreSharedKey);
        if (!bMatch)
        {
            UE_LOG(LogSekiroAIBridge, Warning, TEXT("认证失败：PSK不匹配"));
        }
        return bMatch;
    }

    UE_LOG(LogSekiroAIBridge, Warning, TEXT("认证失败：缺少 psk 字段"));
    return false;
}

// ============================================================================
// 消息路由
// ============================================================================

void USKAIBridgeSubsystem::OnMessageReceived(const FString& JsonLine)
{
    FSKJsonRpcMessage Message;
    FString ParseError;

    if (!FSKJsonRpcMessage::Parse(JsonLine, Message, ParseError))
    {
        UE_LOG(LogSekiroAIBridge, Warning, TEXT("消息解析失败: %s"), *ParseError);
        // JSON-RPC 2.0: 解析错误必须返回 ParseError，id 设为 null（空字符串）
        if (Server)
        {
            Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(
                FString(), FSKJsonRpcMessage::ParseError, ParseError));
        }
        return;
    }

    if (!Message.IsValid())
    {
        UE_LOG(LogSekiroAIBridge, Warning, TEXT("无效的JSON-RPC消息（jsonrpc != 2.0）"));
        if (Server && Message.Id.IsSet())
        {
            Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(
                Message.Id.GetValue(),
                FSKJsonRpcMessage::InvalidRequest,
                TEXT("jsonrpc 字段必须为 \"2.0\"")));
        }
        return;
    }

    // ---- 认证处理 ----
    if (Message.Method == TEXT("auth"))
    {
        FString Id = Message.Id.IsSet() ? Message.Id.GetValue() : FString();
        const TSharedPtr<FJsonObject>* AuthParams = nullptr;

        if (!Message.Params.IsValid() || !Message.Params->TryGetObject(AuthParams))
        {
            if (!Id.IsEmpty() && Server)
            {
                Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(Id,
                    FSKJsonRpcMessage::InvalidParams, TEXT("认证需要 params 对象包含 psk 字段")));
            }
            UE_LOG(LogSekiroAIBridge, Warning, TEXT("认证消息缺少params"));
            return;
        }

        if (Authenticate(*AuthParams))
        {
            bAuthenticated = true;
            if (!Id.IsEmpty() && Server)
            {
                TSharedPtr<FJsonObject> OkObj = MakeShareable(new FJsonObject());
                OkObj->SetStringField(TEXT("message"), TEXT("authenticated"));
                Server->EnqueueResponse(FSKJsonRpcMessage::BuildResponse(Id,
                    MakeShareable(new FJsonValueObject(OkObj))));
            }
            UE_LOG(LogSekiroAIBridge, Log, TEXT("客户端认证成功"));
        }
        else
        {
            if (!Id.IsEmpty() && Server)
            {
                Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(Id,
                    FSKJsonRpcMessage::ServerError, TEXT("认证失败：PSK不匹配")));
            }
        }
        return;
    }

    // ---- 认证检查（非auth方法） ----
    if (!bAuthenticated)
    {
        USKAIBridgeSettings* Settings = USKAIBridgeSettings::Get();
        if (!Settings->PreSharedKey.IsEmpty())
        {
            if (Message.Id.IsSet() && Server)
            {
                Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(
                    Message.Id.GetValue(),
                    FSKJsonRpcMessage::ServerError,
                    TEXT("未认证，请先发送 auth 消息")));
            }
            return;
        }
    }

    // ---- 通知 ----
    if (Message.IsNotification())
    {
        // 通知不期望响应，但仍可处理（但不弹出确认对话框）
        if (Message.Method == TEXT("tools/call") && Message.Params.IsValid())
        {
            const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
            if (Message.Params->TryGetObject(ParamsObj))
            {
                QueueToolsCall(FString(), *ParamsObj, true);
            }
        }
        return;
    }

    // ---- 请求（有id） ----
    // JSON-RPC: id缺失的请求视为通知，此处作为InvalidRequest处理
    if (!Message.Id.IsSet())
    {
        UE_LOG(LogSekiroAIBridge, Warning, TEXT("收到无id的非通知消息，返回 InvalidRequest"));
        if (Server)
        {
            Server->EnqueueResponse(FSKJsonRpcMessage::BuildError(
                FString(), FSKJsonRpcMessage::InvalidRequest,
                TEXT("请求消息必须包含 id 字段")));
        }
        return;
    }

    FString Id = Message.Id.GetValue();
    FString Response;

    if (Message.Method == TEXT("tools/list"))
    {
        Response = HandleToolsList(Id);
    }
    else if (Message.Method == TEXT("tools/call"))
    {
        const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
        if (Message.Params.IsValid() && Message.Params->TryGetObject(ParamsObj))
        {
            QueueToolsCall(Id, *ParamsObj, false);
            return;
        }
        else
        {
            Response = FSKJsonRpcMessage::BuildError(Id,
                FSKJsonRpcMessage::InvalidParams, TEXT("tools/call 需要 params 对象"));
        }
    }
    else if (Message.Method == TEXT("ping"))
    {
        TSharedPtr<FJsonObject> Pong = MakeShareable(new FJsonObject());
        Pong->SetStringField(TEXT("message"), TEXT("pong"));
        Pong->SetBoolField(TEXT("running"), IsServerRunning());
        Pong->SetNumberField(TEXT("tools"), ToolRegistry->GetAllTools().Num());
        Response = FSKJsonRpcMessage::BuildResponse(Id, MakeShareable(new FJsonValueObject(Pong)));
    }
    else
    {
        Response = FSKJsonRpcMessage::BuildError(Id,
            FSKJsonRpcMessage::MethodNotFound,
            FString::Printf(TEXT("未知方法: %s"), *Message.Method));
    }

    if (!Response.IsEmpty() && Server)
    {
        Server->EnqueueResponse(Response);
    }
}

/**
 * 保存一个已经通过协议解析和认证检查的工具调用，等待主循环帧尾串行执行。
 * 本函数只允许在 GameThread 调用；Params 必须是有效的 tools/call 对象，队列通过共享引用延长其生命周期。
 * Id 是原始 JSON-RPC 请求 ID，通知可为空；bIsNotification 明确控制是否回包，避免用空 ID 猜测消息类型。
 * 本函数不执行工具、不发送响应；它会修改 PendingToolCalls，实际副作用最早发生在后续 OnEndFrame。
 */
void USKAIBridgeSubsystem::QueueToolsCall(
    const FString& Id,
    const TSharedPtr<FJsonObject>& Params,
    bool bIsNotification)
{
    check(IsInGameThread());
    if (!Params.IsValid()) return;

    FSekiroPendingToolCall PendingCall;
    PendingCall.Id = Id;
    PendingCall.Params = Params;
    PendingCall.bIsNotification = bIsNotification;
    PendingToolCalls.Add(MoveTemp(PendingCall));
}

/**
 * 在引擎完成当前帧 TickFunction 调度后执行队首工具调用，并把有 ID 请求的结果送回 TCP 发送队列。
 * 本函数由 FCoreDelegates::OnEndFrame 在 GameThread 调用；每帧只消费一个元素以保持严格 FIFO，
 * 并隔离连续的蓝图编译/重实例化操作。通知仍会执行但不会响应；服务端已关闭时仅丢弃响应。
 * 重入保护保证工具内部触发嵌套帧尾广播时不会重复消费队列；本函数会执行工具产生的编辑器副作用。
 */
void USKAIBridgeSubsystem::ProcessPendingToolsCalls()
{
    check(IsInGameThread());
    if (bIsProcessingToolCall || PendingToolCalls.IsEmpty()) return;

    TGuardValue<bool> ProcessingGuard(bIsProcessingToolCall, true);
    FSekiroPendingToolCall PendingCall = MoveTemp(PendingToolCalls[0]);
    PendingToolCalls.RemoveAt(0, 1, EAllowShrinking::No);

    if (!PendingCall.Params.IsValid()) return;

    FString Response = HandleToolsCall(
        PendingCall.Id,
        PendingCall.Params,
        PendingCall.bIsNotification);
    if (!PendingCall.bIsNotification && !Response.IsEmpty() && Server)
    {
        Server->EnqueueResponse(Response);
    }
}

void USKAIBridgeSubsystem::ResetClientAuth()
{
    bAuthenticated = false;
    PendingToolCalls.Reset();
    UE_LOG(LogSekiroAIBridge, Log, TEXT("客户端断开，认证状态已重置"));
}

// ============================================================================
// tools/list 处理
// ============================================================================

FString USKAIBridgeSubsystem::HandleToolsList(const FString& Id)
{
    TArray<TSharedPtr<FJsonValue>> ToolsArray;

    for (const TScriptInterface<ISKAIToolInterface>& Tool : ToolRegistry->GetAllTools())
    {
        if (!Tool) continue;

        TSharedPtr<FJsonObject> ToolObj = MakeShareable(new FJsonObject());
        ToolObj->SetStringField(TEXT("name"), Tool->GetToolName().ToString());
        ToolObj->SetStringField(TEXT("description"), Tool->GetToolDescription());

        TSharedPtr<FJsonObject> SchemaObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Tool->GetInputSchemaJson());
        if (FJsonSerializer::Deserialize(Reader, SchemaObj))
        {
            ToolObj->SetObjectField(TEXT("inputSchema"), SchemaObj);
        }

        ToolsArray.Add(MakeShareable(new FJsonValueObject(ToolObj)));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetArrayField(TEXT("tools"), ToolsArray);

    return FSKJsonRpcMessage::BuildResponse(Id, MakeShareable(new FJsonValueObject(ResultObj)));
}

// ============================================================================
// tools/call 处理（含确认和安全检查）
// ============================================================================

/**
 * 校验并同步执行一个已经延迟到安全帧尾的工具调用，再生成 JSON-RPC/MCP 格式结果。
 * 本函数只允许由 ProcessPendingToolsCalls 在 GameThread 调用；它不负责调度或直接发送响应。
 * Id 是待原样回传的请求 ID；Params 必须包含工具名，可选 arguments 对象；bIsNotification 控制确认语义和回包。
 * 返回完整 JSON-RPC 响应或错误字符串；通知和成功执行返回空字符串。工具执行可能修改编辑器和资产状态。
 */
FString USKAIBridgeSubsystem::HandleToolsCall(
    const FString& Id,
    const TSharedPtr<FJsonObject>& Params,
    bool bIsNotification)
{
    check(IsInGameThread());

    FString ToolName;
    if (!Params->TryGetStringField(TEXT("name"), ToolName))
    {
        return FSKJsonRpcMessage::BuildError(Id,
            FSKJsonRpcMessage::InvalidParams, TEXT("缺少 tool name 参数"));
    }

    TScriptInterface<ISKAIToolInterface> Tool = ToolRegistry->FindTool(FName(*ToolName));
    if (!Tool)
    {
        return FSKJsonRpcMessage::BuildError(Id,
            FSKJsonRpcMessage::MethodNotFound,
            FString::Printf(TEXT("工具未注册: %s。可用工具: %s"),
                *ToolName, *FString::JoinBy(ToolRegistry->ListToolNames(), TEXT(", "), [](const FName& N) { return N.ToString(); })));
    }

    // 提取 arguments
    FString ArgsJson;
    const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
    if (Params->TryGetObjectField(TEXT("arguments"), ArgsObj))
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ArgsJson);
        FJsonSerializer::Serialize((*ArgsObj).ToSharedRef(), Writer);
    }

    // 只读模式检查
    USKAIBridgeSettings* Settings = USKAIBridgeSettings::Get();
    if (Settings->bReadOnlyMode && Tool->RequiresConfirmation())
    {
        return FSKJsonRpcMessage::BuildError(Id,
            FSKJsonRpcMessage::ServerError,
            TEXT("只读模式已启用，拒绝高风险操作"));
    }

    // 确认由客户端（Claude Code /aibridge Skill）在对话中处理，UE侧不弹窗
    bool bUserConfirmed = bIsNotification || Tool->RequiresConfirmation();

    // 执行工具
    FString ErrorMsg;
    FString Result = Tool->Execute(ArgsJson, ErrorMsg);

    if (!ErrorMsg.IsEmpty())
    {
        return FSKJsonRpcMessage::BuildError(Id,
            FSKJsonRpcMessage::ServerError,
            ErrorMsg,
            ErrorMsg  // 重复作为data字段，供调试
        );
    }

    // 构建MCP格式响应
    TSharedPtr<FJsonObject> ResultWrapper = MakeShareable(new FJsonObject());

    TArray<TSharedPtr<FJsonValue>> ContentArray;
    TSharedPtr<FJsonObject> TextContent = MakeShareable(new FJsonObject());
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), Result);
    ContentArray.Add(MakeShareable(new FJsonValueObject(TextContent)));
    ResultWrapper->SetArrayField(TEXT("content"), ContentArray);
    ResultWrapper->SetBoolField(TEXT("user_confirmed"), bUserConfirmed);

    if (bIsNotification) return FString();

    return FSKJsonRpcMessage::BuildResponse(Id, MakeShareable(new FJsonValueObject(ResultWrapper)));
}

// ============================================================================
// 通知广播
// ============================================================================

bool USKAIBridgeSubsystem::IsServerRunning() const
{
    return Server != nullptr && Server->IsRunning();
}

void USKAIBridgeSubsystem::BroadcastNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
    if (Server)
    {
        FString Notification = FSKJsonRpcMessage::BuildNotification(Method, Params);
        Server->BroadcastNotification(Notification);
    }
}
