#pragma once

#include "CoreMinimal.h"
#include "Animation/TrajectoryTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MotionTrajectory.h"
#include "SKMovementComponent.h"
#include "SKMotionMatchingTrajectoryComponent.generated.h"

class APawn;
class USKMovementComponent;

UENUM(BlueprintType)
enum class ESKMotionMatchingRotationMode : uint8
{
    Free,
    Locked,
};

UENUM(BlueprintType)
enum class ESKMotionMatchingQueryDomain : uint8
{
    None,
    Grounded,
    Airborne,
};

/** Lua 或 AI 一次性提交的完整意图，不包含实际速度或搜索请求。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingIntentInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    FVector MoveIntentWorldDirection = FVector::ZeroVector; // 世界方向，提交时投影到 XY 并归一化

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    float MoveIntentAmount = 0.f; // 输入强度，提交时限制到 0–1

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    ESKMotionMatchingGait RequestedGait = ESKMotionMatchingGait::Run; // 请求步态，与静止阶段独立

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    ESKMotionMatchingStance RequestedStance = ESKMotionMatchingStance::Standing; // 请求姿态，不代表实际姿态

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    ESKMotionMatchingRotationMode RotationMode = ESKMotionMatchingRotationMode::Free; // Lua 选择的面向模式

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    float DesiredFacingYaw = 0.f; // Locked 模式下由调用方提供的世界面向角，单位度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Intent")
    bool bHasFacingTarget = false; // 调用方确认锁定目标有效；组件不查询目标 UObject
};

/** 规范化后的纯值意图；版本由组件维护，与 Search Request Generation 无关。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingIntentSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Intent")
    FSKMotionMatchingIntentInput Intent; // 已规范化的完整意图副本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Intent")
    float DesiredMoveYaw = 0.f; // 有输入时派生，无输入时保留最后有效世界角，单位度

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Intent")
    bool bHasMoveIntent = false; // 方向有效且强度超过统一死区

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Intent")
    int64 IntentRevision = 0; // 0 表示尚未提交，之后仅在规范化内容变化时递增
};

/** 最近完成的移动反馈；无有效样本时不得当作静止查询使用。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingActualState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    int64 ActualSampleId = 0; // 成功发布时递增，与意图版本独立

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    double SampleTimeSeconds = 0.0; // 世界游戏时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    FTransform ActorTransformWS = FTransform::Identity; // 回调时角色实际世界变换

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    FVector ActualVelocityWS = FVector::ZeroVector; // CMC 实际世界速度，包含竖直分量，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    FVector ActualTranslationDeltaWS = FVector::ZeroVector; // 本移动步骤实际三维位移，单位 cm

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    FVector CollisionClippedTranslationWS = FVector::ZeroVector; // 既有 RootMotion 请求与实际水平位移差，仅作为诊断

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    TEnumAsByte<EMovementMode> MovementMode = MOVE_None; // 实际移动模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    uint8 CustomMovementMode = 0; // 自定义移动子模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Actual State")
    bool bHasCompletedMovement = false; // 已获得有限且连续的移动结果
};

/** 单次查询的完整值结果；失败结果不携带可搜索轨迹。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingQuerySnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    int64 QueryRevision = 0; // 游戏线程构建序号，与搜索请求代数独立

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    int64 GaitProfileRevision = 0; // 本次使用的速度配置版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FSKMotionMatchingIntentSnapshot Intent; // 包含本次 IntentRevision 的完整副本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FSKMotionMatchingActualState ActualState; // 包含本次 ActualSampleId 的完整副本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    ESKMotionMatchingQueryDomain QueryDomain = ESKMotionMatchingQueryDomain::None; // 有效轨迹使用的预测域

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FVector TargetIntentVelocityWS = FVector::ZeroVector; // 世界水平目标查询速度，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FVector PredictedFutureVelocityWS = FVector::ZeroVector; // 预测末端世界三维速度；地面 Z 为零，空中 Z 含重力，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    float PredictedFuturePlanarSpeed = 0.f; // 预测末端水平速度模长，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    float ExpectedSpeed = 0.f; // 目标查询速度模长，不代表角色实际速度

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FTransform QueryOriginWS = FTransform::Identity; // 查询原点，单位缩放，Actor 朝向；位置另应用既有动画轴适配

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FTransformTrajectory DesiredTrajectory; // 历史、唯一零时刻及未来样本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    bool bValid = false; // 仅完整构建成功时可消费

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Query")
    FName InvalidReason = NAME_None; // 无效原因，不静默沿用上一份轨迹
};

struct FSKTrajectoryHistorySample
{
    FTransform WorldTransform = FTransform::Identity;   // 采样时角色的世界变换
    float WorldGameTime = 0.f;                          // 采样对应的世界游戏时间，单位秒
    FVector ActualTranslationDeltaWS = FVector::ZeroVector; // RootMotion 与碰撞处理后的实际世界水平位移
    FVector CollisionClippedTranslationWS = FVector::ZeroVector; // 本帧 RootMotion 请求被约束掉的世界水平位移
    bool bWasCollisionClipped = false;                  // 本帧 RootMotion 请求是否被碰撞或地面约束裁剪
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKTrajectoryPredictionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Acceleration = 1800.f;                         // 有输入时逼近目标速度的加速度，单位 cm/s²

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Deceleration = 2400.f;                         // 无输入时减速到零的减速度，单位 cm/s²

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "2160.0"))
    float FacingTurnRateDegrees = 720.f;                 // 预测面对方向的最大转速，单位度/秒

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InputThreshold = 0.01f;                        // 低于该强度的输入按释放处理
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKAnimationRootMotionSpeedProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Walk = 140.f;                                  // Walk 动画族离线统计的标称水平 RootMotion 速度，独立于本帧候选，单位 cm/s

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Run = 407.f;                                   // Run 动画族离线统计的标称水平 RootMotion 速度，独立于本帧候选，单位 cm/s

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Sprint = 853.f;                                // Sprint 动画族离线统计的标称水平 RootMotion 速度，独立于本帧候选，单位 cm/s

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
    float Crouch = 140.f;                                // Crouch 动画族离线统计的标称水平 RootMotion 速度，独立于本帧候选，单位 cm/s
};

UCLASS(ClassGroup = (Animation), meta = (BlueprintSpawnableComponent), BlueprintType, Category = "Motion Matching")
class SEKIRO_API USKMotionMatchingTrajectoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // ── 组件生命周期 ──────────────────────────────────────────

    USKMotionMatchingTrajectoryComponent(const FObjectInitializer& ObjectInitializer);
    virtual void OnComponentCreated() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ── 正式意图接口 ──────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Intent")
    bool SubmitMotionMatchingIntent(const FSKMotionMatchingIntentInput& Input); // 校验并整体发布意图，不执行移动

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Intent")
    FSKMotionMatchingIntentSnapshot GetMotionMatchingIntent() const; // 仅游戏线程读取值副本

    // ── 正式实际状态接口 ──────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Actual State")
    FSKMotionMatchingActualState GetMotionMatchingActualState() const; // 游戏线程读取最近移动反馈副本

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Actual State")
    void InvalidateMotionMatchingActualState(); // 数据非法且无法建立新基线时丢弃连续性与历史

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Actual State")
    bool RebaseMotionMatchingActualState(); // 传送或网络校正后从当前角色状态重建零位移基线

    // ── 正式查询构建 ──────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Query")
    FSKMotionMatchingQuerySnapshot BuildMotionMatchingQuerySnapshot(); // 游戏线程整体构建，不自动驱动动画

    // ── 轨迹接口 ──────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    FTransformTrajectory GetMotionMatchingTrajectory() const; // 获取角色局部的历史、现在与未来轨迹

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Trajectory")
    void SetTrajectoryPredictionSettings(const FSKTrajectoryPredictionSettings& Settings); // 更新未来积分参数

    UFUNCTION(BlueprintCallable, Category = "Motion Matching|Trajectory")
    void SetAnimationRootMotionSpeedProfile(const FSKAnimationRootMotionSpeedProfile& SpeedProfile); // 发布动画离线统计速度

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    int32 GetIntentGeneration() const; // 获取最近一次语义意图变化代数

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    FVector2D GetMoveIntentSnapshot() const; // 获取有限且归一化的 MoveIntent 快照

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    float GetDesiredMoveYawSnapshot() const; // 获取移动轨迹使用的世界 Yaw

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    float GetDesiredFacingYawSnapshot() const; // 获取 Facing 轨迹使用的世界 Yaw

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    FVector GetLastActualTranslationDeltaWS() const; // 获取最近一次 RootMotion 消费后的实际水平位移

    UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
    FVector GetLastCollisionClippedTranslationWS() const; // 获取最近一次碰撞裁剪的水平位移

    FTransformTrajectory GetTrajectory() const;
    FTransformTrajectory GetTrajectoryWithSettings(
        const FMotionTrajectorySettings& Settings,
        bool bIncludeHistory) const;

private:
    // ── 查询版本与实际历史 ────────────────────────────────────

    int64 QueryRevision = 0; // 每次游戏线程构建递增
    int64 GaitProfileRevision = 0; // 速度配置变化时递增
    FSKAnimationRootMotionSpeedProfile LastQuerySpeedProfile; // 上次构建使用的速度配置
    TArray<FSKMotionMatchingActualState> CompletedActualHistory; // 仅移动完成回调写入，不混用旧 Tick 历史

    // ── 正式实际状态存储 ──────────────────────────────────────

    UPROPERTY(Transient)
    FSKMotionMatchingActualState CompletedActualState; // 不由旧轨迹 Tick 覆盖的移动反馈

    UFUNCTION()
    void CaptureCompletedMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity); // 角色移动完成委托

    // ── 正式意图存储 ──────────────────────────────────────────

    UPROPERTY(Transient)
    FSKMotionMatchingIntentSnapshot SubmittedIntent; // 新契约唯一提交结果，旧采样链不得覆盖

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|Intent", meta = (ClampMin = "0", ClampMax = "1"))
    float IntentInputDeadZone = 0.01f; // 新接口统一输入死区，后续预测直接消费规范化结果

    // ── 预测配置 ──────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (AllowPrivateAccess = "true"))
    FMotionTrajectorySettings PredictionSettings; // 默认未来预测时域

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (AllowPrivateAccess = "true"))
    FMotionTrajectorySettings HistorySettings; // 真实运动历史保留时域

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (ClampMin = "5", ClampMax = "120", AllowPrivateAccess = "true"))
    int32 SampleRate = 30; // 每秒生成的未来轨迹样本数

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (ClampMin = "0", ClampMax = "3600", AllowPrivateAccess = "true"))
    int32 MaxSamples = 120; // 最多保留的历史和未来样本数

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (AllowPrivateAccess = "true"))
    bool bPredictionIncludesHistory = true; // 默认查询是否包含真实历史

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (AllowPrivateAccess = "true"))
    FSKTrajectoryPredictionSettings TrajectoryPredictionSettings; // MoveIntent 未来积分参数

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Trajectory", meta = (AllowPrivateAccess = "true"))
    FSKAnimationRootMotionSpeedProfile AnimationRootMotionSpeedProfile; // 各 Gait 动画族离线统计的查询标称速度，不引用本帧候选

    // ── 真实状态采样 ──────────────────────────────────────────

    TArray<FSKTrajectoryHistorySample> SampleHistory; // 世界空间历史快照，查询时转换为当前角色局部空间
    FTransform PresentWorldTransform = FTransform::Identity; // 当前帧角色世界变换
    FVector PresentLinearVelocityLS = FVector::ZeroVector; // 当前帧角色局部线速度，单位 cm/s
    FVector2D MoveIntentSnapshot = FVector2D::ZeroVector; // 最近一次有限的屏幕空间 MoveIntent
    float MoveInputAmountSnapshot = 0.f; // 最近一次有限的输入强度
    float DesiredMoveYawSnapshot = 0.f; // Movement Trajectory 使用的世界目标 Yaw
    float DesiredFacingYawSnapshot = 0.f; // Facing Trajectory 使用的世界目标 Yaw
    int32 IntentGeneration = 0; // 输入、步态或 Facing 语义变化时递增的代数
    ESKMovementTier RequestedGaitSnapshot = ESKMovementTier::Idle; // 最近一次请求的移动档位
    bool bHasMoveIntentSnapshot = false; // 最近一次快照是否包含有效移动输入
    bool bLockedOnSnapshot = false; // 最近一次快照是否使用独立锁定 Facing
    FVector LastActualTranslationDeltaWS = FVector::ZeroVector; // 最近一次 RootMotion 消费后的实际世界水平位移
    FVector LastCollisionClippedTranslationWS = FVector::ZeroVector; // 最近一次碰撞或地面约束裁剪量

    // ── 预测实现 ──────────────────────────────────────────────

    const APawn* TryGetOwnerPawn() const;
    void TickTrajectory(float DeltaTime);
    void RefreshIntentSnapshot(const USKMovementComponent* MovementComponent);
    void TickHistoryEvictionPolicy();
    void FlushHistory();
    FTransformTrajectory GetHistory() const;
    FTransformTrajectory CombineHistoryPresentPrediction(
        bool bIncludeHistory,
        const FTransformTrajectory& Prediction) const;
    FTransform CalcWorldSpacePresentTransform(FVector& OutLinearVelocityWS) const;

    void PredictTrajectory(
        const USKMovementComponent* MovementComponent,
        float PredictionSeconds,
        int32 PredictionSampleRate,
        int32 PredictionMaxSamples,
        FTransformTrajectory& OutPrediction) const; // 生成角色局部未来轨迹
    FVector ResolveTargetVelocityLS(
        const USKMovementComponent* MovementComponent,
        bool& bOutHasMoveInput,
        float& OutMoveYawWS) const; // 解析角色局部目标速度
    float ResolveFacingYawLS(
        const USKMovementComponent* MovementComponent,
        bool bHasMoveInput,
        float MoveYawWS) const; // 解析独立于移动方向的局部 Facing
    float ResolveGaitProfileSpeed(const USKMovementComponent* MovementComponent) const; // 解析独立于本帧候选的当前 Gait 标称速度
    FTransformTrajectory SanitizeTrajectoryRange(
        const FTransformTrajectory& CombinedTrajectory) const; // 清理非法样本并保证时间严格递增
    void TransformTrajectoryToQuerySpace(
        FTransformTrajectory& Trajectory) const; // 在唯一适配点转换位置与 Facing
};
