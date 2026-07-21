// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/SKWeaponComponent.h"

/**
 * 创建旧 SKWeaponComponent 序列化模板的兼容实例。
 * 本类不增加状态、默认资源或装载流程，全部运行时行为仅继承 USKWeaponManagerComponent；
 * 因而旧蓝图可以安全加载，同时仍只有一套 Lua WeaponManager 业务逻辑。
 * 仅允许 Unreal 对旧资产反序列化时在游戏线程构造。
 */
USKWeaponComponent::USKWeaponComponent()
{
}
