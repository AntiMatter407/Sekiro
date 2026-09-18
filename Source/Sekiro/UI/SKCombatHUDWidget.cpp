#include "UI/SKCombatHUDWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

/** 游戏线程返回可配置的UnLua绑定模块名，不创建额外Lua代理对象。 */
FString USKCombatHUDWidget::GetModuleName_Implementation() const
{
    return LuaModuleName;
}

/**
 * 游戏线程设置已核验的原版逻辑画布尺寸，不假定固定分辨率。
 * @param NewReferenceSize 有限且两轴为正的原版逻辑尺寸。
 * @return 配置有效返回 true；非法输入保留原配置并返回 false。
 */
bool USKCombatHUDWidget::SetReferenceSize(FVector2D NewReferenceSize)
{
    if (!IsInGameThread() || !FMath::IsFinite(NewReferenceSize.X) || !FMath::IsFinite(NewReferenceSize.Y)
        || NewReferenceSize.X <= 0.0 || NewReferenceSize.Y <= 0.0) return false;
    ReferenceSize = NewReferenceSize;
    InvalidateLayoutAndVolatility();
    return true;
}

/**
 * 游戏线程添加或替换原纹理图层，控件持有纹理引用直至清理，不加载资产路径。
 * @param Sprite 完整图层描述；尺寸为正，UV 位于零到一且具有正面积，颜色和坐标必须有限。
 * @return 图层通过校验并保存返回 true；缺图/非法布局返回 false，不产生程序图形回退。
 */
bool USKCombatHUDWidget::AddHUDSprite(const FSKCombatHUDSprite& Sprite)
{
    if (!IsInGameThread() || Sprite.Id.IsNone() || Sprite.Group.IsNone() || !IsValid(Sprite.Texture)
        || static_cast<uint8>(Sprite.Fill) > static_cast<uint8>(ESKHUDFillMode::CenterOut)
        || !FMath::IsFinite(Sprite.Position.X) || !FMath::IsFinite(Sprite.Position.Y)
        || !FMath::IsFinite(Sprite.Size.X) || !FMath::IsFinite(Sprite.Size.Y)
        || Sprite.Size.X <= 0.0 || Sprite.Size.Y <= 0.0
        || !FMath::IsFinite(Sprite.Anchor.X) || !FMath::IsFinite(Sprite.Anchor.Y)
        || Sprite.Anchor.X < 0.0 || Sprite.Anchor.X > 1.0 || Sprite.Anchor.Y < 0.0 || Sprite.Anchor.Y > 1.0
        || !FMath::IsFinite(Sprite.UVMin.X) || !FMath::IsFinite(Sprite.UVMin.Y)
        || !FMath::IsFinite(Sprite.UVMax.X) || !FMath::IsFinite(Sprite.UVMax.Y)
        || Sprite.UVMin.X < 0.0 || Sprite.UVMin.Y < 0.0 || Sprite.UVMax.X > 1.0 || Sprite.UVMax.Y > 1.0
        || Sprite.UVMax.X <= Sprite.UVMin.X || Sprite.UVMax.Y <= Sprite.UVMin.Y
        || !FMath::IsFinite(Sprite.Tint.R) || !FMath::IsFinite(Sprite.Tint.G)
        || !FMath::IsFinite(Sprite.Tint.B) || !FMath::IsFinite(Sprite.Tint.A)) return false;
    for (FSKCombatHUDSprite& Existing : Sprites)
    {
        if (Existing.Id == Sprite.Id)
        {
            Existing = Sprite;
            InvalidateLayoutAndVolatility();
            return true;
        }
    }
    Sprites.Add(Sprite);
    Groups.FindOrAdd(Sprite.Group);
    InvalidateLayoutAndVolatility();
    return true;
}

/** 游戏线程释放所有图层纹理引用和显示状态，不触及角色或输入模式。 */
void USKCombatHUDWidget::ClearHUDSprites()
{
    if (!IsInGameThread()) return;
    Sprites.Reset();
    Groups.Reset();
    InvalidateLayoutAndVolatility();
}

/**
 * 游戏线程更新已提交快照对应的显示组；仅保存显示比例，不持有生命权威。
 * @param Group 非空组名，与图层配置相同。
 * @param bVisible 只有快照有效时才传 true。
 * @param Ratio 有限显示比例，夹取零到一；非法值自动隐藏该组。
 * @param bBroken Survival 提供的实际崩溃状态，不由比例反推。
 * @return 合法组名和比例返回 true；非法比例返回 false 并隐藏。
 */
bool USKCombatHUDWidget::SetHUDGroupState(FName Group, bool bVisible, float Ratio, bool bBroken)
{
    if (!IsInGameThread() || Group.IsNone()) return false;
    FSKCombatHUDGroupState& State = Groups.FindOrAdd(Group);
    const bool bValid = FMath::IsFinite(Ratio);
    State.bVisible = bVisible && bValid;
    State.Ratio = bValid ? FMath::Clamp(Ratio, 0.f, 1.f) : 0.f;
    State.bBroken = bBroken;
    InvalidateLayoutAndVolatility();
    return bValid;
}

/**
 * 游戏线程提交当前目标的玩家视口相对像素；绘制时仅除一次视口 DPI。
 * @param Group 非空目标组，调用后此组不再按固定参考画布定位。
 * @param ScreenPositionPixels ProjectWorldLocationToScreen(..., true) 的有限像素结果。
 * @param bVisible 投影、屏幕范围和目标有效性均通过时为 true；失败时可传零坐标。
 */
