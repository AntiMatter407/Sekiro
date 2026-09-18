#include "UI/SKLockOnIndicatorWidget.h"

#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"

namespace
{
    void SKAppendCirclePoints(TArray<FVector2D>& OutPoints, const FVector2D& Center, float Radius, int32 SegmentCount)
    {
        OutPoints.Reset();
        const int32 ClampedSegmentCount = FMath::Max(8, SegmentCount);
        for (int32 SegmentIndex = 0; SegmentIndex <= ClampedSegmentCount; ++SegmentIndex)
        {
            const float Angle = 2.0f * PI * static_cast<float>(SegmentIndex) / static_cast<float>(ClampedSegmentCount);
            OutPoints.Add(Center + FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
        }
    }

    void SKDrawLine(
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FGeometry& Geometry,
        const FVector2D& Start,
        const FVector2D& End,
        const FLinearColor& Color,
        float Thickness)
    {
        TArray<FVector2D> Points;
        Points.Add(Start);
        Points.Add(End);
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId,
            Geometry.ToPaintGeometry(),
            Points,
            ESlateDrawEffect::None,
            Color,
            true,
            Thickness);
    }

    void SKDrawCircle(
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FGeometry& Geometry,
        const FVector2D& Center,
        float Radius,
        const FLinearColor& Color,
        float Thickness)
    {
        TArray<FVector2D> Points;
        SKAppendCirclePoints(Points, Center, Radius, 40);
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId,
            Geometry.ToPaintGeometry(),
            Points,
            ESlateDrawEffect::None,
            Color,
            true,
            Thickness);
    }
}

USKLockOnIndicatorWidget::USKLockOnIndicatorWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void USKLockOnIndicatorWidget::SetIndicatorStyle(float NewSize, float NewThickness, FLinearColor NewColor)
{
    IndicatorSize = FMath::Max(8.0f, NewSize);
    IndicatorThickness = FMath::Max(1.0f, NewThickness);
    IndicatorColor = NewColor;
}

/**
 * 游戏线程设置已导入原纹理与有效图集区域，不加载资产或猜测 UV。
 * @param Texture 可空纹理，空值清除正式显示。
 * @param UVMin 归一化左上坐标。
 * @param UVMax 归一化右下坐标，必须构成零到一内的正面积。
 * @return 非空纹理且 UV 合法为 true；无效输入清除旧纹理并返回 false。
 */
bool USKLockOnIndicatorWidget::SetIndicatorTexture(UTexture2D* Texture, FVector2D UVMin, FVector2D UVMax)
{
    if (!IsInGameThread()) return false;
    IndicatorTexture = nullptr;
    const bool bValid = IsValid(Texture) && FMath::IsFinite(UVMin.X) && FMath::IsFinite(UVMin.Y)
        && FMath::IsFinite(UVMax.X) && FMath::IsFinite(UVMax.Y)
        && UVMin.X >= 0.0 && UVMin.Y >= 0.0 && UVMax.X <= 1.0 && UVMax.Y <= 1.0
        && UVMax.X > UVMin.X && UVMax.Y > UVMin.Y;
    if (bValid)
    {
        IndicatorTexture = Texture;
        TextureUVMin = UVMin;
        TextureUVMax = UVMax;
    }
    InvalidateLayoutAndVolatility();
    return bValid;
}

/**
 * 游戏线程显式启停旧程序圆环，不影响纹理路径或目标算法。
 * @param bEnabled true 仅用于调试；默认 false，正式纹理存在时始终只绘制纹理。
 */
void USKLockOnIndicatorWidget::SetDebugDrawingEnabled(bool bEnabled)
{
    if (!IsInGameThread()) return;
    bDebugDrawingEnabled = bEnabled;
    InvalidateLayoutAndVolatility();
}

/**
 * Slate 绘制阶段优先显示原图，缺图且未显式启用调试时保持空白。
 * @param Args Slate 本帧绘制上下文，不保存引用。
 * @param AllottedGeometry 指示器的逻辑空间。
 * @param MyCullingRect 父裁切矩形，透传父类。
 * @param OutDrawElements 输出绘制命令。
 * @param LayerId 初始层号。
 * @param InWidgetStyle 父样式调制。
 * @param bParentEnabled 父可用状态。
 * @return 最后使用层号，不会同时叠加原图和旧圆环。
 */
