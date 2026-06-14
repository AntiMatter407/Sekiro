#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SekiroAnimLogicData.generated.h"

// ============================================================================
// 运行时动画逻辑数据资产（从 TAE 提取，供 AnimInstance 运行时查询）
// ============================================================================

USTRUCT(BlueprintType)
struct FSKCancelRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 EndFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName TargetAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CrossfadeDuration = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Priority = 0;

	bool IsInWindow(int32 Frame) const { return Frame >= StartFrame && Frame <= EndFrame; }
};

USTRUCT(BlueprintType)
struct FSKCancelRuleList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FSKCancelRule> Rules;
};

USTRUCT(BlueprintType)
struct FSKAttackHitboxConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 EndFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BehaviorJudgeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 AttackType = 0;
};

USTRUCT(BlueprintType)
struct FSKSpEffectConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SpEffectID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 EndFrame = 0;
};

USTRUCT(BlueprintType)
struct FSKAnimIDList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<int32> IDs;
};

UCLASS(BlueprintType)
class SEKIROIMPORT_API USKAnimationLogicData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	TMap<int32, FSKCancelRuleList> CancelRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TMap<int32, FSKAttackHitboxConfig> AttackHitboxConfigs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpEffect")
	TMap<int32, FSKSpEffectConfig> SpEffectConfigs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<int32, FString> AnimNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<FString, FSKAnimIDList> CategoryAnimMap;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool CanCancelTo(int32 AnimID, float CurrentTime, FName TargetAction, float& OutCrossfade) const;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool GetAttackHitboxAtFrame(int32 AnimID, int32 Frame, FSKAttackHitboxConfig& OutConfig) const;
};
