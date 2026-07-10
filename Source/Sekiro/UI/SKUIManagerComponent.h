#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "UnLuaInterface.h"
#include "SKUIManagerComponent.generated.h"

class APlayerController;

UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKUIManagerComponent : public UActorComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKUIManagerComponent();

    UFUNCTION(BlueprintCallable, Category = "UI|Lua")
    void SetUseLuaUIManagerLogic(bool bNewUseLuaUIManagerLogic); // 设置是否由 Lua 接管 UI 管理

    UFUNCTION(BlueprintCallable, Category = "UI|Lua")
    bool IsUsingLuaUIManagerLogic() const;              // 是否启用 Lua UI 管理

    UFUNCTION(BlueprintCallable, Category = "UI|Lua")
    void SetLuaUIManagerModuleName(const FString& ModuleName); // 设置 Lua UI 管理模块名

    UFUNCTION(BlueprintCallable, Category = "UI|Lua")
    FString GetLuaUIManagerModuleName() const;          // 获取 Lua UI 管理模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    UFUNCTION(BlueprintCallable, Category = "UI|Runtime")
    void RefreshCachedUIOwner();                        // 刷新 UI 所属控制器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Runtime")
    bool HasPlayerController() const;                   // 是否存在玩家控制器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Runtime")
    bool IsLocalPlayerController() const;               // 是否是本地玩家控制器

    UFUNCTION(BlueprintCallable, Category = "UI|Layer")
    void SetLayerZOrder(FName LayerName, int32 ZOrder); // 设置 UI 层级 ZOrder

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Layer")
    int32 GetLayerZOrder(FName LayerName) const;        // 获取 UI 层级 ZOrder

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Layer")
    int32 GetResolvedWidgetZOrder(FName LayerName, int32 ZOrderOffset) const; // 获取最终 ZOrder

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    bool CreateWidgetByClass(FName WidgetName, TSubclassOf<UUserWidget> WidgetClass, FName LayerName, int32 ZOrderOffset); // 通过类型创建控件

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    bool CreateWidgetByPath(FName WidgetName, const FString& WidgetClassPath, FName LayerName, int32 ZOrderOffset); // 通过路径创建控件

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Widget")
    bool HasManagedWidget(FName WidgetName) const;      // 是否存在托管控件

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|Widget")
    UUserWidget* GetManagedWidget(FName WidgetName) const; // 获取托管控件

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void ShowWidget(FName WidgetName);                  // 显示托管控件

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void HideWidget(FName WidgetName);                  // 隐藏托管控件

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void SetWidgetVisible(FName WidgetName, bool bVisible); // 设置托管控件显隐

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void SetWidgetOpacity(FName WidgetName, float Opacity); // 设置托管控件透明度

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void SetWidgetPosition(FName WidgetName, float ScreenX, float ScreenY); // 设置托管控件屏幕位置

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void SetWidgetSize(FName WidgetName, float SizeX, float SizeY); // 设置托管控件尺寸

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void SetWidgetAlignment(FName WidgetName, float AlignmentX, float AlignmentY); // 设置托管控件锚点

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void RemoveWidget(FName WidgetName);                // 移除托管控件

    UFUNCTION(BlueprintCallable, Category = "UI|Widget")
    void RemoveAllWidgets();                            // 移除所有托管控件

    UFUNCTION(BlueprintCallable, Category = "UI|Input")
    void SetGameOnlyInputMode();                        // 设置游戏输入模式

    UFUNCTION(BlueprintCallable, Category = "UI|Input")
    void SetUIOnlyInputMode(FName FocusWidgetName, bool bShowCursor); // 设置纯 UI 输入模式

    UFUNCTION(BlueprintCallable, Category = "UI|Input")
    void SetGameAndUIInputMode(FName FocusWidgetName, bool bShowCursor); // 设置游戏与 UI 输入模式

    UFUNCTION(BlueprintCallable, Category = "UI|Input")
    void SetMouseCursorVisible(bool bVisible);          // 设置鼠标显隐

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Lua")
    bool bUseLuaUIManagerLogic = true;                  // 是否由 Lua 接管 UI 管理

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Lua")
    FString LuaUIManagerModuleName = TEXT("Gameplay.Sekiro.UI.SKUIManager"); // Lua UI 管理模块名

private:
    UPROPERTY()
    TObjectPtr<APlayerController> PlayerController;     // 所属玩家控制器

    UPROPERTY()
    TMap<FName, TObjectPtr<UUserWidget>> ManagedWidgets; // 托管控件表

    UPROPERTY()
    TMap<FName, int32> LayerZOrders;                    // UI 层级 ZOrder 表

    bool TryCallLuaUIManagerTick(float DeltaTime);       // 调用 Lua UI Tick
    FString ResolveLuaUIManagerModuleName() const;      // 解析 Lua UI 模块名
    void RefreshCachedOwner();                          // 刷新所属控制器
    TSubclassOf<UUserWidget> LoadWidgetClass(const FString& WidgetClassPath) const; // 加载控件类型
    FString MakeGeneratedWidgetClassPath(const FString& WidgetClassPath) const; // 生成蓝图类路径
    UUserWidget* FindManagedWidget(FName WidgetName) const; // 查找托管控件
    void SetInputModeWithWidget(FName FocusWidgetName, bool bGameAndUI, bool bShowCursor); // 设置带控件焦点的输入模式
};
