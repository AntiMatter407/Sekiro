#include "LuaAnimSnapshotTimeline.h"

#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

/**
 * 初始化时间轴的数据引用与选择回调，并建立默认五秒视窗。
 * 必须在游戏线程的 Slate 构造阶段调用；InArgs.Frames 的生命周期必须覆盖本控件。
 */
void SLuaAnimSnapshotTimeline::Construct(const FArguments& InArgs)
{
    Frames = InArgs._Frames;
    OnSnapshotSelected = InArgs._OnSnapshotSelected;
    ResetView();
}

/**
 * 替换时间轴只读帧数组并重新适配全部数据，不复制帧。
 * 必须在游戏线程调用；InFrames 可为空，但非空指针必须在控件使用期间保持有效。
 */
void SLuaAnimSnapshotTimeline::SetFrames(
    const TArray<TSharedPtr<FLuaAnimSnapshotFrame>>* InFrames)
{
    Frames = InFrames;
    SelectedFrameIndex = INDEX_NONE;
    HoveredFrameIndex = INDEX_NONE;
    FitAll();
}

/**
 * 更新高亮帧数组下标，不触发选择回调。
 * 必须在游戏线程调用；越界值会清除高亮。
 */
void SLuaAnimSnapshotTimeline::SetSelectedFrameIndex(const int32 InFrameIndex)
{
    SelectedFrameIndex = Frames != nullptr && Frames->IsValidIndex(InFrameIndex)
        ? InFrameIndex
        : INDEX_NONE;
    Invalidate(EInvalidateWidgetReason::Paint);
}

/**
 * 把时间窗适配到全部快照并留出少量左右边距。
 * 必须在游戏线程调用；空数据恢复为 0 到 5 秒。
 */
void SLuaAnimSnapshotTimeline::FitAll()
{
    if (Frames == nullptr || Frames->IsEmpty())
    {
        ViewStartSeconds = 0.0;
        ViewEndSeconds = 5.0;
        Invalidate(EInvalidateWidgetReason::Paint);
        return;
    }

    const double FirstTime = (*Frames)[0]->SessionElapsedSeconds;
    const double LastTime = (*Frames)[Frames->Num() - 1]->SessionElapsedSeconds;
    const double DataSpan = FMath::Max(LastTime - FirstTime, 0.1);
    const double Padding = FMath::Max(DataSpan * 0.05, 0.05);
    ViewStartSeconds = FirstTime - Padding;
    ViewEndSeconds = LastTime + Padding;
    Invalidate(EInvalidateWidgetReason::Paint);
}

/**
 * 恢复五秒默认观察尺度，并从第一帧附近开始显示。
 * 必须在游戏线程调用；不会改变当前选中帧。
 */
void SLuaAnimSnapshotTimeline::ResetView()
{
    const double FirstTime = Frames != nullptr
        && !Frames->IsEmpty()
        && (*Frames)[0].IsValid()
        ? (*Frames)[0]->SessionElapsedSeconds
        : 0.0;
    ViewStartSeconds = FMath::Max(0.0, FirstTime - 0.25);
    ViewEndSeconds = ViewStartSeconds + 5.0;
    Invalidate(EInvalidateWidgetReason::Paint);
}

/** 返回时间轴推荐尺寸；无外部状态读取，可由 Slate 在任意布局阶段调用。 */
FVector2D SLuaAnimSnapshotTimeline::ComputeDesiredSize(
    const float LayoutScaleMultiplier) const
{
    return FVector2D(700.0f, 112.0f);
}

/**
 * 绘制时间刻度与事件点；超大数据按固定步长抽样，但状态变化和选中点始终绘制。
 * 由 Slate 绘制阶段调用，只读帧数组。返回最后使用的绘制层级。
 */
