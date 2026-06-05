#include "SekiroTools.h"
#include "Modules/ModuleManager.h"

void FSekiroToolsModule::StartupModule()
{
    FModuleManager::Get().LoadModule(TEXT("MaterialEditor"));
}

void FSekiroToolsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSekiroToolsModule, SekiroTools)
