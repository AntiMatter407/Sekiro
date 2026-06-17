// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SKAnimationController.h"
#include "SekiroAnimLogicData.h"
#include "Character/SKCharacter.h"
#include "Input/SKInputHandler.h"
#include "Animation/SKAnimInstance.h"
#include "Weapon/SKWeapon.h"
#include "Weapon/SKWeaponComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

// ============================================================================
// USKAnimationController
// ============================================================================

USKAnimationController::USKAnimationController()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// ── 状态查询 ──────────────────────────────────────────────

FName USKAnimationController::GetCurrentAction() const
{
	return CurrentAction;
}

int32 USKAnimationController::GetCurrentAnimID() const
{
	return CurrentAnimID;
}

int32 USKAnimationController::GetCurrentPriority() const
{
	return CurrentPriority;
}

// ── 生命周期 ──────────────────────────────────────────────

void USKAnimationController::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ASKCharacter>(GetOwner());
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	InputHandler = OwnerCharacter->GetInputHandler();
	Mesh = OwnerCharacter->GetMesh();

	// 自动加载 AnimLogicData（如果在蓝图中未手动赋值）
	if (!AnimLogicData)
	{
		AnimLogicData = LoadObject<USKAnimationLogicData>(nullptr,
			TEXT("/Game/Characters/Sekiro/SK_AnimLogicData.SK_AnimLogicData"));
		if (AnimLogicData)
		{
			UE_LOG(LogTemp, Log,
				TEXT("AnimController[%s]: AnimLogicData auto-loaded from /Game/Characters/Sekiro/SK_AnimLogicData"),
				*GetNameSafe(OwnerCharacter.Get()));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AnimController[%s]: AnimLogicData not set and failed to load from default path"),
				*GetNameSafe(OwnerCharacter.Get()));
		}
	}
	else
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("AnimController[%s]: AnimLogicData already set via Blueprint"),
			*GetNameSafe(OwnerCharacter.Get()));
	}

	UE_LOG(LogTemp, Log,
		TEXT("AnimController[%s]: Initialized (InputHandler=%s, Mesh=%s, AnimLogicData=%s)"),
		*GetNameSafe(OwnerCharacter.Get()),
		InputHandler.IsValid() ? TEXT("OK") : TEXT("NULL"),
		Mesh.IsValid() ? TEXT("OK") : TEXT("NULL"),
		AnimLogicData ? TEXT("OK") : TEXT("NULL"));
}

void USKAnimationController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 1. 更新当前动画帧
	UpdateFrameState();

	// 2. 应用当前帧行为标志（bCanDeflect / bDisableMovement 等）
	ApplyFrameFlags();

	// 2.0 更新攻击碰撞体
	UpdateAttackHitbox();

	// 2.1 更新蓄力状态
	UpdateChargeState(DeltaTime);

	// 3. 按优先级处理输入意图
	ProcessIntents();

	// 4. 移动层处理（Priority 0，始终可被更高优先级动作打断）
	ProcessLocomotion();
}

// ── 帧级更新 ──────────────────────────────────────────────

void USKAnimationController::UpdateFrameState()
{
	if (!Mesh.IsValid())
	{
		return;
	}

	UAnimInstance* AnimInst = Mesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
	if (ActiveMontage)
	{
		CurrentAnimTime = AnimInst->Montage_GetPosition(ActiveMontage);
	}
}

void USKAnimationController::ApplyFrameFlags()
{
	if (!AnimLogicData || CurrentAnimID <= 0)
	{
		return;
	}

	int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);

	FSKFrameFlags Flags;
	if (!AnimLogicData->GetFrameFlags(CurrentAnimID, Frame, Flags))
	{
		return;
	}

	if (!Mesh.IsValid())
	{
		return;
	}

	USKAnimInstance* AnimInst = Cast<USKAnimInstance>(Mesh->GetAnimInstance());
	if (!AnimInst)
	{
		return;
	}

	AnimInst->bCanDeflect = Flags.bEnableParry && !Flags.bDisableParry;
	AnimInst->bDisableTurning = Flags.bDisableTurning;
	AnimInst->bDisableMovement = Flags.bDisableMovement || Flags.bLimitMoveSpeedWalk || Flags.bLimitMoveSpeedDash;
}

