#include "UI/SKHUD.h"

#include "GameFramework/PlayerController.h"
#include "UI/SKUIManagerComponent.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static UnLua::FLuaRetValues RequireSKHUDLuaModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKHUD Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKHUD Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    static bool ReadSKHUDLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName, FName FunctionName)
    {
        if (!ReturnValues.IsValid()) return false;
        if (ReturnValues.Num() == 0) return false;
        if (ReturnValues[0].GetType() == LUA_TNIL) return false;

        if (ReturnValues[0].GetType() == LUA_TBOOLEAN)
        {
            return ReturnValues[0].Value<bool>();
        }

        UE_LOG(LogTemp, Warning, TEXT("SKHUD Lua %s should return boolean. Module=%s"), *FunctionName.ToString(), *LuaModuleName);
        return false;
    }
}

ASKHUD::ASKHUD()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    UIManager = CreateDefaultSubobject<USKUIManagerComponent>(TEXT("UIManager"));
}

USKUIManagerComponent* ASKHUD::GetUIManager() const
{
    return UIManager;
}

void ASKHUD::SetUseLuaHUDLogic(bool bNewUseLuaHUDLogic)
{
    bUseLuaHUDLogic = bNewUseLuaHUDLogic;
}

bool ASKHUD::IsUsingLuaHUDLogic() const
{
    return bUseLuaHUDLogic;
}

void ASKHUD::SetLuaHUDModuleName(const FString& ModuleName)
{
    LuaHUDModuleName = ModuleName;
}

FString ASKHUD::GetLuaHUDModuleName() const
{
    return LuaHUDModuleName;
}

FString ASKHUD::GetModuleName_Implementation() const
{
    return LuaHUDModuleName;
}

void ASKHUD::RefreshCachedHUDOwner()
{
    RefreshCachedOwner();
}

bool ASKHUD::HasPlayerController() const
{
    return CachedPlayerController != nullptr;
}

bool ASKHUD::IsLocalPlayerController() const
{
    return CachedPlayerController && CachedPlayerController->IsLocalController();
}

APlayerController* ASKHUD::GetHUDPlayerController() const
{
    return CachedPlayerController;
}

void ASKHUD::BeginPlay()
{
    Super::BeginPlay();

    RefreshCachedOwner();
    TryCallLuaHUDBeginPlay();
}

void ASKHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UIManager)
    {
        UIManager->RemoveAllWidgets();
    }

    Super::EndPlay(EndPlayReason);
}

void ASKHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    RefreshCachedOwner();
    TryCallLuaHUDTick(DeltaSeconds);
}

bool ASKHUD::TryCallLuaHUDBeginPlay()
{
    const FString ModuleName = ResolveLuaHUDModuleName();
    if (!bUseLuaHUDLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKHUDLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["BeginPlay"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this);
    const bool bHandled = ReadSKHUDLuaHandled(FunctionReturnValues, ModuleName, FName(TEXT("BeginPlay")));
    FunctionReturnValues.Pop();
    return bHandled;
}

bool ASKHUD::TryCallLuaHUDTick(float DeltaSeconds)
{
    const FString ModuleName = ResolveLuaHUDModuleName();
    if (!bUseLuaHUDLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKHUDLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaSeconds);
    const bool bHandled = ReadSKHUDLuaHandled(FunctionReturnValues, ModuleName, FName(TEXT("Tick")));
    FunctionReturnValues.Pop();
    return bHandled;
}

FString ASKHUD::ResolveLuaHUDModuleName() const
{
    if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<ASKHUD*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaHUDModuleName;
}

void ASKHUD::RefreshCachedOwner()
{
    if (!CachedPlayerController)
    {
        CachedPlayerController = PlayerOwner;
    }
}
