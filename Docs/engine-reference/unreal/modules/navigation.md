# Unreal Engine 5.7 — 导航模块参考

**最后验证：** 2026-02-13
**知识缺口：** UE 5.7 导航改进

---

## 概述

UE 5.7 导航系统：
- **Nav Mesh**（导航网格）：AI 自动寻路网格
- **AI Controller**：控制 AI 移动和行为
- **Behavior Trees**（行为树）：AI 决策（在 AI 模块中介绍）

---

## Nav Mesh 设置

### 添加 Nav Mesh Bounds Volume

1. Place Actors > Volumes > Nav Mesh Bounds Volume
2. 缩放以覆盖可行走区域
3. 按 `P` 切换 Nav Mesh 可视化（绿色覆盖层）

### Nav Mesh 设置

```cpp
// Project Settings > Engine > Navigation System
// - Generate Navigation Only Around Navigation Invokers：性能优化
// - Auto Update Enabled：几何体变化时重建 NavMesh
```

---

## AI Controller 与移动

### 创建 AI Controller

```cpp
UCLASS()
class AEnemyAIController : public AAIController {
    GENERATED_BODY()

public:
    void BeginPlay() override {
        Super::BeginPlay();

        // 移动到指定位置
        FVector TargetLocation = FVector(1000, 0, 0);
        MoveToLocation(TargetLocation);
    }
};
```

### 为 Pawn 分配 AI Controller

```cpp
UCLASS()
class AEnemyCharacter : public ACharacter {
    GENERATED_BODY()

public:
    AEnemyCharacter() {
        // ✅ 分配 AI Controller 类
        AIControllerClass = AEnemyAIController::StaticClass();
        AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    }
};
```

---

## 基本 AI 移动

### 移动到位置

```cpp
AAIController* AIController = Cast<AAIController>(GetController());
if (AIController) {
    FVector TargetLocation = FVector(1000, 0, 0);
    EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(TargetLocation);

    if (Result == EPathFollowingRequestResult::RequestSuccessful) {
        UE_LOG(LogTemp, Warning, TEXT("正在移动到目标位置"));
    }
}
```

### 移动到 Actor

```cpp
AActor* Target = /* 获取目标 Actor */;
AIController->MoveToActor(Target, 100.0f); // 距离 100 单位处停止
```

### 停止移动

```cpp
AIController->StopMovement();
```

---

## 路径跟随事件

### 移动完成回调

```cpp
UCLASS()
class AEnemyAIController : public AAIController {
    GENERATED_BODY()

public:
    void BeginPlay() override {
        Super::BeginPlay();

        // 绑定移动完成事件
        ReceiveMoveCompleted.AddDynamic(this, &AEnemyAIController::OnMoveCompleted);
    }

    UFUNCTION()
    void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result) {
        if (Result == EPathFollowingResult::Success) {
            UE_LOG(LogTemp, Warning, TEXT("已到达目的地"));
        } else {
            UE_LOG(LogTemp, Warning, TEXT("未能到达目的地"));
        }
    }
};
```

---

## 寻路查询

### 查找到位置的路径

```cpp
#include "NavigationSystem.h"
#include "NavigationPath.h"

UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
if (NavSys) {
    FVector Start = GetActorLocation();
    FVector End = TargetLocation;

    FPathFindingQuery Query;
    Query.StartLocation = Start;
    Query.EndLocation = End;
    Query.NavData = NavSys->GetDefaultNavDataInstance();

    FPathFindingResult Result = NavSys->FindPathSync(Query);

    if (Result.IsSuccessful()) {
        UNavigationPath* NavPath = Result.Path.Get();
        // 使用路径点：NavPath->GetPathPoints()
    }
}
```

### 检查位置是否可达

```cpp
UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
FNavLocation OutLocation;
bool bReachable = NavSys->ProjectPointToNavigation(TargetLocation, OutLocation);

if (bReachable) {
    UE_LOG(LogTemp, Warning, TEXT("该位置可达"));
}
```

---

