#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/SKMovementComponent.h"
#include "SekiroAnimLogicData.h"
#include "SKAnimInstance.generated.h"

class ASKCharacter;
struct FAnimNode_BlendSpacePlayer;

UCLASS()
class SEKIRO_API USKAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ── Locomotion ──────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Angle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	ESKMovementTier MovementTier = ESKMovementTier::Run;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	uint32 bIsInAir : 1;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	uint32 bIsCrouching : 1;

	// ── Dodge ───────────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "Dodge")
	uint32 bIsDodging : 1;

	UPROPERTY(BlueprintReadOnly, Category = "Dodge")
	float DodgeDirection = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Dodge")
	float DodgeDirectionLateral = 0.f;

	// ── TAE 动作状态 ───────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FName CurrentAction;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	int32 CurrentAnimID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float CurrentAnimTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float CurrentAnimLength = 0.f;

	// ── JumpTable 等价标志 ─────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "Flags")
	uint32 bCanDeflect : 1;

	UPROPERTY(BlueprintReadOnly, Category = "Flags")
	uint32 bDisableTurning : 1;

	UPROPERTY(BlueprintReadOnly, Category = "Flags")
	uint32 bDisableMovement : 1;

	// ── 输入意图 ───────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FName InputIntent;

	// ── 数据资产引用 ───────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	TObjectPtr<USKAnimationLogicData> AnimLogicData;

	// ── 查询接口 ───────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "Cancel")
	bool CanCancelTo(FName TargetAction, float& OutCrossfade) const;

	UFUNCTION(BlueprintCallable, Category = "Attack")
	bool GetCurrentHitbox(FSKAttackHitboxConfig& OutConfig) const;

	UFUNCTION(BlueprintCallable, Category = "Cancel")
	static int32 GetActionPriority(FName Action);

protected:
	UPROPERTY()
	TObjectPtr<ASKCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<USKMovementComponent> OwnerMovement;

	// Locomotion BlendSpace 节点缓存（NativeInitializeAnimation 中初始化的裸指针）
	FAnimNode_BlendSpacePlayer* CachedBlendSpacePlayer = nullptr;
};
