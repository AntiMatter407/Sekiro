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

/**
 * 游戏线程设置独立 Boss 展示对象，允许空对象显式关闭；不寻找敌人或修改锁定。
 * @param TargetActor 可空目标，只保存弱引用。
 * @param DisplayName 外部提供的显示资料，不推导Boss身份、阶段或忍杀节点。
 */
void ASKHUD::SetBossDisplayTarget(AActor* TargetActor, FText DisplayName)
{
    if (!IsInGameThread()) return;
    BossDisplayTarget = IsValid(TargetActor) ? TargetActor : nullptr;
    BossDisplayName = DisplayName;
    HandleBossDisplayTargetChanged(BossDisplayTarget.Get(), BossDisplayName);
}

/** 游戏线程返回显式Boss弱目标；已销毁或未设置返回空，不转移所有权。 */
AActor* ASKHUD::GetBossDisplayTarget() const
{
    return IsInGameThread() ? BossDisplayTarget.Get() : nullptr;
}

/** 游戏线程返回外部提供的Boss名称资料，不生成占位名称。 */
FText ASKHUD::GetBossDisplayName() const
{
    return IsInGameThread() ? BossDisplayName : FText::GetEmpty();
}

/**
 * 游戏线程提供Lua展示切换入口；原生不自动创建界面。
 * @param TargetActor 新的可空展示对象。
 * @param DisplayName 外部显示名称，只在当前调用栈读取。
 */
void ASKHUD::HandleBossDisplayTargetChanged_Implementation(AActor* TargetActor, const FText& DisplayName)
{
}

/** 游戏线程在移除Widget前给Lua解绑委托和清理显示状态的机会。 */
void ASKHUD::HandleHUDShutdown_Implementation()
{
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
    HandleHUDShutdown();
    BossDisplayTarget.Reset();
    BossDisplayName = FText::GetEmpty();
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
