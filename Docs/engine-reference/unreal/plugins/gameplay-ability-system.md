# Unreal Engine 5.7 — Gameplay Ability System (GAS)

**最后验证：** 2026-02-13
**状态：** 生产就绪
**插件：** `GameplayAbilities`（内置，在 Plugins 中启用）

---

## 概述

**Gameplay Ability System (GAS)** 是一个模块化框架，用于构建技能、属性、效果和游戏机制。它是 RPG、MOBA、带技能的射击游戏以及任何有复杂技能系统的游戏的标准方案。

**适用场景：**
- 角色技能（法术、技能、攻击）
- 属性（生命值、法力值、体力、属性值）
- 增益/减益（临时效果）
- 冷却时间和消耗
- 伤害计算
- 支持多人游戏的技能复制

---

## 核心概念

### 1. **Ability System Component** (ASC)
- 拥有技能、属性和效果的主要组件
- 添加到角色或 PlayerState

### 2. **Gameplay Abilities**（游戏技能）
- 独立的技能/操作（火球、治疗、冲刺等）
- 可激活、提交（消耗/冷却）、可取消

### 3. **属性与属性集 (Attributes & Attribute Sets)**
- 可修改的数值属性（生命值、法力值、体力、力量等）
- 存储在属性集中

### 4. **Gameplay Effects**（游戏效果）
- 修改属性（伤害、治疗、增益、减益）
- 可以是瞬时、持续时间或无限持续

### 5. **Gameplay Tags**（游戏标签）
- 用于技能逻辑的层级标签（如 `Ability.Attack.Melee`、`Status.Stunned`）

---

## 设置

### 1. 启用插件

`Edit > Plugins > Gameplay Abilities > Enabled > Restart`

### 2. 添加 Ability System Component

```cpp
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"

UCLASS()
class AMyCharacter : public ACharacter {
    GENERATED_BODY()

public:
    AMyCharacter() {
        // 创建 ASC
        AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
        AbilitySystemComponent->SetIsReplicated(true);
        AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

        // 创建属性集
        AttributeSet = CreateDefaultSubobject<UMyAttributeSet>(TEXT("AttributeSet"));
    }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    UPROPERTY()
    TObjectPtr<const UAttributeSet> AttributeSet;
};
```

### 3. 初始化 ASC（多人游戏重要）

```cpp
void AMyCharacter::PossessedBy(AController* NewController) {
    Super::PossessedBy(NewController);

    // 服务器：初始化 ASC
    if (AbilitySystemComponent) {
        AbilitySystemComponent->InitAbilityActorInfo(this, this);
        GiveDefaultAbilities();
    }
}

void AMyCharacter::OnRep_PlayerState() {
    Super::OnRep_PlayerState();

    // 客户端：初始化 ASC
    if (AbilitySystemComponent) {
        AbilitySystemComponent->InitAbilityActorInfo(this, this);
    }
}
```

---

## 属性与属性集

### 创建属性集

```cpp
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"

UCLASS()
class UMyAttributeSet : public UAttributeSet {
    GENERATED_BODY()

public:
    UMyAttributeSet();

    // 生命值
    UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
    FGameplayAttributeData Health;
    ATTRIBUTE_ACCESSORS(UMyAttributeSet, Health)

    UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
    FGameplayAttributeData MaxHealth;
    ATTRIBUTE_ACCESSORS(UMyAttributeSet, MaxHealth)

    // 法力值
    UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Mana)
    FGameplayAttributeData Mana;
    ATTRIBUTE_ACCESSORS(UMyAttributeSet, Mana)

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    UFUNCTION()
    virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

    UFUNCTION()
    virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

    UFUNCTION()
    virtual void OnRep_Mana(const FGameplayAttributeData& OldMana);
};
```

### 实现属性集

```cpp
#include "Net/UnrealNetwork.h"

UMyAttributeSet::UMyAttributeSet() {
    // 默认值
    Health = 100.0f;
    MaxHealth = 100.0f;
    Mana = 50.0f;
}

void UMyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMyAttributeSet, Mana, COND_None, REPNOTIFY_Always);
}

void UMyAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) {
    GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, Health, OldHealth);
}

// 类似实现其他 OnRep 函数...
```

---

## Gameplay Abilities

### 创建 Gameplay Ability

