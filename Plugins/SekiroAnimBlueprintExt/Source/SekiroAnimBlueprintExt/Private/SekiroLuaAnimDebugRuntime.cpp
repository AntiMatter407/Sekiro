#include "SekiroLuaAnimDebugRuntime.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Queue.h"
#include "Containers/StringConv.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "LuaEnv.h"
#include "Misc/ScopeExit.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnLuaModule.h"
#include "UObject/UnrealType.h"
#include "lua.hpp"

namespace SekiroLuaAnimDebugPrivate
{
    constexpr uint64 DebugMessageKey = 0x534B4C5541414E49ull;
    constexpr float DefaultSnapshotInterval = 0.15f;

    enum class EDebugView : uint8
    {
        Off,
        Hierarchy,
        Help,
    };

    struct FFlatNode
    {
        FSekiroLuaAnimDebugNode Node; // 已解析节点
        int32 ParentIndex = INDEX_NONE; // 扁平数组父索引
    };

    struct FPendingTransitionDebugValue
    {
        TWeakObjectPtr<UAnimInstance> AnimInstance; // 产生该结果的动画实例
        FSekiroLuaAnimTransitionDebugValue Value; // 工作线程完成构造的不可变调试值
    };

    struct FRuntimeState
    {
        EDebugView DebugView = EDebugView::Off; // 当前屏幕视图
        TMap<TWeakObjectPtr<UAnimInstance>, FString> LuaModules; // 实例模块映射
        TMap<FString, FString> ResolvedAnimationNames; // 当前 PIE 的 Lua 动画名解析缓存
        TMap<TWeakObjectPtr<UAnimInstance>, TArray<FSekiroLuaAnimTransitionDebugValue>> Transitions; // 最近规则值
        FSekiroLuaAnimDebugFrame LatestFrame; // 供 Editor 读取的最近帧
        bool bHasLatestFrame = false; // 最近帧是否有效
        bool bSnapshotActive = false; // JSONL Session 是否活动
        bool bIntervalSamplingEnabled = false; // 是否同时启用定时采样
        float SnapshotInterval = DefaultSnapshotInterval; // 定时间隔秒
        double SessionStartSeconds = 0.0; // Session 平台起点
        double NextCaptureSeconds = 0.0; // 下次定时采样时间
        double NextInspectionSeconds = 0.0; // Snapshot-only 下次允许重建节点树的时间
        uint64 NextFrameIndex = 0; // 下一个帧序号
        FString PreviousStateSignature; // 上次检测的状态签名
        FString SnapshotPath; // 当前 JSONL 路径
        TUniquePtr<IFileHandle> SnapshotFile; // 当前写入句柄
        IConsoleObject* DebugCommand = nullptr; // Debug 命令句柄
        IConsoleObject* SnapshotCommand = nullptr; // Snapshot 命令句柄
        IConsoleObject* SnapshotStopCommand = nullptr; // Stop 命令句柄
        FTSTicker::FDelegateHandle TickerHandle; // 每帧刷新句柄
    };

    FRuntimeState RuntimeState;
    TAtomic<bool> SamplingEnabledForAnyThread(false); // 工作线程只读的采样开关
    TQueue<FPendingTransitionDebugValue, EQueueMode::Mpsc> PendingTransitionValues; // 并行动画线程待合并记录

    /**
     * 按 Transition 与表达式身份保存最新一次不同的求值，避免采样期间累积逐帧历史。
     * 只能在游戏线程修改目标数组；相同值保持原时间戳，变化值原位替换，不改变稳定显示顺序。
     *
     * @param Values 当前 AnimInstance 的最新 Transition 值集合。
     * @param Value 本次待合并值，函数会移动其字符串所有权。
     */
    void StoreLatestTransitionValue(
        TArray<FSekiroLuaAnimTransitionDebugValue>& Values,
        FSekiroLuaAnimTransitionDebugValue&& Value)
    {
        FSekiroLuaAnimTransitionDebugValue* Existing = Values.FindByPredicate(
            [&Value](const FSekiroLuaAnimTransitionDebugValue& Candidate)
            {
                return Candidate.TransitionId == Value.TransitionId
                    && Candidate.ExpressionLabel == Value.ExpressionLabel
                    && Candidate.bIsFinal == Value.bIsFinal;
            });
        if (Existing == nullptr)
        {
            Values.Add(MoveTemp(Value));
            return;
        }
        if (Existing->ParameterName == Value.ParameterName
            && Existing->ParameterType == Value.ParameterType
            && Existing->ParameterValue == Value.ParameterValue
            && Existing->ExpectedValue == Value.ExpectedValue
            && Existing->Threshold == Value.Threshold
            && Existing->bExpressionResult == Value.bExpressionResult
            && Existing->bRuleResult == Value.bRuleResult)
        {
            return;
        }
        *Existing = MoveTemp(Value);
    }

    /** 根据仅由游戏线程修改的 Debug/Snapshot 状态发布任意线程可读采样开关。 */
    void RefreshSamplingEnabledForAnyThread()
    {
        check(IsInGameThread());
        SamplingEnabledForAnyThread.Store(
            RuntimeState.DebugView == EDebugView::Hierarchy
                || RuntimeState.bSnapshotActive,
            EMemoryOrder::SequentiallyConsistent);
    }

    /**
     * 把并行动画线程提交的 Transition 值合并到游戏线程实例表。
     * 只能在游戏线程采帧或结束 PIE 时调用；失效实例和采样关闭后的残留记录直接丢弃。
     */
    void DrainPendingTransitionValues()
    {
        check(IsInGameThread());
        FPendingTransitionDebugValue Pending;
        while (PendingTransitionValues.Dequeue(Pending))
        {
            if (!SamplingEnabledForAnyThread.Load(EMemoryOrder::SequentiallyConsistent)
                || !Pending.AnimInstance.IsValid())
            {
                continue;
            }
            TArray<FSekiroLuaAnimTransitionDebugValue>& Values =
                RuntimeState.Transitions.FindOrAdd(Pending.AnimInstance);
            StoreLatestTransitionValue(Values, MoveTemp(Pending.Value));
        }
    }

    /** 清空尚未由游戏线程消费的并行动画 Transition 值。 */
    void DiscardPendingTransitionValues()
    {
        check(IsInGameThread());
        FPendingTransitionDebugValue Pending;
        while (PendingTransitionValues.Dequeue(Pending))
        {
        }
    }

    /** 仅移除本功能固定 Key 的屏幕内容。 */
    void RemoveScreenMessage()
    {
        if (GEngine != nullptr) GEngine->RemoveOnScreenDebugMessage(DebugMessageKey);
    }

