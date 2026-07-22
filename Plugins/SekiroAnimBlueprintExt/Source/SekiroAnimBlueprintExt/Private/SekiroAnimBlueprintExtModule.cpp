#include "SekiroLuaAnimDebugRuntime.h"

#include "Modules/ModuleManager.h"

class FSekiroAnimBlueprintExtModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FSekiroLuaAnimDebugRuntime::Startup();
    }

    virtual void ShutdownModule() override
    {
        FSekiroLuaAnimDebugRuntime::Shutdown();
    }
};

IMPLEMENT_MODULE(FSekiroAnimBlueprintExtModule, SekiroAnimBlueprintExt)
