#include "Camera/SKCameraManagerComponent.h"

#include "Animation/SKAnimInstance.h"
#include "Input/SKInputManager.h"
#include "Movement/SKMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static UnLua::FLuaRetValues RequireSKCameraLuaModule(UnLua::FLuaEnv* LuaEnv, const FString& LuaModuleName, bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKCameraManager Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKCameraManager Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    static bool ReadSKCameraLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName)
    {
        if (!ReturnValues.IsValid()) return false;
        if (ReturnValues.Num() == 0) return false;
        if (ReturnValues[0].GetType() == LUA_TNIL) return false;

        if (ReturnValues[0].GetType() == LUA_TBOOLEAN)
        {
            return ReturnValues[0].Value<bool>();
        }

        UE_LOG(LogTemp, Warning, TEXT("SKCameraManager Lua Tick should return boolean. Module=%s"), *LuaModuleName);
        return false;
    }

    static float InterpSKYawShortest(float CurrentYaw, float TargetYaw, float DeltaTime, float InterpSpeed)
    {
        if (InterpSpeed <= 0.f) return FMath::UnwindDegrees(TargetYaw);
        if (DeltaTime <= 0.f) return FMath::UnwindDegrees(CurrentYaw);

        const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
        if (FMath::Square(DeltaYaw) < UE_SMALL_NUMBER) return FMath::UnwindDegrees(TargetYaw);

        const float Alpha = FMath::Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);
        return FMath::UnwindDegrees(CurrentYaw + DeltaYaw * Alpha);
    }
}

USKCameraManagerComponent::USKCameraManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;

    bInvertPitch = false;
}

void USKCameraManagerComponent::SetLockTarget(AActor* NewTarget)
{
    if (NewTarget == GetOwner())
    {
        LockTarget = nullptr;
        return;
    }

    LockTarget = NewTarget;
}

void USKCameraManagerComponent::ClearLockTarget()
{
    LockTarget = nullptr;
}

void USKCameraManagerComponent::ToggleLockTarget(AActor* NewTarget)
{
    if (IsLockedOn())
    {
        ClearLockTarget();
        return;
    }

    SetLockTarget(NewTarget);
}

bool USKCameraManagerComponent::ToggleLockTargetInView()
{
    if (IsLockedOn())
    {
        ClearLockTarget();
        return false;
    }

    AActor* BestTarget = FindBestLockTargetInView();
    if (!BestTarget) return false;

    SetLockTarget(BestTarget);
    return IsLockedOn();
}

AActor* USKCameraManagerComponent::FindBestLockTargetInView() const
{
    if (!OwnerCharacter || !GetWorld()) return nullptr;

    FVector ViewLocation = OwnerCharacter->GetActorLocation();
    FRotator ViewRotation = OwnerCharacter->GetActorRotation();
    const AController* Controller = OwnerCharacter->GetController();
    if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
    {
        PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
    }
    else if (Controller)
    {
        ViewRotation = Controller->GetControlRotation();
    }

    const FVector ViewForward = ViewRotation.Vector();
    const float ClampedMaxRange = FMath::Max(1.f, MaxLockOnRange);
    const float ClampedHalfAngle = FMath::Max(1.f, LockOnSearchHalfAngle);
    const float MaxRangeSq = FMath::Square(ClampedMaxRange);
    AActor* BestTarget = nullptr;
    float BestScore = -TNumericLimits<float>::Max();

    for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        ACharacter* Candidate = *It;
        if (!IsValid(Candidate) || Candidate == OwnerCharacter.Get()) continue;

        const FVector ToTarget = Candidate->GetActorLocation() - ViewLocation;
        const float DistanceSq = ToTarget.SizeSquared();
        if (DistanceSq <= KINDA_SMALL_NUMBER || DistanceSq > MaxRangeSq) continue;

        const FVector TargetDirection = ToTarget.GetSafeNormal();
        const float Dot = FMath::Clamp(FVector::DotProduct(ViewForward, TargetDirection), -1.f, 1.f);
        const float Angle = FMath::RadiansToDegrees(FMath::Acos(Dot));
        if (Angle > ClampedHalfAngle) continue;

        const float Distance = FMath::Sqrt(DistanceSq);
        const float AngleScore = 1.f - (Angle / ClampedHalfAngle);
        const float DistanceScore = 1.f - (Distance / ClampedMaxRange);
        const float Score = AngleScore * LockOnAngleScoreWeight + DistanceScore * LockOnDistanceScoreWeight;
        if (Score > BestScore)
        {
            BestScore = Score;
            BestTarget = Candidate;
        }
    }

    return BestTarget;
}

