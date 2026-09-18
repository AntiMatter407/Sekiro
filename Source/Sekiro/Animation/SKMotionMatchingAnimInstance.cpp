#include "SKMotionMatchingAnimInstance.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimationAsset.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "Misc/ScopeLock.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Movement/SKMovementComponent.h"
#include "Movement/SKMotionMatchingTrajectoryComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSKMotionMatchingAnimInstance, Log, All);

namespace
{
/**
 * 将任意浮点输入转换为有限值。
 * 可在任意线程处理纯值；本函数不访问 UObject，也不改变外部状态。
 *
 * @param Value 待检查的浮点值。
 * @param Fallback Value 非有限时使用的有限回退值。
 * @return Value 有限时原样返回，否则返回 Fallback。
 */
float SKSanitizeMotionMatchingFloat(float Value, float Fallback)
{
    return FMath::IsFinite(Value) ? Value : Fallback;
}

/**
 * 判断当前语义阶段是否允许基础 Locomotion 向 Motion Matching 提交搜索。
 * 可在游戏线程或动画更新线程处理纯枚举值；不读取 UObject，也不改变请求状态。
 * JumpStart、Ascending、Apex、Falling、Landing 与 RecoveryRequested 允许由 Chooser 数据决定候选；
 * 旧 Airborne 继续失败关闭。ActionOwned 必须让位给当前 FullBody/Traversal 所有者。
 *
 * @param Phase 当前已发布的 Locomotion 语义阶段。
 * @return 当前阶段允许基础 Locomotion 搜索时返回 true。
 */
bool SKAllowsMotionMatchingLocomotionSearch(ESKMotionMatchingLocomotionPhase Phase)
{
    return Phase != ESKMotionMatchingLocomotionPhase::Airborne
        && Phase != ESKMotionMatchingLocomotionPhase::ActionOwned;
}

}

/**
 * 初始化父类动画采集后重置完整 Motion Matching 请求生命周期，缓存当前 Pawn 的轨迹组件，
 * 并同步解析 Locomotion Chooser 软引用。由 UE 动画生命周期在游戏线程调用；资产加载只发生在这里，
 * 动画更新线程只读取强引用缓存，本函数不发布轨迹快照。Chooser 尚未生成或路径失效时缓存为空，
 * 后续搜索会报告空候选。
 */
void USKMotionMatchingAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    MotionMatchingQuerySnapshot = FSKMotionMatchingQuerySnapshot();
    bMotionMatchingQueryValid = false;
    bMotionMatchingSearchBranchEnabled = false;
    bMotionMatchingPoseBranchEnabled = false;
    MotionMatchingLocomotionState = FSKMotionMatchingLocomotionStateSnapshot();
    MotionMatchingSearchRequest = FSKMotionMatchingSearchRequest();
    MotionMatchingSelectionSnapshot = FSKMotionMatchingSelectionSnapshot();
    MotionMatchingTrajectory = FTransformTrajectory();
    LocomotionSearchTags.Reset();
    MotionMatchingOwnedGameplayTags.Reset();
    bMotionMatchingBlockedByGameplayTags = false;
    MotionMatchingMovementTier = ESKMovementTier::Idle;
    bMotionMatchingLockedOn = false;
    MotionMatchingExpectedSpeed = 0.f;
    MotionMatchingFacingYaw = 0.f;
    MotionMatchingFootIKAlpha = 0.f;
    bMotionMatchingInterruptRequested = false;
    {
        FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
        PublishedSearchRequest = FSKMotionMatchingPublishedSearchRequest();
        PendingPostSelectionMessage = FSKMotionMatchingPostSelectionMessage();
        LastQueuedPostSelectionGeneration = 0;
    }
    LastAppliedSearchRequest = FSKMotionMatchingPublishedSearchRequest();
    ResolvedMotionMatchingDatabases.Reset();
    LastReportedChooserFailureGeneration = MIN_int64;
    LastReportedIncompleteSelectionGeneration = MIN_int64;
    LastReportedChooserSuccessGeneration = MIN_int64;
    MotionMatchingGameplayTagRevision = 0;
    CompletedSearchEvaluationFrameCount = 0;
    LastReportedMotionMatchingQueryInvalidReason = NAME_None;
    bReportedSearchRequestNodeConversionFailure = false;
    bReportedPostSelectionNodeConversionFailure = false;
    LastObservedMovementModeSampleId = 0;
    LastObservedMovementMode = MOVE_None;
    ResolvedLocomotionDatabaseChooser = nullptr;
    if (IsInGameThread())
    {
        ResolvedLocomotionDatabaseChooser = LocomotionDatabaseChooser.LoadSynchronous();
        RefreshMotionMatchingCaches();
    }
}

/**
 * 在父类完成通用动画采集后，刷新 Motion Matching 所需的纯值快照。
 * 由 UE 动画生命周期在游戏线程调用；本函数不在动画线程访问 Pawn、组件、Lua 或其他 UObject，
 * 也不会自动清除强制重搜请求，AnimGraph 可持续观察该请求直到显式清除。
 *
 * @param DeltaSeconds 当前动画更新步长，单位为秒；父类消费后本类不再用于积分。
 */
void USKMotionMatchingAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!IsInGameThread()) return;

    ConsumeMotionMatchingPostSelectionMailbox();

    APawn* OwnerPawn = TryGetPawnOwner();
    const bool bTrajectoryCacheInvalid = !IsValid(MotionMatchingTrajectoryComponent)
        || MotionMatchingTrajectoryComponent->GetOwner() != OwnerPawn
        || !IsValid(MotionMatchingMovementComponent)
        || MotionMatchingMovementComponent->GetOwner() != OwnerPawn;
    if (bTrajectoryCacheInvalid)
    {
        RefreshMotionMatchingCaches();
    }

    RefreshMotionMatchingGameplayTagSnapshot();
    RefreshMotionMatchingSnapshot(DeltaSeconds);
    RefreshMotionMatchingFootIK(DeltaSeconds);
    UpdateMotionMatchingSearchRequestTimeout(DeltaSeconds);
    PublishMotionMatchingSearchRequest();
}

/**
 * 设置持续的 Motion Matching 手动强制重搜请求。
 * 只能在游戏线程调用；重复请求幂等，不访问角色或组件。标志会在下一次游戏线程发布时
 * 合并到 PublishedSearchRequest.bForceSearch，并保持到显式清除。
 */
void USKMotionMatchingAnimInstance::RequestMotionMatchingInterrupt()
{
    if (!IsInGameThread()) return;

    bMotionMatchingInterruptRequested = true;
}

/**
 * 清除此前保持的 Motion Matching 手动强制重搜请求。
 * 只能在游戏线程调用；未请求时调用幂等。下一次游戏线程发布会撤销手动 ForceSearch，
 * 但不会覆盖仍处于 PendingSearch 的持久状态请求。
 */
void USKMotionMatchingAnimInstance::ClearMotionMatchingInterrupt()
{
    if (!IsInGameThread()) return;

    bMotionMatchingInterruptRequested = false;
}

/**
 * 记录正式 AnimGraph 转发的一次 PostSelection 结果，并校验它是否对应当前等待选择的持久请求。
 * 本函数只能在游戏线程调用。无效、过期、语义不匹配或已经进入实际状态确认阶段的结果会在
 * 修改 SelectionSnapshot 前被拒绝，确保首个合法选择建立的 CMC 确认基线不会被后续调用覆盖。
 * 成功只证明搜索选中了合法结果，不清除 Pending、不推进 Phase，也不把动画选择等同于 CMC 已产生实际位移。
 *
 * @param RequestGeneration AnimGraph 发起本次搜索时读取的 Search Request Generation，必须大于 0。
 * @param SelectedPhase 选中数据库声明的 Locomotion 阶段。
 * @param SelectedGait 选中数据库声明的目标速度族。
 * @param SelectedStance 选中数据库声明的站立或蹲伏姿态。
 * @param SelectedMode 选中数据库声明的自由或锁定旋转模式。
 * @param SelectedDatabase 产生搜索结果的数据库；不接管所有权，不允许为空。
 * @param SelectedAsset 搜索结果指向的动画资产；不接管所有权，不允许为空。
 * @param SelectedAssetTime 选中动画内的采样时间，单位秒；必须为非负有限值。
 * @param WantedPlayRate 搜索建议的播放速率；必须为正有限值。
 * @param SelectionCost 搜索返回的最终比较代价；必须为有限值。
 * @param bMirrored 搜索结果是否使用镜像姿势。
 * @param bJumpedToPose 本次选择是否触发了新 Pose 跳转。
 * 无 RootMotion 的 Ascending/Apex/Falling 姿势在选择事实成立时即可确认，因为它们不承诺新的位移；
 * 其余阶段继续等待后续实际状态确认。
 *
 * @return 数据格式有效、请求仍等待选择且 Generation 与全部离散语义匹配时返回 true；失败不修改快照。
 */