    /** 优先返回本地玩家 Pawn 的动画实例，否则返回最近登记模块的有效实例。 */
    UAnimInstance* SelectTargetAnimInstance()
    {
        if (GEngine != nullptr)
        {
            const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
            for (const FWorldContext& Context : Contexts)
            {
                UWorld* World = Context.World();
                if (World == nullptr || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)) continue;
                APlayerController* Controller = World->GetFirstPlayerController();
                APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
                USkeletalMeshComponent* Mesh = Pawn != nullptr ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
                UAnimInstance* AnimInstance = Mesh != nullptr ? Mesh->GetAnimInstance() : nullptr;
                if (IsValid(AnimInstance)) return AnimInstance;
            }
        }
        for (TPair<TWeakObjectPtr<UAnimInstance>, FString>& Pair : RuntimeState.LuaModules)
        {
            if (Pair.Key.IsValid()) return Pair.Key.Get();
        }
        return nullptr;
    }

    /** 从原始行首解析原生节点类型。 */
    FString ParseNodeType(const FString& DebugLine)
    {
        if (DebugLine.StartsWith(TEXT("Montage('"))) return TEXT("Montage");
        int32 WeightIndex = INDEX_NONE;
        return DebugLine.FindChar(TEXT('<'), WeightIndex)
            ? DebugLine.Left(WeightIndex).TrimStartAndEnd()
            : TEXT("DebugItem");
    }

    /**
     * 从 UE 动画调试行提取两个固定标记之间的文本，不解释内容。
     * 本函数只处理游戏线程已经复制出的 FString；任一标记缺失时返回空串。
     *
     * @param DebugLine 原始调试行，只读。
     * @param StartMarker 值之前的完整起始标记，不能为空。
     * @param EndMarker 值之后的完整结束标记，不能为空。
     * @return 标记之间的文本；格式不匹配或值为空时返回空串。
     */
    FString ParseDelimitedValue(
        const FString& DebugLine,
        const FString& StartMarker,
        const FString& EndMarker)
    {
        const int32 MarkerIndex = DebugLine.Find(StartMarker, ESearchCase::CaseSensitive);
        if (MarkerIndex == INDEX_NONE) return FString();
        const int32 ValueStart = MarkerIndex + StartMarker.Len();
        const int32 ValueEnd = DebugLine.Find(
            EndMarker,
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            ValueStart);
        return ValueEnd > ValueStart
            ? DebugLine.Mid(ValueStart, ValueEnd - ValueStart)
            : FString();
    }

    /** 从状态分支描述 `(State: Name)` 解析名称，失败返回空串。 */
    FString ParseStateBranchName(const FString& DebugLine)
    {
        const FString Marker(TEXT("(State: "));
        const int32 Start = DebugLine.Find(Marker, ESearchCase::CaseSensitive);
        if (Start == INDEX_NONE) return FString();
        const int32 ValueStart = Start + Marker.Len();
        int32 End = INDEX_NONE;
        if (!DebugLine.FindChar(TEXT(')'), End) || End <= ValueStart) return FString();
        return DebugLine.Mid(ValueStart, End - ValueStart);
    }

    /** 从状态机原始行 `(Machine->Current)` 解析机器与当前状态。 */
    void ParseStateMachine(FSekiroLuaAnimDebugNode& Node)
    {
        if (!Node.NodeType.Contains(TEXT("StateMachine"))) return;
        const int32 Arrow = Node.RawDebugLine.Find(TEXT("->"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (Arrow == INDEX_NONE) return;
        const int32 Open = Node.RawDebugLine.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromEnd, Arrow);
        const int32 Close = Node.RawDebugLine.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Arrow);
        if (Open == INDEX_NONE || Close == INDEX_NONE || Open >= Arrow || Close <= Arrow + 2) return;
        Node.MachineName = Node.RawDebugLine.Mid(Open + 1, Arrow - Open - 1);
        Node.CurrentState = Node.RawDebugLine.Mid(Arrow + 2, Close - Arrow - 2);
    }

    /** 从 SequencePlayer 原始行解析原生资产名。 */
    FString ParseNativeAssetName(const FString& DebugLine)
    {
        const int32 Start = DebugLine.Find(TEXT("('"), ESearchCase::CaseSensitive);
        if (Start == INDEX_NONE) return FString();
        const int32 End = DebugLine.Find(TEXT("' Play Time:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
        return End > Start + 2 ? DebugLine.Mid(Start + 2, End - Start - 2) : FString();
    }

    FString ResolveAnimationName(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        const FString& NativeAssetName);

    /**
     * 解析 Slot 调试行中的槽位名与 Montage 权重，值统一写入节点 Inputs。
     * 仅解析 UE5.2 FAnimNode_Slot 的稳定字段；格式不匹配时保留 RawDebugLine 作为兜底。
     *
     * @param Node 已写入原始行和节点类型的可变节点。
     */
    void ParseSlotInputs(FSekiroLuaAnimDebugNode& Node)
    {
        if (!Node.NodeType.Contains(TEXT("Slot"), ESearchCase::IgnoreCase)) return;
        const FString SlotName = ParseDelimitedValue(
            Node.RawDebugLine,
            TEXT("Slot Name: '"),
            TEXT("'"));
        if (!SlotName.IsEmpty()) Node.Inputs.Add(TEXT("SlotName"), SlotName);

        FString SlotWeight = ParseDelimitedValue(
            Node.RawDebugLine,
            TEXT("Weight:"),
            TEXT("%)"));
        SlotWeight.TrimStartAndEndInline();
        if (!SlotWeight.IsEmpty())
        {
            Node.Inputs.Add(
                TEXT("SlotWeight"),
                FString::SanitizeFloat(FCString::Atof(*SlotWeight) / 100.0f));
        }
    }

    /**
     * 解析 Slot 追加的动态 Montage 叶节点，并把实际动画解析为 Lua 语义名。
     * 本函数不查询战斗组件，只使用 UE GatherDebugData 已确认参与最终 Pose 的 Montage 数据。
     *
     * @param AnimInstance 当前动画实例，只读且必须位于游戏线程；可空时只跳过 Lua 名解析。
     * @param LuaModuleName 当前动画蓝图 Lua 模块名，可为空。
     * @param Node 已写入 Montage 原始行的可变节点。
     */
    void ParseMontageNode(
        UAnimInstance* AnimInstance,
        const FString& LuaModuleName,
        FSekiroLuaAnimDebugNode& Node)
    {
        if (Node.NodeType != TEXT("Montage")) return;
        const FString MontageName = ParseDelimitedValue(
            Node.RawDebugLine,
            TEXT("Montage('"),
            TEXT("')"));
        Node.NativeAssetName = ParseDelimitedValue(
            Node.RawDebugLine,
            TEXT("Anim('"),
            TEXT("')"));
        Node.ResolvedAnimationName = ResolveAnimationName(
            AnimInstance,
            LuaModuleName,
            Node.NativeAssetName);
        if (!MontageName.IsEmpty()) Node.Inputs.Add(TEXT("MontageName"), MontageName);

        const FString Position = ParseDelimitedValue(Node.RawDebugLine, TEXT(" P("), TEXT(")"));
        if (!Position.IsEmpty()) Node.Inputs.Add(TEXT("Position"), Position);
        FString Weight = ParseDelimitedValue(Node.RawDebugLine, TEXT(" W("), TEXT("%)"));
        Weight.TrimStartAndEndInline();
        if (!Weight.IsEmpty())
        {
            Node.Inputs.Add(
                TEXT("MontageWeight"),
                FString::SanitizeFloat(FCString::Atof(*Weight) / 100.0f));
        }
        Node.OutputKind = TEXT("Montage");
        Node.OutputDerivation = Node.ResolvedAnimationName;
    }

    /**
     * 从已加载 Lua 模块调用 ResolveDebugAnimationName，并缓存成功或失败结果。
     * 只能在游戏线程调用；本函数绝不 require 模块，因此不会在 Debug/Snapshot 采样中触发动画蓝图编译。
     *
     * @param AnimInstance 提供当前 UnLua 环境的动画实例；可空，空值直接回退原生名。
     * @param LuaModuleName 已由运行时登记的动画蓝图模块名；不能为空。
     * @param NativeAssetName UE 调试树给出的原生动画资产名；不能为空。
     * @return 已加载模块返回的 Lua 动画名；模块、函数或结果无效时返回原生名，并缓存该回退结果。
     */
    FString ResolveAnimationName(UAnimInstance* AnimInstance, const FString& LuaModuleName, const FString& NativeAssetName)
    {
        if (AnimInstance == nullptr || LuaModuleName.IsEmpty() || NativeAssetName.IsEmpty()) return NativeAssetName;
        check(IsInGameThread());

        const FString CacheKey = LuaModuleName + TEXT("\n") + NativeAssetName;
        const FString* CachedName = RuntimeState.ResolvedAnimationNames.Find(CacheKey);
        if (CachedName != nullptr) return *CachedName;
        auto CacheFallback = [&CacheKey, &NativeAssetName]()
        {
            RuntimeState.ResolvedAnimationNames.Add(CacheKey, NativeAssetName);
            return NativeAssetName;
        };

        IUnLuaModule* UnLuaModule = FModuleManager::GetModulePtr<IUnLuaModule>(TEXT("UnLua"));
        if (UnLuaModule == nullptr || !UnLuaModule->IsActive()) return CacheFallback();
        UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv(AnimInstance);
        if (Environment == nullptr) return CacheFallback();
        lua_State* State = Environment->GetMainState();
        const int32 InitialTop = lua_gettop(State);
        ON_SCOPE_EXIT { lua_settop(State, InitialTop); };

        lua_getglobal(State, "package");
        if (!lua_istable(State, -1)) return CacheFallback();
        lua_getfield(State, -1, "loaded");
        if (!lua_istable(State, -1)) return CacheFallback();
        const FTCHARToUTF8 ModuleUtf8(*LuaModuleName);
        lua_pushlstring(State, ModuleUtf8.Get(), ModuleUtf8.Length());
        lua_rawget(State, -2);
        if (!lua_istable(State, -1)) return CacheFallback();
        if (lua_getfield(State, -1, "ResolveDebugAnimationName") != LUA_TFUNCTION) return CacheFallback();
        const FTCHARToUTF8 NativeUtf8(*NativeAssetName);
        lua_pushlstring(State, NativeUtf8.Get(), NativeUtf8.Length());
        if (lua_pcall(State, 1, 1, 0) != LUA_OK || !lua_isstring(State, -1)) return CacheFallback();
        const FString Resolved = UTF8_TO_TCHAR(lua_tostring(State, -1));
        const FString Result = Resolved.IsEmpty() ? NativeAssetName : Resolved;
        RuntimeState.ResolvedAnimationNames.Add(CacheKey, Result);
        return Result;
    }

    /** 从 raw 中提取可靠的 `Key: Value` 数值输入；找不到时不写入。 */
    void ParseNumericInput(const FString& DebugLine, const FString& Key, TMap<FString, FString>& Inputs)
    {
        const FString Marker = Key + TEXT(": ");
        const int32 Start = DebugLine.Find(Marker, ESearchCase::IgnoreCase);
        if (Start == INDEX_NONE) return;
        const int32 ValueStart = Start + Marker.Len();
        int32 ValueEnd = ValueStart;
        while (ValueEnd < DebugLine.Len())
        {
            const TCHAR Character = DebugLine[ValueEnd];
            if (!(FChar::IsDigit(Character) || Character == TEXT('-') || Character == TEXT('+') || Character == TEXT('.'))) break;
            ++ValueEnd;
        }
        if (ValueEnd > ValueStart) Inputs.Add(Key, DebugLine.Mid(ValueStart, ValueEnd - ValueStart));
    }

    /** 从引擎 FlattenedDebugData 构造保留所有 relevant 分支的树。 */
    void BuildFullDebugTree(UAnimInstance* AnimInstance, const FString& LuaModuleName, TArray<FSekiroLuaAnimDebugNode>& OutRoots)
    {
        OutRoots.Reset();
        if (AnimInstance == nullptr) return;
        FNodeDebugData DebugData(AnimInstance);
        AnimInstance->GatherDebugData(DebugData);
        const TArray<FNodeDebugData::FFlattenedDebugData> Flattened = DebugData.GetFlattenedDebugData();
        TArray<FFlatNode> Nodes;
        Nodes.Reserve(Flattened.Num());
        for (int32 Index = 0; Index < Flattened.Num(); ++Index)
        {
            const FNodeDebugData::FFlattenedDebugData& Source = Flattened[Index];
            if (!FAnimWeight::IsRelevant(Source.AbsoluteWeight)) continue;
            FFlatNode Item;
            Item.Node.NodeType = ParseNodeType(Source.DebugLine);
            Item.Node.RawDebugLine = Source.DebugLine;
            Item.Node.AbsoluteWeight = Source.AbsoluteWeight;
            Item.Node.Depth = Source.Indent;
            Item.Node.ChainId = Source.ChainID;
            Item.Node.bPoseSource = Source.bPoseSource;
            ParseStateMachine(Item.Node);
            ParseNumericInput(Source.DebugLine, TEXT("Play Time"), Item.Node.Inputs);
            ParseNumericInput(Source.DebugLine, TEXT("Blend Weight"), Item.Node.Inputs);
            ParseNumericInput(Source.DebugLine, TEXT("Weight"), Item.Node.Inputs);
            ParseNumericInput(Source.DebugLine, TEXT("Alpha"), Item.Node.Inputs);
            ParseNumericInput(Source.DebugLine, TEXT("Num Poses"), Item.Node.Inputs);
            const FString ActivePose = ParseDelimitedValue(
                Source.DebugLine,
                TEXT("Active: ("),
                TEXT(")"));
            if (!ActivePose.IsEmpty()) Item.Node.Inputs.Add(TEXT("ActivePose"), ActivePose);
            const FString BlendTime = ParseDelimitedValue(
                Source.DebugLine,
                TEXT(" Time "),
                TEXT(")"));
            if (!BlendTime.IsEmpty()) Item.Node.Inputs.Add(TEXT("BlendTime"), BlendTime);
            ParseSlotInputs(Item.Node);
            ParseMontageNode(AnimInstance, LuaModuleName, Item.Node);
            if (Item.Node.NodeType.Contains(TEXT("SequencePlayer")))
            {
                Item.Node.NativeAssetName = ParseNativeAssetName(Source.DebugLine);
                Item.Node.ResolvedAnimationName = ResolveAnimationName(AnimInstance, LuaModuleName, Item.Node.NativeAssetName);
                Item.Node.OutputKind = TEXT("Animation");
                Item.Node.OutputDerivation = Item.Node.ResolvedAnimationName;
            }
            for (int32 Previous = Nodes.Num() - 1; Previous >= 0; --Previous)
            {
                if (Nodes[Previous].Node.ChainId == Item.Node.ChainId || Nodes[Previous].Node.Depth < Item.Node.Depth)
                {
                    Item.ParentIndex = Previous;
                    break;
                }
            }
            Nodes.Add(MoveTemp(Item));
        }

        TArray<TArray<int32>> ChildIndices;
        ChildIndices.SetNum(Nodes.Num());
        TArray<int32> RootIndices;
        for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        {
            if (Nodes[Index].ParentIndex == INDEX_NONE) RootIndices.Add(Index);
            else ChildIndices[Nodes[Index].ParentIndex].Add(Index);
        }

        int32 NextPoseAlias = 1;
        TFunction<FSekiroLuaAnimDebugNode(int32)> BuildNode = [&](const int32 Index)
        {
            FSekiroLuaAnimDebugNode Node = Nodes[Index].Node;
            for (const int32 ChildIndex : ChildIndices[Index]) Node.Children.Add(BuildNode(ChildIndex));

            if (!Node.MachineName.IsEmpty())
            {
                float TotalWeight = 0.0f;
                float CurrentWeight = 0.0f;
                float PreviousWeight = -1.0f;
                for (const FSekiroLuaAnimDebugNode& Child : Node.Children)
                {
                    const FString StateName = ParseStateBranchName(Child.RawDebugLine);
                    if (StateName.IsEmpty()) continue;
                    Node.StateWeights.Add(StateName, Child.AbsoluteWeight);
                    TotalWeight += Child.AbsoluteWeight;
                    if (StateName == Node.CurrentState) CurrentWeight = Child.AbsoluteWeight;
                    else if (Child.AbsoluteWeight > PreviousWeight)
                    {
                        PreviousWeight = Child.AbsoluteWeight;
                        Node.PreviousState = StateName;
                    }
                }
                Node.BlendAlpha = TotalWeight > SMALL_NUMBER ? CurrentWeight / TotalWeight : 1.0f;
                if (TotalWeight > SMALL_NUMBER)
                {
                    for (TPair<FString, float>& Pair : Node.StateWeights) Pair.Value /= TotalWeight;
                }
            }

            if (Node.OutputKind.IsEmpty())
            {
                if (Node.Children.Num() == 1)
                {
                    Node.OutputKind = Node.Children[0].OutputKind;
                    Node.OutputDerivation = Node.Children[0].OutputDerivation;
                    Node.NativeAssetName = Node.Children[0].NativeAssetName;
                    Node.ResolvedAnimationName = Node.Children[0].ResolvedAnimationName;
                }
                else if (Node.Children.Num() > 1)
                {
                    Node.PoseAlias = FString::Printf(TEXT("Pose#%d"), NextPoseAlias++);
                    if (!Node.MachineName.IsEmpty())
                    {
                        Node.OutputKind = TEXT("StateBlend");
                    }
                    else if (Node.NodeType.Contains(TEXT("Layered"), ESearchCase::IgnoreCase))
                    {
                        Node.OutputKind = TEXT("LayeredBlend");
                    }
                    else if (Node.NodeType.Contains(TEXT("Additive"), ESearchCase::IgnoreCase))
                    {
                        Node.OutputKind = TEXT("Additive");
                    }
                    else if (Node.NodeType.Contains(TEXT("Blend"), ESearchCase::IgnoreCase))
                    {
                        Node.OutputKind = TEXT("Blend");
                    }
                    else if (Node.NodeType.Contains(TEXT("Slot"), ESearchCase::IgnoreCase))
                    {
                        Node.OutputKind = TEXT("SlotBlend");
                    }
                    else Node.OutputKind = TEXT("Unknown");
                    TArray<FString> Sources;
                    for (const FSekiroLuaAnimDebugNode& Child : Node.Children)
                    {
                        const FString Name = !Child.PoseAlias.IsEmpty() ? Child.PoseAlias : (!Child.ResolvedAnimationName.IsEmpty() ? Child.ResolvedAnimationName : Child.NodeType);
                        const float RelativeWeight = Child.AbsoluteWeight / FMath::Max(Node.AbsoluteWeight, SMALL_NUMBER);
                        Sources.Add(FString::Printf(TEXT("%s@%.4f"), *Name, RelativeWeight));
                    }
                    Node.OutputDerivation = FString::Join(Sources, TEXT(" + "));
                }
            }
            return Node;
        };
        for (const int32 RootIndex : RootIndices) OutRoots.Add(BuildNode(RootIndex));
    }

    /**
     * 收集一个曲线类型当前实际求值出的全部值，并以类型前缀避免同名冲突。
     * 仅允许在游戏线程读取 AnimInstance Proxy；本函数不推进曲线或动画时间。
     *
     * @param AnimInstance 当前目标动画实例，不能为空。
     * @param CurveType UE 曲线通道类型。
     * @param TypeLabel 写入键名的稳定英文类型前缀。
     * @param OutCurves 接收“类型.名称 → 值”的当前帧曲线映射。
     */
    void CaptureCurveType(
        UAnimInstance* AnimInstance,
        const EAnimCurveType CurveType,
        const FString& TypeLabel,
        TMap<FString, float>& OutCurves)
    {
        const TMap<FName, float>& Curves = AnimInstance->GetAnimationCurveList(CurveType);
        for (const TPair<FName, float>& Pair : Curves)
        {
            OutCurves.Add(TypeLabel + TEXT(".") + Pair.Key.ToString(), Pair.Value);
        }
    }

    /**
     * 导出 AnimInstance 上全部 Blueprint 可见变量和三类当前曲线值，供 JSONL 快照回放。
     * 变量通过反射按当前实例值导出，不包含生成类内部 AnimNode 结构和不可见引擎状态；仅在落盘帧调用。
     *
     * @param AnimInstance 当前目标动画实例，必须非空且只在游戏线程读取。
     * @param Frame 接收变量和曲线的当前快照；原映射会先被清空。
     */
    void CaptureFrameValues(UAnimInstance* AnimInstance, FSekiroLuaAnimDebugFrame& Frame)
    {
        Frame.Variables.Reset();
        Frame.Curves.Reset();
        if (AnimInstance == nullptr) return;

        for (TFieldIterator<FProperty> PropertyIt(
            AnimInstance->GetClass(),
            EFieldIteratorFlags::IncludeSuper,
            EFieldIteratorFlags::ExcludeDeprecated); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            if (Property == nullptr
                || !Property->HasAnyPropertyFlags(CPF_BlueprintVisible)
                || Frame.Variables.Contains(Property->GetName()))
            {
                continue;
            }

            FString Value;
            if (Property->ArrayDim <= 1)
            {
                Property->ExportText_InContainer(
                    0,
                    Value,
                    AnimInstance,
                    nullptr,
                    AnimInstance,
                    PPF_PropertyWindow | PPF_BlueprintDebugView | PPF_SimpleObjectText);
            }
            else
            {
                TArray<FString> Elements;
                for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
                {
                    FString ElementValue;
                    Property->ExportText_InContainer(
                        ElementIndex,
                        ElementValue,
                        AnimInstance,
                        nullptr,
                        AnimInstance,
                        PPF_PropertyWindow | PPF_BlueprintDebugView | PPF_SimpleObjectText);
                    Elements.Add(FString::Printf(TEXT("[%d]=%s"), ElementIndex, *ElementValue));
                }
                Value = FString::Join(Elements, TEXT(", "));
            }
            Frame.Variables.Add(Property->GetName(), Value);
        }

        CaptureCurveType(
            AnimInstance,
            EAnimCurveType::AttributeCurve,
            TEXT("Attribute"),
            Frame.Curves);
        CaptureCurveType(
            AnimInstance,
            EAnimCurveType::MorphTargetCurve,
            TEXT("MorphTarget"),
            Frame.Curves);
        CaptureCurveType(
            AnimInstance,
            EAnimCurveType::MaterialCurve,
            TEXT("Material"),
            Frame.Curves);
    }

    /** 收集全部活跃拓扑、状态机 current state 和实际动画，生成离散变化签名。 */
    void AppendStateSignature(const FSekiroLuaAnimDebugNode& Node, TArray<FString>& Parts)
    {
        Parts.Add(FString::Printf(
            TEXT("%d:%d:%s:%s:%s"),
            Node.ChainId,
            Node.Depth,
            *Node.NodeType,
            *Node.CurrentState,
            *Node.NativeAssetName));
        for (const FSekiroLuaAnimDebugNode& Child : Node.Children) AppendStateSignature(Child, Parts);
    }

    /** 生成帧活跃拓扑签名；连续权重和播放时间不参与，避免每帧误判状态变化。 */
    FString BuildStateSignature(const FSekiroLuaAnimDebugFrame& Frame)
    {
        TArray<FString> Parts;
        for (const FSekiroLuaAnimDebugNode& Root : Frame.Roots) AppendStateSignature(Root, Parts);
        Parts.Sort();
        return FString::Join(Parts, TEXT("|"));
    }

    /**
     * 收集一个真实 AnimInstance 的活跃节点树、模块名和 Transition；无目标仍返回带时间戳的空帧。
     * 本函数只在游戏线程调用，不导出全量变量和曲线，落盘调用方应随后调用 CaptureFrameValues。
     *
     * @param CaptureReason 当前帧原因标签。
     * @param FrameIndex Session 内帧序号。
     * @param OutTarget 返回本次实际采集的 AnimInstance；无目标时写 nullptr，不转移所有权。
     * @return 可直接用于实时显示的帧；Variables 和 Curves 保持为空。
     */
    FSekiroLuaAnimDebugFrame GatherFrame(
        const FString& CaptureReason,
        const uint64 FrameIndex,
        UAnimInstance*& OutTarget)
    {
        OutTarget = nullptr;
        FSekiroLuaAnimDebugFrame Frame;
        Frame.FrameIndex = FrameIndex;
        Frame.CaptureReason = CaptureReason;
        Frame.UtcTimestamp = FDateTime::UtcNow().ToIso8601();
        Frame.SessionElapsedSeconds = RuntimeState.bSnapshotActive
            ? FMath::Max(0.0, FPlatformTime::Seconds() - RuntimeState.SessionStartSeconds)
            : 0.0;
        OutTarget = SelectTargetAnimInstance();
        if (OutTarget == nullptr) return Frame;
        Frame.AnimInstancePath = OutTarget->GetPathName();
        const FString* Module = RuntimeState.LuaModules.Find(TWeakObjectPtr<UAnimInstance>(OutTarget));
        if (Module != nullptr) Frame.LuaModuleName = *Module;
        BuildFullDebugTree(OutTarget, Frame.LuaModuleName, Frame.Roots);
        const TArray<FSekiroLuaAnimTransitionDebugValue>* Values =
            RuntimeState.Transitions.Find(TWeakObjectPtr<UAnimInstance>(OutTarget));
        if (Values != nullptr) Frame.Transitions = *Values;
        return Frame;
    }

    /** 将节点及其所有子节点转为 JSON 对象；未知节点只写真实 raw 与子来源。 */
    TSharedPtr<FJsonObject> NodeToJson(const FSekiroLuaAnimDebugNode& Node)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("NodeType"), Node.NodeType);
        Object->SetStringField(TEXT("RawDebugLine"), Node.RawDebugLine);
        Object->SetNumberField(TEXT("AbsoluteWeight"), Node.AbsoluteWeight);
        Object->SetNumberField(TEXT("Depth"), Node.Depth);
        Object->SetNumberField(TEXT("ChainId"), Node.ChainId);
        Object->SetBoolField(TEXT("bPoseSource"), Node.bPoseSource);
        Object->SetStringField(TEXT("MachineName"), Node.MachineName);
        Object->SetStringField(TEXT("CurrentState"), Node.CurrentState);
        Object->SetStringField(TEXT("PreviousState"), Node.PreviousState);
        Object->SetNumberField(TEXT("BlendAlpha"), Node.BlendAlpha);
        Object->SetStringField(TEXT("NativeAssetName"), Node.NativeAssetName);
        Object->SetStringField(TEXT("ResolvedAnimationName"), Node.ResolvedAnimationName);
        Object->SetStringField(TEXT("PoseAlias"), Node.PoseAlias);
        Object->SetStringField(TEXT("OutputKind"), Node.OutputKind);
        Object->SetStringField(TEXT("OutputDerivation"), Node.OutputDerivation);

        TSharedPtr<FJsonObject> StateWeights = MakeShared<FJsonObject>();
        for (const TPair<FString, float>& Pair : Node.StateWeights) StateWeights->SetNumberField(Pair.Key, Pair.Value);
        Object->SetObjectField(TEXT("StateWeights"), StateWeights);
        TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& Pair : Node.Inputs) Inputs->SetStringField(Pair.Key, Pair.Value);
        Object->SetObjectField(TEXT("Inputs"), Inputs);

        TArray<TSharedPtr<FJsonValue>> Children;
        for (const FSekiroLuaAnimDebugNode& Child : Node.Children) Children.Add(MakeShared<FJsonValueObject>(NodeToJson(Child)));
        Object->SetArrayField(TEXT("Children"), Children);
        return Object;
    }

    /** 将公共帧 Schema 序列化为一行压缩 JSON。 */
    FString FrameToJsonLine(const FSekiroLuaAnimDebugFrame& Frame)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("SchemaVersion"), Frame.SchemaVersion);
        Object->SetNumberField(TEXT("FrameIndex"), static_cast<double>(Frame.FrameIndex));
        Object->SetStringField(TEXT("CaptureReason"), Frame.CaptureReason);
        Object->SetStringField(TEXT("UtcTimestamp"), Frame.UtcTimestamp);
        Object->SetNumberField(TEXT("SessionElapsedSeconds"), Frame.SessionElapsedSeconds);
        Object->SetStringField(TEXT("AnimInstancePath"), Frame.AnimInstancePath);
        Object->SetStringField(TEXT("LuaModuleName"), Frame.LuaModuleName);

        TSharedPtr<FJsonObject> Variables = MakeShared<FJsonObject>();
        TArray<FString> VariableNames;
        Frame.Variables.GetKeys(VariableNames);
        VariableNames.Sort();
        for (const FString& VariableName : VariableNames)
        {
            Variables->SetStringField(VariableName, Frame.Variables[VariableName]);
        }
        Object->SetObjectField(TEXT("Variables"), Variables);

        TSharedPtr<FJsonObject> Curves = MakeShared<FJsonObject>();
        TArray<FString> CurveNames;
        Frame.Curves.GetKeys(CurveNames);
        CurveNames.Sort();
        for (const FString& CurveName : CurveNames)
        {
            Curves->SetNumberField(CurveName, Frame.Curves[CurveName]);
        }
        Object->SetObjectField(TEXT("Curves"), Curves);

        TArray<TSharedPtr<FJsonValue>> Roots;
        for (const FSekiroLuaAnimDebugNode& Root : Frame.Roots) Roots.Add(MakeShared<FJsonValueObject>(NodeToJson(Root)));
        Object->SetArrayField(TEXT("Roots"), Roots);
        TArray<TSharedPtr<FJsonValue>> Transitions;
        for (const FSekiroLuaAnimTransitionDebugValue& Value : Frame.Transitions)
        {
            TSharedPtr<FJsonObject> Transition = MakeShared<FJsonObject>();
            Transition->SetStringField(TEXT("TransitionId"), Value.TransitionId);
            Transition->SetStringField(TEXT("ExpressionLabel"), Value.ExpressionLabel);
            Transition->SetStringField(TEXT("ParameterName"), Value.ParameterName);
            Transition->SetStringField(TEXT("ParameterType"), Value.ParameterType);
            Transition->SetStringField(TEXT("ParameterValue"), Value.ParameterValue);
            Transition->SetStringField(TEXT("ExpectedValue"), Value.ExpectedValue);
            Transition->SetStringField(TEXT("Threshold"), Value.Threshold);
            Transition->SetBoolField(TEXT("ExpressionResult"), Value.bExpressionResult);
            Transition->SetBoolField(TEXT("bResult"), Value.bExpressionResult);
            Transition->SetBoolField(TEXT("RuleResult"), Value.bRuleResult);
            Transition->SetBoolField(TEXT("IsFinal"), Value.bIsFinal);
            Transition->SetStringField(TEXT("EvaluatedUtcTimestamp"), Value.EvaluatedUtcTimestamp);
            Transitions.Add(MakeShared<FJsonValueObject>(Transition));
        }
        Object->SetArrayField(TEXT("Transitions"), Transitions);

        FString Line;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Line;
    }

    /** 写入并立即 Flush 一帧 JSONL；返回写入是否完整成功。 */
    bool WriteFrame(const FSekiroLuaAnimDebugFrame& Frame)
    {
        if (!RuntimeState.SnapshotFile.IsValid()) return false;
        const FString Line = FrameToJsonLine(Frame) + LINE_TERMINATOR;
        FTCHARToUTF8 Utf8(*Line);
        return RuntimeState.SnapshotFile->Write(
            reinterpret_cast<const uint8*>(Utf8.Get()),
            Utf8.Length());
    }

    /** 成功写帧后消费该目标自上一快照以来的 Transition 值，实时帧不会调用本函数。 */
    void ConsumeCapturedTransitions(const FSekiroLuaAnimDebugFrame& Frame)
    {
        if (Frame.AnimInstancePath.IsEmpty()) return;
        for (TPair<TWeakObjectPtr<UAnimInstance>, TArray<FSekiroLuaAnimTransitionDebugValue>>& Pair : RuntimeState.Transitions)
        {
            if (Pair.Key.IsValid() && Pair.Key->GetPathName() == Frame.AnimInstancePath)
            {
                Pair.Value.Reset();
                return;
            }
        }
    }

    /** 停止当前 Session 并 Flush，保留最后路径供 UI 查询。 */
    void StopSnapshot()
    {
        if (RuntimeState.SnapshotFile.IsValid()) RuntimeState.SnapshotFile->Flush();
        RuntimeState.SnapshotFile.Reset();
        RuntimeState.bSnapshotActive = false;
        RuntimeState.bIntervalSamplingEnabled = false;
        RuntimeState.PreviousStateSignature.Reset();
        RuntimeState.NextInspectionSeconds = 0.0;
        RefreshSamplingEnabledForAnyThread();
    }

    /**
     * 开始替换式新 Session 并立即写入 Start 基线帧；OutputPath 为空时写 Saved/LuaAnimSnapshots。
     * 只能在游戏线程调用；状态变化采样始终开启，bEnableIntervalSampling 只控制额外的定时采样。
     *
     * @param IntervalSeconds 定时采样间隔秒，最小按 0.01 秒处理；禁用定时采样时仅保留配置值。
     * @param bEnableIntervalSampling true 同时按时间采样，false 仅在状态签名变化时采样。
     * @param OutputPath 输出 JSONL 路径；为空时自动生成 Session 文件。
     * @return 文件创建且 Start 帧写入成功时返回 true，否则返回 false。
     */
    bool StartSnapshot(
        const float IntervalSeconds,
        const bool bEnableIntervalSampling,
        const FString& OutputPath)
    {
        StopSnapshot();
        RuntimeState.SnapshotInterval = FMath::Max(0.01f, IntervalSeconds);
        RuntimeState.SnapshotPath = OutputPath;
        if (RuntimeState.SnapshotPath.IsEmpty())
        {
            const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LuaAnimSnapshots"));
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            PlatformFile.CreateDirectoryTree(*Directory);
            const FDateTime NowUtc = FDateTime::UtcNow();
            const FString FileName = FString::Printf(
                TEXT("LuaAnimSnapshot_%s_%lld.jsonl"),
                *NowUtc.ToString(TEXT("%Y%m%d_%H%M%S")),
                NowUtc.GetTicks());
            RuntimeState.SnapshotPath = FPaths::Combine(Directory, FileName);
        }
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        RuntimeState.SnapshotFile.Reset(PlatformFile.OpenWrite(*RuntimeState.SnapshotPath, false, false));
        if (!RuntimeState.SnapshotFile.IsValid()) return false;

        RuntimeState.bSnapshotActive = true;
        RuntimeState.bIntervalSamplingEnabled = bEnableIntervalSampling;
        RefreshSamplingEnabledForAnyThread();
        RuntimeState.SessionStartSeconds = FPlatformTime::Seconds();
        RuntimeState.NextFrameIndex = 0;
        UAnimInstance* Target = nullptr;
        FSekiroLuaAnimDebugFrame Frame = GatherFrame(
            TEXT("Start"),
            RuntimeState.NextFrameIndex++,
            Target);
        CaptureFrameValues(Target, Frame);
        RuntimeState.PreviousStateSignature = BuildStateSignature(Frame);
        RuntimeState.NextCaptureSeconds = RuntimeState.SessionStartSeconds + RuntimeState.SnapshotInterval;
        RuntimeState.NextInspectionSeconds = RuntimeState.NextCaptureSeconds;
        RuntimeState.LatestFrame = Frame;
        RuntimeState.bHasLatestFrame = true;
        const bool bWritten = WriteFrame(Frame);
        if (bWritten) ConsumeCapturedTransitions(Frame);
        return bWritten;
    }

    /**
     * 从最终 Pose 根开始输出一个节点及全部 relevant 子节点，不省略 Sequence、Slot、Montage 或混合分支。
     * 每个节点占一行并显示绝对权重、状态、动画和已解析输入；Children 保持 UE GatherDebugData 顺序。
     *
     * @param Node 当前树节点，只读。
     * @param Indent 当前显示缩进层级，零表示根节点。
     * @param Lines 接收完整层级文本，不会在函数内清空。
     */
    void AppendRealtimeHierarchy(const FSekiroLuaAnimDebugNode& Node, const int32 Indent, TArray<FString>& Lines)
    {
        const FString Padding = FString::ChrN(Indent * 2, TEXT(' '));
        FString Line = FString::Printf(
            TEXT("%s%s | Weight: %.4f"),
            *Padding,
            *Node.NodeType,
            Node.AbsoluteWeight);
        if (Node.bPoseSource) Line += TEXT(" | PoseSource");

        const FString StateName = ParseStateBranchName(Node.RawDebugLine);
        if (!StateName.IsEmpty()) Line += TEXT(" | State: ") + StateName;
        if (!Node.MachineName.IsEmpty())
        {
            Line += FString::Printf(
                TEXT(" | Machine: %s | Current: %s"),
                *Node.MachineName,
                *Node.CurrentState);
            if (!Node.PreviousState.IsEmpty())
            {
                Line += FString::Printf(
                    TEXT(" | Previous: %s | BlendAlpha: %.3f"),
                    *Node.PreviousState,
                    Node.BlendAlpha);
            }
            if (!Node.StateWeights.IsEmpty())
            {
                TArray<FString> StateNames;
                Node.StateWeights.GetKeys(StateNames);
                StateNames.Sort();
                TArray<FString> StateWeightParts;
                for (const FString& State : StateNames)
                {
                    StateWeightParts.Add(FString::Printf(
                        TEXT("%s=%.3f"),
                        *State,
                        Node.StateWeights[State]));
                }
                Line += TEXT(" | StateWeights: ")
                    + FString::Join(StateWeightParts, TEXT(", "));
            }
        }
        if (!Node.ResolvedAnimationName.IsEmpty())
        {
            Line += TEXT(" | Animation: ") + Node.ResolvedAnimationName;
        }
        if (!Node.PoseAlias.IsEmpty())
        {
            Line += FString::Printf(
                TEXT(" | Output: %s (%s)"),
                *Node.PoseAlias,
                *Node.OutputKind);
        }
        if (!Node.Inputs.IsEmpty())
        {
            TArray<FString> InputNames;
            Node.Inputs.GetKeys(InputNames);
            InputNames.Sort();
            TArray<FString> InputParts;
            for (const FString& InputName : InputNames)
            {
                InputParts.Add(InputName + TEXT("=") + Node.Inputs[InputName]);
            }
            Line += TEXT(" | Inputs: ") + FString::Join(InputParts, TEXT(", "));
        }
        Lines.Add(Line);

        for (const FSekiroLuaAnimDebugNode& Child : Node.Children)
        {
            AppendRealtimeHierarchy(Child, Indent + 1, Lines);
        }
    }

    /** 构建实时唯一屏幕消息；完整显示从根到全部活跃子节点的层级。 */
    FString BuildRealtimeText(const FSekiroLuaAnimDebugFrame& Frame)
    {
        TArray<FString> Lines;
        Lines.Add(TEXT("Lua Animation Hierarchy"));
        Lines.Add(TEXT("AnimInstance: ") + (Frame.AnimInstancePath.IsEmpty() ? TEXT("<none>") : Frame.AnimInstancePath));
        Lines.Add(TEXT("LuaModule: ") + (Frame.LuaModuleName.IsEmpty() ? TEXT("<unknown>") : Frame.LuaModuleName));
        Lines.Add(TEXT("------------------------------"));
        for (const FSekiroLuaAnimDebugNode& Root : Frame.Roots) AppendRealtimeHierarchy(Root, 0, Lines);
        if (Frame.Roots.IsEmpty()) Lines.Add(TEXT("No active pose hierarchy."));
        return FString::Join(Lines, TEXT("\n"));
    }

    /** Debug 命令：无参启用 Hierarchy，Off 关闭，其余参数显示帮助并替换旧内容。 */
    void HandleDebugCommand(const TArray<FString>& Arguments)
    {
        RemoveScreenMessage();
        if (Arguments.IsEmpty()) RuntimeState.DebugView = EDebugView::Hierarchy;
        else if (Arguments.Num() == 1 && Arguments[0].Equals(TEXT("Off"), ESearchCase::IgnoreCase)) RuntimeState.DebugView = EDebugView::Off;
        else RuntimeState.DebugView = EDebugView::Help;
        RefreshSamplingEnabledForAnyThread();
    }

    /**
     * 按控制台参数启动快照：无参数仅采样状态变化，有参数则按正数秒间隔追加定时采样。
     * 参数解析失败或非正数时回退 0.15 秒；OutputPath 为空时使用默认 Session 目录。
     *
     * @param Arguments 控制台参数；仅首个参数作为秒间隔。
     * @param OutputPath 可选显式输出路径，生产命令传空，自动化测试传临时文件。
     * @return Session 创建及 Start 帧写入是否成功。
     */
    bool StartSnapshotFromArguments(
        const TArray<FString>& Arguments,
        const FString& OutputPath)
    {
        float Interval = DefaultSnapshotInterval;
        if (!Arguments.IsEmpty()) Interval = FCString::Atof(*Arguments[0]);
        return StartSnapshot(
            Interval > 0.0f ? Interval : DefaultSnapshotInterval,
            !Arguments.IsEmpty(),
            OutputPath);
    }

    /** Snapshot 命令：无参仅采样状态变化，有秒参数时替换为定时采样 Session。 */
    void HandleSnapshotCommand(const TArray<FString>& Arguments)
    {
        StartSnapshotFromArguments(Arguments, FString());
    }

    /** Snapshot.Stop 命令：安全 Flush 并停止。 */
    void HandleSnapshotStopCommand()
    {
        StopSnapshot();
    }

    /** 实时层级每帧 Gather；Snapshot-only 按采样间隔限频检查状态并复用同一帧。 */
    bool Tick(const float DeltaSeconds)
    {
        static_cast<void>(DeltaSeconds);
        if (RuntimeState.DebugView == EDebugView::Off && !RuntimeState.bSnapshotActive) return true;
        if (RuntimeState.DebugView == EDebugView::Help)
        {
            RemoveScreenMessage();
            if (GEngine != nullptr) GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.1f, FColor::Cyan,
                TEXT("Usage:\nSekiro.LuaAnim.Debug [Off]\nSekiro.LuaAnim.Snapshot [IntervalSeconds]\n  no interval: state changes only\nSekiro.LuaAnim.Snapshot.Stop"), false);
            return true;
        }

        const double Now = FPlatformTime::Seconds();
        const bool bRealtimeHierarchy = RuntimeState.DebugView == EDebugView::Hierarchy;
        if (!bRealtimeHierarchy
            && RuntimeState.bSnapshotActive
            && Now < RuntimeState.NextInspectionSeconds)
        {
            return true;
        }
        if (!bRealtimeHierarchy && RuntimeState.bSnapshotActive)
        {
            RuntimeState.NextInspectionSeconds = Now + RuntimeState.SnapshotInterval;
        }

        DrainPendingTransitionValues();
        UAnimInstance* Target = nullptr;
        FSekiroLuaAnimDebugFrame Frame = GatherFrame(
            TEXT("Realtime"),
            RuntimeState.NextFrameIndex,
            Target);
        const FString StateSignature = BuildStateSignature(Frame);
        if (RuntimeState.bSnapshotActive)
        {
            const bool bStateChanged = !RuntimeState.PreviousStateSignature.IsEmpty() && StateSignature != RuntimeState.PreviousStateSignature;
            const bool bIntervalDue =
                RuntimeState.bIntervalSamplingEnabled
                && Now >= RuntimeState.NextCaptureSeconds;
            if (bStateChanged || bIntervalDue)
            {
                Frame.FrameIndex = RuntimeState.NextFrameIndex++;
                Frame.CaptureReason = bStateChanged ? TEXT("StateChanged") : TEXT("Interval");
                Frame.SessionElapsedSeconds = Now - RuntimeState.SessionStartSeconds;
                CaptureFrameValues(Target, Frame);
                if (WriteFrame(Frame)) ConsumeCapturedTransitions(Frame);
                RuntimeState.NextCaptureSeconds = Now + RuntimeState.SnapshotInterval;
            }
            RuntimeState.PreviousStateSignature = StateSignature;
        }
        RuntimeState.LatestFrame = Frame;
        RuntimeState.bHasLatestFrame = true;
        if (RuntimeState.DebugView == EDebugView::Hierarchy)
        {
            RemoveScreenMessage();
            if (GEngine != nullptr) GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.1f, FColor::Cyan, BuildRealtimeText(Frame), false);
        }
        return true;
    }
}

