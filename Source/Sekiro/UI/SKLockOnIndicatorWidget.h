#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SKLockOnIndicatorWidget.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SEKIRO_API USKLockOnIndicatorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    explicit USKLockOnIndicatorWidget(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn")
    void SetIndicatorStyle(float NewSize, float NewThickness, FLinearColor NewColor);

protected:
    virtual int32 NativePaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    float IndicatorSize = 48.0f;          // 锁定点绘制尺寸

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    float IndicatorThickness = 2.0f;      // 锁定点线条粗细

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    FLinearColor IndicatorColor = FLinearColor(1.0f, 0.35f, 0.05f, 1.0f); // 锁定点颜色
};
