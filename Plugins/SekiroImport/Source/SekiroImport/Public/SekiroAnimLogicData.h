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

// ── 帧级行为标志（16 位位域，对齐 JumpTable ID 映射）─────────
USTRUCT(BlueprintType)
struct FSKFrameFlags
{
	GENERATED_BODY()

	UPROPERTY() uint32 bDisableTurning : 1;                         // JumpTableID 7
	UPROPERTY() uint32 bDisableMovement : 1;                        // JumpTableID 89
	UPROPERTY() uint32 bDisableMapHit : 1;                          // JumpTableID 19
	UPROPERTY() uint32 bEnableParry : 1;                            // JumpTableID 119
	UPROPERTY() uint32 bDisableParry : 1;                           // JumpTableID 137
	UPROPERTY() uint32 bDisableSpecial : 1;                         // JumpTableID 133（义手/战技）
	UPROPERTY() uint32 bDisableItem : 1;                            // JumpTableID 134
	UPROPERTY() uint32 bInvincible : 1;                             // JumpTableID 51
	UPROPERTY() uint32 bSetNoGravity : 1;                           // JumpTableID 27
	UPROPERTY() uint32 bFlagAsDodging : 1;                          // JumpTableID 8
	UPROPERTY() uint32 bInvokeDeath : 1;                            // JumpTableID 12
	UPROPERTY() uint32 bLimitMoveSpeedWalk : 1;                     // JumpTableID 90
	UPROPERTY() uint32 bLimitMoveSpeedDash : 1;                     // JumpTableID 91
	UPROPERTY() uint32 bEnterMovement : 1;                          // JumpTableID 32
	UPROPERTY() uint32 bExitMovement : 1;                           // JumpTableID 31
	UPROPERTY() uint32 bStaggered : 1;                              // JumpTableID 55
};

// ── 帧级数据（关键帧存储，减少冗余）────────────────────────────
USTRUCT(BlueprintType)
struct FSKAnimFrameData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> KeyFrames;                                        // 关键帧号

	UPROPERTY()
	TArray<FSKFrameFlags> Flags;                                    // 对应标志（与 KeyFrames 同索引）
};

// ── 攻击盒列表（多盒支持）─────────────────────────────────────
USTRUCT(BlueprintType)
struct FSKAttackHitboxList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSKAttackHitboxConfig> Hitboxes;                         // 该动画的所有攻击盒
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
	TMap<int32, FSKAttackHitboxList> AttackHitboxConfigs;           // AnimID → 攻击盒列表（多盒）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpEffect")
	TMap<int32, FSKSpEffectConfig> SpEffectConfigs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FrameFlags")
	TMap<int32, FSKAnimFrameData> AnimFrameFlags;                   // AnimID → 帧级行为标志

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<int32, FString> AnimNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<FString, FSKAnimIDList> CategoryAnimMap;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool CanCancelTo(int32 AnimID, float CurrentTime, FName TargetAction, float& OutCrossfade) const;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool GetAttackHitboxAtFrame(int32 AnimID, int32 Frame, FSKAttackHitboxConfig& OutConfig) const;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool GetFrameFlags(int32 AnimID, int32 Frame, FSKFrameFlags& OutFlags) const;  // 查询帧级标志

	// 获取当前帧所有激活的攻击盒（支持多盒）
	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	void GetActiveHitboxesAtFrame(int32 AnimID, int32 Frame, TArray<FSKAttackHitboxConfig>& OutHitboxes) const;
};
