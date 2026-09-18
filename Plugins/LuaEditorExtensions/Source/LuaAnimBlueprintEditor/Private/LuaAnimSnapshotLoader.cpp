#include "LuaAnimSnapshotLoader.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LuaAnimSnapshotLoaderPrivate
{
/**
 * 读取可选字符串字段；字段缺失或类型不匹配时保留目标值。
 * 可在任意线程调用，只读取调用方持有的 JSON 对象；FieldName 是字段名，OutValue 是输出字符串。
 */
void ReadString(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    FString& OutValue)
{
    if (Object.IsValid()) Object->TryGetStringField(FieldName, OutValue);
}

/**
 * 读取可选数值字段；字段缺失或类型不匹配时保留目标值。
 * 可在任意线程调用；FieldName 是字段名，OutValue 是输出双精度数值。
 */
void ReadNumber(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    double& OutValue)
{
    if (Object.IsValid()) Object->TryGetNumberField(FieldName, OutValue);
}

/**
 * 读取可选布尔字段；字段缺失或类型不匹配时保留目标值。
 * 可在任意线程调用；FieldName 是字段名，OutValue 是输出布尔值。
 */
void ReadBool(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    bool& OutValue)
{
    if (Object.IsValid()) Object->TryGetBoolField(FieldName, OutValue);
}

/**
 * 将 JSON 标量转换为面向查看器的文本，复合值返回紧凑占位说明而不递归展开。
 * 可在任意线程调用；Value 可为空。返回空值或不支持类型的可读文本。
 */
FString ScalarToString(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid()) return TEXT("<空>");

    switch (Value->Type)
    {
    case EJson::String:
        return Value->AsString();
    case EJson::Number:
    {
        const double Number = Value->AsNumber();
        const double RoundedNumber = FMath::RoundToDouble(Number);
        return FMath::IsNearlyEqual(Number, RoundedNumber)
            ? FString::Printf(TEXT("%.0f"), Number)
            : FString::SanitizeFloat(Number);
    }
    case EJson::Boolean:
        return Value->AsBool() ? TEXT("true") : TEXT("false");
    case EJson::Null:
        return TEXT("null");
    case EJson::Array:
        return TEXT("<数组>");
    case EJson::Object:
        return TEXT("<对象>");
    default:
        return TEXT("<未知>");
    }
}

/**
 * 把指定 JSON 标量字段转换为字符串，兼容历史字符串和新版数字标识。
 * 可在任意线程调用；Object 是源对象，FieldName 是字段名，OutValue 仅在字段存在时更新。
 */
void ReadScalarString(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    FString& OutValue)
{
    if (!Object.IsValid()) return;
    const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
    if (Value != nullptr) OutValue = ScalarToString(*Value);
}

/**
 * 读取 object<string, scalar> 字段并转换为字符串映射，未知复合值仅显示类型占位。
 * 可在任意线程调用；Object 是源对象，FieldName 是字段名，OutValues 会被填充但不会持有 JSON 引用。
 */
void ReadStringMap(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    TMap<FString, FString>& OutValues)
{
    const TSharedPtr<FJsonObject>* MapObject = nullptr;
    if (!Object.IsValid()
        || !Object->TryGetObjectField(FieldName, MapObject)
        || MapObject == nullptr
        || !MapObject->IsValid())
    {
        return;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
    {
        OutValues.Add(Pair.Key, ScalarToString(Pair.Value));
    }
}

/**
 * 读取 object<string, number> 字段，非数值成员被忽略以保持整帧可用。
 * 可在任意线程调用；Object 是源对象，FieldName 是字段名，OutValues 接收有效权重。
 */
void ReadNumberMap(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    TMap<FString, double>& OutValues)
{
    const TSharedPtr<FJsonObject>* MapObject = nullptr;
    if (!Object.IsValid()
        || !Object->TryGetObjectField(FieldName, MapObject)
        || MapObject == nullptr
        || !MapObject->IsValid())
    {
        return;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapObject)->Values)
    {
        if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Number)
        {
            OutValues.Add(Pair.Key, Pair.Value->AsNumber());
        }
    }
}

/**
 * 递归解析一个节点及其 Children；缺失字段使用模型默认值，未知字段被忽略。
 * 可在任意线程调用；Object 必须是当前节点对象。返回独立节点共享指针，空对象返回默认节点。
 */