bool USKMotionMatchingAnimInstance::RecordMotionMatchingPostSelection(
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
    bool bJumpedToPose)
{
    if (!IsInGameThread()) return false;

    const bool bSelectionValid = RequestGeneration > 0
        && IsValid(SelectedDatabase)
        && IsValid(SelectedAsset)
        && FMath::IsFinite(SelectedAssetTime)
        && SelectedAssetTime >= 0.f
        && FMath::IsFinite(WantedPlayRate)
        && WantedPlayRate > 0.f
        && FMath::IsFinite(SelectionCost);
    if (!bSelectionValid) return false;

    const bool bRequestAcceptsSelection = MotionMatchingSearchRequest.bPending
        && (MotionMatchingSearchRequest.State == ESKMotionMatchingSearchRequestState::PendingSearch
            || MotionMatchingSearchRequest.State
                == ESKMotionMatchingSearchRequestState::PendingSearchNotAcknowledged)
        && RequestGeneration == MotionMatchingSearchRequest.Generation
        && SelectedPhase == MotionMatchingSearchRequest.RequestedPhase
        && SelectedGait == MotionMatchingSearchRequest.RequestedGait
        && SelectedStance == MotionMatchingSearchRequest.RequestedStance
        && SelectedMode == MotionMatchingSearchRequest.RequestedMode;
    if (!bRequestAcceptsSelection) return false;
    const bool bAcknowledgedAfterTimeout = MotionMatchingSearchRequest.State
        == ESKMotionMatchingSearchRequestState::PendingSearchNotAcknowledged;

    if (MotionMatchingSelectionSnapshot.SelectionRevision < MAX_int64)
    {
        ++MotionMatchingSelectionSnapshot.SelectionRevision;
    }
    MotionMatchingSelectionSnapshot.RequestGeneration = RequestGeneration;
    MotionMatchingSelectionSnapshot.SelectedPhase = SelectedPhase;
    MotionMatchingSelectionSnapshot.SelectedGait = SelectedGait;
    MotionMatchingSelectionSnapshot.SelectedStance = SelectedStance;
    MotionMatchingSelectionSnapshot.SelectedMode = SelectedMode;
    MotionMatchingSelectionSnapshot.SelectedDatabase = SelectedDatabase;
    MotionMatchingSelectionSnapshot.SelectedAsset = SelectedAsset;
    MotionMatchingSelectionSnapshot.SelectedAssetTime = SelectedAssetTime;
    MotionMatchingSelectionSnapshot.WantedPlayRate = WantedPlayRate;
    MotionMatchingSelectionSnapshot.SelectionCost = SelectionCost;
    const UWorld* World = GetWorld();
    MotionMatchingSelectionSnapshot.SelectionTimeSeconds = World ? World->GetTimeSeconds() : 0.0;
    MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection
        = MotionMatchingQuerySnapshot.ActualState.ActualSampleId;
    MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = 0;
    MotionMatchingSelectionSnapshot.bJumpedToPose = bJumpedToPose;
    MotionMatchingSelectionSnapshot.bMirrored = bMirrored;
    MotionMatchingSelectionSnapshot.bValid = true;
    MotionMatchingSelectionSnapshot.bMatchesPendingRequest = true;
    MotionMatchingSelectionSnapshot.bConfirmedByActualState = false;
    const bool bPoseOnlyAirborneSelection
        = SelectedPhase == ESKMotionMatchingLocomotionPhase::Ascending
        || SelectedPhase == ESKMotionMatchingLocomotionPhase::Apex
        || SelectedPhase == ESKMotionMatchingLocomotionPhase::Falling;
    if (!IsValid(MotionMatchingMovementComponent)) RefreshMotionMatchingCaches();
    if (IsValid(MotionMatchingMovementComponent))
        MotionMatchingMovementComponent->SetPoseOnlyAirborneMomentumActive(
            bPoseOnlyAirborneSelection);
    MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = !bPoseOnlyAirborneSelection;
    if (bPoseOnlyAirborneSelection)
    {
        MotionMatchingSearchRequest.bPending = false;
        MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
    }
    else
    {
        MotionMatchingSearchRequest.State
            = ESKMotionMatchingSearchRequestState::AwaitingActualStateConfirmation;
    }
    if (bAcknowledgedAfterTimeout)
    {
        UE_LOG(
            LogSKMotionMatchingAnimInstance,
            Display,
            TEXT("MotionMatchingSearchAcknowledgedLate: Generation=%lld, WaitElapsed=%f, Phase=%d, Gait=%d, Mode=%d, Stance=%d, Database='%s', Animation='%s'."),
            MotionMatchingSearchRequest.Generation,
            MotionMatchingSearchRequest.SearchWaitElapsedSeconds,
            static_cast<int32>(MotionMatchingSearchRequest.RequestedPhase),
            static_cast<int32>(MotionMatchingSearchRequest.RequestedGait),
            static_cast<int32>(MotionMatchingSearchRequest.RequestedMode),
            static_cast<int32>(MotionMatchingSearchRequest.RequestedStance),
            *SelectedDatabase->GetPathName(),
            *SelectedAsset->GetPathName());
    }
    return true;
}

/**
 * 在 Motion Matching 节点执行搜索前，把游戏线程随请求一并发布的合法数据库数组提交给节点。
 * UE 可在并行动画更新线程调用本函数；这里只读取受锁保护的请求快照与其中的数据库弱引用，
 * 不重新评估 Chooser，也不主动访问 Pawn、World、组件或 Lua。
 * Pending 请求的空结果会覆盖为显式空数据库集合并按代次报告一次错误，绝不沿用上一帧候选。
 * 请求强制重搜优先于数据库变化中断；有移动意图的新阶段会同时清除旧 Continuing Pose，
 * 防止 Idle 结果参与启动请求。匹配 PostSelection 后停止 ForceInterrupt，但仍持续刷新合法集合。
 *
 * @param Context 节点本次动画更新上下文；仅用于满足 UE5.8 Anim Node Function 原型，本函数不持有引用。
 * @param Node 即将更新的动画节点引用；必须可转换为 Motion Matching 节点，只在当前调用栈内使用。
 */
void USKMotionMatchingAnimInstance::Update_MotionMatching_SearchRequest(
    const FAnimUpdateContext& Context,
    const FAnimNodeReference& Node)
{
    (void)Context;

    FSKMotionMatchingPublishedSearchRequest SearchRequest;
    {
        FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
        SearchRequest = PublishedSearchRequest;
    }

    EAnimNodeReferenceConversionResult ConversionResult;
    const FMotionMatchingAnimNodeReference MotionMatchingNode
        = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, ConversionResult);
    if (ConversionResult != EAnimNodeReferenceConversionResult::Succeeded)
    {
        if (!bReportedSearchRequestNodeConversionFailure)
        {
            bReportedSearchRequestNodeConversionFailure = true;
            UE_LOG(
                LogSKMotionMatchingAnimInstance,
                Error,
                TEXT("MotionMatchingNodeConversionFailed: OnUpdate pre-search binding did not receive a Motion Matching node."));
        }
        return;
    }

    TArray<UPoseSearchDatabase*> Databases;
    Databases.Reserve(SearchRequest.Databases.Num());
    for (const TWeakObjectPtr<UPoseSearchDatabase>& PublishedDatabase : SearchRequest.Databases)
    {
        UPoseSearchDatabase* Database = PublishedDatabase.Get();
        if (IsValid(Database)) Databases.AddUnique(Database);
    }

    bool bDatabaseSetChanged = LastAppliedSearchRequest.Databases.Num() != Databases.Num();
    if (!bDatabaseSetChanged)
    {
        for (int32 Index = 0; Index < Databases.Num(); ++Index)
        {
            if (LastAppliedSearchRequest.Databases[Index].Get() != Databases[Index])
            {
                bDatabaseSetChanged = true;
                break;
            }
        }
    }

    EPoseSearchInterruptMode InterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
    if (SearchRequest.bForceSearch)
    {
        InterruptMode = SearchRequest.bInvalidateContinuingPose
            ? EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose
            : EPoseSearchInterruptMode::ForceInterrupt;
    }
    else if (Databases.IsEmpty())
    {
        InterruptMode =
            EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose;
    }
    else if (bDatabaseSetChanged)
    {
        InterruptMode = EPoseSearchInterruptMode::InterruptOnDatabaseChange;
    }

    UMotionMatchingAnimNodeLibrary::SetDatabasesToSearch(
        MotionMatchingNode,
        Databases,
        InterruptMode);

    // StateUpdated 必须使用这一轮真正提交给节点的请求，不能重新读取可能已前进的发布邮箱。
    LastAppliedSearchRequest = SearchRequest;

    if (SearchRequest.bPending
        && Databases.IsEmpty()
        && LastReportedChooserFailureGeneration != SearchRequest.Generation)
    {
        LastReportedChooserFailureGeneration = SearchRequest.Generation;
        UE_LOG(
            LogSKMotionMatchingAnimInstance,
            Error,
            TEXT("PublishedMotionMatchingDatabaseUnavailable: Pending request Generation=%lld, Phase=%d, Gait=%d, Mode=%d, Stance=%d contains no live database."),
            SearchRequest.Generation,
            static_cast<int32>(SearchRequest.Phase),
            static_cast<int32>(SearchRequest.Gait),
            static_cast<int32>(SearchRequest.Mode),
            static_cast<int32>(SearchRequest.Stance));
    }
    else if (!Databases.IsEmpty())
    {
        LastReportedChooserFailureGeneration = MIN_int64;
        if (LastReportedChooserSuccessGeneration != SearchRequest.Generation)
        {
            LastReportedChooserSuccessGeneration = SearchRequest.Generation;
            UE_LOG(
                LogSKMotionMatchingAnimInstance,
                Display,
                TEXT("MotionMatchingDatabasesReady: Generation=%lld, Count=%d, Phase=%d, Gait=%d, Mode=%d, Stance=%d."),
                SearchRequest.Generation,
                Databases.Num(),
                static_cast<int32>(SearchRequest.Phase),
                static_cast<int32>(SearchRequest.Gait),
                static_cast<int32>(SearchRequest.Mode),
                static_cast<int32>(SearchRequest.Stance));
        }
    }
}

/**
 * 在 Motion Matching 节点完成本次状态更新后提取搜索结果，并写入跨线程单槽邮箱。
 * UE 可在并行动画更新线程调用本函数；函数只读取节点当前结果与搜索前保存的已应用请求，
 * 不读取 Pawn、World、组件或游戏线程 UPROPERTY，也不直接推进 Locomotion 状态。
 * 结果数据库必须属于同次搜索前 Chooser 实际提交的集合，防止旧 Continuing Pose 或异常节点结果
 * 借用当前请求语义进入确认链。
 * 同一 Generation 只排队首个合法结果，避免每帧 Continuing Pose 覆盖实际移动确认基线。
 *
 * @param Context 节点本次动画更新上下文；仅用于满足 UE5.8 Anim Node Function 原型，本函数不持有引用。
 * @param Node 触发回调的动画节点引用；必须可转换为 Motion Matching 节点，只在当前调用栈内访问。
 */
