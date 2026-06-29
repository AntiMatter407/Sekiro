// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/SKAnimationController.h"
#include "SekiroAnimLogicData.h"
#include "SKAnimInstance.h"
#include "Character/SKCharacter.h"
#include "Input/SKInputHandler.h"
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

// ── 生命周期 ──────────────────────────────────────────────────

void USKAnimationController::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<ASKCharacter>(GetOwner());
    if (!OwnerCharacter.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("AnimController[%s]: Owner is not ASKCharacter!"),
            *GetNameSafe(GetOwner()));
        return;
    }

    InputHandler = OwnerCharacter->GetInputHandler();
    Mesh = OwnerCharacter->GetMesh();

    if (!AnimLogicData)
    {
        AnimLogicData = LoadObject<USKAnimationLogicData>(nullptr,
            TEXT("/Game/Characters/Sekiro/DA_Sekiro_AnimLogic.DA_Sekiro_AnimLogic"));
    }

    // DataTable 自动加载
    if (!StateAnimTable)
    {
        StateAnimTable = LoadObject<UDataTable>(nullptr,
            TEXT("/Game/Characters/Sekiro/Data/DT_StateAnimMap.DT_StateAnimMap"));
    }
    if (!TransitionTable)
    {
        TransitionTable = LoadObject<UDataTable>(nullptr,
            TEXT("/Game/Characters/Sekiro/Data/DT_StateTransitions.DT_StateTransitions"));
    }

    InitStateMap();

    UE_LOG(LogTemp, Log, TEXT("AnimController[%s]: Initialized (DataTable: StateAnim=%s, Transition=%s)"),
        *GetNameSafe(OwnerCharacter.Get()),
        StateAnimTable ? TEXT("ok") : TEXT("null"),
        TransitionTable ? TEXT("ok") : TEXT("null"));
}

void USKAnimationController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateFrameState();
    UpdateFrameFlags();
    TimeInState += DeltaTime;

    static int32 TickCounter = 0;
    if (++TickCounter % 10 == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("AnimTick[%s]: State=%d Action=%s AnimID=%d Priority=%d Flags(Turn=%d Move=%d Deflect=%d Inv=%d)"),
            *GetNameSafe(OwnerCharacter.Get()),
            (int32)CurrentState, *CurrentAction.ToString(),
            CurrentAnimID, CurrentPriority,
            bDisableTurning, bDisableMovement, bCanDeflect, bInvincible);
        if (CurrentAnimID > 0)
        {
            TObjectPtr<UAnimSequence>* FoundSeq = MontageCache.Find(CurrentAnimID);
            UE_LOG(LogTemp, Log, TEXT("  CacheHit=%s AnimLogicData=%s"),
                FoundSeq && *FoundSeq ? TEXT("yes") : TEXT("no"),
                AnimLogicData ? TEXT("ok") : TEXT("null"));
        }
    }

    UpdateAttackHitbox();
    UpdateChargeState(DeltaTime);
    UpdateContextFlags();
    ProcessIntents();
    ProcessLocomotion();
}

// ── 帧级更新 ──────────────────────────────────────────────────

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

void USKAnimationController::UpdateFrameFlags()
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) return;

    float CurveValue = 0.f;
    if (AnimInst->GetCurveValue(TEXT("FrameFlags"), CurveValue))
    {
        int32 Mask = FMath::RoundToInt(CurveValue);
        bDisableTurning  = (Mask & (1 << (uint8)ESKFrameFlag::DisableTurning)) != 0;
        bDisableMovement = (Mask & (1 << (uint8)ESKFrameFlag::DisableMovement)) != 0
                        || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedWalk)) != 0
                        || (Mask & (1 << (uint8)ESKFrameFlag::LimitMoveSpeedDash)) != 0;
        bCanDeflect      = (Mask & (1 << (uint8)ESKFrameFlag::EnableParry)) != 0
                        && (Mask & (1 << (uint8)ESKFrameFlag::DisableParry)) == 0;
        bInvincible      = (Mask & (1 << (uint8)ESKFrameFlag::Invincible)) != 0;
    }
    else
    {
        bDisableTurning = false;
        bDisableMovement = false;
        bCanDeflect = false;
        bInvincible = false;
    }
}

// ── 上下文标志更新 ────────────────────────────────────────────

void USKAnimationController::UpdateContextFlags()
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) { bIsInAir = false; return; }

    bool bPrevInAir = bIsInAir;
    bIsInAir = AnimInst->bIsInAir;

    if (bPrevInAir && !bIsInAir)
    {
        if (CurrentState == ESKCharacterState::Airborne)
        {
            UE_LOG(LogTemp, Log, TEXT("State: Airborne -> Idle (landed)"));
            TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
        }
        bWasInAir = false;
    }

    if (bCounterWindow && CurrentAction != TEXT("Deflect") && CurrentAction != TEXT("Attack_Counter"))
    {
        bCounterWindow = false;
    }
}

