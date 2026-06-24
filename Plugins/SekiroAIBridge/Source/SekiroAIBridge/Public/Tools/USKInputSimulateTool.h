#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "InputActionValue.h"
#include "USKInputSimulateTool.generated.h"

class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;

/**
 * 输入模拟工具
 *
 * 在 PIE 运行时模拟玩家输入，通过 InjectInputForAction 将输入事件注入
 * Enhanced Input 系统，经过完整输入管道后自动分发到绑定的回调函数。
 *
 * 与旧方案（反射调用 USKInputHandler 回调）的区别：
 * - 不再直接调用 OnXxxStarted/OnXxxCompleted 等 protected 方法
 * - 而是将输入注入到 UEnhancedInputLocalPlayerSubsystem，
 *   经过 MappingContext、Trigger、Modifier 等完整处理后到达回调
 *
 * 工具名称: input.simulate
 * 安全等级: 低（RequiresConfirmation = false），只注入输入，不影响资产
 */
UCLASS()
class SEKIROAIBRIDGE_API USKInputSimulateTool : public UObject, public ISKAIToolInterface
{
	GENERATED_BODY()

public:
	virtual FName GetToolName() const override { return FName(TEXT("input.simulate")); }
	virtual FString GetToolDescription() const override;
	virtual FString GetInputSchemaJson() const override;
	virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
	virtual bool RequiresConfirmation() const override { return false; }

private:
	/** 通过 InjectInputForAction 模拟一个输入动作（立即执行） */
	static FString SimulateAction(UWorld* World, const FString& Action, float ValueX, float ValueY, FString& OutError);

	/** 查找 PIE 中的 Enhanced Input 子系统 */
	static UEnhancedInputLocalPlayerSubsystem* FindEnhancedInputSubsystem(FString& OutError);

	/** 按动作名查找 UInputAction（通过反射从游戏模块加载） */
	static UInputAction* GetInputAction(const FString& Action, FString& OutError);

	/** 注入输入值到 Enhanced Input 子系统 */
	static void InjectInput(UInputAction* InputAction, const FInputActionValue& InputValue);

	/** 延迟后执行 */
	static void ExecuteWithDelay(UWorld* World, const FString& Action, float ValueX, float ValueY, float Delay);

	/** 长按后自动释放 */
	static void ScheduleRelease(UWorld* World, const FString& Action, float HoldTime);

	/** 按钮类动作：延迟 1 帧后自动注入 false（脉冲释放），确保 Started/Completed 事件触发 */
	static void SchedulePulseRelease(UWorld* World, const UInputAction* InputAction);

	/** 构建成功 JSON 响应 */
	static FString BuildSuccessJson(const FString& Message);
};
