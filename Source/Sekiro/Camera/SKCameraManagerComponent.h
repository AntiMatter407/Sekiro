#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SKCameraManagerComponent.generated.h"

class ACharacter;
class USKInputManager;
class USKMovementComponent;
class UCharacterMovementComponent;

UENUM(BlueprintType)
enum class ESKCameraMode : uint8
{
    Free,          // 自由视角
    SprintAlign,   // 冲刺：身体朝运动方向，摄像机按锁定状态选择目标
    LockOn         // 锁定目标
};

UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class SEKIRO_API USKCameraManagerComponent : public UActorComponent
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

    UFUNCTION(BlueprintCallable, Category = "Camera|State")
    float GetMoveDirectionAngle() const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ── 调参项 ───────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float SprintActorInterpSpeed = 12.0f;             // 冲刺时身体转向速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float SprintCameraYawInterpSpeed = 3.0f;          // 冲刺时摄像机追身体速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float LockOnActorInterpSpeed = 14.0f;             // 锁定时身体转向速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float LockOnCameraYawInterpSpeed = 8.0f;          // 锁定时摄像机追目标速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Rotation")
    float MinMoveInputForFacing = 0.1f;               // 触发朝向更新的最小移动输入

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

private:
    // ── 内部状态 ─────────────────────────────────────────────

    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;            // 所属角色

    UPROPERTY()
    TObjectPtr<USKInputManager> InputManager;         // 输入组件缓存

    UPROPERTY()
    TObjectPtr<USKMovementComponent> MovementComponent; // 只狼移动组件缓存

    UPROPERTY()
    TObjectPtr<AActor> LockTarget;                    // 当前锁定目标

    ESKCameraMode CameraMode = ESKCameraMode::Free;   // 当前摄像机模式
    FVector2D PendingLookInput = FVector2D::ZeroVector; // 本帧待消费视角输入
    float MoveDirectionAngle = 0.f;                   // 移动方向相对角色朝向角度

    // ── 内部流程 ─────────────────────────────────────────────

    void RefreshCachedComponents();                   // 刷新角色相关组件缓存
    void ValidateLockTarget();                        // 检查当前锁定目标是否仍有效
    ESKCameraMode ResolveCameraMode() const;          // 计算本帧摄像机模式
    void UpdateMovementRotationSettings();            // 更新移动组件朝向开关
    void UpdateMoveDirectionAngle();                  // 更新动画方向角
    void UpdateFreeMode(float DeltaTime);             // 自由模式更新
    void UpdateSprintAlignMode(float DeltaTime);      // 冲刺对齐模式更新
    void UpdateLockOnMode(float DeltaTime);           // 锁定模式更新
    bool GetDesiredMoveYaw(float& OutYaw) const;      // 获取移动输入对应世界 Yaw
    bool GetLockTargetYaw(float& OutYaw) const;       // 获取锁定目标世界 Yaw
    void ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime); // 插值角色 Yaw
    void ApplyControllerYaw(float TargetYaw, float InterpSpeed, float DeltaTime); // 插值控制器 Yaw
    void ApplyPendingLookInput();                     // 消费自由视角输入
};