// ── 意图处理 ──────────────────────────────────────────────

void USKAnimationController::ProcessIntents()
{
	if (!InputHandler.IsValid())
	{
		return;
	}

	// 如果 AnimLogicData 为空，输入无法映射为动画，打 Log 但继续执行（后续 TryPlayAction 会自行拦截）
	if (!AnimLogicData)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("AnimController[%s]: ProcessIntents() called but AnimLogicData is null — input will be consumed but no animation plays"),
			*GetNameSafe(OwnerCharacter.Get()));
	}

	// 优先级 8: 受击（预留，由战斗系统后续填充）

	// 优先级 7: 闪避
	if (InputHandler->ConsumeDodgePressed())
	{
		TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge);
	}

	// 优先级 6: 弹刀（预留，由防御系统后续填充）

	// 优先级 5: 防御（按住时尝试进入防御姿态）
	if (InputHandler->IsGuardHeld() && CurrentAction != TEXT("Guard"))
	{
		TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard);
	}

	// 优先级 4: 义手
	if (InputHandler->ConsumeProstheticPressed())
	{
		TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic);
	}

	// 优先级 3: 道具
	if (InputHandler->ConsumeUseItemPressed() || InputHandler->ConsumeHealingGourdPressed())
	{
		TryPlayAction(TEXT("Item"), ESKActionPriority::ItemUse);
	}

	// 优先级 2: 攻击（连段+蓄力+移动变体）
	HandleAttack();
}

bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
	if (!AnimLogicData)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AnimController[%s]: TryPlayAction(%s) skipped — AnimLogicData is null"),
			*GetNameSafe(OwnerCharacter.Get()), *Action.ToString());
		return false;
	}

	// 优先级判定：低或同优先级不可打断当前动作
	if (Priority <= CurrentPriority)
	{
		return false;
	}

	float Crossfade = 0.1f;

	// 空闲状态（AnimID == 0）跳过 CancelWindow 判定
	if (CurrentAnimID != 0)
	{
		if (!AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, Action, Crossfade))
		{
			UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: TryPlayAction(%s) CancelWindow拒绝(AnimID=%d, Time=%.2f)"),
				*GetNameSafe(OwnerCharacter.Get()), *Action.ToString(), CurrentAnimID, CurrentAnimTime);
			return false;
		}
	}

	// 解析 AnimID
	int32 TargetAnimID = ResolveAnimID(Action);
	if (TargetAnimID <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: TryPlayAction(%s) ResolveAnimID失败(返回%d)"),
			*GetNameSafe(OwnerCharacter.Get()), *Action.ToString(), TargetAnimID);
		return false;
	}

	// 播放 Montage
	PlayMontageByID(TargetAnimID, Crossfade);

	// 更新运行时状态
	CurrentAction = Action;
	CurrentAnimID = TargetAnimID;
	CurrentPriority = Priority;
	CurrentAnimTime = 0.f;

	return true;
}

// ============================================================================
// 移动层（Locomotion）
// ============================================================================

