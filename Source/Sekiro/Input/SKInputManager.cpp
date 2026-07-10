// Copyright Epic Games, Inc. All Rights Reserved.

#include "SKInputManager.h"
#include "Camera/SKCameraManagerComponent.h"
#include "Character/SKCharacter.h"
#include "Movement/SKMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
	static UnLua::FLuaRetValues RequireSKInputLuaModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
	{
		bOutSucceeded = false;
		if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

		lua_State* LuaState = LuaEnv->GetMainState();
		if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

		const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
		UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
		if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("SKInputManager Lua require failed. Module=%s"), *LuaModuleName);
			return ReturnValues;
		}

		if (ReturnValues[0].GetType() != LUA_TTABLE)
		{
			UE_LOG(LogTemp, Warning, TEXT("SKInputManager Lua module must return a table. Module=%s"), *LuaModuleName);
			return ReturnValues;
		}

		bOutSucceeded = true;
		return ReturnValues;
	}

	static bool ReadSKInputLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName, FName FunctionName)
	{
		if (!ReturnValues.IsValid()) return false;
		if (ReturnValues.Num() == 0) return false;
		if (ReturnValues[0].GetType() == LUA_TNIL) return false;

		if (ReturnValues[0].GetType() == LUA_TBOOLEAN)
		{
			return ReturnValues[0].Value<bool>();
		}

		UE_LOG(LogTemp, Warning, TEXT("SKInputManager Lua function should return boolean. Module=%s Function=%s"),
			*LuaModuleName,
			*FunctionName.ToString());
		return false;
	}

	static bool ResolveSKScreenInputWorldDirection(const ACharacter* Owner, float InputX, float InputY, FVector& OutDirection)
	{
		OutDirection = FVector::ZeroVector;
		if (!Owner) return false;

		const AController* Controller = Owner->GetController();
		if (!Controller) return false;

		const FVector2D Input(InputX, InputY);
		if (Input.IsNearlyZero()) return false;

		const FVector2D Normalized = Input.GetSafeNormal();
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		OutDirection = (Forward * Normalized.Y + Right * Normalized.X).GetSafeNormal2D();
		return !OutDirection.IsNearlyZero();
	}
}

//////////////////////////////////////////////////////////////////////////
// USKInputManager

USKInputManager::USKInputManager()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

//////////////////////////////////////////////////////////////////////////
// 初始化

void USKInputManager::SetupInput(UEnhancedInputComponent* Input)
{
	if (!Input) return;

	// ── 移动/视角 ──
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &USKInputManager::OnMove);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &USKInputManager::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &USKInputManager::OnLook);

	// ── 跳跃 ──
	Input->BindAction(JumpAction, ETriggerEvent::Started,   this, &USKInputManager::OnJumpStarted);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &USKInputManager::OnJumpCompleted);

	// ── 闪避/冲刺 ──
	Input->BindAction(DodgeAction, ETriggerEvent::Started,   this, &USKInputManager::OnDodgeStarted);
	Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &USKInputManager::OnDodgeCompleted);
	Input->BindAction(WalkModifierAction, ETriggerEvent::Started, this, &USKInputManager::OnWalkModifierStarted);
	Input->BindAction(WalkModifierAction, ETriggerEvent::Completed, this, &USKInputManager::OnWalkModifierCompleted);

	// ── 蹲下 ──
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &USKInputManager::OnCrouchStarted);

	// ── 战斗 ──
	Input->BindAction(AttackAction,     ETriggerEvent::Started,   this, &USKInputManager::OnAttackStarted);
	Input->BindAction(AttackAction,     ETriggerEvent::Completed, this, &USKInputManager::OnAttackCompleted);
	Input->BindAction(GuardAction,      ETriggerEvent::Started,   this, &USKInputManager::OnGuardStarted);
	Input->BindAction(GuardAction,      ETriggerEvent::Completed, this, &USKInputManager::OnGuardCompleted);
	Input->BindAction(LockOnAction,     ETriggerEvent::Started,   this, &USKInputManager::OnLockOnStarted);
	Input->BindAction(ProstheticAction, ETriggerEvent::Started,   this, &USKInputManager::OnProstheticStarted);
	Input->BindAction(ProstheticAction, ETriggerEvent::Completed, this, &USKInputManager::OnProstheticCompleted);
	Input->BindAction(GrappleAction,    ETriggerEvent::Started,   this, &USKInputManager::OnGrappleStarted);

	// ── 交互/道具 ──
	Input->BindAction(InteractAction,      ETriggerEvent::Started, this, &USKInputManager::OnInteractStarted);
	Input->BindAction(UseItemAction,       ETriggerEvent::Started, this, &USKInputManager::OnUseItemStarted);
	Input->BindAction(HealingGourdAction,  ETriggerEvent::Started, this, &USKInputManager::OnHealingGourdStarted);
	Input->BindAction(CycleItemNextAction, ETriggerEvent::Started, this, &USKInputManager::OnCycleItemNextStarted);
	Input->BindAction(CycleItemPrevAction, ETriggerEvent::Started, this, &USKInputManager::OnCycleItemPrevStarted);

	// ── 系统 ──
	Input->BindAction(PauseAction, ETriggerEvent::Started, this, &USKInputManager::OnPauseStarted);
	Input->BindAction(MenuAction,  ETriggerEvent::Started, this, &USKInputManager::OnMenuStarted);
}