TSharedPtr<FLuaAnimSnapshotNode> ParseNode(const TSharedPtr<FJsonObject>& Object)
{
    TSharedPtr<FLuaAnimSnapshotNode> Node =
        MakeShared<FLuaAnimSnapshotNode>();
    ReadString(Object, TEXT("NodeType"), Node->NodeType);
    ReadString(Object, TEXT("RawDebugLine"), Node->RawDebugLine);
    ReadNumber(Object, TEXT("AbsoluteWeight"), Node->AbsoluteWeight);

    double Depth = 0.0;
    ReadNumber(Object, TEXT("Depth"), Depth);
    Node->Depth = static_cast<int32>(Depth);

    ReadScalarString(Object, TEXT("ChainId"), Node->ChainId);
    ReadBool(Object, TEXT("bPoseSource"), Node->bPoseSource);
    ReadString(Object, TEXT("NativeAssetName"), Node->NativeAssetName);
    ReadString(Object, TEXT("ResolvedAnimationName"), Node->ResolvedAnimationName);
    ReadString(Object, TEXT("PoseAlias"), Node->PoseAlias);
    ReadStringMap(Object, TEXT("Inputs"), Node->Inputs);
    ReadString(Object, TEXT("MachineName"), Node->MachineName);
    ReadString(Object, TEXT("CurrentState"), Node->CurrentState);
    ReadString(Object, TEXT("PreviousState"), Node->PreviousState);
    ReadNumber(Object, TEXT("BlendAlpha"), Node->BlendAlpha);
    ReadNumberMap(Object, TEXT("StateWeights"), Node->StateWeights);
    ReadString(Object, TEXT("OutputKind"), Node->OutputKind);
    ReadString(Object, TEXT("OutputDerivation"), Node->OutputDerivation);

    const TArray<TSharedPtr<FJsonValue>>* ChildValues = nullptr;
    if (Object.IsValid()
        && Object->TryGetArrayField(TEXT("Children"), ChildValues)
        && ChildValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& ChildValue : *ChildValues)
        {
            const TSharedPtr<FJsonObject>* ChildObject = nullptr;
            if (ChildValue.IsValid()
                && ChildValue->TryGetObject(ChildObject)
                && ChildObject != nullptr)
            {
                Node->Children.Add(ParseNode(*ChildObject));
            }
        }
    }
    return Node;
}

/**
 * 解析一条 Transition 表达式采样，缺失字段使用默认值。
 * 可在任意线程调用；Object 是采样对象。返回不持有 JSON 引用的值模型。
 */
FLuaAnimTransitionSample ParseTransition(const TSharedPtr<FJsonObject>& Object)
{
    FLuaAnimTransitionSample Sample;
    ReadString(Object, TEXT("TransitionId"), Sample.TransitionId);
    ReadString(Object, TEXT("ExpressionLabel"), Sample.ExpressionLabel);
    ReadString(Object, TEXT("ParameterName"), Sample.ParameterName);
    ReadString(Object, TEXT("ParameterType"), Sample.ParameterType);
    ReadString(Object, TEXT("ParameterValue"), Sample.ParameterValue);
    ReadScalarString(Object, TEXT("ExpectedValue"), Sample.ExpectedValue);
    ReadScalarString(Object, TEXT("Threshold"), Sample.Threshold);
    const bool bHasExpressionResult = Object.IsValid()
        && Object->TryGetBoolField(TEXT("ExpressionResult"), Sample.bExpressionResult);
    if (!bHasExpressionResult)
    {
        ReadBool(Object, TEXT("bResult"), Sample.bExpressionResult);
    }
    ReadBool(Object, TEXT("RuleResult"), Sample.bRuleResult);
    ReadBool(Object, TEXT("IsFinal"), Sample.bIsFinal);
    ReadString(Object, TEXT("EvaluatedUtcTimestamp"), Sample.EvaluatedUtcTimestamp);
    return Sample;
}

/**
 * 从单个 JSON 对象构造一帧快照，递归复制 Roots 与 Transition 数据。
 * 可在任意线程调用；Object 是当前行反序列化结果。返回可独立持有的帧模型。
 */
