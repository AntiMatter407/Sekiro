#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/SKAnimInstance.h"
#include "GameplayTagContainer.h"
#include "Movement/SKMovementComponent.h"
#include "Movement/SKMotionMatchingTrajectoryComponent.h"
#include "SKMotionMatchingAnimInstance.generated.h"

class USKMotionMatchingTrajectoryComponent;
class UChooserTable;
class UPoseSearchDatabase;

UENUM(BlueprintType)
enum class ESKMotionMatchingLocomotionPhase : uint8
{
    Stationary,
    StartRequested,
    Moving,
    StopRequested,
    PivotRequested,
    Airborne, // 旧粗粒度占位值；保留枚举序号兼容，正式状态改用下方四个空中阶段
    Landing,
    ActionOwned,
    RecoveryRequested,
    JumpStart,
    Ascending,
    Apex,
    Falling,
};

UENUM(BlueprintType)
enum class ESKMotionMatchingPhaseTransitionReason : uint8
{
    None,
    Initialized,
    MoveIntentStarted,
    MoveIntentReleased,
    DirectionChangeRequested,
    MovementModeLeftGround,
    MovementModeLanded,
    LandingRecoveryStarted,
    LandingRecoverySettled,
    LandingRecoveryStopping,
    PostSelectionMovementConfirmed,
    PostSelectionSettled,
    RootMotionOwnerAcquired,
    RootMotionOwnerReleased,
    RootMotionRecoveryStarted,
    RootMotionRecoverySettled,
    RootMotionRecoveryStopping,
    AirborneAscending,
    AirborneApexReached,
    AirborneFalling,
    PivotCandidateUnavailable,
    PostSelectionPivotConfirmed,
};

UENUM(BlueprintType)
enum class ESKMotionMatchingSearchRequestState : uint8
{
    Inactive,
    PendingSearch,
    AwaitingActualStateConfirmation,
    Confirmed,
    PendingSearchNotAcknowledged,
};

