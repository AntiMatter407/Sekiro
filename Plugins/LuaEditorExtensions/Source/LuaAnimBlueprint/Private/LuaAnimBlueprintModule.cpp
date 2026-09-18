#include "LuaAnimDebugRuntime.h"

#include "Modules/ModuleManager.h"

class FLuaAnimBlueprintModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FLuaAnimDebugRuntime::Startup();
    }

    virtual void ShutdownModule() override
    {
        FLuaAnimDebugRuntime::Shutdown();
    }
};

IMPLEMENT_MODULE(FLuaAnimBlueprintModule, LuaAnimBlueprint)