void USKInputManager::AddMappingContext(APlayerController* PC)
{
	if (!PC || !DefaultMappingContext) return;

	if (UEnhancedInputLocalPlayerSubsystem* Sub =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Sub->AddMappingContext(DefaultMappingContext, 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// 消费型意图

bool USKInputManager::ConsumeAttackPressed()
{
	if (bAttackPressed)
	{
		bAttackPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeJumpPressed()
{
	if (bJumpPressed)
	{
		bJumpPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeDodgePressed()
{
	if (bDodgePressed)
	{
		bDodgePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeInteractPressed()
{
	if (bInteractPressed)
	{
		bInteractPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeUseItemPressed()
{
	if (bUseItemPressed)
	{
		bUseItemPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeHealingGourdPressed()
{
	if (bHealingGourdPressed)
	{
		bHealingGourdPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeGrapplePressed()
{
	if (bGrapplePressed)
	{
		bGrapplePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeProstheticPressed()
{
	if (bProstheticPressed)
	{
		bProstheticPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeLockOnPressed()
{
	if (bLockOnPressed)
	{
		bLockOnPressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCrouchToggled()
{
	if (bCrouchToggled)
	{
		bCrouchToggled = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCycleItemNext()
{
	if (bCycleItemNext)
	{
		bCycleItemNext = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeCycleItemPrev()
{
	if (bCycleItemPrev)
	{
		bCycleItemPrev = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumePausePressed()
{
	if (bPausePressed)
	{
		bPausePressed = false;
		return true;
	}
	return false;
}

bool USKInputManager::ConsumeMenuPressed()
{
	if (bMenuPressed)
	{
		bMenuPressed = false;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// 输入缓冲

bool USKInputManager::ConsumeBufferedInput(FName Action)
{
	// 升序遍历：移除匹配的第一个条目（FIFO 消费，先入先出）
	for (int32 i = 0; i < InputBuffer.Num(); ++i)
	{
		if (InputBuffer[i].Action == Action)
		{
			InputBuffer.RemoveAt(i);
			return true;
		}
	}
	return false;
}

void USKInputManager::ClearInputBuffer()
{
	InputBuffer.Reset();
}

//////////////////////////////////////////////////////////////////////////
// 持续型意图

FVector2D USKInputManager::GetMoveIntent() const
{
	return MoveIntent;
}

float USKInputManager::GetMoveInputAmount() const
{
	return MoveInputAmount;
}

FVector2D USKInputManager::GetLookIntent() const
{
	return LookIntent;
}

bool USKInputManager::IsAttackHeld() const
{
	return bAttackHeld;
}

bool USKInputManager::IsGuardHeld() const
{
	return bGuardHeld;
}

bool USKInputManager::IsProstheticHeld() const
{
	return bProstheticHeld;
}

bool USKInputManager::IsDodgeHeld() const
{
	return bDodgeHeld;
}

bool USKInputManager::IsDodgeActive() const
{
	return bDodgeActive;
}

bool USKInputManager::IsWalkHeld() const
{
	return bWalkHeld;
}

float USKInputManager::GetAttackHoldTime() const
{
	return AttackHoldTime;
}

float USKInputManager::GetProstheticHoldTime() const
{
	return ProstheticHoldTime;
}

//////////////////////////////////////////////////////////////////////////
// 连段

int32 USKInputManager::GetComboIndex() const
{
	return ComboIndex;
}

float USKInputManager::GetTimeSinceLastAttack() const
{
	return TimeSinceLastAttack;
}

//////////////////////////////////////////////////////////////////////////
// Lua 输入宿主

void USKInputManager::SetUseLuaInputLogic(bool bNewUseLuaInputLogic)
{
	bUseLuaInputLogic = bNewUseLuaInputLogic;
}

bool USKInputManager::IsUsingLuaInputLogic() const
{
	return bUseLuaInputLogic;
}

void USKInputManager::SetLuaInputModuleName(const FString& ModuleName)
{
	LuaInputModuleName = ModuleName;
}

FString USKInputManager::GetLuaInputModuleName() const
{
	return LuaInputModuleName;
}

FString USKInputManager::GetModuleName_Implementation() const
{
	return LuaInputModuleName;
}

float USKInputManager::GetSprintHoldThreshold() const
{
	return SprintHoldThreshold;
}

float USKInputManager::GetDodgeActiveDuration() const
{
	return DodgeActiveDuration;
}

float USKInputManager::GetMoveInputReleaseBufferDuration() const
{
	return MoveInputReleaseBufferDuration;
}

float USKInputManager::GetMoveInputReleaseBufferRemaining() const
{
	return MoveInputReleaseBufferRemaining;
}

void USKInputManager::SetMoveInputReleaseBufferRemaining(float RemainingTime)
{
	MoveInputReleaseBufferRemaining = FMath::Max(0.f, RemainingTime);
}

float USKInputManager::GetAnalogWalkEnterThreshold() const
{
	return AnalogWalkEnterThreshold;
}

float USKInputManager::GetAnalogRunEnterThreshold() const
{
	return AnalogRunEnterThreshold;
}

float USKInputManager::GetDodgeHoldTime() const
{
	return DodgeHoldTime;
}

void USKInputManager::SetDodgeHoldTime(float NewDodgeHoldTime)
{
	DodgeHoldTime = FMath::Max(0.f, NewDodgeHoldTime);
}

float USKInputManager::GetDodgeActiveTimeRemaining() const
{
	return DodgeActiveTimeRemaining;
}

void USKInputManager::SetDodgeActiveState(bool bNewDodgeActive, float ActiveTimeRemaining)
{
	bDodgeActive = bNewDodgeActive;
	DodgeActiveTimeRemaining = FMath::Max(0.f, ActiveTimeRemaining);
}

void USKInputManager::SetAttackHoldTime(float NewAttackHoldTime)
{
	AttackHoldTime = FMath::Max(0.f, NewAttackHoldTime);
}

void USKInputManager::SetProstheticHoldTime(float NewProstheticHoldTime)
{
	ProstheticHoldTime = FMath::Max(0.f, NewProstheticHoldTime);
}

void USKInputManager::SetComboState(int32 NewComboIndex, float NewTimeSinceLastAttack)
{
	ComboIndex = FMath::Max(0, NewComboIndex);
	TimeSinceLastAttack = FMath::Max(0.f, NewTimeSinceLastAttack);
}

float USKInputManager::GetWorldTimeSecondsForScript() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void USKInputManager::SetMoveIntentForScript(float InputX, float InputY, float InputAmount, float ReleaseBufferRemaining)
{
	MoveIntent = FVector2D(InputX, InputY);
	MoveInputAmount = FMath::Clamp(InputAmount, 0.f, 1.f);
	MoveInputReleaseBufferRemaining = FMath::Max(0.f, ReleaseBufferRemaining);
}

void USKInputManager::ClearMoveIntentForScript()
{
	MoveIntent = FVector2D::ZeroVector;
	MoveInputAmount = 0.f;
	MoveInputReleaseBufferRemaining = 0.f;
}

void USKInputManager::SetLookIntentForScript(float InputX, float InputY)
{
	LookIntent = FVector2D(InputX, InputY);
}

bool USKInputManager::AddMovementInputFromScreen(float InputX, float InputY)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	const AController* Controller = Owner->GetController();
	if (!Controller) return false;

	const FVector2D Input(InputX, InputY);
	if (Input.IsNearlyZero()) return false;

	const FVector2D Normalized = Input.GetSafeNormal();
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	Owner->AddMovementInput(Forward, Normalized.Y);
	Owner->AddMovementInput(Right, Normalized.X);
	return true;
}

bool USKInputManager::AddMovementImpulseFromScreen(float InputX, float InputY, float VelocityChange)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
	if (!Movement) return false;

	FVector WorldDirection = FVector::ZeroVector;
	if (!ResolveSKScreenInputWorldDirection(Owner, InputX, InputY, WorldDirection)) return false;

	const float SafeVelocityChange = FMath::Max(0.f, VelocityChange);
	if (SafeVelocityChange <= UE_KINDA_SMALL_NUMBER) return false;

	Movement->AddImpulse(WorldDirection * SafeVelocityChange, true);
	return true;
}

bool USKInputManager::AddLookInputToCamera(float InputX, float InputY)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	const FVector2D LookAxis(InputX, InputY);
	if (USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>())
	{
		CameraManager->AddLookInput(LookAxis);
		return true;
	}

	Owner->AddControllerYawInput(LookAxis.X);
	Owner->AddControllerPitchInput(LookAxis.Y);
	return true;
}

bool USKInputManager::IsOwnerFalling() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	return Owner && Owner->GetCharacterMovement() && Owner->GetCharacterMovement()->IsFalling();
}

bool USKInputManager::IsOwnerCrouched() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	return Owner && Owner->bIsCrouched;
}

bool USKInputManager::IsOwnerDodging() const
{
	const ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	return SekiroOwner && SekiroOwner->IsDodging();
}

bool USKInputManager::CanOwnerAirDodge() const
{
	const ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	return SekiroOwner && SekiroOwner->CanAirDodge();
}

void USKInputManager::JumpOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Jump();
	}
}

void USKInputManager::StopJumpingOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->StopJumping();
	}
}

void USKInputManager::CrouchOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Crouch();
	}
}

void USKInputManager::UnCrouchOwner()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->UnCrouch();
	}
}

void USKInputManager::SetOwnerDodging(bool bNewDodging)
{
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	if (SekiroOwner)
	{
		SekiroOwner->SetDodging(bNewDodging);
	}
}

void USKInputManager::SetOwnerDodgeDirection(float ForwardAmount, float LateralAmount)
{
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get());
	if (SekiroOwner)
	{
		SekiroOwner->SetDodgeDirection(ForwardAmount, LateralAmount);
	}
}

FName USKInputManager::GetMovementTierName() const
{
	const ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return FName(TEXT("Run"));

	const USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return FName(TEXT("Run"));

	switch (MoveComp->CurrentMovementTier)
	{
	case ESKMovementTier::Idle:
		return FName(TEXT("Idle"));
	case ESKMovementTier::Walk:
		return FName(TEXT("Walk"));
	case ESKMovementTier::Run:
		return FName(TEXT("Run"));
	case ESKMovementTier::Sprint:
		return FName(TEXT("Sprint"));
	case ESKMovementTier::Crouch:
		return FName(TEXT("Crouch"));
	default:
		return FName(TEXT("Run"));
	}
}

void USKInputManager::SetMovementTierByName(FName TierName)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return;

	MoveComp->CurrentMovementTier = ResolveMovementTierByName(TierName);
}

void USKInputManager::SetPressedFlag(FName ActionName, bool bPressed)
{
	const FString NormalizedName = ActionName.ToString().ToLower();
	if (NormalizedName == TEXT("attack")) bAttackPressed = bPressed;
	else if (NormalizedName == TEXT("jump")) bJumpPressed = bPressed;
	else if (NormalizedName == TEXT("dodge")) bDodgePressed = bPressed;
	else if (NormalizedName == TEXT("interact")) bInteractPressed = bPressed;
	else if (NormalizedName == TEXT("useitem") || NormalizedName == TEXT("item")) bUseItemPressed = bPressed;
	else if (NormalizedName == TEXT("healinggourd")) bHealingGourdPressed = bPressed;
	else if (NormalizedName == TEXT("grapple")) bGrapplePressed = bPressed;
	else if (NormalizedName == TEXT("prosthetic")) bProstheticPressed = bPressed;
	else if (NormalizedName == TEXT("lockon")) bLockOnPressed = bPressed;
	else if (NormalizedName == TEXT("crouch")) bCrouchToggled = bPressed;
	else if (NormalizedName == TEXT("cycleitemnext")) bCycleItemNext = bPressed;
	else if (NormalizedName == TEXT("cycleitemprev")) bCycleItemPrev = bPressed;
	else if (NormalizedName == TEXT("pause")) bPausePressed = bPressed;
	else if (NormalizedName == TEXT("menu")) bMenuPressed = bPressed;
}

void USKInputManager::SetHeldFlag(FName ActionName, bool bHeld)
{
	const FString NormalizedName = ActionName.ToString().ToLower();
	if (NormalizedName == TEXT("attack")) bAttackHeld = bHeld;
	else if (NormalizedName == TEXT("guard")) bGuardHeld = bHeld;
	else if (NormalizedName == TEXT("dodge")) bDodgeHeld = bHeld;
	else if (NormalizedName == TEXT("walk")) bWalkHeld = bHeld;
	else if (NormalizedName == TEXT("prosthetic")) bProstheticHeld = bHeld;
}

void USKInputManager::AddBufferedInput(FName Action, int32 Priority, float Lifetime)
{
	FSKBufferedInput Entry;
	Entry.Action = Action;
	Entry.Priority = Priority;
	Entry.Timestamp = GetWorldTimeSecondsForScript();
	Entry.Lifetime = FMath::Max(0.f, Lifetime);
	InputBuffer.Add(Entry);
}

void USKInputManager::PruneInputBufferForScript()
{
	const float Now = GetWorldTimeSecondsForScript();
	InputBuffer.RemoveAll([Now](const FSKBufferedInput& Entry) {
		return (Now - Entry.Timestamp) > Entry.Lifetime;
	});
	while (InputBuffer.Num() > 6)
	{
		InputBuffer.RemoveAt(0);
	}
}

void USKInputManager::ClearPressedFlagsForScript()
{
	bAttackPressed = false;
	bJumpPressed = false;
	bDodgePressed = false;
	bInteractPressed = false;
	bUseItemPressed = false;
	bHealingGourdPressed = false;
	bGrapplePressed = false;
	bProstheticPressed = false;
	bLockOnPressed = false;
	bCycleItemNext = false;
	bCycleItemPrev = false;
	bPausePressed = false;
	bMenuPressed = false;
	bCrouchToggled = false;
}

bool USKInputManager::ToggleLockTargetInViewForScript()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return false;

	USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>();
	return CameraManager && CameraManager->ToggleLockTargetInView();
}

//////////////////////////////////////////////////////////////////////////
// 生命周期

void USKInputManager::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	UE_LOG(LogTemp, Log, TEXT("InputManager[%s]: BeginPlay Owner=%s Class=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OwnerCharacter.Get()),
		*GetNameSafe(OwnerCharacter.IsValid() ? OwnerCharacter->GetClass() : nullptr));
}

void USKInputManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (TryCallLuaInputTick(DeltaTime)) return;

	// ── 长按计时 ──
	if (bAttackHeld)
	{
		AttackHoldTime += DeltaTime;
	}
	if (bProstheticHeld)
	{
		ProstheticHoldTime += DeltaTime;
	}
	if (bDodgeHeld)
	{
		DodgeHoldTime += DeltaTime;
	}
	if (DodgeActiveTimeRemaining > 0.f)
	{
		DodgeActiveTimeRemaining = FMath::Max(0.f, DodgeActiveTimeRemaining - DeltaTime);
		if (DodgeActiveTimeRemaining <= 0.f)
		{
			bDodgeActive = false;
			if (ASKCharacter* SekiroOwner = Cast<ASKCharacter>(OwnerCharacter.Get()))
			{
				SekiroOwner->SetDodging(false);
			}
		}
	}
	if (MoveInputReleaseBufferRemaining > 0.f)
	{
		MoveInputReleaseBufferRemaining = FMath::Max(0.f, MoveInputReleaseBufferRemaining - DeltaTime);
		if (MoveInputReleaseBufferRemaining <= 0.f)
		{
			MoveIntent = FVector2D::ZeroVector;
			MoveInputAmount = 0.f;
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}

	// ── 连段窗口 ──
	TimeSinceLastAttack += DeltaTime;
	if (TimeSinceLastAttack > 0.5f)
	{
		ComboIndex = 0;
	}

	// ── 输入缓冲管理 ──
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// 移除超时条目
	InputBuffer.RemoveAll([Now](const FSKBufferedInput& E) {
		return (Now - E.Timestamp) > E.Lifetime;
	});
	// 限制队列最大长度 6（移除最旧的条目）
	while (InputBuffer.Num() > 6)
	{
		InputBuffer.RemoveAt(0);
	}

	// ── 冲刺状态检查（无输入或离地时退出冲刺）──
	if (OwnerCharacter.IsValid())
	{
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement()))
		{
			if (bDodgeHeld
				&& DodgeHoldTime >= SprintHoldThreshold
				&& MoveIntent.Size() >= 0.1f
				&& !OwnerCharacter->GetCharacterMovement()->IsFalling()
				&& MoveComp->CurrentMovementTier != ESKMovementTier::Sprint)
			{
				MoveComp->CurrentMovementTier = ESKMovementTier::Sprint;
			}
			else if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
			{
				if (MoveIntent.Size() < 0.1f || OwnerCharacter->GetCharacterMovement()->IsFalling())
				{
					ApplyDesiredMovementTier(MoveInputAmount);
				}
			}
		}
	}

	// ── 消费型意图帧末清零（未被 Consume 的残留标记）──
	bAttackPressed = false;
	bJumpPressed = false;
	bDodgePressed = false;
	bInteractPressed = false;
	bUseItemPressed = false;
	bHealingGourdPressed = false;
	bGrapplePressed = false;
	bProstheticPressed = false;
	bLockOnPressed = false;
	bCycleItemNext = false;
	bCycleItemPrev = false;
	bPausePressed = false;
	bMenuPressed = false;
	bCrouchToggled = false;
}

//////////////////////////////////////////////////////////////////////////
// 移动/视角回调

void USKInputManager::OnMove(const FInputActionValue& Value)
{
	const FVector2D LuaInput = Value.Get<FVector2D>();
	if (TryCallLuaInputAxisEvent(TEXT("OnMove"), LuaInput)) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	const FVector2D Input = LuaInput;
	const float RawInputAmount = FMath::Clamp(Input.Size(), 0.f, 1.f);
	const bool bHasRawMoveInput = RawInputAmount > 0.1f;
	if (bHasRawMoveInput)
	{
		MoveInputAmount = RawInputAmount;
		MoveIntent = Input.GetSafeNormal();
		MoveInputReleaseBufferRemaining = MoveInputReleaseBufferDuration;
	}
	else if (MoveInputReleaseBufferRemaining <= 0.f)
	{
		MoveInputAmount = 0.f;
		MoveIntent = FVector2D::ZeroVector;
	}

	const FVector2D Normalized = bHasRawMoveInput ? Input.GetSafeNormal() : FVector2D::ZeroVector;

	// ── 世界空间方向 ──
	const AController* Controller = Owner->GetController();
	if (!Controller) return;

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	if (bHasRawMoveInput)
	{
		Owner->AddMovementInput(Forward, Normalized.Y);
		Owner->AddMovementInput(Right,   Normalized.X);
	}

	// ── 冲刺状态更新 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier == ESKMovementTier::Sprint)
		{
			if (MoveInputAmount < 0.1f || Owner->GetCharacterMovement()->IsFalling())
			{
				ApplyDesiredMovementTier(MoveInputAmount);
			}
		}
		else if (!Owner->GetCharacterMovement()->IsFalling() && (bHasRawMoveInput || MoveInputReleaseBufferRemaining <= 0.f))
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}

	// ── 闪避方向更新 ──
	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner && SekiroOwner->IsDodging() && Input.Size() > 0.1f)
	{
		SekiroOwner->SetDodgeDirection(Normalized.Y, Normalized.X);
	}
}

void USKInputManager::OnLook(const FInputActionValue& Value)
{
	const FVector2D LuaInput = Value.Get<FVector2D>();
	if (TryCallLuaInputAxisEvent(TEXT("OnLook"), LuaInput)) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	FVector2D LookAxis = LuaInput;
	LookIntent = LookAxis;

	USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>();
	if (CameraManager)
	{
		CameraManager->AddLookInput(LookAxis);
		return;
	}

	Owner->AddControllerYawInput(LookAxis.X);
	Owner->AddControllerPitchInput(LookAxis.Y);
}

//////////////////////////////////////////////////////////////////////////
// 跳跃回调

void USKInputManager::OnJumpStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnJumpStarted"))) return;

	bJumpPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Jump");
	Entry.Priority = 5;                          // ESKActionPriority::Jump
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->Jump();
	}
}

void USKInputManager::OnJumpCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnJumpCompleted"))) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (Owner)
	{
		Owner->StopJumping();
	}
}

//////////////////////////////////////////////////////////////////////////
// 闪避/冲刺回调

void USKInputManager::OnDodgeStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnDodgeStarted"))) return;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	bDodgeHeld = true;
	DodgeHoldTime = 0.f;

	// ── 长按后才进入冲刺；短按在松开时生成闪避输入 ──
	if (Owner->bIsCrouched)
	{
		Owner->UnCrouch();
	}
}

void USKInputManager::OnDodgeCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnDodgeCompleted"))) return;

	const bool bWasShortPress = DodgeHoldTime < SprintHoldThreshold;
	bDodgeHeld = false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (bWasShortPress)
	{
		QueueDodgePressed();
	}

	// ── 长按结束退出 Sprint；短按保持 Walk/Run 档位并交给 Dodge 动作处理 ──
	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		MoveComp->CurrentMovementTier = ResolveMovementTierFromInput(MoveInputAmount);
	}
	DodgeHoldTime = 0.f;
}

void USKInputManager::OnWalkModifierStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnWalkModifierStarted"))) return;

	bWalkHeld = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier != ESKMovementTier::Sprint && !Owner->GetCharacterMovement()->IsFalling())
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}
}