/** 当前 Locomotion 语义状态；可在地面查询暂停时继续表达 CMC 空中事实，不直接选择动画。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingLocomotionStateSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    ESKMotionMatchingLocomotionPhase Phase = ESKMotionMatchingLocomotionPhase::Stationary; // 当前正式阶段

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    ESKMotionMatchingLocomotionPhase PreviousPhase = ESKMotionMatchingLocomotionPhase::Stationary; // 最近一次变化前的阶段

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    ESKMotionMatchingPhaseTransitionReason TransitionReason = ESKMotionMatchingPhaseTransitionReason::None; // 最近一次阶段变化的结构化原因

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 PhaseRevision = 0; // 首次有效发布及阶段变化时递增

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 QueryRevision = 0; // 本次状态使用的查询版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 IntentRevision = 0; // 本次状态使用的意图版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 ActualSampleId = 0; // 本次状态使用的实际移动样本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 GameplayTagRevision = 0; // 本次状态使用的 ASC 标签快照版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    int64 RootMotionOwnershipRevision = 0; // 本次状态使用的 Movement 所有权快照版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    ESKRootMotionOwnerType EffectiveRootMotionOwner = ESKRootMotionOwnerType::Locomotion; // 当前有效根运动所有者

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bHasRootMotionOwnershipSnapshot = false; // 当前是否获得 Movement 权威快照

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bUpperBodyActionActive = false; // 不夺取 Locomotion 根运动的上身通道是否占用

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bLocomotionRootMotionAllowed = true; // 排他 FullBody/Traversal 令牌是否仍允许 Locomotion

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    TEnumAsByte<EMovementMode> MovementMode = MOVE_None; // 当前 CMC 实际移动模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    uint8 CustomMovementMode = 0; // 当前 CMC 自定义移动子模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    float ActualPlanarSpeed = 0.f; // CMC 实际水平速度，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    float PredictedFuturePlanarSpeed = 0.f; // 同一查询未来积分末端的水平速度，单位 cm/s

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    float MoveDirectionErrorDegrees = 0.f; // 当前实际移动方向与查询移动方向的最小夹角，单位度

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    float FutureDirectionErrorDegrees = 0.f; // 当前实际移动方向与预测末端速度方向的最小夹角，单位度

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bTurnStartFallbackActive = false; // 当前 StartRequested 是否由缺少正式 Pivot 数据的大角度门禁触发

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bJustLanded = false; // 当前实际样本是否形成非地面到地面的落地边沿

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bBlockedByGameplayTags = false; // 当前 ASC 标签是否命中基础 Locomotion 阻塞集合

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    float PhaseElapsedSeconds = 0.f; // 当前阶段已保持的动画更新时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|State")
    bool bValid = false; // 已获得有限 ActualState；不等同于地面轨迹查询可搜索
};

/** 持久搜索请求；RootMotion 阶段等待后续实际状态确认，无位移空中姿势可由 PostSelection 直接确认。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingSearchRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    int64 Generation = 0; // 离散请求变化时递增，不复用 IntentRevision

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    int64 PhaseRevision = 0; // 创建请求时使用的语义状态版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    int64 QueryRevision = 0; // 创建请求时使用的查询版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    int64 GameplayTagRevision = 0; // 创建请求时使用的 ASC 标签快照版本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    ESKMotionMatchingLocomotionPhase RequestedPhase = ESKMotionMatchingLocomotionPhase::Stationary; // 合法数据库阶段

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    ESKMotionMatchingGait RequestedGait = ESKMotionMatchingGait::Run; // 请求步态

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    ESKMotionMatchingStance RequestedStance = ESKMotionMatchingStance::Standing; // 请求姿态

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    ESKMotionMatchingRotationMode RequestedMode = ESKMotionMatchingRotationMode::Free; // 自由或锁定候选模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    double RequestTimeSeconds = 0.0; // 创建请求时的世界游戏时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    float SearchWaitElapsedSeconds = 0.f; // 查询有效且等待 PostSelection 的累计动画更新时间

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    float MinimumHoldTime = 0.f; // 确认前最短保持时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    ESKMotionMatchingSearchRequestState State = ESKMotionMatchingSearchRequestState::Inactive; // 请求生命周期或显式故障状态

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Search Request")
    bool bPending = false; // 尚未由阶段所需实际状态完成确认时持续为 true
};

/** PostSelection 发布的候选事实；匹配请求后仍需等待后续实际移动确认。 */
USTRUCT(BlueprintType)
struct SEKIRO_API FSKMotionMatchingSelectionSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    int64 SelectionRevision = 0; // 每次接收格式有效的选择结果时递增

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    int64 RequestGeneration = 0; // AnimGraph 执行搜索时消费的请求代次

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    ESKMotionMatchingLocomotionPhase SelectedPhase = ESKMotionMatchingLocomotionPhase::Stationary; // 选中数据库声明的阶段

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    ESKMotionMatchingGait SelectedGait = ESKMotionMatchingGait::Run; // 选中数据库声明的步态

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    ESKMotionMatchingStance SelectedStance = ESKMotionMatchingStance::Standing; // 选中数据库声明的姿态

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    ESKMotionMatchingRotationMode SelectedMode = ESKMotionMatchingRotationMode::Free; // 选中数据库声明的旋转模式

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    TObjectPtr<UObject> SelectedDatabase; // 实际参与搜索并产生结果的数据库

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    TObjectPtr<UObject> SelectedAsset; // 搜索结果指向的动画资产

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    float SelectedAssetTime = 0.f; // 选中动画内的采样时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    float WantedPlayRate = 1.f; // 搜索结果建议的播放速率

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    float SelectionCost = 0.f; // Motion Matching 返回的最终比较代价

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    double SelectionTimeSeconds = 0.0; // 接收结果时的世界游戏时间，单位秒

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    int64 ActualSampleIdAtSelection = 0; // 接收选择结果时已经发布的 CMC 完成移动样本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    int64 ConfirmedActualSampleId = 0; // 首次满足该阶段确认条件的后续 CMC 样本

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bJumpedToPose = false; // 本次结果是否触发了新 Pose 跳转

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bMirrored = false; // 选中姿势是否使用镜像结果

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bMatchesPendingRequest = false; // Generation 与全部离散语义均匹配当前 Pending 请求

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bAwaitingActualStateConfirmation = false; // 选择已匹配，但尚未由后续 CMC 阶段条件确认

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bConfirmedByActualState = false; // 已由晚于选择的 CMC 阶段特定实际状态确认

    UPROPERTY(BlueprintReadOnly, Category = "Motion Matching|Post Selection")
    bool bValid = false; // 数据库、资产、时间和 Cost 均通过基础格式校验
};