void USKAnimationController::ProcessLocomotion()
{
	// 仅当无更高优先级动作时处理移动
	if (CurrentPriority > ESKActionPriority::Locomotion)
	{
		return;
	}

	if (!AnimLogicData)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("AnimController[%s]: ProcessLocomotion() skipped — AnimLogicData is null"),
			*GetNameSafe(OwnerCharacter.Get()));
		return;
	}

	USKAnimInstance* AnimInst = GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// 空中/蹲行由独立系统处理（Task 8 跳跃, Task 4.3 蹲行），Locomotion 层不覆盖
	if (AnimInst->bIsInAir || AnimInst->bIsCrouching)
	{
		return;
	}

	const float Speed = AnimInst->Speed;
	const float Angle = AnimInst->Angle;

	// 转身冷却递减
	if (TurnCooldown > 0.f)
	{
		TurnCooldown -= GetWorld()->GetDeltaSeconds();
	}

	// 原地转身判定（静止 + 角度变化超过阈值 + 冷却结束）
	if (Speed < 50.f && !CurrentLocoState.bIsMoving && TurnCooldown <= 0.f)
	{
		float AngleDelta = FMath::UnwindDegrees(Angle - LastAngle);
		if (FMath::Abs(AngleDelta) > 22.5f)
		{
			int32 TurnAnim = GetTurnAnimID(AngleDelta);
			if (TurnAnim > 0)
			{
				PlayLocomotionMontage(TurnAnim, false);
				TurnCooldown = 0.5f;                          // 转身冷却0.5秒
				LastAngle = Angle;
				return;
			}
		}
	}

	LastAngle = Angle;

	// 刚停下 → 播放 Stop 过渡动画
	if (Speed < 50.f && CurrentLocoState.bIsMoving)
	{
		int32 StopAnim = GetStopAnimID(CurrentLocoState);
		if (StopAnim > 0)
		{
			PlayLocomotionMontage(StopAnim, false);
			CurrentLocoState.bIsMoving = false;
			CurrentLocoState.Tier = ESKMovementTier::Idle;
			CurrentLocoState.AnimID = 0;
		}
		return;
	}

	// 计算目标移动状态
	FSKLocomotionState Target = EvaluateLocomotionState(Speed, Angle);

	// 同一状态，无需切换
	if (Target.AnimID == CurrentLocoState.AnimID && Target.Tier == CurrentLocoState.Tier)
	{
		return;
	}

	// Tier 变化 → 播放过渡动画
	if (Target.Tier != CurrentLocoState.Tier)
	{
		int32 TransAnim = GetTransitionAnimID(CurrentLocoState, Target);
		if (TransAnim > 0)
		{
			// 过渡播完后由 OnLocoTransitionEnded 衔接目标循环
			PlayLocomotionMontage(TransAnim, false);
			return;
		}
	}

	// 同 Tier 方向切换 / 无过渡动画 → 直接切循环动画（Idle 则停止 Montage 回到默认姿态）
	if (Target.AnimID <= 0)
	{
		if (UAnimInstance* AI = Mesh.IsValid() ? Mesh->GetAnimInstance() : nullptr)
		{
			AI->Montage_Stop(0.15f);
		}
	}
	else
	{
		PlayLocomotionMontage(Target.AnimID, Target.bIsMoving);
	}
	CurrentLocoState = Target;
}

FSKLocomotionState USKAnimationController::EvaluateLocomotionState(float Speed, float Angle) const
{
	FSKLocomotionState State;

	// ── 速度→层级 ──────────────────────────────────────────
	if (Speed < 50.f)
	{
		State.Tier = ESKMovementTier::Idle;
		State.bIsMoving = false;
	}
	else if (Speed < 200.f)
	{
		State.Tier = ESKMovementTier::Walk;
		State.bIsMoving = true;
	}
	else if (Speed < 400.f)
	{
		State.Tier = ESKMovementTier::Jog;
		State.bIsMoving = true;
	}
	else if (Speed < 525.f)
	{
		State.Tier = ESKMovementTier::Run;
		State.bIsMoving = true;
	}
	else
	{
		State.Tier = ESKMovementTier::Sprint;
		State.bIsMoving = true;
	}

	// ── 角度→方向 ──────────────────────────────────────────
	if (Angle >= -22.5f && Angle <= 22.5f)
	{
		State.Direction = ESKLocomotionDirection::Fwd;
	}
	else if (Angle > 22.5f && Angle <= 67.5f)
	{
		State.Direction = ESKLocomotionDirection::Fwd_L;
	}
	else if (Angle > 67.5f && Angle <= 112.5f)
	{
		State.Direction = ESKLocomotionDirection::L;
	}
	else if (Angle > 112.5f && Angle <= 157.5f)
	{
		State.Direction = ESKLocomotionDirection::Bwd_L;
	}
	else if (Angle > 157.5f || Angle < -157.5f)
	{
		State.Direction = ESKLocomotionDirection::Bwd;
	}
	else if (Angle >= -157.5f && Angle < -112.5f)
	{
		State.Direction = ESKLocomotionDirection::Bwd_R;
	}
	else if (Angle >= -112.5f && Angle < -67.5f)
	{
		State.Direction = ESKLocomotionDirection::R;
	}
	else
	{
		State.Direction = ESKLocomotionDirection::Fwd_R;        // -67.5 ~ -22.5
	}

	State.AnimID = ResolveLocomotionAnimID(State);
	return State;
}

