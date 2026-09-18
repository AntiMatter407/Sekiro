#include "Movement/SKMovementComponent.h"

#include "Camera/SKCameraManagerComponent.h"
#include "Combat/SKCombatComponent.h"
#include "Input/SKInputManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Movement/SKMotionMatchingTrajectoryComponent.h"

namespace
{
    static float InterpSKMovementYawShortest(float CurrentYaw, float TargetYaw, float DeltaTime, float InterpSpeed)
    {
        if (InterpSpeed <= 0.f) return FMath::UnwindDegrees(TargetYaw);
        if (DeltaTime <= 0.f) return FMath::UnwindDegrees(CurrentYaw);

        const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
        if (FMath::Square(DeltaYaw) < UE_SMALL_NUMBER) return FMath::UnwindDegrees(TargetYaw);

        const float Alpha = FMath::Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);
        return FMath::UnwindDegrees(CurrentYaw + DeltaYaw * Alpha);
    }
}

/**
 * 初始化原生 CharacterMovement 承载层和 Lua Movement 默认状态。
 * 构造阶段不访问世界或角色；速度策略、朝向模式和插值参数由 Lua 在运行时发布。
 */
USKMovementComponent::USKMovementComponent()
{
    bHasDesiredMoveYawSnapshot = false;
    bHasLockOnLocomotionSnapshot = false;
    bRootMotionDirectionWarpingEnabled = false;
    MaxWalkSpeed = RunSpeed;
}

/**
 * 接收输入层已经决定的旧移动档位，并同步发布独立的 Motion Matching Gait/Stance 请求。
 * 只能在游戏线程、输入策略求值时调用；本函数不提交轨迹 Intent，也不改变角色蹲伏或移动。
 * Idle 只表示当前没有移动输入，不覆盖最后请求的动画速度族和姿态；Crouch 只改变姿态，
 * 其余站立档位同时选择对应 Gait 并明确恢复 Standing。
 *
 * @param NewMovementTier 输入层本次选择的兼容移动档位。
 */
void USKMovementComponent::ApplyInputMovementTier(ESKMovementTier NewMovementTier)
{
    CurrentMovementTier = NewMovementTier;
    switch (NewMovementTier)
    {
    case ESKMovementTier::Walk:
        RequestedMotionMatchingGait = ESKMotionMatchingGait::Walk;
        RequestedMotionMatchingStance = ESKMotionMatchingStance::Standing;
        break;
    case ESKMovementTier::Run:
        RequestedMotionMatchingGait = ESKMotionMatchingGait::Run;
        RequestedMotionMatchingStance = ESKMotionMatchingStance::Standing;
        break;
    case ESKMovementTier::Sprint:
        RequestedMotionMatchingGait = ESKMotionMatchingGait::Sprint;
        RequestedMotionMatchingStance = ESKMotionMatchingStance::Standing;
        break;
    case ESKMovementTier::Crouch:
        RequestedMotionMatchingStance = ESKMotionMatchingStance::Crouching;
        break;
    case ESKMovementTier::Idle:
    default:
        break;
    }
}

/**
 * 保存 Lua 使用的 Walk、Run、Sprint 速度配置，供动画采集和调试读取。
 * 只能在游戏线程调用；本函数不选择当前档位，也不直接改变本帧速度上限。
 *
 * @param NewWalkSpeed 步行目标速度，单位 cm/s，负值会限制为 0。
 * @param NewRunSpeed 跑步目标速度，单位 cm/s，负值会限制为 0。
 * @param NewSprintSpeed 冲刺目标速度，单位 cm/s，负值会限制为 0。
 */
void USKMovementComponent::SetMovementSpeedProfileForScript(
    float NewWalkSpeed,
    float NewRunSpeed,
    float NewSprintSpeed)
{
    WalkSpeed = FMath::Max(0.f, NewWalkSpeed);
    RunSpeed = FMath::Max(0.f, NewRunSpeed);
    SprintSpeed = FMath::Max(0.f, NewSprintSpeed);
}

/**
 * 设置 CharacterMovement 本帧及后续帧使用的地面最大速度。
 * 只能在游戏线程、原生移动求值前调用；不影响蹲伏专用 MaxWalkSpeedCrouched。
 *
 * @param NewMaxWalkSpeed 地面最大速度，单位 cm/s，负值会限制为 0。
 */
void USKMovementComponent::SetMaxWalkSpeedForScript(float NewMaxWalkSpeed)
{
    MaxWalkSpeed = FMath::Max(0.f, NewMaxWalkSpeed);
}

/** 查询当前输入 Lua 发布的移动档位是否为 Sprint；只读，不修改移动状态。 */
bool USKMovementComponent::IsMovementTierSprint() const
{
    return CurrentMovementTier == ESKMovementTier::Sprint;
}

/**
 * 在游戏线程申请一个上身姿势令牌，或申请唯一的 FullBody/Traversal 根运动覆盖令牌。
 * Locomotion 是无令牌默认所有者，不能显式申请；UpperBody 与排他覆盖通道互不抢占，且不会关闭
 * Locomotion RootMotion。相同申请者对同一通道重复申请幂等返回现有令牌，不延长 Requester 生命周期。
 *
 * @param Requester 发起动作的 UObject 身份，必须有效；通常为 GA 实例或拥有动作生命周期的组件。
 * @param RequestedOwner 请求通道；只接受 UpperBody、FullBody 或 Traversal。
 * @return 带明确结果码、当前或新令牌及操作后权威快照的值副本。
 */
FSKRootMotionOwnershipResult USKMovementComponent::AcquireRootMotionOwnership(
    UObject* Requester,
    ESKRootMotionOwnerType RequestedOwner)
{
    if (!IsInGameThread())
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::WrongThread,
            FSKRootMotionOwnerToken());
    ReconcileRootMotionOwnership();
    if (!IsValid(Requester))
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::InvalidRequester,
            FSKRootMotionOwnerToken());
    if (RequestedOwner == ESKRootMotionOwnerType::Locomotion
        || static_cast<uint8>(RequestedOwner) > static_cast<uint8>(ESKRootMotionOwnerType::Traversal))
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::InvalidOwnerType,
            FSKRootMotionOwnerToken());

    FSKRootMotionOwnerToken* ActiveToken = RequestedOwner == ESKRootMotionOwnerType::UpperBody
        ? &ActiveUpperBodyToken
        : &ActiveRootMotionOverrideToken;
    if (ActiveToken->Serial > 0)
    {
        const bool bAlreadyOwned = ActiveToken->Requester.Get() == Requester
            && ActiveToken->OwnerType == RequestedOwner;
        if (bAlreadyOwned)
            return MakeRootMotionOwnershipResult(
                ESKRootMotionOwnershipResultCode::AlreadyOwned,
                *ActiveToken);
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::Busy,
            FSKRootMotionOwnerToken());
    }
    if (RootMotionOwnerTokenSerial == MAX_int64
        || RootMotionOwnershipSnapshot.Revision == MAX_int64)
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::SerialExhausted,
            FSKRootMotionOwnerToken());

    FSKRootMotionOwnerToken NewToken;
    NewToken.Authority = this;
    NewToken.Requester = Requester;
    NewToken.Serial = ++RootMotionOwnerTokenSerial;
    NewToken.OwnerType = RequestedOwner;
    *ActiveToken = NewToken;
    RefreshRootMotionOwnershipSnapshot();
    return MakeRootMotionOwnershipResult(
        ESKRootMotionOwnershipResultCode::Acquired,
        NewToken);
}