void USKMotionMatchingAnimInstance::Update_MotionMatching_PostSelection(
    const FAnimUpdateContext& Context,
    const FAnimNodeReference& Node)
{
    (void)Context;

    // 与 Update_MotionMatching_SearchRequest 位于同一次节点更新链，必须沿用它实际应用的代次。
    const FSKMotionMatchingPublishedSearchRequest SearchRequest = LastAppliedSearchRequest;
    if (SearchRequest.Generation <= 0) return;

    EAnimNodeReferenceConversionResult ConversionResult;
    const FMotionMatchingAnimNodeReference MotionMatchingNode
        = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, ConversionResult);
    if (ConversionResult != EAnimNodeReferenceConversionResult::Succeeded)
    {
        if (!bReportedPostSelectionNodeConversionFailure)
        {
            bReportedPostSelectionNodeConversionFailure = true;
            UE_LOG(
                LogSKMotionMatchingAnimInstance,
                Error,
                TEXT("MotionMatchingNodeConversionFailed: StateUpdated post-selection binding did not receive a Motion Matching node."));
        }
        return;
    }

    FPoseSearchBlueprintResult SearchResult;
    bool bIsResultValid = false;
    UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(
        MotionMatchingNode,
        SearchResult,
        bIsResultValid);
    bool bSelectedDatabaseEligible = !SearchRequest.Databases.IsEmpty();
    if (bSelectedDatabaseEligible)
    {
        bSelectedDatabaseEligible = false;
        for (const TWeakObjectPtr<UPoseSearchDatabase>& EligibleDatabase : SearchRequest.Databases)
        {
            if (EligibleDatabase.Get() == SearchResult.SelectedDatabase.Get())
            {
                bSelectedDatabaseEligible = true;
                break;
            }
        }
    }
    const bool bHasCompleteResult = bIsResultValid
        && SearchResult.SelectedDatabase != nullptr
        && SearchResult.SelectedAnim != nullptr
        && bSelectedDatabaseEligible
        && FMath::IsFinite(SearchResult.SelectedTime)
        && SearchResult.SelectedTime >= 0.f
        && FMath::IsFinite(SearchResult.WantedPlayRate)
        && SearchResult.WantedPlayRate > 0.f
        && FMath::IsFinite(SearchResult.SearchCost);
    if (!bHasCompleteResult)
    {
        if (LastReportedIncompleteSelectionGeneration != SearchRequest.Generation)
        {
            LastReportedIncompleteSelectionGeneration = SearchRequest.Generation;
            UE_LOG(
                LogSKMotionMatchingAnimInstance,
                Warning,
                TEXT("MotionMatchingSelectionIncomplete: Generation=%lld, Pending=%s, ResultValid=%s, Database=%s, DatabaseEligible=%s, Animation=%s, SelectedTime=%f, WantedPlayRate=%f, SearchCost=%f."),
                SearchRequest.Generation,
                SearchRequest.bPending ? TEXT("true") : TEXT("false"),
                bIsResultValid ? TEXT("true") : TEXT("false"),
                SearchResult.SelectedDatabase != nullptr ? TEXT("valid") : TEXT("null"),
                bSelectedDatabaseEligible ? TEXT("true") : TEXT("false"),
                SearchResult.SelectedAnim != nullptr ? TEXT("valid") : TEXT("null"),
                SearchResult.SelectedTime,
                SearchResult.WantedPlayRate,
                SearchResult.SearchCost);
        }
        return;
    }

    FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
    if (!SearchRequest.bPending
        || SearchRequest.Generation <= 0
        || !PublishedSearchRequest.bPending
        || PublishedSearchRequest.Generation != SearchRequest.Generation
        || LastQueuedPostSelectionGeneration == SearchRequest.Generation)
    {
        return;
    }

    PendingPostSelectionMessage.RequestGeneration = SearchRequest.Generation;
    PendingPostSelectionMessage.SelectedPhase = SearchRequest.Phase;
    PendingPostSelectionMessage.SelectedGait = SearchRequest.Gait;
    PendingPostSelectionMessage.SelectedStance = SearchRequest.Stance;
    PendingPostSelectionMessage.SelectedMode = SearchRequest.Mode;
    PendingPostSelectionMessage.SelectedDatabase
        = const_cast<UPoseSearchDatabase*>(SearchResult.SelectedDatabase.Get());
    PendingPostSelectionMessage.SelectedAsset = SearchResult.SelectedAnim.Get();
    PendingPostSelectionMessage.SelectedAssetTime = SearchResult.SelectedTime;
    PendingPostSelectionMessage.WantedPlayRate = SearchResult.WantedPlayRate;
    PendingPostSelectionMessage.SelectionCost = SearchResult.SearchCost;
    PendingPostSelectionMessage.bJumpedToPose = !SearchResult.bIsContinuingPoseSearch;
    PendingPostSelectionMessage.bMirrored = SearchResult.bIsMirrored;
    PendingPostSelectionMessage.bValid = true;
    LastQueuedPostSelectionGeneration = SearchRequest.Generation;
    UE_LOG(
        LogSKMotionMatchingAnimInstance,
        Display,
        TEXT("MotionMatchingSelectionReady: Generation=%lld, SelectedTime=%f, WantedPlayRate=%f, SearchCost=%f."),
        SearchRequest.Generation,
        SearchResult.SelectedTime,
        SearchResult.WantedPlayRate,
        SearchResult.SearchCost);
}

/**
 * 在下一次游戏线程动画更新开始时取走动画线程发布的选择消息，并交给正式请求校验入口。
 * 本函数只能在游戏线程调用；锁内只复制并清空单槽，UObject 弱引用解析和状态写入均在锁外完成。
 * 已失效或已被新请求取代的消息会由 RecordMotionMatchingPostSelection 拒绝，不回写动画线程状态。
 */
void USKMotionMatchingAnimInstance::ConsumeMotionMatchingPostSelectionMailbox()
{
    FSKMotionMatchingPostSelectionMessage Message;
    {

        FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
        if (!PendingPostSelectionMessage.bValid) return;

        Message = PendingPostSelectionMessage;
        PendingPostSelectionMessage = FSKMotionMatchingPostSelectionMessage();
    }

    RecordMotionMatchingPostSelection(
        Message.RequestGeneration,
        Message.SelectedPhase,
        Message.SelectedGait,
        Message.SelectedStance,
        Message.SelectedMode,
        Message.SelectedDatabase.Get(),
        Message.SelectedAsset.Get(),
        Message.SelectedAssetTime,
        Message.WantedPlayRate,
        Message.SelectionCost,
        Message.bMirrored,
        Message.bJumpedToPose);
}

/**
 * 检查当前 PendingSearch 是否在期限内收到同代合法 PostSelection，并发布显式超时状态。
 * 本函数只能在游戏线程调用；请求尚未发布给 AnimGraph 时不计时，并至少保留两次完整求值机会，
 * 避免数据库切换首帧仍返回旧 Continuing Pose 或低帧率大 DeltaSeconds 时产生假超时。
 * 超时只发生一次并输出带 Generation、离散语义和等待时间的结构化错误；它不清除 Pending、
 * 不切换 Locomotion Phase，也不停止后续重搜。查询无效、已进入实际状态确认、已确认或已超时的
 * 请求保持现状；迟到结果仍可由 RecordMotionMatchingPostSelection 恢复到实际状态确认阶段。
 *
 * @param DeltaSeconds 本次动画更新步长，单位秒；非有限值或负值按 0 处理。
 */
void USKMotionMatchingAnimInstance::UpdateMotionMatchingSearchRequestTimeout(float DeltaSeconds)
{
    if (!bMotionMatchingQueryValid
        || !MotionMatchingSearchRequest.bPending
        || MotionMatchingSearchRequest.State != ESKMotionMatchingSearchRequestState::PendingSearch)
    {
        return;
    }

    bool bCurrentRequestWasPublished = false;
    {
        FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
        bCurrentRequestWasPublished = PublishedSearchRequest.bPending
            && PublishedSearchRequest.Generation == MotionMatchingSearchRequest.Generation;
    }
    if (!bCurrentRequestWasPublished) return;

    if (CompletedSearchEvaluationFrameCount < MAX_int32)
    {
        ++CompletedSearchEvaluationFrameCount;
    }

    const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
        ? FMath::Max(0.f, DeltaSeconds)
        : 0.f;
    MotionMatchingSearchRequest.SearchWaitElapsedSeconds = FMath::Min(
        TNumericLimits<float>::Max(),
        MotionMatchingSearchRequest.SearchWaitElapsedSeconds + SafeDeltaSeconds);
    const float SafeTimeoutSeconds = FMath::IsFinite(SearchAcknowledgementTimeoutSeconds)
        ? FMath::Max(0.01f, SearchAcknowledgementTimeoutSeconds)
        : 0.5f;
    constexpr int32 MinimumCompletedEvaluationFramesBeforeTimeout = 2;
    if (CompletedSearchEvaluationFrameCount < MinimumCompletedEvaluationFramesBeforeTimeout
        || MotionMatchingSearchRequest.SearchWaitElapsedSeconds < SafeTimeoutSeconds)
    {
        return;
    }

    MotionMatchingSearchRequest.State
        = ESKMotionMatchingSearchRequestState::PendingSearchNotAcknowledged;
    UE_LOG(
        LogSKMotionMatchingAnimInstance,
        Error,
        TEXT("MotionMatchingSearchNotAcknowledged: Generation=%lld, PhaseRevision=%lld, QueryRevision=%lld, Phase=%d, Gait=%d, Mode=%d, Stance=%d, CompletedEvaluationFrames=%d, WaitElapsed=%f, Timeout=%f, Pending=true."),
        MotionMatchingSearchRequest.Generation,
        MotionMatchingSearchRequest.PhaseRevision,
        MotionMatchingSearchRequest.QueryRevision,
        static_cast<int32>(MotionMatchingSearchRequest.RequestedPhase),
        static_cast<int32>(MotionMatchingSearchRequest.RequestedGait),
        static_cast<int32>(MotionMatchingSearchRequest.RequestedMode),
        static_cast<int32>(MotionMatchingSearchRequest.RequestedStance),
        CompletedSearchEvaluationFrameCount,
        MotionMatchingSearchRequest.SearchWaitElapsedSeconds,
        SafeTimeoutSeconds);
}

/**
 * 将游戏线程当前持久请求及同代 Chooser 候选发布为动画更新线程可安全读取的快照。
 * 本函数只能在游戏线程、Query 与请求刷新完成后调用；无效查询不会发布可排队结果的 Pending 请求。
 * 尚无合法候选的阶段、让位动作所有权的 ActionOwned，以及项目配置的阻塞标签，同样禁止发布搜索，
 * 避免沿用旧数据库；标签阻塞本身不改变 RootMotion 所有权。
 * JumpStart 总是清除离地前的旧 Continuing Pose；StartRequested、Moving 或 PivotRequested 的新请求
 * 在仍有移动意图时执行相同处理。该纯值决策在游戏线程完成，动画线程无需直接读取 QuerySnapshot。
 * 函数不清空已经排队的旧消息，旧消息消费时仍需通过当前 Generation 的最终校验。
 */
void USKMotionMatchingAnimInstance::PublishMotionMatchingSearchRequest()
{
    FScopeLock MailboxLock(&MotionMatchingPostSelectionMutex);
    PublishedSearchRequest.Generation = MotionMatchingSearchRequest.Generation;
    PublishedSearchRequest.Phase = MotionMatchingSearchRequest.RequestedPhase;
    PublishedSearchRequest.Gait = MotionMatchingSearchRequest.RequestedGait;
    PublishedSearchRequest.Stance = MotionMatchingSearchRequest.RequestedStance;
    PublishedSearchRequest.Mode = MotionMatchingSearchRequest.RequestedMode;
    const bool bHasMatchingSelection = MotionMatchingSelectionSnapshot.bValid
        && MotionMatchingSelectionSnapshot.bMatchesPendingRequest
        && MotionMatchingSelectionSnapshot.RequestGeneration == MotionMatchingSearchRequest.Generation;
    PublishedSearchRequest.bPending = bMotionMatchingSearchBranchEnabled
        && MotionMatchingSearchRequest.bPending
        && !bHasMatchingSelection;
    const ESKMotionMatchingLocomotionPhase RequestedPhase = MotionMatchingSearchRequest.RequestedPhase;
    const bool bRequiresFreshGroundMovementPose
        = RequestedPhase == ESKMotionMatchingLocomotionPhase::StartRequested
        || RequestedPhase == ESKMotionMatchingLocomotionPhase::Moving
        || RequestedPhase == ESKMotionMatchingLocomotionPhase::PivotRequested;
    PublishedSearchRequest.bInvalidateContinuingPose = PublishedSearchRequest.bPending
        && (RequestedPhase == ESKMotionMatchingLocomotionPhase::JumpStart
            || (MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
                && bRequiresFreshGroundMovementPose));
    PublishedSearchRequest.bForceSearch = bMotionMatchingSearchBranchEnabled
        && (bMotionMatchingInterruptRequested || PublishedSearchRequest.bPending);
    PublishedSearchRequest.Databases.Reset();
    if (bMotionMatchingSearchBranchEnabled)
    {
        PublishedSearchRequest.Databases.Reserve(ResolvedMotionMatchingDatabases.Num());
        for (const TObjectPtr<UPoseSearchDatabase>& Database : ResolvedMotionMatchingDatabases)
        {
            if (IsValid(Database))
            {
                PublishedSearchRequest.Databases.Add(
                    TWeakObjectPtr<UPoseSearchDatabase>(Database.Get()));
            }
        }
    }
}