int32 USKAnimationController::ResolveLocomotionAnimID(const FSKLocomotionState& State) const
{
	// Idle → 始终为 0
	if (State.Tier == ESKMovementTier::Idle)
	{
		return 0;
	}

	// Walk 层级：全方向支持
	if (State.Tier == ESKMovementTier::Walk)
	{
		switch (State.Direction)
		{
		case ESKLocomotionDirection::Fwd:   return 100;          // Walk_Fwd
		case ESKLocomotionDirection::Fwd_L: return 101;          // Walk_Fwd_L
		case ESKLocomotionDirection::L:     return 120;          // Walk_L
		case ESKLocomotionDirection::Bwd_L: return 111;          // Walk_Bwd_L
		case ESKLocomotionDirection::Bwd:   return 110;          // Walk_Bwd
		case ESKLocomotionDirection::Bwd_R: return 112;          // Walk_Bwd_R
		case ESKLocomotionDirection::R:     return 122;          // Walk_R
		case ESKLocomotionDirection::Fwd_R: return 102;          // Walk_Fwd_R
		}
	}

	// Jog 层级：仅前向 / 前左 / 前右
	if (State.Tier == ESKMovementTier::Jog)
	{
		switch (State.Direction)
		{
		case ESKLocomotionDirection::Fwd:   return 200;          // Jog_Fwd
		case ESKLocomotionDirection::Fwd_L: return 201;          // Jog_Fwd_L
		case ESKLocomotionDirection::Fwd_R: return 202;          // Jog_Fwd_R
		default:                            return 0;           // 无横向/后退慢跑
		}
	}

	// Run 层级：仅前向 / 左 / 前右 / 右（原版无后退奔跑）
	if (State.Tier == ESKMovementTier::Run)
	{
		switch (State.Direction)
		{
		case ESKLocomotionDirection::Fwd:   return 400;          // Run_Fast_Fwd
		case ESKLocomotionDirection::Fwd_L: return 401;          // Run_Fast_Fwd_L
		case ESKLocomotionDirection::L:     return 420;          // Run_Fast_L
		case ESKLocomotionDirection::Fwd_R: return 402;          // Run_Fast_Fwd_R
		case ESKLocomotionDirection::R:     return 422;          // Run_Fast_R
		default:                            return 0;           // 无后退奔跑
		}
	}

	// Sprint 层级：仅前向 / 前左 / 前右
	if (State.Tier == ESKMovementTier::Sprint)
	{
		switch (State.Direction)
		{
		case ESKLocomotionDirection::Fwd:   return 300;          // Sprint_Fwd
		case ESKLocomotionDirection::Fwd_L: return 301;          // Sprint_Fwd_L
		case ESKLocomotionDirection::Fwd_R: return 302;          // Sprint_Fwd_R
		default:                            return 0;           // 无横向/后退冲刺
		}
	}

	return 0;
}

