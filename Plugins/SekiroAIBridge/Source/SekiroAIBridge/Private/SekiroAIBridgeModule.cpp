#include "SekiroAIBridge.h"
#include "SekiroAIBridgeLog.h"

#define LOCTEXT_NAMESPACE "FSekiroAIBridgeModule"

void FSekiroAIBridgeModule::StartupModule()
{
    UE_LOG(LogSekiroAIBridge, Log, TEXT("SekiroAIBridge 插件已加载"));
    RegisterMenus();
}

void FSekiroAIBridgeModule::ShutdownModule()
{
    UnregisterMenus();
    UE_LOG(LogSekiroAIBridge, Log, TEXT("SekiroAIBridge 插件已卸载"));
}

FSekiroAIBridgeModule& FSekiroAIBridgeModule::Get()
{
    return FModuleManager::LoadModuleChecked<FSekiroAIBridgeModule>("SekiroAIBridge");
}

void FSekiroAIBridgeModule::SendNotificationToAllClients(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
    // 由 USKAIBridgeSubsystem 代理实现，此处为模块级接口
}

void FSekiroAIBridgeModule::RegisterMenus()
{
    UE_LOG(LogSekiroAIBridge, Log, TEXT("菜单已注册"));
}

void FSekiroAIBridgeModule::UnregisterMenus()
{
}

IMPLEMENT_MODULE(FSekiroAIBridgeModule, SekiroAIBridge)

#undef LOCTEXT_NAMESPACE
