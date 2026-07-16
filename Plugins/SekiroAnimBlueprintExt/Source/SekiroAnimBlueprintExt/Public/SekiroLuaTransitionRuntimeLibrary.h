#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SekiroLuaTransitionRuntimeLibrary.generated.h"

class UAnimInstance;

/** 在游戏线程执行 Lua Transition Rule，并向动画工作线程发布只读缓存。 */
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
    /** 执行 Lua Rule 并缓存严格 boolean 结果。 */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Animation|Lua Transition")
    static bool EvaluateAndCacheTransitionRule(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        const FString& RuleFunctionName);

    /** 从线程安全快照读取最近一次 Rule 结果。 */
    UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Lua Transition", meta = (BlueprintThreadSafe))
    static bool GetCachedTransitionRule(
        const UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        const FString& RuleFunctionName);
};
