#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FJsonObject;

struct FSKJsonRpcMessage;

class SEKIROAIBRIDGE_API FSekiroAIBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FSekiroAIBridgeModule& Get();

    /// 发送日志消息到所有已连接客户端
    void SendNotificationToAllClients(const FString& Method, const TSharedPtr<FJsonObject>& Params);

private:
    void RegisterMenus();
    void UnregisterMenus();

    TArray<FDelegateHandle> MenuExtenderHandles;
};
