#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UnLuaInterface.h"
#include "SKMovementComponent.generated.h"

class ACharacter;
class USKCameraManagerComponent;
class USKCombatComponent;
class USKInputManager;
class USKMotionMatchingTrajectoryComponent;
class USKMovementComponent;
class FNetworkPredictionData_Client_Character;
struct FMovementBaseInterfaceData;

UENUM(BlueprintType)
enum class ESKMovementTier : uint8
{
    Idle,                                                           // 静止
    Walk,                                                           // 步行
    Run,                                                            // 奔跑
    Sprint,                                                         // 冲刺
    Crouch                                                          // 蹲行
};

UENUM(BlueprintType)
enum class ESKMotionMatchingGait : uint8
{
    Walk,
    Run,
    Sprint,
};

UENUM(BlueprintType)
enum class ESKMotionMatchingStance : uint8
{
    Standing,
    Crouching,
};

UENUM(BlueprintType)
enum class ESKRootMotionOwnerType : uint8
{
    Locomotion,
    UpperBody,
    FullBody,
    Traversal,
};

UENUM(BlueprintType)
enum class ESKRootMotionOwnershipResultCode : uint8
{
    Acquired,
    AlreadyOwned,
    Released,
    InvalidRequester,
    InvalidOwnerType,
    Busy,
    StaleToken,
    SerialExhausted,
    WrongThread,
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKRootMotionOwnerToken
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    TWeakObjectPtr<USKMovementComponent> Authority; // 签发令牌的角色 Movement，禁止跨角色释放

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    TWeakObjectPtr<UObject> Requester; // 申请动作的 UObject 弱身份，不延长 GA 或组件生命周期

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    int64 Serial = 0; // Movement 内单调递增的令牌序号，零表示无效

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    ESKRootMotionOwnerType OwnerType = ESKRootMotionOwnerType::Locomotion; // 申请的姿势或根运动通道
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKRootMotionOwnershipSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    int64 Revision = 0; // 任一通道所有权发生变化时递增

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    ESKRootMotionOwnerType EffectiveRootMotionOwner = ESKRootMotionOwnerType::Locomotion; // 当前实际根运动来源

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    TWeakObjectPtr<UObject> RootMotionRequester; // FullBody 或 Traversal 排他覆盖申请者

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    TWeakObjectPtr<UObject> UpperBodyRequester; // 不夺取根运动的上身姿势申请者

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    bool bUpperBodyActive = false; // 是否存在与 Locomotion 可共存的上身令牌

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    bool bLocomotionRootMotionAllowed = true; // FullBody/Traversal 未覆盖时允许基础动画提交根运动
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKRootMotionOwnershipResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    ESKRootMotionOwnershipResultCode Code = ESKRootMotionOwnershipResultCode::InvalidRequester; // 申请或释放结果

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    FSKRootMotionOwnerToken Token; // 成功申请、幂等申请或失败输入关联的令牌

    UPROPERTY(BlueprintReadOnly, Category = "Movement|Root Motion Ownership")
    FSKRootMotionOwnershipSnapshot Snapshot; // 操作完成后的权威所有权快照
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKRootMotionCoordinationSnapshot
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    int64 SampleId = 0;                                           // Movement Tick 单调递增采样号

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    float DeltaSeconds = 0.f;                                     // 本次 RootMotion 求值步长，单位秒

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    bool bValid = false;                                          // 当前快照是否属于 Motion Matching 正式路径

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    bool bHasAnimationRootMotion = false;                          // 本次是否收到非单位动画 RootMotion

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    int64 OwnershipRevision = 0;                                  // 本次协调使用的所有权快照版本

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    ESKRootMotionOwnerType EffectiveRootMotionOwner = ESKRootMotionOwnerType::Locomotion; // 本次最终根运动来源

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    FTransform AnimationDeltaLS = FTransform::Identity;           // PreConvert 捕获的动画原始局部 Delta

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    FTransform AnimationDeltaWS = FTransform::Identity;           // Mesh 转换后、Steering 前的世界 Delta

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    FVector SteeringTranslationDeltaWS = FVector::ZeroVector;     // Steering 对世界平移追加的修正量

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    float SteeringYawDeltaDegrees = 0.f;                          // Steering 对根旋转追加的世界 Yaw

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    float SteeringTranslationYawDeltaDegrees = 0.f;               // Steering 对水平平移方向追加的世界 Yaw

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    bool bSteeringSuppressed = false;                             // 全身动作或上下文无效时是否禁止 Locomotion Steering

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Root Motion")
    FTransform FinalDeltaWS = FTransform::Identity;               // 唯一交给 CMC 消费的最终世界 Delta
};

UCLASS()
class SEKIRO_API USKMovementComponent : public UCharacterMovementComponent, public IUnLuaInterface
{
    GENERATED_BODY()

public:
    USKMovementComponent();