```cpp
#include "Abilities/GameplayAbility.h"

UCLASS()
class UGA_Fireball : public UGameplayAbility {
    GENERATED_BODY()

public:
    UGA_Fireball() {
        // 技能配置
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

        // 标签
        AbilityTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Attack.Fireball")));
    }

    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override {

        if (!CommitAbility(Handle, ActorInfo, ActivationInfo)) {
            // 提交失败（法力不足、冷却中等）
            EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
            return;
        }

        // 生成火球投射物
        SpawnFireball();

        // 结束技能
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
    }

    void SpawnFireball() {
        // 火球生成逻辑
    }
};
```

### 为角色授予技能

```cpp
void AMyCharacter::GiveDefaultAbilities() {
    if (!HasAuthority() || !AbilitySystemComponent) return;

    // 授予技能
    AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Fireball::StaticClass(), 1, INDEX_NONE, this));
    AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UGA_Heal::StaticClass(), 1, INDEX_NONE, this));
}
```

### 激活技能

```cpp
// 按类激活
AbilitySystemComponent->TryActivateAbilityByClass(UGA_Fireball::StaticClass());

// 按标签激活
FGameplayTagContainer TagContainer;
TagContainer.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Attack.Fireball")));
AbilitySystemComponent->TryActivateAbilitiesByTag(TagContainer);
```

---

## Gameplay Effects

### 创建 Gameplay Effect（伤害）

```cpp
// 创建蓝图：Content Browser > Gameplay > Gameplay Effect

// 或在 C++ 中：
UCLASS()
class UGE_Damage : public UGameplayEffect {
    GENERATED_BODY()

public:
    UGE_Damage() {
        // 瞬时伤害
        DurationPolicy = EGameplayEffectDurationType::Instant;

        // 修改器：减少生命值
        FGameplayModifierInfo ModifierInfo;
        ModifierInfo.Attribute = UMyAttributeSet::GetHealthAttribute();
        ModifierInfo.ModifierOp = EGameplayModOp::Additive;
        ModifierInfo.ModifierMagnitude = FScalableFloat(-25.0f); // -25 生命值

        Modifiers.Add(ModifierInfo);
    }
};
```

### 应用 Gameplay Effect

```cpp
// 对目标施加伤害
if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target)) {
    FGameplayEffectContextHandle EffectContext = AbilitySystemComponent->MakeEffectContext();
    EffectContext.AddSourceObject(this);

    FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(
        UGE_Damage::StaticClass(), 1, EffectContext);

    if (SpecHandle.IsValid()) {
        AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
    }
}
```

---

## Gameplay Tags

### 定义标签

`Project Settings > Project > Gameplay Tags > Gameplay Tag List`

示例层级：
```
Ability
  ├─ Ability.Attack
  │   ├─ Ability.Attack.Melee
  │   └─ Ability.Attack.Ranged
  ├─ Ability.Defend
  └─ Ability.Utility

Status
  ├─ Status.Stunned
  ├─ Status.Invulnerable
  └─ Status.Silenced
```

### 在技能中使用标签

```cpp
UCLASS()
class UGA_MeleeAttack : public UGameplayAbility {
    GENERATED_BODY()

public:
    UGA_MeleeAttack() {
        // 此技能拥有这些标签
        AbilityTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Attack.Melee")));

        // 激活时阻止这些标签
        BlockAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Attack")));

        // 激活时取消这些技能
        CancelAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Defend")));

        // 如果目标有这些标签则无法激活
        ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Status.Stunned")));
    }
};
```

---

## 冷却时间与消耗

### 添加冷却时间

```cpp
// 在技能蓝图或 C++ 中：
// 创建持续时间为冷却时间的 Gameplay Effect
// 分配到 Ability > Cooldown Gameplay Effect Class
```

### 添加消耗（法力值）

```cpp
// 创建减少法力值的 Gameplay Effect
// 分配到 Ability > Cost Gameplay Effect Class
```

---

## 常用模式

### 获取当前属性值

```cpp
float CurrentHealth = AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
```

### 监听属性变化

```cpp
AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UMyAttributeSet::GetHealthAttribute())
    .AddUObject(this, &AMyCharacter::OnHealthChanged);

void AMyCharacter::OnHealthChanged(const FOnAttributeChangeData& Data) {
    UE_LOG(LogTemp, Warning, TEXT("Health: %f"), Data.NewValue);
}
```

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/gameplay-ability-system-for-unreal-engine/
- https://github.com/tranek/GASDocumentation（社区指南）
