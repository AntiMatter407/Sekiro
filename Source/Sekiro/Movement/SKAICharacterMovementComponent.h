#pragma once

#include "CoreMinimal.h"
#include "Movement/SKMovementComponent.h"
#include "SKAICharacterMovementComponent.generated.h"

UCLASS()
class SEKIRO_API USKAICharacterMovementComponent : public USKMovementComponent
{
	GENERATED_BODY()

public:
	USKAICharacterMovementComponent();

	/** 返回 AI CharacterMovement 使用的稳定 Lua 模块名。 */
	virtual FString GetModuleName_Implementation() const override;
};
