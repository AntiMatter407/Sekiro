#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/SKAnimDataTypes.h"
#include "SKAnimBlueprintLibrary.generated.h"

UCLASS()
class SEKIRO_API USKAnimBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @brief 将角色局部方向角离散为前、后、左、右四个基础移动方向。
	 *
	 * @param Angle float，待分类的局部方向角，单位为度；函数会将其规范到 [-180, 180]。
	 * @param ForwardAngleRange float，前向区域的半角，单位为度；绝对值会限制到 [0, 179]。
	 * @param BackwardAngleRange float，后向区域的起始绝对角，单位为度；绝对值会限制到
	 *        [ForwardAngleRange + 1, 180]。
	 * @return ESKLocomotionDirection，角度所属的 Fwd、Bwd、L 或 R 基础方向。
	 */
	UFUNCTION(BlueprintPure, Category = "Sekiro|Animation|Locomotion", meta = (DisplayName = "Get Locomotion Direction From Angle"))
	static ESKLocomotionDirection GetLocomotionDirectionFromAngle(float Angle, float ForwardAngleRange = 35.f, float BackwardAngleRange = 135.f);
};
