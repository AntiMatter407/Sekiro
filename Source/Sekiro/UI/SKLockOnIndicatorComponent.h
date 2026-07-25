#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "UnLuaInterface.h"
#include "SKLockOnIndicatorComponent.generated.h"

class ACharacter;
class APlayerController;
class USKCameraManagerComponent;
class USKLockOnIndicatorWidget;

UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKLockOnIndicatorComponent : public UActorComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKLockOnIndicatorComponent();

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetUseLuaLockOnIndicatorLogic(bool bNewUseLuaLockOnIndicatorLogic); // 设置是否由 Lua 接管锁定 UI

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool IsUsingLuaLockOnIndicatorLogic() const;         // 是否启用 Lua 锁定 UI

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLuaLockOnIndicatorModuleName(const FString& ModuleName); // 设置 Lua 锁定 UI 模块名

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    FString GetLuaLockOnIndicatorModuleName() const;     // 获取 Lua 锁定 UI 模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    /** Lua 可覆盖的锁定指示器逐帧入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "UI|LockOn|Gameplay")
    bool HandleLockOnIndicatorTick(float DeltaTime);

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void RefreshCachedLockOnComponents();                // 刷新锁定 UI 依赖组件

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void ValidateLockTargetForScript();                  // 校验当前锁定目标

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool HasOwnerCharacter() const;                      // 是否存在所属角色

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool IsLocalPlayerControlled() const;                // 是否由本地玩家控制

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool IsLockedOn() const;                             // 是否存在锁定目标

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    AActor* GetLockTarget() const;                       // 获取当前锁定目标

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    float GetLockTargetDistance() const;                 // 获取锁定目标距离

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool UpdateLockTargetScreenPositionForScript(float TargetHeightOffset); // 更新锁定点屏幕位置

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|LockOn|Lua")
    FVector2D GetCachedLockTargetScreenPosition() const; // 获取缓存屏幕位置

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|LockOn|Lua")
    float GetCachedLockTargetScreenX() const;            // 获取缓存屏幕 X

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|LockOn|Lua")
    float GetCachedLockTargetScreenY() const;            // 获取缓存屏幕 Y

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    bool EnsureLockOnIndicatorWidget();                  // 确保锁定点控件存在

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|LockOn|Lua")
    bool HasLockOnIndicatorWidget() const;               // 是否已创建锁定点控件

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorVisible(bool bVisible);       // 设置锁定点显隐

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorScreenPositionXY(float ScreenX, float ScreenY); // 设置锁定点屏幕位置

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorSize(float NewSize);          // 设置锁定点尺寸

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorScale(float NewScale);        // 设置锁定点缩放

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorOpacity(float NewOpacity);    // 设置锁定点透明度

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorThickness(float NewThickness); // 设置锁定点线宽

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorColor(FLinearColor NewColor); // 设置锁定点颜色

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void SetLockOnIndicatorColorRGBA(float Red, float Green, float Blue, float Alpha); // 设置锁定点 RGBA 颜色

    UFUNCTION(BlueprintCallable, Category = "UI|LockOn|Lua")
    void RemoveLockOnIndicatorWidget();                  // 移除锁定点控件

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    TSubclassOf<USKLockOnIndicatorWidget> IndicatorWidgetClass; // 锁定点控件类型

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    int32 IndicatorZOrder = 80;                         // 锁定点视口层级

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn")
    float ScreenCullPadding = 80.0f;                    // 屏幕边缘隐藏缓冲

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn|Lua")
    bool bUseLuaLockOnIndicatorLogic = true;            // 是否由 Lua 接管锁定 UI 逻辑

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|LockOn|Lua")
    FString LuaLockOnIndicatorModuleName = TEXT("Gameplay.Sekiro.UI.SKLockOnIndicator"); // Lua 锁定 UI 模块名

private:
    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;              // 所属角色

    UPROPERTY()
    TObjectPtr<APlayerController> PlayerController;     // 本地玩家控制器

    UPROPERTY()
    TObjectPtr<USKCameraManagerComponent> CameraManager; // 锁定目标来源组件

    UPROPERTY()
    TObjectPtr<USKLockOnIndicatorWidget> IndicatorWidget; // 锁定点控件实例

    FVector2D CachedLockTargetScreenPosition = FVector2D::ZeroVector; // 缓存屏幕位置
    float IndicatorSize = 48.0f;                       // 当前锁定点尺寸
    float IndicatorThickness = 2.0f;                   // 当前锁定点线条粗细
    FLinearColor IndicatorColor = FLinearColor(1.0f, 0.35f, 0.05f, 1.0f); // 当前锁定点颜色

    void RefreshCachedComponents();                    // 刷新依赖组件缓存
    FVector GetLockTargetAnchorLocation(float TargetHeightOffset) const; // 获取锁定点世界锚点
    bool IsScreenPositionInViewport(const FVector2D& ScreenPosition) const; // 判断屏幕点是否在可见范围
};