/**
 * 从当前动画 Pawn 重新解析轨迹组件与 Movement 所有权签发源。
 * 只能在游戏线程调用；Pawn 无效或组件缺失时清空对应缓存，不创建组件也不调用 Lua。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingCaches()
{
    APawn* OwnerPawn = TryGetPawnOwner();
    if (!IsValid(OwnerPawn))
    {
        MotionMatchingTrajectoryComponent = nullptr;
        MotionMatchingMovementComponent = nullptr;
        return;
    }

    MotionMatchingTrajectoryComponent = OwnerPawn->FindComponentByClass<USKMotionMatchingTrajectoryComponent>();
    MotionMatchingMovementComponent = OwnerPawn->FindComponentByClass<USKMovementComponent>();
}

/**
 * 在游戏线程从当前 Pawn 的 ASC 复制完整 GameplayTag 集合，并用项目配置的阻塞集合执行 Any 匹配。
 * 本函数不保留 ASC 引用、不添加或移除标签，也不把标签命中解释为 RootMotion Owner；标签集合或
 * 阻塞结果变化时递增独立 Revision，使解除阻塞后能够创建新的持久搜索请求。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingGameplayTagSnapshot()
{
    FGameplayTagContainer NewOwnedTags;
    const APawn* OwnerPawn = TryGetPawnOwner();
    const IAbilitySystemInterface* AbilitySystemOwner = Cast<IAbilitySystemInterface>(OwnerPawn);
    const UAbilitySystemComponent* AbilitySystem = AbilitySystemOwner
        ? AbilitySystemOwner->GetAbilitySystemComponent()
        : nullptr;
    if (AbilitySystem)
    {
        AbilitySystem->GetOwnedGameplayTags(NewOwnedTags);
    }

    const bool bNewBlocked = !LocomotionBlockingTags.IsEmpty()
        && NewOwnedTags.HasAny(LocomotionBlockingTags);
    const bool bTagsChanged = NewOwnedTags != MotionMatchingOwnedGameplayTags
        || bNewBlocked != bMotionMatchingBlockedByGameplayTags;
    MotionMatchingOwnedGameplayTags = MoveTemp(NewOwnedTags);
    bMotionMatchingBlockedByGameplayTags = bNewBlocked;
    if (bTagsChanged && MotionMatchingGameplayTagRevision < MAX_int64)
    {
        ++MotionMatchingGameplayTagRevision;
    }
}

/**
 * 在游戏线程构建一次正式 QuerySnapshot，所有查询属性仅从该值副本派生。
 * 不再逐字段读取 Movement；无效结果清空轨迹和标签，不将失败伪装成有效静止查询。
 * 现有平铺属性仅作为图接入过渡，完整快照及 bValid 是后续动画消费者的权威输入。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingSnapshot(float DeltaSeconds)
{
    LocomotionSearchTags.Reset();
    ResolvedMotionMatchingDatabases.Reset();
    bMotionMatchingSearchBranchEnabled = false;
    bMotionMatchingPoseBranchEnabled = false;
    RefreshMotionMatchingOrientationWarping();
    MotionMatchingQuerySnapshot = IsValid(MotionMatchingTrajectoryComponent)
        ? MotionMatchingTrajectoryComponent->BuildMotionMatchingQuerySnapshot()
        : FSKMotionMatchingQuerySnapshot();
    if (!IsValid(MotionMatchingTrajectoryComponent))
    {
        MotionMatchingQuerySnapshot.InvalidReason = TEXT("MissingTrajectoryComponent");
    }
    bMotionMatchingQueryValid = MotionMatchingQuerySnapshot.bValid;
    MotionMatchingTrajectory = FTransformTrajectory();

    MotionMatchingMovementTier = ESKMovementTier::Idle;
    bMotionMatchingLockedOn = false;
    MotionMatchingExpectedSpeed = 0.f;
    const float SafeActorYaw = SKSanitizeMotionMatchingFloat(ActorYaw, 0.f);
    MotionMatchingFacingYaw = FRotator::NormalizeAxis(SafeActorYaw);

    if (!bMotionMatchingQueryValid)
    {
        const FName InvalidReason = MotionMatchingQuerySnapshot.InvalidReason.IsNone()
            ? FName(TEXT("Unspecified"))
            : MotionMatchingQuerySnapshot.InvalidReason;
        if (LastReportedMotionMatchingQueryInvalidReason != InvalidReason)
        {
            LastReportedMotionMatchingQueryInvalidReason = InvalidReason;
            UE_LOG(
                LogSKMotionMatchingAnimInstance,
                Warning,
                TEXT("MotionMatchingQueryInvalid: %s."),
                *InvalidReason.ToString());
        }
        UpdateMotionMatchingLocomotionState(DeltaSeconds);
        // FullBody Slot 即使在轨迹暂不可用时也必须进入更新链，动作结束后才能无缝恢复基础姿势。
        bMotionMatchingPoseBranchEnabled = MotionMatchingLocomotionState.Phase
            == ESKMotionMatchingLocomotionPhase::ActionOwned;
        // 请求保持 Pending，但查询门禁关闭时不驱动 Motion Matching 分支。
        return;
    }
    if (!LastReportedMotionMatchingQueryInvalidReason.IsNone())
    {
        UE_LOG(
            LogSKMotionMatchingAnimInstance,
            Display,
            TEXT("MotionMatchingQueryReady: IntentRevision=%lld, ActualSampleId=%lld, QueryRevision=%lld, TrajectorySamples=%d."),
            MotionMatchingQuerySnapshot.Intent.IntentRevision,
            MotionMatchingQuerySnapshot.ActualState.ActualSampleId,
            MotionMatchingQuerySnapshot.QueryRevision,
            MotionMatchingQuerySnapshot.DesiredTrajectory.Samples.Num());
        LastReportedMotionMatchingQueryInvalidReason = NAME_None;
    }

    const FSKMotionMatchingIntentInput& Intent = MotionMatchingQuerySnapshot.Intent.Intent;
    MotionMatchingTrajectory = MotionMatchingQuerySnapshot.DesiredTrajectory;
    MotionMatchingExpectedSpeed = MotionMatchingQuerySnapshot.ExpectedSpeed;
    bMotionMatchingLockedOn = Intent.RotationMode == ESKMotionMatchingRotationMode::Locked;
    switch (Intent.RequestedGait)
    {
    case ESKMotionMatchingGait::Walk: MotionMatchingMovementTier = ESKMovementTier::Walk; break;
    case ESKMotionMatchingGait::Run: MotionMatchingMovementTier = ESKMovementTier::Run; break;
    case ESKMotionMatchingGait::Sprint: MotionMatchingMovementTier = ESKMovementTier::Sprint; break;
    }
    const FGameplayTag& ModeSearchTag = bMotionMatchingLockedOn
        ? LockedModeSearchTag
        : FreeModeSearchTag;
    if (ModeSearchTag.IsValid()) LocomotionSearchTags.AddTag(ModeSearchTag);

    const bool bCrouching = Intent.RequestedStance == ESKMotionMatchingStance::Crouching;
    const FGameplayTag& StanceSearchTag = bCrouching
        ? CrouchingStanceSearchTag
        : StandingStanceSearchTag;
    if (StanceSearchTag.IsValid()) LocomotionSearchTags.AddTag(StanceSearchTag);

    const FGameplayTag* GaitSearchTag = nullptr;
    switch (MotionMatchingMovementTier)
    {
    case ESKMovementTier::Walk:
    case ESKMovementTier::Crouch:
        GaitSearchTag = &WalkGaitSearchTag;
        break;
    case ESKMovementTier::Run:
        GaitSearchTag = &RunGaitSearchTag;
        break;
    case ESKMovementTier::Sprint:
        GaitSearchTag = &SprintGaitSearchTag;
        break;
    case ESKMovementTier::Idle:
    default:
        break;
    }
    if (GaitSearchTag && GaitSearchTag->IsValid()) LocomotionSearchTags.AddTag(*GaitSearchTag);

    // Free 无输入时与预测器一致，采用本次实际状态朝向，不重新读取 Actor。
    MotionMatchingFacingYaw = !bMotionMatchingLockedOn && !MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        ? MotionMatchingQuerySnapshot.QueryOriginWS.Rotator().Yaw : Intent.DesiredFacingYaw;
    UpdateMotionMatchingLocomotionState(DeltaSeconds);
    // Chooser 绑定读取 MotionMatchingSearchRequest，因此必须先把当前离散语义写入请求再评估候选。
    RefreshMotionMatchingSearchRequest();
    bool bHasEligibleDatabase = MotionMatchingLocomotionState.bValid
        && SKAllowsMotionMatchingLocomotionSearch(MotionMatchingLocomotionState.Phase)
        && ResolveEligibleMotionMatchingDatabases();
    if (ResolveMotionMatchingPivotFallback(bHasEligibleDatabase))
    {
        RefreshMotionMatchingSearchRequest();
        bHasEligibleDatabase = ResolveEligibleMotionMatchingDatabases();
    }
    const int64 PhaseRevisionBeforeConfirmation = MotionMatchingLocomotionState.PhaseRevision;
    ConfirmMotionMatchingJumpStartFromActualState(bHasEligibleDatabase);
    ConfirmMotionMatchingRecoveryFromActualState(bHasEligibleDatabase);
    ConfirmMotionMatchingPivotFromActualState();
    ConfirmMotionMatchingStartFromActualState();
    ConfirmMotionMatchingSettledFromActualState();
    if (MotionMatchingLocomotionState.PhaseRevision != PhaseRevisionBeforeConfirmation)
    {
        RefreshMotionMatchingSearchRequest();
        bHasEligibleDatabase = MotionMatchingLocomotionState.bValid
            && SKAllowsMotionMatchingLocomotionSearch(MotionMatchingLocomotionState.Phase)
            && ResolveEligibleMotionMatchingDatabases();
    }
    bMotionMatchingSearchBranchEnabled = bHasEligibleDatabase
        && !MotionMatchingLocomotionState.bBlockedByGameplayTags;
    if (!bMotionMatchingSearchBranchEnabled)
    {
        MotionMatchingSearchRequest.bPending = false;
        MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Inactive;
        MotionMatchingSelectionSnapshot.bMatchesPendingRequest = false;
        MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
        ResolvedMotionMatchingDatabases.Reset();
    }
    // 搜索准入关闭不等于停止整个 Pose 子图；FullBody 动作期间 Slot 与其 Source 必须持续更新。
    bMotionMatchingPoseBranchEnabled = bMotionMatchingSearchBranchEnabled
        || MotionMatchingLocomotionState.Phase == ESKMotionMatchingLocomotionPhase::ActionOwned;
    RefreshMotionMatchingOrientationWarping();
}

/**
 * 在游戏线程把上一轮 Coordinator 已经实际施加的水平 RootMotion 转向角发布给 AnimGraph。
 * 该值只驱动 Manual Orientation Warping 的骨骼姿势补偿，不重新推导输入方向、不修改 RootMotion，
 * 也不读取本帧尚未完成的动画 Evaluate 结果。搜索分支关闭、当前没有移动意图、排他所有权生效、
 * 上一协调样本无效或 Steering 未实际参与时都会发布 Angle=0、Alpha=0。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingOrientationWarping()
{
    MotionMatchingOrientationWarpingAngle = 0.f;
    MotionMatchingOrientationWarpingAlpha = 0.f;
    if (!bMotionMatchingSearchBranchEnabled
        || !MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        || !MotionMatchingLocomotionState.bLocomotionRootMotionAllowed
        || !IsValid(MotionMatchingMovementComponent)) return;

    const FSKRootMotionCoordinationSnapshot CoordinationSnapshot =
        MotionMatchingMovementComponent->GetRootMotionCoordinationSnapshot();
    const float SteeringAngle = CoordinationSnapshot.SteeringTranslationYawDeltaDegrees;
    if (!CoordinationSnapshot.bValid
        || !CoordinationSnapshot.bHasAnimationRootMotion
        || CoordinationSnapshot.bSteeringSuppressed
        || CoordinationSnapshot.EffectiveRootMotionOwner != ESKRootMotionOwnerType::Locomotion
        || !FMath::IsFinite(SteeringAngle)
        || FMath::IsNearlyZero(SteeringAngle, UE_KINDA_SMALL_NUMBER)) return;

    MotionMatchingOrientationWarpingAngle = FRotator::NormalizeAxis(SteeringAngle);
    MotionMatchingOrientationWarpingAlpha = 1.f;
}

/**
 * 在游戏线程根据已发布的 Locomotion 状态推进 Foot Placement 与 Leg IK 的共享 Alpha。
 * 有效地面 Locomotion 按配置速度淡入，空中按独立速度淡出；状态无效、Gameplay Tag 阻塞、
 * FullBody/Traversal 夺取 RootMotion 或进入 ActionOwned 时立即归零，禁止脚部求解污染排他动作。
 * 本函数只发布 AnimGraph 纯值，不执行地面 Trace、不修改骨骼、RootMotion、Actor 或胶囊。
 *
 * @param DeltaSeconds 当前游戏线程动画更新步长，单位秒；非法值按 0 处理。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingFootIK(float DeltaSeconds)
{
    const bool bExclusiveOwnerActive = !MotionMatchingLocomotionState.bLocomotionRootMotionAllowed
        || MotionMatchingLocomotionState.EffectiveRootMotionOwner != ESKRootMotionOwnerType::Locomotion
        || MotionMatchingLocomotionState.Phase == ESKMotionMatchingLocomotionPhase::ActionOwned;
    if (!MotionMatchingLocomotionState.bValid
        || MotionMatchingLocomotionState.bBlockedByGameplayTags
        || bExclusiveOwnerActive)
    {
        MotionMatchingFootIKAlpha = 0.f;
        return;
    }

    const bool bGrounded = MotionMatchingLocomotionState.MovementMode == MOVE_Walking
        || MotionMatchingLocomotionState.MovementMode == MOVE_NavWalking;
    const float TargetAlpha = bGrounded ? 1.f : 0.f;
    const float ConfiguredSpeed = bGrounded
        ? MotionMatchingFootIKGroundBlendInSpeed
        : MotionMatchingFootIKAirBlendOutSpeed;
    const float SafeSpeed = FMath::IsFinite(ConfiguredSpeed) ? FMath::Max(0.f, ConfiguredSpeed) : 0.f;
    const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    MotionMatchingFootIKAlpha = FMath::FInterpConstantTo(
        FMath::Clamp(MotionMatchingFootIKAlpha, 0.f, 1.f),
        TargetAlpha,
        SafeDeltaSeconds,
        SafeSpeed);
}

/**
 * 从同一 QuerySnapshot 携带的 ActualState 与 Movement 权威所有权快照更新 Locomotion 阶段，不读取动画节点。
 * 只能在游戏线程、查询属性发布后调用。CMC 实际样本会按竖直速度发布
 * JumpStart/Ascending/Apex/Falling/Landing；空中 Query 可以有效，但搜索分支仍由阶段白名单和
 * Chooser 实际可用结果共同门禁，尚无专用数据库的阶段不会误用地面数据库。
 * FullBody/Traversal 所有权优先于 MovementMode 进入 ActionOwned；释放边沿先进入
 * RecoveryRequested，再由 Chooser 数据门禁与恢复确认协议回到现有 Start/Stop/Stationary 阶段。
 * Moving 在持续有意图时不会因碰墙降速退出；但当前实际移动方向与预测末端速度方向超过进入阈值时，
 * 会退出 Loop 并以 StartRequested 承载尚无正式 Pivot 数据的方向切换。该回退带进入/退出迟滞，
 * StartRequested 也不会仅凭速度自行确认，后续必须由 PostSelection、方向收敛与有效 RootMotion
 * 推进协议共同将其切回 Moving。
 *
 * @param DeltaSeconds 本次动画更新时间，单位秒；非法或负值按 0 处理。
 */
