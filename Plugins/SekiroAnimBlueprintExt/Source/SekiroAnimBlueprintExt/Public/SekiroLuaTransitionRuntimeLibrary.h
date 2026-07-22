#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SekiroLuaTransitionRuntimeLibrary.generated.h"

class UAnimInstance;

/** 在游戏线程执行 Lua 动画更新与 Transition Rule。 */
UCLASS()
class SEKIROANIMBLUEPRINTEXT_API USekiroLuaTransitionRuntimeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── AnimInstance Update ──────────────────────────────────
    /** 在游戏线程调用 Lua BlueprintUpdateAnimation(Inst, DeltaSeconds)。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Lua Update")
    static bool EvaluateBlueprintUpdateAnimation(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        float DeltaSeconds);

    // ── Transition Rule ──────────────────────────────────────
    /** 按需执行 Lua Rule 并直接返回严格 boolean 结果。 */
    UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Lua Transition")
    static bool EvaluateLuaTransitionRule(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        const FString& RuleFunctionName);

    // ── Transition Debug Pass-through ────────────────────────
    UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Lua Debug")
    static bool RecordBoolTransitionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        const FString& ParameterName,
        bool ActualValue,
        bool ExpectedValue,
        bool Result,
        bool bIsFinal);

    UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Lua Debug")
    static bool RecordFloatTransitionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        const FString& ParameterName,
        float ActualValue,
        float Threshold,
        bool Result,
        bool bIsFinal);

    UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Lua Debug")
    static bool RecordTransitionExpressionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        bool Result,
        bool bIsFinal);
};
