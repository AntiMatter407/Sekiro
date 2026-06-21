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

USKAnimationController::USKAnimationController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true;
}

FName USKAnimationController::GetCurrentAction() const { return CurrentAction; }
int32 USKAnimationController::GetCurrentAnimID() const { return CurrentAnimID; }
int32 USKAnimationController::GetCurrentPriority() const { return CurrentPriority; }

// ── 生命周期 ──────────────────────────────────────────────

void USKAnimationController::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<ASKCharacter>(GetOwner());
    if (!OwnerCharacter.IsValid()) return;

    InputHandler = OwnerCharacter->GetInputHandler();
    Mesh = OwnerCharacter->GetMesh();

    if (!AnimLogicData)
    {
        AnimLogicData = LoadObject<USKAnimationLogicData>(nullptr,
            TEXT("/Game/Characters/Sekiro/DA_Sekiro_AnimLogic.DA_Sekiro_AnimLogic"));
        if (AnimLogicData)
        {
            UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: AnimLogicData auto-loaded"),
                *GetNameSafe(OwnerCharacter.Get()));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: AnimLogicData failed to load"),
                *GetNameSafe(OwnerCharacter.Get()));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Initialized"),
        *GetNameSafe(OwnerCharacter.Get()));
}

void USKAnimationController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateFrameState();
    ApplyFrameFlags();
    UpdateAttackHitbox();
    UpdateChargeState(DeltaTime);
    ProcessIntents();
    ProcessLocomotion();
}

// ── 帧级更新 ──────────────────────────────────────────────

void USKAnimationController::UpdateFrameState()
{
    if (!Mesh.IsValid()) return;
    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;
    UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
    if (ActiveMontage)
    {
        CurrentAnimTime = AnimInst->Montage_GetPosition(ActiveMontage);
    }
}

void USKAnimationController::ApplyFrameFlags()
{
    if (!AnimLogicData || CurrentAnimID <= 0) return;
    if (!Mesh.IsValid()) return;

    int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);
    FSKFrameFlags Flags;
    if (!AnimLogicData->GetFrameFlags(CurrentAnimID, Frame, Flags)) return;

    USKAnimInstance* AnimInst = Cast<USKAnimInstance>(Mesh->GetAnimInstance());
    if (!AnimInst) return;

    AnimInst->bCanDeflect = Flags.bEnableParry && !Flags.bDisableParry;
    AnimInst->bDisableTurning = Flags.bDisableTurning;
    AnimInst->bDisableMovement = Flags.bDisableMovement || Flags.bLimitMoveSpeedWalk || Flags.bLimitMoveSpeedDash;
}

// ── 意图处理 ──────────────────────────────────────────────

void USKAnimationController::ProcessIntents()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;

    // Deathblow: triggered externally
    if (InputHandler->ConsumeDodgePressed())            { HandleDodge(); return; }
    if (InputHandler->ConsumeJumpPressed())             { HandleJump(); return; }
    if (InputHandler->IsGuardHeld())               { HandleGuard(); return; }
    if (InputHandler->ConsumeProstheticPressed())       { HandleProsthetic(); return; }
    if (InputHandler->ConsumeUseItemPressed())          { HandleItemUse(); return; }
    if (InputHandler->ConsumeGrapplePressed())          { HandleGrapple(); return; }
    // CombatArt: triggered externally
    if (InputHandler->ConsumeAttackPressed())           { HandleAttack(); return; }
}

bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
    if (!AnimLogicData) return false;
    if (CurrentPriority > Priority && CurrentAnimID > 0) return false;

    if (CurrentAnimID > 0)
    {
        float Crossfade;
        if (!AnimLogicData->CanCancelTo(CurrentAnimID, CurrentAnimTime, Action, Crossfade)) return false;
    }

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0) return false;

    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
    return true;
}

// ── 攻击 ──────────────────────────────────────────────────

