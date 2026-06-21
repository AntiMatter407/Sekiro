#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SekiroAnimLogicData.generated.h"

// ============================================================================
// 杩愯鏃跺姩鐢婚€昏緫鏁版嵁璧勪骇锛堜粠 TAE 鎻愬彇锛屼緵 AnimInstance 杩愯鏃舵煡璇級
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

// 鈹€鈹€ 甯х骇琛屼负鏍囧織锛?6 浣嶄綅鍩燂紝瀵归綈 JumpTable ID 鏄犲皠锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
USTRUCT(BlueprintType)
struct FSKFrameFlags
{
	GENERATED_BODY()

	UPROPERTY() uint32 bDisableTurning : 1;                         // JumpTableID 7
	UPROPERTY() uint32 bDisableMovement : 1;                        // JumpTableID 89
	UPROPERTY() uint32 bDisableMapHit : 1;                          // JumpTableID 19
	UPROPERTY() uint32 bEnableParry : 1;                            // JumpTableID 119
	UPROPERTY() uint32 bDisableParry : 1;                           // JumpTableID 137
	UPROPERTY() uint32 bDisableSpecial : 1;                         // JumpTableID 133锛堜箟鎵?鎴樻妧锛?
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

// 鈹€鈹€ 甯х骇鏁版嵁锛堝叧閿抚瀛樺偍锛屽噺灏戝啑浣欙級鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
USTRUCT(BlueprintType)
struct FSKAnimFrameData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> KeyFrames;                                        // 鍏抽敭甯у彿

	UPROPERTY()
	TArray<FSKFrameFlags> Flags;                                    // 瀵瑰簲鏍囧織锛堜笌 KeyFrames 鍚岀储寮曪級
};

// 鈹€鈹€ 鏀诲嚮鐩掑垪琛紙澶氱洅鏀寔锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
USTRUCT(BlueprintType)
struct FSKAttackHitboxList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSKAttackHitboxConfig> Hitboxes;                         // 璇ュ姩鐢荤殑鎵€鏈夋敾鍑荤洅
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
class SEKIROASSETMANAGER_API USKAnimationLogicData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	TMap<int32, FSKCancelRuleList> CancelRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TMap<int32, FSKAttackHitboxList> AttackHitboxConfigs;           // AnimID 鈫?鏀诲嚮鐩掑垪琛紙澶氱洅锛?

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpEffect")
	TMap<int32, FSKSpEffectConfig> SpEffectConfigs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FrameFlags")
	TMap<int32, FSKAnimFrameData> AnimFrameFlags;                   // AnimID 鈫?甯х骇琛屼负鏍囧織


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
	TMap<FString, FSKAnimIDList> CategoryAnimMap;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
    TMap<int32, FString> AnimPrefixMap;  // AnimID → 动画前缀 (a000, a010, a200...)

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool CanCancelTo(int32 AnimID, float CurrentTime, FName TargetAction, float& OutCrossfade) const;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool GetAttackHitboxAtFrame(int32 AnimID, int32 Frame, FSKAttackHitboxConfig& OutConfig) const;

	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	bool GetFrameFlags(int32 AnimID, int32 Frame, FSKFrameFlags& OutFlags) const;  // 鏌ヨ甯х骇鏍囧織

	// 鑾峰彇褰撳墠甯ф墍鏈夋縺娲荤殑鏀诲嚮鐩掞紙鏀寔澶氱洅锛?
	UFUNCTION(BlueprintCallable, Category = "Animation Logic")
	void GetActiveHitboxesAtFrame(int32 AnimID, int32 Frame, TArray<FSKAttackHitboxConfig>& OutHitboxes) const;
};