bool USKCameraManagerComponent::IsLockedOn() const
{
    return IsValid(LockTarget);
}

AActor* USKCameraManagerComponent::GetLockTarget() const
{
    return LockTarget;
}

void USKCameraManagerComponent::AddLookInput(FVector2D LookAxis)
{
    LookAxis.X *= LookSensitivityYaw;
    LookAxis.Y *= LookSensitivityPitch;

    if (bInvertPitch)
    {
        LookAxis.Y *= -1.f;
    }

    PendingLookInput += LookAxis;
}

ESKCameraMode USKCameraManagerComponent::GetCameraMode() const
{
    return CameraMode;
}

bool USKCameraManagerComponent::IsSprintCameraAligning() const
{
    return CameraMode == ESKCameraMode::SprintAlign;
}

float USKCameraManagerComponent::GetMoveDirectionAngle() const
{
    return MoveDirectionAngle;
}

void USKCameraManagerComponent::SetUseLuaCameraLogic(bool bNewUseLuaCameraLogic)
{
    bUseLuaCameraLogic = bNewUseLuaCameraLogic;
}

bool USKCameraManagerComponent::IsUsingLuaCameraLogic() const
{
    return bUseLuaCameraLogic;
}

void USKCameraManagerComponent::SetLuaCameraModuleName(const FString& ModuleName)
{
    LuaCameraModuleName = ModuleName;
}

FString USKCameraManagerComponent::GetLuaCameraModuleName() const
{
    return LuaCameraModuleName;
}

FString USKCameraManagerComponent::GetModuleName_Implementation() const
{
    return LuaCameraModuleName;
}

void USKCameraManagerComponent::RefreshCachedCameraComponents()
{
    RefreshCachedComponents();
}

void USKCameraManagerComponent::ValidateLockTargetForScript()
{
    ValidateLockTarget();
}

bool USKCameraManagerComponent::HasOwnerCharacter() const
{
    return OwnerCharacter != nullptr;
}

void USKCameraManagerComponent::SetCameraModeByName(FName ModeName)
{
    CameraMode = ResolveCameraModeByName(ModeName);
}

FName USKCameraManagerComponent::GetCameraModeName() const
{
    if (CameraMode == ESKCameraMode::LockOn) return FName(TEXT("LockOn"));
    if (CameraMode == ESKCameraMode::SprintAlign) return FName(TEXT("SprintAlign"));
    return FName(TEXT("Free"));
}

void USKCameraManagerComponent::SetMoveDirectionAngleForScript(float NewMoveDirectionAngle)
{
    MoveDirectionAngle = NewMoveDirectionAngle;
}

void USKCameraManagerComponent::SetMovementRotationSettingsForScript(bool bOrientRotationToMovement, bool bUseControllerDesiredRotation)
{
    if (!OwnerCharacter) return;

    UCharacterMovementComponent* CharacterMovement = OwnerCharacter->GetCharacterMovement();
    if (!CharacterMovement) return;

    CharacterMovement->bOrientRotationToMovement = bOrientRotationToMovement;
    CharacterMovement->bUseControllerDesiredRotation = bUseControllerDesiredRotation;
}

bool USKCameraManagerComponent::IsMovementTierSprint() const
{
    return MovementComponent && MovementComponent->CurrentMovementTier == ESKMovementTier::Sprint;
}

bool USKCameraManagerComponent::HasDesiredMoveYaw() const
{
    float TargetYaw = 0.f;
    return GetDesiredMoveYaw(TargetYaw);
}

