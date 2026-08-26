// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/SKAIController.h"

#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSKAIController, Log, All);

/**
 * 创建控制器持有的原生感知组件和视觉配置，注册统一的感知更新回调。
 * 构造阶段只建立默认子对象和初始配置；蓝图默认值会在 OnPossess 时重新应用。
 * 所有子对象由 UObject 生命周期管理，本函数不加载行为树或导航资源。
 */
ASKAIController::ASKAIController()
{
    AIPerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerceptionComponent"));
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

    SetPerceptionComponent(*AIPerceptionComponent);
    ConfigureSightSense();
    AIPerceptionComponent->OnPerceptionUpdated.AddDynamic(this, &ASKAIController::HandlePerceptionUpdated);
}

/**
 * 启动当前实例通过蓝图默认值配置的原生行为树，并让 AAIController 负责创建或复用行为树及黑板组件。
 * 本函数只允许在游戏线程的控制器生命周期或蓝图事件中调用；它不加载资源、不选择目标，也不启动 PIE。
 *
 * @return 行为树资产有效且 AAIController 成功启动该树时返回 true；未配置资产或启动失败时返回 false。
 *         成功调用会修改控制器的 BrainComponent 与 Blackboard 状态，重复调用由 AAIController 负责安全重启。
 */
bool ASKAIController::StartConfiguredBehaviorTree()
{
    if (!BehaviorTreeAsset)
    {
        UE_LOG(LogSKAIController, Warning, TEXT("%s 未配置 BehaviorTreeAsset，无法启动 AI 行为树。"), *GetNameSafe(this));
        return false;
    }

    if (!RunBehaviorTree(BehaviorTreeAsset))
    {
        UE_LOG(
            LogSKAIController,
            Warning,
            TEXT("%s 启动行为树 %s 失败，请检查行为树及 Blackboard 配置。"),
            *GetNameSafe(this),
            *GetNameSafe(BehaviorTreeAsset));
        return false;
    }

    return true;
}

/**
 * 将对象引用写入当前已初始化的黑板键；传入空对象会显式清空该键，便于重复 Possess 时移除旧目标。
 * 本函数只允许在游戏线程调用，不持有 Value 的额外原生所有权，也不负责创建 BlackboardComponent。
 *
 * @param KeyName 目标黑板键名；必须非 None，且必须存在于当前 BlackboardData 中。
 * @param Value 要写入的 UObject；允许为空，黑板组件会按对象键的引用规则管理该值。
 * @return 黑板已初始化且键名有效时返回 true；组件缺失、键名为 None 或键不存在时返回 false。
 *         成功时会修改当前行为树共享的黑板状态，并可能触发观察该键的行为树节点重新评估。
 */
bool ASKAIController::SetBlackboardObjectValue(FName KeyName, UObject* Value)
{
    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent || KeyName.IsNone()) return false;

    const FBlackboard::FKey KeyId = BlackboardComponent->GetKeyID(KeyName);
    if (KeyId == FBlackboard::InvalidKey) return false;

    BlackboardComponent->SetValueAsObject(KeyName, Value);
    return true;
}

/**
 * 将向量写入当前已初始化的黑板键，供原生 MoveTo 等行为树节点直接消费。
 * 本函数只允许在游戏线程调用，不创建 BlackboardComponent，也不验证键所代表的业务语义。
 *
 * @param KeyName 目标黑板键名；必须非 None，且必须存在于当前 BlackboardData 中。
 * @param Value 要复制到黑板的世界空间向量，通常使用厘米作为位置单位。
 * @return 黑板已初始化且键名有效时返回 true；组件缺失、键名为 None 或键不存在时返回 false。
 *         成功写入可能触发观察该键的行为树节点重新评估。
 */
bool ASKAIController::SetBlackboardVectorValue(FName KeyName, FVector Value)
{
    UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
    if (!BlackboardComponent || KeyName.IsNone()) return false;

    const FBlackboard::FKey KeyId = BlackboardComponent->GetKeyID(KeyName);
    if (KeyId == FBlackboard::InvalidKey) return false;

    BlackboardComponent->SetValueAsVector(KeyName, Value);
    return true;
}

/**
 * 将候选世界位置投影到当前世界的导航数据，供脚本在提交移动目标前验证空间位置。
 * 本函数只执行通用导航查询，不写 Blackboard、不发起移动也不解释候选点的战术含义；只能在游戏线程调用。
 *
 * @param CandidateLocation 待投影的世界空间位置，单位厘米；包含非有限分量时查询失败。
 * @param QueryExtent 以候选点为中心的查询半尺寸，单位厘米；各轴取绝对值后传给导航系统。
 * @param OutProjectedLocation 成功时接收导航投影位置；失败时重置为 CandidateLocation，不保留内部引用。
 * @return 世界和导航系统有效且候选点成功投影到导航数据时返回 true，否则返回 false。
 */