void USKInputManager::OnWalkModifierCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnWalkModifierCompleted"))) return;

	bWalkHeld = false;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
	{
		if (MoveComp->CurrentMovementTier == ESKMovementTier::Walk && !Owner->GetCharacterMovement()->IsFalling())
		{
			ApplyDesiredMovementTier(MoveInputAmount);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 蹲下回调

void USKInputManager::OnCrouchStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnCrouchStarted"))) return;

	bCrouchToggled = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (Owner->bIsCrouched)
	{
		Owner->UnCrouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Run;
		}
	}
	else
	{
		Owner->Crouch();
		if (USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement()))
		{
			MoveComp->CurrentMovementTier = ESKMovementTier::Crouch;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// 战斗回调

void USKInputManager::OnAttackStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnAttackStarted"))) return;

	bAttackPressed = true;
	bAttackHeld = true;
	AttackHoldTime = 0.f;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Attack");
	Entry.Priority = 2;                          // ESKActionPriority::Attack
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);

	// 连段计数：0.5s 窗口内连续触发 → ComboIndex++
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (TimeSinceLastAttack <= 0.5f && ComboIndex > 0)
	{
		ComboIndex++;
	}
	else
	{
		ComboIndex = 1;
	}
	TimeSinceLastAttack = 0.f;
}

