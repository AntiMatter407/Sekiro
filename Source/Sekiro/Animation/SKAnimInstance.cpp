#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

int32 USKAnimInstance::GetActionPriority(FName Action)
{
	static const TMap<FName, int32> PriorityMap = {
		{TEXT("Deathblow"),     10},
		{TEXT("Resurrection"),  10},
		{TEXT("Death"),          9},
		{TEXT("Hit"),            8},
		{TEXT("Dodge"),          7},
		{TEXT("Deflect"),        6},
		{TEXT("Guard"),          5},
		{TEXT("Prosthetic"),     4},
		{TEXT("ItemUse"),        3},
		{TEXT("Attack"),         2},
		{TEXT("Quickstep"),      1},
	};
	const int32* P = PriorityMap.Find(Action);
	return P ? *P : 0;
}

void USKAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
	if (OwnerCharacter)
	{
		OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
	}
}

void USKAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<ASKCharacter>(TryGetPawnOwner());
		if (OwnerCharacter)
		{
			OwnerMovement = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
		}
	}

	if (!OwnerCharacter)
	{
		return;
	}

	// Locomotion
	bIsInAir = OwnerCharacter->GetCharacterMovement()->IsFalling();
	bIsCrouching = OwnerCharacter->bIsCrouched;

	if (OwnerMovement)
	{
		MovementTier = OwnerMovement->CurrentMovementTier;
	}

	Speed = OwnerCharacter->GetVelocity().Size2D();
	Angle = UKismetAnimationLibrary::CalculateDirection(OwnerCharacter->GetVelocity(), OwnerCharacter->GetActorRotation());

	// Dodge state
	bIsDodging = OwnerCharacter->bIsDodging;
	DodgeDirection = OwnerCharacter->DodgeDirection;
	DodgeDirectionLateral = OwnerCharacter->DodgeDirectionLateral;

	// TAE: 更新当前动画时间 + 从曲线读取帧标志
	UAnimMontage* CurrentMontage = GetCurrentActiveMontage();
	if (CurrentMontage)
	{
		CurrentAnimTime = Montage_GetPosition(CurrentMontage);
		CurrentAnimLength = CurrentMontage->GetPlayLength();

		// 从 "FrameFlags" 曲线读取位掩码（替代旧的 AnimFrameFlags 查表）
		float CurveValue = 0.f;
		if (GetCurveValue(TEXT("FrameFlags"), CurveValue))
		{
			int32 Mask = FMath::RoundToInt(CurveValue);
			bDisableTurning  = (Mask & (1 << (uint8)ESKFrameFlag::DisableTurning)) != 0;
			bDisableMovement = (Mask & (1 << (uint8)ESKFrameFlag::DisableMovement)) != 0
			                || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedWalk)) != 0
			                || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedDash)) != 0;
			bCanDeflect      = (Mask & (1 << (uint8)ESKFrameFlag::EnableParry)) != 0
			                && (Mask & (1 << (uint8)ESKFrameFlag::DisableParry)) == 0;
			bInvincible      = (Mask & (1 << (uint8)ESKFrameFlag::Invincible)) != 0;
		}
	}
}

bool USKAnimInstance::CanCancelTo(FName TargetAction, float& OutCrossfade) const
{
	// 从 "CancelActions" 曲线读取当前帧可取消的动作
	// 曲线值 = ESKCancelAction 枚举值，0 = 无取消
	float CurveValue = 0.f;
	if (!GetCurveValue(TEXT("CancelActions"), CurveValue))
		return false;

	ESKCancelAction CancelAction = (ESKCancelAction)FMath::RoundToInt(CurveValue);

	int32 CurrentPrio = GetActionPriority(CurrentAction);
	int32 TargetPrio = GetActionPriority(TargetAction);
	if (TargetPrio <= CurrentPrio && CurrentAction != NAME_None)
		return false;

	// 将 TargetAction 映射为 ESKCancelAction 匹配
	static const TMap<FName, ESKCancelAction> ActionMap = {
		{TEXT("Attack"),     ESKCancelAction::Attack},
		{TEXT("Guard"),      ESKCancelAction::Guard},
		{TEXT("Dodge"),      ESKCancelAction::Dodge},
		{TEXT("Prosthetic"), ESKCancelAction::Prosthetic},
		{TEXT("Item"),       ESKCancelAction::Item},
	};

	const ESKCancelAction* Target = ActionMap.Find(TargetAction);
	if (!Target) return false;

	if (CancelAction != *Target)
		return false;

	OutCrossfade = 0.1f;
	return true;
}

bool USKAnimInstance::IsHitboxActive() const
{
	float CurveValue = 0.f;
	if (!GetCurveValue(TEXT("AttackHitbox"), CurveValue))
		return false;

	return (ESKAttackHitboxType)FMath::RoundToInt(CurveValue) != ESKAttackHitboxType::None;
}