void USKMotionMatchingAnimInstance::UpdateMotionMatchingLocomotionState(float DeltaSeconds)
{
    const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    MotionMatchingLocomotionState.QueryRevision = MotionMatchingQuerySnapshot.QueryRevision;
    MotionMatchingLocomotionState.IntentRevision = MotionMatchingQuerySnapshot.Intent.IntentRevision;
    MotionMatchingLocomotionState.ActualSampleId = MotionMatchingQuerySnapshot.ActualState.ActualSampleId;
    MotionMatchingLocomotionState.GameplayTagRevision = MotionMatchingGameplayTagRevision;
    MotionMatchingLocomotionState.bBlockedByGameplayTags = bMotionMatchingBlockedByGameplayTags;
    const bool bHasOwnershipSnapshot = IsValid(MotionMatchingMovementComponent);
    MotionMatchingLocomotionState.bHasRootMotionOwnershipSnapshot = bHasOwnershipSnapshot;
    if (bHasOwnershipSnapshot)
    {
        const FSKRootMotionOwnershipSnapshot OwnershipSnapshot =
            MotionMatchingMovementComponent->GetRootMotionOwnershipSnapshot();
        MotionMatchingLocomotionState.RootMotionOwnershipRevision = OwnershipSnapshot.Revision;
        MotionMatchingLocomotionState.EffectiveRootMotionOwner =
            OwnershipSnapshot.EffectiveRootMotionOwner;
        MotionMatchingLocomotionState.bUpperBodyActionActive = OwnershipSnapshot.bUpperBodyActive;
        MotionMatchingLocomotionState.bLocomotionRootMotionAllowed =
            OwnershipSnapshot.bLocomotionRootMotionAllowed;
    }
    const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
    MotionMatchingLocomotionState.MovementMode = ActualState.MovementMode;
    MotionMatchingLocomotionState.CustomMovementMode = ActualState.CustomMovementMode;
    MotionMatchingLocomotionState.bJustLanded = false;
    const float ActualVerticalSpeed = ActualState.ActualVelocityWS.Z;
    FVector ActualVelocity = ActualState.ActualVelocityWS;
    ActualVelocity.Z = 0.f;
    MotionMatchingLocomotionState.ActualPlanarSpeed = ActualVelocity.Size();
    FVector PredictedFutureVelocity = bMotionMatchingQueryValid
        ? MotionMatchingQuerySnapshot.PredictedFutureVelocityWS
        : FVector::ZeroVector;
    PredictedFutureVelocity.Z = 0.f;
    MotionMatchingLocomotionState.PredictedFuturePlanarSpeed = bMotionMatchingQueryValid
        ? MotionMatchingQuerySnapshot.PredictedFuturePlanarSpeed
        : 0.f;
    MotionMatchingLocomotionState.FutureDirectionErrorDegrees = 0.f;
    MotionMatchingLocomotionState.bValid = ActualState.bHasCompletedMovement
        && ActualState.ActualSampleId > 0
        && !ActualState.ActorTransformWS.ContainsNaN()
        && FMath::IsFinite(MotionMatchingLocomotionState.ActualPlanarSpeed)
        && FMath::IsFinite(ActualVerticalSpeed);
    const bool bExclusiveRootMotionOwner = bHasOwnershipSnapshot
        && !MotionMatchingLocomotionState.bLocomotionRootMotionAllowed;
    const bool bRootMotionOwnerReleased = bHasOwnershipSnapshot
        && !bExclusiveRootMotionOwner
        && MotionMatchingLocomotionState.Phase == ESKMotionMatchingLocomotionPhase::ActionOwned;
    if (bExclusiveRootMotionOwner || bRootMotionOwnerReleased)
    {
        MotionMatchingLocomotionState.PhaseElapsedSeconds = FMath::Min(
            TNumericLimits<float>::Max(),
            MotionMatchingLocomotionState.PhaseElapsedSeconds + SafeDeltaSeconds);
        const ESKMotionMatchingLocomotionPhase OwnershipPhase = bExclusiveRootMotionOwner
            ? ESKMotionMatchingLocomotionPhase::ActionOwned
            : ESKMotionMatchingLocomotionPhase::RecoveryRequested;
        if (MotionMatchingLocomotionState.PhaseRevision == 0
            || MotionMatchingLocomotionState.Phase != OwnershipPhase)
        {
            if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
            {
                ++MotionMatchingLocomotionState.PhaseRevision;
            }
            MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
            MotionMatchingLocomotionState.Phase = OwnershipPhase;
            MotionMatchingLocomotionState.TransitionReason = bExclusiveRootMotionOwner
                ? ESKMotionMatchingPhaseTransitionReason::RootMotionOwnerAcquired
                : ESKMotionMatchingPhaseTransitionReason::RootMotionOwnerReleased;
            MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
            MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
        }
        return;
    }
    if (!bHasOwnershipSnapshot
        && MotionMatchingLocomotionState.Phase == ESKMotionMatchingLocomotionPhase::ActionOwned) return;
    if (!MotionMatchingLocomotionState.bValid) return;

    const bool bIsGrounded = ActualState.MovementMode == MOVE_Walking
        || ActualState.MovementMode == MOVE_NavWalking;
    bool bJustLeftGround = false;
    bool bJustLanded = false;
    if (ActualState.ActualSampleId != LastObservedMovementModeSampleId)
    {
        const bool bHadPreviousMovementMode = LastObservedMovementModeSampleId > 0;
        const bool bWasGrounded = LastObservedMovementMode == MOVE_Walking
            || LastObservedMovementMode == MOVE_NavWalking;
        bJustLeftGround = bHadPreviousMovementMode && bWasGrounded && !bIsGrounded;
        bJustLanded = bHadPreviousMovementMode && !bWasGrounded && bIsGrounded;
        LastObservedMovementModeSampleId = ActualState.ActualSampleId;
        LastObservedMovementMode = ActualState.MovementMode;
    }
    MotionMatchingLocomotionState.bJustLanded = bJustLanded;

    MotionMatchingLocomotionState.PhaseElapsedSeconds = FMath::Min(
        TNumericLimits<float>::Max(),
        MotionMatchingLocomotionState.PhaseElapsedSeconds + SafeDeltaSeconds);
    ESKMotionMatchingLocomotionPhase NextPhase = MotionMatchingLocomotionState.Phase;
    ESKMotionMatchingPhaseTransitionReason NextTransitionReason
        = ESKMotionMatchingPhaseTransitionReason::None;
    const bool bHasMoveIntent = MotionMatchingQuerySnapshot.Intent.bHasMoveIntent;
    const float SafeStationarySpeed = FMath::Max(0.f, StationaryEnterSpeed);
    const float SafeTurnEnterAngle = FMath::Clamp(TurnStartEnterAngleDegrees, 0.f, 180.f);
    const float SafeTurnExitAngle = FMath::Min(
        FMath::Clamp(TurnStartExitAngleDegrees, 0.f, 180.f),
        SafeTurnEnterAngle);
    const float SafeMinimumHold = FMath::Max(0.f, MinimumPhaseHoldSeconds);
    const float SafeApexEnterSpeed = FMath::Max(0.f, AirborneApexEnterVerticalSpeed);
    const float SafeApexExitSpeed = FMath::Max(
        SafeApexEnterSpeed,
        AirborneApexExitVerticalSpeed);
    const bool bCanLeavePhase = MotionMatchingLocomotionState.PhaseRevision == 0
        || MotionMatchingLocomotionState.PhaseElapsedSeconds >= SafeMinimumHold;
    const bool bFutureMovementRequested = bHasMoveIntent
        && MotionMatchingLocomotionState.PredictedFuturePlanarSpeed > UE_KINDA_SMALL_NUMBER;
    FVector ActualTranslation = MotionMatchingQuerySnapshot.ActualState.ActualTranslationDeltaWS;
    ActualTranslation.Z = 0.f;
    const bool bHasReliableActualDirection
        = MotionMatchingQuerySnapshot.ActualState.bHasCompletedMovement
        && !ActualTranslation.IsNearlyZero(0.1f);
    const float CurrentMovementYaw = bHasReliableActualDirection
        ? ActualTranslation.Rotation().Yaw
        : ActualState.ActorTransformWS.Rotator().Yaw;
    MotionMatchingLocomotionState.MoveDirectionErrorDegrees = bHasMoveIntent
        ? FMath::Abs(FMath::FindDeltaAngleDegrees(
            CurrentMovementYaw,
            MotionMatchingQuerySnapshot.Intent.DesiredMoveYaw))
        : 0.f;
    MotionMatchingLocomotionState.FutureDirectionErrorDegrees = bFutureMovementRequested
        ? FMath::Abs(FMath::FindDeltaAngleDegrees(
            CurrentMovementYaw,
            PredictedFutureVelocity.Rotation().Yaw))
        : 0.f;

    if (!bIsGrounded)
    {
        MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
        if (bJustLeftGround)
        {
            NextPhase = ActualVerticalSpeed > SafeApexEnterSpeed
                ? ESKMotionMatchingLocomotionPhase::JumpStart
                : ESKMotionMatchingLocomotionPhase::Falling;
            NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MovementModeLeftGround;
        }
        else
        {
            switch (MotionMatchingLocomotionState.Phase)
            {
            case ESKMotionMatchingLocomotionPhase::JumpStart:
                // 起跳退出由同代 PostSelection、动画剩余时间与其后的 CMC 完成样本共同确认。
                break;
            case ESKMotionMatchingLocomotionPhase::Ascending:
                if (ActualVerticalSpeed <= SafeApexEnterSpeed)
                {
                    NextPhase = ActualVerticalSpeed < -SafeApexExitSpeed
                        ? ESKMotionMatchingLocomotionPhase::Falling
                        : ESKMotionMatchingLocomotionPhase::Apex;
                    NextTransitionReason = ActualVerticalSpeed < -SafeApexExitSpeed
                        ? ESKMotionMatchingPhaseTransitionReason::AirborneFalling
                        : ESKMotionMatchingPhaseTransitionReason::AirborneApexReached;
                }
                break;
            case ESKMotionMatchingLocomotionPhase::Apex:
                if (ActualVerticalSpeed > SafeApexExitSpeed)
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Ascending;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneAscending;
                }
                else if (ActualVerticalSpeed < -SafeApexExitSpeed)
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Falling;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneFalling;
                }
                break;
            case ESKMotionMatchingLocomotionPhase::Falling:
                if (ActualVerticalSpeed > SafeApexExitSpeed)
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Ascending;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneAscending;
                }
                break;
            default:
                if (ActualVerticalSpeed > SafeApexEnterSpeed)
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Ascending;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneAscending;
                }
                else if (ActualVerticalSpeed < -SafeApexEnterSpeed)
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Falling;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneFalling;
                }
                else
                {
                    NextPhase = ESKMotionMatchingLocomotionPhase::Apex;
                    NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneApexReached;
                }
                break;
            }
        }
    }
    else if (bJustLanded)
    {
        MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
        NextPhase = ESKMotionMatchingLocomotionPhase::Landing;
        NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MovementModeLanded;
    }
    else
    {
        switch (MotionMatchingLocomotionState.Phase)
        {
        case ESKMotionMatchingLocomotionPhase::Stationary:
            if (bFutureMovementRequested)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = ESKMotionMatchingLocomotionPhase::StartRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MoveIntentStarted;
            }
            break;
        case ESKMotionMatchingLocomotionPhase::StartRequested:
            if (!bFutureMovementRequested && bCanLeavePhase)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = MotionMatchingLocomotionState.ActualPlanarSpeed <= SafeStationarySpeed
                    ? ESKMotionMatchingLocomotionPhase::Stationary
                    : ESKMotionMatchingLocomotionPhase::StopRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MoveIntentReleased;
            }
            break;
        case ESKMotionMatchingLocomotionPhase::Moving:
            if (!bFutureMovementRequested && bCanLeavePhase)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = ESKMotionMatchingLocomotionPhase::StopRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MoveIntentReleased;
            }
            else if (bCanLeavePhase
                && MotionMatchingLocomotionState.FutureDirectionErrorDegrees >= SafeTurnEnterAngle)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = ESKMotionMatchingLocomotionPhase::PivotRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::DirectionChangeRequested;
            }
            break;
        case ESKMotionMatchingLocomotionPhase::PivotRequested:
            if (!bFutureMovementRequested && bCanLeavePhase)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = MotionMatchingLocomotionState.ActualPlanarSpeed <= SafeStationarySpeed
                    ? ESKMotionMatchingLocomotionPhase::Stationary
                    : ESKMotionMatchingLocomotionPhase::StopRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MoveIntentReleased;
            }
            break;
        case ESKMotionMatchingLocomotionPhase::StopRequested:
            if (bFutureMovementRequested && bCanLeavePhase)
            {
                MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
                NextPhase = ESKMotionMatchingLocomotionPhase::StartRequested;
                NextTransitionReason = ESKMotionMatchingPhaseTransitionReason::MoveIntentStarted;
            }
            break;
        case ESKMotionMatchingLocomotionPhase::Landing:
        case ESKMotionMatchingLocomotionPhase::RecoveryRequested:
            // 恢复阶段的退出由 Chooser 数据门禁、PostSelection 与后续 CMC 完成样本共同确认。
            break;
        default:
            break;
        }
    }

    if (MotionMatchingLocomotionState.PhaseRevision == 0
        || NextPhase != MotionMatchingLocomotionState.Phase)
    {
        const bool bInitializingPhase = MotionMatchingLocomotionState.PhaseRevision == 0;
        if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
        {
            ++MotionMatchingLocomotionState.PhaseRevision;
        }
        MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
        MotionMatchingLocomotionState.Phase = NextPhase;
        MotionMatchingLocomotionState.TransitionReason = bInitializingPhase
            ? ESKMotionMatchingPhaseTransitionReason::Initialized
            : NextTransitionReason;
        MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
    }
}

