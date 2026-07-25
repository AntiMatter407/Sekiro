#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UnLuaInterface.h"
#include "SKCameraManagerComponent.generated.h"

class ACharacter;
class USKMovementComponent;

UENUM(BlueprintType)
enum class ESKCameraMode : uint8
{
    Free,          // 自由视角
    SprintAlign,   // 冲刺：身体朝运动方向，锁定时摄像机看目标
    LockOn         // 锁定目标
};

UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKCameraManagerComponent : public UActorComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKCameraManagerComponent();

    // ── 锁定目标接口 ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    void SetLockTarget(AActor* NewTarget);

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    void ClearLockTarget();

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    void ToggleLockTarget(AActor* NewTarget);

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    bool ToggleLockTargetInView();

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    AActor* FindBestLockTargetInView() const;

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    bool IsLockedOn() const;

    UFUNCTION(BlueprintCallable, Category = "Camera|LockOn")
    AActor* GetLockTarget() const;

    // ── 输入与状态查询 ───────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Camera|Input")
    void AddLookInput(FVector2D LookAxis);

    // ── 视角调参 ─────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
    float LookSensitivityYaw = 1.0f;                  // 水平视角灵敏度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
    float LookSensitivityPitch = 1.0f;                // 俯仰视角灵敏度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Sensitivity")
    uint32 bInvertPitch : 1;                          // 是否反转俯仰

    UFUNCTION(BlueprintCallable, Category = "Camera|State")
    ESKCameraMode GetCameraMode() const;

    UFUNCTION(BlueprintCallable, Category = "Camera|State")
    bool IsSprintCameraAligning() const;

    // ── Lua 相机宿主 ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void SetUseLuaCameraLogic(bool bNewUseLuaCameraLogic); // 设置是否由 Lua 接管相机逻辑

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    bool IsUsingLuaCameraLogic() const;                    // 是否启用 Lua 相机逻辑

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void SetLuaCameraModuleName(const FString& ModuleName); // 设置 Lua 相机模块名

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    FString GetLuaCameraModuleName() const;                // 获取 Lua 相机模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    UFUNCTION(BlueprintNativeEvent, Category = "Camera|Lua")
    void UpdateCameraLogic(float DeltaTime);               // Lua 可覆盖的逐帧相机策略入口

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void RefreshCachedCameraComponents();                  // 刷新脚本可用组件缓存

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void ValidateLockTargetForScript();                    // 校验当前锁定目标

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    bool HasOwnerCharacter() const;                        // 是否存在所属角色

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void SetCameraModeByName(FName ModeName);              // 通过名称设置相机模式

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    FName GetCameraModeName() const;                       // 获取相机模式名

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    bool IsMovementTierSprint() const;                     // 当前移动档位是否 Sprint

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    bool HasLockTargetYaw() const;                         // 是否存在锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    float GetLockTargetYawOrFallback(float FallbackYaw) const; // 获取锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    float GetOwnerYaw() const;                             // 获取所属角色 Yaw

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    float GetControllerYawOrFallback(float FallbackYaw) const; // 获取控制器 Yaw

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void ApplyControllerYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime); // 脚本应用控制器 Yaw

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void ApplyPendingLookInputForScript();                  // 脚本消费自由视角输入

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    void ClearPendingLookInputForScript();                  // 清空本帧视角输入

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    float GetSprintCameraYawInterpSpeed() const;            // 获取冲刺相机插值速度

    UFUNCTION(BlueprintCallable, Category = "Camera|Lua")
    float GetLockOnCameraYawInterpSpeed() const;            // 获取锁定相机跟随速度

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ── 调参项 ───────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float SprintCameraYawInterpSpeed = 3.0f;          // 冲刺时摄像机追身体速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float LockOnCameraYawInterpSpeed = 8.0f;          // 锁定时摄像机跟随目标速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
    float MaxLockOnRange = 1500.f;                    // 锁定搜索最大距离

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
    float LockOnSearchHalfAngle = 35.f;               // 当前摄像机视角内可锁定半角

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
    float LockOnBreakDistanceMultiplier = 1.2f;       // 已锁定目标超出搜索距离倍率后断开

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
    float LockOnAngleScoreWeight = 0.7f;              // 锁定候选角度评分权重

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|LockOn")
    float LockOnDistanceScoreWeight = 0.3f;           // 锁定候选距离评分权重

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lua")
    bool bUseLuaCameraLogic = true;                   // 是否由 Lua 接管相机逻辑

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lua")
    FString LuaCameraModuleName = TEXT("Gameplay.Sekiro.Camera.SKCameraManager"); // Lua 相机模块名

private:
    // ── 内部状态 ─────────────────────────────────────────────

    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;            // 所属角色

    UPROPERTY()
    TObjectPtr<USKMovementComponent> MovementComponent; // 只狼移动组件缓存

    UPROPERTY()
    TObjectPtr<AActor> LockTarget;                    // 当前锁定目标

    ESKCameraMode CameraMode = ESKCameraMode::Free;   // 当前摄像机模式
    FVector2D PendingLookInput = FVector2D::ZeroVector; // 本帧待消费视角输入

    // ── 内部流程 ─────────────────────────────────────────────

    ESKCameraMode ResolveCameraModeByName(FName ModeName) const; // 根据名称解析相机模式
    void RefreshCachedComponents();                   // 刷新角色相关组件缓存
    void ValidateLockTarget();                        // 检查当前锁定目标是否仍有效
    bool GetLockTargetYaw(float& OutYaw) const;       // 获取锁定目标世界 Yaw
    void ApplyControllerYaw(float TargetYaw, float InterpSpeed, float DeltaTime); // 插值控制器 Yaw
    void ApplyPendingLookInput();                     // 消费自由视角输入
};