## Nav Mesh 修改器

### Nav Modifier Volume（阻挡/允许区域）

1. Place Actors > Volumes > Nav Modifier Volume
2. 配置 Area Class（如 NavArea_Null 用于阻挡，NavArea_LowHeight 用于蹲伏）

---

## 自定义 Nav Area

### 创建自定义 Nav Area

```cpp
UCLASS()
class UNavArea_Jump : public UNavArea {
    GENERATED_BODY()

public:
    UNavArea_Jump() {
        DefaultCost = 10.0f; // 更高的代价 = AI 除非必要否则避免
        FixedAreaEnteringCost = 100.0f; // 进入的一次性代价
    }
};
```

### 使用自定义 Nav Area

```cpp
// 分配到 Nav Modifier Volume 或几何体
```

---

## Nav Mesh 生成

### 运行时重建 Nav Mesh

```cpp
UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
NavSys->Build(); // 重建整个 NavMesh
```

### 动态 Nav Mesh（移动障碍物）

```cpp
// 启用：Project Settings > Navigation System > Runtime Generation = Dynamic

// 将 Actor 标记为动态障碍物：
UStaticMeshComponent* Mesh = /* 获取网格体 */;
Mesh->SetCanEverAffectNavigation(true);
Mesh->bDynamicObstacle = true;
```

---

## Nav Links（网格外连接）

### Nav Link Proxy（跳跃、传送）

1. Place Actors > Navigation > Nav Link Proxy
2. 设置起点和终点
3. 配置：
   - **Direction**：单向或双向
   - **Smart Link**：穿越时动画化角色

---

## 人群管理

### Detour Crowd（避免重叠）

```cpp
// 启用：Character Movement Component > Avoidance Enabled = true

// 配置回避组和标志
UCharacterMovementComponent* MoveComp = GetCharacterMovement();
MoveComp->SetAvoidanceGroup(1);
MoveComp->SetGroupsToAvoid(1);
MoveComp->SetAvoidanceEnabled(true);
```

---

## 性能提示

### Nav Mesh 优化

```cpp
// 为大型世界减小瓦片大小：
// Project Settings > Navigation System > Cell Size = 19（默认）

// 使用 Navigation Invokers 进行动态生成：
// 仅在玩家/重要 Actor 周围生成 NavMesh
```

---

## 调试

### 可视化 Nav Mesh

```cpp
// 控制台命令：
// show navigation - 切换 Nav Mesh 可视化
// p - 切换 Nav Mesh（编辑器视口）

// 绘制调试路径：
if (NavPath) {
    for (int i = 0; i < NavPath->GetPathPoints().Num() - 1; i++) {
        DrawDebugLine(GetWorld(), NavPath->GetPathPoints()[i], NavPath->GetPathPoints()[i + 1], FColor::Green, false, 5.0f, 0, 5.0f);
    }
}
```

---

## 常用模式

### 在路径点之间巡逻

```cpp
UPROPERTY(EditAnywhere, Category = "AI")
TArray<AActor*> PatrolPoints;

int32 CurrentPatrolIndex = 0;

void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result) {
    if (Result == EPathFollowingResult::Success) {
        // 移动到下一个路径点
        CurrentPatrolIndex = (CurrentPatrolIndex + 1) % PatrolPoints.Num();
        MoveToActor(PatrolPoints[CurrentPatrolIndex]);
    }
}
```

### 追逐玩家

```cpp
void Tick(float DeltaTime) {
    Super::Tick(DeltaTime);

    AAIController* AIController = Cast<AAIController>(GetController());
    APawn* PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();

    if (AIController && PlayerPawn) {
        float Distance = FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation());

        if (Distance < 1000.0f) {
            // 追逐玩家
            AIController->MoveToActor(PlayerPawn, 100.0f);
        } else {
            // 停止追逐
            AIController->StopMovement();
        }
    }
}
```

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/navigation-system-in-unreal-engine/
- https://docs.unrealengine.com/5.7/en-US/ai-in-unreal-engine/
