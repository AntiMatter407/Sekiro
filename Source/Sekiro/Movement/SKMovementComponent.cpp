#include "Movement/SKMovementComponent.h"

#include "Camera/SKCameraManagerComponent.h"
#include "Input/SKInputManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "UnLua.h"
#include "UnLuaModule.h"

namespace
{
    static UnLua::FLuaRetValues RequireSKMovementLuaModule(
        UnLua::FLuaEnv* LuaEnv,
        const FString& LuaModuleName,
        bool& bOutSucceeded)
    {
        bOutSucceeded = false;
        if (!LuaEnv || LuaModuleName.IsEmpty()) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        lua_State* LuaState = LuaEnv->GetMainState();
        if (!LuaState) return UnLua::FLuaRetValues(LuaEnv, INDEX_NONE);

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaModuleName);
        UnLua::FLuaRetValues ReturnValues = UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKMovementComponent Lua require failed. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        if (ReturnValues[0].GetType() != LUA_TTABLE)
        {
            UE_LOG(LogTemp, Warning, TEXT("SKMovementComponent Lua module must return a table. Module=%s"), *LuaModuleName);
            return ReturnValues;
        }

        bOutSucceeded = true;
        return ReturnValues;
    }

    static bool ReadSKMovementLuaHandled(UnLua::FLuaRetValues& ReturnValues, const FString& LuaModuleName)
    {
        if (!ReturnValues.IsValid() || ReturnValues.Num() == 0 || ReturnValues[0].GetType() == LUA_TNIL) return false;
        if (ReturnValues[0].GetType() == LUA_TBOOLEAN) return ReturnValues[0].Value<bool>();

        UE_LOG(LogTemp, Warning, TEXT("SKMovementComponent Lua Tick should return boolean. Module=%s"), *LuaModuleName);
        return false;
    }

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
    bRootMotionMoveDirectionEnabled = false;
    MaxWalkSpeed = RunSpeed;
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
 * 设置 UE CharacterMovement 内置自动旋转开关，供 Lua 明确分配 ActorYaw 所有权。
 * 只能在游戏线程调用；不直接旋转角色。
 *
 * @param bNewOrientRotationToMovement 是否由加速度方向驱动内置旋转。
 * @param bNewUseControllerDesiredRotation 是否由控制器期望旋转驱动内置旋转。
 */
