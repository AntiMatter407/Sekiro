#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/SKMovementComponent.h"
#include "Animation/SKAnimDataTypes.h"
#include "Camera/SKCameraManagerComponent.h"
#include "Combat/SKCombatTypes.h"
#include "SKAnimInstance.generated.h"

class ASKCharacter;
class USKCameraManagerComponent;
class USKInputManager;
class USKCombatComponent;

// ============================================================================
// USKAnimInstance — 项目角色动画数据适配层
// 负责采集角色、移动、输入和相机数据，供 AnimBlueprint 消费。
// 动画选择、过渡编排和最终 Pose 求值由 AnimBlueprint 负责。
// ============================================================================

UCLASS()
class SEKIRO_API USKAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    /** 初始化项目动画实例的 UE Root Motion 基线。 */
    USKAnimInstance();

    /** 初始化动画实例并缓存数据采集所需的角色组件。 */
    virtual void NativeInitializeAnimation() override;

    /** 采集当前帧动画变量并计算项目移动状态。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    /** 在 ShowDebug Animation 中附加当前活动动画资产的 Lua 语义名。 */
    virtual void DisplayDebugInstance(FDisplayDebugManager& DisplayDebugManager, float& Indent) override;

    // ── Locomotion（Blueprint 读取） ────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Speed = 0.f;                            // 当前水平速度（cm/s，只取 XY 平面）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    float Angle = 0.f;                            // 当前速度方向相对角色朝向的角度（-180 到 180）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ESKMovementTier MovementTier = ESKMovementTier::Run; // 移动组件目标档位（决定真实最大速度）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    ESKLocomotionDirection Direction = ESKLocomotionDirection::Fwd; // 当前速度方向离散枚举（兼容旧状态机）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimMovementState MovementState = ESKAnimMovementState::Grounded; // 动画运动大状态（地面/空中/蹲姿）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimMovementAction MovementAction = ESKAnimMovementAction::None; // 当前互斥移动动作（无/垫步/闪避）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimRotationMode RotationMode = ESKAnimRotationMode::VelocityDirection; // 动画旋转模式（非锁定/锁定/冲刺对齐）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimGait Gait = ESKAnimGait::Run;          // 动画当前步态（状态机正在表现的 Walk/Run/Sprint）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimGait DesiredGait = ESKAnimGait::Run;   // 输入目标步态（由 Alt/摇杆推力/Shift 决定）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float GaitBlendAlpha = 1.f;                   // 当前步态切换进度（0=旧步态，1=目标步态）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimStance Stance = ESKAnimStance::Standing; // 当前姿态（站立/蹲姿）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimGroundedEntryState GroundedEntryState = ESKAnimGroundedEntryState::Idle; // 地面状态机入口建议（起步/循环/停止/转向）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector Velocity = FVector::ZeroVector;       // 当前世界速度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector SmoothedVelocity = FVector::ZeroVector; // 平滑后的世界速度，仅供动画方向和倾斜计算

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector LocalVelocity = FVector::ZeroVector;  // 角色局部空间速度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Jump")
    float VerticalVelocity = 0.f;                 // 当前垂直速度，供动画图判断起跳阶段和落地强度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Jump")
    float FallSpeed = 0.f;                        // 向下速度绝对值（cm/s，上升或接地时为 0）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector Acceleration = FVector::ZeroVector;   // 当前世界加速度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector LocalAcceleration = FVector::ZeroVector; // 角色局部空间加速度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float AccelerationAmount = 0.f;               // 当前水平加速度大小

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float MovementInputAmount = 0.f;              // 当前移动输入强度（0-1，保留摇杆轻推幅度）

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    float MoveInputX = 0.f;                       // 屏幕横向移动输入（-1=左，1=右）

    UPROPERTY(BlueprintReadOnly, Category = "Input")
    float MoveInputY = 0.f;                       // 屏幕纵向移动输入（-1=后，1=前）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float AimYawDelta = 0.f;                      // 控制器朝向相对角色朝向的 Yaw 差

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float AimPitch = 0.f;                         // 归一化控制器 Pitch（-180 到 180）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float AimYawRate = 0.f;                       // 控制器每秒 Yaw 变化速率（度/秒）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float RootYawOffset = 0.f;                    // 锁定待机时由身体吸收的视角 Yaw 偏差

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector2D GroundedLeanAmount = FVector2D::ZeroVector; // 地面横向/前向加速度归一化倾斜参数

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    FVector2D InAirLeanAmount = FVector2D::ZeroVector; // 空中横向/前向速度归一化倾斜参数

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Jump")
    float LandPredictionAmount = 0.f;             // 预测即将接地的归一化权重（0-1）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float DirectionDelta = 0.f;                   // 新输入方向相对当前速度方向的角度差

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float TurnAngle = 0.f;                        // 当前转向修正角度（正数右转，负数左转，用于 Start/Cycle 姿态修正）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    float RotationModeTransitionAngle = 0.f;      // 锁定/非锁定切换时需要补偿的转向角度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    ESKAnimTurnDirection TurnDirection = ESKAnimTurnDirection::None; // 当前转向修正方向（只区分左/右）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bIsMoving : 1;                         // 角色是否仍有实际水平速度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bHasMovementInput : 1;                 // 玩家当前是否有有效移动输入

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bIsAccelerating : 1;                   // 当前是否处于加速状态

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bIsGaitChanging : 1;                   // Walk/Run/Sprint 是否正在跨速度区间切换

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bShouldTurn : 1;                       // 移动循环中是否需要大角度转向修正（不代表进入 Turn 状态）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|ALS")
    uint32 bShouldRotationModeTurn : 1;           // 锁定/非锁定切换时是否播放转向过渡

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bShouldTurnInPlace : 1;                // 锁定待机时是否需要原地转身

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bShouldPivot : 1;                      // 预留急转标记（当前无后转动画，移动转向暂不使用）

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bIsInAir : 1;                          // 角色是否离地

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
    uint32 bIsCrouching : 1;                      // 角色是否蹲下

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Jump")
    uint32 bJumpStartedCrouched : 1;              // 本次离地前是否为蹲姿，解除胶囊蹲伏后仍可选择蹲姿起跳动画

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Camera")
    uint32 bIsLockedOn : 1;                       // 当前是否处于锁定目标模式

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Camera")
    uint32 bIsSprintCameraAligning : 1;           // 冲刺时摄像机是否正在向身体朝向对齐

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Camera")
    ESKCameraMode CameraMode = ESKCameraMode::Free; // 当前摄像机模式

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Camera")
    float MoveDirectionAngle = 0.f;               // 输入移动方向相对角色朝向的角度

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Rotation")
    float MoveDirectionAngleBeforeRotation = 0.f; // Movement Lua 旋转角色前锁存的输入方向角

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Rotation")
    float ActorYaw = 0.f;                         // 本帧角色世界 Yaw，供动画图锁定 Turn/Step 的绝对目标

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Rotation")
    float DesiredMoveYaw = 0.f;                   // 相机相对移动输入对应的绝对世界 Yaw

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|Rotation")
    uint32 bHasDesiredMoveYaw : 1;                // 当前是否存在可用的绝对移动目标 Yaw

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|LockOn")
    int32 LockOnCardinalDirection = 0;             // Movement Lua 发布的锁定四方向素材枚举值

    UPROPERTY(BlueprintReadOnly, Category = "Locomotion|LockOn")
    uint32 bHasLockOnLocomotionSnapshot : 1;       // 是否使用 Movement Lua 发布的锁定素材快照

    // ── Dodge（Blueprint 读取） ─────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    uint32 bIsDodging : 1;                       // 当前是否处于垫步或闪避动作

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    float DodgeDirection = 0.f;                  // 闪避方向相对角色朝向的角度，单位为度

    UPROPERTY(BlueprintReadOnly, Category = "Dodge")
    float DodgeDirectionLateral = 0.f;           // 闪避横向输入分量，负数为左、正数为右

    // ── Combat（Blueprint 读取） ──────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Combat|ALS")
    ESKAnimOverlayState OverlayState = ESKAnimOverlayState::Default; // 当前 ALS V4 风格姿态覆盖层

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    ESKCombatActionState CombatActionState = ESKCombatActionState::Neutral; // 当前战斗动作状态

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    ESKCombatPostureState CombatPostureState = ESKCombatPostureState::Normal; // 当前基础战斗姿态

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bIsGuardHeld = false;                   // 防御键是否仍按住

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bIsCombatGuardGroundPosture = false;    // 是否应输出地面防御基础姿态

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bIsCombatGuardAirPosture = false;       // 是否应输出空中防御基础姿态

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bIsCombatGuardPoseActive = false;       // 兼容字段：当前是否为任意防御基础姿态

    UPROPERTY(BlueprintReadOnly, Category = "Combat")
    bool bIsCombatFullBodyActionActive = false;  // 战斗全身 Slot 是否正在播放或混合

