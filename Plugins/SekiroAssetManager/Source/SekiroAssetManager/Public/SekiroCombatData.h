#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SekiroCombatData.generated.h"

// ============================================================================
// 连段派生数据结构（从 BehaviorParam_PC.param 提取）
// ============================================================================

// 单个连段条目：从当前 AnimID 出发，在各种输入下派生到什么动画
USTRUCT(BlueprintType)
struct FSKComboEntry
{
    GENERATED_BODY()

    // R1 攻击连段的下一个 AnimID（按 R1 时从当前动画派生到哪个动画）
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnR1 = -1;

    // 蓄力攻击派生
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnCharged = -1;

    // 防御取消派生（L1 按下时播什么）
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnGuard = -1;

    // 闪避取消派生（Dodge 按下时播什么）
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnDodge = -1;

    // 完美格挡后的反斩派生
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnCounter = -1;

    // 跳跃取消派生
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NextOnJump = -1;

    // 是否为连段终结段（之后回到 Idle）
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bIsComboEnd = false;
};

UCLASS(BlueprintType)
class SEKIROASSETMANAGER_API USKCombatData : public UDataAsset
{
    GENERATED_BODY()

public:
    // AnimID -> 连段派生表
    // 例如: 201010 -> {NextOnR1: 201011, NextOnGuard: 301000}
    //       201011 -> {NextOnR1: 201050, NextOnGuard: 301000}
    //       201050 -> {NextOnR1: -1, NextOnGuard: 301000, bIsComboEnd: true}
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
    TMap<int32, FSKComboEntry> ComboChain;

    // 从当前 AnimID 查 R1 连段的下一个 AnimID
    UFUNCTION(BlueprintCallable, Category = "Combo")
    int32 GetNextComboAnim(int32 CurrentAnimID) const;

    // 从当前 AnimID 查特定动作的派生动画
    UFUNCTION(BlueprintCallable, Category = "Combo")
    int32 GetDerivedAnim(int32 CurrentAnimID, FName Action) const;
};