/**
 * 在游戏线程释放仍匹配当前 Authority、Requester、Serial 与通道类型的所有权令牌。
 * 过期、跨角色或被新申请替换的令牌不会改变当前所有权；释放 UpperBody 不影响排他根运动通道，
 * 释放 FullBody/Traversal 后立即回退到隐式 Locomotion 所有者。
 *
 * @param Token 调用方此前取得的值令牌，不保留引用；弱 Requester 已销毁时按过期处理。
 * @return Released 表示状态已改变；其他结果附带操作后的实际快照。
 */
FSKRootMotionOwnershipResult USKMovementComponent::ReleaseRootMotionOwnership(
    const FSKRootMotionOwnerToken& Token)
{
    if (!IsInGameThread())
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::WrongThread,
            Token);
    ReconcileRootMotionOwnership();
    if (Token.Authority.Get() != this || Token.Serial <= 0
        || Token.OwnerType == ESKRootMotionOwnerType::Locomotion
        || static_cast<uint8>(Token.OwnerType) > static_cast<uint8>(ESKRootMotionOwnerType::Traversal))
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::StaleToken,
            Token);

    FSKRootMotionOwnerToken* ActiveToken = Token.OwnerType == ESKRootMotionOwnerType::UpperBody
        ? &ActiveUpperBodyToken
        : &ActiveRootMotionOverrideToken;
    const bool bMatchesActive = ActiveToken->Authority.Get() == this
        && ActiveToken->Requester == Token.Requester
        && ActiveToken->Serial == Token.Serial
        && ActiveToken->OwnerType == Token.OwnerType;
    if (!bMatchesActive)
        return MakeRootMotionOwnershipResult(
            ESKRootMotionOwnershipResultCode::StaleToken,
            Token);

    *ActiveToken = FSKRootMotionOwnerToken();
    RefreshRootMotionOwnershipSnapshot();
    return MakeRootMotionOwnershipResult(
        ESKRootMotionOwnershipResultCode::Released,
        Token);
}

/** 返回当前所有权纯值快照；非游戏线程失败关闭为默认 Locomotion 快照，不访问申请者对象。 */
FSKRootMotionOwnershipSnapshot USKMovementComponent::GetRootMotionOwnershipSnapshot() const
{
    return IsInGameThread()
        ? RootMotionOwnershipSnapshot
        : FSKRootMotionOwnershipSnapshot();
}

/**
 * 在游戏线程校验令牌是否仍精确持有对应通道；只比较弱身份与序号，不更新状态。
 *
 * @param Token 待验证的值令牌，可来自 Blueprint、Lua 或 GA 保存字段。
 * @return Authority、Requester、Serial 和 OwnerType 均匹配当前活动通道时返回 true。
 */
bool USKMovementComponent::IsRootMotionOwnerTokenValid(
    const FSKRootMotionOwnerToken& Token) const
{
    if (!IsInGameThread() || Token.Authority.Get() != this || !Token.Requester.IsValid()
        || Token.Serial <= 0) return false;
    const FSKRootMotionOwnerToken& ActiveToken = Token.OwnerType == ESKRootMotionOwnerType::UpperBody
        ? ActiveUpperBodyToken
        : ActiveRootMotionOverrideToken;
    return Token.OwnerType != ESKRootMotionOwnerType::Locomotion
        && static_cast<uint8>(Token.OwnerType) <= static_cast<uint8>(ESKRootMotionOwnerType::Traversal)
        && ActiveToken.Authority.Get() == this
        && ActiveToken.Requester == Token.Requester
        && ActiveToken.Serial == Token.Serial
        && ActiveToken.OwnerType == Token.OwnerType;
}

/** 设置 Lua Movement 是否接管业务策略；关闭后原生 CharacterMovement 仍继续执行物理和 Root Motion。 */
void USKMovementComponent::SetUseLuaMovementLogic(bool bNewUseLuaMovementLogic)
{
    bUseLuaMovementLogic = bNewUseLuaMovementLogic;
}

/** 查询 Lua Movement 策略开关；只读，可在游戏线程调用。 */
bool USKMovementComponent::IsUsingLuaMovementLogic() const
{
    return bUseLuaMovementLogic;
}

/** 设置 UnLua require 使用的 Movement 模块名；调用方负责保证模块存在。 */
void USKMovementComponent::SetLuaMovementModuleName(const FString& ModuleName)
{
    LuaMovementModuleName = ModuleName;
}

/** 返回当前 Movement Lua 模块名；字符串按值返回。 */
FString USKMovementComponent::GetLuaMovementModuleName() const
{
    return LuaMovementModuleName;
}

/** 向 UnLua 报告 Movement 默认模块名；不创建 Lua 环境。 */
FString USKMovementComponent::GetModuleName_Implementation() const
{
    return LuaMovementModuleName;
}

/** 刷新 Lua 访问所需的角色、输入和相机引用；只在游戏线程调用。 */
void USKMovementComponent::RefreshCachedMovementComponents()
{
    RefreshCachedComponents();
}

/** 查询 Movement 是否已缓存有效角色；只读。 */
bool USKMovementComponent::HasOwnerCharacter() const
{
    return OwnerCharacter != nullptr;
}

/** 返回屏幕空间横向移动输入，输入组件不可用时返回 0。 */
float USKMovementComponent::GetMoveInputX() const
{
    return InputManager ? InputManager->GetMoveIntent().X : 0.f;
}

/** 返回屏幕空间纵向移动输入，输入组件不可用时返回 0。 */
float USKMovementComponent::GetMoveInputY() const
{
    return InputManager ? InputManager->GetMoveIntent().Y : 0.f;
}

/** 返回保留模拟摇杆幅度的移动输入强度，输入组件不可用时返回 0。 */
float USKMovementComponent::GetMoveInputAmount() const
{
    return InputManager ? InputManager->GetMoveInputAmount() : 0.f;
}

/** 查询相机系统是否持有有效锁定目标；相机组件不可用时返回 false。 */
bool USKMovementComponent::IsLockedOn() const
{
    return CameraManager && CameraManager->IsLockedOn();
}

/** 查询锁定目标是否能提供平面 Yaw；只读，不改变锁定状态。 */
bool USKMovementComponent::HasLockTargetYaw() const
{
    return CameraManager && CameraManager->HasLockTargetYaw();
}

/**
 * 返回锁定目标世界 Yaw，相机或目标不可用时返回调用方回退值。
 *
 * @param FallbackYaw 无有效锁定目标时返回的世界 Yaw，单位为度。
 * @return 有目标时为目标方向 Yaw，否则为 FallbackYaw。
 */
float USKMovementComponent::GetLockTargetYawOrFallback(float FallbackYaw) const
{
    return CameraManager ? CameraManager->GetLockTargetYawOrFallback(FallbackYaw) : FallbackYaw;
}

/** 返回角色当前世界 Yaw，角色不可用时返回 0。 */
float USKMovementComponent::GetOwnerYaw() const
{
    return OwnerCharacter ? OwnerCharacter->GetActorRotation().Yaw : 0.f;
}

/**
 * 返回控制器世界 Yaw，角色或控制器不可用时返回调用方回退值。
 *
 * @param FallbackYaw 无控制器时的回退 Yaw，单位为度。
 * @return 控制器有效时为控制旋转 Yaw，否则为 FallbackYaw。
 */
float USKMovementComponent::GetControllerYawOrFallback(float FallbackYaw) const
{
    if (!OwnerCharacter) return FallbackYaw;

    const AController* Controller = OwnerCharacter->GetController();
    return Controller ? Controller->GetControlRotation().Yaw : FallbackYaw;
}

/**
 * 计算 FromYaw 到 ToYaw 的最短有符号角，结果范围为 -180..180。
 *
 * @param FromYaw 起始世界 Yaw，单位为度。
 * @param ToYaw 目标世界 Yaw，单位为度。
 * @return 正数表示右转，负数表示左转的最短角度。
 */
