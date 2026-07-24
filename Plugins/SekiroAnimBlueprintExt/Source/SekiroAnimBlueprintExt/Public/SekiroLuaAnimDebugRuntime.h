#pragma once

#include "CoreMinimal.h"

class UAnimInstance;

/** 单个 Transition 表达式或参数的真实求值记录。 */
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimTransitionDebugValue
{
    FString TransitionId;              // 稳定 Transition 标识
    FString ExpressionLabel;           // 表达式显示名
    FString ParameterName;             // 参数名
    FString ParameterType;             // 参数类型
    FString ParameterValue;            // 保真字符串值
    FString ExpectedValue;             // Bool 期望值
    FString Threshold;                 // Float 阈值
    bool bExpressionResult = false;     // 当前表达式结果
    bool bRuleResult = false;           // 所属规则最终结果
    bool bIsFinal = false;              // 是否为规则最终结果记录
    FString EvaluatedUtcTimestamp;      // ISO-8601 UTC 求值时间
};

/** GatherDebugData 解析出的一个动画 Pose 节点。 */
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimDebugNode
{
    FString NodeType;                   // 原生节点类型
    FString RawDebugLine;               // 引擎原始调试行
    float AbsoluteWeight = 0.0f;        // 含父分支后的绝对权重
    int32 Depth = 0;                    // 原始层级深度
    int32 ChainId = 0;                  // 原始 Debug Chain
    bool bPoseSource = false;            // 是否为最终 Pose 来源
    FString MachineName;                // 状态机名，非状态机为空
    FString CurrentState;               // 当前/目标状态
    FString PreviousState;              // 混合中的上一状态
    float BlendAlpha = 1.0f;            // 当前状态归一化权重
    TMap<FString, float> StateWeights;  // relevant 状态权重
    FString NativeAssetName;            // SequencePlayer 原生动画名
    FString ResolvedAnimationName;      // Lua resolver 返回名或原生名
    FString PoseAlias;                  // 快照内稳定 Pose#N
    FString OutputKind;                 // Animation/Blend/Additive/Unknown
    FString OutputDerivation;           // 输出来源描述
    TMap<FString, FString> Inputs;      // 从 raw 可靠解析出的常见输入
    TArray<FSekiroLuaAnimDebugNode> Children; // 所有 relevant 子贡献
};

/** 实时 UI 与 JSONL 共用的不可变帧快照。 */
struct SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimDebugFrame
{
    int32 SchemaVersion = 2;                       // JSONL schema 版本
    uint64 FrameIndex = 0;                         // Session 内递增序号
    FString CaptureReason;                         // Start/Interval/StateChanged/Realtime
    FString UtcTimestamp;                          // ISO-8601 UTC 时间
    double SessionElapsedSeconds = 0.0;            // Session 起点后的秒数
    FString AnimInstancePath;                      // 目标实例路径
    FString LuaModuleName;                         // 当前真实 Lua 模块
    TMap<FString, FString> Variables;              // 全部 Blueprint 可见 AnimInstance 变量
    TMap<FString, float> Curves;                   // 当前实际求值曲线，键包含曲线类型
    TArray<FSekiroLuaAnimDebugNode> Roots;         // 完整 relevant 层级
    TArray<FSekiroLuaAnimTransitionDebugValue> Transitions; // 最近 Transition 求值
};

/** Lua 动画层级屏幕调试与 JSONL 快照记录器。 */
class SEKIROANIMBLUEPRINTEXT_API FSekiroLuaAnimDebugRuntime
{
public:
    static void Startup();
    static void Shutdown();

    /** 任意线程查询当前是否需要采集 Transition 调试值。 */
    static bool IsSamplingEnabled();
    static void RecordAnimInstanceModule(UAnimInstance* AnimInstance, const FString& LuaModuleName);
    /** 从游戏线程或并行动画线程提交 Transition 调试值。 */
    static void RecordTransitionValue(
        UAnimInstance* AnimInstance,
        const FString& TransitionId,
        const FString& ExpressionLabel,
        const FString& ParameterName,
        const FString& ParameterType,
        const FString& ParameterValue,
        const FString& ExpectedValue,
        const FString& Threshold,
        bool bExpressionResult,
        bool bRuleResult,
        bool bIsFinal);
    static bool GetLatestFrame(FSekiroLuaAnimDebugFrame& OutFrame);
    static FString GetSnapshotSessionPath();
    /** Flush 并关闭当前快照 Session，保留最后文件路径。 */
    static void StopSnapshotSession();
    /** PIE/SIE 开始前关闭全部采样并清理上一运行态。 */
    static void ResetForPIEStart();
    /** PIE/SIE 结束时关闭全部采样并清理本次运行态。 */
    static void ResetForPIEEnd();

#if WITH_DEV_AUTOMATION_TESTS
    static void ApplyDebugArgumentsForTesting(const TArray<FString>& Arguments);
    static bool StartSnapshotForTesting(float IntervalSeconds, const FString& OutputPath);
    static bool ApplySnapshotArgumentsForTesting(
        const TArray<FString>& Arguments,
        const FString& OutputPath);
    static void StopSnapshotForTesting();
    static bool IsDebugEnabledForTesting();
    static bool IsSnapshotActiveForTesting();
    static bool IsSnapshotIntervalSamplingEnabledForTesting();
    static float GetSnapshotIntervalForTesting();
    static FSekiroLuaAnimDebugNode ParseDebugLineForTesting(const FString& DebugLine);
    static FString BuildRealtimeTextForTesting(const FSekiroLuaAnimDebugFrame& Frame);
    static void ResetForTesting();
#endif
};