/** 注册三个唯一命令与每帧采样器；Runtime 模块只在游戏线程调用一次。 */
void FSekiroLuaAnimDebugRuntime::Startup()
{
    using namespace SekiroLuaAnimDebugPrivate;
    check(IsInGameThread());
    if (RuntimeState.DebugCommand != nullptr) return;
    RuntimeState.DebugCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Sekiro.LuaAnim.Debug"), TEXT("Sekiro.LuaAnim.Debug [Off]"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleDebugCommand), ECVF_Default);
    RuntimeState.SnapshotCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Sekiro.LuaAnim.Snapshot"),
        TEXT("Sekiro.LuaAnim.Snapshot [IntervalSeconds]; omit interval to capture state changes only."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleSnapshotCommand), ECVF_Default);
    RuntimeState.SnapshotStopCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Sekiro.LuaAnim.Snapshot.Stop"), TEXT("Stop and flush the active Lua animation snapshot session."),
        FConsoleCommandDelegate::CreateStatic(&HandleSnapshotStopCommand), ECVF_Default);
    RuntimeState.TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
}

/** 注销命令与采样器、Flush Session 并移除固定屏幕消息，避免 Hot Reload 重复。 */
void FSekiroLuaAnimDebugRuntime::Shutdown()
{
    using namespace SekiroLuaAnimDebugPrivate;
    check(IsInGameThread());
    StopSnapshot();
    RemoveScreenMessage();
    SamplingEnabledForAnyThread.Store(false, EMemoryOrder::SequentiallyConsistent);
    DiscardPendingTransitionValues();
    if (RuntimeState.TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(RuntimeState.TickerHandle);
        RuntimeState.TickerHandle.Reset();
    }
    IConsoleManager& ConsoleManager = IConsoleManager::Get();
    if (RuntimeState.DebugCommand != nullptr) ConsoleManager.UnregisterConsoleObject(RuntimeState.DebugCommand);
    if (RuntimeState.SnapshotCommand != nullptr) ConsoleManager.UnregisterConsoleObject(RuntimeState.SnapshotCommand);
    if (RuntimeState.SnapshotStopCommand != nullptr) ConsoleManager.UnregisterConsoleObject(RuntimeState.SnapshotStopCommand);
    RuntimeState = FRuntimeState();
}