bool ASKAIController::ProjectNavigationPoint(
    const FVector& CandidateLocation,
    const FVector& QueryExtent,
    FVector& OutProjectedLocation) const
{
    OutProjectedLocation = CandidateLocation;
    if (CandidateLocation.ContainsNaN() || QueryExtent.ContainsNaN()) return false;

    UWorld* World = GetWorld();
    if (!World) return false;

    UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavigationSystem) return false;

    const FVector SafeQueryExtent = QueryExtent.GetAbs();
    FNavLocation ProjectedLocation;
    if (!NavigationSystem->ProjectPointToNavigation(
        CandidateLocation,
        ProjectedLocation,
        SafeQueryExtent))
    {
        return false;
    }

    OutProjectedLocation = ProjectedLocation.Location;
    return true;
}

/**
 * 围绕本次 Possess 记录的出生点请求一个导航可达随机点，并写入配置的巡逻黑板键。
 * 本函数只允许在游戏线程调用；有感知目标时不会改变巡逻点，也不发起实际移动请求。
 *
 * @return 无当前目标、巡逻配置有效、导航系统成功取点且黑板写入成功时返回 true；
 *         缺少 Pawn、出生点、导航系统、黑板或可达点时返回 false。
 *         成功时会更新 CurrentPatrolLocation 与 bHasPatrolLocation。
 */
bool ASKAIController::RefreshPatrolLocation()
{
    APawn* ControlledPawn = GetPawn();
    if (CurrentPerceptionTarget.IsValid() || !ControlledPawn || !bHasPatrolOrigin ||
        PatrolLocationBlackboardKey.IsNone() || PatrolRadius <= 0.0f)
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (!World) return false;

    UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavigationSystem) return false;

    FNavLocation RandomPatrolLocation;
    if (!NavigationSystem->GetRandomReachablePointInRadius(PatrolOrigin, PatrolRadius, RandomPatrolLocation))
    {
        return false;
    }

    if (!SetBlackboardVectorValue(PatrolLocationBlackboardKey, RandomPatrolLocation.Location)) return false;

    CurrentPatrolLocation = RandomPatrolLocation.Location;
    bHasPatrolLocation = true;
    return true;
}

/**
 * 完成 AAIController 的 Pawn 接管后，按配置启动行为树，并把指定索引的玩家 Pawn 写入初始目标键。
 * 同时应用蓝图视觉配置、记录巡逻中心，并在没有可见玩家目标时启动轻量巡逻检查。
 * 本函数由引擎在游戏线程调用；它不依赖具体 Pawn 类型，也不负责生成行为树资产或执行移动。
 *
 * @param InPawn 本次被接管的 Pawn；所有权与生命周期由引擎管理，本函数不保存额外引用。
 *        找不到玩家 Pawn 时会向已初始化的目标键写入空值，从而清理重复 Possess 遗留的目标。
 */
void ASKAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    StopPatrolRefresh();
    CurrentPerceptionTarget.Reset();
    bHasPerceptionTarget = false;
    bHasPatrolLocation = false;
    bHasPatrolOrigin = InPawn != nullptr;
    PatrolOrigin = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;

    ConfigureSightSense();
    AIPerceptionComponent->RequestStimuliListenerUpdate();

    if (bStartBehaviorTreeAutomatically)
    {
        StartConfiguredBehaviorTree();
    }

    if (!InitialPlayerTargetBlackboardKey.IsNone())
    {
        APawn* InitialPlayerTarget = UGameplayStatics::GetPlayerPawn(this, InitialPlayerTargetIndex);
        SetBlackboardObjectValue(InitialPlayerTargetBlackboardKey, InitialPlayerTarget);
    }

    StartPatrolRefresh(true);
}

/**
 * 在控制器释放 Pawn 前停止巡逻定时器、清理感知目标和本次接管产生的巡逻状态。
 * 本函数由引擎在游戏线程调用；它不销毁默认子对象，也不修改蓝图默认配置。
 */
void ASKAIController::OnUnPossess()
{
    StopPatrolRefresh();
    SetBlackboardObjectValue(PerceptionTargetBlackboardKey, nullptr);
    CurrentPerceptionTarget.Reset();
    bHasPerceptionTarget = false;
    bHasPatrolOrigin = false;
    bHasPatrolLocation = false;

    if (AIPerceptionComponent) AIPerceptionComponent->ForgetAll();

    Super::OnUnPossess();
}

/**
 * 响应原生 AI 感知系统的批量刺激变化，并从所有当前可见对象中重新选择最近玩家 Pawn。
 * 本函数由感知组件在游戏线程调用，不持有 UpdatedActors 中对象的强引用。
 *
 * @param UpdatedActors 本次刺激状态发生变化的 Actor 集合；仅用于触发全量重选，允许为空。
 */
void ASKAIController::HandlePerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
    static_cast<void>(UpdatedActors);
    SelectNearestVisiblePlayerTarget();
}

/**
 * 把当前蓝图默认值同步到原生视觉 SenseConfig，并启用所有阵营检测后由玩家 Pawn 过滤器做最终筛选。
 * 本函数仅在构造或游戏线程的 Possess 生命周期调用；不会清理已有刺激记录或选择目标。
 */