TSharedPtr<FLuaAnimSnapshotFrame> ParseFrame(const TSharedPtr<FJsonObject>& Object)
{
    TSharedPtr<FLuaAnimSnapshotFrame> Frame =
        MakeShared<FLuaAnimSnapshotFrame>();

    double SchemaVersion = 0.0;
    double FrameIndex = 0.0;
    ReadNumber(Object, TEXT("SchemaVersion"), SchemaVersion);
    ReadNumber(Object, TEXT("FrameIndex"), FrameIndex);
    Frame->SchemaVersion = static_cast<int32>(SchemaVersion);
    Frame->FrameIndex = static_cast<int64>(FrameIndex);
    ReadString(Object, TEXT("UtcTimestamp"), Frame->UtcTimestamp);
    ReadNumber(Object, TEXT("SessionElapsedSeconds"), Frame->SessionElapsedSeconds);
    ReadString(Object, TEXT("CaptureReason"), Frame->CaptureReason);
    ReadString(Object, TEXT("AnimInstancePath"), Frame->AnimInstancePath);
    ReadString(Object, TEXT("LuaModuleName"), Frame->LuaModuleName);
    ReadStringMap(Object, TEXT("Variables"), Frame->Variables);
    ReadNumberMap(Object, TEXT("Curves"), Frame->Curves);

    const TArray<TSharedPtr<FJsonValue>>* RootValues = nullptr;
    if (Object.IsValid()
        && Object->TryGetArrayField(TEXT("Roots"), RootValues)
        && RootValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& RootValue : *RootValues)
        {
            const TSharedPtr<FJsonObject>* RootObject = nullptr;
            if (RootValue.IsValid()
                && RootValue->TryGetObject(RootObject)
                && RootObject != nullptr)
            {
                Frame->Roots.Add(ParseNode(*RootObject));
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* TransitionValues = nullptr;
    if (Object.IsValid()
        && Object->TryGetArrayField(TEXT("Transitions"), TransitionValues)
        && TransitionValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& TransitionValue : *TransitionValues)
        {
            const TSharedPtr<FJsonObject>* TransitionObject = nullptr;
            if (TransitionValue.IsValid()
                && TransitionValue->TryGetObject(TransitionObject)
                && TransitionObject != nullptr)
            {
                Frame->Transitions.Add(ParseTransition(*TransitionObject));
            }
        }
    }
    return Frame;
}

struct FNodeFact
{
    FString NodeType; // 原生节点类型
    FString DisplayName; // 面向用户的节点名
    FString CurrentState; // 状态机当前状态
    FString AnimationName; // Lua 动画名或原生资产名
    FString ParentSlotName; // 最近所属 Slot 名
    int32 Depth = MAX_int32; // 节点原始 Root 深度
    int32 EffectiveDepth = MAX_int32; // Montage 使用所属 Slot 深度
};

struct FFrameFacts
{
    TMap<FString, FString> States; // 状态机稳定名到当前状态
    TMap<FString, int32> StateDepths; // 状态机到 Root 的节点深度
    TSet<FString> Animations; // 当前活跃 Lua 动画名
    TMap<FString, FNodeFact> Nodes; // 稳定节点键到离散节点事实
    TMap<FString, bool> TransitionResults; // 最终 Transition 结果
    int32 ActiveNodeCount = 0; // 活跃节点总数
};

/**
 * 递归收集一帧中的状态机、Lua 动画和活跃节点数量，用于生成相邻快照差异。
 * 可在任意线程调用；Node 可为空，OutFacts 会累加当前子树事实且不被清空。
 *
 * @param Node 当前节点，只读且可为空。
 * @param ParentSlotName 当前节点最近的父 Slot 名，可为空。
 * @param ParentSlotDepth 最近父 Slot 到 Root 的深度，无父 Slot 时为 MAX_int32。
 * @param OutFacts 接收当前节点及全部子节点的离散事实。
 */
void CollectNodeFacts(
    const TSharedPtr<FLuaAnimSnapshotNode>& Node,
    const FString& ParentSlotName,
    const int32 ParentSlotDepth,
    FFrameFacts& OutFacts)
{
    if (!Node.IsValid()) return;
    ++OutFacts.ActiveNodeCount;

    const FString SlotName = Node->Inputs.FindRef(TEXT("SlotName"));
    const bool bIsSlot = Node->NodeType.Contains(
        TEXT("Slot"),
        ESearchCase::IgnoreCase);
    const bool bIsMontage = Node->NodeType.Contains(
        TEXT("Montage"),
        ESearchCase::IgnoreCase);
    const FString CurrentSlotName = bIsSlot
        ? (SlotName.IsEmpty() ? Node->NodeType : SlotName)
        : ParentSlotName;
    const int32 CurrentSlotDepth = bIsSlot
        ? Node->Depth
        : ParentSlotDepth;

    if (!Node->CurrentState.IsEmpty())
    {
        FString StateLabel = Node->MachineName;
        if (StateLabel.IsEmpty()) StateLabel = Node->PoseAlias;
        if (StateLabel.IsEmpty()) StateLabel = Node->NodeType;
        if (!Node->ChainId.IsEmpty()) StateLabel += TEXT(" [") + Node->ChainId + TEXT("]");
        const int32* ExistingDepth = OutFacts.StateDepths.Find(StateLabel);
        if (ExistingDepth == nullptr || Node->Depth < *ExistingDepth)
        {
            OutFacts.States.Add(StateLabel, Node->CurrentState);
            OutFacts.StateDepths.Add(StateLabel, Node->Depth);
        }
    }

    const FString AnimationName = Node->ResolvedAnimationName.IsEmpty()
        ? Node->NativeAssetName
        : Node->ResolvedAnimationName;
    if (!AnimationName.IsEmpty()) OutFacts.Animations.Add(AnimationName);

    FNodeFact NodeFact;
    NodeFact.NodeType = Node->NodeType;
    NodeFact.CurrentState = Node->CurrentState;
    NodeFact.AnimationName = AnimationName;
    NodeFact.ParentSlotName = ParentSlotName;
    NodeFact.Depth = Node->Depth;
    NodeFact.EffectiveDepth = bIsMontage
        && ParentSlotDepth != MAX_int32
            ? ParentSlotDepth
            : Node->Depth;
    if (bIsMontage)
    {
        const FString MontageName = Node->Inputs.FindRef(TEXT("MontageName"));
        NodeFact.DisplayName = AnimationName.IsEmpty()
            ? (MontageName.IsEmpty() ? TEXT("Montage") : MontageName)
            : AnimationName;
    }
    else if (bIsSlot)
    {
        NodeFact.DisplayName = CurrentSlotName;
    }
    else if (!Node->MachineName.IsEmpty())
    {
        NodeFact.DisplayName = Node->MachineName;
    }
    else if (!AnimationName.IsEmpty())
    {
        NodeFact.DisplayName = AnimationName;
    }
    else if (!Node->PoseAlias.IsEmpty())
    {
        NodeFact.DisplayName = Node->PoseAlias;
    }
    else
    {
        NodeFact.DisplayName = Node->NodeType;
    }

    FString NodeKey = FString::Printf(
        TEXT("%s|%d|%s|%s|%s"),
        *Node->ChainId,
        Node->Depth,
        *Node->NodeType,
        *Node->MachineName,
        *SlotName);
    const FString BaseNodeKey = NodeKey;
    int32 DuplicateIndex = 1;
    while (OutFacts.Nodes.Contains(NodeKey))
    {
        NodeKey = FString::Printf(
            TEXT("%s|%d"),
            *BaseNodeKey,
            DuplicateIndex++);
    }
    OutFacts.Nodes.Add(NodeKey, MoveTemp(NodeFact));

    for (const TSharedPtr<FLuaAnimSnapshotNode>& Child : Node->Children)
    {
        CollectNodeFacts(
            Child,
            CurrentSlotName,
            CurrentSlotDepth,
            OutFacts);
    }
}

/**
 * 从一帧构造用于差异比较的离散事实；Transition 同一标识出现多次时保留最后一个最终结果。
 * 可在任意线程调用；Frame 可为空。返回值不持有 Frame 的裸指针。
 *
 * @param Frame 当前快照，只读且可为空。
 * @return 状态、动画、Transition 和节点数事实集合。
 */
FFrameFacts BuildFrameFacts(const TSharedPtr<FLuaAnimSnapshotFrame>& Frame)
{
    FFrameFacts Facts;
    if (!Frame.IsValid()) return Facts;
    for (const TSharedPtr<FLuaAnimSnapshotNode>& Root : Frame->Roots)
    {
        CollectNodeFacts(
            Root,
            FString(),
            MAX_int32,
            Facts);
    }
    for (const FLuaAnimTransitionSample& Transition : Frame->Transitions)
    {
        if (Transition.bIsFinal && !Transition.TransitionId.IsEmpty())
        {
            Facts.TransitionResults.Add(
                Transition.TransitionId,
                Transition.bRuleResult);
        }
    }
    return Facts;
}

/**
 * 比较字符串映射并追加新增、移除和修改项；键按字典序输出以保持描述稳定。
 * 可在任意线程调用；Before/After 只读，OutChanges 追加而不清空。
 *
 * @param Label 面向用户的值类别，如“变量”。
 * @param Before 上一帧映射。
 * @param After 当前帧映射。
 * @param OutChanges 接收中文变化描述。
 */
void AppendStringMapChanges(
    const FString& Label,
    const TMap<FString, FString>& Before,
    const TMap<FString, FString>& After,
    TArray<FString>& OutChanges)
{
    TArray<FString> Keys;
    Before.GetKeys(Keys);
    for (const TPair<FString, FString>& Pair : After)
    {
        if (!Before.Contains(Pair.Key)) Keys.Add(Pair.Key);
    }
    Keys.Sort();

    for (const FString& Key : Keys)
    {
        const FString* BeforeValue = Before.Find(Key);
        const FString* AfterValue = After.Find(Key);
        if (BeforeValue == nullptr && AfterValue != nullptr)
        {
            OutChanges.Add(FString::Printf(
                TEXT("%s %s = %s"),
                *Label,
                *Key,
                **AfterValue));
        }
        else if (BeforeValue != nullptr && AfterValue == nullptr)
        {
            OutChanges.Add(FString::Printf(
                TEXT("%s %s 已移除（原值 %s）"),
                *Label,
                *Key,
                **BeforeValue));
        }
        else if (BeforeValue != nullptr
            && AfterValue != nullptr
            && *BeforeValue != *AfterValue)
        {
            OutChanges.Add(FString::Printf(
                TEXT("%s %s: %s → %s"),
                *Label,
                *Key,
                **BeforeValue,
                **AfterValue));
        }
    }
}

/**
 * 比较曲线映射并追加新增、移除和数值变化项；小于 KINDA_SMALL_NUMBER 的抖动忽略。
 * 可在任意线程调用；Before/After 只读，OutChanges 追加而不清空。
 *
 * @param Before 上一帧曲线。
 * @param After 当前帧曲线。
 * @param OutChanges 接收中文变化描述。
 */
void AppendCurveChanges(
    const TMap<FString, double>& Before,
    const TMap<FString, double>& After,
    TArray<FString>& OutChanges)
{
    TArray<FString> Keys;
    Before.GetKeys(Keys);
    for (const TPair<FString, double>& Pair : After)
    {
        if (!Before.Contains(Pair.Key)) Keys.Add(Pair.Key);
    }
    Keys.Sort();

    for (const FString& Key : Keys)
    {
        const double* BeforeValue = Before.Find(Key);
        const double* AfterValue = After.Find(Key);
        if (BeforeValue == nullptr && AfterValue != nullptr)
        {
            OutChanges.Add(FString::Printf(
                TEXT("曲线 %s = %.4f"),
                *Key,
                *AfterValue));
        }
        else if (BeforeValue != nullptr && AfterValue == nullptr)
        {
            OutChanges.Add(FString::Printf(
                TEXT("曲线 %s 已移除（原值 %.4f）"),
                *Key,
                *BeforeValue));
        }
        else if (BeforeValue != nullptr
            && AfterValue != nullptr
            && !FMath::IsNearlyEqual(*BeforeValue, *AfterValue, KINDA_SMALL_NUMBER))
        {
            OutChanges.Add(FString::Printf(
                TEXT("曲线 %s: %.4f → %.4f"),
                *Key,
                *BeforeValue,
                *AfterValue));
        }
    }
}

/**
 * 生成当前快照相对上一快照的完整中文变化明细，供摘要生成、悬停和搜索。
 * 可在任意线程调用；Previous 可为空表示首帧，Current 为空时返回“无效快照”。
 *
 * @param Previous 排序后的上一帧，可为空。
 * @param Current 当前帧，可为空。
 * @return 以中文分号分隔的确定性变化描述。
 */
FString BuildChangeDetails(
    const TSharedPtr<FLuaAnimSnapshotFrame>& Previous,
    const TSharedPtr<FLuaAnimSnapshotFrame>& Current)
{
    if (!Current.IsValid()) return TEXT("无效快照");

    const FFrameFacts CurrentFacts = BuildFrameFacts(Current);
    TArray<FString> Changes;
    if (!Previous.IsValid())
    {
        Changes.Add(TEXT("开始记录"));
        TArray<FString> StateNames;
        CurrentFacts.States.GetKeys(StateNames);
        StateNames.Sort();
        for (const FString& StateName : StateNames)
        {
            Changes.Add(FString::Printf(
                TEXT("当前状态 %s = %s"),
                *StateName,
                *CurrentFacts.States[StateName]));
        }
        TArray<FString> AnimationNames;
        for (const FString& AnimationName : CurrentFacts.Animations)
        {
            AnimationNames.Add(AnimationName);
        }
        AnimationNames.Sort();
        for (const FString& AnimationName : AnimationNames)
        {
            Changes.Add(TEXT("当前动画 ") + AnimationName);
        }
        return FString::Join(Changes, TEXT("；"));
    }

    const FFrameFacts PreviousFacts = BuildFrameFacts(Previous);
    TArray<FString> StateNames;
    PreviousFacts.States.GetKeys(StateNames);
    for (const TPair<FString, FString>& Pair : CurrentFacts.States)
    {
        if (!PreviousFacts.States.Contains(Pair.Key)) StateNames.Add(Pair.Key);
    }
    StateNames.Sort();
    for (const FString& StateName : StateNames)
    {
        const FString* BeforeState = PreviousFacts.States.Find(StateName);
        const FString* AfterState = CurrentFacts.States.Find(StateName);
        if (BeforeState == nullptr && AfterState != nullptr)
        {
            Changes.Add(FString::Printf(
                TEXT("状态 %s: <无> → %s"),
                *StateName,
                **AfterState));
        }
        else if (BeforeState != nullptr && AfterState == nullptr)
        {
            Changes.Add(FString::Printf(
                TEXT("状态 %s: %s → <无>"),
                *StateName,
                **BeforeState));
        }
        else if (BeforeState != nullptr
            && AfterState != nullptr
            && *BeforeState != *AfterState)
        {
            Changes.Add(FString::Printf(
                TEXT("状态 %s: %s → %s"),
                *StateName,
                **BeforeState,
                **AfterState));
        }
    }

    TArray<FString> AnimationNames;
    for (const FString& AnimationName : CurrentFacts.Animations)
    {
        if (!PreviousFacts.Animations.Contains(AnimationName))
        {
            AnimationNames.Add(AnimationName);
        }
    }
    AnimationNames.Sort();
    for (const FString& AnimationName : AnimationNames)
    {
        Changes.Add(TEXT("动画开始 ") + AnimationName);
    }
    AnimationNames.Reset();
    for (const FString& AnimationName : PreviousFacts.Animations)
    {
        if (!CurrentFacts.Animations.Contains(AnimationName))
        {
            AnimationNames.Add(AnimationName);
        }
    }
    AnimationNames.Sort();
    for (const FString& AnimationName : AnimationNames)
    {
        Changes.Add(TEXT("动画结束 ") + AnimationName);
    }

    if (PreviousFacts.ActiveNodeCount != CurrentFacts.ActiveNodeCount)
    {
        Changes.Add(FString::Printf(
            TEXT("活跃节点 %d → %d"),
            PreviousFacts.ActiveNodeCount,
            CurrentFacts.ActiveNodeCount));
    }
    AppendStringMapChanges(
        TEXT("变量"),
        Previous->Variables,
        Current->Variables,
        Changes);
    AppendCurveChanges(
        Previous->Curves,
        Current->Curves,
        Changes);

    TArray<FString> TransitionIds;
    PreviousFacts.TransitionResults.GetKeys(TransitionIds);
    for (const TPair<FString, bool>& Pair : CurrentFacts.TransitionResults)
    {
        if (!PreviousFacts.TransitionResults.Contains(Pair.Key))
        {
            TransitionIds.Add(Pair.Key);
        }
    }
    TransitionIds.Sort();
    for (const FString& TransitionId : TransitionIds)
    {
        const bool* BeforeResult =
            PreviousFacts.TransitionResults.Find(TransitionId);
        const bool* AfterResult =
            CurrentFacts.TransitionResults.Find(TransitionId);
        if (AfterResult == nullptr) continue;
        if (BeforeResult == nullptr || *BeforeResult != *AfterResult)
        {
            Changes.Add(FString::Printf(
                TEXT("Transition %s: %s → %s"),
                *TransitionId,
                BeforeResult == nullptr
                    ? TEXT("<无>")
                    : (*BeforeResult ? TEXT("通过") : TEXT("未通过")),
                *AfterResult ? TEXT("通过") : TEXT("未通过")));
        }
    }

    return Changes.IsEmpty()
        ? TEXT("与上一快照相比无可见变化")
        : FString::Join(Changes, TEXT("；"));
}

struct FChangeTitleCandidate
{
    FString Title; // 面向用户的具体变化
    int32 EffectiveDepth = MAX_int32; // 到 Root 的有效深度
    int32 SemanticPriority = MAX_int32; // 同深度时的语义优先级
};

/** 返回节点是否只是最终 Root 容器；Root 自身不作为变化标题。 */
bool IsRootContainer(const FNodeFact& NodeFact)
{
    return NodeFact.NodeType.Contains(
        TEXT("Root"),
        ESearchCase::IgnoreCase);
}

/**
 * 用更靠近 Root、同深度语义更明确的候选替换当前标题。
 * 可在任意线程调用；Title 为空的候选被忽略，OutCandidate 原地更新。
 *
 * @param Title 具体变化标题。
 * @param EffectiveDepth 变化影响到 Root 的最短节点深度，越小越重要。
 * @param SemanticPriority 同深度优先级，Montage/Slot 小于状态机和普通节点。
 * @param OutCandidate 当前最佳候选。
 */
void ConsiderTitleCandidate(
    const FString& Title,
    const int32 EffectiveDepth,
    const int32 SemanticPriority,
    FChangeTitleCandidate& OutCandidate)
{
    if (Title.IsEmpty()) return;
    const bool bIsBetter =
        EffectiveDepth < OutCandidate.EffectiveDepth
        || (EffectiveDepth == OutCandidate.EffectiveDepth
            && SemanticPriority < OutCandidate.SemanticPriority)
        || (EffectiveDepth == OutCandidate.EffectiveDepth
            && SemanticPriority == OutCandidate.SemanticPriority
            && (OutCandidate.Title.IsEmpty()
                || Title.Compare(OutCandidate.Title) < 0));
    if (!bIsBetter) return;

    OutCandidate.Title = Title;
    OutCandidate.EffectiveDepth = EffectiveDepth;
    OutCandidate.SemanticPriority = SemanticPriority;
}

/**
 * 为节点进入或离开活跃输出层级生成具体标题，Montage 会显示所属 Slot。
 * 可在任意线程调用；Root 容器返回空字符串。
 *
 * @param NodeFact 节点离散事实。
 * @param bStarted true 表示节点开始参与输出，false 表示结束。
 * @return 面向用户的具体节点变化标题。
 */
FString BuildNodePresenceTitle(
    const FNodeFact& NodeFact,
    const bool bStarted)
{
    if (IsRootContainer(NodeFact)) return FString();
    const bool bIsMontage = NodeFact.NodeType.Contains(
        TEXT("Montage"),
        ESearchCase::IgnoreCase);
    const bool bIsSlot = NodeFact.NodeType.Contains(
        TEXT("Slot"),
        ESearchCase::IgnoreCase);
    if (bIsMontage && !NodeFact.ParentSlotName.IsEmpty())
    {
        return FString::Printf(
            TEXT("Slot %s：Montage %s %s"),
            *NodeFact.ParentSlotName,
            bStarted ? TEXT("开始") : TEXT("结束"),
            *NodeFact.DisplayName);
    }
    if (bIsMontage)
    {
        return FString::Printf(
            TEXT("Montage %s：%s"),
            bStarted ? TEXT("开始") : TEXT("结束"),
            *NodeFact.DisplayName);
    }
    if (!NodeFact.CurrentState.IsEmpty())
    {
        return FString::Printf(
            TEXT("%s：%s %s"),
            *NodeFact.DisplayName,
            bStarted ? TEXT("进入") : TEXT("离开"),
            *NodeFact.CurrentState);
    }
    if (!NodeFact.AnimationName.IsEmpty())
    {
        return FString::Printf(
            TEXT("动画%s：%s"),
            bStarted ? TEXT("开始") : TEXT("结束"),
            *NodeFact.AnimationName);
    }
    if (bIsSlot)
    {
        return FString::Printf(
            TEXT("Slot %s %s参与输出"),
            *NodeFact.DisplayName,
            bStarted ? TEXT("开始") : TEXT("结束"));
    }
    return FString::Printf(
        TEXT("%s %s参与输出"),
        *NodeFact.DisplayName,
        bStarted ? TEXT("开始") : TEXT("结束"));
}

/**
 * 选择当前帧相对上一帧最靠近 Root 的具体节点变化作为卡片标题。
 * Montage 使用所属 Slot 深度参与比较，因此近 Root 的 Slot/Montage 会优先于深层状态机。
 * 节点没有离散变化时依次回退 Transition、变量、曲线和采样原因。
 *
 * @param Previous 排序后的上一帧，首帧可为空。
 * @param Current 当前帧，可为空。
 * @param ChangeDetails 已生成的完整变化明细。
 * @return 不使用泛化“状态变化”占位的具体标题。
 */
FString BuildChangeTitle(
    const TSharedPtr<FLuaAnimSnapshotFrame>& Previous,
    const TSharedPtr<FLuaAnimSnapshotFrame>& Current,
    const FString& ChangeDetails)
{
    if (!Current.IsValid()) return TEXT("无效快照");

    const FFrameFacts CurrentFacts = BuildFrameFacts(Current);
    FChangeTitleCandidate BestCandidate;
    if (!Previous.IsValid())
    {
        for (const TPair<FString, FNodeFact>& Pair : CurrentFacts.Nodes)
        {
            const FNodeFact& NodeFact = Pair.Value;
            const FString PresenceTitle = BuildNodePresenceTitle(
                NodeFact,
                true);
            ConsiderTitleCandidate(
                PresenceTitle.IsEmpty()
                    ? FString()
                    : TEXT("开始记录：") + PresenceTitle,
                NodeFact.EffectiveDepth,
                0,
                BestCandidate);
        }
        return BestCandidate.Title.IsEmpty()
            ? TEXT("开始记录")
            : BestCandidate.Title;
    }

    const FFrameFacts PreviousFacts = BuildFrameFacts(Previous);
    TArray<FString> NodeKeys;
    PreviousFacts.Nodes.GetKeys(NodeKeys);
    for (const TPair<FString, FNodeFact>& Pair : CurrentFacts.Nodes)
    {
        if (!PreviousFacts.Nodes.Contains(Pair.Key)) NodeKeys.Add(Pair.Key);
    }
    NodeKeys.Sort();

    for (const FString& NodeKey : NodeKeys)
    {
        const FNodeFact* BeforeNode = PreviousFacts.Nodes.Find(NodeKey);
        const FNodeFact* AfterNode = CurrentFacts.Nodes.Find(NodeKey);
        if (BeforeNode == nullptr && AfterNode != nullptr)
        {
            const int32 Priority = AfterNode->NodeType.Contains(
                TEXT("Montage"),
                ESearchCase::IgnoreCase)
                    || AfterNode->NodeType.Contains(
                        TEXT("Slot"),
                        ESearchCase::IgnoreCase)
                ? 0
                : 3;
            ConsiderTitleCandidate(
                BuildNodePresenceTitle(*AfterNode, true),
                AfterNode->EffectiveDepth,
                Priority,
                BestCandidate);
            continue;
        }
        if (BeforeNode != nullptr && AfterNode == nullptr)
        {
            const int32 Priority = BeforeNode->NodeType.Contains(
                TEXT("Montage"),
                ESearchCase::IgnoreCase)
                    || BeforeNode->NodeType.Contains(
                        TEXT("Slot"),
                        ESearchCase::IgnoreCase)
                ? 0
                : 3;
            ConsiderTitleCandidate(
                BuildNodePresenceTitle(*BeforeNode, false),
                BeforeNode->EffectiveDepth,
                Priority,
                BestCandidate);
            continue;
        }
        if (BeforeNode == nullptr || AfterNode == nullptr) continue;

        if (BeforeNode->CurrentState != AfterNode->CurrentState
            && (!BeforeNode->CurrentState.IsEmpty()
                || !AfterNode->CurrentState.IsEmpty()))
        {
            ConsiderTitleCandidate(
                FString::Printf(
                    TEXT("%s：%s → %s"),
                    *AfterNode->DisplayName,
                    BeforeNode->CurrentState.IsEmpty()
                        ? TEXT("<无>")
                        : *BeforeNode->CurrentState,
                    AfterNode->CurrentState.IsEmpty()
                        ? TEXT("<无>")
                        : *AfterNode->CurrentState),
                FMath::Min(
                    BeforeNode->EffectiveDepth,
                    AfterNode->EffectiveDepth),
                1,
                BestCandidate);
        }
        if (BeforeNode->AnimationName != AfterNode->AnimationName
            && (!BeforeNode->AnimationName.IsEmpty()
                || !AfterNode->AnimationName.IsEmpty()))
        {
            const bool bIsMontage = AfterNode->NodeType.Contains(
                TEXT("Montage"),
                ESearchCase::IgnoreCase);
            const FString TitlePrefix = bIsMontage
                && !AfterNode->ParentSlotName.IsEmpty()
                    ? TEXT("Slot ") + AfterNode->ParentSlotName
                        + TEXT("：Montage")
                    : TEXT("动画");
            ConsiderTitleCandidate(
                FString::Printf(
                    TEXT("%s：%s → %s"),
                    *TitlePrefix,
                    BeforeNode->AnimationName.IsEmpty()
                        ? TEXT("<无>")
                        : *BeforeNode->AnimationName,
                    AfterNode->AnimationName.IsEmpty()
                        ? TEXT("<无>")
                        : *AfterNode->AnimationName),
                FMath::Min(
                    BeforeNode->EffectiveDepth,
                    AfterNode->EffectiveDepth),
                bIsMontage ? 0 : 2,
                BestCandidate);
        }
    }
    if (!BestCandidate.Title.IsEmpty()) return BestCandidate.Title;

    TArray<FString> Details;
    ChangeDetails.ParseIntoArray(Details, TEXT("；"), true);
    const TArray<FString> Prefixes =
    {
        TEXT("Transition "),
        TEXT("变量 "),
        TEXT("曲线 "),
        TEXT("活跃节点 "),
    };
    for (const FString& Prefix : Prefixes)
    {
        for (const FString& Detail : Details)
        {
            if (Detail.StartsWith(Prefix)) return Detail;
        }
    }
    return Current->CaptureReason.Equals(
        TEXT("Interval"),
        ESearchCase::IgnoreCase)
            ? TEXT("定时采样：没有关键节点变化")
            : TEXT("动画输出层级发生变化");
}

/**
 * 把完整变化明细压缩为左侧列表的一句中文摘要，优先展示状态和动画，其余用数量概括。
 * 可在任意线程调用；ChangeDetails 允许为空。返回始终以中文句号结束的非空句子。
 *
 * @param ChangeDetails 以中文分号分隔的完整变化明细。
 * @return 最多展示两个代表变化的一句话摘要。
 */
FString BuildChangeDescription(const FString& ChangeDetails)
{
    if (ChangeDetails.IsEmpty())
    {
        return TEXT("本次记录没有可见变化。");
    }

    TArray<FString> Details;
    ChangeDetails.ParseIntoArray(Details, TEXT("；"), true);
    if (Details.IsEmpty()) return TEXT("本次记录没有可见变化。");

    TArray<FString> SummaryParts;
    if (Details[0] == TEXT("开始记录"))
    {
        SummaryParts.Add(TEXT("开始记录"));
        for (const FString& Detail : Details)
        {
            if (SummaryParts.Num() >= 3) break;
            if (Detail.StartsWith(TEXT("当前状态 "))
                || Detail.StartsWith(TEXT("当前动画 ")))
            {
                SummaryParts.Add(Detail);
            }
        }
    }
    else
    {
        for (const FString& Detail : Details)
        {
            if (Detail.StartsWith(TEXT("状态 "))
                || Detail.StartsWith(TEXT("动画开始 "))
                || Detail.StartsWith(TEXT("动画结束 ")))
            {
                SummaryParts.Add(Detail);
                if (SummaryParts.Num() >= 2) break;
            }
        }
        for (const FString& Detail : Details)
        {
            if (SummaryParts.Num() >= 2) break;
            if (!SummaryParts.Contains(Detail)) SummaryParts.Add(Detail);
        }
    }

    const int32 HiddenChangeCount = FMath::Max(
        0,
        Details.Num() - SummaryParts.Num());
    FString Summary = FString::Join(SummaryParts, TEXT("，"));
    if (HiddenChangeCount > 0)
    {
        Summary += FString::Printf(
            TEXT("，另有 %d 项变化"),
            HiddenChangeCount);
    }
    if (!Summary.EndsWith(TEXT("。"))) Summary += TEXT("。");
    return Summary;
}
}

