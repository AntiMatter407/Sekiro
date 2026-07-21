// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Weapon/SKWeaponManagerComponent.h"
#include "SKWeaponComponent.generated.h"

/** 仅用于加载旧蓝图序列化模板；新代码应使用 USKWeaponManagerComponent。 */
UCLASS(ClassGroup=(Weapon), meta=(DeprecatedNode, DeprecationMessage="Use SKWeaponManagerComponent"))
class SEKIRO_API USKWeaponComponent : public USKWeaponManagerComponent
{
    GENERATED_BODY()

public:
    USKWeaponComponent();
};