/**
 * 查询实时层级或 JSONL 是否需要采样。
 * 可由游戏线程和并行动画线程调用；返回值来自原子快照，不读取可变 RuntimeState。
 */
bool FSekiroLuaAnimDebugRuntime::IsSamplingEnabled()
{
    using namespace SekiroLuaAnimDebugPrivate;
    return SamplingEnabledForAnyThread.Load(EMemoryOrder::SequentiallyConsistent);
}

/** 在真实 Lua Update/Transition 调用时登记实例与模块；只在游戏线程且采样开启时保留弱引用。 */
void FSekiroLuaAnimDebugRuntime::RecordAnimInstanceModule(UAnimInstance* AnimInstance, const FString& LuaModuleName)
{
    using namespace SekiroLuaAnimDebugPrivate;
    if (!IsSamplingEnabled() || !IsInGameThread() || AnimInstance == nullptr || LuaModuleName.IsEmpty()) return;
    RuntimeState.LuaModules.Add(TWeakObjectPtr<UAnimInstance>(AnimInstance), LuaModuleName);
}

/**
 * 记录一个叶子、逻辑表达式或最终 Transition 结果。
 * 游戏线程直接追加，动画工作线程通过 MPSC 队列提交并在下一次采帧前合并；函数不执行 UObject 方法。
 *
 * @param AnimInstance 当前 Transition 所属实例；仅作为弱键保存，不转移所有权。
 * @param TransitionId Transition 稳定 ID。
 * @param ExpressionLabel AST 表达式稳定标签。
 * @param ParameterName 本次读取的参数名；组合表达式可为空。
 * @param ParameterType Bool、Float、Expression 或 Error。
 * @param ParameterValue 本次真实输入值的稳定文本。
 * @param ExpectedValue Bool 比较期望值；不适用时为空。
 * @param Threshold Float 比较阈值；不适用时为空。
 * @param bExpressionResult 当前表达式结果。
 * @param bRuleResult 最终规则结果；非最终表达式传 false。
 * @param bIsFinal 是否为接入 Transition Result 的最终值。
 */