/**
 * 从磁盘读取一个 JSONL 文件并解析全部可用帧，坏行写入 Warnings 后跳过。
 * 可在任意线程调用，但调用方应避免在 Slate 绘制期间读取大文件。
 *
 * @param FilePath JSONL 文件绝对或项目相对路径，只读且不会修改文件。
 * @param OutDocument 输出文档；调用时原内容会被清空，失败时仍包含来源路径与中文警告。
 * @return 文件读取成功返回 true；文件不存在或不可读返回 false，单行解析失败不影响返回值。
 */
bool FLuaAnimSnapshotLoader::LoadFile(
    const FString& FilePath,
    FLuaAnimSnapshotDocument& OutDocument)
{
    FString JsonLines;
    if (!FFileHelper::LoadFileToString(JsonLines, *FilePath))
    {
        OutDocument = FLuaAnimSnapshotDocument();
        OutDocument.SourceFilePath = FilePath;
        OutDocument.Warnings.Add(FString::Printf(TEXT("无法读取快照文件：%s"), *FilePath));
        return false;
    }

    ParseJsonLines(JsonLines, FilePath, OutDocument);
    return true;
}

/**
 * 逐行解析 JSONL 文本；未知字段忽略、缺失字段保留默认值、损坏单行仅产生一条警告。
 * 可在任意线程调用；本函数不访问 UObject 或文件系统。
 *
 * @param JsonLines 完整 JSONL 文本，允许空行以及 LF/CRLF 混用。
 * @param SourceLabel 用于文档来源和警告的可读标签，不要求是有效路径。
 * @param OutDocument 输出文档；调用时会重置、稳定排序，并生成每帧相对上一帧的变化描述。
 */
