#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/SekiroMovementComponent.h"
#include "SekiroAnimInstance.generated.h"

class ASekiroCharacter;

UCLASS()
class SEKIRO_API USekiroAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float Speed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float Angle = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	ESekiroMovementTier MovementTier = ESekiroMovementTier::Run;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	uint32 bIsInAir : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	uint32 bIsCrouching : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dodge")
	uint32 bIsDodging : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dodge")
	float DodgeDirection = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dodge")
	float DodgeDirectionLateral = 0.f;

protected:
	UPROPERTY()
	TObjectPtr<ASekiroCharacter> SekiroCharacter;

	UPROPERTY()
	TObjectPtr<USekiroMovementComponent> SekiroMovementComponent;
};