void USKInputManager::OnAttackCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnAttackCompleted"))) return;

	bAttackHeld = false;
	AttackHoldTime = 0.f;
}

void USKInputManager::OnGuardStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnGuardStarted"))) return;

	bGuardHeld = true;

	// Guard 也入缓冲，但消费方优先使用 IsGuardHeld 持续状态
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Guard");
	Entry.Priority = 5;                          // ESKActionPriority::Guard
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::OnGuardCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnGuardCompleted"))) return;

	bGuardHeld = false;
}

void USKInputManager::OnLockOnStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnLockOnStarted"))) return;

	bLockOnPressed = true;

	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	if (USKCameraManagerComponent* CameraManager = Owner->FindComponentByClass<USKCameraManagerComponent>())
	{
		CameraManager->ToggleLockTargetInView();
	}
}

void USKInputManager::OnProstheticStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnProstheticStarted"))) return;

	bProstheticPressed = true;
	bProstheticHeld = true;
	ProstheticHoldTime = 0.f;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Prosthetic");
	Entry.Priority = 4;                          // ESKActionPriority::Prosthetic
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::OnProstheticCompleted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnProstheticCompleted"))) return;

	bProstheticHeld = false;
	ProstheticHoldTime = 0.f;
}

void USKInputManager::OnGrappleStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnGrappleStarted"))) return;

	bGrapplePressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Grapple");
	Entry.Priority = 2;                          // ESKActionPriority::Attack（与Attack同级）
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

