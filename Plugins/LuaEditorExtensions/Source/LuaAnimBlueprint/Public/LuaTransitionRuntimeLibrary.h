#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LuaTransitionRuntimeLibrary.generated.h"

class UAnimInstance;

/** 执行游戏线程 Lua 更新、兼容 Rule，以及任意线程安全的原生 Transition 调试透传。 */
UCLASS()
class LUAANIMBLUEPRINT_API ULuaTransitionRuntimeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── AnimInstance Update ──────────────────────────────────
    /** 在游戏线程调用 Lua BlueprintUpdateAnimation(Inst, DeltaSeconds)。 */
    UFUNCTION(BlueprintCallable, Category = "Lua|Animation|Lua Update")
    static bool EvaluateBlueprintUpdateAnimation(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        float DeltaSeconds);

    // ── Transition Rule ──────────────────────────────────────
    /** 按需执行 Lua Rule 并直接返回严格 boolean 结果。 */
    UFUNCTION(BlueprintPure, Category = "Lua|Animation|Lua Transition")
    static bool EvaluateLuaTransitionRule(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        const FString& RuleFunctionName);

    // ── Transition Debug Pass-through ────────────────────────
    UFUNCTION(BlueprintPure, Category = "Lua|Animation|Lua Debug", meta = (BlueprintThreadSafe))
    static bool RecordBoolTransitionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        const FString& ParameterName,
        bool ActualValue,
        bool ExpectedValue,
        bool Result,
        bool bIsFinal);

    UFUNCTION(BlueprintPure, Category = "Lua|Animation|Lua Debug", meta = (BlueprintThreadSafe))
    static bool RecordFloatTransitionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        const FString& ParameterName,
        float ActualValue,
        float Threshold,
        bool Result,
        bool bIsFinal);

    UFUNCTION(BlueprintPure, Category = "Lua|Animation|Lua Debug", meta = (BlueprintThreadSafe))
    static bool RecordTransitionExpressionDebugValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        bool Result,
        bool bIsFinal);

    /** 编辑器蓝图编译及 PIE/SIE 边界清除失败抑制与动态类属性缓存。 */
    static void ResetRuntimeCachesForPIESession();
};