int32 USKAnimationController::GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To) const
{
	// 过渡动画表：{FromTier, ToTier, AnimID}
	// 仅列出有明确过渡动画的组合，其余返回 0 则直接切循环
	struct FLocoTransition
	{
		ESKMovementTier FromTier;
		ESKMovementTier ToTier;
		int32 AnimID;
	};

	static const TArray<FLocoTransition> TransitionTable =
	{
		{ ESKMovementTier::Walk,   ESKMovementTier::Idle, 20000  },   // Walk_To_Idle
		{ ESKMovementTier::Walk,   ESKMovementTier::Jog,  20010  },   // Walk_To_Jog
		{ ESKMovementTier::Run,    ESKMovementTier::Idle, 23000  },   // Run_To_Idle
		{ ESKMovementTier::Run,    ESKMovementTier::Walk, 23200  },   // Run_To_Walk
		{ ESKMovementTier::Run,    ESKMovementTier::Jog,  23300  },   // Run_To_Jog
		{ ESKMovementTier::Sprint, ESKMovementTier::Run,  24200  },   // Sprint_To_Run
		{ ESKMovementTier::Sprint, ESKMovementTier::Jog,  24300  },   // Sprint_To_Jog
		{ ESKMovementTier::Sprint, ESKMovementTier::Idle, 500    },   // Sprint_To_Idle (Slides)
	};

	for (const FLocoTransition& T : TransitionTable)
	{
		if (T.FromTier == From.Tier && T.ToTier == To.Tier)
		{
			return T.AnimID;
		}
	}

	return 0;
}

int32 USKAnimationController::GetStopAnimID(const FSKLocomotionState& State) const
{
	// 停止动画：对应层级上行走→静止的过渡
	switch (State.Tier)
	{
	case ESKMovementTier::Walk:   return 103;                      // Walk_Fwd_Stop
	case ESKMovementTier::Jog:    return 203;                      // Jog_Fwd_Stop
	case ESKMovementTier::Sprint: return 303;                      // Sprint_Fwd_Stop
	case ESKMovementTier::Run:    return 403;                      // Run_Fast_Fwd_Stop
	default:                      return 0;                        // Idle/Crouch 无需停止动画
	}
}

int32 USKAnimationController::GetTurnAnimID(float AngleDelta) const
{
	// 转身动画表：角度阈值 → 对应 AnimID pair
	struct FTurnEntry
	{
		float Threshold;                                          // 角度阈值（度）
		int32 LeftAnimID;                                         // 左转动画ID
		int32 RightAnimID;                                        // 右转动画ID
	};

	static const TArray<FTurnEntry> TurnTable =
	{
		{ 45.f,  5000,  5100  },                                    // Turn_L45 / Turn_R45
		{ 90.f,  5010,  5110  },                                    // Turn_L90 / Turn_R90
		{ 135.f, 5200,  5300  },                                    // Turn_L135 / Turn_R135
		{ 180.f, 5400,  5500  },                                    // Turn_L180 / Turn_R180
	};

	const float AbsDelta = FMath::Abs(AngleDelta);
	const bool bLeftTurn = AngleDelta > 0.f;

	// 查找最接近的角度阈值
	int32 BestIndex = -1;
	float BestDiff = FLT_MAX;
	for (int32 i = 0; i < TurnTable.Num(); ++i)
	{
		float Diff = FMath::Abs(TurnTable[i].Threshold - AbsDelta);
		if (Diff < BestDiff)
		{
			BestDiff = Diff;
			BestIndex = i;
		}
	}

	if (BestIndex < 0)
	{
		return 0;
	}

	return bLeftTurn ? TurnTable[BestIndex].LeftAnimID
	                 : TurnTable[BestIndex].RightAnimID;
}

void USKAnimationController::PlayLocomotionMontage(int32 AnimID, bool bLooping)
{
	if (AnimID <= 0)
	{
		return;
	}

	EnsureMontageLoaded(AnimID);

	TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
	if (!Found || !*Found)
	{
		return;
	}

	if (!Mesh.IsValid())
	{
		return;
	}

	UAnimInstance* AnimInst = Mesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimSequence* Seq = *Found;

	if (bLooping)
	{
		// 循环动画：使用 PlaySlotAnimationAsDynamicMontage 播放，循环模式
		UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, 0.15f, 0.0f);
		if (DynMontage)
		{
			// 循环动画：播完后重新播放自身实现自循环
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &USKAnimationController::OnLocoTransitionEnded);
			AnimInst->Montage_SetEndDelegate(EndDelegate, DynMontage);
		}
	}
	else
	{
		// 过渡动画：播放后绑定 EndDelegate，播完衔接目标循环
		UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, 0.1f, 0.0f);
		if (DynMontage)
		{
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &USKAnimationController::OnLocoTransitionEnded);
			AnimInst->Montage_SetEndDelegate(EndDelegate, DynMontage);
		}
	}
}

