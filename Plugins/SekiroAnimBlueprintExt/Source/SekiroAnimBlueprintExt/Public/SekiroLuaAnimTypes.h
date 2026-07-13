#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "SekiroLuaAnimTypes.generated.h"

class UAnimSequenceBase;

UENUM(BlueprintType)
enum class ESekiroLuaRootMotionRotationMode : uint8
{
    Extract UMETA(DisplayName = "Extract"),
    Ignore UMETA(DisplayName = "Ignore"),
    WarpToTarget UMETA(DisplayName = "Warp To Target"),
    SteerToTarget UMETA(DisplayName = "Steer To Target"),
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaRootMotionRotationPolicy
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    ESekiroLuaRootMotionRotationMode Mode = ESekiroLuaRootMotionRotationMode::Extract; // 根运动旋转策略

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float TargetWorldYaw = 0.0f;         // 游戏线程最终应用旋转时使用的绝对世界偏航角

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0"))
    float MaxYawRate = 0.0f;             // 最大转向角速度，单位为度每秒

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float CompletionTimeSeconds = 0.0f;  // Warp 完成时间，非正数表示使用完整动画时长
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaOrientationWarpingPolicy
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bEnabled = false;               // 是否启用当前动画层的方向扭转

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float OrientationAngle = 0.0f;       // Manual 模式方向扭转角度，单位为度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WarpingAlpha = 1.0f;           // 方向扭转强度

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bWarpRootMotionTranslation = false; // 是否由外部 Movement 旋转最终 Root Motion 平移

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float RootMotionTranslationAngle = 0.0f; // 素材主方向到精确目标方向的相对角，单位为度
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimState
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName StateName = NAME_None;          // 状态名称

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> AnimationAsset = nullptr; // 状态动画资产

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0"))
    float BlendTime = 0.15f;              // 进入混合时间

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PlayRate = 1.0f;                // 默认播放速率

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bLoop = true;                    // 是否循环播放
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimDecision
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FName StateName = NAME_None;          // 目标状态

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> AnimationAsset = nullptr; // 目标动画资源

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FString AnimationPath;                // 目标动画资源路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FName AnimationName = NAME_None;      // Lua 动画别名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float BlendTime = 0.15f;              // 进入混合时间

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    float PlayRate = 1.0f;                // 播放速率倍率

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bLoop = true;                    // 是否循环播放

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bResetTime = false;              // 是否重置时间

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float StartPosition = 0.0f;           // 重置时间时使用的归一化起播位置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bUseStartPosition = false;       // 是否使用指定起播位置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    FVector BlendInput = FVector::ZeroVector; // BlendSpace 输入参数

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation")
    bool bUseInertialization = false;     // 本次姿势切换是否请求 UE 原生惯性化

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sekiro|Lua Animation", meta = (ClampMin = "0.0"))
    float InertialBlendTime = 0.15f;      // 原生惯性化持续时间，不作为线性交叉混合时间
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaPoseLink
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 NodeId = INDEX_NONE;            // Lua 到 C++ 的稳定节点句柄编号，不是 UE FPoseLink

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 Generation = 0;                 // 句柄所属图代次，用于拒绝旧句柄，不参与 Pose 转发

    /** 作用：检查该 PoseLink 是否包含可解析的节点编号和图代次。@param 无。@return bool，NodeId 与 Generation 均有效时为 true。 */
    bool IsValid() const
    {
        return NodeId != INDEX_NONE && Generation > 0;
    }

    /** 作用：比较两个 PoseLink 是否指向同一代图中的同一个节点。@param Other const FSekiroLuaPoseLink&，待比较句柄。@return bool，节点编号和图代次完全相同时为 true。 */
    bool operator==(const FSekiroLuaPoseLink& Other) const
    {
        return NodeId == Other.NodeId && Generation == Other.Generation;
    }

    /** 作用：比较两个 PoseLink 是否不同。@param Other const FSekiroLuaPoseLink&，待比较句柄。@return bool，节点编号或图代次不同时为 true。 */
    bool operator!=(const FSekiroLuaPoseLink& Other) const
    {
        return !(*this == Other);
    }
};

UENUM(BlueprintType)
enum class ESekiroLuaPoseNodeType : uint8
{
    None UMETA(DisplayName = "None"),
    SequencePlayer UMETA(DisplayName = "Sequence Player"),
    StateResult UMETA(DisplayName = "State Result"),
    Blend UMETA(DisplayName = "Blend"),
    StateMachine UMETA(DisplayName = "State Machine"),
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaPoseNodeReference
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink PoseLink;          // 输入节点的稳定句柄

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    ESekiroLuaPoseNodeType NodeType = ESekiroLuaPoseNodeType::None; // 输入节点类型，供原生路由和后续节点扩展使用