    // ── 移动档位与速度快照 ────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float WalkSpeed = 140.f;                                      // Lua 当前发布的步行目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float RunSpeed = 407.f;                                       // Lua 当前发布的跑步目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Speed")
    float SprintSpeed = 853.f;                                    // Lua 当前发布的冲刺目标速度

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|State")
    ESKMovementTier CurrentMovementTier = ESKMovementTier::Run;   // 输入 Lua 选择的当前移动档位

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Motion Matching")
    ESKMotionMatchingGait RequestedMotionMatchingGait = ESKMotionMatchingGait::Run; // 输入层持久发布的目标动画速度族

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Motion Matching")
    ESKMotionMatchingStance RequestedMotionMatchingStance = ESKMotionMatchingStance::Standing; // 输入层持久发布的目标动画姿态

    void ApplyInputMovementTier(ESKMovementTier NewMovementTier); // 同步旧移动档位与独立 Motion Matching 请求

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMovementSpeedProfileForScript(float NewWalkSpeed, float NewRunSpeed, float NewSprintSpeed); // 发布 Lua 速度配置

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMaxWalkSpeedForScript(float NewMaxWalkSpeed);          // 设置本帧原生移动速度上限

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsMovementTierSprint() const;                             // 当前档位是否为冲刺

    // ── RootMotion 所有权 ────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Movement|Root Motion Ownership")
    FSKRootMotionOwnershipResult AcquireRootMotionOwnership(
        UObject* Requester,
        ESKRootMotionOwnerType RequestedOwner);                   // 申请上身或排他根运动令牌

    UFUNCTION(BlueprintCallable, Category = "Movement|Root Motion Ownership")
    FSKRootMotionOwnershipResult ReleaseRootMotionOwnership(
        const FSKRootMotionOwnerToken& Token);                    // 释放仍匹配当前身份的令牌

    UFUNCTION(BlueprintPure, Category = "Movement|Root Motion Ownership")
    FSKRootMotionOwnershipSnapshot GetRootMotionOwnershipSnapshot() const; // 获取当前纯值所有权快照

    UFUNCTION(BlueprintPure, Category = "Movement|Root Motion Ownership")
    bool IsRootMotionOwnerTokenValid(const FSKRootMotionOwnerToken& Token) const; // 校验令牌是否仍拥有对应通道

    // ── Lua Movement 宿主 ─────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetUseLuaMovementLogic(bool bNewUseLuaMovementLogic);     // 设置是否由 Lua 接管移动策略

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsUsingLuaMovementLogic() const;                          // 是否启用 Lua 移动策略

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetLuaMovementModuleName(const FString& ModuleName);      // 设置 Lua Movement 模块名

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    FString GetLuaMovementModuleName() const;                      // 获取 Lua Movement 模块名

    virtual FString GetModuleName_Implementation() const override; // UnLua 接口模块名

    UFUNCTION(BlueprintNativeEvent, Category = "Movement|Lua")
    void UpdateMovementLogic(float DeltaTime);                     // Lua 可覆盖的逐帧移动策略入口

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void RefreshCachedMovementComponents();                       // 刷新脚本可用组件缓存

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool HasOwnerCharacter() const;                               // 是否存在所属角色

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputX() const;                                  // 获取屏幕横向移动输入

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputY() const;                                  // 获取屏幕纵向移动输入

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetMoveInputAmount() const;                             // 获取移动输入强度

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsLockedOn() const;                                      // 是否锁定目标

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool HasLockTargetYaw() const;                                // 是否存在锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetLockTargetYawOrFallback(float FallbackYaw) const;    // 获取锁定目标 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetOwnerYaw() const;                                    // 获取角色世界 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetControllerYawOrFallback(float FallbackYaw) const;    // 获取控制器世界 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float NormalizeDeltaYaw(float FromYaw, float ToYaw) const;     // 计算最短 Yaw 差

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMovementRotationSettingsForScript(bool bNewOrientRotationToMovement, bool bNewUseControllerDesiredRotation); // 设置原生自动旋转开关

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void ApplyActorYawForScript(float TargetYaw, float InterpSpeed, float DeltaTime); // 插值角色 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void ApplyActorYawRateForScript(float TargetYaw, float MaxDegreesPerSecond, float DeltaTime); // 按固定角速度更新角色 Yaw

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetMoveFacingSnapshotForScript(bool bHasDesiredMoveYaw, float NewDesiredMoveYaw, float NewMoveDirectionAngleBeforeRotation); // 发布转向前移动快照

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void ClearMoveFacingSnapshotForScript();                       // 清空转向前移动快照

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetLockOnLocomotionSnapshotForScript(bool bEnabled, int32 CardinalDirection); // 发布锁定四方向素材快照

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    void SetRootMotionDirectionWarpingForScript(bool bEnabled, float TargetWorldYaw); // 设置动画根位移的目标世界方向

