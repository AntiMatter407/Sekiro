#pragma once

#include "CoreMinimal.h"
#include "SekiroAnimLogicIR.generated.h"

// ============================================================================
// TAE→UE 动画逻辑中间表示（ABIR: Animation Blueprint Intermediate Representation）
// 从 TAE JSON 提取，用于生成 AnimBlueprint / AnimNotify / DataAsset
// ============================================================================

// ── 事件分类枚举 ──────────────────────────────────────────────
UENUM()
enum class ESKTAEEventCategory : uint8
{
    Unknown,
    JumpTable,             // Type 0: 状态机分支
    AttackBehavior,        // Type 1: 攻击盒+伤害
    BulletBehavior,        // Type 2: 射弹/飞行道具
    CommonBehavior,        // Type 5: 通用行为（踩踏等）
    AddSpEffect,           // Type 66/67: 状态效果
    SpawnFFX,              // Type 96/114-122: 视觉特效
    PlaySound,             // Type 128-132: 音效
    RumbleCam,             // Type 144-147: 镜头震动
    Blend,                 // Type 16: 动画混合
    FootStep,              // Type 400-403: 脚步声
    BehaviorFlag,          // Type 300/301: 行为标志
    FacialExpression,      // Type 607: 面部表情
    CameraModule,          // Type 1xx: 镜头相关
    SetTurnSpeed,          // 转向速度
    Other
};

// ── JumpTable 动作类型 ────────────────────────────────────────
UENUM()
enum class ESKJumpTableAction : uint8
{
    None,
    AnimCancelStart_R1,       // JumpTableID 1: 允许取消到R1(轻攻击)
    AnimCancelStart_L1,       // JumpTableID 9: 允许取消到L1(防御)
    AnimCancelStart_Guard,    // JumpTableID 21: 允许取消到格挡
    AnimCancelStart_Dodge,    // JumpTableID 25: 允许取消到闪避
    AnimCancelStart_Item,     // JumpTableID 30: 允许取消到道具
    AnimCancelStart_L2,       // JumpTableID 105: 允许取消到L2(义手)
    AnimCancelStart_Emergency, // JumpTableID 111: 紧急闪避
    AnimCancelEnd_General,    // JumpTableID 34: 通用取消窗口结束
    AnimCancelEnd_R1,         // JumpTableID 115: R1取消窗口结束
    AnimCancelEnd_L1,         // JumpTableID 117: L1取消窗口结束
    AnimCancelEnd_L2,         // JumpTableID 118: L2取消窗口结束
    AnimCancelEnd_Item,       // JumpTableID 107: 道具取消窗口结束
    AnimCancelEnd_Emergency,  // JumpTableID 112: 紧急闪避取消窗口结束
    DisableTurning,           // JumpTableID 7
    FlagAsDodging,            // JumpTableID 8
    InvokeDeath,              // JumpTableID 12
    DisableMapHit,            // JumpTableID 19
    SetNoGravity,             // JumpTableID 27
    DisableAllMovement,       // JumpTableID 89
    LimitMoveSpeedWalk,       // JumpTableID 90
    LimitMoveSpeedDash,       // JumpTableID 91
    EnableParry,              // JumpTableID 119: 弹刀模式
    InvokeAttackAction,       // JumpTableID 87
    SetHeightCorrection,      // JumpTableID 113
};

// ── ABIR: 单个 TAE 事件 ───────────────────────────────────────
USTRUCT()
struct FSKTAEEventIR
{
    GENERATED_BODY()

    UPROPERTY() int32 Type = 0;
    UPROPERTY() FString TypeName;
    UPROPERTY() float StartTime = 0.f;
    UPROPERTY() float EndTime = 0.f;
    UPROPERTY() int32 StartFrame = 0;
    UPROPERTY() int32 EndFrame = 0;
    UPROPERTY() ESKTAEEventCategory Category = ESKTAEEventCategory::Unknown;

    // 模板参数（仅名字映射的关键参数）
    UPROPERTY() TMap<FString, FString> Params;
};

// ── ABIR: 取消窗口 ───────────────────────────────────────────
USTRUCT()
struct FSKCancelWindowIR
{
    GENERATED_BODY()

    UPROPERTY() int32 StartFrame = 0;
    UPROPERTY() int32 EndFrame = 0;
    UPROPERTY() FName TargetAction;        // "Attack", "Guard", "Dodge", "Item", "Prosthetic"...
    UPROPERTY() ESKJumpTableAction JumpAction = ESKJumpTableAction::None;
    UPROPERTY() float CrossfadeDuration = 0.1f;
};

// ── ABIR: 攻击盒配置 ─────────────────────────────────────────
USTRUCT()
struct FSKAttackHitboxIR
{
    GENERATED_BODY()