    /** 作用：检查节点引用是否包含有效句柄和明确类型。@param 无。@return bool，引用可由原生 PoseGraph 解析时为 true。 */
    bool IsValid() const
    {
        return PoseLink.IsValid() && NodeType != ESekiroLuaPoseNodeType::None;
    }
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaSequencePlayerSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink PoseLink;           // 当前节点的 Lua 稳定句柄，原生连接由 FPoseLink 单独持有

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FName NodeName = NAME_None;            // Lua 图内可读节点名称

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    TObjectPtr<UAnimSequenceBase> Sequence = nullptr; // 游戏线程预先解析的动画序列

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FName AnimationName = NAME_None;       // Lua 资产别名，用于动画调试输出

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float CurrentTime = 0.0f;              // 播放器当前时间，单位为秒

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float PlaybackTargetTime = 0.0f;       // reset serial 变化时原生播放器采用的目标时间，单位为秒

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 PlaybackResetSerial = 0;          // 显式重置播放时间时递增，动画线程据此重置原生播放器状态

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float PlayRate = 1.0f;                 // 播放倍率

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    bool bLoop = true;                     // 是否循环播放
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaStateResultSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink PoseLink;           // 当前 StateResult 的 Lua 稳定句柄

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FName NodeName = NAME_None;            // Lua 图内可读节点名称

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FName StateName = NAME_None;           // StateResult 所属状态名称

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseNodeReference InputNode; // Result 的通用输入引用，当前运行时支持 SequencePlayer
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaPoseGraphSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 Generation = 0;                  // 图代次

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 TopologySerial = 0;              // 本层拓扑版本，仅在节点或连接变化时递增

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    int32 OutputSerial = 0;                // 根输出切换序号，由原生节点识别新过渡

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    TArray<FSekiroLuaSequencePlayerSnapshot> SequencePlayers; // 本层持久 SequencePlayer 参数副本

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    TArray<FSekiroLuaStateResultSnapshot> StateResults; // 本层持久 StateResult 拓扑副本

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink CurrentPose;         // 当前输出 PoseLink

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseLink PreviousPose;        // 普通过渡源 PoseLink

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float TransitionTime = 0.0f;            // 过渡总时间，单位为秒

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float TransitionElapsedTime = 0.0f;     // 过渡已用时间，单位为秒

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    float TransitionAlpha = 1.0f;           // 当前 Pose 权重，范围为 [0,1]

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    bool bHasOutputPose = false;             // 是否已发布有效输出 Pose

    /** 作用：按 PoseLink 查找同代 SequencePlayer 参数。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return const FSekiroLuaSequencePlayerSnapshot*，找到时返回节点参数地址，否则返回 nullptr。 */
    const FSekiroLuaSequencePlayerSnapshot* FindSequencePlayer(const FSekiroLuaPoseLink& PoseLink) const
    {
        if (!PoseLink.IsValid() || PoseLink.Generation != Generation) return nullptr;

        for (const FSekiroLuaSequencePlayerSnapshot& SequencePlayer : SequencePlayers)
        {
            if (SequencePlayer.PoseLink == PoseLink) return &SequencePlayer;
        }

        return nullptr;
    }

    /** 作用：按 PoseLink 查找同代 StateResult 描述。@param PoseLink const FSekiroLuaPoseLink&，目标节点句柄。@return const FSekiroLuaStateResultSnapshot*，找到时返回节点描述地址，否则返回 nullptr。 */
    const FSekiroLuaStateResultSnapshot* FindStateResult(const FSekiroLuaPoseLink& PoseLink) const
    {
        if (!PoseLink.IsValid() || PoseLink.Generation != Generation) return nullptr;

        for (const FSekiroLuaStateResultSnapshot& StateResult : StateResults)
        {
            if (StateResult.PoseLink == PoseLink) return &StateResult;
        }

        return nullptr;
    }
};

USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimSnapshot
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation|Pose Graph")
    FSekiroLuaPoseGraphSnapshot PoseGraph; // Lua Pose Graph 的线程安全参数快照

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName CurrentStateName = NAME_None;   // 当前状态

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName PreviousStateName = NAME_None;  // 上一个状态

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> CurrentAnimationAsset = nullptr; // 当前动画资源

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    TObjectPtr<UAnimationAsset> PreviousAnimationAsset = nullptr; // 上一个动画资源

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName CurrentAnimationName = NAME_None; // 当前 Lua 动画别名

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FName PreviousAnimationName = NAME_None; // 上一个 Lua 动画别名

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentTime = 0.0f;             // 当前状态时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousTime = 0.0f;            // 上一个状态时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentPlayRate = 1.0f;         // 当前播放速率

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousPlayRate = 1.0f;        // 上一个播放速率

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    ESekiroLuaRootMotionRotationMode CurrentRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 当前根运动旋转策略

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentRootMotionTargetWorldYaw = 0.0f; // 当前姿势根运动目标世界偏航角

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentRootMotionMaxYawRate = 0.0f; // 当前姿势最大转向角速度

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float CurrentRootMotionCompletionTimeSeconds = 0.0f; // 当前姿势 Warp 完成时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    ESekiroLuaRootMotionRotationMode PreviousRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 上一个根运动旋转策略

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousRootMotionTargetWorldYaw = 0.0f; // 上一个姿势根运动目标世界偏航角

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousRootMotionMaxYawRate = 0.0f; // 上一个姿势最大转向角速度

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float PreviousRootMotionCompletionTimeSeconds = 0.0f; // 上一个姿势 Warp 完成时间

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FSekiroLuaOrientationWarpingPolicy OrientationWarpingPolicy; // 当前动画层的方向扭转策略

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendAlpha = 1.0f;              // 混合权重

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendTime = 0.0f;               // 混合总时长

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float BlendElapsedTime = 0.0f;        // 混合已用时

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bCurrentLoop = true;             // 当前是否循环

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bPreviousLoop = true;            // 上一个是否循环

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FVector CurrentBlendInput = FVector::ZeroVector; // 当前 BlendSpace 输入

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    FVector PreviousBlendInput = FVector::ZeroVector; // 上一个 BlendSpace 输入

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bUseInertialization = false;     // 当前切换是否应由下游 Inertialization 节点处理

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    float InertialBlendTime = 0.0f;       // 当前原生惯性化请求时长

    UPROPERTY(BlueprintReadOnly, Category = "Sekiro|Lua Animation")
    bool bHasPose = false;                // 是否有有效姿势
};