void USKAnimationController::OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (bInterrupted)
	{
		return;
	}

	USKAnimInstance* AnimInst = GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// 重新评估当前移动状态，播放对应循环动画（Idle 则停止 Montage 回到默认姿态）
	FSKLocomotionState Target = EvaluateLocomotionState(AnimInst->Speed, AnimInst->Angle);
	if (Target.AnimID <= 0)
	{
		AnimInst->Montage_Stop(0.15f);
	}
	else
	{
		PlayLocomotionMontage(Target.AnimID, Target.bIsMoving);
	}
	CurrentLocoState = Target;
	CurrentAction = NAME_None;
	CurrentPriority = ESKActionPriority::Locomotion;
}

// ── 攻击处理（Priority 2）─────────────────────────────────

void USKAnimationController::HandleAttack()
{
	if (!InputHandler.IsValid())
	{
		return;
	}

	// 检查蓄力释放（按键松开且已蓄力）
	if (CheckChargeRelease())
	{
		return; // 蓄力攻击已触发，不需要再处理普通攻击
	}

	// 普通攻击按下
	if (InputHandler->ConsumeAttackPressed())
	{
		FName AttackAction;

		// 根据移动状态选择攻击变体
		USKAnimInstance* AnimInst = GetAnimInstance();

		// 空中攻击
		if (AnimInst && AnimInst->bIsInAir)
		{
			FName Suffix = GetMoveDirectionSuffix();
			if (Suffix == TEXT("Fwd"))
			{
				AttackAction = TEXT("Attack_Jump_Fwd");
			}
			else
			{
				AttackAction = TEXT("Attack_Jump");
			}
		}
		// 蹲行攻击
		else if (AnimInst && AnimInst->bIsCrouching)
		{
			AttackAction = TEXT("Attack_Crouch");
		}
		// 冲刺攻击
		else if (AnimInst && AnimInst->Speed >= 525.f)
		{
			AttackAction = TEXT("Attack_Sprint_R1");
		}
		// Dodge/Quickstep 后攻击
		else if (CurrentAction == TEXT("Dodge") || CurrentAction == TEXT("Quickstep"))
		{
			FName DirSuffix = GetMoveDirectionSuffix();
			if (CurrentAction == TEXT("Dodge"))
			{
				AttackAction = FName(*FString::Printf(TEXT("Attack_Dodge_%s"), *DirSuffix.ToString()));
			}
			else
			{
				AttackAction = FName(*FString::Printf(TEXT("Attack_Quickstep_%s"), *DirSuffix.ToString()));
			}
		}
		// 默认：R1 连段
		else
		{
			AttackAction = GetComboAnimID(AttackState.ComboIndex) > 0
				? FName(*FString::Printf(TEXT("Attack_R1_Combo%02d"), AttackState.ComboIndex))
				: TEXT("Attack");
		}

		if (TryPlayAction(AttackAction, ESKActionPriority::Attack))
		{
			// 攻击成功触发 → 记录连段
			if (AttackState.ComboIndex == 0 || AttackState.ComboTimeout > 0.5f)
			{
				AttackState.ComboIndex = 1;
			}
			else
			{
				AttackState.ComboIndex = (AttackState.ComboIndex % 4) + 1;
			}
			AttackState.ComboTimeout = 0.f;
			AttackState.LastAttackAction = AttackAction;
		}
	}

	// 连段超时计时
	if (AttackState.ComboIndex > 0)
	{
		AttackState.ComboTimeout += GetWorld()->GetDeltaSeconds();
		if (AttackState.ComboTimeout > 0.5f)
		{
			ResetAttackState();
		}
	}
}