void FSekiroLuaAnimDebugRuntime::RecordTransitionValue(
    UAnimInstance* AnimInstance,
    const FString& TransitionId,
    const FString& ExpressionLabel,
    const FString& ParameterName,
    const FString& ParameterType,
    const FString& ParameterValue,
    const FString& ExpectedValue,
    const FString& Threshold,
    const bool bExpressionResult,
    const bool bRuleResult,
    const bool bIsFinal)
{
    using namespace SekiroLuaAnimDebugPrivate;
    if (!IsSamplingEnabled() || AnimInstance == nullptr) return;
    FSekiroLuaAnimTransitionDebugValue Value;
    Value.TransitionId = TransitionId;
    Value.ExpressionLabel = ExpressionLabel;
    Value.ParameterName = ParameterName;
    Value.ParameterType = ParameterType;
    Value.ParameterValue = ParameterValue;
    Value.ExpectedValue = ExpectedValue;
    Value.Threshold = Threshold;
    Value.bExpressionResult = bExpressionResult;
    Value.bRuleResult = bRuleResult;
    Value.bIsFinal = bIsFinal;
    Value.EvaluatedUtcTimestamp = FDateTime::UtcNow().ToIso8601();
    const TWeakObjectPtr<UAnimInstance> WeakAnimInstance(AnimInstance);
    if (IsInGameThread())
    {
        TArray<FSekiroLuaAnimTransitionDebugValue>& Values =
            RuntimeState.Transitions.FindOrAdd(WeakAnimInstance);
        StoreLatestTransitionValue(Values, MoveTemp(Value));
        return;
    }

    FPendingTransitionDebugValue Pending;
    Pending.AnimInstance = WeakAnimInstance;
    Pending.Value = MoveTemp(Value);
    PendingTransitionValues.Enqueue(MoveTemp(Pending));
}