/** 游戏线程发布给动画更新线程的持久请求快照；数据库只携带已预加载对象的弱引用。 */
struct FSKMotionMatchingPublishedSearchRequest
{
    int64 Generation = 0; // 搜索请求代次
    ESKMotionMatchingLocomotionPhase Phase = ESKMotionMatchingLocomotionPhase::Stationary; // 请求阶段
    ESKMotionMatchingGait Gait = ESKMotionMatchingGait::Run; // 请求步态
    ESKMotionMatchingStance Stance = ESKMotionMatchingStance::Standing; // 请求姿态
    ESKMotionMatchingRotationMode Mode = ESKMotionMatchingRotationMode::Free; // 请求旋转模式
    bool bPending = false; // 当前代次是否仍等待首次合法选择
    bool bForceSearch = false; // 本帧搜索前是否必须打断 Continuing Pose
    bool bInvalidateContinuingPose = false; // 有移动意图的新阶段请求是否必须清除旧 Continuing Pose
    TArray<TWeakObjectPtr<UPoseSearchDatabase>> Databases; // 与本代请求在游戏线程一并解析的合法数据库集合
};

/** 动画更新线程写入、游戏线程下一次更新消费的单槽选择消息。 */
struct FSKMotionMatchingPostSelectionMessage
{
    int64 RequestGeneration = 0; // 搜索结果对应的请求代次
    ESKMotionMatchingLocomotionPhase SelectedPhase = ESKMotionMatchingLocomotionPhase::Stationary; // 选择阶段
    ESKMotionMatchingGait SelectedGait = ESKMotionMatchingGait::Run; // 选择步态
    ESKMotionMatchingStance SelectedStance = ESKMotionMatchingStance::Standing; // 选择姿态
    ESKMotionMatchingRotationMode SelectedMode = ESKMotionMatchingRotationMode::Free; // 选择旋转模式
    TWeakObjectPtr<UObject> SelectedDatabase; // 数据库弱引用，只在游戏线程解析
    TWeakObjectPtr<UObject> SelectedAsset; // 动画资产弱引用，只在游戏线程解析
    float SelectedAssetTime = 0.f; // 选择时间，单位秒
    float WantedPlayRate = 1.f; // 搜索建议播放速率
    float SelectionCost = 0.f; // 搜索代价
    bool bJumpedToPose = false; // 是否从非 Continuing Pose 结果跳转
    bool bMirrored = false; // 搜索结果是否镜像
    bool bValid = false; // 单槽中是否存在尚未消费的完整消息
};

// ============================================================================
// USKMotionMatchingAnimInstance — Motion Matching 动画图值快照适配层
// 仅在游戏线程采集轨迹与移动查询参数，AnimGraph 只消费下列值属性。
// ============================================================================

UCLASS()
class SEKIRO_API USKMotionMatchingAnimInstance : public USKAnimInstance
{
    GENERATED_BODY()

public:
    // ── 动画生命周期 ──────────────────────────────────────────

    /** 初始化 Motion Matching 数据源缓存。 */
    virtual void NativeInitializeAnimation() override;

