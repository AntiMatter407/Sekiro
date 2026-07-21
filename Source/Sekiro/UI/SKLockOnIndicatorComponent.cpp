#include "UI/SKLockOnIndicatorComponent.h"

#include "Camera/SKCameraManagerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "UI/SKLockOnIndicatorWidget.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static UnLua::FLuaRetValues RequireSKLockOnIndicatorLuaModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKLockOnIndicator Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKLockOnIndicator Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    static bool ReadSKLockOnIndicatorLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName)
    {
        if (!ReturnValues.IsValid()) return false;
        if (ReturnValues.Num() == 0) return false;
        if (ReturnValues[0].GetType() == LUA_TNIL) return false;

        if (ReturnValues[0].GetType() == LUA_TBOOLEAN)
        {
            return ReturnValues[0].Value<bool>();
        }

        UE_LOG(LogTemp, Warning, TEXT("SKLockOnIndicator Lua Tick should return boolean. Module=%s"), *LuaModuleName);
        return false;
    }
}

USKLockOnIndicatorComponent::USKLockOnIndicatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void USKLockOnIndicatorComponent::SetUseLuaLockOnIndicatorLogic(bool bNewUseLuaLockOnIndicatorLogic)
{
    bUseLuaLockOnIndicatorLogic = bNewUseLuaLockOnIndicatorLogic;
}

bool USKLockOnIndicatorComponent::IsUsingLuaLockOnIndicatorLogic() const
{
    return bUseLuaLockOnIndicatorLogic;
}

void USKLockOnIndicatorComponent::SetLuaLockOnIndicatorModuleName(const FString& ModuleName)
{
    LuaLockOnIndicatorModuleName = ModuleName;
}

FString USKLockOnIndicatorComponent::GetLuaLockOnIndicatorModuleName() const
{
    return LuaLockOnIndicatorModuleName;
}

FString USKLockOnIndicatorComponent::GetModuleName_Implementation() const
{
    return LuaLockOnIndicatorModuleName;
}

void USKLockOnIndicatorComponent::RefreshCachedLockOnComponents()
{
    RefreshCachedComponents();
}

void USKLockOnIndicatorComponent::ValidateLockTargetForScript()
{
    if (CameraManager)
    {
        CameraManager->ValidateLockTargetForScript();
    }
}

bool USKLockOnIndicatorComponent::HasOwnerCharacter() const
{
    return OwnerCharacter != nullptr;
}

bool USKLockOnIndicatorComponent::IsLocalPlayerControlled() const
{
    return OwnerCharacter && OwnerCharacter->IsLocallyControlled();
}

bool USKLockOnIndicatorComponent::IsLockedOn() const
{
    return CameraManager && CameraManager->IsLockedOn();
}

AActor* USKLockOnIndicatorComponent::GetLockTarget() const
{
    return CameraManager ? CameraManager->GetLockTarget() : nullptr;
}

float USKLockOnIndicatorComponent::GetLockTargetDistance() const
{
    const AActor* LockTarget = GetLockTarget();
    if (!OwnerCharacter || !LockTarget) return 0.0f;

    return FVector::Dist(OwnerCharacter->GetActorLocation(), LockTarget->GetActorLocation());
}

bool USKLockOnIndicatorComponent::UpdateLockTargetScreenPositionForScript(float TargetHeightOffset)
{
    RefreshCachedComponents();
    if (!PlayerController || !IsLockedOn()) return false;

    const FVector AnchorLocation = GetLockTargetAnchorLocation(TargetHeightOffset);
    FVector2D ScreenPosition = FVector2D::ZeroVector;
    const bool bProjected = PlayerController->ProjectWorldLocationToScreen(AnchorLocation, ScreenPosition, true);
    if (!bProjected) return false;

    CachedLockTargetScreenPosition = ScreenPosition;
    return IsScreenPositionInViewport(ScreenPosition);
}

FVector2D USKLockOnIndicatorComponent::GetCachedLockTargetScreenPosition() const
{
    return CachedLockTargetScreenPosition;
}

float USKLockOnIndicatorComponent::GetCachedLockTargetScreenX() const
{
    return CachedLockTargetScreenPosition.X;
}

float USKLockOnIndicatorComponent::GetCachedLockTargetScreenY() const
{
    return CachedLockTargetScreenPosition.Y;
}

bool USKLockOnIndicatorComponent::EnsureLockOnIndicatorWidget()
{
    if (IndicatorWidget) return true;

    RefreshCachedComponents();
    if (!PlayerController) return false;

    TSubclassOf<USKLockOnIndicatorWidget> WidgetClass = IndicatorWidgetClass;
    if (!WidgetClass)
    {
        WidgetClass = USKLockOnIndicatorWidget::StaticClass();
    }

    IndicatorWidget = CreateWidget<USKLockOnIndicatorWidget>(PlayerController, WidgetClass);
    if (!IndicatorWidget) return false;

    IndicatorWidget->SetIndicatorStyle(IndicatorSize, IndicatorThickness, IndicatorColor);
    IndicatorWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
    IndicatorWidget->SetDesiredSizeInViewport(FVector2D(IndicatorSize, IndicatorSize));
    IndicatorWidget->SetVisibility(ESlateVisibility::Hidden);
    IndicatorWidget->AddToViewport(IndicatorZOrder);
    return true;
}

