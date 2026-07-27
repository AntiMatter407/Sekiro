// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SKAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UBehaviorTree;

UCLASS(Blueprintable)
class SEKIRO_API ASKAIController : public AAIController
{
    GENERATED_BODY()

public:
    ASKAIController();

    // ── 行为树控制 ──────────────────────────────────────

    /** 启动蓝图配置的行为树。 */
    UFUNCTION(BlueprintCallable, Category = "AI|Behavior Tree")
    bool StartConfiguredBehaviorTree();

    /** 向当前行为树的黑板写入对象值。 */
    UFUNCTION(BlueprintCallable, Category = "AI|Blackboard")
    bool SetBlackboardObjectValue(FName KeyName, UObject* Value);

    /** 向当前行为树的黑板写入向量值。 */
    UFUNCTION(BlueprintCallable, Category = "AI|Blackboard")
    bool SetBlackboardVectorValue(FName KeyName, FVector Value);

    /** 立即生成并写入一个可达巡逻位置。 */
    UFUNCTION(BlueprintCallable, Category = "AI|Patrol")
    bool RefreshPatrolLocation();

protected:
    // ── 生命周期 ────────────────────────────────────────

    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    // ── 行为树配置 ──────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Behavior Tree")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;         // 接管 Pawn 后运行的行为树资产

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Behavior Tree")
    bool bStartBehaviorTreeAutomatically = true;         // 是否在接管 Pawn 时自动启动行为树

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
    FName InitialPlayerTargetBlackboardKey = NAME_None;  // 初始玩家目标写入的黑板键，None 表示禁用

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard", meta = (ClampMin = "0", UIMin = "0"))
    int32 InitialPlayerTargetIndex = 0;                  // 初始目标使用的本地玩家索引

    // ── 视觉感知配置 ────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Perception")
    TObjectPtr<UAIPerceptionComponent> AIPerceptionComponent; // 原生 AI 感知组件

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Perception")
    TObjectPtr<UAISenseConfig_Sight> SightConfig;        // 原生视觉感知配置

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float SightRadius = 2000.0f;                         // 玩家目标进入视觉感知的最大距离

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float LoseSightRadius = 2500.0f;                     // 已发现玩家目标保持可见的最大距离

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
    float PeripheralVisionAngleDegrees = 70.0f;          // 视觉感知的半视野角度

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float TargetMaxAge = 5.0f;                           // 丢失刺激后保留感知记录的秒数

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Perception")
    FName PerceptionTargetBlackboardKey = TEXT("TargetActor"); // 当前最近可见玩家写入的黑板键

    // ── 巡逻配置 ────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol")
    FName PatrolLocationBlackboardKey = TEXT("PatrolLocation"); // 随机可达巡逻位置写入的黑板键

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float PatrolRadius = 1200.0f;                        // 相对出生点的随机巡逻半径

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float PatrolAcceptanceRadius = 120.0f;               // 判定到达巡逻位置的距离

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Patrol", meta = (ClampMin = "0.05", UIMin = "0.05"))
    float PatrolRefreshInterval = 0.5f;                  // 检查并刷新巡逻位置的时间间隔

private:
    // ── 感知与巡逻内部流程 ──────────────────────────────

    UFUNCTION()
    void HandlePerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    void ConfigureSightSense();
    void SelectNearestVisiblePlayerTarget();
    void SetCurrentPerceptionTarget(AActor* NewTarget);
    void StartPatrolRefresh(bool bRefreshImmediately);
    void StopPatrolRefresh();
    void HandlePatrolRefreshTimer();

    TWeakObjectPtr<AActor> CurrentPerceptionTarget;       // 当前写入黑板的可见玩家目标
    FVector PatrolOrigin = FVector::ZeroVector;          // 当前 Pawn 被接管时记录的巡逻中心
    FVector CurrentPatrolLocation = FVector::ZeroVector; // 最近一次成功写入黑板的巡逻位置
    FTimerHandle PatrolRefreshTimerHandle;               // 无目标阶段的巡逻刷新定时器
    bool bHasPerceptionTarget = false;                   // 是否处于视觉目标锁定状态
    bool bHasPatrolOrigin = false;                       // 是否记录了有效的巡逻中心
    bool bHasPatrolLocation = false;                     // 是否存在已写入黑板的巡逻位置
};