/** 复制最近一帧供 Editor UI 读取；无帧时返回 false 且清空输出。 */
bool FSekiroLuaAnimDebugRuntime::GetLatestFrame(FSekiroLuaAnimDebugFrame& OutFrame)
{
    using namespace SekiroLuaAnimDebugPrivate;
    if (!RuntimeState.bHasLatestFrame)
    {
        OutFrame = FSekiroLuaAnimDebugFrame();
        return false;
    }
    OutFrame = RuntimeState.LatestFrame;
    return true;
}

/** 返回当前或最近一个 Snapshot Session 文件路径；不保证文件仍处于写入状态。 */
FString FSekiroLuaAnimDebugRuntime::GetSnapshotSessionPath()
{
    return SekiroLuaAnimDebugPrivate::RuntimeState.SnapshotPath;
}

/**
 * Flush 并关闭当前快照 Session，立即释放 JSONL 文件句柄，同时保留最后文件路径供查看器读取。
 * 只能在游戏线程调用；无活动 Session 时安全无操作，不影响实时动画层级 Debug。
 */
void FSekiroLuaAnimDebugRuntime::StopSnapshotSession()
{
    check(IsInGameThread());
    SekiroLuaAnimDebugPrivate::StopSnapshot();
}

/**
 * 在 PIE/SIE 创建 PlayWorld 前强制关闭实时 Debug 与 Snapshot，保证每次运行默认无采样负担。
 * 只能由编辑器 PreBeginPIE 生命周期在游戏线程调用；会 Flush 未关闭文件并保留最后快照路径。
 */