float USKMovementComponent::NormalizeDeltaYaw(float FromYaw, float ToYaw) const
{
    return FMath::FindDeltaAngleDegrees(FromYaw, ToYaw);
}

/**
 * 设置 UE CharacterMovement 内置自动旋转开关，供 Classic Lua 明确分配 ActorYaw 所有权。
 * 装配 Motion Matching 能力标记时始终强制关闭两项，调用方不能越过正式 RootMotion 所有权边界。
 * 只能在游戏线程调用；不直接旋转角色。
 *
 * @param bNewOrientRotationToMovement 是否由加速度方向驱动内置旋转。
 * @param bNewUseControllerDesiredRotation 是否由控制器期望旋转驱动内置旋转。
 */
void USKMovementComponent::SetMovementRotationSettingsForScript(
    bool bNewOrientRotationToMovement,
    bool bNewUseControllerDesiredRotation)
{
    if (UsesMotionMatchingLocomotion())
    {
        bOrientRotationToMovement = false;
        bUseControllerDesiredRotation = false;
        return;
    }

    bOrientRotationToMovement = bNewOrientRotationToMovement;
    bUseControllerDesiredRotation = bNewUseControllerDesiredRotation;
}

/**
 * 按最短角路径插值并写入角色世界 Yaw。
 * 只能在游戏线程且应在 Super::TickComponent 前调用，确保本帧 Root Motion 平移使用更新后的朝向。
 *
 * @param TargetYaw 目标世界 Yaw，单位为度。
 * @param InterpSpeed 指数近似插值速度；小于等于 0 时立即对齐。
 * @param DeltaTime 当前帧时长，单位秒；非正值保持当前角度。
 */
void USKMovementComponent::ApplyActorYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    ApplyActorYaw(TargetYaw, InterpSpeed, DeltaTime);
}

/**
 * 按固定最大角速度沿最短路径写入角色世界 Yaw，供动画曲线表达确定的每秒转角。
 * 只能在游戏线程且应在 Super::TickComponent 前调用，使同帧 Root Motion 平移沿更新后的角色朝向转换；
 * 本函数不修改控制器、相机、移动输入或 Root Motion 曲线。
 *
 * @param TargetYaw 目标世界 Yaw，单位为度。
 * @param MaxDegreesPerSecond 最大转向速度，单位为度/秒；非正值不旋转。
 * @param DeltaTime 当前帧时长，单位为秒；非正值不旋转。
 */
void USKMovementComponent::ApplyActorYawRateForScript(
    float TargetYaw,
    float MaxDegreesPerSecond,
    float DeltaTime)
{
    if (!OwnerCharacter || MaxDegreesPerSecond <= 0.f || DeltaTime <= 0.f) return;

    const FRotator CurrentRotation = OwnerCharacter->GetActorRotation();
    const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetYaw);
    const float MaxDeltaYaw = MaxDegreesPerSecond * DeltaTime;
    const float NewYaw = FMath::UnwindDegrees(
        CurrentRotation.Yaw + FMath::Clamp(DeltaYaw, -MaxDeltaYaw, MaxDeltaYaw));
    OwnerCharacter->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

/**
 * 保存 Lua 在角色旋转前计算的移动输入方向快照，供同帧 AnimInstance 选择起步/转身动画。
 * 只能在游戏线程调用；不改变角色旋转，也不执行动画选择。
 *
 * @param bHasDesiredMoveYaw 当前是否存在超过输入阈值的有效移动方向。
 * @param NewDesiredMoveYaw 输入相对控制器得到的世界 Yaw，单位为度。
 * @param NewMoveDirectionAngleBeforeRotation 角色旋转前到移动目标的最短角，单位为度。
 */
void USKMovementComponent::SetMoveFacingSnapshotForScript(
    bool bHasDesiredMoveYaw,
    float NewDesiredMoveYaw,
    float NewMoveDirectionAngleBeforeRotation)
{
    bHasDesiredMoveYawSnapshot = bHasDesiredMoveYaw;
    DesiredMoveYawSnapshot = FMath::UnwindDegrees(NewDesiredMoveYaw);
    MoveDirectionAngleBeforeRotationSnapshot = FMath::UnwindDegrees(NewMoveDirectionAngleBeforeRotation);
}

/** 清空 Lua 移动方向快照；保留上次数值但标记为无效，避免无输入帧被动画误用。 */
void USKMovementComponent::ClearMoveFacingSnapshotForScript()
{
    bHasDesiredMoveYawSnapshot = false;
}

/**
 * 保存锁定普通移动的四方向素材和动画朝向快照。
 * Lua 已按锁定目标相对输入选择主轴；本函数不旋转角色或 Root Motion，
 * 只为同帧 AnimInstance 提供与 Movement 完全一致的素材选择。
 * 只能在游戏线程、CharacterMovement 求值前调用。
 *
 * @param bEnabled 是否存在有效锁定普通移动快照。
 * @param CardinalDirection Direction.lua 使用的四方向枚举值。
 */
void USKMovementComponent::SetLockOnLocomotionSnapshotForScript(
    bool bEnabled,
    int32 CardinalDirection)
{
    bHasLockOnLocomotionSnapshot = bEnabled;
    LockOnCardinalDirectionSnapshot = CardinalDirection;
}

/**
 * 设置最终动画 Root Motion 水平平移使用的世界方向。
 * 只能在游戏线程、CharacterMovement 求值前调用；接口仅保存通用方向数据，
 * 不判断锁定、步态或动画状态；Motion Matching 能力标记或 Classic 配置门禁会拒绝启用，
 * 接口仍不改变 Root Motion 长度、垂直分量和旋转。
 *
 * @param bEnabled 是否在本帧 CharacterMovement 转换 Root Motion 时重定向水平平移。
 * @param TargetWorldYaw 目标水平移动方向的世界 Yaw，单位为度。
 */
void USKMovementComponent::SetRootMotionDirectionWarpingForScript(bool bEnabled, float TargetWorldYaw)
{
    bRootMotionDirectionWarpingEnabled = !UsesMotionMatchingLocomotion()
        && bEnableClassicRootMotionDirectionWarping && bEnabled;
    RootMotionDirectionWarpingWorldYaw = FMath::UnwindDegrees(TargetWorldYaw);
}

/**
 * 保存项目 Lua 发布的 RootMotion Steering 速率限制，供唯一 PostConvert Coordinator 消费。
 * 本函数只写配置，不立即旋转 Actor 或 RootMotion；所有输入限制为非负有限值，
 * 每秒速率最大 3600 度，单帧上限最大 180 度，非法值按 0 关闭对应通道。
 *
 * @param FreeDegreesPerSecond Free 模式根旋转与水平平移方向的最大修正速度，单位度/秒。
 * @param LockedFacingDegreesPerSecond Locked 模式根旋转朝向目标的最大修正速度，单位度/秒。
 * @param LockedTranslationDegreesPerSecond Locked 模式水平平移朝移动意图的最大修正速度，单位度/秒。
 * @param MaxDegreesPerFrame 任一通道单帧允许追加的最大绝对角度，单位度。
 */