//////////////////////////////////////////////////////////////////////////
// 交互/道具回调

void USKInputManager::OnInteractStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnInteractStarted"))) return;

	bInteractPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Interact");
	Entry.Priority = 10;                         // ESKActionPriority::Deathblow
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::OnUseItemStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnUseItemStarted"))) return;

	bUseItemPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::OnHealingGourdStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnHealingGourdStarted"))) return;

	bHealingGourdPressed = true;

	// 入队到输入缓冲
	FSKBufferedInput Entry;
	Entry.Action = TEXT("Item");
	Entry.Priority = 3;                          // ESKActionPriority::ItemUse
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}

void USKInputManager::OnCycleItemNextStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnCycleItemNextStarted"))) return;

	bCycleItemNext = true;
}

void USKInputManager::OnCycleItemPrevStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnCycleItemPrevStarted"))) return;

	bCycleItemPrev = true;
}

//////////////////////////////////////////////////////////////////////////
// 系统回调

void USKInputManager::OnPauseStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnPauseStarted"))) return;

	bPausePressed = true;
}

void USKInputManager::OnMenuStarted(const FInputActionValue& Value)
{
	if (TryCallLuaInputEvent(TEXT("OnMenuStarted"))) return;

	bMenuPressed = true;
}