void FLuaAnimSnapshotLoader::ParseJsonLines(
    const FString& JsonLines,
    const FString& SourceLabel,
    FLuaAnimSnapshotDocument& OutDocument)
{
    OutDocument = FLuaAnimSnapshotDocument();
    OutDocument.SourceFilePath = SourceLabel;

    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, false);
    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        const FString Line = Lines[LineIndex].TrimStartAndEnd();
        if (Line.IsEmpty()) continue;

        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
        {
            OutDocument.Warnings.Add(FString::Printf(
                TEXT("第 %d 行无法解析，已跳过。"),
                LineIndex + 1));
            continue;
        }
        OutDocument.Frames.Add(LuaAnimSnapshotLoaderPrivate::ParseFrame(Object));
    }

    OutDocument.Frames.StableSort([](
        const TSharedPtr<FLuaAnimSnapshotFrame>& Left,
        const TSharedPtr<FLuaAnimSnapshotFrame>& Right)
    {
        if (!Left.IsValid()) return false;
        if (!Right.IsValid()) return true;
        if (!FMath::IsNearlyEqual(
            Left->SessionElapsedSeconds,
            Right->SessionElapsedSeconds))
        {
            return Left->SessionElapsedSeconds < Right->SessionElapsedSeconds;
        }
        const int32 UtcOrder = Left->UtcTimestamp.Compare(Right->UtcTimestamp);
        if (UtcOrder != 0) return UtcOrder < 0;
        return Left->FrameIndex < Right->FrameIndex;
    });

    for (int32 FrameArrayIndex = 0;
        FrameArrayIndex < OutDocument.Frames.Num();
        ++FrameArrayIndex)
    {
        const TSharedPtr<FLuaAnimSnapshotFrame> PreviousFrame =
            FrameArrayIndex > 0
                ? OutDocument.Frames[FrameArrayIndex - 1]
                : nullptr;
        if (OutDocument.Frames[FrameArrayIndex].IsValid())
        {
            OutDocument.Frames[FrameArrayIndex]->ChangeDetails =
                LuaAnimSnapshotLoaderPrivate::BuildChangeDetails(
                    PreviousFrame,
                    OutDocument.Frames[FrameArrayIndex]);
            OutDocument.Frames[FrameArrayIndex]->ChangeTitle =
                LuaAnimSnapshotLoaderPrivate::BuildChangeTitle(
                    PreviousFrame,
                    OutDocument.Frames[FrameArrayIndex],
                    OutDocument.Frames[FrameArrayIndex]->ChangeDetails);
            OutDocument.Frames[FrameArrayIndex]->ChangeDescription =
                LuaAnimSnapshotLoaderPrivate::BuildChangeDescription(
                    OutDocument.Frames[FrameArrayIndex]->ChangeDetails);
        }
    }
}