void USKMovementComponent::SetRootMotionSteeringSettingsForScript(
    float FreeDegreesPerSecond,
    float LockedFacingDegreesPerSecond,
    float LockedTranslationDegreesPerSecond,
    float MaxDegreesPerFrame)
{
    FreeRootMotionSteeringRate = FMath::IsFinite(FreeDegreesPerSecond)
        ? FMath::Clamp(FreeDegreesPerSecond, 0.f, 3600.f) : 0.f;
    LockedRootMotionFacingSteeringRate = FMath::IsFinite(LockedFacingDegreesPerSecond)
        ? FMath::Clamp(LockedFacingDegreesPerSecond, 0.f, 3600.f) : 0.f;
    LockedRootMotionTranslationSteeringRate = FMath::IsFinite(LockedTranslationDegreesPerSecond)
        ? FMath::Clamp(LockedTranslationDegreesPerSecond, 0.f, 3600.f) : 0.f;
    MaxRootMotionSteeringDegreesPerFrame = FMath::IsFinite(MaxDegreesPerFrame)
        ? FMath::Clamp(MaxDegreesPerFrame, 0.f, 180.f) : 0.f;
}

/**
 * 查询所属角色的战斗组件当前是否正在播放全身动作。
 *
 * Lua 移动策略用它限制锁定移动的 Root Motion 方向修正范围，避免玩家仍按住移动输入时，
 * 攻击、受击等全身动画的位移被误重定向。该查询只访问游戏线程上的组件状态。
 *
 * @return 找到战斗组件且全身战斗动作正在生效时返回 true，否则返回 false。
 */
bool USKMovementComponent::IsOwnerCombatFullBodyActionActiveForScript() const
{
    return CombatComponent && CombatComponent->IsCombatFullBodyActionActive();
}

/**
 * 查询缓存战斗组件是否处于轻攻击或重攻击状态。
 * 该接口只读取游戏线程状态，不判断动画曲线、输入意图或是否允许转向。
 *
 * @return 当前状态为 LightAttack 或 HeavyAttack 时返回 true；组件无效或其他状态返回 false。
 */
bool USKMovementComponent::IsOwnerAttackActionActiveForScript() const
{
    if (!CombatComponent) return false;

    const ESKCombatActionState ActionState = CombatComponent->GetCombatActionState();
    return ActionState == ESKCombatActionState::LightAttack
        || ActionState == ESKCombatActionState::HeavyAttack;
}

/**
 * 采样当前战斗动作源 UAnimSequence 在当前播放位置的语义曲线。
 * 本函数只提供通用跨组件读取桥梁；曲线名称和数值含义由 Lua 决定。
 * 只能在游戏线程调用，不推进动画时间，也不缓存返回值。
 *
 * @param CurveName Skeleton 中登记的稳定曲线名；None 视为无曲线。
 * @return 战斗组件和曲线有效时返回当前值，否则返回 0。
 */
float USKMovementComponent::SampleOwnerCombatSequenceCurveForScript(FName CurveName) const
{
    return CombatComponent ? CombatComponent->SampleActiveSequenceCurve(CurveName) : 0.f;
}

/** 返回所属角色当前世界速度的水平长度；只能在游戏线程读取，角色无效时返回 0。 */
float USKMovementComponent::GetHorizontalSpeedForScript() const
{
    return OwnerCharacter ? OwnerCharacter->GetVelocity().Size2D() : 0.f;
}

/** 返回最近一次动画 RootMotion 在碰撞求值前请求的世界水平位移副本，单位厘米。 */
FVector USKMovementComponent::GetLastRootMotionRequestedTranslationWS() const
{
    return LastRootMotionRequestedTranslationWS;
}

/** 返回与最近一次动画 RootMotion 求值对应的 Actor 实际世界水平位移副本，单位厘米。 */
FVector USKMovementComponent::GetLastRootMotionActualTranslationWS() const
{
    return LastRootMotionActualTranslationWS;
}

/** 返回最近一次 RootMotion 请求被碰撞、地面或 MovementMode 裁剪掉的世界水平位移，单位厘米。 */
FVector USKMovementComponent::GetLastRootMotionCollisionClippedTranslationWS() const
{
    return LastRootMotionCollisionClippedTranslationWS;
}

/** 返回最近一次动画 RootMotion 水平请求与 Actor 实际位移是否存在可观测差异。 */
bool USKMovementComponent::WasLastRootMotionTranslationClipped() const
{
    return bLastRootMotionTranslationClipped;
}

/**
 * 返回最近一次已经完成 Movement 更新的 Motion Matching RootMotion 协调快照副本。
 * 当前 Tick 开头会重置写入中快照，但不会覆盖本值，因此 AnimInstance 能稳定读取上一轮结果。
 * 可在游戏线程用于姿势反馈和诊断采集；调用方不取得内部状态引用。
 *
 * @return 当前快照的值副本；bValid 为 false 表示本帧不属于正式协调路径。
 */
FSKRootMotionCoordinationSnapshot USKMovementComponent::GetRootMotionCoordinationSnapshot() const
{
    return CompletedRootMotionCoordinationSnapshot;
}

/**
 * 在游戏线程切换 Motion Matching PoseOnly 空中水平惯性交接状态。
 * 启用只表示后续物理步应保留 CMC 当前水平速度；函数不读取输入、不立即写 Velocity，也不移动角色。
 * 停用会让动画 RootMotion 从下一次移动求值起恢复正常覆盖规则。
 *
 * @param bActive true 表示 PoseOnly 空中阶段已由 PostSelection 正式接受；false 结束或撤销交接。
 */
void USKMovementComponent::SetPoseOnlyAirborneMomentumActive(bool bActive)
{
    if (!IsInGameThread()) return;
    bPoseOnlyAirborneMomentumActive = bActive;
}

/**
 * 判断当前角色是否装配正式 Motion Matching Locomotion 路径。
 * 本函数只读取缓存组件，不创建组件、不修改 ActorYaw，也不推断 AnimInstance 类型；
 * 由同一角色上的 USKMotionMatchingTrajectoryComponent 作为稳定能力标记。
 *
 * @return 能力标记组件仍有效时返回 true，否则返回 false 并保留 Classic 移动行为。
 */
bool USKMovementComponent::UsesMotionMatchingLocomotion() const
{
    return IsValid(MotionMatchingTrajectoryComponent);
}

/** 查询 Lua 本帧是否发布了有效移动目标 Yaw；供动画数据采集只读调用。 */
bool USKMovementComponent::HasDesiredMoveYawSnapshot() const
{
    return bHasDesiredMoveYawSnapshot;
}

/** 查询 Lua 本帧是否发布了有效锁定四方向移动快照；只读，供 AnimInstance 在游戏线程采集。 */
bool USKMovementComponent::HasLockOnLocomotionSnapshot() const
{
    return bHasLockOnLocomotionSnapshot;
}

/** 返回 Lua 发布的锁定四方向素材枚举值；调用方应先检查快照有效标记。 */
int32 USKMovementComponent::GetLockOnCardinalDirectionSnapshot() const
{
    return LockOnCardinalDirectionSnapshot;
}

/** 返回 Lua 最近发布的移动目标世界 Yaw；调用方应先检查快照有效标记。 */
float USKMovementComponent::GetDesiredMoveYawSnapshot() const
{
    return DesiredMoveYawSnapshot;
}

/** 返回 Lua 最近发布的角色转身前移动方向角；调用方应先检查快照有效标记。 */
float USKMovementComponent::GetMoveDirectionAngleBeforeRotationSnapshot() const
{
    return MoveDirectionAngleBeforeRotationSnapshot;
}

/**
 * 缓存运行时引用、建立 Input -> Movement 的 Tick 前置关系，并为原生 UnLua 组件补发一次
 * 标准 ReceiveBeginPlay 生命周期。蓝图生成类和非原生类仍由引擎派发，避免 Lua 初始化重复；
 * 纯原生组件在依赖关系建立后派发，使 Lua 可安全发布移动参数。
 * 本函数由 UE 在游戏线程调用，不执行业务移动策略，也不直接调用 Lua Initialize。
 */
