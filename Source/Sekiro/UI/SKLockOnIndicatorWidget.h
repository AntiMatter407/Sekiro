#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SKLockOnIndicatorWidget.generated.h"

class UTexture2D;

UCLASS(Blueprintable, BlueprintType)
class SEKIRO_API USKLockOnIndicatorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    explicit USKLockOnIndicatorWidget(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn")
    void SetIndicatorStyle(float NewSize, float NewThickness, FLinearColor NewColor);

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn")
    bool SetIndicatorTexture(UTexture2D* Texture, FVector2D UVMin, FVector2D UVMax);

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn")
    void SetDebugDrawingEnabled(bool bEnabled);

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
    FLinearColor IndicatorColor = FLinearColor::White; // 默认不改变原版纹理颜色

private:
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> IndicatorTexture; // 正式显示纹理，缺失时不绘制调试替代品

    FVector2D TextureUVMin = FVector2D::ZeroVector; // 图集左上坐标
    FVector2D TextureUVMax = FVector2D::UnitVector; // 图集右下坐标
    bool bDebugDrawingEnabled = false; // 旧圆环仅允许显式调试开启
};