    UFUNCTION(BlueprintCallable, Category = "Movement|Root Motion")
    void SetRootMotionSteeringSettingsForScript(
        float FreeDegreesPerSecond,
        float LockedFacingDegreesPerSecond,
        float LockedTranslationDegreesPerSecond,
        float MaxDegreesPerFrame);                                // 发布 RootMotion Steering 速率限制

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsOwnerCombatFullBodyActionActiveForScript() const;      // 查询全身战斗动作是否正在占用 Root Motion

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    bool IsOwnerAttackActionActiveForScript() const;              // 查询角色是否正在执行轻攻击或重攻击

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float SampleOwnerCombatSequenceCurveForScript(FName CurveName) const; // 采样当前战斗动作源动画曲线

    UFUNCTION(BlueprintCallable, Category = "Movement|Lua")
    float GetHorizontalSpeedForScript() const;                    // 获取所属角色当前水平速度

    FVector GetLastRootMotionRequestedTranslationWS() const;      // 最近一次动画 RootMotion 请求的世界水平位移
    FVector GetLastRootMotionActualTranslationWS() const;         // 最近一次移动求值后的实际世界水平位移
    FVector GetLastRootMotionCollisionClippedTranslationWS() const; // 最近一次被碰撞或地面约束裁剪的世界水平位移
    bool WasLastRootMotionTranslationClipped() const;             // 最近一次 RootMotion 水平位移是否被裁剪

    UFUNCTION(BlueprintPure, Category = "Movement|Root Motion")
    FSKRootMotionCoordinationSnapshot GetRootMotionCoordinationSnapshot() const; // 获取最近完成的协调快照副本

    void SetPoseOnlyAirborneMomentumActive(bool bActive);          // 切换 PoseOnly 空中水平惯性交接状态
    bool UsesMotionMatchingLocomotion() const;                   // 是否由 Motion Matching 正式路径持有基础移动
    bool HasDesiredMoveYawSnapshot() const;                       // 动画采集是否可读取移动目标 Yaw
    float GetDesiredMoveYawSnapshot() const;                      // 动画采集读取移动目标 Yaw
    float GetMoveDirectionAngleBeforeRotationSnapshot() const;    // 动画采集读取转身前相对角
    bool HasLockOnLocomotionSnapshot() const;                     // 动画采集是否可读取锁定移动快照
    int32 GetLockOnCardinalDirectionSnapshot() const;             // 动画采集读取锁定四方向枚举值

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
    virtual void OnTeleported() override;
    virtual void OnClientCorrectionReceived(
        FNetworkPredictionData_Client_Character& ClientData,
        float TimeStamp,
        FVector NewLocation,
        FVector NewVelocity,
        FMovementBaseInterfaceData* NewMovementBaseInterfaceData,
        FName NewBaseBoneName,
        bool bHasBase,
        bool bBaseRelativePosition,
        uint8 ServerMovementMode,
        FVector ServerGravityDirection) override;
    virtual void SmoothCorrection(
        const FVector& OldLocation,
        const FQuat& OldRotation,
        const FVector& NewLocation,
        const FQuat& NewRotation) override;
    virtual void UpdateVelocityBeforeMovement(float DeltaSeconds) override;
    virtual FVector ConstrainAnimRootMotionVelocity(
        const FVector& RootMotionVelocity,
        const FVector& CurrentVelocity) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Lua")
    bool bUseLuaMovementLogic = true;                             // 是否由 Lua 接管移动策略

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Lua")
    FString LuaMovementModuleName = TEXT("Gameplay.Sekiro.Movement.SKMovementComponent"); // Lua Movement 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Root Motion")
    bool bEnableClassicRootMotionDirectionWarping = true;        // 是否允许旧锁定移动强制重定向 RootMotion 水平位移

private:
    // ── 运行时引用与快照 ──────────────────────────────────────

    UPROPERTY()
    TObjectPtr<ACharacter> OwnerCharacter;                       // 所属角色缓存

    UPROPERTY()
    TObjectPtr<USKInputManager> InputManager;                    // 输入组件缓存

    UPROPERTY()
    TObjectPtr<USKCameraManagerComponent> CameraManager;         // 相机组件缓存

    UPROPERTY()
    TObjectPtr<USKCombatComponent> CombatComponent;              // 战斗组件缓存

    UPROPERTY()
    TObjectPtr<USKMotionMatchingTrajectoryComponent> MotionMatchingTrajectoryComponent; // Motion Matching 正式路径标记组件

