#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UnLuaInterface.h"
#include "SKCombatHUDWidget.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class ESKHUDFillMode : uint8
{
    None,
    LeftToRight,
    RightToLeft,
    CenterOut
};

/** 一层已导入原图的显示描述；坐标/UV/样式由 Lua 显式提供。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKCombatHUDSprite
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName Id; // 稳定图层标识，同名提交替换原图层

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName Group; // 显示数据组，不携带业务数值

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UTexture2D> Texture = nullptr; // 已导入纹理，控件持有显示期间引用

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Position = FVector2D::ZeroVector; // 原版参考坐标；投影组使用锚点相对偏移

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Size = FVector2D::ZeroVector; // 原版参考尺寸，不从纹理猜测布局

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D UVMin = FVector2D::ZeroVector; // 图集左上归一化坐标

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D UVMax = FVector2D::UnitVector; // 图集右下归一化坐标

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Anchor = FVector2D::ZeroVector; // 宽屏布局锚点，范围零到一

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ESKHUDFillMode Fill = ESKHUDFillMode::None; // 裁切方向，不压缩整张原图

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor Tint = FLinearColor::White; // 显式颜色调制，默认保留原图

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bBrokenOnly = false; // 仅在生存快照为躯干崩溃时绘制

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bMirrorX = false; // 原版左右半条纹理的水平镜像
};

USTRUCT()
struct FSKCombatHUDGroupState
{
    GENERATED_BODY()

    bool bVisible = false; // 由脚本提供的有效快照显隐
    bool bBroken = false; // 只读生存状态镜像，不控制玩法
    float Ratio = 0.f; // 显示比例，不能反向写入 GAS
    bool bScreenPositioned = false; // 是否采用世界目标投影位置
    bool bProjectionVisible = false; // 最近投影是否可见
    FVector2D ScreenPosition = FVector2D::ZeroVector; // 玩家视口相对像素，仅绘制时换算一次 DPI
};

/** 通用原图/图集 HUD 绘制宿主；不读取角色、不选择资源、不编排生命逻辑。 */
UCLASS(BlueprintType, Blueprintable)
class SEKIRO_API USKCombatHUDWidget : public UUserWidget, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    // ── 样式和显示数据 ──────────────────────────────────────
    virtual FString GetModuleName_Implementation() const override;

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    bool SetReferenceSize(FVector2D NewReferenceSize);

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    bool AddHUDSprite(const FSKCombatHUDSprite& Sprite);

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    void ClearHUDSprites();

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    bool SetHUDGroupState(FName Group, bool bVisible, float Ratio, bool bBroken);

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    void SetHUDGroupScreenPosition(FName Group, FVector2D ScreenPositionPixels, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "UI|CombatHUD")
    bool ProjectActorAnchor(AActor* Actor, float HeightOffset, FVector2D& OutScreenPosition) const;

protected:
    // ── Slate 原图绘制 ──────────────────────────────────────
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    // ── 已验证显示配置 ──────────────────────────────────────
    UPROPERTY(Transient)
    TArray<FSKCombatHUDSprite> Sprites; // 按配置顺序绘制且持有纹理强引用

    UPROPERTY(Transient)
    TMap<FName, FSKCombatHUDGroupState> Groups; // 只读资源比例和投影显示状态

    UPROPERTY(EditAnywhere, Category = "UI|Lua")
    FString LuaModuleName = TEXT("Gameplay.Sekiro.UI.SKCombatHUD"); // 只读GAS绑定和UI显示编排

    FVector2D ReferenceSize = FVector2D::ZeroVector; // 没有显式参考尺寸时不绘制
};