void USKMovementComponent::BeginPlay()
{
    const bool bEngineDispatchesReceiveBeginPlay =
        GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
        || !GetClass()->HasAnyClassFlags(CLASS_Native);

    Super::BeginPlay();
    RefreshCachedComponents();
    if (InputManager) AddTickPrerequisiteComponent(InputManager);
    ProcessRootMotionPreConvertToWorld.BindUObject(
        this,
        &USKMovementComponent::CaptureAnimationRootMotionPreConvert);
    ProcessRootMotionPostConvertToWorld.BindUObject(
        this,
        &USKMovementComponent::CoordinateRootMotionPostConvert);

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

/**
 * 在原生 CharacterMovement 求值前先强制执行 Motion Matching 旋转所有权门禁，再调用 Lua Movement，
 * 最后交还 UE 处理物理、碰撞、Root Motion 和网络预测。
 * 由 UE 在游戏线程的 PrePhysics 阶段调用；Lua 返回 false 时只跳过脚本策略，不阻断原生移动。
 *
 * @param DeltaTime 当前移动更新步长，单位为秒。
 * @param TickType UE 当前 Tick 类型。
 * @param ThisTickFunction 当前组件 Tick 函数，可为空，由父类消费。
 */
void USKMovementComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    RefreshCachedComponents();
    if (bMotionMatchingActualStateRebasePending)
    {
        bMotionMatchingActualStateRebasePending = IsValid(MotionMatchingTrajectoryComponent)
            && !MotionMatchingTrajectoryComponent->RebaseMotionMatchingActualState();
    }
    if (UsesMotionMatchingLocomotion())
    {
        bOrientRotationToMovement = false;
        bUseControllerDesiredRotation = false;
        bRootMotionDirectionWarpingEnabled = false;
        BeginRootMotionCoordinationSample(DeltaTime);
        if (!IsFalling()) bPoseOnlyAirborneMomentumActive = false;
    }
    else
    {
        bPoseOnlyAirborneMomentumActive = false;
        RootMotionCoordinationSnapshot = FSKRootMotionCoordinationSnapshot();
        CompletedRootMotionCoordinationSnapshot = FSKRootMotionCoordinationSnapshot();
    }
    if (bUseLuaMovementLogic) UpdateMovementLogic(DeltaTime);
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

/**
 * 在 CharacterMovement 完成 RootMotion、碰撞和地面约束后发布本帧真实位移诊断。
 * 只能由 UE 在游戏线程的移动更新尾部调用；不修改 Actor Transform 或 Velocity。
 *
 * @param DeltaSeconds 本次移动求值时长，单位秒；非有限或非正值按无有效 RootMotion 请求处理。
 * @param OldLocation 本次移动前的世界位置，单位厘米。
 * @param OldVelocity 本次移动前的世界速度；仅转交父类，不作为实际位移权威。
 */
void USKMovementComponent::OnMovementUpdated(
    float DeltaSeconds,
    const FVector& OldLocation,
    const FVector& OldVelocity)
{
    Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

    // OnMovementUpdated 位于本次 RootMotion 转换、协调和 CMC 移动之后；在下次 Tick 重置写入快照前保留稳定副本。
    if (UsesMotionMatchingLocomotion())
        CompletedRootMotionCoordinationSnapshot = RootMotionCoordinationSnapshot;

    LastRootMotionRequestedTranslationWS = FVector::ZeroVector;
    LastRootMotionActualTranslationWS = FVector::ZeroVector;
    LastRootMotionCollisionClippedTranslationWS = FVector::ZeroVector;
    bLastRootMotionTranslationClipped = false;
    if (!bHasPendingRootMotionVelocity || !CharacterOwner
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= UE_SMALL_NUMBER)
    {
        bHasPendingRootMotionVelocity = false;
        PendingRootMotionVelocityWS = FVector::ZeroVector;
        return;
    }

    LastRootMotionRequestedTranslationWS = PendingRootMotionVelocityWS * DeltaSeconds;
    LastRootMotionRequestedTranslationWS.Z = 0.f;
    LastRootMotionActualTranslationWS = CharacterOwner->GetActorLocation() - OldLocation;
    LastRootMotionActualTranslationWS.Z = 0.f;
    LastRootMotionCollisionClippedTranslationWS =
        LastRootMotionRequestedTranslationWS - LastRootMotionActualTranslationWS;
    bLastRootMotionTranslationClipped =
        !LastRootMotionCollisionClippedTranslationWS.IsNearlyZero(0.1f);
    bHasPendingRootMotionVelocity = false;
    PendingRootMotionVelocityWS = FVector::ZeroVector;
}

/**
 * 在 Actor 成功传送并由 UE 更新 MovementMode/地面信息后重建 Motion Matching 实际状态基线。
 * 只能由 CharacterMovement 的游戏线程传送回调调用；先执行父类逻辑，再清空传送前历史，
 * 不把世界位置跳变记录为动画 RootMotion。组件缓存不可用时不创建无意义的待处理状态。
 */
void USKMovementComponent::OnTeleported()
{
    Super::OnTeleported();
    RefreshCachedComponents();
    bPoseOnlyAirborneMomentumActive = false;
    bMotionMatchingActualStateRebasePending = IsValid(MotionMatchingTrajectoryComponent)
        && !MotionMatchingTrajectoryComponent->RebaseMotionMatchingActualState();
}

/**
 * 在自主客户端收到服务器位置校正、但校正尚未写入 UpdatedComponent 时登记实际状态重建请求。
 * 本函数只转交父类诊断并登记一次校正事实；真正 Rebase 延后到下一次 Movement Tick 开始，
 * 确保采集的是服务器校正已经落地后的 Actor/Velocity/MovementMode。每次真实服务器校正都重建，
 * 同时覆盖未通过本回调参数暴露的可选 Rotation 修正。
 *
 * @param ClientData 当前客户端网络预测状态，由父类只读/记录。
 * @param TimeStamp 服务器确认的客户端移动时间戳，单位秒。
 * @param NewLocation 已转换到客户端世界空间的服务器位置，单位 cm。
 * @param NewVelocity 服务器校正速度，单位 cm/s。
 * @param NewMovementBaseInterfaceData 服务器移动基座数据，可为空。
 * @param NewBaseBoneName 基座骨骼名，可为 None。
 * @param bHasBase 服务器校正是否声明有效基座。
 * @param bBaseRelativePosition NewLocation 是否使用基座相对语义；引擎传入本重载前已转换世界位置。
 * @param ServerMovementMode 服务器压缩 MovementMode。
 * @param ServerGravityDirection 服务器重力方向。
 */
void USKMovementComponent::OnClientCorrectionReceived(
    FNetworkPredictionData_Client_Character& ClientData,
    float TimeStamp,
    FVector NewLocation,
    FVector NewVelocity,
    FMovementBaseInterfaceData* NewMovementBaseInterfaceData,
    FName NewBaseBoneName,
    bool bHasBase,
    bool bBaseRelativePosition,
    uint8 ServerMovementMode,
    FVector ServerGravityDirection)
{
    Super::OnClientCorrectionReceived(
        ClientData,
        TimeStamp,
        NewLocation,
        NewVelocity,
        NewMovementBaseInterfaceData,
        NewBaseBoneName,
        bHasBase,
        bBaseRelativePosition,
        ServerMovementMode,
        ServerGravityDirection);
    RefreshCachedComponents();
    bPoseOnlyAirborneMomentumActive = false;
    bMotionMatchingActualStateRebasePending |= IsValid(MotionMatchingTrajectoryComponent);
}

/**
 * 在模拟代理或 RootMotion 网络更新把胶囊校正到服务器位置后重建 Motion Matching 实际基线。
 * 先让父类完成胶囊瞬移与 Mesh 平滑偏移，再从最终胶囊事实 Rebase；视觉 Mesh 的后续插值不进入
 * Actor 历史。位置与旋转都未改变时保留历史，组件不可用时不创建无意义的待处理状态。
 *
 * @param OldLocation 网络校正前胶囊世界位置，单位 cm。
 * @param OldRotation 网络校正前胶囊世界旋转。
 * @param NewLocation 服务器目标胶囊世界位置，单位 cm。
 * @param NewRotation 服务器目标胶囊世界旋转。
 */
void USKMovementComponent::SmoothCorrection(
    const FVector& OldLocation,
    const FQuat& OldRotation,
    const FVector& NewLocation,
    const FQuat& NewRotation)
{
    const bool bHasTransformCorrection = !OldLocation.Equals(NewLocation, 0.1)
        || !OldRotation.Equals(NewRotation, UE_KINDA_SMALL_NUMBER);
    Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
    if (!bHasTransformCorrection) return;

    RefreshCachedComponents();
    bPoseOnlyAirborneMomentumActive = false;
    bMotionMatchingActualStateRebasePending = IsValid(MotionMatchingTrajectoryComponent)
        && !MotionMatchingTrajectoryComponent->RebaseMotionMatchingActualState();
}

/**
 * 在 PerformMovement 已完成动画 RootMotion 转换、但尚未进入具体 Phys* 前结束 PoseOnly 混合残留。
 * 仅当正式 Motion Matching、Falling、Locomotion 仍拥有 RootMotion 且 PostSelection 已开启交接时生效；
 * 清除参数后 PhysFalling 会把当前水平速度当作普通物理惯性继续积分，并恢复标准碰撞速度修正。
 * 本函数不生成新速度、不读取 MoveIntent，也不影响全身动作持有的排他 RootMotion。
 *
 * @param DeltaSeconds 当前移动求值步长，单位秒；本实现不积分该值，仅原样保留引擎回调契约。
 */
void USKMovementComponent::UpdateVelocityBeforeMovement(float DeltaSeconds)
{
    static_cast<void>(DeltaSeconds);
    if (!ShouldUsePoseOnlyAirborneMomentum() || !HasAnimRootMotion()) return;

    RootMotionParams.Clear();
    AnimRootMotionVelocity = FVector::ZeroVector;
}

/**
 * 捕获动画 RootMotion 在物理与碰撞求值前提出的世界速度，并复用 UE 原生 MovementMode 约束。
 * 本函数由 CharacterMovement 在游戏线程调用；const 仅符合引擎接口，捕获值在同帧
 * OnMovementUpdated 消费。PoseOnly 空中交接时保留 CurrentVelocity 的水平分量，禁止状态混合尾部
 * 用零 RootMotion 覆盖已经由 JumpStart 实际产生的惯性；垂直速度仍使用 UE 原生约束结果。
 *
 * @param RootMotionVelocity 动画 RootMotion 换算出的世界速度，单位 cm/s。
 * @param CurrentVelocity 当前 MovementMode 的世界速度，单位 cm/s。
 * @return UE 原生约束后的 RootMotion 速度。
 */
FVector USKMovementComponent::ConstrainAnimRootMotionVelocity(
    const FVector& RootMotionVelocity,
    const FVector& CurrentVelocity) const
{
    FVector ConstrainedVelocity = Super::ConstrainAnimRootMotionVelocity(
        RootMotionVelocity,
        CurrentVelocity);
    const bool bUsePoseOnlyAirborneMomentum = ShouldUsePoseOnlyAirborneMomentum();
    PendingRootMotionVelocityWS = ConstrainedVelocity;
    PendingRootMotionVelocityWS.Z = 0.f;
    bHasPendingRootMotionVelocity = !bUsePoseOnlyAirborneMomentum
        && FMath::IsFinite(PendingRootMotionVelocityWS.X)
        && FMath::IsFinite(PendingRootMotionVelocityWS.Y)
        && FMath::IsFinite(PendingRootMotionVelocityWS.Z);
    if (!bHasPendingRootMotionVelocity) PendingRootMotionVelocityWS = FVector::ZeroVector;
    if (bUsePoseOnlyAirborneMomentum)
    {
        ConstrainedVelocity.X = CurrentVelocity.X;
        ConstrainedVelocity.Y = CurrentVelocity.Y;
    }
    return ConstrainedVelocity;
}

/**
 * 提供未被 Blueprint 或 UnLua 覆盖时的默认移动策略入口。
 * TickComponent 在游戏线程、原生 CharacterMovement 求值前调用此反射事件；
 * 默认实现刻意不编排 Gameplay 规则，使关闭 Lua 策略或未绑定脚本时仍由原生组件负责物理、碰撞和 Root Motion。
 *
 * @param DeltaTime 当前帧时长，单位为秒。
 */
void USKMovementComponent::UpdateMovementLogic_Implementation(float DeltaTime)
{
    static_cast<void>(DeltaTime);
}

/** 刷新角色、输入、相机和战斗组件缓存；发现输入组件时建立一次 Tick 前置关系。 */
void USKMovementComponent::RefreshCachedComponents()
{
    if (!OwnerCharacter) OwnerCharacter = Cast<ACharacter>(GetOwner());
    if (!OwnerCharacter) return;

    if (!InputManager)
    {
        InputManager = OwnerCharacter->FindComponentByClass<USKInputManager>();
        if (InputManager) AddTickPrerequisiteComponent(InputManager);
    }
    if (!CameraManager)
    {
        CameraManager = OwnerCharacter->FindComponentByClass<USKCameraManagerComponent>();
    }
    if (!CombatComponent)
    {
        CombatComponent = OwnerCharacter->FindComponentByClass<USKCombatComponent>();
    }
    if (!IsValid(MotionMatchingTrajectoryComponent))
    {
        MotionMatchingTrajectoryComponent =
            OwnerCharacter->FindComponentByClass<USKMotionMatchingTrajectoryComponent>();
    }
}

/**
 * 判断本次游戏线程移动求值是否处于 PoseOnly 空中水平惯性交接窗口。
 * 只有正式 Motion Matching、Falling 且 Locomotion 仍持有根运动通道时返回 true；全身动作接管、
 * 地面阶段或能力组件失效都会失败关闭。本函数只读运行时状态，不修改速度、RootMotion 或令牌。
 *
 * @return 应屏蔽动画混合残留并由 CMC 延续现有水平惯性时返回 true。
 */
bool USKMovementComponent::ShouldUsePoseOnlyAirborneMomentum() const
{
    return bPoseOnlyAirborneMomentumActive
        && UsesMotionMatchingLocomotion()
        && IsFalling()
        && RootMotionOwnershipSnapshot.bLocomotionRootMotionAllowed
        && (!CombatComponent || !CombatComponent->IsCombatFullBodyActionActive());
}

/**
 * 在游戏线程清理 Requester 已销毁的活动令牌，防止异常结束路径永久阻塞 Locomotion。
 * 正常 GA 生命周期仍必须显式释放；本函数只提供弱引用失效后的最后安全网，不停止 Montage 或广播事件。
 */
void USKMovementComponent::ReconcileRootMotionOwnership()
{
    bool bChanged = false;
    if (ActiveUpperBodyToken.Serial > 0 && !ActiveUpperBodyToken.Requester.IsValid())
    {
        ActiveUpperBodyToken = FSKRootMotionOwnerToken();
        bChanged = true;
    }
    if (ActiveRootMotionOverrideToken.Serial > 0
        && !ActiveRootMotionOverrideToken.Requester.IsValid())
    {
        ActiveRootMotionOverrideToken = FSKRootMotionOwnerToken();
        bChanged = true;
    }
    if (bChanged) RefreshRootMotionOwnershipSnapshot();
}

/**
 * 从两个活动令牌发布下一版权威所有权快照。
 * UpperBody 只影响姿势通道；只有 FullBody/Traversal 令牌会替换有效根运动所有者并关闭 Locomotion。
 * 本函数不验证 Requester 生命周期，调用前必须完成申请校验或 Reconcile。
 */
void USKMovementComponent::RefreshRootMotionOwnershipSnapshot()
{
    FSKRootMotionOwnershipSnapshot Next;
    Next.Revision = RootMotionOwnershipSnapshot.Revision < MAX_int64
        ? RootMotionOwnershipSnapshot.Revision + 1
        : MAX_int64;
    Next.bUpperBodyActive = ActiveUpperBodyToken.Serial > 0;
    Next.UpperBodyRequester = ActiveUpperBodyToken.Requester;
    if (ActiveRootMotionOverrideToken.Serial > 0)
    {
        Next.EffectiveRootMotionOwner = ActiveRootMotionOverrideToken.OwnerType;
        Next.RootMotionRequester = ActiveRootMotionOverrideToken.Requester;
        Next.bLocomotionRootMotionAllowed = false;
    }
    RootMotionOwnershipSnapshot = Next;
}

/**
 * 构造所有权操作结果的纯值副本，不修改活动令牌或版本。
 * 非游戏线程只返回默认 Locomotion 快照，不读取正在变化的内部状态。
 *
 * @param Code 已由调用方确定的申请或释放结果。
 * @param Token 与本次操作相关的令牌；失败时允许为空或为当前冲突令牌。
 * @return 携带当前权威快照的完整结果。
 */
FSKRootMotionOwnershipResult USKMovementComponent::MakeRootMotionOwnershipResult(
    ESKRootMotionOwnershipResultCode Code,
    const FSKRootMotionOwnerToken& Token) const
{
    FSKRootMotionOwnershipResult Result;
    Result.Code = Code;
    Result.Token = Token;
    Result.Snapshot = IsInGameThread()
        ? RootMotionOwnershipSnapshot
        : FSKRootMotionOwnershipSnapshot();
    return Result;
}

/**
 * 在原生 Movement 求值前初始化一条正式 RootMotion 协调记录。
 * 每个 Motion Matching Movement Tick 恰好创建一个采样号；若本帧没有动画 RootMotion，
 * 快照仍保持有效但 bHasAnimationRootMotion 为 false，避免消费者误读上一帧结果。
 *
 * @param DeltaSeconds 当前 Movement Tick 步长，单位秒；非有限或负值按 0 保存。
 */
void USKMovementComponent::BeginRootMotionCoordinationSample(float DeltaSeconds)
{
    ReconcileRootMotionOwnership();
    RootMotionCoordinationSnapshot = FSKRootMotionCoordinationSnapshot();
    RootMotionCoordinationSnapshot.SampleId = ++RootMotionCoordinationSampleId;
    RootMotionCoordinationSnapshot.DeltaSeconds =
        FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    RootMotionCoordinationSnapshot.OwnershipRevision = RootMotionOwnershipSnapshot.Revision;
    RootMotionCoordinationSnapshot.EffectiveRootMotionOwner =
        RootMotionOwnershipSnapshot.EffectiveRootMotionOwner;
    RootMotionCoordinationSnapshot.bValid = true;
}

/**
 * 计算当前世界 Yaw 到目标世界 Yaw 的最短 Steering 修正，并同时执行每秒速率与单帧角度上限。
 * 本函数只返回角度，不修改快照、RootMotion 或 Actor；任一输入非法、步长非正或通道关闭时返回 0。
 *
 * @param CurrentYaw Steering 前的世界 Yaw，单位度。
 * @param TargetYaw 期望收敛的世界 Yaw，单位度。
 * @param MaxDegreesPerSecond 当前模式允许的最大修正速度，单位度/秒。
 * @param DeltaSeconds 当前 RootMotion 求值步长，单位秒。
 * @return 限制后的有符号最短修正角，正值绕世界 Z 轴正向旋转。
 */
float USKMovementComponent::ResolveRootMotionSteeringDelta(
    float CurrentYaw,
    float TargetYaw,
    float MaxDegreesPerSecond,
    float DeltaSeconds) const
{
    if (!FMath::IsFinite(CurrentYaw) || !FMath::IsFinite(TargetYaw)
        || !FMath::IsFinite(MaxDegreesPerSecond) || MaxDegreesPerSecond <= 0.f
        || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f
        || MaxRootMotionSteeringDegreesPerFrame <= 0.f) return 0.f;

    const float MaxDelta = FMath::Min(
        MaxDegreesPerSecond * DeltaSeconds,
        MaxRootMotionSteeringDegreesPerFrame);
    const float RequiredDelta = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
    return FMath::Clamp(RequiredDelta, -MaxDelta, MaxDelta);
}

/**
 * 以最短路径更新角色 ActorYaw；只修改旋转，不处理控制器、相机或移动方向选择。
 *
 * @param TargetYaw 目标世界 Yaw，单位为度。
 * @param InterpSpeed 插值速度；小于等于 0 时立即对齐。
 * @param DeltaTime 当前帧时长，单位秒。
 */
void USKMovementComponent::ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime)
{
    if (!OwnerCharacter) return;

    const FRotator CurrentRotation = OwnerCharacter->GetActorRotation();
    const float NewYaw = InterpSKMovementYawShortest(CurrentRotation.Yaw, TargetYaw, DeltaTime, InterpSpeed);
    OwnerCharacter->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

/**
 * 在 Mesh 空间转换之前捕获动画评估产出的原始局部 RootMotion，并原样交回 UE。
 * 只为 Motion Matching 正式路径写协调快照；Classic、错误委托来源或无效能力标记均不记录。
 * 本函数在 CharacterMovement 游戏线程移动求值内部调用，不修改 Actor Transform。
 *
 * @param LocalSpaceRootMotion 动画评估产出的组件局部 RootMotion Delta。
 * @param MovementComponent 发起回调的 CharacterMovement；必须是当前实例才接受。
 * @param DeltaSeconds 当前 RootMotion 求值步长，单位秒；非有限或负值不会覆盖 Tick 步长。
 * @return 始终返回未修改的 LocalSpaceRootMotion，保证本阶段只有采集职责。
 */
FTransform USKMovementComponent::CaptureAnimationRootMotionPreConvert(
    const FTransform& LocalSpaceRootMotion,
    UCharacterMovementComponent* MovementComponent,
    float DeltaSeconds)
{
    if (MovementComponent != this || !UsesMotionMatchingLocomotion()) return LocalSpaceRootMotion;

    if (!RootMotionCoordinationSnapshot.bValid) BeginRootMotionCoordinationSample(DeltaSeconds);
    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.f)
        RootMotionCoordinationSnapshot.DeltaSeconds = DeltaSeconds;

    RootMotionCoordinationSnapshot.AnimationDeltaLS = LocalSpaceRootMotion;
    RootMotionCoordinationSnapshot.bHasAnimationRootMotion =
        !LocalSpaceRootMotion.GetTranslation().IsNearlyZero()
        || !LocalSpaceRootMotion.GetRotation().IsIdentity();
    return LocalSpaceRootMotion;
}