    float DesiredMoveYawSnapshot = 0.f;                          // Lua 发布的输入目标世界 Yaw
    float MoveDirectionAngleBeforeRotationSnapshot = 0.f;        // Lua 发布的转身前角色局部方向角
    int32 LockOnCardinalDirectionSnapshot = 0;                   // Lua 发布的锁定四方向素材枚举值
    float RootMotionDirectionWarpingWorldYaw = 0.f;              // Lua 发布的动画根位移目标世界 Yaw
    uint32 bHasDesiredMoveYawSnapshot : 1;                        // 当前是否存在有效移动目标快照
    uint32 bHasLockOnLocomotionSnapshot : 1;                      // 当前是否存在有效锁定移动快照
    uint32 bRootMotionDirectionWarpingEnabled : 1;                // 是否重定向动画根位移的水平平移
    mutable FVector PendingRootMotionVelocityWS = FVector::ZeroVector; // 本次物理求值前动画请求的世界速度
    FVector LastRootMotionRequestedTranslationWS = FVector::ZeroVector; // 最近一次动画请求的世界水平位移
    FVector LastRootMotionActualTranslationWS = FVector::ZeroVector; // 最近一次物理求值后的实际世界水平位移
    FVector LastRootMotionCollisionClippedTranslationWS = FVector::ZeroVector; // 请求与实际结果之间的水平裁剪量
    mutable bool bHasPendingRootMotionVelocity = false;           // 当前移动求值是否捕获到动画 RootMotion 速度
    bool bLastRootMotionTranslationClipped = false;               // 最近一次动画 RootMotion 水平位移是否被约束
    FSKRootMotionCoordinationSnapshot RootMotionCoordinationSnapshot; // 当前 Movement Tick 正在写入的协调快照
    FSKRootMotionCoordinationSnapshot CompletedRootMotionCoordinationSnapshot; // 最近完成移动步骤的稳定协调快照
    int64 RootMotionCoordinationSampleId = 0;                      // 下次 Movement Tick 使用的单调采样号
    float FreeRootMotionSteeringRate = 0.f;                        // Free 模式旋转和平移最大修正速度，度/秒
    float LockedRootMotionFacingSteeringRate = 0.f;                // Locked 模式面向最大修正速度，度/秒
    float LockedRootMotionTranslationSteeringRate = 0.f;           // Locked 模式平移方向最大修正速度，度/秒
    float MaxRootMotionSteeringDegreesPerFrame = 0.f;              // 任一 Steering 通道单帧最大修正角
    bool bMotionMatchingActualStateRebasePending = false;          // 等待网络校正应用后从新胶囊状态重建实际基线
    bool bPoseOnlyAirborneMomentumActive = false;                   // PoseOnly 空中阶段是否由 CMC 延续 RootMotion 已产生的水平惯性
    FSKRootMotionOwnerToken ActiveUpperBodyToken;                  // 当前唯一上身姿势令牌，不夺取根运动
    FSKRootMotionOwnerToken ActiveRootMotionOverrideToken;         // 当前 FullBody/Traversal 排他根运动令牌
    FSKRootMotionOwnershipSnapshot RootMotionOwnershipSnapshot;    // 游戏线程发布的权威所有权纯值快照
    int64 RootMotionOwnerTokenSerial = 0;                           // 下一个所有权令牌使用的单调序号

    // ── 通用执行 ──────────────────────────────────────────────

    void RefreshCachedComponents();                              // 刷新角色相关组件缓存
    bool ShouldUsePoseOnlyAirborneMomentum() const;               // 当前物理步是否应屏蔽动画混合残留 RootMotion
    void ReconcileRootMotionOwnership();                         // 清理申请者已销毁的所有权令牌
    void RefreshRootMotionOwnershipSnapshot();                   // 按两个活动通道发布新版本快照
    FSKRootMotionOwnershipResult MakeRootMotionOwnershipResult(
        ESKRootMotionOwnershipResultCode Code,
        const FSKRootMotionOwnerToken& Token) const;              // 构造带当前快照的操作结果
    void BeginRootMotionCoordinationSample(float DeltaSeconds);  // 初始化本次正式协调快照
    float ResolveRootMotionSteeringDelta(
        float CurrentYaw,
        float TargetYaw,
        float MaxDegreesPerSecond,
        float DeltaSeconds) const;                               // 计算受双重上限约束的最短 Yaw 修正
    void ApplyActorYaw(float TargetYaw, float InterpSpeed, float DeltaTime); // 原生执行最短路径 Yaw 插值
    FTransform CaptureAnimationRootMotionPreConvert(
        const FTransform& LocalSpaceRootMotion,
        UCharacterMovementComponent* MovementComponent,
        float DeltaSeconds);                                     // 原样透传并记录动画局部 RootMotion
    FTransform CoordinateRootMotionPostConvert(
        const FTransform& WorldSpaceRootMotion,
        UCharacterMovementComponent* MovementComponent,
        float DeltaSeconds);                                     // 协调并唯一提交世界 RootMotion
};
