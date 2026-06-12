#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SekiroMovementComponent.generated.h"

UENUM(BlueprintType)
enum class ESekiroMovementTier : uint8
{
	Walk,
	Jog,
	Run,
	Sprint,
	Crouch
};

UCLASS()
class SEKIRO_API USekiroMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	USekiroMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float WalkSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float JogSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float RunSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float SprintSpeed = 600.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State")
	ESekiroMovementTier CurrentMovementTier = ESekiroMovementTier::Run;

	virtual float GetMaxSpeed() const override;
};