protected:
    /** 缓存当前动画 Pawn 及数据采集所需的项目组件。 */
    void CacheOwnerReferences();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true"))
    float RotationModeTurnEnterAngle = 45.f;      // 锁定/非锁定切换进入转向过渡的最小角度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true"))
    float RotationModeTurnDuration = 0.22f;       // 锁定/非锁定切换转向过渡保持时间

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
    float VelocitySmoothingInterpSpeed = 8.f;     // 动画速度快照插值速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
    float LeanInterpSpeed = 6.f;                  // 地面与空中 Lean 参数插值速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "180.0"))
    float RootYawOffsetLimit = 180.f;             // 锁定待机允许积累的最大根骨 Yaw 偏差

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
    float RootYawOffsetInterpSpeed = 12.f;        // RootYawOffset 进入和回零插值速度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
    float LandPredictionMinFallSpeed = 200.f;     // 开始预测落地的最小向下速度（cm/s）

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion|Tuning", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
    float LandPredictionMaxTime = 1.5f;           // 抛物线落地预测的最大模拟时长（秒）

    UPROPERTY()
    TObjectPtr<ASKCharacter> OwnerCharacter;      // 当前动画实例所属的 Sekiro 角色

    UPROPERTY()
    TObjectPtr<USKMovementComponent> OwnerMovement; // 所属角色的项目移动组件

    UPROPERTY()
    TObjectPtr<USKCameraManagerComponent> OwnerCameraManager; // 所属角色的项目相机组件

    UPROPERTY()
    TObjectPtr<USKInputManager> OwnerInputManager; // 所属角色的项目输入组件

    UPROPERTY()
    TObjectPtr<USKCombatComponent> OwnerCombatComponent; // 所属角色的战斗动作宿主

    float RotationModeTurnTimeRemaining = 0.f;    // 锁定/非锁定切换转向过渡剩余时间
    float PreviousControlYaw = 0.f;               // 上一帧控制器世界 Yaw
    uint32 bHasPreviousControlYaw : 1;             // 是否已经采集过可用于计算 AimYawRate 的控制器 Yaw
};
