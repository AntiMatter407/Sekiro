#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_StateResult.h"
#include "SekiroLuaAnimTypes.h"
#include "AnimNode_SekiroLuaStateMachine.generated.h"

struct FSekiroLuaPoseGraphRuntimeState;

struct FSekiroLuaSequencePlayerNode_Standalone : public FAnimNode_SequencePlayer_Standalone
{
    /** 作用：按显式 reset serial 重置原生播放器并保留首帧 Tick 区间。@param StartTime float，新的播放时间，单位为秒。@return void，无返回值。 */
    void ResetPlayback(float StartTime);
};

struct FSekiroLuaStateResultNode_Standalone : public FAnimNode_StateResult
{
    FName NodeName = NAME_None;           // Lua 图内 StateResult 节点名称
    FName StateName = NAME_None;          // StateResult 所属状态名称
    FSekiroLuaPoseNodeReference InputNode; // Result 当前连接的通用输入节点

    /** 作用：输出 StateResult 到输入节点的调试链。@param DebugData FNodeDebugData&，节点调试数据构建器。@return void，无返回值。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
};

struct FSekiroLuaActiveTransition
{
    FSekiroLuaPoseLink PreviousPose;     // 过渡源 PoseLink
    FSekiroLuaPoseLink NextPose;         // 过渡目标 PoseLink
    float CrossfadeDuration = 0.0f;      // 标准交叉混合总时长，单位为秒
    float ElapsedTime = 0.0f;            // 已进行的交叉混合时间，单位为秒
    float Alpha = 1.0f;                  // 目标 Pose 权重
    ESekiroLuaRootMotionRotationMode PreviousRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 源节点根运动旋转策略
    ESekiroLuaRootMotionRotationMode NextRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 目标节点根运动旋转策略

    /** 作用：按动画线程 DeltaTime 推进过渡。@param DeltaSeconds float，本帧时间，单位为秒。@return void，无返回值。 */
    void Advance(float DeltaSeconds)
    {
        ElapsedTime = FMath::Min(ElapsedTime + FMath::Max(0.0f, DeltaSeconds), CrossfadeDuration);
        Alpha = CrossfadeDuration > UE_KINDA_SMALL_NUMBER
            ? FMath::Clamp(ElapsedTime / CrossfadeDuration, 0.0f, 1.0f)
            : 1.0f;
    }

    /** 作用：检查交叉混合是否完成。@param 无。@return bool，目标权重达到 1 时为 true。 */
    bool IsComplete() const
    {
        return Alpha >= 1.0f - UE_KINDA_SMALL_NUMBER;
    }
};

USTRUCT(BlueprintInternalUseOnly)
struct SEKIROANIMBLUEPRINTEXT_API FAnimNode_SekiroLuaStateMachine : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    FName LayerName = NAME_None;          // Lua 动画层名称，留空时读取默认层

    /** 作用：初始化节点及游戏线程已准备的稳定拓扑。@param Context const FAnimationInitializeContext&，动画初始化上下文。@return void，无返回值。 */
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    /** 作用：把骨骼缓存阶段转发到稳定 StateResult 链。@param Context const FAnimationCacheBonesContext&，骨骼缓存上下文。@return void，无返回值。 */
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    /** 作用：读取 Proxy 不可变快照、推进原生过渡并唯一更新活动叶节点。@param Context const FAnimationUpdateContext&，动画更新上下文。@return void，无返回值。 */
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    /** 作用：求值并顺序混合活动状态 Pose。@param Output FPoseContext&，输出姿势上下文。@return void，无返回值。 */
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    /** 作用：输出状态、资产和活动过渡调试数据。@param DebugData FNodeDebugData&，节点调试数据构建器。@return void，无返回值。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

    /** 作用：在游戏线程按稳定 NodeId 准备拓扑并搬迁原生播放器状态。@param AnimInstanceProxy FAnimInstanceProxy*，节点所属代理。@param PoseGraph const FSekiroLuaPoseGraphSnapshot&，最新不可变图快照。@return void，无返回值。 */
    void PreparePoseGraphTopology(FAnimInstanceProxy* AnimInstanceProxy, const FSekiroLuaPoseGraphSnapshot& PoseGraph);
    /** 作用：在 PostUpdate 收集活动节点、原生时间和过渡状态。@param OutRuntimeState FSekiroLuaPoseGraphRuntimeState&，输出运行时状态。@return void，无返回值。 */
    void CollectRuntimeState(FSekiroLuaPoseGraphRuntimeState& OutRuntimeState) const;

