#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "SKNumericGameplayEffect.generated.h"

/** 数值系统唯一原生即时效果模板，参数由 SetByCaller 注入。 */
UCLASS()
class SEKIRO_API USKNumericGameplayEffect : public UGameplayEffect
{
    GENERATED_BODY()

public:
    // ── 模板构造 ──────────────────────────────────────────
    USKNumericGameplayEffect();
};

UCLASS()
class SEKIRO_API USKNumericExecution : public UGameplayEffectExecutionCalculation
{
    GENERATED_BODY()

public:
    // ── 即时执行 ──────────────────────────────────────────
    virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& Parameters,
        FGameplayEffectCustomExecutionOutput& Output) const override;
};