int32 USKLockOnIndicatorWidget::NativePaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    const int32 BaseLayerId = Super::NativePaint(
        Args,
        AllottedGeometry,
        MyCullingRect,
        OutDrawElements,
        LayerId,
        InWidgetStyle,
        bParentEnabled);

    if (IsValid(IndicatorTexture))
    {
        FSlateBrush Brush;
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.SetResourceObject(IndicatorTexture.Get());
        Brush.SetUVRegion(FBox2d(TextureUVMin, TextureUVMax));
        FSlateDrawElement::MakeBox(OutDrawElements, BaseLayerId + 1, AllottedGeometry.ToPaintGeometry(),
            &Brush, ESlateDrawEffect::None, IndicatorColor * InWidgetStyle.GetColorAndOpacityTint());
        return BaseLayerId + 1;
    }
    if (!bDebugDrawingEnabled) return BaseLayerId;
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
    const float DrawSize = FMath::Max(8.0f, FMath::Min(FMath::Min(LocalSize.X, LocalSize.Y), IndicatorSize));
    const FVector2D Center = LocalSize * 0.5f;
    const float Radius = DrawSize * 0.26f;
    const float TickOuter = DrawSize * 0.46f;
    const float TickInner = DrawSize * 0.34f;
    const float DotRadius = DrawSize * 0.055f;
    const float Thickness = FMath::Max(1.0f, IndicatorThickness);
    const FLinearColor ShadowColor(0.0f, 0.0f, 0.0f, IndicatorColor.A * 0.55f);
    const int32 ShadowLayerId = BaseLayerId + 1;
    const int32 MainLayerId = BaseLayerId + 2;

    SKDrawCircle(OutDrawElements, ShadowLayerId, AllottedGeometry, Center, Radius, ShadowColor, Thickness + 1.5f);
    SKDrawCircle(OutDrawElements, MainLayerId, AllottedGeometry, Center, Radius, IndicatorColor, Thickness);

    SKDrawCircle(OutDrawElements, ShadowLayerId, AllottedGeometry, Center, DotRadius, ShadowColor, Thickness + 2.0f);
    SKDrawCircle(OutDrawElements, MainLayerId, AllottedGeometry, Center, DotRadius, IndicatorColor, Thickness + 1.0f);

    SKDrawLine(OutDrawElements, ShadowLayerId, AllottedGeometry, Center + FVector2D(0.0f, -TickOuter), Center + FVector2D(0.0f, -TickInner), ShadowColor, Thickness + 1.5f);
    SKDrawLine(OutDrawElements, ShadowLayerId, AllottedGeometry, Center + FVector2D(0.0f, TickOuter), Center + FVector2D(0.0f, TickInner), ShadowColor, Thickness + 1.5f);
    SKDrawLine(OutDrawElements, ShadowLayerId, AllottedGeometry, Center + FVector2D(-TickOuter, 0.0f), Center + FVector2D(-TickInner, 0.0f), ShadowColor, Thickness + 1.5f);
    SKDrawLine(OutDrawElements, ShadowLayerId, AllottedGeometry, Center + FVector2D(TickOuter, 0.0f), Center + FVector2D(TickInner, 0.0f), ShadowColor, Thickness + 1.5f);

    SKDrawLine(OutDrawElements, MainLayerId, AllottedGeometry, Center + FVector2D(0.0f, -TickOuter), Center + FVector2D(0.0f, -TickInner), IndicatorColor, Thickness);
    SKDrawLine(OutDrawElements, MainLayerId, AllottedGeometry, Center + FVector2D(0.0f, TickOuter), Center + FVector2D(0.0f, TickInner), IndicatorColor, Thickness);
    SKDrawLine(OutDrawElements, MainLayerId, AllottedGeometry, Center + FVector2D(-TickOuter, 0.0f), Center + FVector2D(-TickInner, 0.0f), IndicatorColor, Thickness);
    SKDrawLine(OutDrawElements, MainLayerId, AllottedGeometry, Center + FVector2D(TickOuter, 0.0f), Center + FVector2D(TickInner, 0.0f), IndicatorColor, Thickness);

    return MainLayerId;
}
