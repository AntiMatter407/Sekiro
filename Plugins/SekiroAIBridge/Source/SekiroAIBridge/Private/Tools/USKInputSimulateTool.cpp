#include "Tools/USKInputSimulateTool.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SekiroAIBridgeLog.h"
#include "TimerManager.h"

// ============================================================================
// 动作 → UInputAction 资产路径映射
// Schema 中的 action 名 → 项目中的 InputAction 资产路径
// ============================================================================

struct FActionToInputAction
{
	const TCHAR* ActionName;            // Schema 中的动作名（小写）
	const TCHAR* AssetPath;             // InputAction 资产路径
	bool bIsAxis;                       // true=Axis1D/Axis2D, false=bool
};

static const FActionToInputAction ActionInputActionMappings[] =
{
	// 按键类（bool）
	{ TEXT("attack"),          TEXT("/Game/Input/Actions/IA_Attack.IA_Attack"),          false },
	{ TEXT("guard"),           TEXT("/Game/Input/Actions/IA_Guard.IA_Guard"),             false },
	{ TEXT("dodge"),           TEXT("/Game/Input/Actions/IA_Dodge.IA_Dodge"),             false },
	{ TEXT("jump"),            TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"),               false },
	{ TEXT("interact"),        TEXT("/Game/Input/Actions/IA_Interact.IA_Interact"),       false },
	{ TEXT("use_item"),        TEXT("/Game/Input/Actions/IA_UseItem.IA_UseItem"),         false },
	{ TEXT("healing_gourd"),   TEXT("/Game/Input/Actions/IA_HealingGourd.IA_HealingGourd"), false },
	{ TEXT("grapple"),         TEXT("/Game/Input/Actions/IA_Grapple.IA_Grapple"),         false },
	{ TEXT("prosthetic"),      TEXT("/Game/Input/Actions/IA_Prosthetic.IA_Prosthetic"),   false },
	{ TEXT("lock_on"),         TEXT("/Game/Input/Actions/IA_LockOn.IA_LockOn"),           false },
	{ TEXT("crouch"),          TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"),           false },
	{ TEXT("cycle_item_next"), TEXT("/Game/Input/Actions/IA_CycleItemNext.IA_CycleItemNext"), false },
	{ TEXT("cycle_item_prev"), TEXT("/Game/Input/Actions/IA_CycleItemPrev.IA_CycleItemPrev"), false },
	{ TEXT("pause"),           TEXT("/Game/Input/Actions/IA_Pause.IA_Pause"),             false },
	{ TEXT("menu"),            TEXT("/Game/Input/Actions/IA_Menu.IA_Menu"),               false },

	// 轴类（Axis2D）
	{ TEXT("move"),            TEXT("/Game/Input/Actions/IA_Move.IA_Move"),                true  },
	{ TEXT("look"),            TEXT("/Game/Input/Actions/IA_Look.IA_Look"),                true  },
};

static constexpr int32 NumActionMappings = sizeof(ActionInputActionMappings) / sizeof(ActionInputActionMappings[0]);

// ============================================================================
// 工具描述 / Schema
// ============================================================================

FString USKInputSimulateTool::GetToolDescription() const
{
	return TEXT("PIE 运行时模拟玩家输入（通过 Enhanced Input 完整管道注入）："
	           "支持攻击/防御/闪避/跳跃/移动/视角等全部动作，支持长按和延迟执行。"
	           "与直接调用回调不同，此工具将输入注入到输入堆栈，经过 Trigger/Modifier 等完整处理。"
	           "移动/视角是持续型动作，需要显式触发相应的 stop 操作来停止。");
}

FString USKInputSimulateTool::GetInputSchemaJson() const
{
	return TEXT(
		"{"
		"\"type\":\"object\","
		"\"properties\":{"
			"\"action\":{"
				"\"type\":\"string\","
				"\"enum\":["
					"\"attack\",\"attack_release\",\"guard\",\"guard_release\","
					"\"dodge\",\"dodge_release\",\"jump\",\"jump_release\","
					"\"interact\",\"use_item\",\"healing_gourd\",\"grapple\","
					"\"prosthetic\",\"lock_on\",\"crouch\","
					"\"move\",\"move_stop\",\"look\",\"look_stop\","
					"\"cycle_item_next\",\"cycle_item_prev\",\"pause\",\"menu\""
				"],"
				"\"description\":\"Input action to simulate. "
					"Button actions (attack/guard/dodge/jump): inject then auto-release after hold_time. "
					"Release actions (attack_release/guard_release/...): inject the release event. "
					"Axis actions (move/look): inject axis value, use move_stop/look_stop to clear. "
					"Button-stops (move_stop/look_stop): inject zero to stop.\""
			"},"
			"\"value_x\":{\"type\":\"number\",\"description\":\"X component for move/look value (default 0)\"},"
			"\"value_y\":{\"type\":\"number\",\"description\":\"Y component for move/look value (default 0)\"},"
			"\"hold_time\":{\"type\":\"number\",\"description\":\"Hold duration in seconds (0 = pulse, >0 = hold and release after n seconds). For axis actions with hold_time, continues injecting until release.\"},"
			"\"delay\":{\"type\":\"number\",\"description\":\"Delay before executing (seconds)\"}"
		"},"
		"\"required\":[\"action\"]"
		"}"
	);
}

// ============================================================================
// Execute — 主入口
// ============================================================================

FString USKInputSimulateTool::Execute(const FString& ArgsJson, FString& OutError)
{
	TSharedPtr<FJsonObject> Args;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
	if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
	{
		OutError = TEXT("参数解析失败，需要有效的 JSON 对象");
		return FString();
	}

	// ── 解析参数 ──

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		OutError = TEXT("缺少必需参数: action");
		return FString();
	}

	double ValueX = 0.0;
	double ValueY = 0.0;
	double HoldTime = 0.0;
	double Delay = 0.0;

	Args->TryGetNumberField(TEXT("value_x"), ValueX);
	Args->TryGetNumberField(TEXT("value_y"), ValueY);
	Args->TryGetNumberField(TEXT("hold_time"), HoldTime);
	Args->TryGetNumberField(TEXT("delay"), Delay);

	// ── 检查 PIE 状态 ──

	if (!GEditor || !GEditor->IsPlayingSessionInEditor())
	{
		OutError = TEXT("PIE 未运行，请在 PIE 会话中执行 input.simulate");
		return FString();
	}

	UWorld* PlayWorld = nullptr;
	for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
		{
			PlayWorld = Ctx.World();
			break;
		}
	}

	if (!PlayWorld)
	{
		OutError = TEXT("无法获取 PIE World");
		return FString();
	}

	// ── 延迟执行 ──

	if (Delay > 0.0)
	{
		ExecuteWithDelay(PlayWorld, Action, ValueX, ValueY, Delay);

		FString Msg = FString::Printf(TEXT("已安排 %s 在 %.2f 秒后执行"), *Action, Delay);
		if (HoldTime > 0.0)
		{
			Msg += FString::Printf(TEXT("（按住 %.2f 秒后自动释放）"), HoldTime);
		}
		return BuildSuccessJson(Msg);
	}

	// ── 立即执行 ──

	FString Result = SimulateAction(PlayWorld, Action, ValueX, ValueY, OutError);
	if (!OutError.IsEmpty())
	{
		return FString();
	}

	// ── 长按后自动释放（仅按钮类动作） ──

	if (HoldTime > 0.0)
	{
		ScheduleRelease(PlayWorld, Action, HoldTime);
	}

	return Result;
}

