#include "Movement/SKAICharacterMovementComponent.h"

/**
 * 初始化面向导航路径的原生旋转默认值。
 * 本函数只在 UObject 构造阶段调用；不访问 Owner、控制器或世界，Lua 可在运行时覆盖旋转策略。
 */
USKAICharacterMovementComponent::USKAICharacterMovementComponent()
{
	bOrientRotationToMovement = true;
	bUseControllerDesiredRotation = false;
}

/**
 * 向 UnLua 返回 AI CharacterMovement 的稳定项目模块映射。
 * 本函数只读且不加载 Lua；UnLua 在游戏线程绑定该独立 UClass 时调用。
 *
 * @return 固定返回 Gameplay.Sekiro.AI.SKAICharacterMovement，对应项目 AI 移动脚本模块。
 */
FString USKAICharacterMovementComponent::GetModuleName_Implementation() const
{
	return TEXT("Gameplay.Sekiro.AI.SKAICharacterMovement");
}