/**
 * 在 Mesh 已把动画 RootMotion 转到世界空间后执行唯一协调与提交。
 * Motion Matching 路径记录动画世界 Delta、Steering 修正和最终 Delta：Free 以动画根旋转为主，
 * 仅向 DesiredMoveYaw 有限收敛；Locked 用 DesiredFacingYaw 修正根旋转，用 DesiredMoveYaw 独立修正
 * 水平平移方向。PoseOnly 空中交接后把混合残留压为单位 Delta；全身动作期间 Steering 关闭。
 * Classic 继续保留旧水平重定向，不写正式协调快照。
 * 返回值只交给 CharacterMovement 后续速度、碰撞和根旋转消费，本函数不直接移动或旋转 Actor。
 *
 * @param WorldSpaceRootMotion Mesh 转换后的动画世界 RootMotion Delta。
 * @param MovementComponent 发起回调的 CharacterMovement；不是当前实例时原样返回。
 * @param DeltaSeconds 当前 RootMotion 求值步长，单位秒；仅用于刷新协调快照时间。
 * @return Motion Matching 返回唯一 FinalDeltaWS；Classic 返回旧重定向结果或原始值。
 */
FTransform USKMovementComponent::CoordinateRootMotionPostConvert(
    const FTransform& WorldSpaceRootMotion,
    UCharacterMovementComponent* MovementComponent,
    float DeltaSeconds)
{
    if (MovementComponent != this) return WorldSpaceRootMotion;

    if (UsesMotionMatchingLocomotion())
    {
        if (!RootMotionCoordinationSnapshot.bValid) BeginRootMotionCoordinationSample(DeltaSeconds);
        if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds >= 0.f)
            RootMotionCoordinationSnapshot.DeltaSeconds = DeltaSeconds;

        RootMotionCoordinationSnapshot.AnimationDeltaWS = WorldSpaceRootMotion;
        RootMotionCoordinationSnapshot.SteeringTranslationDeltaWS = FVector::ZeroVector;
        RootMotionCoordinationSnapshot.SteeringYawDeltaDegrees = 0.f;
        RootMotionCoordinationSnapshot.SteeringTranslationYawDeltaDegrees = 0.f;
        RootMotionCoordinationSnapshot.bSteeringSuppressed = false;
        RootMotionCoordinationSnapshot.FinalDeltaWS = WorldSpaceRootMotion;
        RootMotionCoordinationSnapshot.bHasAnimationRootMotion =
            RootMotionCoordinationSnapshot.bHasAnimationRootMotion
            || !WorldSpaceRootMotion.GetTranslation().IsNearlyZero()
            || !WorldSpaceRootMotion.GetRotation().IsIdentity();

        if (ShouldUsePoseOnlyAirborneMomentum())
        {
            RootMotionCoordinationSnapshot.bSteeringSuppressed = true;
            RootMotionCoordinationSnapshot.FinalDeltaWS = FTransform::Identity;
            return RootMotionCoordinationSnapshot.FinalDeltaWS;
        }

        if (!OwnerCharacter || !IsValid(MotionMatchingTrajectoryComponent)
            || !RootMotionOwnershipSnapshot.bLocomotionRootMotionAllowed
            || (CombatComponent && CombatComponent->IsCombatFullBodyActionActive()))
        {
            RootMotionCoordinationSnapshot.bSteeringSuppressed = true;
            return RootMotionCoordinationSnapshot.FinalDeltaWS;
        }

        const FSKMotionMatchingIntentSnapshot Intent =
            MotionMatchingTrajectoryComponent->GetMotionMatchingIntent();
        const FQuat CurrentActorRotation = OwnerCharacter->GetActorQuat();
        const FQuat AnimatedActorRotation =
            WorldSpaceRootMotion.GetRotation() * CurrentActorRotation;
        const float AnimatedFacingYaw = AnimatedActorRotation.Rotator().Yaw;
        float RotationSteeringYaw = 0.f;
        float TranslationSteeringYaw = 0.f;
        const FVector AnimationTranslation = WorldSpaceRootMotion.GetTranslation();
        const FVector AnimationHorizontalTranslation(
            AnimationTranslation.X,
            AnimationTranslation.Y,
            0.f);

        if (Intent.Intent.RotationMode == ESKMotionMatchingRotationMode::Locked)
        {
            if (Intent.Intent.bHasFacingTarget)
            {
                RotationSteeringYaw = ResolveRootMotionSteeringDelta(
                    AnimatedFacingYaw,
                    Intent.Intent.DesiredFacingYaw,
                    LockedRootMotionFacingSteeringRate,
                    DeltaSeconds);
            }
            if (Intent.bHasMoveIntent && !AnimationHorizontalTranslation.IsNearlyZero())
            {
                TranslationSteeringYaw = ResolveRootMotionSteeringDelta(
                    AnimationHorizontalTranslation.Rotation().Yaw,
                    Intent.DesiredMoveYaw,
                    LockedRootMotionTranslationSteeringRate,
                    DeltaSeconds);
            }
        }
        else if (Intent.bHasMoveIntent)
        {
            RotationSteeringYaw = ResolveRootMotionSteeringDelta(
                AnimatedFacingYaw,
                Intent.DesiredMoveYaw,
                FreeRootMotionSteeringRate,
                DeltaSeconds);
            if (!AnimationHorizontalTranslation.IsNearlyZero())
            {
                TranslationSteeringYaw = ResolveRootMotionSteeringDelta(
                    AnimationHorizontalTranslation.Rotation().Yaw,
                    Intent.DesiredMoveYaw,
                    FreeRootMotionSteeringRate,
                    DeltaSeconds);
            }
        }

        FTransform FinalRootMotion = WorldSpaceRootMotion;
        const FQuat RotationCorrection =
            FRotator(0.f, RotationSteeringYaw, 0.f).Quaternion();
        FQuat FinalRotation = RotationCorrection * WorldSpaceRootMotion.GetRotation();
        FinalRotation.Normalize();
        FinalRootMotion.SetRotation(FinalRotation);

        FVector FinalTranslation = AnimationTranslation;
        if (!AnimationHorizontalTranslation.IsNearlyZero())
        {
            const FVector SteeredHorizontalTranslation =
                AnimationHorizontalTranslation.RotateAngleAxis(
                    TranslationSteeringYaw,
                    FVector::UpVector);
            FinalTranslation.X = SteeredHorizontalTranslation.X;
            FinalTranslation.Y = SteeredHorizontalTranslation.Y;
        }
        FinalRootMotion.SetTranslation(FinalTranslation);

        RootMotionCoordinationSnapshot.SteeringTranslationDeltaWS =
            FinalTranslation - AnimationTranslation;
        RootMotionCoordinationSnapshot.SteeringYawDeltaDegrees = RotationSteeringYaw;
        RootMotionCoordinationSnapshot.SteeringTranslationYawDeltaDegrees =
            TranslationSteeringYaw;
        RootMotionCoordinationSnapshot.FinalDeltaWS = FinalRootMotion;
        return RootMotionCoordinationSnapshot.FinalDeltaWS;
    }

    if (!bEnableClassicRootMotionDirectionWarping
        || !bRootMotionDirectionWarpingEnabled) return WorldSpaceRootMotion;

    const FVector OriginalTranslation = WorldSpaceRootMotion.GetTranslation();
    const float HorizontalDistance = FVector(OriginalTranslation.X, OriginalTranslation.Y, 0.f).Size();
    if (HorizontalDistance <= UE_SMALL_NUMBER) return WorldSpaceRootMotion;

    const FVector TargetDirection = FRotator(0.f, RootMotionDirectionWarpingWorldYaw, 0.f).Vector();
    FVector WarpedTranslation = TargetDirection * HorizontalDistance;
    WarpedTranslation.Z = OriginalTranslation.Z;

    FTransform WarpedRootMotion = WorldSpaceRootMotion;
    WarpedRootMotion.SetTranslation(WarpedTranslation);
    return WarpedRootMotion;
}