// ============================================================================
// 模拟输入 — 核心逻辑
// ============================================================================

FString USKInputSimulateTool::SimulateAction(UWorld* World, const FString& Action, float ValueX, float ValueY, FString& OutError)
{
	// ── 处理 release 类动作 ──

	if (Action.EndsWith(TEXT("_release")))
	{
		FString BaseAction = Action.LeftChop(8); // 去掉 "_release"

		UInputAction* InputAction = GetInputAction(BaseAction, OutError);
		if (!InputAction)
		{
			return FString();
		}

		InjectInput(InputAction, FInputActionValue(false));

		UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: %s (release, inject false)"), *BaseAction);
		return BuildSuccessJson(FString::Printf(TEXT("已释放: %s"), *BaseAction));
	}

	// ── 处理 stop 类动作（move_stop / look_stop） ──

	if (Action.EndsWith(TEXT("_stop")))
	{
		FString BaseAction = Action.LeftChop(5); // 去掉 "_stop"

		UInputAction* InputAction = GetInputAction(BaseAction, OutError);
		if (!InputAction)
		{
			return FString();
		}

		InjectInput(InputAction, FInputActionValue(FVector2D::ZeroVector));

		UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: %s (stop, inject zero)"), *BaseAction);
		return BuildSuccessJson(FString::Printf(TEXT("已停止: %s"), *BaseAction));
	}

	// ── 正常动作 ──

	UInputAction* InputAction = GetInputAction(Action, OutError);
	if (!InputAction)
	{
		return FString();
	}

	// 判断是轴还是按钮：通过 ActionInputActionMappings 表查询
	bool bIsAxis = false;
	for (int32 i = 0; i < NumActionMappings; ++i)
	{
		if (FCString::Strcmp(ActionInputActionMappings[i].ActionName, *Action) == 0)
		{
			bIsAxis = ActionInputActionMappings[i].bIsAxis;
			break;
		}
	}

	// 构建输入值并注入
	FInputActionValue InputValue;

	if (bIsAxis)
	{
		InputValue = FInputActionValue(FVector2D(ValueX, ValueY));
	}
	else
	{
		InputValue = FInputActionValue(true);
	}

	InjectInput(InputAction, InputValue);

	UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: %s (x=%.2f, y=%.2f, isAxis=%d)"), *Action, ValueX, ValueY, bIsAxis ? 1 : 0);

	// ── 按钮类动作：安排 1 帧后自动释放（形成完整的 pulse），确保 Started/Completed 事件触发 ──

	if (!bIsAxis && World)
	{
		SchedulePulseRelease(World, InputAction);
	}

	return BuildSuccessJson(FString::Printf(TEXT("已模拟输入: %s"), *Action));
}