int32 USKAnimationController::GetComboAnimID(int32 ComboIdx) const
{
	if (!AnimLogicData || ComboIdx < 1 || ComboIdx > 4)
	{
		return 0;
	}

	FString Category = FString::Printf(TEXT("Attack_R1_Combo%02d"), ComboIdx);
	const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Category);
	if (List && List->IDs.Num() > 0)
	{
		return List->IDs[0];
	}

	return 0;
}

FName USKAnimationController::GetMoveDirectionSuffix() const
{
	if (!InputHandler.IsValid())
	{
		return TEXT("Fwd");
	}

	FVector2D MoveIntent = InputHandler->GetMoveIntent();
	float Y = MoveIntent.Y;  // 前后（正=前）
	float X = MoveIntent.X;  // 左右（正=右）

	if (FMath::Abs(X) < 0.3f && FMath::Abs(Y) < 0.3f)
	{
		return TEXT("Fwd");
	}

	// 优先明显的前后方向
	if (FMath::Abs(Y) > FMath::Abs(X))
	{
		return Y > 0.f ? TEXT("Fwd") : TEXT("Bwd");
	}
	else
	{
		return X > 0.f ? TEXT("R") : TEXT("L");
	}
}

void USKAnimationController::ResetAttackState()
{
	AttackState.ComboIndex = 0;
	AttackState.ComboTimeout = 0.f;
	AttackState.bIsCharging = false;
	AttackState.ChargeTime = 0.f;
	AttackState.LastAttackAction = NAME_None;
	AttackState.bAttackHeldPrev = false;
}

void USKAnimationController::UpdateChargeState(float DeltaTime)
{
	if (!InputHandler.IsValid())
	{
		return;
	}

	bool bCurrentlyHeld = InputHandler->IsAttackHeld();

	// 检测下降沿：之前按住 → 现在松开
	if (AttackState.bAttackHeldPrev && !bCurrentlyHeld)
	{
		// 下降沿由 CheckChargeRelease 处理
	}

	// 按住且当前正在攻击动作中 → 累计蓄力
	if (bCurrentlyHeld && CurrentAction.ToString().StartsWith(TEXT("Attack")))
	{
		AttackState.ChargeTime += DeltaTime;
		if (AttackState.ChargeTime > 0.3f && !AttackState.bIsCharging)
		{
			AttackState.bIsCharging = true;
			UE_LOG(LogTemp, Verbose, TEXT("AnimController[%s]: 开始蓄力"),
				*GetNameSafe(OwnerCharacter.Get()));
		}
	}

	AttackState.bAttackHeldPrev = bCurrentlyHeld;
}

bool USKAnimationController::CheckChargeRelease()
{
	if (!InputHandler.IsValid())
	{
		return false;
	}

	bool bCurrentlyHeld = InputHandler->IsAttackHeld();

	// 检测下降沿：之前按住 → 现在松开，且已进入蓄力状态
	if (AttackState.bAttackHeldPrev && !bCurrentlyHeld && AttackState.bIsCharging)
	{
		AttackState.bIsCharging = false;
		AttackState.ChargeTime = 0.f;

		// 选择蓄力攻击变体
		FName ChargeAction;
		USKAnimInstance* AnimInst = GetAnimInstance();

		if (AnimInst && AnimInst->Speed >= 525.f)
		{
			ChargeAction = TEXT("Attack_Charged_Dash");
		}
		else if (CurrentAction == TEXT("Dodge") || CurrentAction == TEXT("Quickstep"))
		{
			ChargeAction = TEXT("Attack_Charged_Step");
		}
		else
		{
			FName DirSuffix = GetMoveDirectionSuffix();
			if (DirSuffix == TEXT("L"))
			{
				ChargeAction = TEXT("Attack_Charged_L");
			}
			else
			{
				ChargeAction = TEXT("Attack_Charged");
			}
		}

		if (TryPlayAction(ChargeAction, ESKActionPriority::Attack))
		{
			UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: 蓄力攻击触发 %s"),
				*GetNameSafe(OwnerCharacter.Get()), *ChargeAction.ToString());
			ResetAttackState();
			return true;
		}
	}

	return false;
}