// ── 状态迁移 ──────────────────────────────────────────────────

void USKAnimationController::TransitionTo(ESKCharacterState NewState, FName Action, int32 Priority)
{
    FName OldAction = CurrentAction;
    CurrentState = NewState;
    CurrentAction = Action;
    CurrentPriority = Priority;

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID > 0)
    {
        CurrentAnimID = AnimID;
        PlayMontageByID(AnimID, 0.1f);
    }
    else
    {
        CurrentAnimID = 0;
    }

    OnStateChanged(OldAction, Action);
}

bool USKAnimationController::CanTransition(FName Action, int32& OutPriority)
{
    OutPriority = GetActionPriority(Action);
    if (OutPriority >= CurrentPriority) return true;
    if (TransitionMap.Contains(Action)) return true;
    if (OutPriority == ESKActionPriority::Locomotion && CurrentPriority <= ESKActionPriority::Locomotion) return true;
    return false;
}

// ── 意图处理 ──────────────────────────────────────────────────

void USKAnimationController::ProcessIntents()
{
    if (!InputHandler.IsValid()) return;

    switch (CurrentState)
    {
    case ESKCharacterState::Idle:
    {
        if (bDeathBlowActive && HandleDeathblow()) break;
        USKAnimInstance* AI = GetAnimInstance();
        if (AI && AI->bIsInAir && !bWasInAir)
        {
            TransitionTo(ESKCharacterState::Airborne, TEXT("Airborne"), ESKActionPriority::Jump);
            bWasInAir = true;
            break;
        }
        if (InputHandler->ConsumeJumpPressed() && TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump)) break;
        if (InputHandler->ConsumeDodgePressed() && TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge)) break;
        if (InputHandler->IsGuardHeld() && TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard)) break;
        if (InputHandler->ConsumeAttackPressed() && TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack)) break;
        if (InputHandler->ConsumeProstheticPressed() && TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic)) break;
        if (InputHandler->ConsumeUseItemPressed() && TryPlayAction(TEXT("ItemUse"), ESKActionPriority::ItemUse)) break;
        if (InputHandler->ConsumeGrapplePressed() && TryPlayAction(TEXT("Grapple"), ESKActionPriority::Quickstep)) break;
        break;
    }
    case ESKCharacterState::Attack:
    {
        if (HandleDeathblow()) break;
        if (bIsInAir) { if (HandleAirDodge()) break; if (HandleAirAttack()) break; break; }
        HandleAttack();
        if (InputHandler->ConsumeDodgePressed() && TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge)) break;
        if (InputHandler->IsGuardHeld() && TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard)) break;
        if (InputHandler->ConsumeJumpPressed() && TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump)) break;
        break;
    }
    case ESKCharacterState::Guard:
    {
        if (HandleDeathblow()) break;
        if (InputHandler->ConsumeAttackPressed() && TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack)) break;
        if (InputHandler->ConsumeDodgePressed() && TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge)) break;
        if (InputHandler->ConsumeJumpPressed() && TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump)) break;
        if (!InputHandler->IsGuardHeld()) TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
        break;
    }
    case ESKCharacterState::Dodge:
    {
        if (HandleDeathblow()) break;
        if (InputHandler->ConsumeAttackPressed() && TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack)) break;
        break;
    }
    case ESKCharacterState::Jump:
    {
        if (HandleDeathblow()) break;
        USKAnimInstance* AI = GetAnimInstance();
        if (AI && AI->bIsInAir) { TransitionTo(ESKCharacterState::Airborne, TEXT("Airborne"), ESKActionPriority::Jump); bWasInAir = true; }
        break;
    }
    case ESKCharacterState::Airborne:
    {
        if (HandleAirAttack()) break;
        if (HandleAirDodge()) break;
        break;
    }
    case ESKCharacterState::Hit: break;
    case ESKCharacterState::Death: break;
    }
}

// ── 动作处理 ──────────────────────────────────────────────────