void FSekiroLuaAnimDebugRuntime::ResetForPIEStart()
{
    ResetForPIEEnd();
}

/**
 * 在 PIE/SIE 结束后关闭实时层级 Debug 与 Snapshot，Flush 文件并清除仅属于本次运行的采样缓存。
 * 只能由编辑器 EndPIE 生命周期在游戏线程调用；保留最后快照路径，供时间轴查看器继续加载刚结束的记录。
 */
void FSekiroLuaAnimDebugRuntime::ResetForPIEEnd()
{
    using namespace SekiroLuaAnimDebugPrivate;
    check(IsInGameThread());
    StopSnapshot();
    RemoveScreenMessage();
    SamplingEnabledForAnyThread.Store(false, EMemoryOrder::SequentiallyConsistent);
    DiscardPendingTransitionValues();
    RuntimeState.DebugView = EDebugView::Off;
    RuntimeState.LuaModules.Reset();
    RuntimeState.ResolvedAnimationNames.Reset();
    RuntimeState.Transitions.Reset();
    RuntimeState.LatestFrame = FSekiroLuaAnimDebugFrame();
    RuntimeState.bHasLatestFrame = false;
    RuntimeState.SessionStartSeconds = 0.0;
    RuntimeState.NextCaptureSeconds = 0.0;
    RuntimeState.NextInspectionSeconds = 0.0;
    RuntimeState.NextFrameIndex = 0;
}

