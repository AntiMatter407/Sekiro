#include "UI/SKLockOnIndicatorWidget.h"

#include "Rendering/DrawElements.h"

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
