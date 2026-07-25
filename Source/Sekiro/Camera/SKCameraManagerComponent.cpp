#include "Camera/SKCameraManagerComponent.h"

#include "Movement/SKMovementComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"

namespace
{
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

bool USKCameraManagerComponent::IsMovementTierSprint() const
{
    return MovementComponent && MovementComponent->CurrentMovementTier == ESKMovementTier::Sprint;
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

float USKCameraManagerComponent::GetSprintCameraYawInterpSpeed() const
{
    return SprintCameraYawInterpSpeed;
}

float USKCameraManagerComponent::GetLockOnCameraYawInterpSpeed() const
{
    return LockOnCameraYawInterpSpeed;
}

/**
 * 缓存相机运行时依赖，并为原生 UnLua 组件补发一次标准 ReceiveBeginPlay 生命周期。
 * 蓝图生成类和非原生类已由 UActorComponent::BeginPlay 派发，本函数不会重复调用；
 * 纯原生组件则在依赖缓存完成后派发，使 Lua 可安全调用反射接口配置相机。
 * 本函数只在游戏线程执行，不直接调用 Lua Initialize。
 */
void USKCameraManagerComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();

    RefreshCachedComponents();

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
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

    if (bUseLuaCameraLogic) UpdateCameraLogic(DeltaTime);
    PendingLookInput = FVector2D::ZeroVector;
}

/**
 * 提供未被 Blueprint 或 UnLua 覆盖时的默认相机策略入口。
 * TickComponent 在游戏线程且角色缓存有效时调用此反射事件；默认实现不消费视角输入，
 * 以便关闭 Lua 策略或缺少脚本绑定时仍维持原生组件状态，并由 TickComponent 统一清理本帧输入。
 *
 * @param DeltaTime 当前帧时长，单位为秒。
 */
void USKCameraManagerComponent::UpdateCameraLogic_Implementation(float DeltaTime)
{
    static_cast<void>(DeltaTime);
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

    if (!MovementComponent)
    {
        MovementComponent = Cast<USKMovementComponent>(OwnerCharacter->GetCharacterMovement());
        if (MovementComponent)
        {
            // 相机读取 Movement Lua 已确定的档位和 ActorYaw，固定 Input → Movement → Camera 顺序。
            AddTickPrerequisiteComponent(MovementComponent);
        }
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

bool USKCameraManagerComponent::GetLockTargetYaw(float& OutYaw) const
{
    if (!OwnerCharacter || !IsLockedOn()) return false;

    const FVector ToTarget = LockTarget->GetActorLocation() - OwnerCharacter->GetActorLocation();
    const FVector FlatDirection(ToTarget.X, ToTarget.Y, 0.f);
    if (FlatDirection.IsNearlyZero()) return false;

    OutYaw = FlatDirection.Rotation().Yaw;
    return true;
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