bool USKInputManager::TryCallLuaInputEvent(FName FunctionName)
{
	const FString ModuleName = ResolveLuaInputModuleName();
	if (!bUseLuaInputLogic || ModuleName.IsEmpty()) return false;

	IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
	UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
	if (!LuaEnv) return false;

	bool bRequireSucceeded = false;
	UnLua::FLuaRetValues RequireReturnValues = RequireSKInputLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
	if (!bRequireSucceeded) return false;

	UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
	const FString DirectFunctionNameText = FunctionName.ToString();
	const FTCHARToUTF8 DirectFunctionNameUtf8(*DirectFunctionNameText);
	UnLua::FLuaValue FunctionValue = ModuleTable[DirectFunctionNameUtf8.Get()];
	if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

	UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
	UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this);
	const bool bHandled = ReadSKInputLuaHandled(FunctionReturnValues, ModuleName, FunctionName);
	FunctionReturnValues.Pop();
	return bHandled;
}

bool USKInputManager::TryCallLuaInputAxisEvent(FName FunctionName, const FVector2D& AxisValue)
{
	const FString ModuleName = ResolveLuaInputModuleName();
	if (!bUseLuaInputLogic || ModuleName.IsEmpty()) return false;

	IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
	UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
	if (!LuaEnv) return false;

	bool bRequireSucceeded = false;
	UnLua::FLuaRetValues RequireReturnValues = RequireSKInputLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
	if (!bRequireSucceeded) return false;

	UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
	const FString DirectFunctionNameText = FunctionName.ToString();
	const FTCHARToUTF8 DirectFunctionNameUtf8(*DirectFunctionNameText);
	UnLua::FLuaValue FunctionValue = ModuleTable[DirectFunctionNameUtf8.Get()];
	if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

	UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
	UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, AxisValue.X, AxisValue.Y);
	const bool bHandled = ReadSKInputLuaHandled(FunctionReturnValues, ModuleName, FunctionName);
	FunctionReturnValues.Pop();
	return bHandled;
}