    /** 在游戏线程刷新 Motion Matching 值快照。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // ── 搜索控制 ──────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Motion Matching")
    void RequestMotionMatchingInterrupt(); // 请求 AnimGraph 在后续更新中强制重搜

    UFUNCTION(BlueprintCallable, Category = "Motion Matching")
    void ClearMotionMatchingInterrupt(); // 显式清除强制重搜请求

    /** 记录一次由正式 AnimGraph 转发的 PostSelection 结果。 */
    UFUNCTION(BlueprintCallable, Category = "Motion Matching")
    bool RecordMotionMatchingPostSelection(
        int64 RequestGeneration,
        ESKMotionMatchingLocomotionPhase SelectedPhase,
        ESKMotionMatchingGait SelectedGait,
        ESKMotionMatchingStance SelectedStance,
        ESKMotionMatchingRotationMode SelectedMode,
        UObject* SelectedDatabase,
        UObject* SelectedAsset,
        float SelectedAssetTime,
        float WantedPlayRate,
        float SelectionCost,
        bool bMirrored,
        bool bJumpedToPose);

    /** 在 Motion Matching 搜索前应用持久请求的重搜策略。 */
    UFUNCTION(BlueprintCallable, Category = "Motion Matching", meta = (BlueprintThreadSafe))
    void Update_MotionMatching_SearchRequest(
        const FAnimUpdateContext& Context,
        const FAnimNodeReference& Node);

    /** UE5.8 Motion Matching StateUpdated 的线程安全原生绑定入口。 */
    UFUNCTION(BlueprintCallable, Category = "Motion Matching", meta = (BlueprintThreadSafe))
    void Update_MotionMatching_PostSelection(
        const FAnimUpdateContext& Context,
        const FAnimNodeReference& Node);

    // ── Motion Matching（Blueprint 读取） ──────────────────────

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FSKMotionMatchingQuerySnapshot MotionMatchingQuerySnapshot; // 本次完整查询；消费前必须检查 bValid

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    bool bMotionMatchingQueryValid = false; // 查询数据是否完整；空中查询可有效但尚不代表存在合法搜索库

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    bool bMotionMatchingSearchBranchEnabled = false; // AnimGraph 搜索分支门禁；阶段或标签不准入时使用安全姿势

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    bool bMotionMatchingPoseBranchEnabled = false; // Pose 分支门禁；FullBody 动作期间保持节点更新，但不开放 Locomotion 搜索

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FSKMotionMatchingLocomotionStateSnapshot MotionMatchingLocomotionState; // Chooser 后续消费的正式语义状态

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FSKMotionMatchingSearchRequest MotionMatchingSearchRequest; // Chooser 与 PostSelection 后续消费的持久请求

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FSKMotionMatchingSelectionSnapshot MotionMatchingSelectionSnapshot; // 最近一次 PostSelection 选择事实

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FTransformTrajectory MotionMatchingTrajectory; // 角色局部的历史、现在与未来轨迹快照

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    FGameplayTagContainer LocomotionSearchTags; // 当前基础移动数据库筛选标签，首版保持为空

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTagContainer MotionMatchingOwnedGameplayTags; // 游戏线程从 ASC 复制的只读标签快照

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching|Tags")
    bool bMotionMatchingBlockedByGameplayTags = false; // 标签快照是否命中基础 Locomotion 阻塞集合

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    ESKMovementTier MotionMatchingMovementTier = ESKMovementTier::Idle; // 查询请求步态的旧枚举投影，不代表 Phase 或实际速度

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    bool bMotionMatchingLockedOn = false; // 查询 Facing 是否由锁定目标驱动

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    float MotionMatchingExpectedSpeed = 0.f; // QuerySnapshot 中统一计算的目标查询速度（cm/s）

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching")
    float MotionMatchingFacingYaw = 0.f; // 查询使用的有限世界 Facing Yaw（度）

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching|Orientation Warping")
    float MotionMatchingOrientationWarpingAngle = 0.f; // 上一轮 Coordinator 实际施加的姿势补偿角，单位度

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching|Orientation Warping")
    float MotionMatchingOrientationWarpingAlpha = 0.f; // Manual Orientation Warping 的严格 0/1 门禁