void USKAnimationController::HandleAttack()
{
    if (!InputHandler.IsValid()) return;
    if (InputHandler->IsAttackHeld())
    {
        if (!AttackState.bIsCharging) { AttackState.bIsCharging = true; AttackState.ChargeTime = 0.f; }
        return;
    }
    if (InputHandler->ConsumeAttackPressed())
    {
        FName ComboAction = TEXT("Attack");
        int32 NextAnimID = ResolveAnimID(ComboAction, CurrentAnimID);
        if (NextAnimID > 0 && NextAnimID != CurrentAnimID)
        {
            TryPlayAction(ComboAction, ESKActionPriority::Attack);
            AttackState.ComboIndex++;
        }
        else
        {
            TryPlayAction(ComboAction, ESKActionPriority::Attack);
            AttackState.ComboIndex = 1;
        }
    }
}

int32 USKAnimationController::GetComboAnimID(int32 ComboIdx) const
{
    const FSKAnimIDList* List = AnimLogicData ? AnimLogicData->CategoryAnimMap.Find(TEXT("Attack")) : nullptr;
    if (!List || List->IDs.Num() == 0) return -1;
    for (int32 Id : List->IDs) { if (Id >= 201000) return Id + ComboIdx; }
    return List->IDs[0] + ComboIdx;
}

FName USKAnimationController::GetMoveDirectionSuffix() const
{
    if (!OwnerCharacter.IsValid()) return NAME_None;
    FVector Vel = OwnerCharacter->GetVelocity();
    if (Vel.Size2D() < 10.f) return NAME_None;
    FVector Fwd = OwnerCharacter->GetActorForwardVector();
    FVector Right = OwnerCharacter->GetActorRightVector();
    float FwdDot = FVector::DotProduct(Vel.GetSafeNormal2D(), Fwd);
    float RightDot = FVector::DotProduct(Vel.GetSafeNormal2D(), Right);
    if (FwdDot > 0.7f) return TEXT("_Fwd");
    if (FwdDot < -0.7f) return TEXT("_Bwd");
    if (RightDot > 0.7f) return TEXT("_R");
    if (RightDot < -0.7f) return TEXT("_L");
    return NAME_None;
}

void USKAnimationController::ResetAttackState() { AttackState = FAttackState(); }

bool USKAnimationController::CheckChargeRelease()
{
    if (AttackState.bIsCharging && !InputHandler->IsAttackHeld())
    {
        AttackState.bIsCharging = false;
        if (AttackState.ChargeTime > 0.3f) { TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack); return true; }
    }
    return false;
}

void USKAnimationController::HandleGuard() { if (InputHandler.IsValid() && InputHandler->IsGuardHeld()) TryPlayAction(TEXT("Guard"), ESKActionPriority::Guard); }
void USKAnimationController::HandleDodge() { if (InputHandler.IsValid() && InputHandler->ConsumeDodgePressed()) TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge); }
void USKAnimationController::HandleJump() { if (InputHandler.IsValid() && InputHandler->ConsumeJumpPressed()) TryPlayAction(TEXT("Jump"), ESKActionPriority::Jump); }
bool USKAnimationController::HandleAirAttack() { return InputHandler.IsValid() && InputHandler->ConsumeAttackPressed() && TryPlayAction(TEXT("Attack"), ESKActionPriority::Attack); }
bool USKAnimationController::HandleAirDodge() { return InputHandler.IsValid() && InputHandler->ConsumeDodgePressed() && TryPlayAction(TEXT("Dodge"), ESKActionPriority::Dodge); }
void USKAnimationController::HandleProsthetic() { if (InputHandler.IsValid() && InputHandler->ConsumeProstheticPressed()) TryPlayAction(TEXT("Prosthetic"), ESKActionPriority::Prosthetic); }
void USKAnimationController::HandleItemUse() { if (InputHandler.IsValid() && InputHandler->ConsumeUseItemPressed()) TryPlayAction(TEXT("ItemUse"), ESKActionPriority::ItemUse); }
void USKAnimationController::HandleGrapple() { if (InputHandler.IsValid() && InputHandler->ConsumeGrapplePressed()) TryPlayAction(TEXT("Grapple"), ESKActionPriority::Quickstep); }
void USKAnimationController::HandleCombatArt() { /* TODO: 战技系统 */ }

bool USKAnimationController::HandleDeathblow()
{
    if (!bDeathBlowActive) return false;
    bDeathBlowActive = false;
    return TryPlayAction(TEXT("Deathblow"), ESKActionPriority::Deathblow);
}

void USKAnimationController::UpdateChargeState(float DeltaTime)
{
    if (AttackState.bIsCharging) AttackState.ChargeTime += DeltaTime;
    CheckChargeRelease();
}

// ── Locomotion ────────────────────────────────────────────────