float USKCameraManagerComponent::GetDesiredMoveYawOrFallback(float FallbackYaw) const
{
    float TargetYaw = FallbackYaw;
    GetDesiredMoveYaw(TargetYaw);
    return TargetYaw;
}

bool USKCameraManagerComponent::HasLockTargetYaw() const
{
    float TargetYaw = 0.f;
    return GetLockTargetYaw(TargetYaw);
}

float USKCameraManagerComponent::GetLockTargetYawOrFallback(float FallbackYaw) const
{
    float TargetYaw = FallbackYaw;
    GetLockTargetYaw(TargetYaw);
    return TargetYaw;
}

bool USKCameraManagerComponent::HasOwnerVelocity() const
{
    return OwnerCharacter && !OwnerCharacter->GetVelocity().IsNearlyZero();
}

float USKCameraManagerComponent::GetOwnerVelocityYawOrFallback(float FallbackYaw) const
{
    if (!HasOwnerVelocity()) return FallbackYaw;

    return OwnerCharacter->GetVelocity().Rotation().Yaw;
}

float USKCameraManagerComponent::GetOwnerYaw() const
{
    return OwnerCharacter ? OwnerCharacter->GetActorRotation().Yaw : 0.f;
}

float USKCameraManagerComponent::GetControllerYawOrFallback(float FallbackYaw) const
{
    if (!OwnerCharacter) return FallbackYaw;

    const AController* Controller = OwnerCharacter->GetController();
    if (!Controller) return FallbackYaw;

    return Controller->GetControlRotation().Yaw;
}

float USKCameraManagerComponent::NormalizeDeltaYaw(float FromYaw, float ToYaw) const
{
    return FMath::FindDeltaAngleDegrees(FromYaw, ToYaw);
}

bool USKCameraManagerComponent::IsActorYawOwnedByRootMotion() const
{
    if (!OwnerCharacter || !OwnerCharacter->GetMesh()) return false;

    const USKAnimInstance* AnimInstance = Cast<USKAnimInstance>(OwnerCharacter->GetMesh()->GetAnimInstance());
    return AnimInstance && AnimInstance->IsActorYawOwnedByRootMotion();
}

void USKCameraManagerComponent::ApplyActorYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    ApplyActorYaw(TargetYaw, InterpSpeed, DeltaTime);
}

void USKCameraManagerComponent::ApplyControllerYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    ApplyControllerYaw(TargetYaw, InterpSpeed, DeltaTime);
}

void USKCameraManagerComponent::ApplyPendingLookInputForScript()
{
    ApplyPendingLookInput();
}

void USKCameraManagerComponent::ClearPendingLookInputForScript()
{
    PendingLookInput = FVector2D::ZeroVector;
}

float USKCameraManagerComponent::GetSprintActorInterpSpeed() const
{
    return SprintActorInterpSpeed;
}

float USKCameraManagerComponent::GetSprintCameraYawInterpSpeed() const
{
    return SprintCameraYawInterpSpeed;
}

float USKCameraManagerComponent::GetLockOnActorInterpSpeed() const
{
    return LockOnActorInterpSpeed;
}

float USKCameraManagerComponent::GetLockOnCameraYawInterpSpeed() const
{
    return LockOnCameraYawInterpSpeed;
}

float USKCameraManagerComponent::GetMinMoveInputForFacing() const
{
    return MinMoveInputForFacing;
}

void USKCameraManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    RefreshCachedComponents();
}

void USKCameraManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    RefreshCachedComponents();
    if (!OwnerCharacter)
    {
        PendingLookInput = FVector2D::ZeroVector;
        return;
    }

    if (TryCallLuaCameraTick(DeltaTime))
    {
        PendingLookInput = FVector2D::ZeroVector;
        return;
    }

    ValidateLockTarget();
    CameraMode = ResolveCameraMode();
    UpdateMovementRotationSettings();
    UpdateMoveDirectionAngle();

    if (CameraMode == ESKCameraMode::SprintAlign)
    {
        UpdateSprintAlignMode(DeltaTime);
    }
    else if (CameraMode == ESKCameraMode::LockOn)
    {
        UpdateLockOnMode(DeltaTime);
    }
    else
    {
        UpdateFreeMode(DeltaTime);
    }

    PendingLookInput = FVector2D::ZeroVector;
}

