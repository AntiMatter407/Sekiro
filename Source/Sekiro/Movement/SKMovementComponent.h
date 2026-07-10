#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SKMovementComponent.generated.h"

UENUM(BlueprintType)
enum class ESKMovementTier : uint8
{
	Idle,                                                           // 静止
	Walk,                                                           // 步行
	Run,                                                            // 奔跑
	Sprint,                                                         // 冲刺
	Crouch                                                          // 蹲行
};

UCLASS()
class SEKIRO_API USKMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	USKMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float WalkSpeed = 140.f;                                      // Walk 循环稳健阈值

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float RunSpeed = 407.f;                                       // Run 循环稳健阈值

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed")
	float SprintSpeed = 853.f;                                    // Sprint 循环稳健阈值

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State")
	ESKMovementTier CurrentMovementTier = ESKMovementTier::Run;

	virtual float GetMaxSpeed() const override;
};