    UPROPERTY(Transient, BlueprintReadOnly, Category = "Motion Matching|Foot IK")
    float MotionMatchingFootIKAlpha = 0.f; // Foot Placement 与 Leg IK 共用的地面姿势修正权重

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Foot IK", meta = (ClampMin = "0.0"))
    float MotionMatchingFootIKGroundBlendInSpeed = 8.f; // 接地后每秒恢复的 Foot IK 权重

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Foot IK", meta = (ClampMin = "0.0"))
    float MotionMatchingFootIKAirBlendOutSpeed = 20.f; // 离地后每秒淡出的 Foot IK 权重

    // ── Chooser 配置 ─────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Chooser")
    TSoftObjectPtr<UChooserTable> LocomotionDatabaseChooser; // 按当前语义状态输出合法 Pose Search Database 数组

    // ── 搜索标签配置 ──────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag FreeModeSearchTag; // 自由移动数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag LockedModeSearchTag; // 锁定移动数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag WalkGaitSearchTag; // 步行数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag RunGaitSearchTag; // 跑步数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag SprintGaitSearchTag; // 冲刺数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag StandingStanceSearchTag; // 站立姿态数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTag CrouchingStanceSearchTag; // 蹲伏姿态数据库筛选标签

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Motion Matching|Tags")
    FGameplayTagContainer LocomotionBlockingTags; // 任一匹配时暂停基础 Locomotion 搜索，不代表 RootMotion Owner

private:
    // ── 游戏线程采集 ──────────────────────────────────────────

    void ConsumeMotionMatchingPostSelectionMailbox(); // 在下一次游戏线程动画更新消费选择消息
    void UpdateMotionMatchingSearchRequestTimeout(float DeltaSeconds); // 标记未收到 PostSelection 的请求超时
    void PublishMotionMatchingSearchRequest(); // 发布供动画更新线程关联结果的纯值请求
    void RefreshMotionMatchingCaches(); // 重新解析当前 Pawn 的轨迹组件
    void RefreshMotionMatchingGameplayTagSnapshot(); // 从 ASC 复制并版本化基础移动相关标签
    void RefreshMotionMatchingSnapshot(float DeltaSeconds); // 从有效组件刷新 AnimGraph 值快照
    void RefreshMotionMatchingOrientationWarping(); // 从已完成 RootMotion 协调快照发布 pose-only 残差
    void RefreshMotionMatchingFootIK(float DeltaSeconds); // 发布 Foot Placement 与 Leg IK 共用的姿势权重
    void UpdateMotionMatchingLocomotionState(float DeltaSeconds); // 从统一事实更新地面与空中阶段
    bool ResolveEligibleMotionMatchingDatabases(); // 在游戏线程解析并缓存当前请求的合法数据库集合
    bool ResolveMotionMatchingPivotFallback(bool bHasEligibleDatabase); // 当前组合无正式 Pivot 时退回 Start 协议
    void ConfirmMotionMatchingJumpStartFromActualState(bool bHasEligibleDatabase); // 用选择后的 CMC 样本和动画剩余时间确认起跳阶段
    void ConfirmMotionMatchingRecoveryFromActualState(bool bHasEligibleDatabase); // 用选择后的 CMC 样本确认落地或动作恢复
    void ConfirmMotionMatchingStartFromActualState(); // 用后续实际移动确认 StartRequested
    void ConfirmMotionMatchingPivotFromActualState(); // 用后续实际朝向确认 PivotRequested
    void ConfirmMotionMatchingSettledFromActualState(); // 用后续低速样本确认 Stationary 或 StopRequested
    void RefreshMotionMatchingSearchRequest(); // 离散语义变化时创建并保持请求

    // ── 运行时引用 ────────────────────────────────────────────

    UPROPERTY(Transient)
    TObjectPtr<USKMotionMatchingTrajectoryComponent> MotionMatchingTrajectoryComponent; // 当前 Pawn 的轨迹数据源缓存

