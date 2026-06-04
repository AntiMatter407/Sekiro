# Unreal Engine 5.7 — 当前最佳实践

**最后验证：** 2026-02-13

现代 UE5 模式，可能不在 LLM 的训练数据中。这些是截至 UE 5.7 的生产就绪推荐。

---

## 项目设置

### 新项目使用 UE 5.7
- 最新特性：Megalights、生产就绪的 Substrate 和 PCG
- 更好的性能和稳定性

### 选择合适的渲染特性
- **Lumen**：实时全局光照 (Global Illumination)（大多数项目推荐）
- **Nanite**：高面数网格体的虚拟化几何体（细节丰富的环境推荐）
- **Megalights**：百万级动态光源（复杂光照推荐）
- **Substrate**：模块化材质系统（新项目推荐）

---

## C++ 编码

### 使用现代 C++ 特性（UE5.7 中的 C++20）

```cpp
// ✅ 使用 TObjectPtr<T>（UE5 类型安全指针）
UPROPERTY()
TObjectPtr<UStaticMeshComponent> MeshComp;

// ✅ 结构化绑定
if (auto [bSuccess, Value] = TryGetValue(); bSuccess) {
    // 使用 Value
}

// ✅ 概念和约束（C++20）
template<typename T>
concept Damageable = requires(T t, float damage) {
    { t.TakeDamage(damage) } -> std::same_as<void>;
};
```

### 使用 UPROPERTY() 进行垃圾回收 (Garbage Collection)

```cpp
// ✅ UPROPERTY 确保 GC 不会删除此对象
UPROPERTY()
TObjectPtr<AActor> MyActor;

// ❌ 原始指针可能成为悬空指针
AActor* MyActor; // 危险！可能被垃圾回收
```

### 使用 UFUNCTION() 暴露给蓝图 (Blueprint)

```cpp
// ✅ 可从蓝图调用
UFUNCTION(BlueprintCallable, Category="Combat")
void TakeDamage(float Damage);

// ✅ 可在蓝图中实现
UFUNCTION(BlueprintImplementableEvent, Category="Combat")
void OnDeath();
```

---

## 蓝图最佳实践

### 蓝图 vs C++ 的选择

- **C++**：核心游戏系统、性能关键代码、底层引擎交互
- **蓝图 (Blueprint)**：快速原型制作、内容创作、数据驱动逻辑、设计师工作流

### 蓝图性能提示

```cpp
// ✅ 谨慎使用 Event Tick（开销大）
// 推荐使用计时器或事件

// ✅ 使用蓝图本地化 (Blueprint Nativization)（蓝图 → C++）
// Project Settings > Packaging > Blueprint Nativization

// ✅ 缓存频繁访问的组件
// 不要每帧都调用 GetComponent
```

---

## 渲染（UE 5.7）

### 使用 Lumen 进行全局光照

```cpp
// 启用：Project Settings > Engine > Rendering > Dynamic Global Illumination Method = Lumen
// 实时 GI，无需光照贴图烘焙（推荐）
```

### 使用 Nanite 处理高面数网格体

```cpp
// 在静态网格体上启用：Details > Nanite Settings > Enable Nanite Support
// 自动 LOD 百万级三角形（细节丰富的网格体推荐）
```

### 使用 Megalights 处理复杂光照（UE 5.5+）

```cpp
// 启用：Project Settings > Engine > Rendering > Megalights = Enabled
// 以极低开销支持百万级动态光源
```

### 使用 Substrate 材质（5.7 生产就绪）

```cpp
// 启用：Project Settings > Engine > Substrate > Enable Substrate
// 模块化、物理准确的材质（新项目推荐）
```

---

## Enhanced Input 系统

### 设置 Enhanced Input

```cpp
// 1. 创建 Input Action（IA_Jump）
// 2. 创建 Input Mapping Context（IMC_Default）
// 3. 添加映射：IA_Jump → 空格键

// C++ 设置：
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

void AMyCharacter::BeginPlay() {
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController())) {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }
}

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
    EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMyCharacter::Move);
}

void AMyCharacter::Move(const FInputActionValue& Value) {
    FVector2D MoveVector = Value.Get<FVector2D>();
    AddMovementInput(GetActorForwardVector(), MoveVector.Y);
    AddMovementInput(GetActorRightVector(), MoveVector.X);
}
```

---

## Gameplay Ability System (GAS)

### 使用 GAS 处理复杂玩法

