#include "SekiroLuaAnimSnapshotLoader.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SekiroLuaAnimSnapshotLoaderPrivate
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
TSharedPtr<FSekiroLuaAnimSnapshotNode> ParseNode(const TSharedPtr<FJsonObject>& Object)
{
    TSharedPtr<FSekiroLuaAnimSnapshotNode> Node =
        MakeShared<FSekiroLuaAnimSnapshotNode>();
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
FSekiroLuaAnimTransitionSample ParseTransition(const TSharedPtr<FJsonObject>& Object)
{
    FSekiroLuaAnimTransitionSample Sample;
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
TSharedPtr<FSekiroLuaAnimSnapshotFrame> ParseFrame(const TSharedPtr<FJsonObject>& Object)
{
    TSharedPtr<FSekiroLuaAnimSnapshotFrame> Frame =
        MakeShared<FSekiroLuaAnimSnapshotFrame>();

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
}

/**
 * 从磁盘读取一个 JSONL 文件并解析全部可用帧，坏行写入 Warnings 后跳过。
 * 可在任意线程调用，但调用方应避免在 Slate 绘制期间读取大文件。
 *
 * @param FilePath JSONL 文件绝对或项目相对路径，只读且不会修改文件。
 * @param OutDocument 输出文档；调用时原内容会被清空，失败时仍包含来源路径与中文警告。
 * @return 文件读取成功返回 true；文件不存在或不可读返回 false，单行解析失败不影响返回值。
 */
bool FSekiroLuaAnimSnapshotLoader::LoadFile(
    const FString& FilePath,
    FSekiroLuaAnimSnapshotDocument& OutDocument)
{
    FString JsonLines;
    if (!FFileHelper::LoadFileToString(JsonLines, *FilePath))
    {
        OutDocument = FSekiroLuaAnimSnapshotDocument();
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
 * @param OutDocument 输出文档；调用时会完全重置并按相对秒、UTC、FrameIndex 稳定排序。
 */
void FSekiroLuaAnimSnapshotLoader::ParseJsonLines(
    const FString& JsonLines,
    const FString& SourceLabel,
    FSekiroLuaAnimSnapshotDocument& OutDocument)
{
    OutDocument = FSekiroLuaAnimSnapshotDocument();
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
        OutDocument.Frames.Add(SekiroLuaAnimSnapshotLoaderPrivate::ParseFrame(Object));
    }

    OutDocument.Frames.StableSort([](
        const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Left,
        const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Right)
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
}
