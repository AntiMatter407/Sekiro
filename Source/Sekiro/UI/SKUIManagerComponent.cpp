#include "UI/SKUIManagerComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
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

/**
 * 提供未绑定 Lua 时的空 UI 管理 Tick 回退，避免 C++ 依赖模块名手写调用脚本。
 * 同名 Lua override 负责 UI 的逐帧业务编排；仅由组件 Tick 在游戏线程调用。
 *
 * @param DeltaTime 当前帧步长，单位秒；默认实现不消费该值。
 */
void USKUIManagerComponent::HandleUIManagerTick_Implementation(float DeltaTime)
{
    (void)DeltaTime;
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
    if (bUseLuaUIManagerLogic)
    {
        HandleUIManagerTick(DeltaTime);
    }
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