```cpp
// ✅ 使用 GAS 处理：技能、增益、伤害计算、冷却时间
// 模块化、可扩展、支持多人

// 安装：启用 "Gameplay Abilities" 插件

// 示例技能：
UCLASS()
class UGA_Fireball : public UGameplayAbility {
    GENERATED_BODY()

public:
    virtual void ActivateAbility(...) override {
        // 技能逻辑
        SpawnFireball();
        CommitAbility(); // 提交消耗/冷却
    }
};
```

---

## World Partition（大型世界）

### 使用 World Partition 构建开放世界

```cpp
// 启用：World Settings > Enable World Partition
// 根据玩家位置自动流式加载世界单元格

// Data Layers：组织内容（如 "Gameplay"、"Audio"、"Lighting"）
// Runtime Data Layers：运行时加载/卸载
```

---

## Niagara（视觉效果，VFX）

### 使用 Niagara（而非 Cascade）

```cpp
// 创建：Content Browser > 右键 > FX > Niagara System
// GPU 加速、基于节点的粒子系统（推荐）

// 生成粒子：
UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
    GetWorld(),
    ExplosionSystem,
    GetActorLocation()
);
```

---

## MetaSounds（音频）

### 使用 MetaSounds 进行程序化音频

```cpp
// 创建：Content Browser > 右键 > Sounds > MetaSound Source
// 基于节点的音频，替代复杂逻辑的 Sound Cue（推荐）

// 播放 MetaSound：
UAudioComponent* AudioComp = UGameplayStatics::SpawnSound2D(
    GetWorld(),
    MetaSoundSource
);
```

---

## 网络复制 (Replication)（多人）

### 服务器权威模式 (Server-Authoritative Pattern)

```cpp
// ✅ 客户端发送输入，服务器验证并复制
UFUNCTION(Server, Reliable)
void Server_Move(FVector Direction);

void AMyCharacter::Server_Move_Implementation(FVector Direction) {
    // 服务器验证并应用移动
    AddMovementInput(Direction);
}

// ✅ 复制重要状态
UPROPERTY(Replicated)
int32 Health;

void AMyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMyCharacter, Health);
}
```

---

## 性能优化

### 使用对象池 (Object Pooling)

```cpp
// ✅ 复用对象而非 Spawn/Destroy
TArray<AActor*> ProjectilePool;

AActor* GetPooledProjectile() {
    for (AActor* Proj : ProjectilePool) {
        if (!Proj->IsActive()) {
            Proj->SetActive(true);
            return Proj;
        }
    }
    // 池耗尽，生成新的
    return SpawnNewProjectile();
}
```

### 使用实例化静态网格体

```cpp
// ✅ 层级实例化静态网格体组件 (HISM)
// 一次绘制调用渲染数千个相同网格体
UHierarchicalInstancedStaticMeshComponent* HISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Trees"));
for (int i = 0; i < 1000; i++) {
    HISM->AddInstance(FTransform(RandomLocation));
}
```

---

## 调试

### 使用日志

```cpp
// ✅ 结构化日志
UE_LOG(LogTemp, Warning, TEXT("Player health: %d"), Health);

// 自定义日志类别
DECLARE_LOG_CATEGORY_EXTERN(LogMyGame, Log, All);
DEFINE_LOG_CATEGORY(LogMyGame);
UE_LOG(LogMyGame, Error, TEXT("Critical error!"));
```

### 使用 Visual Logger（可视化日志）

```cpp
// ✅ 可视化调试
#include "VisualLogger/VisualLogger.h"

UE_VLOG_SEGMENT(this, LogTemp, Log, StartPos, EndPos, FColor::Red, TEXT("Raycast"));
UE_VLOG_LOCATION(this, LogTemp, Log, TargetLocation, 50.f, FColor::Green, TEXT("Target"));
```

---

## 总结：UE 5.7 推荐技术栈

| 特性 | 使用这个（2026） | 备注 |
|---------|------------------|-------|
| **光照** | Lumen + Megalights | 实时 GI，百万级光源 |
| **几何体** | Nanite | 高面数网格体，自动 LOD |
| **材质** | Substrate | 模块化，物理准确 |
| **输入** | Enhanced Input | 可重绑定，模块化 |
| **VFX** | Niagara | GPU 加速 |
| **音频** | MetaSounds | 程序化音频 |
| **世界流式加载** | World Partition | 大型开放世界 |
| **游戏玩法** | Gameplay Ability System | 复杂技能、增益 |

---

**来源：**
- https://docs.unrealengine.com/5.7/en-US/
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-release-notes