void USKAnimationController::HandleAttack()
{
    if (!InputHandler.IsValid()) return;

    int32 ComboIdx = AttackState.ComboIndex;
    int32 AnimID = GetComboAnimID(ComboIdx);
    if (AnimID <= 0) { ComboIdx = 0; AnimID = GetComboAnimID(0); }
    if (AnimID <= 0) return;

    FName DirSuffix = GetMoveDirectionSuffix();
    if (DirSuffix != NAME_None)
    {
        FName DirAction = FName(*(TEXT("Attack_") + DirSuffix.ToString()));
        if (TryPlayAction(DirAction, ESKActionPriority::Attack))
        {
            AttackState.ComboIndex = FMath::Min(ComboIdx + 1, 4);
            AttackState.ComboTimeout = 1.0f;
            return;
        }
    }

    if (TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack))
    {
        AttackState.ComboIndex = FMath::Min(ComboIdx + 1, 4);
        AttackState.ComboTimeout = 1.0f;
    }
}

int32 USKAnimationController::GetComboAnimID(int32 ComboIdx) const
{
    if (!AnimLogicData) return -1;
    FString Category = FString::Printf(TEXT("Attack_R1_Combo%02d"), ComboIdx);
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Category);
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

FName USKAnimationController::GetMoveDirectionSuffix() const
{
    if (!InputHandler.IsValid()) return NAME_None;
    FVector2D MoveDir = InputHandler->GetMoveIntent();
    if (MoveDir.IsNearlyZero()) return NAME_None;

    float Angle = FMath::RadiansToDegrees(FMath::Atan2(MoveDir.Y, MoveDir.X));
    if (Angle > -22.5f && Angle <= 22.5f)       return TEXT("Fwd");
    if (Angle > 22.5f && Angle <= 67.5f)        return TEXT("Fwd_R");
    if (Angle > 67.5f && Angle <= 112.5f)       return TEXT("R");
    if (Angle > 112.5f && Angle <= 157.5f)      return TEXT("Bwd_R");
    if (Angle > 157.5f || Angle <= -157.5f)     return TEXT("Bwd");
    if (Angle > -157.5f && Angle <= -112.5f)    return TEXT("Bwd_L");
    if (Angle > -112.5f && Angle <= -67.5f)     return TEXT("L");
    return TEXT("Fwd_L");
}

void USKAnimationController::ResetAttackState()
{
    AttackState.ComboIndex = 0;
    AttackState.ComboTimeout = 0.f;
    AttackState.bIsCharging = false;
    AttackState.ChargeTime = 0.f;
}

void USKAnimationController::UpdateChargeState(float DeltaTime)
{
    if (!InputHandler.IsValid()) return;
    bool bCurrentlyHeld = InputHandler->IsAttackHeld();

    if (bCurrentlyHeld && !AttackState.bAttackHeldPrev && CurrentAction == TEXT("Attack"))
    {
        AttackState.bIsCharging = true;
        AttackState.ChargeTime = 0.f;
    }
    if (AttackState.bIsCharging) AttackState.ChargeTime += DeltaTime;

    if (CheckChargeRelease()) return;
    AttackState.bAttackHeldPrev = bCurrentlyHeld;
}

bool USKAnimationController::CheckChargeRelease()
{
    if (!InputHandler.IsValid()) return false;
    bool bCurrentlyHeld = InputHandler->IsAttackHeld();

    if (AttackState.bAttackHeldPrev && !bCurrentlyHeld && AttackState.bIsCharging)
    {
        AttackState.bIsCharging = false;
        AttackState.ChargeTime = 0.f;

        FName ChargeAction;
        USKAnimInstance* AnimInst = GetAnimInstance();
        if (AnimInst && AnimInst->Speed >= 525.f)
            ChargeAction = TEXT("Attack_Charged_Dash");
        else if (CurrentAction == TEXT("Dodge") || CurrentAction == TEXT("Quickstep"))
            ChargeAction = TEXT("Attack_Charged_Step");
        else
        {
            FName DirSuffix = GetMoveDirectionSuffix();
            ChargeAction = (DirSuffix == TEXT("L")) ? TEXT("Attack_Charged_L") : TEXT("Attack_Charged");
        }

        if (TryPlayAction(ChargeAction, ESKActionPriority::Attack))
        {
            ResetAttackState();
            return true;
        }
    }
    return false;
}

// ── 防御 ──────────────────────────────────────────────────

