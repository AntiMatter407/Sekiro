#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UnLuaInterface.h"
#include "SKHUD.generated.h"

class APlayerController;
class USKUIManagerComponent;

UCLASS()
class SEKIRO_API ASKHUD : public AHUD, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    ASKHUD();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|HUD")
    USKUIManagerComponent* GetUIManager() const;

    UFUNCTION(BlueprintCallable, Category = "UI|HUD|Lua")
    void SetUseLuaHUDLogic(bool bNewUseLuaHUDLogic); // 设置是否由 Lua 接管 HUD 逻辑

    UFUNCTION(BlueprintCallable, Category = "UI|HUD|Lua")
    bool IsUsingLuaHUDLogic() const;                  // 是否启用 Lua HUD 逻辑

    UFUNCTION(BlueprintCallable, Category = "UI|HUD|Lua")
    void SetLuaHUDModuleName(const FString& ModuleName); // 设置 Lua HUD 模块名

    UFUNCTION(BlueprintCallable, Category = "UI|HUD|Lua")
    FString GetLuaHUDModuleName() const;              // 获取 Lua HUD 模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    /** Lua 可覆盖的 HUD 初始化入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "UI|HUD|Gameplay")
    void HandleHUDInitialized();

    /** Lua 可覆盖的 HUD 逐帧入口。 */
    UFUNCTION(BlueprintNativeEvent, Category = "UI|HUD|Gameplay")
    void HandleHUDTick(float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "UI|HUD|Lua")
    void RefreshCachedHUDOwner();                     // 刷新 HUD 所属玩家

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|HUD|Lua")
    bool HasPlayerController() const;                 // 是否存在玩家控制器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|HUD|Lua")
    bool IsLocalPlayerController() const;             // 是否是本地玩家控制器

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UI|HUD|Lua")
    APlayerController* GetHUDPlayerController() const; // 获取 HUD 玩家控制器

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USKUIManagerComponent> UIManager;      // UI 管理组件

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD|Lua")
    bool bUseLuaHUDLogic = true;                      // 是否由 Lua 接管 HUD 逻辑

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HUD|Lua")
    FString LuaHUDModuleName = TEXT("Gameplay.Sekiro.UI.SKHUD"); // Lua HUD 模块名

private:
    UPROPERTY()
    TObjectPtr<APlayerController> CachedPlayerController; // 缓存玩家控制器

    void RefreshCachedOwner();                        // 刷新所属玩家
};