#if WITH_DEV_AUTOMATION_TESTS
/** 测试专用：应用真实 Debug 命令参数替换逻辑。 */
void FSekiroLuaAnimDebugRuntime::ApplyDebugArgumentsForTesting(const TArray<FString>& Arguments)
{
    SekiroLuaAnimDebugPrivate::HandleDebugCommand(Arguments);
}

/** 测试专用：用显式临时路径开始替换式 Session。 */
bool FSekiroLuaAnimDebugRuntime::StartSnapshotForTesting(const float IntervalSeconds, const FString& OutputPath)
{
    return SekiroLuaAnimDebugPrivate::StartSnapshot(
        IntervalSeconds,
        true,
        OutputPath);
}

/** 测试专用：用显式临时路径应用真实 Snapshot 控制台参数。 */
bool FSekiroLuaAnimDebugRuntime::ApplySnapshotArgumentsForTesting(
    const TArray<FString>& Arguments,
    const FString& OutputPath)
{
    return SekiroLuaAnimDebugPrivate::StartSnapshotFromArguments(
        Arguments,
        OutputPath);
}

/** 测试专用：安全停止并 Flush Session。 */
void FSekiroLuaAnimDebugRuntime::StopSnapshotForTesting()
{
    SekiroLuaAnimDebugPrivate::StopSnapshot();
}

/** 测试专用：查询 Hierarchy 是否为当前唯一实时页。 */
bool FSekiroLuaAnimDebugRuntime::IsDebugEnabledForTesting()
{
    return SekiroLuaAnimDebugPrivate::RuntimeState.DebugView == SekiroLuaAnimDebugPrivate::EDebugView::Hierarchy;
}

/** 测试专用：查询 Snapshot Session 是否活动。 */
bool FSekiroLuaAnimDebugRuntime::IsSnapshotActiveForTesting()
{
    return SekiroLuaAnimDebugPrivate::RuntimeState.bSnapshotActive;
}

/** 测试专用：查询当前 Session 是否启用额外定时采样。 */
bool FSekiroLuaAnimDebugRuntime::IsSnapshotIntervalSamplingEnabledForTesting()
{
    return SekiroLuaAnimDebugPrivate::RuntimeState.bIntervalSamplingEnabled;
}

/** 测试专用：读取经下限修正后的采样间隔秒数。 */
float FSekiroLuaAnimDebugRuntime::GetSnapshotIntervalForTesting()
{
    return SekiroLuaAnimDebugPrivate::RuntimeState.SnapshotInterval;
}

/** 测试专用：使用真实解析器把一条 UE 动画调试行转换为节点。 */
FSekiroLuaAnimDebugNode FSekiroLuaAnimDebugRuntime::ParseDebugLineForTesting(
    const FString& DebugLine)
{
    using namespace SekiroLuaAnimDebugPrivate;
    FSekiroLuaAnimDebugNode Node;
    Node.RawDebugLine = DebugLine;
    Node.NodeType = ParseNodeType(DebugLine);
    ParseSlotInputs(Node);
    ParseMontageNode(nullptr, FString(), Node);
    return Node;
}

/** 测试专用：使用真实格式化逻辑输出手工构造的完整活跃节点树。 */
FString FSekiroLuaAnimDebugRuntime::BuildRealtimeTextForTesting(
    const FSekiroLuaAnimDebugFrame& Frame)
{
    return SekiroLuaAnimDebugPrivate::BuildRealtimeText(Frame);
}

/** 测试专用：恢复关闭状态并清空全部内存采样，保留命令注册。 */
void FSekiroLuaAnimDebugRuntime::ResetForTesting()
{
    using namespace SekiroLuaAnimDebugPrivate;
    ResetForPIEEnd();
    RuntimeState.SnapshotPath.Reset();
}
#endif