bool USKInputManager::TryCallLuaInputTick(float DeltaTime)
{
	const FString ModuleName = ResolveLuaInputModuleName();
	if (!bUseLuaInputLogic || ModuleName.IsEmpty()) return false;

	IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
	UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
	if (!LuaEnv) return false;

	bool bRequireSucceeded = false;
	UnLua::FLuaRetValues RequireReturnValues = RequireSKInputLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
	if (!bRequireSucceeded) return false;

	UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
	UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
	if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

	UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
	UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
	const bool bHandled = ReadSKInputLuaHandled(FunctionReturnValues, ModuleName, FName(TEXT("Tick")));
	FunctionReturnValues.Pop();
	return bHandled;
}

FString USKInputManager::ResolveLuaInputModuleName() const
{
	if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
	{
		const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USKInputManager*>(this));
		if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
	}

	return LuaInputModuleName;
}

ESKMovementTier USKInputManager::ResolveMovementTierFromInput(float InputMagnitude) const
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return ESKMovementTier::Run;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return ESKMovementTier::Run;

	if (Owner->bIsCrouched)
	{
		return ESKMovementTier::Crouch;
	}

	if (bDodgeHeld
		&& DodgeHoldTime >= SprintHoldThreshold
		&& InputMagnitude > 0.1f
		&& !Owner->GetCharacterMovement()->IsFalling())
	{
		return ESKMovementTier::Sprint;
	}

	if (InputMagnitude <= 0.1f)
	{
		return bWalkHeld ? ESKMovementTier::Walk : ESKMovementTier::Run;
	}

	if (bWalkHeld)
	{
		return ESKMovementTier::Walk;
	}

	if (MoveComp->CurrentMovementTier == ESKMovementTier::Walk)
	{
		return InputMagnitude >= AnalogRunEnterThreshold ? ESKMovementTier::Run : ESKMovementTier::Walk;
	}

	return InputMagnitude <= AnalogWalkEnterThreshold ? ESKMovementTier::Walk : ESKMovementTier::Run;
}