void USKAnimationController::HandleGuard()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Guard"));
    if (!List || List->IDs.Num() == 0) return;

    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Guard");
    CurrentPriority = ESKActionPriority::Guard;
    CurrentAnimID = AnimID;
    GuardState.Phase = ESKGuardPhase::Idle;
}

void USKAnimationController::OnGuardHit(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    GuardState.Phase = ESKGuardPhase::HitReaction;
}

void USKAnimationController::OnGuardBreak()
{
    GuardState.Phase = ESKGuardPhase::Broken;
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("GuardBreak"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.1f);
    }
}

// ── 回避 ──────────────────────────────────────────────────

void USKAnimationController::HandleDodge()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    FName DirSuffix = GetMoveDirectionSuffix();
    FName DodgeAction = (DirSuffix != NAME_None) ? FName(*(TEXT("Dodge_") + DirSuffix.ToString())) : TEXT("Dodge");
    if (!TryPlayAction(DodgeAction, ESKActionPriority::Dodge))
        TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge);
}

// ── 跳跃 ──────────────────────────────────────────────────
void USKAnimationController::HandleJump()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    if (bWasInAir) return;
    FName DirSuffix = GetMoveDirectionSuffix();
    FName JumpAction = (DirSuffix != NAME_None) ? FName(*(TEXT("Jump_") + DirSuffix.ToString())) : TEXT("Jump");
    if (!TryPlayAction(JumpAction, ESKActionPriority::Jump))
        TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump);
    bWasInAir = true;
}

// ── 忍义�?────────────────────────────────────────────────

void USKAnimationController::HandleProsthetic()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Prosthetic"));
    if (!List || List->IDs.Num() == 0) return;
    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Prosthetic");
    CurrentAnimID = AnimID;
}

// ── 道具 ──────────────────────────────────────────────────

void USKAnimationController::HandleItemUse()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Item"));
    if (!List || List->IDs.Num() == 0) return;
    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Item");
    CurrentAnimID = AnimID;
}

// ── 钩绳 ──────────────────────────────────────────────────

void USKAnimationController::HandleGrapple()
{
    if (!InputHandler.IsValid() || !AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Grapple"));
    if (!List || List->IDs.Num() == 0) return;
    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Grapple");
    CurrentAnimID = AnimID;
}

// ── 战技 ──────────────────────────────────────────────────

void USKAnimationController::HandleCombatArt()
{
    if (!AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("CombatArt"));
    if (!List || List->IDs.Num() == 0) return;
    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("CombatArt");
    CurrentAnimID = AnimID;
}

// ── 忍杀 ──────────────────────────────────────────────────

void USKAnimationController::HandleDeathblow()
{
    if (!AnimLogicData) return;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Deathblow"));
    if (!List || List->IDs.Num() == 0) return;
    int32 AnimID = List->IDs[0];
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.15f);
    CurrentAction = TEXT("Deathblow");
    CurrentAnimID = AnimID;
}

// ── 受击 / 死亡 ───────────────────────────────────────────

void USKAnimationController::OnHitReceived(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.05f);
    CurrentAction = TEXT("Hit");
}

void USKAnimationController::OnDeath(int32 AnimID)
{
    EnsureMontageLoaded(AnimID);
    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = TEXT("Death");
}

void USKAnimationController::OnResurrection()
{
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(TEXT("Resurrection"));
        if (List && List->IDs.Num() > 0) PlayMontageByID(List->IDs[0], 0.2f);
    }
}

// ── 移动�?────────────────────────────────────────────────

void USKAnimationController::ProcessLocomotion()
{
    if (!AnimLogicData || !Mesh.IsValid()) return;
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) return;

    float Speed = AnimInst->Speed;
    float Angle = AnimInst->Angle;
    FSKLocomotionState NewState = EvaluateLocomotionState(Speed, Angle);

    if (CurrentPriority > ESKActionPriority::Locomotion)
    {
        CurrentLocoState = NewState;
        return;
    }

    if (NewState.Tier == ESKMovementTier::Idle && CurrentLocoState.Tier != ESKMovementTier::Idle)
    {
        int32 StopID = GetStopAnimID(CurrentLocoState);
        if (StopID > 0) PlayLocomotionMontage(StopID, false);
    }
    else if (NewState.Tier != CurrentLocoState.Tier || NewState.Direction != CurrentLocoState.Direction)
    {
        if (CurrentLocoState.Tier != ESKMovementTier::Idle && NewState.Tier != ESKMovementTier::Idle)
        {
            int32 TransID = GetTransitionAnimID(CurrentLocoState, NewState);
            if (TransID > 0) { PlayLocomotionMontage(TransID, false); CurrentLocoState = NewState; return; }
        }
        int32 LocoID = ResolveLocomotionAnimID(NewState);
        if (LocoID > 0) PlayLocomotionMontage(LocoID, NewState.Tier != ESKMovementTier::Idle);
    }
    CurrentLocoState = NewState;
}

