#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "USKAIBridgeSubsystem.generated.h"

class FSKAIBridgeServer;
class USKAIToolRegistry;
class FSKAccessControl;
class USKAIBridgeSettings;
class FJsonObject;

/**
 * AI桥接子系统（EditorSubsystem）
 *
 * 随编辑器生命周期自动创建/销毁。
 * Initialize() 启动 TCP 服务端并注册所有内置工具，
 * Deinitialize() 关闭连接并清理资源。
 * 负责将收到的 JSON-RPC 消息路由到工具注册表，
 * 对高风险操作通过 FSKAccessControl 弹出确认对话框。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKAIBridgeSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** 获取服务端实例 */
    FSKAIBridgeServer* GetServer() const { return Server; }

    /** 获取工具注册表 */
    USKAIToolRegistry* GetToolRegistry() const { return ToolRegistry; }

    /** 获取访问控制 */
    FSKAccessControl* GetAccessControl() const { return AccessControl; }

    /** 是否正在监听 */
    bool IsServerRunning() const;

    /** 获取监听端口 */
    int32 GetServerPort() const { return ServerPort; }

    /** 广播通知给所有客户端 */
    void BroadcastNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params);

private:
    /** 注册所有内置工具 */
    void RegisterAllTools();

    /** JSON-RPC 消息处理入口（在GameThread上调用） */
    void OnMessageReceived(const FString& JsonLine);

    /** 处理 tools/list 请求 */
    FString HandleToolsList(const FString& Id);

    /** 处理 tools/call 请求（含确认检查） */
    FString HandleToolsCall(const FString& Id, const TSharedPtr<FJsonObject>& Params);

    /** 验证PSK认证（如有配置） */
    bool Authenticate(const TSharedPtr<FJsonObject>& AuthParams);

    /** 客户端断开时重置认证状态 */
    void ResetClientAuth();

    FSKAIBridgeServer* Server;

    UPROPERTY()
    USKAIToolRegistry* ToolRegistry;

    FSKAccessControl* AccessControl;
    int32 ServerPort;
    bool bAuthenticated = false;
};