void USKCombatHUDWidget::SetHUDGroupScreenPosition(
    FName Group, FVector2D ScreenPositionPixels, bool bVisible)
{
    if (!IsInGameThread() || Group.IsNone()) return;
    FSKCombatHUDGroupState& State = Groups.FindOrAdd(Group);
    State.bScreenPositioned = true;
    State.bProjectionVisible = bVisible && FMath::IsFinite(ScreenPositionPixels.X) && FMath::IsFinite(ScreenPositionPixels.Y);
    State.ScreenPosition = State.bProjectionVisible ? ScreenPositionPixels : FVector2D::ZeroVector;
    InvalidateLayoutAndVolatility();
}

/**
 * 游戏线程投影显式目标的包围盒顶部锚点，不搜索敌人、不建立锁定。
 * @param Actor 有效展示目标，只在调用期间读取。
 * @param HeightOffset 相对包围盒顶部的有限世界Z偏移，单位厘米。
 * @param OutScreenPosition 成功输出玩家视口相对像素；失败输出零。
 * @return 目标有效、投影成功且位于当前玩家视口内返回 true。
 */
bool USKCombatHUDWidget::ProjectActorAnchor(AActor* Actor, float HeightOffset, FVector2D& OutScreenPosition) const
{
    OutScreenPosition = FVector2D::ZeroVector;
    if (!IsInGameThread() || !IsValid(Actor) || !FMath::IsFinite(HeightOffset)) return false;
    APlayerController* Controller = GetOwningPlayer();
    if (!Controller) return false;
    FVector Origin;
    FVector Extent;
    Actor->GetActorBounds(true, Origin, Extent);
    const FVector Anchor = Origin + FVector(0.0, 0.0, Extent.Z + HeightOffset);
    FVector2D Screen;
    if (!Controller->ProjectWorldLocationToScreen(Anchor, Screen, true)) return false;
    int32 Width = 0;
    int32 Height = 0;
    Controller->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0 || Screen.X < 0.0 || Screen.Y < 0.0 || Screen.X > Width || Screen.Y > Height) return false;
    OutScreenPosition = Screen;
    return true;
}

/**
 * 游戏线程/Slate 绘制阶段按参考画布等比缩放原图，随比例同步裁切目标矩形与源 UV。
 * 中心填充左右对称扩展，不把完整纹理压缩进剩余长度；宽屏锚点保持相对屏幕边缘的位置。
 * @param Args Slate 本帧绘制上下文，不保存引用。
 * @param AllottedGeometry 控件的 UMG 逻辑空间几何；固定布局不额外重复应用 DPI。
 * @param MyCullingRect 父裁切区域，透传父类。
 * @param OutDrawElements 输出 Slate 绘制命令。
 * @param LayerId 初始绘制层。
 * @param InWidgetStyle 父级颜色/透明度调制。
 * @param bParentEnabled 父级可用状态，透传父类。
 * @return 本控件最后使用的绘制层；缺配置时仅返回父类结果。
 */
int32 USKCombatHUDWidget::NativePaint(
    const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    if (ReferenceSize.X <= 0.0 || ReferenceSize.Y <= 0.0) return Layer;
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
    const double Scale = FMath::Min(LocalSize.X / ReferenceSize.X, LocalSize.Y / ReferenceSize.Y);
    if (!FMath::IsFinite(Scale) || Scale <= 0.0) return Layer;
    const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
    for (const FSKCombatHUDSprite& Sprite : Sprites)
    {
        const FSKCombatHUDGroupState* State = Groups.Find(Sprite.Group);
        if (!State || !State->bVisible || !IsValid(Sprite.Texture)
            || (Sprite.bBrokenOnly && !State->bBroken) || (State->bScreenPositioned && !State->bProjectionVisible)) continue;
        FVector2D Position;
        if (State->bScreenPositioned)
        {
            if (!FMath::IsFinite(DPIScale) || DPIScale <= 0.f) continue;
            Position = State->ScreenPosition / DPIScale + Sprite.Position * Scale;
        }
        else
        {
            Position = Sprite.Anchor * LocalSize + (Sprite.Position - Sprite.Anchor * ReferenceSize) * Scale;
        }
        FVector2D Size = Sprite.Size * Scale;
        double Left = 0.0;
        double Right = 1.0;
        if (Sprite.Fill == ESKHUDFillMode::LeftToRight) Right = State->Ratio;
        else if (Sprite.Fill == ESKHUDFillMode::RightToLeft) Left = 1.0 - State->Ratio;
        else if (Sprite.Fill == ESKHUDFillMode::CenterOut)
        {
            Left = (1.0 - State->Ratio) * 0.5;
            Right = 1.0 - Left;
        }
        if (Right <= Left) continue;
        Position.X += Size.X * Left;
        Size.X *= Right - Left;
        const FVector2D UVSize = Sprite.UVMax - Sprite.UVMin;
        const double UVLeft = Sprite.bMirrorX ? 1.0 - Right : Left;
        const double UVRight = Sprite.bMirrorX ? 1.0 - Left : Right;
        const FVector2D UVMin(Sprite.UVMin.X + UVSize.X * UVLeft, Sprite.UVMin.Y);
        const FVector2D UVMax(Sprite.UVMin.X + UVSize.X * UVRight, Sprite.UVMax.Y);
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.Mirroring = Sprite.bMirrorX ? ESlateBrushMirrorType::Horizontal : ESlateBrushMirrorType::NoMirror;
        Brush.SetResourceObject(Sprite.Texture.Get());
        Brush.SetUVRegion(FBox2d(UVMin, UVMax));
        Brush.ImageSize = FVector2f(Sprite.Size);
        FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
            AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Position))),
            &Brush, ESlateDrawEffect::None, Sprite.Tint * InWidgetStyle.GetColorAndOpacityTint());
    }
    return Layer;
}