void USKAnimationController::ProcessLocomotion()
{
    if (CurrentPriority > ESKActionPriority::Locomotion && CurrentAnimID > 0) return;
    if (bDisableMovement && CurrentAnimID > 0) return;
    if (!OwnerCharacter.IsValid() || !InputHandler.IsValid()) return;

    USKAnimInstance* AnimInst = GetAnimInstance();
    float Speed = OwnerCharacter->GetVelocity().Size2D();
    float Angle = 0.f;

    if (!bDisableTurning || CurrentAnimID == 0)
    {
        FVector VelDir = OwnerCharacter->GetVelocity().GetSafeNormal2D();
        if (!VelDir.IsNearlyZero())
        {
            FVector Fwd = OwnerCharacter->GetActorForwardVector();
            Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(VelDir, Fwd), -1.f, 1.f)));
            Angle *= FVector::CrossProduct(Fwd, VelDir).Z > 0 ? 1.f : -1.f;
        }
    }

    if (AnimInst)
    {
        AnimInst->Speed = Speed;
        AnimInst->Angle = Angle;

        FVector2D MoveIntent = InputHandler->GetMoveIntent();
        if (MoveIntent.Size() > 0.1f)
        {
            float MoveAngle = FMath::RadiansToDegrees(FMath::Atan2(MoveIntent.X, MoveIntent.Y));
            MoveAngle = FMath::UnwindDegrees(MoveAngle);
            if (MoveAngle > -22.5f && MoveAngle <= 22.5f)        AnimInst->Direction = ESKLocomotionDirection::Fwd;
            else if (MoveAngle > 22.5f && MoveAngle <= 67.5f)    AnimInst->Direction = ESKLocomotionDirection::Fwd_R;
            else if (MoveAngle > 67.5f && MoveAngle <= 112.5f)   AnimInst->Direction = ESKLocomotionDirection::R;
            else if (MoveAngle > 112.5f && MoveAngle <= 157.5f)  AnimInst->Direction = ESKLocomotionDirection::Bwd_R;
            else if (MoveAngle > 157.5f || MoveAngle <= -157.5f) AnimInst->Direction = ESKLocomotionDirection::Bwd;
            else if (MoveAngle > -157.5f && MoveAngle <= -112.5f)AnimInst->Direction = ESKLocomotionDirection::Bwd_L;
            else if (MoveAngle > -112.5f && MoveAngle <= -67.5f) AnimInst->Direction = ESKLocomotionDirection::L;
            else if (MoveAngle > -67.5f && MoveAngle <= -22.5f)  AnimInst->Direction = ESKLocomotionDirection::Fwd_L;
        }
    }
}

// ── AnimID 解析 ───────────────────────────────────────────────

int32 USKAnimationController::ResolveAnimID(FName Action)
{
    return ResolveAnimID(Action, 0);
}

int32 USKAnimationController::ResolveAnimID(FName Action, int32 FromAnimID)
{
    // 1. 同动作连段推导
    if (FromAnimID > 0 && Action == CurrentAction)
    {
        int32 NextAnimID = DeriveNextAnim(FromAnimID, Action);
        if (NextAnimID > 0) return NextAnimID;
    }

    // 2. TAE DataAsset CategoryAnimMap
    if (AnimLogicData)
    {
        const FSKAnimIDList* List = AnimLogicData->CategoryAnimMap.Find(Action.ToString());
        if (List && List->IDs.Num() > 0)
        {
            if (Action == TEXT("Attack"))
            {
                for (int32 Id : List->IDs) { if (Id >= 201000) return Id; }
            }
            return List->IDs[0];
        }
    }

    // 3. 回退到 StateAnimMap（DataTable / 硬编码）
    FName* AnimStr = StateAnimMap.Find(Action);
    if (AnimStr && !AnimStr->IsNone())
    {
        FString Str = AnimStr->ToString();
        int32 UnderscoreIndex;
        if (Str.FindChar(TCHAR('_'), UnderscoreIndex))
        {
            int32 Id = FCString::Atoi(*Str.Mid(UnderscoreIndex + 1));
            if (Id > 0) return Id;
        }
    }

    return -1;
}

int32 USKAnimationController::DeriveNextAnim(int32 InCurrentAnimID, FName Action) const
{
    if (!AnimLogicData || InCurrentAnimID <= 0) return -1;
    int32 JudgeId = InCurrentAnimID % 1000;
    int32 NextAnimID = (InCurrentAnimID / 1000) * 1000 + (JudgeId + 1);
    if (AnimLogicData->AnimPrefixMap.Contains(NextAnimID)) return NextAnimID;
    return -1;
}

// ── TryPlayAction ─────────────────────────────────────────────