int32 SLuaAnimSnapshotTimeline::OnPaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    const int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    const bool bParentEnabled) const
{
    const FVector2D Size = AllottedGeometry.GetLocalSize();
    const FSlateBrush* BackgroundBrush = FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder"));
    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        BackgroundBrush,
        ESlateDrawEffect::None,
        FLinearColor(0.08f, 0.09f, 0.11f, 1.0f));

    const float AxisY = Size.Y - 26.0f;
    TArray<FVector2D> AxisPoints;
    AxisPoints.Add(FVector2D(8.0f, AxisY));
    AxisPoints.Add(FVector2D(FMath::Max(Size.X - 8.0f, 8.0f), AxisY));
    FSlateDrawElement::MakeLines(
        OutDrawElements,
        LayerId + 1,
        AllottedGeometry.ToPaintGeometry(),
        AxisPoints,
        ESlateDrawEffect::None,
        FLinearColor(0.35f, 0.38f, 0.42f, 1.0f),
        true,
        1.0f);

    const FSlateFontInfo SmallFont = FAppStyle::GetFontStyle(TEXT("SmallFont"));
    const int32 TickCount = FMath::Clamp(FMath::FloorToInt(Size.X / 120.0f), 2, 10);
    for (int32 TickIndex = 0; TickIndex <= TickCount; ++TickIndex)
    {
        const float Alpha = static_cast<float>(TickIndex) / static_cast<float>(TickCount);
        const float TickX = FMath::Lerp(8.0f, Size.X - 8.0f, Alpha);
        const double TickTime = FMath::Lerp(ViewStartSeconds, ViewEndSeconds, Alpha);
        TArray<FVector2D> TickPoints;
        TickPoints.Add(FVector2D(TickX, AxisY - 4.0f));
        TickPoints.Add(FVector2D(TickX, AxisY + 4.0f));
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId + 1,
            AllottedGeometry.ToPaintGeometry(),
            TickPoints,
            ESlateDrawEffect::None,
            FLinearColor(0.35f, 0.38f, 0.42f, 1.0f));
        FSlateDrawElement::MakeText(
            OutDrawElements,
            LayerId + 2,
            AllottedGeometry.ToPaintGeometry(
                FVector2f(50.0f, 16.0f),
                FSlateLayoutTransform(FVector2f(TickX - 20.0f, AxisY + 6.0f))),
            FString::Printf(TEXT("%.2fs"), TickTime),
            SmallFont,
            ESlateDrawEffect::None,
            FLinearColor(0.65f, 0.68f, 0.72f, 1.0f));
    }

    if (Frames == nullptr) return LayerId + 2;

    const int32 DrawStride = FMath::Max(1, FMath::CeilToInt(
        static_cast<double>(Frames->Num()) / 1600.0));
    const FSlateBrush* PointBrush = FAppStyle::GetBrush(TEXT("WhiteBrush"));
    for (int32 FrameArrayIndex = 0; FrameArrayIndex < Frames->Num(); ++FrameArrayIndex)
    {
        const TSharedPtr<FLuaAnimSnapshotFrame>& Frame = (*Frames)[FrameArrayIndex];
        if (!Frame.IsValid()) continue;
        const bool bStateChanged = Frame->CaptureReason.Equals(
            TEXT("StateChanged"),
            ESearchCase::IgnoreCase);
        const bool bSelected = FrameArrayIndex == SelectedFrameIndex;
        if (FrameArrayIndex % DrawStride != 0 && !bStateChanged && !bSelected) continue;

        const float PointX = TimeToScreenX(Frame->SessionElapsedSeconds, Size.X);
        if (PointX < -8.0f || PointX > Size.X + 8.0f) continue;

        FLinearColor PointColor(0.25f, 0.65f, 1.0f, 1.0f);
        if (Frame->CaptureReason.Equals(TEXT("Start"), ESearchCase::IgnoreCase))
        {
            PointColor = FLinearColor(0.35f, 0.9f, 0.45f, 1.0f);
        }
        else if (bStateChanged)
        {
            PointColor = FLinearColor(1.0f, 0.35f, 0.12f, 1.0f);
        }
        if (bSelected) PointColor = FLinearColor(1.0f, 0.9f, 0.12f, 1.0f);

        const float PointSize = bSelected ? 12.0f : (bStateChanged ? 9.0f : 6.0f);
        FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId + 3,
            AllottedGeometry.ToPaintGeometry(
                FVector2f(PointSize, PointSize),
                FSlateLayoutTransform(FVector2f(
                    PointX - PointSize * 0.5f,
                    AxisY - 18.0f - PointSize * 0.5f))),
            PointBrush,
            ESlateDrawEffect::None,
            PointColor);
    }
    return LayerId + 3;
}

/**
 * 左键立即选择最近真实快照，中键或右键进入平移模式并捕获鼠标。
 * 由 Slate 游戏线程输入分发调用；返回事件是否已消费。
 */
