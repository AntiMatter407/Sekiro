#pragma once

#include "CoreMinimal.h"
#include "LuaAnimSnapshotModel.h"
#include "Widgets/SLeafWidget.h"

DECLARE_DELEGATE_OneParam(FOnLuaAnimSnapshotSelected, int32);

/** 可缩放、可平移并按时间选择真实快照的 Slate 时间轴。 */
class SLuaAnimSnapshotTimeline final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLuaAnimSnapshotTimeline)
        : _Frames(nullptr)
    {
    }
        SLATE_ARGUMENT(const TArray<TSharedPtr<FLuaAnimSnapshotFrame>>*, Frames)
        SLATE_EVENT(FOnLuaAnimSnapshotSelected, OnSnapshotSelected)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetFrames(const TArray<TSharedPtr<FLuaAnimSnapshotFrame>>* InFrames);
    void SetSelectedFrameIndex(int32 InFrameIndex);
    void FitAll();
    void ResetView();

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual int32 OnPaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;
    virtual FReply OnMouseButtonDown(
        const FGeometry& MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(
        const FGeometry& MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(
        const FGeometry& MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseWheel(
        const FGeometry& MyGeometry,
        const FPointerEvent& MouseEvent) override;

private:
    double ScreenXToTime(float LocalX, float Width) const;
    float TimeToScreenX(double TimeSeconds, float Width) const;
    int32 FindNearestFrame(float LocalX, float Width) const;
    FText MakeFrameTooltip(int32 FrameArrayIndex) const;

    const TArray<TSharedPtr<FLuaAnimSnapshotFrame>>* Frames = nullptr; // 查看器拥有的有序帧数组
    FOnLuaAnimSnapshotSelected OnSnapshotSelected; // 点选真实帧后的通知
    double ViewStartSeconds = 0.0; // 当前时间窗左边界
    double ViewEndSeconds = 5.0; // 当前时间窗右边界
    int32 SelectedFrameIndex = INDEX_NONE; // 帧数组下标，不是 FrameIndex 字段
    int32 HoveredFrameIndex = INDEX_NONE; // 当前提示对应的帧数组下标
    bool bPanning = false; // 中键或右键拖动状态
    FVector2D LastPointerScreenPosition = FVector2D::ZeroVector; // 上一次拖动屏幕位置
};