bool USKAnimationController::TryPlayAction(FName Action, int32 Priority)
{
    if (!AnimLogicData) return false;
    if (CurrentPriority > Priority && CurrentAnimID > 0) return false;

    int32 OutPriority;
    if (!CanTransition(Action, OutPriority)) return false;

    if (CurrentAnimID > 0 && Action != CurrentAction)
    {
        float Crossfade;
        if (!CanCancelTo(Action, Crossfade)) return false;
    }

    int32 AnimID = ResolveAnimID(Action);
    if (AnimID <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("TryPlayAction: No AnimID for Action=%s"), *Action.ToString());
        return false;
    }

    PlayMontageByID(AnimID, 0.1f);
    CurrentAction = Action;
    CurrentPriority = Priority;
    CurrentAnimID = AnimID;
    return true;
}

// ── Montage 播放 ──────────────────────────────────────────────

void USKAnimationController::PlayMontageByID(int32 AnimID, float Crossfade)
{
    if (!Mesh.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: Mesh invalid")); return; }
    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) { UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: AnimInstance null")); return; }

    EnsureMontageLoaded(AnimID);
    TObjectPtr<UAnimSequence>* Found = MontageCache.Find(AnimID);
    if (!Found || !*Found) { UE_LOG(LogTemp, Warning, TEXT("PlayMontageByID: AnimID=%d not in cache"), AnimID); return; }

    UAnimSequence* Seq = *Found;
    UAnimMontage* DynMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), 0.1f, 0.1f, 1.0f, 1, Crossfade, 0.0f);
    if (DynMontage)
    {
        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &USKAnimationController::OnActionMontageEnded);
        AnimInst->Montage_SetEndDelegate(EndDelegate);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("PlayMontageByID: AnimID=%d -> FAILED"), AnimID);
    }
}

void USKAnimationController::EnsureMontageLoaded(int32 AnimID)
{
    if (!AnimLogicData || MontageCache.Contains(AnimID)) return;
    FString AssetPath = AnimLogicData->BuildAnimAssetPath(AnimID);
    UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (Seq) MontageCache.Add(AnimID, Seq);
}

void USKAnimationController::OnActionMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (!bInterrupted)
    {
        UE_LOG(LogTemp, Log, TEXT("MontageEnd: Action=%s AnimID=%d -> Idle"), *CurrentAction.ToString(), CurrentAnimID);
        TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("MontageEnd: Action=%s AnimID=%d INTERRUPTED"), *CurrentAction.ToString(), CurrentAnimID);
    }
}

void USKAnimationController::OnAnyMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    OnActionMontageEnded(Montage, bInterrupted);
}

// ── 帧级曲线查询 ──────────────────────────────────────────────

bool USKAnimationController::CanCancelTo(FName TargetAction, float& OutCrossfade) const
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) return false;

    float CurveValue = 0.f;
    if (!AnimInst->GetCurveValue(TEXT("CancelActions"), CurveValue)) return false;

    ESKCancelAction CancelAction = (ESKCancelAction)FMath::RoundToInt(CurveValue);

    int32 CurrentPrio = GetActionPriority(CurrentAction);
    int32 TargetPrio = GetActionPriority(TargetAction);
    if (TargetPrio <= CurrentPrio && CurrentAction != NAME_None) return false;

    static const TMap<FName, ESKCancelAction> ActionMap = {
        {TEXT("Attack"),     ESKCancelAction::Attack},
        {TEXT("Guard"),      ESKCancelAction::Guard},
        {TEXT("Dodge"),      ESKCancelAction::Dodge},
        {TEXT("Prosthetic"), ESKCancelAction::Prosthetic},
        {TEXT("Item"),       ESKCancelAction::Item},
    };
    const ESKCancelAction* Target = ActionMap.Find(TargetAction);
    if (!Target || CancelAction != *Target) return false;

    OutCrossfade = 0.1f;
    return true;
}

bool USKAnimationController::IsHitboxActive() const
{
    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) return false;
    float CurveValue = 0.f;
    if (!AnimInst->GetCurveValue(TEXT("AttackHitbox"), CurveValue)) return false;
    return (ESKAttackHitboxType)FMath::RoundToInt(CurveValue) != ESKAttackHitboxType::None;
}

int32 USKAnimationController::GetActionPriority(FName Action)
{
    static const TMap<FName, int32> PriorityMap = {
        {TEXT("Deathblow"), 10}, {TEXT("Resurrection"), 10}, {TEXT("Death"), 9},
        {TEXT("Hit"), 8}, {TEXT("Dodge"), 7}, {TEXT("Deflect"), 6},
        {TEXT("Guard"), 5}, {TEXT("Prosthetic"), 4}, {TEXT("ItemUse"), 3},
        {TEXT("Attack"), 2}, {TEXT("Quickstep"), 1},
    };
    const int32* P = PriorityMap.Find(Action);
    return P ? *P : 0;
}