bool USKCameraManagerComponent::TryCallLuaCameraTick(float DeltaTime)
{
    const FString ModuleName = ResolveLuaCameraModuleName();
    if (!bUseLuaCameraLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKCameraLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
    const bool bHandled = ReadSKCameraLuaHandled(FunctionReturnValues, ModuleName);
    FunctionReturnValues.Pop();
    return bHandled;
}

FString USKCameraManagerComponent::ResolveLuaCameraModuleName() const
{
    if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USKCameraManagerComponent*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaCameraModuleName;
}

ESKCameraMode USKCameraManagerComponent::ResolveCameraModeByName(FName ModeName) const
{
    const FString NormalizedName = ModeName.ToString().ToLower();
    if (NormalizedName == TEXT("lockon")) return ESKCameraMode::LockOn;
    if (NormalizedName == TEXT("sprintalign") || NormalizedName == TEXT("sprint")) return ESKCameraMode::SprintAlign;
    return ESKCameraMode::Free;
}

void USKCameraManagerComponent::RefreshCachedComponents()
{
    if (!OwnerCharacter)
    {
        OwnerCharacter = Cast<ACharacter>(GetOwner());
    }
    if (!OwnerCharacter) return;

    if (!InputManager)
    {
        InputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
    }
    if (!MovementComponent)
    {
        MovementComponent = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
    }
}

void USKCameraManagerComponent::ValidateLockTarget()
{
    if (!LockTarget) return;

    if (!IsValid(LockTarget))
    {
        ClearLockTarget();
        return;
    }

    if (!OwnerCharacter) return;

    const float BreakDistance = MaxLockOnRange * LockOnBreakDistanceMultiplier;
    const float BreakDistanceSq = FMath::Square(FMath::Max(1.f, BreakDistance));
    const float TargetDistanceSq = FVector::DistSquared(OwnerCharacter->GetActorLocation(), LockTarget->GetActorLocation());
    if (TargetDistanceSq > BreakDistanceSq)
    {
        ClearLockTarget();
    }
}

ESKCameraMode USKCameraManagerComponent::ResolveCameraMode() const
{
    if (MovementComponent && MovementComponent->CurrentMovementTier == ESKMovementTier::Sprint)
    {
        return ESKCameraMode::SprintAlign;
    }

    if (IsLockedOn())
    {
        return ESKCameraMode::LockOn;
    }

    return ESKCameraMode::Free;
}

void USKCameraManagerComponent::UpdateMovementRotationSettings()
{
    if (!OwnerCharacter) return;

    UCharacterMovementComponent* CharacterMovement = OwnerCharacter->GetCharacterMovement();
    if (!CharacterMovement) return;

    if (CameraMode == ESKCameraMode::LockOn)
    {
        CharacterMovement->bOrientRotationToMovement = false;
        CharacterMovement->bUseControllerDesiredRotation = false;
    }
    else if (CameraMode == ESKCameraMode::SprintAlign)
    {
        CharacterMovement->bOrientRotationToMovement = false;
        CharacterMovement->bUseControllerDesiredRotation = false;
    }
    else
    {
        CharacterMovement->bOrientRotationToMovement = false;
        CharacterMovement->bUseControllerDesiredRotation = false;
    }
}

void USKCameraManagerComponent::UpdateMoveDirectionAngle()
{
    if (!OwnerCharacter)
    {
        MoveDirectionAngle = 0.f;
        return;
    }

    float TargetYaw = 0.f;
    if (GetDesiredMoveYaw(TargetYaw))
    {
        MoveDirectionAngle = FMath::FindDeltaAngleDegrees(OwnerCharacter->GetActorRotation().Yaw, TargetYaw);
        return;
    }

    const FVector Velocity = OwnerCharacter->GetVelocity();
    if (!Velocity.IsNearlyZero())
    {
        const float VelocityYaw = Velocity.Rotation().Yaw;
        MoveDirectionAngle = FMath::FindDeltaAngleDegrees(OwnerCharacter->GetActorRotation().Yaw, VelocityYaw);
        return;
    }

    MoveDirectionAngle = 0.f;
}

void USKCameraManagerComponent::UpdateFreeMode(float DeltaTime)
{
    ApplyPendingLookInput();
}

void USKCameraManagerComponent::UpdateSprintAlignMode(float DeltaTime)
{
    float TargetYaw = 0.f;
    if (!GetDesiredMoveYaw(TargetYaw))
    {
        TargetYaw = OwnerCharacter->GetActorRotation().Yaw;
    }

    if (!IsActorYawOwnedByRootMotion())
    {
        ApplyActorYaw(TargetYaw, SprintActorInterpSpeed, DeltaTime);
    }

    float CameraTargetYaw = 0.f;
    if (IsLockedOn() && GetLockTargetYaw(CameraTargetYaw))
    {
        ApplyControllerYaw(CameraTargetYaw, LockOnCameraYawInterpSpeed, DeltaTime);
        return;
    }

    ApplyPendingLookInput();
}

void USKCameraManagerComponent::UpdateLockOnMode(float DeltaTime)
{
    float TargetYaw = 0.f;
    if (!GetLockTargetYaw(TargetYaw))
    {
        ApplyPendingLookInput();
        return;
    }

    if (!IsActorYawOwnedByRootMotion())
    {
        ApplyActorYaw(TargetYaw, LockOnActorInterpSpeed, DeltaTime);
    }
    ApplyControllerYaw(TargetYaw, LockOnCameraYawInterpSpeed, DeltaTime);
}

bool USKCameraManagerComponent::GetDesiredMoveYaw(float& OutYaw) const
{
    if (!OwnerCharacter || !InputManager) return false;

    const FVector2D MoveIntent = InputManager->GetMoveIntent();
    if (MoveIntent.Size() < MinMoveInputForFacing) return false;

    const AController* Controller = OwnerCharacter->GetController();
    if (!Controller) return false;

    const FRotator ControlYawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
    const FVector Forward = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);
    const FVector DesiredDirection = (Forward * MoveIntent.Y + Right * MoveIntent.X).GetSafeNormal();
    if (DesiredDirection.IsNearlyZero()) return false;

    OutYaw = DesiredDirection.Rotation().Yaw;
    return true;
}

