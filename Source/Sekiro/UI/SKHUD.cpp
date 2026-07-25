#include "UI/SKHUD.h"

#include "GameFramework/PlayerController.h"
#include "UI/SKUIManagerComponent.h"

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

/**
 * 提供未绑定 Lua 时的 HUD 初始化回退；默认实现不创建业务界面。
 * 同名 Lua override 可在玩家和 UIManager 缓存完成后配置界面；仅由 BeginPlay 在游戏线程调用。
 */
void ASKHUD::HandleHUDInitialized_Implementation()
{
}

/**
 * 提供未绑定 Lua 时的空 HUD Tick 回退，避免 C++ 通过模块名手写分发脚本。
 * 同名 Lua override 负责 HUD 逐帧编排；仅由 AHUD Tick 在游戏线程调用。
 *
 * @param DeltaSeconds 当前帧步长，单位秒；默认实现不消费该值。
 */
void ASKHUD::HandleHUDTick_Implementation(float DeltaSeconds)
{
    (void)DeltaSeconds;
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
    if (bUseLuaHUDLogic)
    {
        HandleHUDInitialized();
    }
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
    if (bUseLuaHUDLogic)
    {
        HandleHUDTick(DeltaSeconds);
    }
}

void ASKHUD::RefreshCachedOwner()
{
    if (!CachedPlayerController)
    {
        CachedPlayerController = PlayerOwner;
    }
}