    UPROPERTY() int32 StartFrame = 0;
    UPROPERTY() int32 EndFrame = 0;
    UPROPERTY() int32 BehaviorJudgeID = 0;
    UPROPERTY() int32 AttackType = 0;       // 0=Standard, 2=ForwardR1, 62=Plunging, 64=Parry
    UPROPERTY() int32 Source = 0;           // 0=Default, 1=Right Hand, 2=Left Hand
};

// ── ABIR: SpEffect 应用 ──────────────────────────────────────
USTRUCT()
struct FSKSpEffectIR
{
    GENERATED_BODY()

    UPROPERTY() int32 StartFrame = 0;
    UPROPERTY() int32 EndFrame = 0;
    UPROPERTY() int32 SpEffectID = 0;
    UPROPERTY() bool bIsMultiplayerOnly = false;
};

// ── ABIR: 音效事件 ───────────────────────────────────────────
USTRUCT()
struct FSKSEventIR
{
    GENERATED_BODY()

    UPROPERTY() int32 Frame = 0;
    UPROPERTY() int32 SoundType = 0;
    UPROPERTY() int32 SoundID = 0;
    UPROPERTY() int32 DummyPolyID = -1;
};

// ── ABIR: 视觉特效事件 ───────────────────────────────────────
USTRUCT()
struct FSKFFXEventIR
{
    GENERATED_BODY()

    UPROPERTY() int32 StartFrame = 0;
    UPROPERTY() int32 EndFrame = 0;
    UPROPERTY() int32 FFXID = 0;
    UPROPERTY() int32 DummyPolyID = -1;
    UPROPERTY() bool bIsFollowDummyPoly = false;
    UPROPERTY() bool bIsRepeat = false;
};

// ── ABIR: 单个动画的完整逻辑 ──────────────────────────────────
USTRUCT()
struct FSKAnimationLogicIR
{
    GENERATED_BODY()

    UPROPERTY() int32 AnimID = 0;
    UPROPERTY() FString AnimName;                         // 由 AnimationNameMap 翻译

    // 事件分类
    UPROPERTY() TArray<FSKTAEEventIR> AllEvents;
    UPROPERTY() TArray<FSKCancelWindowIR> CancelWindows;   // JumpTable提取的取消窗口
    UPROPERTY() TArray<FSKAttackHitboxIR> AttackHitboxes;  // 攻击盒
    UPROPERTY() TArray<FSKSpEffectIR> SpEffects;           // 状态效果
    UPROPERTY() TArray<FSKSEventIR> SoundEvents;           // 音效
    UPROPERTY() TArray<FSKFFXEventIR> FFXEvents;           // 视觉特效

    // 推断
    UPROPERTY() bool bIsLooping = false;
    UPROPERTY() int32 TotalFrames = 0;                     // 从最大事件帧推断
    UPROPERTY() FString InferredCategory;                  // "Locomotion", "Attack", "Deflect", "Dodge", "Hit", "Death", etc.
    UPROPERTY() int32 ComboNextAnimID = -1;                // 连段下一动画ID
};

// ── ABIR: 状态机过渡规则 ──────────────────────────────────────
USTRUCT()
struct FSKSMTransitionIR
{
    GENERATED_BODY()

    UPROPERTY() FName FromState;
    UPROPERTY() FName ToState;
    UPROPERTY() int32 FromAnimID = -1;
    UPROPERTY() int32 ToAnimID = -1;
    UPROPERTY() int32 CancelWindowStart = 0;
    UPROPERTY() int32 CancelWindowEnd = 0;
    UPROPERTY() float CrossfadeDuration = 0.1f;
    UPROPERTY() ESKJumpTableAction TriggerAction = ESKJumpTableAction::None;
    UPROPERTY() int32 Priority = 0;                        // 高优先级打断低优先级
};

// ── ABIR: 完整状态机定义 ─────────────────────────────────────
USTRUCT()
struct FSKStateMachineIR
{
    GENERATED_BODY()

    UPROPERTY() FString StateMachineName;
    UPROPERTY() TArray<FSKAnimationLogicIR> AnimStates;
    UPROPERTY() TArray<FSKSMTransitionIR> Transitions;

    // 按类别分组的动画ID（非UPROPERTY — 仅C++内部使用）
    TMap<FString, TArray<int32>> AnimIDsByCategory;
};

// ── ABIR: 完整导入结果 ────────────────────────────────────────
USTRUCT()
struct FSKAnimLogicImportResult
{
    GENERATED_BODY()

    UPROPERTY() int32 TotalTaeFiles = 0;
    UPROPERTY() int32 TotalAnims = 0;
    UPROPERTY() int32 TotalEvents = 0;
    UPROPERTY() TMap<int32, FSKAnimationLogicIR> AnimLogicMap;  // AnimID → Logic
    UPROPERTY() FSKStateMachineIR MainStateMachine;
};
