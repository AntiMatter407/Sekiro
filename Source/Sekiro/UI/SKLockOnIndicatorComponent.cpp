#include "UI/SKLockOnIndicatorComponent.h"

#include "Camera/SKCameraManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "UI/SKLockOnIndicatorWidget.h"
#include "Engine/Texture2D.h"
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

/**
 * 提供未绑定 Lua 时的锁定指示器 Tick 回退，并主动隐藏可能残留的指示器。
 * 同名 Lua override 返回 true 时表示已完成本帧显示计算；仅由组件 Tick 在游戏线程调用。
 *
 * @param DeltaTime 当前帧步长，单位秒；默认实现不消费该值。
 * @return 默认返回 false，使调用方执行安全隐藏。
 */
bool USKLockOnIndicatorComponent::HandleLockOnIndicatorTick_Implementation(float DeltaTime)
{
    (void)DeltaTime;
    return false;
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

/**
 * 游戏线程读取当前锁定目标的动画骨骼位置并投影，不更改相机锁定或动画姿态。
 * @param TargetBoneName Lua 配置的现有骨骼名；None 或目标缺少该骨骼时失败，不使用包围盒或 Actor 原点兜底。
 * @return 骨骼有效且投影位于视口允许范围内时返回 true 并更新缓存；失败时由 Lua 隐藏锁定标记。
 */
bool USKLockOnIndicatorComponent::UpdateLockTargetScreenPositionForScript(FName TargetBoneName)
{
    if (!IsInGameThread()) return false;
    RefreshCachedComponents();
    if (!PlayerController || !IsLockedOn()) return false;

    FVector AnchorLocation = FVector::ZeroVector;
    if (!GetLockTargetAnchorLocation(TargetBoneName, AnchorLocation)) return false;
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
    IndicatorWidget->SetIndicatorTexture(IndicatorTexture.Get(), IndicatorUVMin, IndicatorUVMax);
    IndicatorWidget->SetDebugDrawingEnabled(bIndicatorDebugDrawing);
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

/**
 * 游戏线程注入正式锁定纹理，不选择资源路径、不修改目标或投影算法。
 * @param Texture 可空已导入纹理；无效时清除旧图，交由Lua隐藏并诊断。
 * @param UVMin 图集左上归一化坐标。
 * @param UVMax 图集右下归一化坐标，必须构成有效正面积。
 * @return 纹理与UV合法返回 true；可以在Widget创建前配置。
 */
bool USKLockOnIndicatorComponent::SetLockOnIndicatorTexture(UTexture2D* Texture, FVector2D UVMin, FVector2D UVMax)
{
    if (!IsInGameThread()) return false;
    const bool bValid = IsValid(Texture) && FMath::IsFinite(UVMin.X) && FMath::IsFinite(UVMin.Y)
        && FMath::IsFinite(UVMax.X) && FMath::IsFinite(UVMax.Y) && UVMin.X >= 0.0 && UVMin.Y >= 0.0
        && UVMax.X <= 1.0 && UVMax.Y <= 1.0 && UVMax.X > UVMin.X && UVMax.Y > UVMin.Y;
    IndicatorTexture = bValid ? Texture : nullptr;
    IndicatorUVMin = UVMin;
    IndicatorUVMax = UVMax;
    if (IndicatorWidget) IndicatorWidget->SetIndicatorTexture(IndicatorTexture.Get(), UVMin, UVMax);
    return bValid;
}

/**
 * 游戏线程开启或关闭显式调试圆环，正式样式保持关闭。
 * @param bEnabled true 为人工请求的调试显示；不会覆盖已配置原纹理。
 */
void USKLockOnIndicatorComponent::SetLockOnIndicatorDebugDrawing(bool bEnabled)
{
    if (!IsInGameThread()) return;
    bIndicatorDebugDrawing = bEnabled;
    if (IndicatorWidget) IndicatorWidget->SetDebugDrawingEnabled(bEnabled);
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
    if (bUseLuaLockOnIndicatorLogic && HandleLockOnIndicatorTick(DeltaTime)) return;

    SetLockOnIndicatorVisible(false);
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

/**
 * 游戏线程从 Character 主网格读取已更新的骨骼世界位置，不创建 Socket，不猜测不同网格的腰部高度。
 * @param TargetBoneName 必须存在于主网格的骨骼名；仅有同名 Socket 而没有骨骼也不接受。
 * @param OutLocation 成功时输出世界坐标，单位厘米；失败时保持零，调用方不得继续投影。
 * @return 当前目标是有效 Character、骨骼存在且位置有限时返回 true。
 */
bool USKLockOnIndicatorComponent::GetLockTargetAnchorLocation(FName TargetBoneName, FVector& OutLocation) const
{
    OutLocation = FVector::ZeroVector;
    const ACharacter* LockTarget = Cast<ACharacter>(GetLockTarget());
    if (!IsValid(LockTarget) || TargetBoneName.IsNone()) return false;

    const USkeletalMeshComponent* TargetMesh = LockTarget->GetMesh();
    if (!IsValid(TargetMesh) || TargetMesh->GetBoneIndex(TargetBoneName) == INDEX_NONE) return false;

    const FVector BoneLocation = TargetMesh->GetBoneLocation(TargetBoneName, EBoneSpaces::WorldSpace);
    if (BoneLocation.ContainsNaN()) return false;
    OutLocation = BoneLocation;
    return true;
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
