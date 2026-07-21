#include "UI/SKUIManagerComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static UnLua::FLuaRetValues RequireSKUIManagerLuaModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKUIManager Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKUIManager Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    static bool ReadSKUIManagerLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName)
    {
        if (!ReturnValues.IsValid()) return false;
        if (ReturnValues.Num() == 0) return false;
        if (ReturnValues[0].GetType() == LUA_TNIL) return false;

        if (ReturnValues[0].GetType() == LUA_TBOOLEAN)
        {
            return ReturnValues[0].Value<bool>();
        }

        UE_LOG(LogTemp, Warning, TEXT("SKUIManager Lua Tick should return boolean. Module=%s"), *LuaModuleName);
        return false;
    }
}

USKUIManagerComponent::USKUIManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

    LayerZOrders.Add(FName(TEXT("Background")), 0);
    LayerZOrders.Add(FName(TEXT("HUD")), 10);
    LayerZOrders.Add(FName(TEXT("Indicator")), 80);
    LayerZOrders.Add(FName(TEXT("Menu")), 100);
    LayerZOrders.Add(FName(TEXT("Modal")), 200);
    LayerZOrders.Add(FName(TEXT("Debug")), 900);
}

void USKUIManagerComponent::SetUseLuaUIManagerLogic(bool bNewUseLuaUIManagerLogic)
{
    bUseLuaUIManagerLogic = bNewUseLuaUIManagerLogic;
}

bool USKUIManagerComponent::IsUsingLuaUIManagerLogic() const
{
    return bUseLuaUIManagerLogic;
}

void USKUIManagerComponent::SetLuaUIManagerModuleName(const FString& ModuleName)
{
    LuaUIManagerModuleName = ModuleName;
}

FString USKUIManagerComponent::GetLuaUIManagerModuleName() const
{
    return LuaUIManagerModuleName;
}

FString USKUIManagerComponent::GetModuleName_Implementation() const
{
    return LuaUIManagerModuleName;
}

void USKUIManagerComponent::RefreshCachedUIOwner()
{
    RefreshCachedOwner();
}

bool USKUIManagerComponent::HasPlayerController() const
{
    return PlayerController != nullptr;
}

bool USKUIManagerComponent::IsLocalPlayerController() const
{
    return PlayerController && PlayerController->IsLocalController();
}

void USKUIManagerComponent::SetLayerZOrder(FName LayerName, int32 ZOrder)
{
    if (LayerName.IsNone()) return;

    LayerZOrders.Add(LayerName, ZOrder);
}

int32 USKUIManagerComponent::GetLayerZOrder(FName LayerName) const
{
    const int32* FoundZOrder = LayerZOrders.Find(LayerName);
    return FoundZOrder ? *FoundZOrder : 0;
}

int32 USKUIManagerComponent::GetResolvedWidgetZOrder(FName LayerName, int32 ZOrderOffset) const
{
    return GetLayerZOrder(LayerName) + ZOrderOffset;
}

bool USKUIManagerComponent::CreateWidgetByClass(FName WidgetName, TSubclassOf<UUserWidget> WidgetClass, FName LayerName, int32 ZOrderOffset)
{
    RefreshCachedOwner();
    if (WidgetName.IsNone() || !WidgetClass || !PlayerController) return false;

    RemoveWidget(WidgetName);

    UUserWidget* Widget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
    if (!Widget) return false;

    Widget->SetVisibility(ESlateVisibility::Hidden);
    Widget->AddToViewport(GetResolvedWidgetZOrder(LayerName, ZOrderOffset));
    ManagedWidgets.Add(WidgetName, Widget);
    return true;
}

bool USKUIManagerComponent::CreateWidgetByPath(FName WidgetName, const FString& WidgetClassPath, FName LayerName, int32 ZOrderOffset)
{
    const TSubclassOf<UUserWidget> WidgetClass = LoadWidgetClass(WidgetClassPath);
    return CreateWidgetByClass(WidgetName, WidgetClass, LayerName, ZOrderOffset);
}