void ASKAIController::ConfigureSightSense()
{
    if (!AIPerceptionComponent || !SightConfig) return;

    SightConfig->SightRadius = FMath::Max(0.0f, SightRadius);
    SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius, LoseSightRadius);
    SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(PeripheralVisionAngleDegrees, 0.0f, 180.0f);
    SightConfig->SetMaxAge(FMath::Max(0.0f, TargetMaxAge));
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

    AIPerceptionComponent->ConfigureSense(*SightConfig);
    AIPerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());
}

/**
 * 枚举视觉 Sense 当前仍判定为可见的 Actor，只保留玩家控制的 Pawn，并按与受控 Pawn 的距离选取最近者。
 * 本函数只允许在游戏线程调用；它不保留候选数组，最终目标通过弱引用保存。
 */
void ASKAIController::SelectNearestVisiblePlayerTarget()
{
    APawn* ControlledPawn = GetPawn();
    if (!AIPerceptionComponent || !ControlledPawn)
    {
        SetCurrentPerceptionTarget(nullptr);
        return;
    }

    TArray<AActor*> PerceivedActors;
    AIPerceptionComponent->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

    AActor* NearestTarget = nullptr;
    float NearestDistanceSquared = TNumericLimits<float>::Max();

    for (AActor* PerceivedActor : PerceivedActors)
    {
        APawn* PerceivedPawn = Cast<APawn>(PerceivedActor);
        if (!IsValid(PerceivedPawn) || PerceivedPawn == ControlledPawn || !PerceivedPawn->IsPlayerControlled()) continue;

        const float DistanceSquared = FVector::DistSquared(ControlledPawn->GetActorLocation(), PerceivedPawn->GetActorLocation());
        if (DistanceSquared >= NearestDistanceSquared) continue;

        NearestDistanceSquared = DistanceSquared;
        NearestTarget = PerceivedPawn;
    }

    SetCurrentPerceptionTarget(NearestTarget);
}

/**
 * 更新当前感知目标及黑板对象，并在“有目标/无目标”状态切换时暂停或恢复巡逻。
 * 本函数只允许在游戏线程调用；NewTarget 以弱引用保存，允许为空且不转移所有权。
 *
 * @param NewTarget 最近可见的玩家 Pawn；为空表示当前没有视觉目标。
 */
void ASKAIController::SetCurrentPerceptionTarget(AActor* NewTarget)
{
    AActor* PreviousTarget = CurrentPerceptionTarget.Get();
    const bool bNewTargetIsValid = IsValid(NewTarget);
    if (PreviousTarget == NewTarget && bHasPerceptionTarget == bNewTargetIsValid) return;

    CurrentPerceptionTarget = NewTarget;
    const bool bPreviouslyHadTarget = bHasPerceptionTarget;
    bHasPerceptionTarget = bNewTargetIsValid;
    SetBlackboardObjectValue(PerceptionTargetBlackboardKey, NewTarget);

    if (bHasPerceptionTarget)
    {
        StopPatrolRefresh();
        return;
    }

    StartPatrolRefresh(bPreviouslyHadTarget);
}

/**
 * 在无感知目标阶段启动循环巡逻检查，并可立即生成首个或目标丢失后的新巡逻点。
 * 本函数只允许在游戏线程调用；重复调用会替换已有定时器，不执行 Pawn 移动。
 *
 * @param bRefreshImmediately 为 true 时在设置定时器前立即尝试生成巡逻位置。
 */
void ASKAIController::StartPatrolRefresh(bool bRefreshImmediately)
{
    if (CurrentPerceptionTarget.IsValid() || !bHasPatrolOrigin) return;

    UWorld* World = GetWorld();
    if (!World) return;

    StopPatrolRefresh();
    if (bRefreshImmediately) RefreshPatrolLocation();

    const float SafeRefreshInterval = FMath::Max(0.05f, PatrolRefreshInterval);
    World->GetTimerManager().SetTimer(
        PatrolRefreshTimerHandle,
        this,
        &ASKAIController::HandlePatrolRefreshTimer,
        SafeRefreshInterval,
        true);
}

/**
 * 停止当前世界中的巡逻检查定时器。
 * 本函数只允许在游戏线程调用；没有有效世界或定时器时安全返回，不清除现有巡逻黑板值。
 */
void ASKAIController::StopPatrolRefresh()
{
    UWorld* World = GetWorld();
    if (!World) return;

    World->GetTimerManager().ClearTimer(PatrolRefreshTimerHandle);
}

/**
 * 定时检查受控 Pawn 是否已进入当前巡逻点的接受半径，到达后生成下一个导航可达点。
 * 本函数由 TimerManager 在游戏线程调用；有感知目标、缺少 Pawn 或尚无巡逻点时不会发起移动。
 */
void ASKAIController::HandlePatrolRefreshTimer()
{
    if (CurrentPerceptionTarget.IsValid()) return;

    APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn) return;

    if (!bHasPatrolLocation)
    {
        RefreshPatrolLocation();
        return;
    }

    const float SafeAcceptanceRadius = FMath::Max(0.0f, PatrolAcceptanceRadius);
    if (FVector::DistSquared(ControlledPawn->GetActorLocation(), CurrentPatrolLocation) <= FMath::Square(SafeAcceptanceRadius))
    {
        RefreshPatrolLocation();
    }
}