// ── 动画播放 ──────────────────────────────────────────────

int32 USKAnimationController::ResolveAnimID(FName Action)
{
	if (!AnimLogicData)
	{
		return -1;
	}

	const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
	if (List && List->IDs.Num() > 0)
	{
		return List->IDs[0];
	}

	return -1;
}

void USKAnimationController::PlayMontageByID(int32 AnimID, float Crossfade)
{
	EnsureMontageLoaded(AnimID);

	TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
	if (!Found || !*Found)
	{
		return;
	}

	if (!Mesh.IsValid())
	{
		return;
	}

	UAnimInstance* AnimInst = Mesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	UAnimSequence* Seq = *Found;
	// 使用 PlaySlotAnimationAsDynamicMontage 将 AnimSequence 包装为临时 Montage 播放
	UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, Crossfade, 0.0f);
	if (!DynMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: PlaySlotAnimationAsDynamicMontage failed for %s"),
			*GetNameSafe(OwnerCharacter.Get()), *Seq->GetName());
	}
}

void USKAnimationController::EnsureMontageLoaded(int32 AnimID)
{
	if (!AnimLogicData)
	{
		return;
	}

	if (MontageCache.Contains(AnimID))
	{
		return;
	}

	const FString* AnimName = AnimLogicData->AnimNameMap.Find(AnimID);
	if (!AnimName)
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: AnimNameMap中未找到ID %d"),
			*GetOwner()->GetName(), AnimID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: 加载AnimID=%d, AnimName=%s"),
		*GetOwner()->GetName(), AnimID, **AnimName);

	// AnimNameMap 存储的是 Sekiro_Walk_Fwd 格式，需要加 Anim_ 前缀
	// 也可能已经是完整名称（如 Anim_Sekiro_Walk_Fwd）
	FString AssetPath;
	if (AnimName->StartsWith(TEXT("Anim_")))
	{
		AssetPath = FString::Printf(TEXT("/Game/Characters/Sekiro/Animations/%s.%s"), **AnimName, **AnimName);
	}
	else
	{
		FString FullName = FString::Printf(TEXT("Anim_%s"), **AnimName);
		AssetPath = FString::Printf(TEXT("/Game/Characters/Sekiro/Animations/%s.%s"), *FullName, *FullName);
	}

	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
	if (Seq)
	{
		MontageCache.Add(AnimID, Seq);
	}
}

// ── 攻击碰撞体更新 ──────────────────────────────────────────

void USKAnimationController::UpdateAttackHitbox()
{
    if (!AnimLogicData || CurrentAnimID <= 0)
    {
        ASKWeapon* Weapon = GetWeapon();
        if (Weapon) Weapon->DeactivateHitbox();
        return;
    }

    int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);

    ASKWeapon* Weapon = GetWeapon();
    if (!Weapon) return;

    bool bHasActiveHitbox = false;
    const FSKAttackHitboxList* List = AnimLogicData->AttackHitboxConfigs.Find(CurrentAnimID);
    if (List)
    {
        for (const FSKAttackHitboxConfig& Cfg : List->Hitboxes)
        {
            if (Frame >= Cfg.StartFrame && Frame <= Cfg.EndFrame)
            {
                bHasActiveHitbox = true;
                break;
            }
        }
    }

    if (bHasActiveHitbox)
    {
        Weapon->ActivateHitbox();
    }
    else
    {
        Weapon->DeactivateHitbox();
        Weapon->ClearHitActors();
    }
}

ASKWeapon* USKAnimationController::GetWeapon() const
{
    if (!OwnerCharacter.IsValid()) return nullptr;
    USKWeaponComponent* WComp = OwnerCharacter->GetWeaponComponent();
    return WComp ? WComp->CurrentWeapon : nullptr;
}

// ── 工具 ──────────────────────────────────────────────────

USKAnimInstance* USKAnimationController::GetAnimInstance() const
{
	if (!Mesh.IsValid())
	{
		return nullptr;
	}

	return Cast<USKAnimInstance>(Mesh->GetAnimInstance());
}