void USKMovementComponent::SetMovementRotationSettingsForScript(
    bool bNewOrientRotationToMovement,
    bool bNewUseControllerDesiredRotation)
{
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
 * 设置动画 Root Motion 转为世界空间后的水平目标方向。
 * Lua 负责决定何时启用以及目标世界 Yaw；本函数只保存当前帧策略，不改变位移长度、垂直分量或旋转。
 * 只能在游戏线程、CharacterMovement 求值前调用；后续由原生 Root Motion 转换委托读取。
 *
 * @param bEnabled 是否启用水平位移方向校正；false 时原样保留动画根运动。
 * @param TargetWorldYaw 期望水平位移指向的世界 Yaw，单位为度。
 */
void USKMovementComponent::SetRootMotionMoveDirectionForScript(bool bEnabled, float TargetWorldYaw)
{
    bRootMotionMoveDirectionEnabled = bEnabled;
    RootMotionMoveDirectionYaw = FMath::UnwindDegrees(TargetWorldYaw);
}

/** 查询 Lua 本帧是否发布了有效移动目标 Yaw；供动画数据采集只读调用。 */
bool USKMovementComponent::HasDesiredMoveYawSnapshot() const
{
    return bHasDesiredMoveYawSnapshot;
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
    ProcessRootMotionPostConvertToWorld.BindUObject(this, &USKMovementComponent::RedirectRootMotionTranslation);
    RefreshCachedComponents();
    if (InputManager) AddTickPrerequisiteComponent(InputManager);

    if (!bEngineDispatchesReceiveBeginPlay) ReceiveBeginPlay();
}

/**
 * 在原生 CharacterMovement 求值前调用 Lua Movement，再交还 UE 处理物理、碰撞、Root Motion 和网络预测。
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
    TryCallLuaMovementTick(DeltaTime);
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

/**
 * require 当前 Movement 模块并调用其 Tick(context, delta_seconds) 导出入口。
 * 只能在游戏线程调用；返回值仅表示 Lua 是否处理策略，不代表原生移动是否成功。
 *
 * @param DeltaTime 当前帧时长，单位为秒。
 * @return Lua 返回 true 时为 true；模块、环境、函数或返回值无效时为 false。
 */
bool USKMovementComponent::TryCallLuaMovementTick(float DeltaTime)
{
    const FString ModuleName = ResolveLuaMovementModuleName();
    if (!bUseLuaMovementLogic || ModuleName.IsEmpty()) return false;

    IUnLuaModule& UnLuaModule = IUnLuaModule::Get();
    UnLua::FLuaEnv* LuaEnv = UnLuaModule.GetEnv(this);
    if (!LuaEnv) return false;

    bool bRequireSucceeded = false;
    UnLua::FLuaRetValues RequireReturnValues = RequireSKMovementLuaModule(LuaEnv, ModuleName, bRequireSucceeded);
    if (!bRequireSucceeded) return false;

    UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
    UnLua::FLuaValue FunctionValue = ModuleTable["Tick"];
    if (FunctionValue.GetType() != LUA_TFUNCTION) return false;

    UnLua::FLuaFunction LuaFunction(LuaEnv, FunctionValue);
    UnLua::FLuaRetValues FunctionReturnValues = LuaFunction.Call(this, DeltaTime);
    const bool bHandled = ReadSKMovementLuaHandled(FunctionReturnValues, ModuleName);
    FunctionReturnValues.Pop();
    return bHandled;
}

/** 解析 UnLua 接口覆盖后的模块名，接口未提供值时回退到组件配置。 */
FString USKMovementComponent::ResolveLuaMovementModuleName() const
{
    if (GetClass()->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FString InterfaceModuleName = IUnLuaInterface::Execute_GetModuleName(const_cast<USKMovementComponent*>(this));
        if (!InterfaceModuleName.IsEmpty()) return InterfaceModuleName;
    }

    return LuaMovementModuleName;
}

/** 刷新角色、输入和相机组件缓存；发现输入组件时建立一次 Tick 前置关系。 */
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
 * 把已转换到世界空间的动画 Root Motion 水平位移旋转到 Lua 发布的目标方向。
 * 本函数由 CharacterMovement 的 PostConvert 委托在游戏线程调用；只重定向 XY，保留原始水平长度、Z、旋转和缩放。
 * 非本组件调用、策略关闭或水平位移近似为零时原样返回，避免影响空中、原地动作和其他移动组件。
 *
 * @param WorldRootMotion UE 已完成组件空间到世界空间转换的根运动变换。
 * @param SourceMovementComponent 发起转换的 CharacterMovement；必须等于当前组件才会处理，可为空。
 * @param DeltaSeconds 当前根运动求值步长，单位为秒；方向重定向不依赖该值，仅保留委托契约。
 * @return 校正后的世界根运动变换；不满足处理条件时返回输入值。
 */
FTransform USKMovementComponent::RedirectRootMotionTranslation(
    const FTransform& WorldRootMotion,
    UCharacterMovementComponent* SourceMovementComponent,
    float DeltaSeconds) const
{
    static_cast<void>(DeltaSeconds);
    if (!bRootMotionMoveDirectionEnabled || SourceMovementComponent != this) return WorldRootMotion;

    const FVector OriginalTranslation = WorldRootMotion.GetTranslation();
    const float HorizontalDistance = FVector2D(OriginalTranslation.X, OriginalTranslation.Y).Size();
    if (HorizontalDistance <= UE_SMALL_NUMBER) return WorldRootMotion;

    const FVector TargetDirection = FRotator(0.f, RootMotionMoveDirectionYaw, 0.f).Vector();
    FTransform RedirectedRootMotion = WorldRootMotion;
    RedirectedRootMotion.SetTranslation(FVector(
        TargetDirection.X * HorizontalDistance,
        TargetDirection.Y * HorizontalDistance,
        OriginalTranslation.Z));
    return RedirectedRootMotion;
}
