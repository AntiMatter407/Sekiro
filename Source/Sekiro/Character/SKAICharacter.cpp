#include "Character/SKAICharacter.h"

#include "Movement/SKAICharacterMovementComponent.h"

/**
 * 使用 AI 专用 CharacterMovement 构造精简角色，并在父类创建可选默认子对象前抑制所有玩家专属组件。
 * 本函数只在 UObject 构造阶段调用；保留 ASKCharacter 的网格、胶囊、武器、战斗和动画兼容状态，
 * 不创建相机、玩家输入、相机管理或锁定 UI，也不访问世界与运行时 Lua 环境。
 *
 * @param ObjectInitializer 引擎提供的默认子对象初始化器；函数在构造链中写入移动类型与组件抑制规则。
 */
ASKAICharacter::ASKAICharacter(const FObjectInitializer& ObjectInitializer)
	: Super(
		ObjectInitializer
			.SetDefaultSubobjectClass<USKAICharacterMovementComponent>(ACharacter::CharacterMovementComponentName)
			.DoNotCreateDefaultSubobject(TEXT("CameraBoom"))
			.DoNotCreateDefaultSubobject(TEXT("FollowCamera"))
			.DoNotCreateDefaultSubobject(TEXT("InputManager"))
			.DoNotCreateDefaultSubobject(TEXT("CameraManager"))
			.DoNotCreateDefaultSubobject(TEXT("LockOnIndicator")),
		FSKMovementComponentOverrideTag())
{
}