/**
 * 在游戏线程用已经刷新的 MotionMatchingSearchRequest 反射状态评估 Locomotion Chooser，并缓存
 * 当前请求的完整合法数据库集合。具体覆盖完全由 Lua 生成的 Chooser 数据扩展；动画线程不再
 * 重复评估可变化的 AnimInstance 状态，只消费随同请求发布的数据库弱引用快照。
 *
 * @return 当前 Chooser 有效且至少返回一个有效 Pose Search Database 时返回 true。
 */
bool USKMotionMatchingAnimInstance::ResolveEligibleMotionMatchingDatabases()
{
    ResolvedMotionMatchingDatabases.Reset();
    if (!IsInGameThread() || !IsValid(ResolvedLocomotionDatabaseChooser)) return false;

    const TArray<UObject*> ChooserResults = UChooserFunctionLibrary::EvaluateChooserMulti(
        this,
        ResolvedLocomotionDatabaseChooser,
        UPoseSearchDatabase::StaticClass());
    for (UObject* ChooserResult : ChooserResults)
    {
        UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(ChooserResult);
        if (IsValid(Database)) ResolvedMotionMatchingDatabases.AddUnique(Database);
    }
    return !ResolvedMotionMatchingDatabases.IsEmpty();
}

/**
 * 当 Moving 已请求大角度 Pivot、但 Chooser 对当前 Gait/Mode/Stance 没有正式 Pivot 数据时，
 * 在创建搜索请求前确定性退回既有 StartRequested 协议。该回退只根据当前 Chooser 空结果执行，
 * 不复制项目侧组合表，也不让无数据的 PivotRequested 卡住状态机；未来 Lua 增加合法行后会自然停留
 * 在 PivotRequested。只能在游戏线程、当前语义已经评估 Chooser 后调用。
 *
 * @param bHasEligibleDatabase 当前 Phase/Gait/Mode/Stance 的 Chooser 是否返回有效数据库。
 * @return 发生 PivotRequested 到 StartRequested 回退时返回 true，调用方需重新评估 Chooser。
 */
bool USKMotionMatchingAnimInstance::ResolveMotionMatchingPivotFallback(bool bHasEligibleDatabase)
{
    if (!IsInGameThread()
        || bHasEligibleDatabase
        || MotionMatchingLocomotionState.Phase != ESKMotionMatchingLocomotionPhase::PivotRequested)
    {
        return false;
    }

    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = ESKMotionMatchingLocomotionPhase::StartRequested;
    MotionMatchingLocomotionState.TransitionReason
        = ESKMotionMatchingPhaseTransitionReason::PivotCandidateUnavailable;
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
    MotionMatchingLocomotionState.bTurnStartFallbackActive = true;
    return true;
}