// ============================================================================
// 查找 Enhanced Input 子系统
// ============================================================================

UEnhancedInputLocalPlayerSubsystem* USKInputSimulateTool::FindEnhancedInputSubsystem(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor 不可用");
		return nullptr;
	}

	UWorld* PlayWorld = nullptr;
	for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
		{
			PlayWorld = Ctx.World();
			break;
		}
	}

	if (!PlayWorld)
	{
		OutError = TEXT("无法获取 PIE World");
		return nullptr;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(PlayWorld, 0);
	if (!PC)
	{
		OutError = TEXT("无法获取 Player Controller 0");
		return nullptr;
	}

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		OutError = TEXT("Player Controller 0 没有 LocalPlayer");
		return nullptr;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem)
	{
		OutError = TEXT("LocalPlayer 上没有 UEnhancedInputLocalPlayerSubsystem");
		return nullptr;
	}

	return Subsystem;
}

// ============================================================================
// 按动作名查找 UInputAction
// ============================================================================

UInputAction* USKInputSimulateTool::GetInputAction(const FString& Action, FString& OutError)
{
	// 在映射表中查找资产路径
	for (int32 i = 0; i < NumActionMappings; ++i)
	{
		if (FCString::Strcmp(ActionInputActionMappings[i].ActionName, *Action) == 0)
		{
			const FString& AssetPath = ActionInputActionMappings[i].AssetPath;

			UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *AssetPath);
			if (!InputAction)
			{
				// 尝试不带 . 后缀的路径
				FString AltPath = AssetPath;
				int32 DotPos;
				if (AltPath.FindLastChar('.', DotPos))
				{
					AltPath = AltPath.Left(DotPos);
				}
				InputAction = LoadObject<UInputAction>(nullptr, *AltPath);
			}

			if (!InputAction)
			{
				OutError = FString::Printf(TEXT("无法加载 InputAction: %s (%s)"), *Action, *AssetPath);
				return nullptr;
			}

			return InputAction;
		}
	}

	OutError = FString::Printf(TEXT("未知 action: %s，未在 InputAction 映射表中定义"), *Action);
	return nullptr;
}

// ============================================================================
// 注入输入
// ============================================================================