protected:
    /** 作用：检查当前原生拓扑是否匹配快照版本。@param PoseGraph const FSekiroLuaPoseGraphSnapshot&，目标拓扑。@return bool，版本和稳定顺序一致时为 true。 */
    bool DoesPoseGraphTopologyMatch(const FSekiroLuaPoseGraphSnapshot& PoseGraph) const;
    /** 作用：在游戏线程为全部 StateResult 建立稳定 FPoseLink。@param 无。@return void，无返回值。 */
    void RebuildPoseGraphLinks();
    /** 作用：同步原生 SequencePlayer 的资产、参数和显式重置请求。@param NodeIndex int32，播放器索引。@param Snapshot const FSekiroLuaSequencePlayerSnapshot&，不可变参数。@return void，无返回值。 */
    void SynchronizePoseGraphNode(int32 NodeIndex, const FSekiroLuaSequencePlayerSnapshot& Snapshot);
    /** 作用：推进原生活动过渡并识别新的 OutputSerial。@param NewSnapshot const FSekiroLuaAnimSnapshot&，本帧快照。@param DeltaSeconds float，本帧时间。@return void，无返回值。 */
    void UpdateActiveTransitions(const FSekiroLuaAnimSnapshot& NewSnapshot, float DeltaSeconds);
    /** 作用：计算状态和共享叶节点最终权重。@param NewSnapshot const FSekiroLuaAnimSnapshot&，本帧快照。@return void，无返回值。 */
    void BuildPoseGraphNodeWeights(const FSekiroLuaAnimSnapshot& NewSnapshot);
    /** 作用：清空原生活动过渡计数并保留预分配缓冲。@param 无。@return void，无返回值。 */
    void ResetActiveTransitions();
    /** 作用：删除已完成过渡前缀且不调整数组容量。@param 无。@return void，无返回值。 */
    void RemoveCompletedTransitions();
    /** 作用：按稳定句柄查找原生 SequencePlayer 索引。@param PoseLink const FSekiroLuaPoseLink&，节点句柄。@return int32，找到时返回索引。 */
    int32 FindPoseGraphSequencePlayerIndex(const FSekiroLuaPoseLink& PoseLink) const;
    /** 作用：按稳定句柄查找原生 StateResult 索引。@param PoseLink const FSekiroLuaPoseLink&，节点句柄。@return int32，找到时返回索引。 */
    int32 FindPoseGraphStateResultIndex(const FSekiroLuaPoseLink& PoseLink) const;
    /** 作用：按通用输入引用解析 StateResult 的 SequencePlayer。@param PoseLink const FSekiroLuaPoseLink&，StateResult 句柄。@return int32，当前类型受支持时返回播放器索引。 */
    int32 FindPoseGraphStateResultInputSequencePlayerIndex(const FSekiroLuaPoseLink& PoseLink) const;

    const FSekiroLuaAnimSnapshot* CachedSnapshot = nullptr; // 当前更新周期的 Proxy 不可变快照地址
    TArray<FSekiroLuaSequencePlayerNode_Standalone> PoseGraphSequencePlayers; // 按 NodeId 排序的原生播放器
    TArray<FSekiroLuaStateResultNode_Standalone> PoseGraphStateResults; // 按 NodeId 排序的原生状态根节点
    TArray<FPoseLink> StatePoseLinks;     // 宿主到 StateResult 的稳定 FPoseLink
    TArray<int32> PoseGraphSequencePlayerNodeIds; // 与播放器数组一一对应的稳定 NodeId
    TArray<int32> PoseGraphStateResultNodeIds; // 与状态根数组一一对应的稳定 NodeId
    TArray<FSekiroLuaPoseNodeReference> PoseGraphStateResultInputs; // 每个 StateResult 的通用输入引用
    TArray<int32> PoseGraphResetSerials;  // 原生播放器已消费的 reset serial
    TArray<FSekiroLuaActiveTransition> ActiveTransitionSlots; // 游戏线程按状态数预分配的活动过渡缓冲
    TArray<float> PoseGraphStateResultWeights; // 每个 StateResult 的最终权重
    TArray<ESekiroLuaRootMotionRotationMode> PoseGraphStateResultRootMotionRotationModes; // 每个 StateResult 的根旋转策略
    TArray<float> PoseGraphSequencePlayerWeights; // 聚合到每个共享叶节点的最终权重
    TArray<ESekiroLuaRootMotionRotationMode> PoseGraphSequencePlayerRootMotionRotationModes; // 每个叶节点的主导根旋转策略
    TArray<float> DominantStateResultWeights; // 权重构建时复用的主导状态缓冲
    FSekiroLuaPoseLink LastPublishedCurrentPose; // 上次消费的根输出
    int32 ActiveTransitionCount = 0;      // 活动过渡缓冲有效元素数量
    int32 PoseGraphGeneration = 0;        // 当前原生拓扑图代次
    int32 PoseGraphTopologySerial = 0;    // 当前原生拓扑版本
    int32 LastOutputSerial = 0;           // 上次消费的根输出切换序号
    ESekiroLuaRootMotionRotationMode DominantRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 本帧主导根旋转策略
    float DominantRootMotionWeight = 0.0f; // 本帧主导根运动叶节点权重
    bool bTopologyInitialized = false;    // 稳定链接是否已完成动画节点初始化
    bool bWarnedMissingInertializationRequester = false; // 缺少惯性化节点时避免重复告警
};