bool USKLockOnIndicatorComponent::HasLockOnIndicatorWidget() const
{
    return IndicatorWidget != nullptr;
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorVisible(bool bVisible)
{
    if (bVisible && !EnsureLockOnIndicatorWidget()) return;
    if (!IndicatorWidget) return;

    IndicatorWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorScreenPositionXY(float ScreenX, float ScreenY)
{
    if (!EnsureLockOnIndicatorWidget()) return;

    CachedLockTargetScreenPosition = FVector2D(ScreenX, ScreenY);
    IndicatorWidget->SetPositionInViewport(CachedLockTargetScreenPosition, true);
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorSize(float NewSize)
{
    IndicatorSize = FMath::Max(8.0f, NewSize);
    if (!IndicatorWidget) return;

    IndicatorWidget->SetIndicatorStyle(IndicatorSize, IndicatorThickness, IndicatorColor);
    IndicatorWidget->SetDesiredSizeInViewport(FVector2D(IndicatorSize, IndicatorSize));
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorScale(float NewScale)
{
    if (!EnsureLockOnIndicatorWidget()) return;

    const float ClampedScale = FMath::Clamp(NewScale, 0.1f, 4.0f);
    IndicatorWidget->SetRenderScale(FVector2D(ClampedScale, ClampedScale));
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorOpacity(float NewOpacity)
{
    if (!EnsureLockOnIndicatorWidget()) return;

    IndicatorWidget->SetRenderOpacity(FMath::Clamp(NewOpacity, 0.0f, 1.0f));
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorThickness(float NewThickness)
{
    IndicatorThickness = FMath::Max(1.0f, NewThickness);
    if (!IndicatorWidget) return;

    IndicatorWidget->SetIndicatorStyle(IndicatorSize, IndicatorThickness, IndicatorColor);
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorColor(FLinearColor NewColor)
{
    IndicatorColor = NewColor;
    if (!IndicatorWidget) return;

    IndicatorWidget->SetIndicatorStyle(IndicatorSize, IndicatorThickness, IndicatorColor);
}

void USKLockOnIndicatorComponent::SetLockOnIndicatorColorRGBA(float Red, float Green, float Blue, float Alpha)
{
    SetLockOnIndicatorColor(FLinearColor(Red, Green, Blue, Alpha));
}

void USKLockOnIndicatorComponent::RemoveLockOnIndicatorWidget()
{
    if (!IndicatorWidget) return;

    IndicatorWidget->RemoveFromParent();
    IndicatorWidget = nullptr;
}

/**
 * 缓存锁定 UI 的角色、控制器与相机依赖，并为原生 UnLua 组件补发一次标准
 * ReceiveBeginPlay 生命周期。蓝图生成类和非原生类沿用引擎派发，纯原生类才在缓存完成后补发，
 * 因此 Lua 的运行期初始化不会重复且可安全访问锁定目标相关接口。
 * 本函数只在游戏线程执行，不创建锁定点控件，也不直接调用 Lua Initialize。
 */
void USKLockOnIndicatorComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();

    RefreshCachedComponents();

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

void USKLockOnIndicatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveLockOnIndicatorWidget();

    Super::EndPlay(EndPlayReason);
}

void USKLockOnIndicatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    RefreshCachedComponents();
    if (TryCallLuaLockOnIndicatorTick(DeltaTime)) return;

    SetLockOnIndicatorVisible(false);
}

bool USKLockOnIndicatorComponent::TryCallLuaLockOnIndicatorTick(float DeltaTime)
{
    const FString ModuleName = ResolveLuaLockOnIndicatorModuleName();
    if (!bUseLuaLockOnIndicatorLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKLockOnIndicatorLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
    const bool bHandled = ReadSKLockOnIndicatorLuaHandled(FunctionReturnValues, ModuleName);
    FunctionReturnValues.Pop();
    return bHandled;
}

FString USKLockOnIndicatorComponent::ResolveLuaLockOnIndicatorModuleName() const
{
    if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USKLockOnIndicatorComponent*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaLockOnIndicatorModuleName;
}

void USKLockOnIndicatorComponent::RefreshCachedComponents()
{
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(GetOwner());
    }
    if (!OwnerCharacter) return;

    if (!PlayerController)
    {
        PlayerController = Cast<APlayerController>(OwnerCharacter->GetController());
    }
    if (!CameraManager)
    {
        CameraManager = OwnerCharacter->FindComponentByClass<USKCameraManagerComponent>();
    }
}

FVector USKLockOnIndicatorComponent::GetLockTargetAnchorLocation(float TargetHeightOffset) const
{
    const AActor* LockTarget = GetLockTarget();
    if (!LockTarget) return FVector::ZeroVector;

    FVector BoundsOrigin = FVector::ZeroVector;
    FVector BoundsExtent = FVector::ZeroVector;
    LockTarget->GetActorBounds(true, BoundsOrigin, BoundsExtent);
    return BoundsOrigin + FVector(0.0f, 0.0f, BoundsExtent.Z * 0.18f + TargetHeightOffset);
}

bool USKLockOnIndicatorComponent::IsScreenPositionInViewport(const FVector2D& ScreenPosition) const
{
    if (!PlayerController) return false;

    int32 ViewportSizeX = 0;
    int32 ViewportSizeY = 0;
    PlayerController->GetViewportSize(ViewportSizeX, ViewportSizeY);
    if (ViewportSizeX <= 0 || ViewportSizeY <= 0) return true;

    return ScreenPosition.X >= -ScreenCullPadding
        && ScreenPosition.Y >= -ScreenCullPadding
        && ScreenPosition.X <= static_cast<float>(ViewportSizeX) + ScreenCullPadding
        && ScreenPosition.Y <= static_cast<float>(ViewportSizeY) + ScreenCullPadding;
}