void USKInputSimulateTool::InjectInput(UInputAction* InputAction, const FInputActionValue& InputValue)
{
	FString Error;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = FindEnhancedInputSubsystem(Error);
	if (!Subsystem)
	{
		UE_LOG(LogSekiroAIBridge, Warning, TEXT("input.simulate: InjectInput 失败 — %s"), *Error);
		return;
	}

	// 注入到 Enhanced Input 系统
	// 空 Modifiers/Triggers 数组意味着使用 InputAction 自身的 Trigger 设定
	Subsystem->InjectInputForAction(InputAction, InputValue, TArray<UInputModifier*>(), TArray<UInputTrigger*>());

	UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: InjectInputForAction(%s, value_type=%d)"),
		*InputAction->GetName(), (int32)InputValue.GetValueType());
}

// ============================================================================
// 延迟执行
// ============================================================================

void USKInputSimulateTool::ExecuteWithDelay(UWorld* World, const FString& Action, float ValueX, float ValueY, float Delay)
{
	if (!World)
	{
		return;
	}

	FTimerHandle Handle;
	FTimerDelegate Delegate = FTimerDelegate::CreateLambda([World, Action, ValueX, ValueY]()
	{
		FString Error;
		SimulateAction(World, Action, ValueX, ValueY, Error);
		if (!Error.IsEmpty())
		{
			UE_LOG(LogSekiroAIBridge, Warning, TEXT("input.simulate 延迟执行失败: %s"), *Error);
		}
	});

	World->GetTimerManager().SetTimer(Handle, Delegate, Delay, false);
}

// ============================================================================
// 长按后自动释放
// ============================================================================

void USKInputSimulateTool::ScheduleRelease(UWorld* World, const FString& Action, float HoldTime)
{
	if (!World || HoldTime <= 0.0f)
	{
		return;
	}

	// 只对按钮类动作（非轴类）自动释放
	bool bIsAxis = false;
	for (int32 i = 0; i < NumActionMappings; ++i)
	{
		if (FCString::Strcmp(ActionInputActionMappings[i].ActionName, *Action) == 0)
		{
			bIsAxis = ActionInputActionMappings[i].bIsAxis;
			break;
		}
	}

	if (bIsAxis)
	{
		return; // 轴动作持续注入，不自动释放
	}

	FTimerHandle Handle;
	FTimerDelegate Delegate = FTimerDelegate::CreateLambda([Action]()
	{
		FString Error;

		// 找到对应的 InputAction
		UInputAction* InputAction = GetInputAction(Action, Error);
		if (!InputAction)
		{
			UE_LOG(LogSekiroAIBridge, Warning, TEXT("input.simulate 释放失败: %s"), *Error);
			return;
		}

		// 注入 false 模拟释放
		InjectInput(InputAction, FInputActionValue(false));

		UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: 自动释放 %s"), *Action);
	});

	World->GetTimerManager().SetTimer(Handle, Delegate, HoldTime, false);
}

// ============================================================================
// 脉冲释放（按钮类动作自动释放，1 帧后发 false）
// ============================================================================

void USKInputSimulateTool::SchedulePulseRelease(UWorld* World, const UInputAction* InputAction)
{
	if (!World || !InputAction)
	{
		return;
	}

	// 延迟 1 帧后注入 false（约 0.05s 或一个 delta 时间）
	const float PulseDelay = FMath::Max(World->GetDeltaSeconds(), 0.03f);

	FTimerHandle Handle;
	FTimerDelegate Delegate = FTimerDelegate::CreateLambda([InputAction]()
	{
		UInputAction* MutableAction = const_cast<UInputAction*>(InputAction);
		if (!MutableAction)
		{
			return;
		}

		// 注入 false 模拟释放，形成完整的 pulse (true → false)
		InjectInput(MutableAction, FInputActionValue(false));

		UE_LOG(LogSekiroAIBridge, Verbose, TEXT("input.simulate: pulse release %s (1-frame delay)"),
			*MutableAction->GetName());
	});

	World->GetTimerManager().SetTimer(Handle, Delegate, PulseDelay, false);
}

// ============================================================================
// 工具函数
// ============================================================================

FString USKInputSimulateTool::BuildSuccessJson(const FString& Message)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Message);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}