bool USKUIManagerComponent::HasManagedWidget(FName WidgetName) const
{
    return FindManagedWidget(WidgetName) != nullptr;
}

UUserWidget* USKUIManagerComponent::GetManagedWidget(FName WidgetName) const
{
    return FindManagedWidget(WidgetName);
}

void USKUIManagerComponent::ShowWidget(FName WidgetName)
{
    SetWidgetVisible(WidgetName, true);
}

void USKUIManagerComponent::HideWidget(FName WidgetName)
{
    SetWidgetVisible(WidgetName, false);
}

void USKUIManagerComponent::SetWidgetVisible(FName WidgetName, bool bVisible)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (!Widget) return;

    Widget->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
}

void USKUIManagerComponent::SetWidgetOpacity(FName WidgetName, float Opacity)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (!Widget) return;

    Widget->SetRenderOpacity(FMath::Clamp(Opacity, 0.0f, 1.0f));
}

void USKUIManagerComponent::SetWidgetPosition(FName WidgetName, float ScreenX, float ScreenY)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (!Widget) return;

    Widget->SetPositionInViewport(FVector2D(ScreenX, ScreenY), true);
}

void USKUIManagerComponent::SetWidgetSize(FName WidgetName, float SizeX, float SizeY)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (!Widget) return;

    Widget->SetDesiredSizeInViewport(FVector2D(FMath::Max(1.0f, SizeX), FMath::Max(1.0f, SizeY)));
}

void USKUIManagerComponent::SetWidgetAlignment(FName WidgetName, float AlignmentX, float AlignmentY)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (!Widget) return;

    Widget->SetAlignmentInViewport(FVector2D(AlignmentX, AlignmentY));
}

void USKUIManagerComponent::RemoveWidget(FName WidgetName)
{
    UUserWidget* Widget = FindManagedWidget(WidgetName);
    if (Widget)
    {
        Widget->RemoveFromParent();
    }

    ManagedWidgets.Remove(WidgetName);
}

void USKUIManagerComponent::RemoveAllWidgets()
{
    for (TMap<FName, TObjectPtr<UUserWidget>>::TIterator WidgetIt(ManagedWidgets); WidgetIt; ++WidgetIt)
    {
        UUserWidget* Widget = WidgetIt.Value().Get();
        if (Widget)
        {
            Widget->RemoveFromParent();
        }
    }

    ManagedWidgets.Empty();
}

void USKUIManagerComponent::SetGameOnlyInputMode()
{
    RefreshCachedOwner();
    if (!PlayerController) return;

    FInputModeGameOnly InputMode;
    PlayerController->SetInputMode(InputMode);
    PlayerController->bShowMouseCursor = false;
}

void USKUIManagerComponent::SetUIOnlyInputMode(FName FocusWidgetName, bool bShowCursor)
{
    SetInputModeWithWidget(FocusWidgetName, false, bShowCursor);
}

void USKUIManagerComponent::SetGameAndUIInputMode(FName FocusWidgetName, bool bShowCursor)
{
    SetInputModeWithWidget(FocusWidgetName, true, bShowCursor);
}

void USKUIManagerComponent::SetMouseCursorVisible(bool bVisible)
{
    RefreshCachedOwner();
    if (!PlayerController) return;

    PlayerController->bShowMouseCursor = bVisible;
}

/**
 * 缓存 UI 所属玩家，并为原生 UnLua 组件补发一次标准 ReceiveBeginPlay 生命周期。
 * 蓝图生成类和非原生类已经由 UActorComponent::BeginPlay 派发，本函数仅处理纯原生组件，
 * 从而保证 Lua 的运行期初始化只执行一次且能安全访问 PlayerController。
 * 本函数只在游戏线程执行，不创建具体业务界面，也不直接调用 Lua Initialize。
 */
void USKUIManagerComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();

    RefreshCachedOwner();

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

void USKUIManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveAllWidgets();

    Super::EndPlay(EndPlayReason);
}

void USKUIManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    RefreshCachedOwner();
    TryCallLuaUIManagerTick(DeltaTime);
}

bool USKUIManagerComponent::TryCallLuaUIManagerTick(float DeltaTime)
{
    const FString ModuleName = ResolveLuaUIManagerModuleName();
    if (!bUseLuaUIManagerLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKUIManagerLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
    const bool bHandled = ReadSKUIManagerLuaHandled(FunctionReturnValues, ModuleName);
    FunctionReturnValues.Pop();
    return bHandled;
}

FString USKUIManagerComponent::ResolveLuaUIManagerModuleName() const
{
    if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USKUIManagerComponent*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaUIManagerModuleName;
}

void USKUIManagerComponent::RefreshCachedOwner()
{
    if (PlayerController) return;

    if (APlayerController* OwnerPlayerController = Cast<APlayerController>(GetOwner()))
    {
        PlayerController = OwnerPlayerController;
        return;
    }

    if (AHUD* OwnerHUD = Cast<AHUD>(GetOwner()))
    {
        PlayerController = OwnerHUD->PlayerOwner;
    }
}

TSubclassOf<UUserWidget> USKUIManagerComponent::LoadWidgetClass(const FString& WidgetClassPath) const
{
    const FString TrimmedWidgetClassPath = WidgetClassPath.TrimStartAndEnd();
    if (TrimmedWidgetClassPath.IsEmpty()) return nullptr;

    UClass* LoadedClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *TrimmedWidgetClassPath);
    if (!LoadedClass)
    {
        const FString GeneratedWidgetClassPath = MakeGeneratedWidgetClassPath(TrimmedWidgetClassPath);
        LoadedClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *GeneratedWidgetClassPath);
    }

    return LoadedClass;
}

FString USKUIManagerComponent::MakeGeneratedWidgetClassPath(const FString& WidgetClassPath) const
{
    FString NormalizedPath = WidgetClassPath.TrimStartAndEnd();
    if (NormalizedPath.EndsWith(TEXT("_C")) || NormalizedPath.Contains(TEXT("_C'")))
    {
        return NormalizedPath;
    }

    NormalizedPath.RemoveFromStart(TEXT("Blueprint'"));
    NormalizedPath.RemoveFromStart(TEXT("WidgetBlueprint'"));
    NormalizedPath.RemoveFromEnd(TEXT("'"));

    FString PackagePath;
    FString AssetName;
    if (!NormalizedPath.Split(TEXT("."), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        int32 LastSlashIndex = INDEX_NONE;
        if (NormalizedPath.FindLastChar(TEXT('/'), LastSlashIndex))
        {
            AssetName = NormalizedPath.Mid(LastSlashIndex + 1);
            PackagePath = NormalizedPath;
        }
    }

    if (PackagePath.IsEmpty() || AssetName.IsEmpty()) return NormalizedPath;

    return FString::Printf(TEXT("%s.%s_C"), *PackagePath, *AssetName);
}

UUserWidget* USKUIManagerComponent::FindManagedWidget(FName WidgetName) const
{
    const TObjectPtr<UUserWidget>* FoundWidget = ManagedWidgets.Find(WidgetName);
    return FoundWidget ? FoundWidget->Get() : nullptr;
}

void USKUIManagerComponent::SetInputModeWithWidget(FName FocusWidgetName, bool bGameAndUI, bool bShowCursor)
{
    RefreshCachedOwner();
    if (!PlayerController) return;

    UUserWidget* FocusWidget = FindManagedWidget(FocusWidgetName);
    if (bGameAndUI)
    {
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        if (FocusWidget)
        {
            InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
        }
        PlayerController->SetInputMode(InputMode);
    }
    else
    {
        FInputModeUIOnly InputMode;
        if (FocusWidget)
        {
            InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
        }
        PlayerController->SetInputMode(InputMode);
    }

    PlayerController->bShowMouseCursor = bShowCursor;
}