/**
 * 确认 JumpStart 已由合法数据库选中、选中动画的剩余播放窗口已结束，并且选择之后至少完成了一个
 * CMC 移动样本。只能在游戏线程、当前 Query 与状态更新后调用；本函数不采样动画 Pose，也不产生
 * 位移，只在现有 RootMotion 消费链完成起跳窗口后按 CMC 实际竖直速度推进空中语义阶段。
 * 当前 Phase/Gait/Mode/Stance 没有 Chooser 候选时，在最短保持时间后安全退出 JumpStart，避免首版
 * 单组合覆盖把其他语义永久卡住；该回退不会伪造 PostSelection 或实际移动确认。
 *
 * @param bHasEligibleDatabase 当前游戏线程 Chooser 是否至少返回一个有效 Pose Search Database。
 */
void USKMotionMatchingAnimInstance::ConfirmMotionMatchingJumpStartFromActualState(
    bool bHasEligibleDatabase)
{
    if (!bMotionMatchingQueryValid
        || !MotionMatchingLocomotionState.bValid
        || MotionMatchingLocomotionState.Phase
            != ESKMotionMatchingLocomotionPhase::JumpStart)
    {
        return;
    }

    const bool bMinimumHoldSatisfied = MotionMatchingLocomotionState.PhaseElapsedSeconds
        >= FMath::Max(0.f, MinimumPhaseHoldSeconds);
    if (!bMinimumHoldSatisfied) return;

    if (bHasEligibleDatabase)
    {
        const bool bSelectionMatchesJumpStartRequest = MotionMatchingSearchRequest.bPending
            && MotionMatchingSearchRequest.RequestedPhase
                == ESKMotionMatchingLocomotionPhase::JumpStart
            && MotionMatchingSelectionSnapshot.bValid
            && MotionMatchingSelectionSnapshot.bMatchesPendingRequest
            && MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation
            && MotionMatchingSelectionSnapshot.RequestGeneration
                == MotionMatchingSearchRequest.Generation;
        if (!bSelectionMatchesJumpStartRequest) return;

        const UAnimationAsset* SelectedAnimation = Cast<UAnimationAsset>(
            MotionMatchingSelectionSnapshot.SelectedAsset);
        const UWorld* World = GetWorld();
        if (!IsValid(SelectedAnimation) || !World) return;

        const float PlayLength = SelectedAnimation->GetPlayLength();
        const float SelectedTime = MotionMatchingSelectionSnapshot.SelectedAssetTime;
        const float WantedPlayRate = MotionMatchingSelectionSnapshot.WantedPlayRate;
        if (!FMath::IsFinite(PlayLength)
            || PlayLength <= 0.f
            || !FMath::IsFinite(SelectedTime)
            || !FMath::IsFinite(WantedPlayRate)
            || WantedPlayRate <= 0.f)
        {
            return;
        }

        const float RemainingPlaybackSeconds
            = FMath::Max(0.f, PlayLength - FMath::Clamp(SelectedTime, 0.f, PlayLength))
            / WantedPlayRate;
        const double RequiredCompletionTime
            = MotionMatchingSelectionSnapshot.SelectionTimeSeconds
            + static_cast<double>(RemainingPlaybackSeconds);
        if (World->GetTimeSeconds() < RequiredCompletionTime) return;

        const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
        const bool bCompletedAirborneSampleAfterSelection = ActualState.bHasCompletedMovement
            && ActualState.ActualSampleId
                > MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection
            && ActualState.MovementMode == MOVE_Falling;
        if (!bCompletedAirborneSampleAfterSelection) return;

        MotionMatchingSearchRequest.bPending = false;
        MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
        MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = ActualState.ActualSampleId;
        MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
        MotionMatchingSelectionSnapshot.bConfirmedByActualState = true;
    }

    const float ActualVerticalSpeed = MotionMatchingQuerySnapshot.ActualState.ActualVelocityWS.Z;
    const float SafeApexEnterSpeed = FMath::Max(0.f, AirborneApexEnterVerticalSpeed);
    ESKMotionMatchingLocomotionPhase NextPhase = ESKMotionMatchingLocomotionPhase::Apex;
    ESKMotionMatchingPhaseTransitionReason TransitionReason
        = ESKMotionMatchingPhaseTransitionReason::AirborneApexReached;
    if (ActualVerticalSpeed > SafeApexEnterSpeed)
    {
        NextPhase = ESKMotionMatchingLocomotionPhase::Ascending;
        TransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneAscending;
    }
    else if (ActualVerticalSpeed < -SafeApexEnterSpeed)
    {
        NextPhase = ESKMotionMatchingLocomotionPhase::Falling;
        TransitionReason = ESKMotionMatchingPhaseTransitionReason::AirborneFalling;
    }

    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = NextPhase;
    MotionMatchingLocomotionState.TransitionReason = TransitionReason;
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
}

/**
 * 确认 Landing 或 RecoveryRequested 已由合法数据库选中，并且选择之后至少完成了一个仍处于地面的
 * CMC 移动样本。只能在游戏线程、当前 Query 与状态更新后调用；合法候选存在时必须满足同代
 * PostSelection、最短保持时间和新完成样本，随后按意图与实际速度进入 StartRequested、Stationary
 * 或 StopRequested。当前语义没有 Chooser 候选时保留原有最短保持后的安全恢复，避免尚未覆盖的组合
 * 永久卡在恢复阶段；该回退不伪造选择确认，也不会把任何具体 Gait/Mode/Stance 组合硬编码进 C++。
 *
 * @param bHasEligibleDatabase 当前游戏线程 Chooser 是否至少返回一个有效 Pose Search Database。
 */
void USKMotionMatchingAnimInstance::ConfirmMotionMatchingRecoveryFromActualState(
    bool bHasEligibleDatabase)
{
    const ESKMotionMatchingLocomotionPhase CurrentPhase = MotionMatchingLocomotionState.Phase;
    const bool bSupportsRecoveryConfirmation
        = CurrentPhase == ESKMotionMatchingLocomotionPhase::Landing
        || CurrentPhase == ESKMotionMatchingLocomotionPhase::RecoveryRequested;
    if (!bMotionMatchingQueryValid
        || !MotionMatchingLocomotionState.bValid
        || !bSupportsRecoveryConfirmation)
    {
        return;
    }

    const bool bMinimumHoldSatisfied = MotionMatchingLocomotionState.PhaseElapsedSeconds
        >= FMath::Max(0.f, MinimumPhaseHoldSeconds);
    if (!bMinimumHoldSatisfied) return;

    if (bHasEligibleDatabase)
    {
        const bool bSelectionMatchesRecoveryRequest = MotionMatchingSearchRequest.bPending
            && MotionMatchingSearchRequest.RequestedPhase == CurrentPhase
            && MotionMatchingSelectionSnapshot.bValid
            && MotionMatchingSelectionSnapshot.bMatchesPendingRequest
            && MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation
            && MotionMatchingSelectionSnapshot.RequestGeneration
                == MotionMatchingSearchRequest.Generation;
        if (!bSelectionMatchesRecoveryRequest) return;

        const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
        const bool bGroundedAfterSelection = ActualState.bHasCompletedMovement
            && ActualState.ActualSampleId
                > MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection
            && (ActualState.MovementMode == MOVE_Walking
                || ActualState.MovementMode == MOVE_NavWalking);
        if (!bGroundedAfterSelection) return;

        MotionMatchingSearchRequest.bPending = false;
        MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
        MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = ActualState.ActualSampleId;
        MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
        MotionMatchingSelectionSnapshot.bConfirmedByActualState = true;
    }

    const bool bFutureMovementRequested = MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        && MotionMatchingLocomotionState.PredictedFuturePlanarSpeed > UE_KINDA_SMALL_NUMBER;
    const bool bActualMovementSettled = MotionMatchingLocomotionState.ActualPlanarSpeed
        <= FMath::Max(0.f, StationaryEnterSpeed);
    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = bFutureMovementRequested
        ? ESKMotionMatchingLocomotionPhase::StartRequested
        : (bActualMovementSettled
            ? ESKMotionMatchingLocomotionPhase::Stationary
            : ESKMotionMatchingLocomotionPhase::StopRequested);
    if (CurrentPhase == ESKMotionMatchingLocomotionPhase::Landing)
    {
        MotionMatchingLocomotionState.TransitionReason = bFutureMovementRequested
            ? ESKMotionMatchingPhaseTransitionReason::LandingRecoveryStarted
            : (bActualMovementSettled
                ? ESKMotionMatchingPhaseTransitionReason::LandingRecoverySettled
                : ESKMotionMatchingPhaseTransitionReason::LandingRecoveryStopping);
    }
    else
    {
        MotionMatchingLocomotionState.TransitionReason = bFutureMovementRequested
            ? ESKMotionMatchingPhaseTransitionReason::RootMotionRecoveryStarted
            : (bActualMovementSettled
                ? ESKMotionMatchingPhaseTransitionReason::RootMotionRecoverySettled
                : ESKMotionMatchingPhaseTransitionReason::RootMotionRecoveryStopping);
    }
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
}

/**
 * 使用晚于 PostSelection 的 CMC 完成移动样本确认 StartRequested 已实际开始推进。
 * 只能在游戏线程、状态与 QuerySnapshot 更新后调用；本步只处理 StartRequested，
 * 不推断 Idle、Stop、Pivot 或动作阶段，也不尝试从 CMC 速度反推动画资产身份。
 * 只有请求仍为同一 Generation、选择仍在等待、保持时间满足、存在新完成样本，且该样本
 * 同时具有非零实际水平位移和足够水平速度时，才清除当前请求并进入 Moving。若本次 Start
 * 是大角度回退，还必须先把实际移动方向误差收敛到退出阈值，防止重新进入同一 Run Loop。
 */
void USKMotionMatchingAnimInstance::ConfirmMotionMatchingStartFromActualState()
{
    if (!bMotionMatchingQueryValid
        || !MotionMatchingLocomotionState.bValid
        || MotionMatchingLocomotionState.Phase != ESKMotionMatchingLocomotionPhase::StartRequested
        || !MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        || MotionMatchingLocomotionState.PredictedFuturePlanarSpeed <= UE_KINDA_SMALL_NUMBER
        || !MotionMatchingSearchRequest.bPending
        || !MotionMatchingSelectionSnapshot.bValid
        || !MotionMatchingSelectionSnapshot.bMatchesPendingRequest
        || !MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation
        || MotionMatchingSelectionSnapshot.RequestGeneration != MotionMatchingSearchRequest.Generation)
    {
        return;
    }

    const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
    FVector ActualTranslation = ActualState.ActualTranslationDeltaWS;
    ActualTranslation.Z = 0.f;
    const bool bHasNewCompletedSample = ActualState.bHasCompletedMovement
        && ActualState.ActualSampleId > MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection;
    const bool bHasActualAdvancement = bHasNewCompletedSample
        && ActualTranslation.SizeSquared() > FMath::Square(UE_KINDA_SMALL_NUMBER)
        && MotionMatchingLocomotionState.ActualPlanarSpeed >= FMath::Max(0.f, MovingEnterSpeed);
    const bool bMinimumHoldSatisfied = MotionMatchingLocomotionState.PhaseElapsedSeconds
        >= FMath::Max(0.f, MotionMatchingSearchRequest.MinimumHoldTime);
    const float SafeTurnExitAngle = FMath::Min(
        FMath::Clamp(TurnStartExitAngleDegrees, 0.f, 180.f),
        FMath::Clamp(TurnStartEnterAngleDegrees, 0.f, 180.f));
    const bool bTurnDirectionSatisfied = !MotionMatchingLocomotionState.bTurnStartFallbackActive
        || MotionMatchingLocomotionState.FutureDirectionErrorDegrees <= SafeTurnExitAngle;
    if (!bHasActualAdvancement || !bMinimumHoldSatisfied || !bTurnDirectionSatisfied) return;

    MotionMatchingSearchRequest.bPending = false;
    MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
    MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = ActualState.ActualSampleId;
    MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
    MotionMatchingSelectionSnapshot.bConfirmedByActualState = true;
    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = ESKMotionMatchingLocomotionPhase::Moving;
    MotionMatchingLocomotionState.TransitionReason
        = ESKMotionMatchingPhaseTransitionReason::PostSelectionMovementConfirmed;
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
    MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
}