ESKMovementTier USKInputManager::ResolveMovementTierByName(FName TierName) const
{
	const FString NormalizedName = TierName.ToString().ToLower();
	if (NormalizedName == TEXT("idle")) return ESKMovementTier::Idle;
	if (NormalizedName == TEXT("walk")) return ESKMovementTier::Walk;
	if (NormalizedName == TEXT("sprint")) return ESKMovementTier::Sprint;
	if (NormalizedName == TEXT("crouch")) return ESKMovementTier::Crouch;
	return ESKMovementTier::Run;
}

void USKInputManager::ApplyDesiredMovementTier(float InputMagnitude)
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	USKMovementComponent* MoveComp = Cast<USKMovementComponent>(Owner->GetCharacterMovement());
	if (!MoveComp) return;

	MoveComp->CurrentMovementTier = ResolveMovementTierFromInput(InputMagnitude);
}

void USKInputManager::QueueDodgePressed()
{
	ACharacter* Owner = OwnerCharacter.Get();
	if (!Owner) return;

	ASKCharacter* SekiroOwner = Cast<ASKCharacter>(Owner);
	if (SekiroOwner)
	{
		const bool bInAir = Owner->GetCharacterMovement()->IsFalling();
		if (bInAir && !SekiroOwner->CanAirDodge())
		{
			return;
		}

		SekiroOwner->SetDodgeDirection(MoveIntent.Y, MoveIntent.X);
		SekiroOwner->SetDodging(true);
	}

	bDodgeActive = true;
	DodgeActiveTimeRemaining = DodgeActiveDuration;
	bDodgePressed = true;

	FSKBufferedInput Entry;
	Entry.Action = TEXT("Dodge");
	Entry.Priority = 7;                          // ESKActionPriority::Dodge
	Entry.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Entry.Lifetime = 0.1f;
	InputBuffer.Add(Entry);
}
