#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/SKAttributeTypes.h"
#include "SKSurvivalTypes.generated.h"

class USKSurvivalComponent;

UENUM(BlueprintType)
enum class ESKLifeState : uint8 { Uninitialized, Alive, Dying, Dead, Reviving };

UENUM(BlueprintType)
enum class ESKSurvivalTransitionKind : uint8 { None, Death, Revive, PostureBreak };

UENUM(BlueprintType)
enum class ESKSurvivalResultCode : uint8
{
    Applied, NoChange, NotReady, InvalidState, InvalidInput, StaleTransition, Blocked, Reentrant, ResourceCommitFailed
};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKSurvivalTransitionToken
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<USKSurvivalComponent> Owner; // 令牌所属弱组件，禁止跨角色

    UPROPERTY(BlueprintReadOnly)
    int64 LifeSerial = 0; // 生命轮次

    UPROPERTY(BlueprintReadOnly)
    int64 TransitionSerial = 0; // 过程或崩溃序号

    UPROPERTY(BlueprintReadOnly)
    ESKSurvivalTransitionKind Kind = ESKSurvivalTransitionKind::None; // 过程类型

};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKSurvivalSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bReady = false; // 快照是否有效

    UPROPERTY(BlueprintReadOnly)
    ESKLifeState LifeState = ESKLifeState::Uninitialized; // 生命状态

    UPROPERTY(BlueprintReadOnly)
    int64 LifeSerial = 0; // 生命轮次

    UPROPERTY(BlueprintReadOnly)
    bool bPostureBroken = false; // 躯干流程状态，不是数值副本

    UPROPERTY(BlueprintReadOnly)
    FSKAttributeSnapshot Attributes; // GAS 当前属性副本，只读事件数据

};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKSurvivalTransitionEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FSKSurvivalTransitionToken Token; // 本次过程令牌

    UPROPERTY(BlueprintReadOnly)
    FName Reason = NAME_None; // 初始化、伤害、死亡等事件原因

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> SourceActor = nullptr; // 可空来源，不推测击杀者

    UPROPERTY(BlueprintReadOnly)
    FSKSurvivalSnapshot Snapshot; // 发布时最终状态与资源

};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKSurvivalTransitionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    ESKSurvivalResultCode Code = ESKSurvivalResultCode::NotReady; // 转换结果

    UPROPERTY(BlueprintReadOnly)
    FSKSurvivalTransitionToken Token; // 当前或完成的过程令牌

    UPROPERTY(BlueprintReadOnly)
    FSKSurvivalSnapshot Snapshot; // 操作结束实际快照

};

USTRUCT(BlueprintType)
struct SEKIRO_API FSKSurvivalImpactResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FSKNumericResult Numeric; // ASC 复合执行实际结果及逐通道原因

    UPROPERTY(BlueprintReadOnly)
    FSKSurvivalSnapshot Snapshot; // 提交后的状态

    UPROPERTY(BlueprintReadOnly)
    bool bDeathStarted = false; // 本次是否首次致死

    UPROPERTY(BlueprintReadOnly)
    bool bPostureBroken = false; // 本次是否首次进入崩溃

};