FReply SLuaAnimSnapshotTimeline::OnMouseButtonDown(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton
        || MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bPanning = true;
        LastPointerScreenPosition = MouseEvent.GetScreenSpacePosition();
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(
            MouseEvent.GetScreenSpacePosition());
        const int32 NearestIndex = FindNearestFrame(
            LocalPosition.X,
            MyGeometry.GetLocalSize().X);
        if (NearestIndex != INDEX_NONE)
        {
            SelectedFrameIndex = NearestIndex;
            OnSnapshotSelected.ExecuteIfBound(NearestIndex);
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

/**
 * 结束中键或右键平移并释放鼠标捕获。
 * 由 Slate 游戏线程输入分发调用；非平移按键返回未处理。
 */
FReply SLuaAnimSnapshotTimeline::OnMouseButtonUp(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    if (bPanning
        && (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton
            || MouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
    {
        bPanning = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}

/**
 * 平移时按像素移动时间窗；普通悬停时查找最近真实快照并刷新中文提示。
 * 由 Slate 游戏线程输入分发调用；平移期间消费事件，否则允许父控件继续处理。
 */
FReply SLuaAnimSnapshotTimeline::OnMouseMove(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    if (bPanning && HasMouseCapture())
    {
        const FVector2D CurrentPosition = MouseEvent.GetScreenSpacePosition();
        const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.0f);
        const double SecondsPerPixel = (ViewEndSeconds - ViewStartSeconds) / Width;
        const double ShiftSeconds =
            -(CurrentPosition.X - LastPointerScreenPosition.X) * SecondsPerPixel;
        ViewStartSeconds += ShiftSeconds;
        ViewEndSeconds += ShiftSeconds;
        LastPointerScreenPosition = CurrentPosition;
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }

    const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(
        MouseEvent.GetScreenSpacePosition());
    HoveredFrameIndex = FindNearestFrame(LocalPosition.X, MyGeometry.GetLocalSize().X);
    SetToolTipText(MakeFrameTooltip(HoveredFrameIndex));
    return FReply::Unhandled();
}

/**
 * 以鼠标所在时间为锚点缩放时间窗，最小跨度 0.01 秒、最大跨度一天。
 * 由 Slate 游戏线程输入分发调用；始终消费滚轮以避免外层列表滚动。
 */
FReply SLuaAnimSnapshotTimeline::OnMouseWheel(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.0f);
    const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(
        MouseEvent.GetScreenSpacePosition());
    const double AnchorTime = ScreenXToTime(LocalPosition.X, Width);
    const double OldSpan = FMath::Max(ViewEndSeconds - ViewStartSeconds, 0.01);
    const double ZoomFactor = FMath::Pow(0.8, MouseEvent.GetWheelDelta());
    const double NewSpan = FMath::Clamp(OldSpan * ZoomFactor, 0.01, 86400.0);
    const double AnchorRatio = FMath::Clamp(
        static_cast<double>(LocalPosition.X / Width),
        0.0,
        1.0);
    ViewStartSeconds = AnchorTime - NewSpan * AnchorRatio;
    ViewEndSeconds = ViewStartSeconds + NewSpan;
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}

/**
 * 把局部 X 坐标映射到当前时间窗秒数。
 * 可在 Slate 线程只读调用；Width 小于等于零时按 1 处理。返回对应会话相对秒。
 */
double SLuaAnimSnapshotTimeline::ScreenXToTime(
    const float LocalX,
    const float Width) const
{
    const double Alpha = static_cast<double>(LocalX / FMath::Max(Width, 1.0f));
    return FMath::Lerp(ViewStartSeconds, ViewEndSeconds, Alpha);
}

/**
 * 把会话相对秒映射到局部 X 坐标。
 * 可在 Slate 绘制阶段调用；Width 是可绘制宽度。返回值可能位于可视区域外。
 */
float SLuaAnimSnapshotTimeline::TimeToScreenX(
    const double TimeSeconds,
    const float Width) const
{
    const double Span = FMath::Max(ViewEndSeconds - ViewStartSeconds, 0.000001);
    return static_cast<float>((TimeSeconds - ViewStartSeconds) / Span) * Width;
}

/**
 * 在全部真实帧中寻找与鼠标 X 对应时间最近的一帧，不受绘制抽样影响。
 * 可在游戏线程输入阶段调用；LocalX 与 Width 使用 Slate 局部像素。无帧返回 INDEX_NONE。
 */
int32 SLuaAnimSnapshotTimeline::FindNearestFrame(
    const float LocalX,
    const float Width) const
{
    if (Frames == nullptr || Frames->IsEmpty()) return INDEX_NONE;
    const double TargetTime = ScreenXToTime(LocalX, Width);
    int32 BestIndex = INDEX_NONE;
    double BestDistance = TNumericLimits<double>::Max();
    for (int32 FrameArrayIndex = 0; FrameArrayIndex < Frames->Num(); ++FrameArrayIndex)
    {
        const TSharedPtr<FLuaAnimSnapshotFrame>& Frame = (*Frames)[FrameArrayIndex];
        if (!Frame.IsValid()) continue;
        const double Distance = FMath::Abs(Frame->SessionElapsedSeconds - TargetTime);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestIndex = FrameArrayIndex;
        }
    }
    return BestIndex;
}

/**
 * 生成悬停帧的中文摘要，供 Slate 原生 Tooltip 显示。
 * 可在游戏线程调用；FrameArrayIndex 越界时返回空文本。
 */
FText SLuaAnimSnapshotTimeline::MakeFrameTooltip(const int32 FrameArrayIndex) const
{
    if (Frames == nullptr || !Frames->IsValidIndex(FrameArrayIndex)) return FText::GetEmpty();
    const TSharedPtr<FLuaAnimSnapshotFrame>& Frame = (*Frames)[FrameArrayIndex];
    if (!Frame.IsValid()) return FText::GetEmpty();
    return FText::FromString(FString::Printf(
        TEXT("帧：%lld\n相对时间：%.3f 秒\nUTC：%s\n原因：%s"),
        Frame->FrameIndex,
        Frame->SessionElapsedSeconds,
        *Frame->UtcTimestamp,
        *Frame->CaptureReason));
}