    UPROPERTY(Transient)
    TObjectPtr<USKMovementComponent> MotionMatchingMovementComponent; // 当前 Pawn 的 RootMotion 所有权签发源

    UPROPERTY(Transient)
    TObjectPtr<UChooserTable> ResolvedLocomotionDatabaseChooser; // 游戏线程预加载并评估的 Locomotion Chooser

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPoseSearchDatabase>> ResolvedMotionMatchingDatabases; // 与当前请求语义一致的强引用候选集合

    bool bMotionMatchingInterruptRequested = false; // 保持到显式清除的手动重搜请求

    FCriticalSection MotionMatchingPostSelectionMutex; // 保护跨线程请求快照与单槽选择消息
    FSKMotionMatchingPublishedSearchRequest PublishedSearchRequest; // 动画更新线程只读的请求及候选集合快照
    FSKMotionMatchingPostSelectionMessage PendingPostSelectionMessage; // 等待游戏线程消费的最新消息
    int64 LastQueuedPostSelectionGeneration = 0; // 防止同一请求每帧覆盖确认基线
    FSKMotionMatchingPublishedSearchRequest LastAppliedSearchRequest; // 动画线程本次实际提交给节点的完整请求
    int64 LastReportedChooserFailureGeneration = MIN_int64; // 同一请求代次的 Chooser 空结果只报告一次
    int64 LastReportedIncompleteSelectionGeneration = MIN_int64; // 同一请求代次的不完整搜索结果只报告一次
    int64 LastReportedChooserSuccessGeneration = MIN_int64; // 同一请求代次的合法数据库集合只报告一次
    int64 MotionMatchingGameplayTagRevision = 0; // ASC 标签集合或阻塞结果变化时递增
    int32 CompletedSearchEvaluationFrameCount = 0; // 当前请求发布后已经经过的完整动画图求值机会数
    FName LastReportedMotionMatchingQueryInvalidReason = NAME_None; // 游戏线程最近已报告的查询失效原因
    bool bReportedSearchRequestNodeConversionFailure = false; // 搜索前节点引用转换失败只报告一次
    bool bReportedPostSelectionNodeConversionFailure = false; // 搜索后节点引用转换失败只报告一次
    int64 LastObservedMovementModeSampleId = 0; // 最近用于 MovementMode 边沿检测的实际样本号
    TEnumAsByte<EMovementMode> LastObservedMovementMode = MOVE_None; // 上一个不同实际样本的移动模式

    // ── Locomotion 状态配置 ───────────────────────────────────

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0"))
    float StationaryEnterSpeed = 5.f; // StopRequested 进入 Stationary 的实际水平速度上限，单位 cm/s

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0"))
    float MovingEnterSpeed = 15.f; // 后续选择确认时判断有效 RootMotion 推进的速度下限，单位 cm/s

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0"))
    float MinimumPhaseHoldSeconds = 0.05f; // 非紧急阶段切换的最短保持时间，单位秒

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0"))
    float AirborneApexEnterVerticalSpeed = 30.f; // 上升速度降至该阈值时进入 Apex，单位 cm/s

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0"))
    float AirborneApexExitVerticalSpeed = 60.f; // Apex 离开阈值，必须不小于进入阈值以形成迟滞，单位 cm/s

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0", ClampMax = "180"))
    float TurnStartEnterAngleDegrees = 100.f; // 实际移动与预测末端方向误差超过该值时请求 Start 回退

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|State", meta = (ClampMin = "0", ClampMax = "180"))
    float TurnStartExitAngleDegrees = 45.f; // 实际移动与预测末端方向误差收敛到该值后才允许确认 Moving

    UPROPERTY(EditDefaultsOnly, Category = "Motion Matching|Search Request", meta = (ClampMin = "0.01"))
    float SearchAcknowledgementTimeoutSeconds = 0.5f; // 等待同代合法 PostSelection 的超时秒数
};