FSKLocomotionState USKAnimationController::EvaluateLocomotionState(float Speed, float Angle) const
{
    if (Speed < 10.f)           { State.Tier = ESKMovementTier::Idle; State.bIsMoving = false; }
    else if (Speed < 200.f)     { State.Tier = ESKMovementTier::Walk; State.bIsMoving = true; }
    else if (Speed < 430.f)     { State.Tier = ESKMovementTier::Jog; State.bIsMoving = true; }
    else if (Speed < 550.f)     { State.Tier = ESKMovementTier::Run; State.bIsMoving = true; }
    else                         { State.Tier = ESKMovementTier::Sprint; State.bIsMoving = true; }

    if (Angle > -22.5f && Angle <= 22.5f)           State.Direction = ESKLocomotionDirection::Fwd;
    else if (Angle > 22.5f && Angle <= 67.5f)       State.Direction = ESKLocomotionDirection::Fwd_R;
    else if (Angle > 67.5f && Angle <= 112.5f)      State.Direction = ESKLocomotionDirection::R;
    else if (Angle > 112.5f && Angle <= 157.5f)     State.Direction = ESKLocomotionDirection::Bwd_R;
    else if (Angle > 157.5f || Angle <= -157.5f)    State.Direction = ESKLocomotionDirection::Bwd;
    else if (Angle > -157.5f && Angle <= -112.5f)   State.Direction = ESKLocomotionDirection::Bwd_L;
    else if (Angle > -112.5f && Angle <= -67.5f)    State.Direction = ESKLocomotionDirection::L;
    else                                             State.Direction = ESKLocomotionDirection::Fwd_L;
    return State;
}

int32 USKAnimationController::ResolveLocomotionAnimID(const FSKLocomotionState& State) const
{
    if (!AnimLogicData) return -1;
    FString Category;
    switch (State.Tier)
    {
    case ESKMovementTier::Idle:   Category = TEXT("Locomotion_Idle"); break;
    case ESKMovementTier::Walk:   Category = TEXT("Locomotion_Walk"); break;
    case ESKMovementTier::Run:    Category = TEXT("Locomotion_Run"); break;
    case ESKMovementTier::Jog:    Category = TEXT("Locomotion_Jog"); break;
    case ESKMovementTier::Sprint: Category = TEXT("Locomotion_Sprint"); break;
    default: return -1;
    }
    FString DirStr;
    switch (State.Direction)
    {
    case ESKLocomotionDirection::Fwd: DirStr = TEXT("_Fwd"); break;
    case ESKLocomotionDirection::Bwd: DirStr = TEXT("_Bwd"); break;
    case ESKLocomotionDirection::L:   DirStr = TEXT("_L"); break;
    case ESKLocomotionDirection::R:   DirStr = TEXT("_R"); break;
    default: DirStr = TEXT("_Fwd"); break;
    }
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Category + DirStr);
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    List = AnimLogicData->CategoryAnimMap.Find(Category);
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetTransitionAnimID(const FSKLocomotionState& From, const FSKLocomotionState& To) const
{
    FString Category = FString::Printf(TEXT("Locomotion_Transition_%d_to_%d"), (int32)From.Tier, (int32)To.Tier);
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetStopAnimID(const FSKLocomotionState& State) const
{
    FString Category = FString::Printf(TEXT("Locomotion_Stop_%d"), (int32)State.Tier);
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

int32 USKAnimationController::GetTurnAnimID(float AngleDelta) const
{
    if (FMath::Abs(AngleDelta) < 15.f) return -1;
    FString Category = (AngleDelta > 0) ? TEXT("Locomotion_Turn_R") : TEXT("Locomotion_Turn_L");
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(Category) : nullptr;
    if (List && List->IDs.Num() > 0) return List->IDs[0];
    // Fallback: any Locomotion anim
    List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Locomotion")) : nullptr;
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

void USKAnimationController::PlayLocomotionMontage(int32 AnimID, bool bLooping)
{
    if (AnimID <= 0) return;
    EnsureMontageLoaded(AnimID);

    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found || !Mesh.IsValid()) return;

    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;

    UAnimSequence* Seq = *Found;
    UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, 0.15f, 0.0f);
    if (DynMontage)
    {
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &USKAnimationController::OnLocoTransitionEnded);
        AnimInst->Montage_SetEndDelegate(EndDelegate);
    }
    CurrentAnimID = AnimID;
    CurrentPriority = ESKActionPriority::Locomotion;
}

