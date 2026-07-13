#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "SekiroLuaAnimTypes.h"

class USekiroLuaAnimInstance;

struct FSekiroLuaSequencePlayerRuntimeState
{
    FSekiroLuaPoseLink PoseLink;          // 原生播放器对应的稳定句柄
    float CurrentTime = 0.0f;             // 原生 Tick 完成后的播放时间
};

struct FSekiroLuaPoseGraphRuntimeState
{
    FName LayerName = NAME_None;          // 运行时状态所属动画层
    int32 Generation = 0;                 // 运行时状态所属图代次
    TArray<int32> ActiveNodeIds;          // 当前输出或活动过渡链内仍有权重的节点编号
    TArray<FSekiroLuaSequencePlayerRuntimeState> SequencePlayers; // 原生播放器时间回传
    FSekiroLuaPoseLink PreviousPose;      // 最新原生过渡源
    float TransitionTime = 0.0f;          // 最新原生过渡总时间
    float TransitionElapsedTime = 0.0f;   // 最新原生过渡已用时间
    float TransitionAlpha = 1.0f;         // 最新原生过渡目标权重
    ESekiroLuaRootMotionRotationMode DominantRootMotionRotationMode = ESekiroLuaRootMotionRotationMode::Extract; // 主导叶节点根旋转策略
    float DominantRootMotionWeight = 0.0f; // 主导叶节点根运动权重
};

struct FSekiroLuaAnimProxyLayerSnapshot
{
    FName LayerName = NAME_None;          // 不可变快照所属动画层
    FSekiroLuaAnimSnapshot Snapshot;      // 游戏线程在并行动画更新前发布的完整副本
};

struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimInstanceProxy : public FAnimInstanceProxy
{
public:
    /** 作用：构造空代理。@param 无。@return 无。 */
    FSekiroLuaAnimInstanceProxy();
    /** 作用：为指定动画实例构造代理。@param AnimInstance UAnimInstance*，代理所属动画实例。@return 无。 */
    explicit FSekiroLuaAnimInstanceProxy(UAnimInstance* AnimInstance);

    /** 作用：在游戏线程发布不可变层快照并准备稳定原生拓扑。@param AnimInstance USekiroLuaAnimInstance&，快照来源。@return void，无返回值。 */
    void PublishSnapshots(USekiroLuaAnimInstance& AnimInstance);
    /** 作用：按层读取当前更新周期的不可变快照。@param LayerName FName，目标层，None 使用默认层。@return const FSekiroLuaAnimSnapshot*，存在时返回代理持有地址。 */
    const FSekiroLuaAnimSnapshot* FindLayerSnapshot(FName LayerName) const;

    /** 作用：在游戏线程回传原生时间、活动链和过渡状态。@param InAnimInstance UAnimInstance*，代理所属实例。@return void，无返回值。 */
    virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;

private:
    /** 作用：从全部 Lua 状态机节点收集本帧原生运行状态。@param InAnimInstance UAnimInstance&，节点容器。@return void，无返回值。 */
    void CollectRuntimeStates(UAnimInstance& InAnimInstance) const;
    /** 作用：按 NodeId 合并同层活动节点与播放器时间。@param RuntimeState const FSekiroLuaPoseGraphRuntimeState&，单个宿主节点状态。@return void，无返回值。 */
    void MergeRuntimeState(const FSekiroLuaPoseGraphRuntimeState& RuntimeState) const;
    /** 作用：按主导策略处理原生 Tick 已累计的根旋转。@param 无。@return void，无返回值。 */
    void ApplyRootMotionRotationPolicy() const;

    FName DefaultLayerName = NAME_None;   // LayerName 为空时采用的默认层
    TArray<FSekiroLuaAnimProxyLayerSnapshot> PublishedLayerSnapshots; // 动画线程只读的按层不可变快照
    mutable TArray<FSekiroLuaPoseGraphRuntimeState> RuntimeLayerStates; // PostUpdate 在游戏线程收集的原生状态
};
