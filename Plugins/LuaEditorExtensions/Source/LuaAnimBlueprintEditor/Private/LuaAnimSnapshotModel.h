#pragma once

#include "CoreMinimal.h"

/** 单个活跃动画节点的可视化数据。 */
struct FLuaAnimSnapshotNode
{
    FString NodeType;
    FString RawDebugLine;
    double AbsoluteWeight = 0.0;
    int32 Depth = 0;
    FString ChainId;
    bool bPoseSource = false;
    FString NativeAssetName;
    FString ResolvedAnimationName;
    FString PoseAlias;
    TMap<FString, FString> Inputs;
    FString MachineName;
    FString CurrentState;
    FString PreviousState;
    double BlendAlpha = 0.0;
    TMap<FString, double> StateWeights;
    FString OutputKind;
    FString OutputDerivation;
    TArray<TSharedPtr<FLuaAnimSnapshotNode>> Children;
};

/** 一次 Transition 表达式求值采样。 */
struct FLuaAnimTransitionSample
{
    FString TransitionId;
    FString ExpressionLabel;
    FString ParameterName;
    FString ParameterType;
    FString ParameterValue;
    FString ExpectedValue;
    FString Threshold;
    bool bExpressionResult = false;
    bool bRuleResult = false;
    bool bIsFinal = false;
    FString EvaluatedUtcTimestamp;
};

/** JSONL 文件中的一帧 Lua 动画快照。 */
struct FLuaAnimSnapshotFrame
{
    int32 SchemaVersion = 0;
    int64 FrameIndex = 0;
    FString UtcTimestamp;
    double SessionElapsedSeconds = 0.0;
    FString CaptureReason;
    FString ChangeTitle; // 最靠近 Root 的关键变化标题
    FString ChangeDescription; // 左侧显示的一句话摘要
    FString ChangeDetails; // 完整变化内容，供搜索和悬停
    FString AnimInstancePath;
    FString LuaModuleName;
    TMap<FString, FString> Variables;
    TMap<FString, double> Curves;
    TArray<TSharedPtr<FLuaAnimSnapshotNode>> Roots;
    TArray<FLuaAnimTransitionSample> Transitions;
};

/** 一个 JSONL 快照文件的容错加载结果。 */
struct FLuaAnimSnapshotDocument
{
    FString SourceFilePath;
    TArray<TSharedPtr<FLuaAnimSnapshotFrame>> Frames;
    TArray<FString> Warnings;
};