// ── 攻击碰撞盒 ────────────────────────────────────────────────

void USKAnimationController::UpdateAttackHitbox()
{
    ASKWeapon* Weapon = GetWeapon();
    if (!Weapon) return;

    USKAnimInstance* AnimInst = GetAnimInstance();
    if (!AnimInst) { Weapon->DeactivateHitbox(); return; }

    float CurveValue = 0.f;
    if (AnimInst->GetCurveValue(TEXT("AttackHitbox"), CurveValue))
    {
        ESKAttackHitboxType HitType = (ESKAttackHitboxType)FMath::RoundToInt(CurveValue);
        if (HitType != ESKAttackHitboxType::None) { Weapon->ActivateHitbox(); return; }
    }

    Weapon->DeactivateHitbox();
    Weapon->ClearHitActors();
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

// ── 外部事件 ──────────────────────────────────────────────────

bool USKAnimationController::IsEnemyAttacking(int32& OutBehaviorJudgeID, int32& OutStartFrame) const
{
    OutBehaviorJudgeID = 0; OutStartFrame = 0;
    return false; // TODO: 锁敌系统
}

ASKCharacter* USKAnimationController::GetLockOnTarget() const
{
    return nullptr; // TODO: 锁敌系统
}

void USKAnimationController::OnGuardHit(int32 AnimID)
{
    if (GuardState.Phase == ESKGuardPhase::NotGuarding) return;
    TransitionTo(ESKCharacterState::Guard, TEXT("Guard"), ESKActionPriority::Guard);
}

void USKAnimationController::OnGuardBreak()
{
    GuardState.Phase = ESKGuardPhase::Broken;
    TransitionTo(ESKCharacterState::Hit, TEXT("Hit"), ESKActionPriority::Hit);
}

void USKAnimationController::OnHitReceived(int32 AnimID)
{
    TransitionTo(ESKCharacterState::Hit, TEXT("Hit"), ESKActionPriority::Hit);
}

void USKAnimationController::OnDeath(int32 AnimID)
{
    TransitionTo(ESKCharacterState::Death, TEXT("Death"), ESKActionPriority::Death);
}

void USKAnimationController::OnResurrection()
{
    TransitionTo(ESKCharacterState::Idle, TEXT("Locomotion"), ESKActionPriority::Locomotion);
}

// ============================================================================
// 状态机初始化
// ============================================================================

void USKAnimationController::InitStateMap()
{
    // ═══ 硬编码过渡规则（Event → TargetState） ═══
    TransitionMap.Add(TEXT("W_StandIdle"),             TEXT("Idle"));
    TransitionMap.Add(TEXT("W_StandMoveStart"),        TEXT("Walk"));
    TransitionMap.Add(TEXT("W_StandMoveLoop"),         TEXT("Walk"));
    TransitionMap.Add(TEXT("W_StandJogLoop"),          TEXT("Jog"));
    TransitionMap.Add(TEXT("W_StandRunLoop"),          TEXT("Run"));
    TransitionMap.Add(TEXT("W_StandRun"),              TEXT("Run"));
    TransitionMap.Add(TEXT("W_SprintLoop"),            TEXT("Sprint"));
    TransitionMap.Add(TEXT("W_Sprint"),                TEXT("Sprint"));
    TransitionMap.Add(TEXT("W_CrouchIdle"),            TEXT("CrouchIdle"));
    TransitionMap.Add(TEXT("W_CrouchMoveLoop"),        TEXT("CrouchMove"));
    TransitionMap.Add(TEXT("W_GroundJumpReady"),       TEXT("Jump"));
    TransitionMap.Add(TEXT("W_GroundJumpStart"),       TEXT("Jump"));
    TransitionMap.Add(TEXT("W_GroundJumpFall"),        TEXT("FreeFall"));
    TransitionMap.Add(TEXT("W_SprintJumpReady"),       TEXT("Jump"));
    TransitionMap.Add(TEXT("W_SprintJumpStart"),       TEXT("Jump"));
    TransitionMap.Add(TEXT("W_FreeFall"),              TEXT("FreeFall"));
    TransitionMap.Add(TEXT("W_LandFreeFall"),          TEXT("Land"));
    TransitionMap.Add(TEXT("W_LandGroundJump"),        TEXT("Land"));
    TransitionMap.Add(TEXT("Jump_to_FreeFall"),        TEXT("FreeFall"));
    TransitionMap.Add(TEXT("FreeFall_to_Land"),        TEXT("Land"));
    TransitionMap.Add(TEXT("Land_to_Idle"),            TEXT("Idle"));
    TransitionMap.Add(TEXT("W_Attack"),                TEXT("Attack"));
    TransitionMap.Add(TEXT("W_AttackCombo"),           TEXT("Attack"));
    TransitionMap.Add(TEXT("W_DeflectGuardIdle"),      TEXT("Guard"));
    TransitionMap.Add(TEXT("W_DeflectGuardMove"),      TEXT("Guard"));
    TransitionMap.Add(TEXT("Deflect_to_Counter"),      TEXT("Attack"));
    TransitionMap.Add(TEXT("Guard_to_Idle"),           TEXT("Idle"));
    TransitionMap.Add(TEXT("W_Dodge"),                 TEXT("Dodge"));
    TransitionMap.Add(TEXT("W_Step"),                  TEXT("Dodge"));
    TransitionMap.Add(TEXT("Dodge_to_Idle"),           TEXT("Idle"));
    TransitionMap.Add(TEXT("W_StandDamageSmall"),      TEXT("HitSmall"));
    TransitionMap.Add(TEXT("W_StandDamageLarge"),      TEXT("HitLarge"));
    TransitionMap.Add(TEXT("W_StandDamageBreak"),      TEXT("Knockdown"));
    TransitionMap.Add(TEXT("Hit_to_Idle"),             TEXT("Idle"));
    TransitionMap.Add(TEXT("W_FallDeathStart"),        TEXT("Death"));
    TransitionMap.Add(TEXT("W_GroundDeathStart"),      TEXT("Death"));
    TransitionMap.Add(TEXT("W_WallJumpReady"),         TEXT("WallJump"));
    TransitionMap.Add(TEXT("W_WallJumpStart"),         TEXT("WallJump"));
    TransitionMap.Add(TEXT("WallJump_to_FreeFall"),    TEXT("FreeFall"));
    TransitionMap.Add(TEXT("W_WireMove"),              TEXT("WireMove"));
    TransitionMap.Add(TEXT("W_AirWireMoveStart"),      TEXT("WireMove"));
    TransitionMap.Add(TEXT("W_GroundWireShoot"),       TEXT("WireMove"));
    TransitionMap.Add(TEXT("W_SwimIdle"),              TEXT("SwimIdle"));
    TransitionMap.Add(TEXT("W_SwimMoveStart"),         TEXT("SwimIdle"));
    TransitionMap.Add(TEXT("W_SwimMoveLoop"),          TEXT("SwimIdle"));
    TransitionMap.Add(TEXT("W_DiveIdle"),              TEXT("DiveIdle"));
    TransitionMap.Add(TEXT("W_DiveMoveStart"),         TEXT("DiveIdle"));
    TransitionMap.Add(TEXT("W_DiveMoveLoop"),          TEXT("DiveIdle"));
    TransitionMap.Add(TEXT("W_HangIdle"),              TEXT("HangIdle"));
    TransitionMap.Add(TEXT("W_HangMove"),              TEXT("HangIdle"));
    TransitionMap.Add(TEXT("W_HangToStand"),           TEXT("Idle"));
    TransitionMap.Add(TEXT("W_FreeFallToHang"),        TEXT("HangIdle"));
    TransitionMap.Add(TEXT("W_StandQuickTurnLeft180"), TEXT("Dash"));
    TransitionMap.Add(TEXT("W_StandQuickTurnRight180"),TEXT("Dash"));
    TransitionMap.Add(TEXT("W_SprintQuickTurnLeft180"),TEXT("Dash"));
    TransitionMap.Add(TEXT("W_SprintQuickTurnRight180"),TEXT("Dash"));

    // ═══ 硬编码 State → AnimID 映射 ═══
    StateAnimMap.Add(TEXT("Idle"),       TEXT("a000_000000"));
    StateAnimMap.Add(TEXT("Walk"),       TEXT("a000_000100"));
    StateAnimMap.Add(TEXT("Jog"),        TEXT("a000_000200"));
    StateAnimMap.Add(TEXT("Run"),        TEXT("a000_000300"));
    StateAnimMap.Add(TEXT("Sprint"),     TEXT("a000_001151"));
    StateAnimMap.Add(TEXT("Dash"),       TEXT("a000_000410"));
    StateAnimMap.Add(TEXT("CrouchIdle"), TEXT("a000_005000"));
    StateAnimMap.Add(TEXT("CrouchMove"), TEXT("a000_005100"));
    StateAnimMap.Add(TEXT("Jump"),       TEXT("a000_060000"));
    StateAnimMap.Add(TEXT("FreeFall"),   TEXT("a000_200000"));
    StateAnimMap.Add(TEXT("Land"),       TEXT("a000_065000"));
    StateAnimMap.Add(TEXT("Attack"),     TEXT("a000_200000"));
    StateAnimMap.Add(TEXT("Deflect"),    TEXT("a050_120100"));
    StateAnimMap.Add(TEXT("Guard"),      TEXT("a050_130100"));
    StateAnimMap.Add(TEXT("HitSmall"),   TEXT("a070_400000"));
    StateAnimMap.Add(TEXT("HitLarge"),   TEXT("a070_401000"));
    StateAnimMap.Add(TEXT("Knockdown"),  TEXT("a070_410000"));
    StateAnimMap.Add(TEXT("Death"),      TEXT("a070_412000"));
    StateAnimMap.Add(TEXT("WallJump"),   TEXT("a000_069000"));
    StateAnimMap.Add(TEXT("WireMove"),   TEXT("a000_207000"));
    StateAnimMap.Add(TEXT("SwimIdle"),   TEXT("a000_300000"));
    StateAnimMap.Add(TEXT("DiveIdle"),   TEXT("a000_104100"));
    StateAnimMap.Add(TEXT("HangIdle"),   TEXT("a000_013000"));

    LoadTransitionsFromDataTable();
    LoadStateAnimMapFromDataTable();

    UE_LOG(LogTemp, Log, TEXT("[AnimController] InitStateMap: %d transitions, %d anim mappings"),
        TransitionMap.Num(), StateAnimMap.Num());
}

void USKAnimationController::LoadTransitionsFromDataTable()
{
    if (!TransitionTable) return;
    static const FString ContextStr(TEXT("USKAnimationController::LoadTransitionsFromDataTable"));
    TArray<FSKStateTransitionRow*> Rows;
    TransitionTable->GetAllRows(ContextStr, Rows);
    for (FSKStateTransitionRow* Row : Rows)
    {
        if (Row && Row->EventName != NAME_None && Row->ToState != NAME_None)
            TransitionMap.Add(Row->EventName, Row->ToState);
    }
    UE_LOG(LogTemp, Log, TEXT("[AnimController] Loaded %d transitions from DataTable"), Rows.Num());
}

void USKAnimationController::LoadStateAnimMapFromDataTable()
{
    if (!StateAnimTable) return;
    static const FString ContextStr(TEXT("USKAnimationController::LoadStateAnimMapFromDataTable"));
    TArray<FSKStateAnimRow*> Rows;
    StateAnimTable->GetAllRows(ContextStr, Rows);
    for (FSKStateAnimRow* Row : Rows)
    {
        if (Row && Row->StateName != NAME_None && !Row->PrimaryAnimID.IsEmpty())
            StateAnimMap.Add(Row->StateName, FName(*Row->PrimaryAnimID));
    }
    UE_LOG(LogTemp, Log, TEXT("[AnimController] Loaded %d state-anim entries from DataTable"), Rows.Num());
}

// ── 状态机运行时 ──────────────────────────────────────────────

bool USKAnimationController::SendStateEvent(FName EventName)
{
    FName* TargetState = TransitionMap.Find(EventName);
    if (!TargetState) return false;
    ForceState(*TargetState);
    return true;
}

void USKAnimationController::ForceState(FName NewState)
{
    if (NewState == CurrentAction) return;
    OnStateChanged(CurrentAction, NewState);
}

FName USKAnimationController::EvaluateLocomotionState(float MoveSpeed) const
{
    if (MoveSpeed < 10.f)  return TEXT("Idle");
    if (MoveSpeed < 200.f) return TEXT("Walk");
    if (MoveSpeed < 400.f) return TEXT("Jog");
    if (MoveSpeed < 600.f) return TEXT("Run");
    return TEXT("Sprint");
}

void USKAnimationController::OnStateChanged(FName OldState, FName NewState)
{
    PreviousAction = OldState;
    CurrentAction = NewState;
    TimeInState = 0.f;

    FName* AnimIDStr = StateAnimMap.Find(NewState);
    if (AnimIDStr && !AnimIDStr->IsNone())
    {
        FString AnimStr = AnimIDStr->ToString();
        int32 UnderscoreIndex;
        if (AnimStr.FindChar(TCHAR('_'), UnderscoreIndex))
        {
            int32 ParsedID = FCString::Atoi(*AnimStr.Mid(UnderscoreIndex + 1));
            if (ParsedID > 0) CurrentAnimID = ParsedID;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[AnimController] StateChanged: %s -> %s (AnimID=%d, TimeInState=%.2f)"),
        *OldState.ToString(), *NewState.ToString(), CurrentAnimID, TimeInState);
}