void USKAnimationController::OnLocoTransitionEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (!bInterrupted) ProcessLocomotion();
}

// ── 动画播放 ──────────────────────────────────────────────

int32 USKAnimationController::ResolveAnimID(FName Action)
{
    if (!AnimLogicData) return -1;
    const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
    return (List && List->IDs.Num() > 0) ? List->IDs[0] : -1;
}

void USKAnimationController::PlayMontageByID(int32 AnimID, float Crossfade)
{
    EnsureMontageLoaded(AnimID);
    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found || !Mesh.IsValid()) return;

    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;

    UAnimSequence* Seq = *Found;
    UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, Crossfade, 0.0f);
    if (!DynMontage)
    {
        UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: PlaySlotAnimationAsDynamicMontage failed"),
            *GetNameSafe(OwnerCharacter.Get()));
    }
}

void USKAnimationController::EnsureMontageLoaded(int32 AnimID)
{
    if (!AnimLogicData) return;
    if (MontageCache.Contains(AnimID)) return;

    FString Prefix = AnimLogicData->AnimPrefixMap.FindRef(AnimID);
    if (Prefix.IsEmpty()) Prefix = TEXT("a000");

    FString PackageName = FString::Printf(TEXT("/Game/Characters/Sekiro/Animations/Anim_Sekiro_%s_%06d"), *Prefix, AnimID);
    FString ObjectName  = FString::Printf(TEXT("Anim_%s_%06d"), *Prefix, AnimID);
    FString AssetPath   = FString::Printf(TEXT("%s.%s"), *PackageName, *ObjectName);

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Load AnimID=%d (%s)"),
        *GetOwner()->GetName(), AnimID, *AssetPath);

    UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (Seq) MontageCache.Add(AnimID, Seq);
}

// ── 攻击碰撞�?────────────────────────────────────────────

void USKAnimationController::UpdateAttackHitbox()
{
    ASKWeapon* Weapon = GetWeapon();
    if (!AnimLogicData || CurrentAnimID <= 0)
    {
        if (Weapon) Weapon->DeactivateHitbox();
        return;
    }

    int32 Frame = FMath::RoundToInt(CurrentAnimTime * 30.0f);
    if (!Weapon) return;

    bool bHasActiveHitbox = false;
    const FSKAttackHitboxList* List = AnimLogicData->AttackHitboxConfigs.Find(CurrentAnimID);
    if (List)
    {
        for (const FSKAttackHitboxConfig& Cfg : List->Hitboxes)
        {
            if (Frame >= Cfg.StartFrame && Frame <= Cfg.EndFrame) { bHasActiveHitbox = true; break; }
        }
    }

    if (bHasActiveHitbox) Weapon->ActivateHitbox();
    else { Weapon->DeactivateHitbox(); Weapon->ClearHitActors(); }
}

ASKWeapon* USKAnimationController::GetWeapon() const
{
    if (!OwnerCharacter.IsValid()) return nullptr;
    USKWeaponComponent* WComp = OwnerCharacter->GetWeaponComponent();
    return WComp ? WComp->CurrentWeapon : nullptr;
}

USKAnimInstance* USKAnimationController::GetAnimInstance() const
{
    if (!Mesh.IsValid()) return nullptr;
    return Cast<USKAnimInstance>(Mesh->GetAnimInstance());
}
