#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UnLuaInterface.h"
#include "SKMovementComponent.generated.h"

class ACharacter;
class USKCameraManagerComponent;
class USKInputManager;

UENUM(BlueprintType)
enum class ESKMovementTier : uint8
{
    Idle,                                                           // 静止
    Walk,                                                           // 步行
    Run,                                                            // 奔跑
    Sprint,                                                         // 冲刺
    Crouch                                                          // 蹲行
};

UCLASS()
class SEKIRO_API USKMovementComponent : public UCharacterMovementComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKMovementComponent();

    // ── 移动档位与速度快照 ────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float WalkSpeed = 140.f;                                      // Lua 当前发布的步行目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float RunSpeed = 407.f;                                       // Lua 当前发布的跑步目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float SprintSpeed = 853.f;                                    // Lua 当前发布的冲刺目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State")
    ESKMovementTier CurrentMovementTier = ESKMovementTier::Run;   // 输入 Lua 选择的当前移动档位

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMovementSpeedProfileForScript(float NewWalkSpeed, float NewRunSpeed, float NewSprintSpeed); // 发布 Lua 速度配置

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMaxWalkSpeedForScript(float NewMaxWalkSpeed);          // 设置本帧原生移动速度上限

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsMovementTierSprint() const;                             // 当前档位是否为冲刺

    // ── Lua Movement 宿主 ─────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetUseLuaMovementLogic(bool bNewUseLuaMovementLogic);     // 设置是否由 Lua 接管移动策略

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsUsingLuaMovementLogic() const;                          // 是否启用 Lua 移动策略

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetLuaMovementModuleName(const FString& ModuleName);      // 设置 Lua Movement 模块名

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    FString GetLuaMovementModuleName() const;                      // 获取 Lua Movement 模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    UFUNCTION(BlueprintNativeEvent, Category = "Movement|Lua")
    void UpdateMovementLogic(float DeltaTime);                     // Lua 可覆盖的逐帧移动策略入口

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void RefreshCachedMovementComponents();                       // 刷新脚本可用组件缓存

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool HasOwnerCharacter() const;                               // 是否存在所属角色

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputX() const;                                  // 获取屏幕横向移动输入

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputY() const;                                  // 获取屏幕纵向移动输入

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputAmount() const;                             // 获取移动输入强度

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsLockedOn() const;                                      // 是否锁定目标

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool HasLockTargetYaw() const;                                // 是否存在锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetLockTargetYawOrFallback(float FallbackYaw) const;    // 获取锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetOwnerYaw() const;                                    // 获取角色世界 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetControllerYawOrFallback(float FallbackYaw) const;    // 获取控制器世界 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float NormalizeDeltaYaw(float FromYaw, float ToYaw) const;     // 计算最短 Yaw 差

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMovementRotationSettingsForScript(bool bNewOrientRotationToMovement, bool bNewUseControllerDesiredRotation); // 设置原生自动旋转开关

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void ApplyActorYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime); // 插值角色 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMoveFacingSnapshotForScript(bool bHasDesiredMoveYaw, float NewDesiredMoveYaw, float NewMoveDirectionAngleBeforeRotation); // 发布转向前移动快照

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void ClearMoveFacingSnapshotForScript();                       // 清空转向前移动快照

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetRootMotionMoveDirectionForScript(bool bEnabled, float TargetWorldYaw); // 设置动画根运动的水平目标方向

    bool HasDesiredMoveYawSnapshot() const;                       // 动画采集是否可读取移动目标 Yaw
    float GetDesiredMoveYawSnapshot() const;                      // 动画采集读取移动目标 Yaw
    float GetMoveDirectionAngleBeforeRotationSnapshot() const;    // 动画采集读取转身前相对角

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Lua")
    bool bUseLuaMovementLogic = true;                             // 是否由 Lua 接管移动策略

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Lua")
    FString LuaMovementModuleName = TEXT("Gameplay.Sekiro.Movement.SKMovementComponent"); // Lua Movement 模块名

private:
    // ── 运行时引用与快照 ──────────────────────────────────────

    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;                       // 所属角色缓存

    UPROPERTY()
    TObjectPtr<USKInputManager> InputManager;                    // 输入组件缓存

    UPROPERTY()
    TObjectPtr<USKCameraManagerComponent> CameraManager;         // 相机组件缓存

    float DesiredMoveYawSnapshot = 0.f;                          // Lua 发布的输入目标世界 Yaw
    float MoveDirectionAngleBeforeRotationSnapshot = 0.f;        // Lua 发布的转身前角色局部方向角
    float RootMotionMoveDirectionYaw = 0.f;                      // Lua 发布的动画根运动水平目标世界 Yaw
    uint32 bHasDesiredMoveYawSnapshot : 1;                        // 当前是否存在有效移动目标快照
    uint32 bRootMotionMoveDirectionEnabled : 1;                   // 是否把动画根运动水平位移对齐到 Lua 目标方向

    // ── 通用执行 ──────────────────────────────────────────────

    void RefreshCachedComponents();                              // 刷新角色相关组件缓存
    void ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime); // 原生执行最短路径 Yaw 插值
    FTransform RedirectRootMotionTranslation(const FTransform& WorldRootMotion, UCharacterMovementComponent* SourceMovementComponent, float DeltaSeconds) const; // 对齐世界根运动水平位移
};