bool USKCameraManagerComponent::GetLockTargetYaw(float& OutYaw) const
{
    if (!OwnerCharacter || !IsLockedOn()) return false;

    const FVector ToTarget = LockTarget->GetActorLocation() - OwnerCharacter->GetActorLocation();
    const FVector FlatDirection(ToTarget.X, ToTarget.Y, 0.f);
    if (FlatDirection.IsNearlyZero()) return false;

    OutYaw = FlatDirection.Rotation().Yaw;
    return true;
}

void USKCameraManagerComponent::ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    if (!OwnerCharacter) return;

    const FRotator CurrentRotation = OwnerCharacter->GetActorRotation();
    const float NewYaw = InterpSKYawShortest(CurrentRotation.Yaw, TargetYaw, DeltaTime, InterpSpeed);
    OwnerCharacter->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

void USKCameraManagerComponent::ApplyControllerYaw(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    if (!OwnerCharacter) return;

    AController* Controller = OwnerCharacter->GetController();
    if (!Controller) return;

    const FRotator CurrentRotation = Controller->GetControlRotation();
    const float NewYaw = InterpSKYawShortest(CurrentRotation.Yaw, TargetYaw, DeltaTime, InterpSpeed);
    Controller->SetControlRotation(FRotator(CurrentRotation.Pitch, NewYaw, CurrentRotation.Roll));
}

void USKCameraManagerComponent::ApplyPendingLookInput()
{
    if (!OwnerCharacter || PendingLookInput.IsNearlyZero()) return;

    OwnerCharacter->AddControllerYawInput(PendingLookInput.X);
    OwnerCharacter->AddControllerPitchInput(PendingLookInput.Y);
}