/**
 * 使用晚于 PostSelection 的 CMC 完成样本确认 PivotRequested 已把角色实际朝向收敛到未来移动方向。
 * Pivot 可以原地转身，因此不要求非零水平位移；但必须保持同代请求、合法选择、最短阶段时间，
 * 并观察到选择之后的新完成样本。Free 使用完成样本 ActorYaw 对 DesiredMoveYaw 的误差；Locked
 * 保持 DesiredFacing 所有权，只按实际位移方向收敛判断。确认后进入 Moving，并由下一代请求选择合法 Loop。
 */
void USKMotionMatchingAnimInstance::ConfirmMotionMatchingPivotFromActualState()
{
    if (!bMotionMatchingQueryValid
        || !MotionMatchingLocomotionState.bValid
        || MotionMatchingLocomotionState.Phase != ESKMotionMatchingLocomotionPhase::PivotRequested
        || !MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        || MotionMatchingLocomotionState.PredictedFuturePlanarSpeed <= UE_KINDA_SMALL_NUMBER
        || !MotionMatchingSearchRequest.bPending
        || MotionMatchingSearchRequest.RequestedPhase != ESKMotionMatchingLocomotionPhase::PivotRequested
        || !MotionMatchingSelectionSnapshot.bValid
        || !MotionMatchingSelectionSnapshot.bMatchesPendingRequest
        || !MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation
        || MotionMatchingSelectionSnapshot.RequestGeneration != MotionMatchingSearchRequest.Generation)
    {
        return;
    }

    const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
    const bool bHasNewCompletedSample = ActualState.bHasCompletedMovement
        && ActualState.ActualSampleId > MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection;
    const bool bMinimumHoldSatisfied = MotionMatchingLocomotionState.PhaseElapsedSeconds
        >= FMath::Max(0.f, MotionMatchingSearchRequest.MinimumHoldTime);
    const float SafeTurnExitAngle = FMath::Min(
        FMath::Clamp(TurnStartExitAngleDegrees, 0.f, 180.f),
        FMath::Clamp(TurnStartEnterAngleDegrees, 0.f, 180.f));
    const bool bLockedMode = MotionMatchingQuerySnapshot.Intent.Intent.RotationMode
        == ESKMotionMatchingRotationMode::Locked;
    const float PivotDirectionErrorDegrees = bLockedMode
        ? MotionMatchingLocomotionState.FutureDirectionErrorDegrees
        : FMath::Abs(FMath::FindDeltaAngleDegrees(
            ActualState.ActorTransformWS.Rotator().Yaw,
            MotionMatchingQuerySnapshot.Intent.DesiredMoveYaw));
    const bool bDirectionConverged = PivotDirectionErrorDegrees <= SafeTurnExitAngle;
    if (!bHasNewCompletedSample || !bMinimumHoldSatisfied || !bDirectionConverged) return;

    MotionMatchingSearchRequest.bPending = false;
    MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
    MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = ActualState.ActualSampleId;
    MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
    MotionMatchingSelectionSnapshot.bConfirmedByActualState = true;
    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = ESKMotionMatchingLocomotionPhase::Moving;
    MotionMatchingLocomotionState.TransitionReason
        = ESKMotionMatchingPhaseTransitionReason::PostSelectionPivotConfirmed;
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
    MotionMatchingLocomotionState.bTurnStartFallbackActive = false;
}

/**
 * 使用晚于 PostSelection 的 CMC 低速完成样本确认 Stationary 或 StopRequested 选择。
 * 只能在游戏线程、状态与 QuerySnapshot 更新后调用；无输入、请求与当前 Phase 完全一致、
 * 最短保持时间满足且新样本速度不高于 StationaryEnterSpeed 时才确认。
 * Stationary 仅结束本代 Idle 请求；StopRequested 还会进入 Stationary，由后续刷新创建新的 Idle 请求。
 * 本函数不要求非零位移，因为 Idle 与 Stop 的正确完成状态就是胶囊已经稳定或正在归零。
 */
void USKMotionMatchingAnimInstance::ConfirmMotionMatchingSettledFromActualState()
{
    const ESKMotionMatchingLocomotionPhase CurrentPhase = MotionMatchingLocomotionState.Phase;
    const bool bSupportsSettledConfirmation
        = CurrentPhase == ESKMotionMatchingLocomotionPhase::Stationary
        || CurrentPhase == ESKMotionMatchingLocomotionPhase::StopRequested;
    if (!bMotionMatchingQueryValid
        || !MotionMatchingLocomotionState.bValid
        || !bSupportsSettledConfirmation
        || MotionMatchingQuerySnapshot.Intent.bHasMoveIntent
        || !MotionMatchingSearchRequest.bPending
        || MotionMatchingSearchRequest.RequestedPhase != CurrentPhase
        || !MotionMatchingSelectionSnapshot.bValid
        || !MotionMatchingSelectionSnapshot.bMatchesPendingRequest
        || !MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation
        || MotionMatchingSelectionSnapshot.RequestGeneration != MotionMatchingSearchRequest.Generation)
    {
        return;
    }

    const FSKMotionMatchingActualState& ActualState = MotionMatchingQuerySnapshot.ActualState;
    const bool bHasNewCompletedSample = ActualState.bHasCompletedMovement
        && ActualState.ActualSampleId > MotionMatchingSelectionSnapshot.ActualSampleIdAtSelection;
    const bool bHasSettled = bHasNewCompletedSample
        && MotionMatchingLocomotionState.ActualPlanarSpeed <= FMath::Max(0.f, StationaryEnterSpeed);
    const bool bMinimumHoldSatisfied = MotionMatchingLocomotionState.PhaseElapsedSeconds
        >= FMath::Max(0.f, MotionMatchingSearchRequest.MinimumHoldTime);
    if (!bHasSettled || !bMinimumHoldSatisfied) return;

    MotionMatchingSearchRequest.bPending = false;
    MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::Confirmed;
    MotionMatchingSelectionSnapshot.ConfirmedActualSampleId = ActualState.ActualSampleId;
    MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
    MotionMatchingSelectionSnapshot.bConfirmedByActualState = true;
    if (CurrentPhase != ESKMotionMatchingLocomotionPhase::StopRequested) return;

    if (MotionMatchingLocomotionState.PhaseRevision < MAX_int64)
    {
        ++MotionMatchingLocomotionState.PhaseRevision;
    }
    MotionMatchingLocomotionState.PreviousPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingLocomotionState.Phase = ESKMotionMatchingLocomotionPhase::Stationary;
    MotionMatchingLocomotionState.TransitionReason
        = ESKMotionMatchingPhaseTransitionReason::PostSelectionSettled;
    MotionMatchingLocomotionState.PhaseElapsedSeconds = 0.f;
}

/**
 * 根据有效语义状态创建或替换持久搜索请求，不根据连续方向、速度或每帧 QueryRevision 重建。
 * 只能在游戏线程、QuerySnapshot 与 LocomotionState 发布后、评估 Chooser 前调用；不执行搜索。
 * 新请求会覆盖尚未确认的旧请求并获得新 Generation；相同离散语义保持原请求与时间。
 * 本函数先发布当前离散语义供 Chooser 绑定读取；调用方随后解析数据库并在无候选或标签阻塞时
 * 显式停用请求。GameplayTagRevision 参与离散变化比较，使解除阻塞后无需伪造 Phase 变化即可恢复搜索。
 */
void USKMotionMatchingAnimInstance::RefreshMotionMatchingSearchRequest()
{
    if (!bMotionMatchingQueryValid || !MotionMatchingLocomotionState.bValid) return;

    const FSKMotionMatchingIntentInput& Intent = MotionMatchingQuerySnapshot.Intent.Intent;
    const bool bRequestChanged = MotionMatchingSearchRequest.Generation == 0
        || MotionMatchingSearchRequest.PhaseRevision != MotionMatchingLocomotionState.PhaseRevision
        || MotionMatchingSearchRequest.GameplayTagRevision
            != MotionMatchingLocomotionState.GameplayTagRevision
        || MotionMatchingSearchRequest.RequestedPhase != MotionMatchingLocomotionState.Phase
        || MotionMatchingSearchRequest.RequestedGait != Intent.RequestedGait
        || MotionMatchingSearchRequest.RequestedStance != Intent.RequestedStance
        || MotionMatchingSearchRequest.RequestedMode != Intent.RotationMode;
    if (!bRequestChanged || MotionMatchingSearchRequest.Generation == MAX_int64) return;

    ++MotionMatchingSearchRequest.Generation;
    MotionMatchingSearchRequest.PhaseRevision = MotionMatchingLocomotionState.PhaseRevision;
    MotionMatchingSearchRequest.QueryRevision = MotionMatchingQuerySnapshot.QueryRevision;
    MotionMatchingSearchRequest.GameplayTagRevision
        = MotionMatchingLocomotionState.GameplayTagRevision;
    MotionMatchingSearchRequest.RequestedPhase = MotionMatchingLocomotionState.Phase;
    MotionMatchingSearchRequest.RequestedGait = Intent.RequestedGait;
    MotionMatchingSearchRequest.RequestedStance = Intent.RequestedStance;
    MotionMatchingSearchRequest.RequestedMode = Intent.RotationMode;
    const UWorld* World = GetWorld();
    MotionMatchingSearchRequest.RequestTimeSeconds = World ? World->GetTimeSeconds() : 0.0;
    MotionMatchingSearchRequest.SearchWaitElapsedSeconds = 0.f;
    CompletedSearchEvaluationFrameCount = 0;
    MotionMatchingSearchRequest.MinimumHoldTime = FMath::Max(0.f, MinimumPhaseHoldSeconds);
    MotionMatchingSearchRequest.State = ESKMotionMatchingSearchRequestState::PendingSearch;
    MotionMatchingSearchRequest.bPending = true;
    MotionMatchingSelectionSnapshot.bMatchesPendingRequest = false;
    MotionMatchingSelectionSnapshot.bAwaitingActualStateConfirmation = false;
}
