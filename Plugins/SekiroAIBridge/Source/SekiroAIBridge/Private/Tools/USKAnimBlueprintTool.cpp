#include "Tools/USKAnimBlueprintTool.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_Root.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimationGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimationTransitionGraph.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "Misc/Paths.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

// Commandlet 模式下资产加载辅助函数
// 尝试多种方式加载资产：FullObjectPath、LoadPackage+FindObject、StaticLoadObject
template<typename T>
static T* LoadAssetHelper(const FString& AssetPath)
{
    // 从路径中提取包名和资产名
    // "/Game/Characters/Sekiro/Sekiro_Skeleton" → Pkg="/Game/.../Sekiro_Skeleton", Name="Sekiro_Skeleton"
    FString PkgPath = AssetPath;
    FString AssetName;
    int32 LastSlash;
    if (PkgPath.FindLastChar('/', LastSlash))
    {
        AssetName = PkgPath.RightChop(LastSlash + 1);
    }
    else
    {
        AssetName = PkgPath;
    }

    // 移除可能存在的 .AssetName 后缀
    int32 LastPeriod;
    if (PkgPath.FindLastChar('.', LastPeriod))
    {
        PkgPath.LeftInline(LastPeriod);
    }

    // 方式1: 先加载包，再从包内遍历匹配类型（Commandlet 模式首选）
    // 原因：LoadPackage 会成功，但包内对象名可能比预期多后缀（如 Sekiro_Skeleton_Skeleton），
    // LoadObject/FindObject 按名称查找会失败；GetObjectsWithOuter 按类型匹配是唯一可靠方式
    UPackage* Pkg = LoadPackage(nullptr, *PkgPath, LOAD_None);
    if (Pkg)
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Pkg, Objects, false);
        for (UObject* Obj : Objects)
        {
            if (T* Match = Cast<T>(Obj))
            {
                return Match;
            }
        }
    }

    // 方式2: LoadObject with full path (e.g., /Game/.../AssetName.AssetName)
    FString FullPath = FString::Printf(TEXT("%s.%s"), *PkgPath, *AssetName);
    if (T* Result = LoadObject<T>(nullptr, *FullPath))
    {
        return Result;
    }

    // 方式3: LoadObject with short path
    if (T* Result = LoadObject<T>(nullptr, *AssetPath))
    {
        return Result;
    }

    return nullptr;
}

static void AppendUtf8Needle(const FString& Text, TArray<uint8>& OutNeedle)
{
    FTCHARToUTF8 ConvertedText(*Text);
    OutNeedle.Append(reinterpret_cast<const uint8*>(ConvertedText.Get()), ConvertedText.Length());
}

static void AppendUtf16LeNeedle(const FString& Text, TArray<uint8>& OutNeedle)
{
    for (int32 CharIndex = 0; CharIndex < Text.Len(); ++CharIndex)
    {
        const uint16 Character = static_cast<uint16>(Text[CharIndex]);
        OutNeedle.Add(static_cast<uint8>(Character & 0xFF));
        OutNeedle.Add(static_cast<uint8>((Character >> 8) & 0xFF));
    }
}

static bool ByteArrayContainsNeedle(const TArray<uint8>& Data, const TArray<uint8>& Needle)
{
    if (Needle.Num() <= 0 || Data.Num() < Needle.Num()) return false;

    const int32 LastStartIndex = Data.Num() - Needle.Num();
    for (int32 DataIndex = 0; DataIndex <= LastStartIndex; ++DataIndex)
    {
        bool bMatched = true;
        for (int32 NeedleIndex = 0; NeedleIndex < Needle.Num(); ++NeedleIndex)
        {
            if (Data[DataIndex + NeedleIndex] != Needle[NeedleIndex])
            {
                bMatched = false;
                break;
            }
        }

        if (bMatched) return true;
    }

    return false;
}

static void BuildCurveNameNeedles(const TArray<FName>& CurveNames, TArray<TArray<uint8>>& OutNeedles)
{
    OutNeedles.Reset();
    for (const FName& CurveName : CurveNames)
    {
        const FString CurveNameText = CurveName.ToString();

        TArray<uint8> Utf8Needle;
        AppendUtf8Needle(CurveNameText, Utf8Needle);
        OutNeedles.Add(Utf8Needle);

        TArray<uint8> Utf16Needle;
        AppendUtf16LeNeedle(CurveNameText, Utf16Needle);
        OutNeedles.Add(Utf16Needle);
    }
}

static bool FileContainsAnyNeedle(const FString& Filename, const TArray<TArray<uint8>>& Needles)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *Filename)) return false;

    for (const TArray<uint8>& Needle : Needles)
    {
        if (ByteArrayContainsNeedle(FileBytes, Needle))
        {
            return true;
        }
    }

    return false;
}

static void FindCurveCandidatePackages(const TArray<FName>& CurveNames, TSet<FName>& OutPackageNames)
{
    OutPackageNames.Reset();

    TArray<TArray<uint8>> Needles;
    BuildCurveNameNeedles(CurveNames, Needles);

    TArray<FString> AssetFiles;
    IFileManager::Get().FindFilesRecursive(
        AssetFiles,
        *FPaths::ProjectContentDir(),
        TEXT("*.uasset"),
        true,
        false);

    for (const FString& AssetFile : AssetFiles)
    {
        if (!FileContainsAnyNeedle(AssetFile, Needles)) continue;

        FString PackageName;
        if (FPackageName::TryConvertFilenameToLongPackageName(AssetFile, PackageName))
        {
            OutPackageNames.Add(FName(*PackageName));
        }
    }
}

static bool ResolveFloatCurveIdentifier(const UAnimSequence* AnimSequence, FName CurveName, FAnimationCurveIdentifier& OutCurveId)
{
    if (!AnimSequence || CurveName.IsNone()) return false;

    const USkeleton* Skeleton = AnimSequence->GetSkeleton();
    if (!Skeleton) return false;

    FSmartName SmartName;
    if (!Skeleton->GetSmartNameByName(USkeleton::AnimCurveMappingName, CurveName, SmartName))
    {
        return false;
    }

    OutCurveId = FAnimationCurveIdentifier(SmartName, ERawCurveTrackTypes::RCT_Float);
    return AnimSequence->GetDataModel() && AnimSequence->GetDataModel()->FindCurve(OutCurveId) != nullptr;
}

static bool ReadFloatCurveKeysFromJson(const TArray<TSharedPtr<FJsonValue>>& KeyValues, ERichCurveInterpMode InterpMode, TArray<FRichCurveKey>& OutKeys)
{
    OutKeys.Reset();

    for (const TSharedPtr<FJsonValue>& KeyValue : KeyValues)
    {
        const TSharedPtr<FJsonObject>* KeyObject = nullptr;
        if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObject)) continue;

        if (!(*KeyObject)->HasTypedField<EJson::Number>(TEXT("time")) ||
            !(*KeyObject)->HasTypedField<EJson::Number>(TEXT("value")))
        {
            continue;
        }

        FRichCurveKey Key;
        Key.Time = static_cast<float>((*KeyObject)->GetNumberField(TEXT("time")));
        Key.Value = static_cast<float>((*KeyObject)->GetNumberField(TEXT("value")));
        Key.InterpMode = InterpMode;
        OutKeys.Add(Key);
    }

    return OutKeys.Num() > 0;
}

FString USKAnimBlueprintTool::GetToolDescription() const
{
    return TEXT("动画蓝图操作（通用接口）：创建/编译AnimBP（支持自定义parent_class）、管理状态机（状态/转换）、添加动画节点（SequencePlayer/BlendSpacePlayer/Slot）、设置AnimGraph根节点、创建BlendSpace资产、查询结构。");
}

FString USKAnimBlueprintTool::GetInputSchemaJson() const
{
    return TEXT("{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"add_state\",\"add_transition\",\"delete_transition\",\"add_node\",\"add_slot\",\"upsert_skeleton_slot\",\"remove_state_machine\",\"add_curve\",\"set_anim_curves\",\"batch_tae_curves\",\"remove_anim_curves\",\"get_info\",\"compile\",\"setup_anim_graph\",\"create_blend_space\",\"set_anim_class\",\"layout\"]},"
            "\"path\":{\"type\":\"string\",\"description\":\"AnimBlueprint或BlendSpace资产路径\"},"
            "\"skeleton_path\":{\"type\":\"string\",\"description\":\"目标骨架路径\"},"
            "\"slot_name\":{\"type\":\"string\",\"description\":\"Slot 名称\"},"
            "\"slot_group_name\":{\"type\":\"string\",\"description\":\"Slot Group 名称\"},"
            "\"state_machine_name\":{\"type\":\"string\",\"description\":\"待删除的断开状态机名称\"},"
            "\"parent_class\":{\"type\":\"string\",\"description\":\"可选：AnimInstance父类脚本路径，如/Script/ModuleName.ClassName\"},"
            "\"state_name\":{\"type\":\"string\"},"
            "\"from_state\":{\"type\":\"string\"},\"to_state\":{\"type\":\"string\"},"
            "\"crossfade_duration\":{\"type\":\"number\",\"default\":0.2},"
            "\"blend_mode\":{\"type\":\"string\",\"enum\":[\"linear\",\"cubic\",\"hermite_cubic\",\"sinusoidal\",\"quadratic_in_out\",\"cubic_in_out\",\"quartic_in_out\",\"quintic_in_out\",\"circular_in_out\",\"exp_in_out\",\"custom\"]},"
            "\"bidirectional\":{\"type\":\"boolean\",\"default\":false},"
            "\"bAutomaticRuleBasedOnSequencePlayerInState\":{\"type\":\"boolean\",\"description\":\"auto transition when source anim finishes\"},"
            "\"condition\":{\"type\":\"object\",\"description\":\"add_transition: 条件 {type:bool|not_bool|time_remaining, variable:string}\"},"
            "\"node_type\":{\"type\":\"string\",\"enum\":[\"sequence_player\",\"blend_space_player\"]},"
            "\"asset_path\":{\"type\":\"string\",\"description\":\"动画资产路径（AnimSequence/BlendSpace）\"},"
            "\"play_rate\":{\"type\":\"number\",\"default\":1.0},"
            "\"loop\":{\"type\":\"boolean\",\"default\":true},"
            "\"pin_connections\":{\"type\":\"object\",\"description\":\"add_node blend_space_player: {X:VariableName, Y:VariableName}\"},"
            "\"blend_space_path\":{\"type\":\"string\",\"description\":\"setup_anim_graph: BlendSpace资产路径\"},"
            "\"axes\":{\"type\":\"array\",\"description\":\"create_blend_space: 坐标轴 [{name,min,max,grid}]\",\"items\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"min\":{\"type\":\"number\"},\"max\":{\"type\":\"number\"},\"grid\":{\"type\":\"integer\"}}}},"
            "\"samples\":{\"type\":\"array\",\"description\":\"create_blend_space: 样本 [{anim_path,x,y}]\",\"items\":{\"type\":\"object\",\"properties\":{\"anim_path\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}}}},"
            "\"character_bp_path\":{\"type\":\"string\",\"description\":\"set_anim_class: 角色Blueprint路径\"},"
            "\"anim_bp_path\":{\"type\":\"string\",\"description\":\"set_anim_class: AnimBlueprint路径（或直接用path参数）\"},"
            "\"mesh_component_name\":{\"type\":\"string\",\"description\":\"set_anim_class: Mesh组件变量名，默认Mesh\",\"default\":\"Mesh\"},"
            "\"curve_names\":{\"type\":\"array\",\"description\":\"remove_anim_curves: 要删除的曲线名\"},"
            "\"animations\":{\"type\":\"array\",\"description\":\"set_anim_curves: 动画曲线批量写入 [{path,curves:[{name,curve_type,keys:[{time,value}]}]}]\"},"
            "\"candidate_scan\":{\"type\":\"boolean\",\"description\":\"remove_anim_curves: 是否先按uasset文件内容筛选候选包\",\"default\":true},"
            "\"dry_run\":{\"type\":\"boolean\",\"description\":\"remove_anim_curves: 只统计不保存\",\"default\":false},"
            "\"save\":{\"type\":\"boolean\",\"description\":\"remove_anim_curves: 是否保存修改资产\",\"default\":true},"
            "\"batch_size\":{\"type\":\"integer\",\"description\":\"remove_anim_curves: 批大小\"},"
            "\"batch_index\":{\"type\":\"integer\",\"description\":\"remove_anim_curves: 批索引\"}"
        "},"
        "\"required\":[\"action\",\"path\"]"
    "}");
}

FString USKAnimBlueprintTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Action = ArgsObj->GetStringField(TEXT("action"));
        FString Path = ArgsObj->GetStringField(TEXT("path"));
        return FString::Printf(TEXT("AnimBlueprint操作: %s -> %s"), *Action, *Path);
    }
    return TEXT("AnimBlueprint操作");
}

FString USKAnimBlueprintTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Action;
    if (!ArgsObj->TryGetStringField(TEXT("action"), Action))
    {
        OutError = TEXT("缺少 action 参数");
        return FString();
    }

    if (Action == TEXT("create"))              return HandleCreate(ArgsObj, OutError);
    if (Action == TEXT("add_state"))           return HandleAddState(ArgsObj, OutError);
    if (Action == TEXT("add_transition"))      return HandleAddTransition(ArgsObj, OutError);
    if (Action == TEXT("delete_transition"))   return HandleDeleteTransition(ArgsObj, OutError);
    if (Action == TEXT("add_node"))            return HandleAddAnimNode(ArgsObj, OutError);
    if (Action == TEXT("get_info"))            return HandleGetInfo(ArgsObj, OutError);
    if (Action == TEXT("compile"))             return HandleCompile(ArgsObj, OutError);
    if (Action == TEXT("setup_anim_graph"))    return HandleSetupAnimGraph(ArgsObj, OutError);
    if (Action == TEXT("create_blend_space"))  return HandleCreateBlendSpace(ArgsObj, OutError);
    if (Action == TEXT("set_anim_class"))      return HandleSetAnimClass(ArgsObj, OutError);
    if (Action == TEXT("layout"))              return HandleLayout(ArgsObj, OutError);
    if (Action == TEXT("rename_node"))         return HandleRenameNode(ArgsObj, OutError);
    if (Action == TEXT("add_slot"))            return HandleAddSlotNode(ArgsObj, OutError);
    if (Action == TEXT("upsert_skeleton_slot")) return HandleUpsertSkeletonSlot(ArgsObj, OutError);
    if (Action == TEXT("remove_state_machine")) return HandleRemoveStateMachine(ArgsObj, OutError);
    if (Action == TEXT("add_curve"))           return HandleAddCurve(ArgsObj, OutError);
    if (Action == TEXT("set_anim_curves"))     return HandleSetAnimCurves(ArgsObj, OutError);
    if (Action == TEXT("batch_tae_curves"))     return HandleBatchTaeCurves(ArgsObj, OutError);
    if (Action == TEXT("remove_anim_curves"))  return HandleRemoveAnimCurves(ArgsObj, OutError);

    OutError = FString::Printf(TEXT("未知操作: %s"), *Action);
    return FString();
}

// ============================================================================
// HandleCreate — 创建 AnimBlueprint
// ============================================================================

FString USKAnimBlueprintTool::HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString SkeletonPath;
    if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        OutError = TEXT("缺少 skeleton_path 参数（目标骨架路径）");
        return FString();
    }

    USkeleton* Skeleton = LoadAssetHelper<USkeleton>(SkeletonPath);
    if (!Skeleton)
    {
        OutError = FString::Printf(TEXT("骨架未找到: %s"), *SkeletonPath);
        return FString();
    }

    int32 LastSlash;
    if (!AssetPath.FindLastChar('/', LastSlash))
    {
        OutError = TEXT("无效的资产路径");
        return FString();
    }
    FString PackagePath = AssetPath.Left(LastSlash);
    FString BPName = AssetPath.RightChop(LastSlash + 1);

    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        // 已存在：加载并返回（不重复创建）
        UAnimBlueprint* Existing = LoadAssetHelper<UAnimBlueprint>(AssetPath);
        if (!Existing)
        {
            OutError = FString::Printf(TEXT("AnimBlueprint已存在但加载失败: %s"), *AssetPath);
            return FString();
        }
        TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
        ResultObj->SetStringField(TEXT("path"), AssetPath);
        ResultObj->SetStringField(TEXT("name"), BPName);
        ResultObj->SetStringField(TEXT("skeleton"), SkeletonPath);
        ResultObj->SetStringField(TEXT("type"), TEXT("AnimBlueprint"));
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetStringField(TEXT("status"), TEXT("already_exists"));
        FString Output;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
        FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
        return Output;
    }

    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = TEXT("创建Package失败");
        return FString();
    }

    // 确定父类：优先使用脚本层传入的 parent_class，否则用 UAnimInstance
    UClass* ParentClass = UAnimInstance::StaticClass();
    FString ParentClassPath;
    if (Args->TryGetStringField(TEXT("parent_class"), ParentClassPath) && !ParentClassPath.IsEmpty())
    {
        if (UClass* CustomClass = LoadObject<UClass>(nullptr, *ParentClassPath))
        {
            ParentClass = CustomClass;
        }
        else
        {
            OutError = FString::Printf(TEXT("指定的 parent_class 未找到: %s"), *ParentClassPath);
            return FString();
        }
    }

    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        FName(*BPName),
        BPTYPE_Normal,
        UAnimBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass()
    );

    if (!BP)
    {
        OutError = TEXT("创建AnimBlueprint失败");
        return FString();
    }

    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
    if (!AnimBP)
    {
        OutError = TEXT("创建的Blueprint不是AnimBlueprint类型");
        return FString();
    }

    AnimBP->TargetSkeleton = Skeleton;
    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);
    FAssetRegistryModule::AssetCreated(AnimBP);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), BPName);
    ResultObj->SetStringField(TEXT("skeleton"), SkeletonPath);
    ResultObj->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
    ResultObj->SetStringField(TEXT("type"), TEXT("AnimBlueprint"));
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddState — 向状态机添加状态
// ============================================================================

FString USKAnimBlueprintTool::HandleAddState(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString StateName;
    if (!Args->TryGetStringField(TEXT("state_name"), StateName))
    {
        OutError = TEXT("缺少 state_name 参数");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimBP, OutError);
    if (!SMNode) return FString();

    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

    // 检查重名
    if (FindStateNode(SMGraph, StateName))
    {
        OutError = FString::Printf(TEXT("状态已存在: %s"), *StateName);
        return FString();
    }

    int32 PosX = 200, PosY = 0;
    if (!Args->HasField(TEXT("x")) && !Args->HasField(TEXT("y")))
    {
        // 自动计算网格位置：统计已有状态数量，4列排列，间距500x350
        int32 StateCount = 0;
        for (UEdGraphNode* Node : SMGraph->Nodes)
        {
            if (Cast<UAnimStateNode>(Node)) ++StateCount;
        }
        static const int32 Cols = 4, ColSpacing = 500, RowSpacing = 350;
        PosX = 200 + (StateCount % Cols) * ColSpacing;
        PosY = 0   + (StateCount / Cols) * RowSpacing;
    }
    else
    {
        Args->TryGetNumberField(TEXT("x"), PosX);
        Args->TryGetNumberField(TEXT("y"), PosY);
    }

    FGraphNodeCreator<UAnimStateNode> NodeCreator(*SMGraph);
    UAnimStateNode* StateNode = NodeCreator.CreateNode();
    StateNode->NodePosX = PosX;
    StateNode->NodePosY = PosY;
    NodeCreator.Finalize();

    // 重命名为用户指定的名称
    if (StateNode->BoundGraph)
    {
        StateNode->BoundGraph->Rename(*StateName);
    }

    // 查找 Entry 节点并连接
    UAnimStateEntryNode* EntryNode = nullptr;
    for (UEdGraphNode* Node : SMGraph->Nodes)
    {
        if (UAnimStateEntryNode* EN = Cast<UAnimStateEntryNode>(Node))
        {
            EntryNode = EN;
            break;
        }
    }
    if (EntryNode)
    {
        UEdGraphPin* EntryOut = EntryNode->FindPin(TEXT("Entry"), EGPD_Output);
        UEdGraphPin* StateIn = StateNode->FindPin(TEXT("In"), EGPD_Input);
        // 仅第一个状态连 Entry，避免多重入口
        if (EntryOut && StateIn && EntryOut->LinkedTo.Num() == 0)
        {
            EntryOut->MakeLinkTo(StateIn);
        }
    }

    // 将状态节点名称更新
    StateNode->Rename(*StateName);

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("state"), StateName);
    ResultObj->SetStringField(TEXT("node_id"), StateNode->GetFName().ToString());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddTransition — 创建状态间转换
// ============================================================================

FString USKAnimBlueprintTool::HandleAddTransition(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString FromState, ToState;
    if (!Args->TryGetStringField(TEXT("from_state"), FromState))
    {
        OutError = TEXT("缺少 from_state 参数");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("to_state"), ToState))
    {
        OutError = TEXT("缺少 to_state 参数");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimBP, OutError);
    if (!SMNode) return FString();

    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

    UAnimStateNode* FromNode = FindStateNode(SMGraph, FromState);
    if (!FromNode)
    {
        OutError = FString::Printf(TEXT("源状态未找到: %s"), *FromState);
        return FString();
    }
    UAnimStateNode* ToNode = FindStateNode(SMGraph, ToState);
    if (!ToNode)
    {
        OutError = FString::Printf(TEXT("目标状态未找到: %s"), *ToState);
        return FString();
    }

    // 检查是否已存在转换
    for (UEdGraphNode* Node : SMGraph->Nodes)
    {
        if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
        {
            if (TransNode->GetPreviousState() == FromNode && TransNode->GetNextState() == ToNode)
            {
                OutError = FString::Printf(TEXT("转换已存在: %s -> %s"), *FromState, *ToState);
                return FString();
            }
        }
    }

    FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*SMGraph);
    UAnimStateTransitionNode* TransNode = NodeCreator.CreateNode();
    NodeCreator.Finalize();

    // 设置转换属性
    double CrossfadeDuration = 0.2;
    Args->TryGetNumberField(TEXT("crossfade_duration"), CrossfadeDuration);
    TransNode->CrossfadeDuration = (float)CrossfadeDuration;

    FString BlendModeStr;
    if (Args->TryGetStringField(TEXT("blend_mode"), BlendModeStr))
    {
        TArray<FString> BlendModeNames = {
            TEXT("linear"), TEXT("cubic"), TEXT("hermite_cubic"), TEXT("sinusoidal"),
            TEXT("quadratic_in_out"), TEXT("cubic_in_out"), TEXT("quartic_in_out"),
            TEXT("quintic_in_out"), TEXT("circular_in_out"), TEXT("exp_in_out"), TEXT("custom")
        };
        int32 BlendIdx = BlendModeNames.Find(BlendModeStr);
        if (BlendIdx != INDEX_NONE)
        {
            TransNode->BlendMode = (EAlphaBlendOption)BlendIdx;
        }
    }

    bool bBidirectional = false;
    if (Args->TryGetBoolField(TEXT("bidirectional"), bBidirectional))
    {
        TransNode->Bidirectional = bBidirectional;
    }

    bool bAutoRule = false;
    if (Args->TryGetBoolField(TEXT("bAutomaticRuleBasedOnSequencePlayerInState"), bAutoRule))
    {
        TransNode->bAutomaticRuleBasedOnSequencePlayerInState = bAutoRule;
    }

    // 连接状态
    TransNode->CreateConnections(FromNode, ToNode);

    // 设置转换条件（必须在 CreateConnections 之后，此时 BoundGraph 已就绪）
    const TSharedPtr<FJsonObject>* ConditionObj = nullptr;
    if (Args->TryGetObjectField(TEXT("condition"), ConditionObj))
    {
        if (!SetupTransitionCondition(TransNode, *ConditionObj, OutError))
        {
            return FString();
        }
    }

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("from_state"), FromState);
    ResultObj->SetStringField(TEXT("to_state"), ToState);
    ResultObj->SetNumberField(TEXT("crossfade_duration"), CrossfadeDuration);
    ResultObj->SetBoolField(TEXT("bidirectional"), bBidirectional);
    ResultObj->SetBoolField(TEXT("auto_rule"), bAutoRule);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleDeleteTransition — 删除状态间转换
// ============================================================================

FString USKAnimBlueprintTool::HandleDeleteTransition(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString FromState, ToState;
    if (!Args->TryGetStringField(TEXT("from_state"), FromState)
        || !Args->TryGetStringField(TEXT("to_state"), ToState))
    {
        OutError = TEXT("缺少 from_state 或 to_state 参数");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimBP, OutError);
    if (!SMNode) return FString();

    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

    UAnimStateNode* FromNode = FindStateNode(SMGraph, FromState);
    UAnimStateNode* ToNode = FindStateNode(SMGraph, ToState);
    if (!FromNode || !ToNode)
    {
        OutError = TEXT("源或目标状态未找到");
        return FString();
    }

    UAnimStateTransitionNode* ToDelete = nullptr;
    for (UEdGraphNode* Node : SMGraph->Nodes)
    {
        if (UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node))
        {
            if (T->GetPreviousState() == FromNode && T->GetNextState() == ToNode)
            {
                ToDelete = T;
                break;
            }
        }
    }
    if (!ToDelete)
    {
        OutError = FString::Printf(TEXT("转换未找到: %s -> %s"), *FromState, *ToState);
        return FString();
    }

    ToDelete->DestroyNode();

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("from_state"), FromState);
    ResultObj->SetStringField(TEXT("to_state"), ToState);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddAnimNode — 向状态的动画图中添加动画节点
// ============================================================================

FString USKAnimBlueprintTool::HandleAddAnimNode(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString StateName;
    FString NodeType;
    if (!Args->TryGetStringField(TEXT("state_name"), StateName))
    {
        OutError = TEXT("缺少 state_name 参数（目标状态名）");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("node_type"), NodeType))
    {
        OutError = TEXT("缺少 node_type 参数（sequence_player / blend_space_player）");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimBP, OutError);
    if (!SMNode) return FString();

    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
    UAnimStateNode* StateNode = FindStateNode(SMGraph, StateName);
    if (!StateNode)
    {
        OutError = FString::Printf(TEXT("状态未找到: %s"), *StateName);
        return FString();
    }

    UEdGraph* StateGraph = StateNode->BoundGraph;
    if (!StateGraph)
    {
        OutError = TEXT("状态绑定图（BoundGraph）为空");
        return FString();
    }

    int32 PosX = 0, PosY = 0;
    if (!Args->HasField(TEXT("x")) && !Args->HasField(TEXT("y")))
    {
        // 自动偏移：统计状态内部图中已有节点数，每个节点Y偏移200
        int32 NodeCount = 0;
        for (UEdGraphNode* N : StateGraph->Nodes)
        {
            if (!Cast<UAnimGraphNode_StateResult>(N)) ++NodeCount;
        }
        PosX = -100;        // 居中偏左，给左侧 VariableGet 留空间
        PosY = NodeCount * 200;
    }
    else
    {
        Args->TryGetNumberField(TEXT("x"), PosX);
        Args->TryGetNumberField(TEXT("y"), PosY);
    }

    UEdGraphNode* NewNode = nullptr;

    if (NodeType == TEXT("sequence_player"))
    {
        FString AnimAssetPath;
        if (!Args->TryGetStringField(TEXT("asset_path"), AnimAssetPath))
        {
            OutError = TEXT("sequence_player 类型需要 asset_path 参数（AnimSequence路径）");
            return FString();
        }

        UAnimSequence* AnimSeq = LoadAssetHelper<UAnimSequence>(AnimAssetPath);
        if (!AnimSeq)
        {
            OutError = FString::Printf(TEXT("AnimSequence未找到: %s"), *AnimAssetPath);
            return FString();
        }

        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> NodeCreator(*StateGraph);
        UAnimGraphNode_SequencePlayer* SeqNode = NodeCreator.CreateNode();
        SeqNode->NodePosX = PosX;
        SeqNode->NodePosY = PosY;
        NodeCreator.Finalize();
        NewNode = SeqNode;

        // 通过反射设置 protected 成员（UPROPERTY 反射无视 C++ 访问级别）
        UScriptStruct* NodeStruct = FAnimNode_SequencePlayer::StaticStruct();
        void* NodeAddr = &SeqNode->Node;

        FObjectProperty* SeqProp = CastField<FObjectProperty>(NodeStruct->FindPropertyByName(TEXT("Sequence")));
        if (SeqProp) SeqProp->SetObjectPropertyValue(SeqProp->ContainerPtrToValuePtr<void>(NodeAddr), AnimSeq);

        double PlayRate = 1.0;
        if (Args->TryGetNumberField(TEXT("play_rate"), PlayRate))
        {
            FFloatProperty* RateProp = CastField<FFloatProperty>(NodeStruct->FindPropertyByName(TEXT("PlayRate")));
            if (RateProp) RateProp->SetFloatingPointPropertyValue(RateProp->ContainerPtrToValuePtr<void>(NodeAddr), (float)PlayRate);
        }

        bool bLoop = true;
        if (Args->TryGetBoolField(TEXT("loop"), bLoop))
        {
            FBoolProperty* LoopProp = CastField<FBoolProperty>(NodeStruct->FindPropertyByName(TEXT("bLoopAnimation")));
            if (LoopProp) LoopProp->SetPropertyValue(LoopProp->ContainerPtrToValuePtr<void>(NodeAddr), bLoop);
        }
    }
    else if (NodeType == TEXT("blend_space_player"))
    {
        FString BlendSpacePath;
        if (!Args->TryGetStringField(TEXT("asset_path"), BlendSpacePath))
        {
            OutError = TEXT("blend_space_player 类型需要 asset_path 参数（BlendSpace路径）");
            return FString();
        }

        UBlendSpace* BlendSpace = LoadAssetHelper<UBlendSpace>(BlendSpacePath);
        if (!BlendSpace)
        {
            OutError = FString::Printf(TEXT("BlendSpace未找到: %s"), *BlendSpacePath);
            return FString();
        }

        FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*StateGraph);
        UAnimGraphNode_BlendSpacePlayer* BspNode = NodeCreator.CreateNode();
        BspNode->NodePosX = PosX;
        BspNode->NodePosY = PosY;
        NodeCreator.Finalize();
        NewNode = BspNode;

        // 通过反射设置 private 成员
        UScriptStruct* NodeStruct = FAnimNode_BlendSpacePlayer::StaticStruct();
        void* NodeAddr = &BspNode->Node;

        FObjectProperty* BsProp = CastField<FObjectProperty>(NodeStruct->FindPropertyByName(TEXT("BlendSpace")));
        if (BsProp) BsProp->SetObjectPropertyValue(BsProp->ContainerPtrToValuePtr<void>(NodeAddr), BlendSpace);

        double PlayRate = 1.0;
        if (Args->TryGetNumberField(TEXT("play_rate"), PlayRate))
        {
            FFloatProperty* RateProp = CastField<FFloatProperty>(NodeStruct->FindPropertyByName(TEXT("PlayRate")));
            if (RateProp) RateProp->SetFloatingPointPropertyValue(RateProp->ContainerPtrToValuePtr<void>(NodeAddr), (float)PlayRate);
        }

        bool bLoop = true;
        if (Args->TryGetBoolField(TEXT("loop"), bLoop))
        {
            FBoolProperty* LoopProp = CastField<FBoolProperty>(NodeStruct->FindPropertyByName(TEXT("bLoop")));
            if (LoopProp) LoopProp->SetPropertyValue(LoopProp->ContainerPtrToValuePtr<void>(NodeAddr), bLoop);
        }

        // BlendSpace 参数引脚连接（X→Angle, Y→Speed）
        const TSharedPtr<FJsonObject>* PinConns = nullptr;
        if (Args->TryGetObjectField(TEXT("pin_connections"), PinConns))
        {
            SetupBlendSpacePinConnections(BspNode, StateGraph, *PinConns);
        }
    }
    else
    {
        OutError = FString::Printf(TEXT("不支持的 node_type: %s（支持: sequence_player, blend_space_player）"), *NodeType);
        return FString();
    }

    // 将新节点的 Pose 输出连接到 StateResult 节点的 Pose 输入
    UAnimGraphNode_StateResult* ResultNode = nullptr;
    for (UEdGraphNode* Node : StateGraph->Nodes)
    {
        if (UAnimGraphNode_StateResult* SR = Cast<UAnimGraphNode_StateResult>(Node))
        {
            ResultNode = SR;
            break;
        }
    }

    if (ResultNode)
    {
        UEdGraphPin* OutputPose = NewNode->FindPin(TEXT("Pose"), EGPD_Output);
        UEdGraphPin* InputPose = ResultNode->FindPin(TEXT("Result"), EGPD_Input);
        if (OutputPose && InputPose)
        {
            OutputPose->MakeLinkTo(InputPose);
        }
    }

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("state"), StateName);
    ResultObj->SetStringField(TEXT("node_type"), NodeType);
    ResultObj->SetStringField(TEXT("node_id"), NewNode->GetFName().ToString());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleGetInfo — 查询 AnimBlueprint 结构
// ============================================================================

FString USKAnimBlueprintTool::HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();
    return AnimBlueprintToJson(AnimBP);
}

// ============================================================================
// HandleCompile — 编译 AnimBlueprint
// ============================================================================

FString USKAnimBlueprintTool::HandleCompile(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetBoolField(TEXT("success"), AnimBP->Status != BS_Error);
    ResultObj->SetStringField(TEXT("status"), AnimBP->Status == BS_UpToDate ? TEXT("UpToDate") : (AnimBP->Status == BS_Error ? TEXT("Error") : TEXT("Dirty")));

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleSetupAnimGraph — 增量式设置 AnimGraph（BlendSpacePlayer + 变量获取 + Root）
// 查找已有节点复用，仅创建缺失节点；已连线则跳过，不破坏手动修改。
// ============================================================================

FString USKAnimBlueprintTool::HandleSetupAnimGraph(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString BlendSpacePath;
    if (!Args->TryGetStringField(TEXT("blend_space_path"), BlendSpacePath))
    {
        OutError = TEXT("缺少 blend_space_path 参数");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UBlendSpace* BlendSpace = LoadAssetHelper<UBlendSpace>(BlendSpacePath);
    if (!BlendSpace)
    {
        OutError = FString::Printf(TEXT("BlendSpace未找到: %s"), *BlendSpacePath);
        return FString();
    }

    // 找到主 AnimGraph（UAnimationGraph）
    UAnimationGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        AnimGraph = Cast<UAnimationGraph>(Graph);
        if (AnimGraph) break;
    }
    if (!AnimGraph)
    {
        for (UEdGraph* Graph : AnimBP->UbergraphPages)
        {
            AnimGraph = Cast<UAnimationGraph>(Graph);
            if (AnimGraph) break;
        }
    }
    if (!AnimGraph)
    {
        OutError = TEXT("未找到 AnimGraph");
        return FString();
    }

    // —— 查找已有节点（增量：复用，不重复创建）——

    UAnimGraphNode_Root* RootNode = nullptr;
    UAnimGraphNode_BlendSpacePlayer* BspNode = nullptr;
    UK2Node_VariableGet* AngleGetter = nullptr;
    UK2Node_VariableGet* SpeedGetter = nullptr;

    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        if (!RootNode)  RootNode  = Cast<UAnimGraphNode_Root>(Node);
        if (!BspNode)   BspNode   = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
        if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
        {
            FName MemberName = VarGet->VariableReference.GetMemberName();
            if (MemberName == TEXT("Angle") && !AngleGetter) AngleGetter = VarGet;
            if (MemberName == TEXT("Speed") && !SpeedGetter) SpeedGetter = VarGet;
        }
    }

    bool bAllNew = (!RootNode && !BspNode && !AngleGetter && !SpeedGetter);

    // —— 仅创建缺失的节点 ——

    if (!RootNode)
    {
        FGraphNodeCreator<UAnimGraphNode_Root> RootCreator(*AnimGraph);
        RootNode = RootCreator.CreateNode();
        RootCreator.Finalize();
    }

    if (!BspNode)
    {
        FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*AnimGraph);
        BspNode = NodeCreator.CreateNode();
        NodeCreator.Finalize();
    }

    auto FindOrCreateVarGet = [&](const FName& VarName, UK2Node_VariableGet* Existing) -> UK2Node_VariableGet*
    {
        if (Existing) return Existing;
        UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(AnimGraph);
        Getter->CreateNewGuid();
        Getter->VariableReference.SetSelfMember(VarName);
        Getter->AllocateDefaultPins();
        AnimGraph->AddNode(Getter, false, false);
        Getter->PostPlacedNewNode();
        return Getter;
    };

    AngleGetter = FindOrCreateVarGet(FName(TEXT("Angle")), AngleGetter);
    SpeedGetter = FindOrCreateVarGet(FName(TEXT("Speed")), SpeedGetter);

    // —— 布局：仅在全部新建时设置位置，否则保留已有布局 ——
    if (bAllNew)
    {
        AngleGetter->NodePosX = -400;  AngleGetter->NodePosY = -200;
        SpeedGetter->NodePosX = -400;  SpeedGetter->NodePosY =  200;
        BspNode->NodePosX    =    0;  BspNode->NodePosY    =    0;
        RootNode->NodePosX   =  400;  RootNode->NodePosY   =    0;
    }

    // —— 设置 BlendSpace 引用 ——
    UScriptStruct* NodeStruct = FAnimNode_BlendSpacePlayer::StaticStruct();
    void* NodeAddr = &BspNode->Node;
    FObjectProperty* BsProp = CastField<FObjectProperty>(NodeStruct->FindPropertyByName(TEXT("BlendSpace")));
    if (BsProp) BsProp->SetObjectPropertyValue(BsProp->ContainerPtrToValuePtr<void>(NodeAddr), BlendSpace);

    // —— 连线（仅当未连时才连，不破坏手动修改）——
    auto ConnectIfNotLinked = [](UEdGraphPin* OutPin, UEdGraphPin* InPin)
    {
        if (!OutPin || !InPin) return;
        if (OutPin->LinkedTo.Contains(InPin)) return;
        OutPin->MakeLinkTo(InPin);
    };

    ConnectIfNotLinked(AngleGetter->GetValuePin(),          BspNode->FindPin(FName(TEXT("X")),     EGPD_Input));
    ConnectIfNotLinked(SpeedGetter->GetValuePin(),          BspNode->FindPin(FName(TEXT("Y")),     EGPD_Input));
    ConnectIfNotLinked(BspNode->FindPin(TEXT("Pose"), EGPD_Output), RootNode->FindPin(TEXT("Result"), EGPD_Input));

    // —— 保存 ——
    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("blend_space"), BlendSpacePath);
    ResultObj->SetStringField(TEXT("bsp_node_id"), BspNode->GetFName().ToString());
    ResultObj->SetStringField(TEXT("root_node_id"), RootNode->GetFName().ToString());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleCreateBlendSpace — 创建 BlendSpace 资产
// ============================================================================

FString USKAnimBlueprintTool::HandleCreateBlendSpace(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString SkeletonPath;
    if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        OutError = TEXT("缺少 skeleton_path 参数");
        return FString();
    }

    USkeleton* Skeleton = LoadAssetHelper<USkeleton>(SkeletonPath);
    if (!Skeleton)
    {
        OutError = FString::Printf(TEXT("骨架未找到: %s"), *SkeletonPath);
        return FString();
    }

    // 读取坐标轴定义
    const TArray<TSharedPtr<FJsonValue>>* AxesArr = nullptr;
    if (!Args->TryGetArrayField(TEXT("axes"), AxesArr) || AxesArr->Num() == 0)
    {
        OutError = TEXT("缺少 axes 参数（坐标轴定义数组）");
        return FString();
    }
    int32 NumAxes = AxesArr->Num();
    if (NumAxes < 1 || NumAxes > 2)
    {
        OutError = TEXT("坐标轴数量必须为 1（1D）或 2（2D）");
        return FString();
    }

    // 解析坐标轴
    struct FAxisDef { FString Name; float Min, Max; int32 Grid; };
    TArray<FAxisDef> Axes;
    for (int32 i = 0; i < NumAxes; ++i)
    {
        const TSharedPtr<FJsonObject>* AxisObj = nullptr;
        if (!(*AxesArr)[i]->TryGetObject(AxisObj)) continue;
        FAxisDef Axis;
        Axis.Name = (*AxisObj)->GetStringField(TEXT("name"));
        Axis.Min = (float)(*AxisObj)->GetNumberField(TEXT("min"));
        Axis.Max = (float)(*AxisObj)->GetNumberField(TEXT("max"));
        Axis.Grid = FMath::Max(2, (int32)(*AxisObj)->GetNumberField(TEXT("grid")));
        Axes.Add(Axis);
    }

    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        // 已存在：加载并返回（不重复创建和配置）
        UBlendSpace* Existing = LoadAssetHelper<UBlendSpace>(AssetPath);
        if (!Existing)
        {
            OutError = FString::Printf(TEXT("BlendSpace已存在但加载失败: %s"), *AssetPath);
            return FString();
        }
        TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
        ResultObj->SetStringField(TEXT("path"), AssetPath);
        ResultObj->SetNumberField(TEXT("num_samples"), Existing->GetNumberOfBlendSamples());
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetStringField(TEXT("status"), TEXT("already_exists"));
        FString Output;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
        FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
        return Output;
    }

    // 以下为新建逻辑
    int32 LastSlash;
    if (!AssetPath.FindLastChar('/', LastSlash))
    {
        OutError = TEXT("无效的资产路径");
        return FString();
    }
    FString PackagePath = AssetPath.Left(LastSlash);
    FString AssetName = AssetPath.RightChop(LastSlash + 1);

    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = TEXT("创建Package失败");
        return FString();
    }

    UBlendSpace* BlendSpace = nullptr;
    if (NumAxes == 1)
    {
        BlendSpace = NewObject<UBlendSpace1D>(Package, UBlendSpace1D::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    }
    else
    {
        BlendSpace = NewObject<UBlendSpace>(Package, UBlendSpace::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
    }
    if (!BlendSpace)
    {
        OutError = TEXT("创建BlendSpace对象失败");
        return FString();
    }

    BlendSpace->SetSkeleton(Skeleton);

    // 通过反射设置 BlendParameters（protected 成员）
    FProperty* ParamProp = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
    FStructProperty* StructParamProp = CastField<FStructProperty>(ParamProp);
    if (StructParamProp)
    {
        UScriptStruct* ParamStruct = StructParamProp->Struct;
        FStrProperty* DisplayNameProp = CastField<FStrProperty>(ParamStruct->FindPropertyByName(TEXT("DisplayName")));
        FFloatProperty* MinProp = CastField<FFloatProperty>(ParamStruct->FindPropertyByName(TEXT("Min")));
        FFloatProperty* MaxProp = CastField<FFloatProperty>(ParamStruct->FindPropertyByName(TEXT("Max")));
        FIntProperty* GridProp = CastField<FIntProperty>(ParamStruct->FindPropertyByName(TEXT("GridNum")));

        for (int32 i = 0; i < NumAxes; ++i)
        {
            void* ElemPtr = StructParamProp->ContainerPtrToValuePtr<void>(BlendSpace, i);
            if (DisplayNameProp) DisplayNameProp->SetPropertyValue_InContainer(ElemPtr, Axes[i].Name);
            if (MinProp) MinProp->SetFloatingPointPropertyValue(MinProp->ContainerPtrToValuePtr<void>(ElemPtr), Axes[i].Min);
            if (MaxProp) MaxProp->SetFloatingPointPropertyValue(MaxProp->ContainerPtrToValuePtr<void>(ElemPtr), Axes[i].Max);
            if (GridProp) GridProp->SetIntPropertyValue(GridProp->ContainerPtrToValuePtr<void>(ElemPtr), (int64)Axes[i].Grid);
        }
    }

    // 读取并添加样本
    const TArray<TSharedPtr<FJsonValue>>* SamplesArr = nullptr;
    if (Args->TryGetArrayField(TEXT("samples"), SamplesArr) && SamplesArr->Num() > 0)
    {
        for (const TSharedPtr<FJsonValue>& SampleVal : *SamplesArr)
        {
            const TSharedPtr<FJsonObject>* SampleObj = nullptr;
            if (!SampleVal->TryGetObject(SampleObj)) continue;

            FString AnimPath = (*SampleObj)->GetStringField(TEXT("anim_path"));
            UAnimSequence* AnimSeq = LoadAssetHelper<UAnimSequence>(AnimPath);
            if (!AnimSeq)
            {
                UE_LOG(LogTemp, Warning, TEXT("[CreateBlendSpace] AnimSequence未找到，跳过: %s"), *AnimPath);
                continue;
            }

            float X = (float)(*SampleObj)->GetNumberField(TEXT("x"));
            float Y = NumAxes > 1 ? (float)(*SampleObj)->GetNumberField(TEXT("y")) : 0.f;
            FVector SampleValue(X, Y, 0.f);
            BlendSpace->AddSample(AnimSeq, SampleValue);
        }
    }

    BlendSpace->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);
    FAssetRegistryModule::AssetCreated(BlendSpace);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), AssetName);
    ResultObj->SetStringField(TEXT("skeleton"), SkeletonPath);
    ResultObj->SetNumberField(TEXT("num_axes"), NumAxes);
    ResultObj->SetNumberField(TEXT("num_samples"), BlendSpace->GetNumberOfBlendSamples());
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleSetAnimClass — 将 AnimBlueprint 分配给角色 Blueprint 的 Mesh 组件
// ============================================================================

FString USKAnimBlueprintTool::HandleSetAnimClass(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString CharacterBPPath;
    if (!Args->TryGetStringField(TEXT("character_bp_path"), CharacterBPPath))
    {
        OutError = TEXT("缺少 character_bp_path 参数（角色Blueprint路径）");
        return FString();
    }

    FString AnimBPPath = Args->GetStringField(TEXT("path"));
    FString MeshName = TEXT("Mesh");
    Args->TryGetStringField(TEXT("mesh_component_name"), MeshName);

    // 加载 AnimBlueprint
    UAnimBlueprint* AnimBP = LoadAssetHelper<UAnimBlueprint>(AnimBPPath);
    if (!AnimBP)
    {
        OutError = FString::Printf(TEXT("AnimBlueprint未找到: %s"), *AnimBPPath);
        return FString();
    }

    if (!AnimBP->GeneratedClass)
    {
        OutError = TEXT("AnimBlueprint没有有效的 GeneratedClass");
        return FString();
    }

    // 加载角色 Blueprint
    UBlueprint* CharBP = LoadAssetHelper<UBlueprint>(CharacterBPPath);
    if (!CharBP)
    {
        OutError = FString::Printf(TEXT("角色Blueprint未找到: %s"), *CharacterBPPath);
        return FString();
    }

    if (!CharBP->GeneratedClass)
    {
        OutError = TEXT("角色Blueprint没有有效的 GeneratedClass");
        return FString();
    }

    // 获取 CDO
    UObject* CDO = CharBP->GeneratedClass->GetDefaultObject();
    if (!CDO)
    {
        OutError = TEXT("无法获取角色 CDO");
        return FString();
    }

    // 通过反射查找 SkeletalMeshComponent 属性
    FObjectProperty* MeshProp = CastField<FObjectProperty>(CharBP->GeneratedClass->FindPropertyByName(*MeshName));
    if (!MeshProp)
    {
        // 尝试查找 ACharacter 父类的 Mesh 属性
        MeshProp = CastField<FObjectProperty>(ACharacter::StaticClass()->FindPropertyByName(*MeshName));
    }
    if (!MeshProp)
    {
        OutError = FString::Printf(TEXT("未找到 Mesh 组件属性: %s"), *MeshName);
        return FString();
    }

    USkeletalMeshComponent* MeshComp = Cast<USkeletalMeshComponent>(MeshProp->GetObjectPropertyValue_InContainer(CDO));
    if (!MeshComp)
    {
        OutError = TEXT("Mesh 组件为空");
        return FString();
    }

    // 设置 AnimClass
    MeshComp->SetAnimInstanceClass(AnimBP->GeneratedClass);
    MeshComp->MarkPackageDirty();
    CharBP->MarkPackageDirty();

    // 保存
    UEditorAssetLibrary::SaveAsset(CharacterBPPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("character_bp"), CharacterBPPath);
    ResultObj->SetStringField(TEXT("anim_bp"), AnimBPPath);
    ResultObj->SetStringField(TEXT("anim_class"), AnimBP->GeneratedClass->GetPathName());
    ResultObj->SetStringField(TEXT("mesh_component"), MeshName);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// LoadAnimBlueprint — 加载 AnimBlueprint
// ============================================================================

UAnimBlueprint* USKAnimBlueprintTool::LoadAnimBlueprint(const FString& AssetPath, FString& OutError)
{
    UAnimBlueprint* AnimBP = LoadAssetHelper<UAnimBlueprint>(AssetPath);
    if (!AnimBP)
    {
        OutError = FString::Printf(TEXT("AnimBlueprint未找到: %s"), *AssetPath);
    }
    return AnimBP;
}

// ============================================================================
// FindOrCreateStateMachineNode — 查找或创建状态机节点
// ============================================================================

UAnimGraphNode_StateMachine* USKAnimBlueprintTool::FindOrCreateStateMachineNode(UAnimBlueprint* AnimBP, FString& OutError)
{
    // 必须使用 Cast<UAnimationGraph> 而非 schema 检查，因为 PostPlacedNewNode
    // 中 CastChecked<UAnimationGraph>(GetOuter()) 要求节点外部必须是 UAnimationGraph 类型
    UAnimationGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        AnimGraph = Cast<UAnimationGraph>(Graph);
        if (AnimGraph) break;
    }
    if (!AnimGraph)
    {
        for (UEdGraph* Graph : AnimBP->UbergraphPages)
        {
            AnimGraph = Cast<UAnimationGraph>(Graph);
            if (AnimGraph) break;
        }
    }
    // 直接 NewObject 创建，确保类型一定是 UAnimationGraph
    if (!AnimGraph)
    {
        AnimGraph = NewObject<UAnimationGraph>(AnimBP, NAME_None, RF_Transactional);
        if (AnimGraph)
        {
            AnimGraph->Schema = UAnimationGraphSchema::StaticClass();
            AnimGraph->Rename(TEXT("AnimGraph"), AnimBP, REN_DoNotDirty | REN_ForceNoResetLoaders);
            AnimBP->FunctionGraphs.Add(AnimGraph);
        }
    }
    if (!AnimGraph)
    {
        OutError = TEXT("无法找到或创建 AnimGraph");
        return nullptr;
    }

    // 查找现有的状态机节点
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
        {
            return SMNode;
        }
    }

    // 创建新的状态机节点（PostPlacedNewNode 自动创建 EditorStateMachineGraph）
    FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*AnimGraph);
    UAnimGraphNode_StateMachine* SMNode = NodeCreator.CreateNode();
    SMNode->NodePosX = 0;
    SMNode->NodePosY = 0;
    NodeCreator.Finalize(); // 自动创建 EditorStateMachineGraph

    // 确保 Entry 节点存在
    if (SMNode->EditorStateMachineGraph)
    {
        bool bHasEntry = false;
        for (UEdGraphNode* Node : SMNode->EditorStateMachineGraph->Nodes)
        {
            if (Cast<UAnimStateEntryNode>(Node)) { bHasEntry = true; break; }
        }
        if (!bHasEntry)
        {
            FGraphNodeCreator<UAnimStateEntryNode> EntryCreator(*SMNode->EditorStateMachineGraph);
            UAnimStateEntryNode* EntryNode = EntryCreator.CreateNode();
            EntryNode->NodePosX = -200;
            EntryNode->NodePosY = 0;
            EntryCreator.Finalize();
        }
    }

    return SMNode;
}

// ============================================================================
// FindStateNode — 在状态机图中按名称查找状态
// ============================================================================

UAnimStateNode* USKAnimBlueprintTool::FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName) const
{
    for (UEdGraphNode* Node : SMGraph->Nodes)
    {
        if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
        {
            if (StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString() == StateName
                || StateNode->GetFName() == FName(*StateName))
            {
                return StateNode;
            }
        }
    }
    return nullptr;
}

// ============================================================================
// CreateConditionOutput — 递归构建条件节点链，返回 bool 输出引脚
// ============================================================================

UEdGraphPin* USKAnimBlueprintTool::CreateConditionOutput(
    UAnimationTransitionGraph* TransGraph,
    const TSharedPtr<FJsonObject>& ConditionObj,
    int32& NodePosX,
    int32& NodePosY,
    FString& OutError)
{
    FString CondType;
    if (!ConditionObj->TryGetStringField(TEXT("type"), CondType))
    {
        OutError = TEXT("condition 缺少 type 字段");
        return nullptr;
    }

    if (CondType == TEXT("bool"))
    {
        FString VarName;
        if (!ConditionObj->TryGetStringField(TEXT("variable"), VarName))
        {
            OutError = TEXT("bool 条件缺少 variable 字段");
            return nullptr;
        }

        UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(TransGraph);
        VarGet->CreateNewGuid();
        VarGet->VariableReference.SetSelfMember(FName(*VarName));
        VarGet->AllocateDefaultPins();
        TransGraph->AddNode(VarGet, false, false);
        VarGet->PostPlacedNewNode();
        VarGet->NodePosX = NodePosX;
        VarGet->NodePosY = NodePosY;
        NodePosX += 200;

        return VarGet->FindPin(FName(*VarName), EGPD_Output);
    }

    if (CondType == TEXT("not_bool"))
    {
        FString VarName;
        if (!ConditionObj->TryGetStringField(TEXT("variable"), VarName))
        {
            OutError = TEXT("not_bool 条件缺少 variable 字段");
            return nullptr;
        }

        UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(TransGraph);
        VarGet->CreateNewGuid();
        VarGet->VariableReference.SetSelfMember(FName(*VarName));
        VarGet->AllocateDefaultPins();
        TransGraph->AddNode(VarGet, false, false);
        VarGet->PostPlacedNewNode();
        VarGet->NodePosX = NodePosX;
        VarGet->NodePosY = NodePosY;
        NodePosX += 200;

        UK2Node_CallFunction* NotNode = NewObject<UK2Node_CallFunction>(TransGraph);
        NotNode->CreateNewGuid();
        UFunction* NotFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Not_PreBool"));
        if (!NotFunc)
        {
            OutError = TEXT("未找到 Not_PreBool 函数");
            return nullptr;
        }
        NotNode->SetFromFunction(NotFunc);
        NotNode->AllocateDefaultPins();
        TransGraph->AddNode(NotNode, false, false);
        NotNode->PostPlacedNewNode();
        NotNode->NodePosX = NodePosX;
        NotNode->NodePosY = NodePosY;
        NodePosX += 200;

        UEdGraphPin* VarOut = VarGet->FindPin(FName(*VarName), EGPD_Output);
        UEdGraphPin* NotIn = NotNode->FindPin(TEXT("A"), EGPD_Input);
        if (VarOut && NotIn) VarOut->MakeLinkTo(NotIn);

        return NotNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
    }

    if (CondType == TEXT("float_compare"))
    {
        FString VarName, Operator;
        double Value = 0.0;
        if (!ConditionObj->TryGetStringField(TEXT("variable"), VarName)
            || !ConditionObj->TryGetStringField(TEXT("operator"), Operator))
        {
            OutError = TEXT("float_compare 需要 variable 和 operator 字段");
            return nullptr;
        }
        ConditionObj->TryGetNumberField(TEXT("value"), Value);

        UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(TransGraph);
        VarGet->CreateNewGuid();
        VarGet->VariableReference.SetSelfMember(FName(*VarName));
        VarGet->AllocateDefaultPins();
        TransGraph->AddNode(VarGet, false, false);
        VarGet->PostPlacedNewNode();
        VarGet->NodePosX = NodePosX;
        VarGet->NodePosY = NodePosY;
        NodePosX += 200;

        static const TMap<FString, FString> OpToFunc = {
            {TEXT(">"),  TEXT("Greater_DoubleDouble")},
            {TEXT(">="), TEXT("GreaterEqual_DoubleDouble")},
            {TEXT("<"),  TEXT("Less_DoubleDouble")},
            {TEXT("<="), TEXT("LessEqual_DoubleDouble")},
            {TEXT("=="), TEXT("EqualEqual_DoubleDouble")},
            {TEXT("!="), TEXT("NotEqual_DoubleDouble")},
        };
        const FString* FuncName = OpToFunc.Find(Operator);
        if (!FuncName)
        {
            OutError = FString::Printf(TEXT("不支持的运算符: %s"), *Operator);
            return nullptr;
        }

        UK2Node_CallFunction* CmpNode = NewObject<UK2Node_CallFunction>(TransGraph);
        CmpNode->CreateNewGuid();
        UFunction* CmpFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(**FuncName);
        if (!CmpFunc)
        {
            OutError = FString::Printf(TEXT("未找到比较函数: %s"), **FuncName);
            return nullptr;
        }
        CmpNode->SetFromFunction(CmpFunc);
        CmpNode->AllocateDefaultPins();
        TransGraph->AddNode(CmpNode, false, false);
        CmpNode->PostPlacedNewNode();
        CmpNode->NodePosX = NodePosX;
        CmpNode->NodePosY = NodePosY;
        NodePosX += 200;

        UEdGraphPin* VarOut = VarGet->FindPin(FName(*VarName), EGPD_Output);
        UEdGraphPin* CmpInA = CmpNode->FindPin(TEXT("A"), EGPD_Input);
        if (VarOut && CmpInA) VarOut->MakeLinkTo(CmpInA);

        UEdGraphPin* CmpInB = CmpNode->FindPin(TEXT("B"), EGPD_Input);
        if (CmpInB) CmpInB->DefaultValue = FString::SanitizeFloat(Value);

        return CmpNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
    }

    if (CondType == TEXT("and"))
    {
        const TArray<TSharedPtr<FJsonValue>>* SubConditions = nullptr;
        if (!ConditionObj->TryGetArrayField(TEXT("conditions"), SubConditions) || SubConditions->Num() < 2)
        {
            OutError = TEXT("and 条件需要 conditions 数组（至少2个子条件）");
            return nullptr;
        }

        UEdGraphPin* ChainOut = nullptr;
        for (int32 i = 0; i < SubConditions->Num(); ++i)
        {
            const TSharedPtr<FJsonObject>* SubObj = nullptr;
            if (!(*SubConditions)[i]->TryGetObject(SubObj)) continue;

            UEdGraphPin* SubOut = CreateConditionOutput(TransGraph, *SubObj, NodePosX, NodePosY, OutError);
            if (!SubOut) return nullptr;

            if (i == 0)
            {
                ChainOut = SubOut;
            }
            else
            {
                UK2Node_CallFunction* AndNode = NewObject<UK2Node_CallFunction>(TransGraph);
                AndNode->CreateNewGuid();
                UFunction* AndFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanAND"));
                if (!AndFunc)
                {
                    OutError = TEXT("未找到 BooleanAND 函数");
                    return nullptr;
                }
                AndNode->SetFromFunction(AndFunc);
                AndNode->AllocateDefaultPins();
                TransGraph->AddNode(AndNode, false, false);
                AndNode->PostPlacedNewNode();
                AndNode->NodePosX = NodePosX;
                AndNode->NodePosY = NodePosY;
                NodePosX += 200;

                UEdGraphPin* AndInA = AndNode->FindPin(TEXT("A"), EGPD_Input);
                UEdGraphPin* AndInB = AndNode->FindPin(TEXT("B"), EGPD_Input);
                if (ChainOut && AndInA) ChainOut->MakeLinkTo(AndInA);
                if (SubOut && AndInB) SubOut->MakeLinkTo(AndInB);

                ChainOut = AndNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
            }
        }
        return ChainOut;
    }

    OutError = FString::Printf(TEXT("不支持的条件类型: %s（支持: bool, not_bool, float_compare, and, time_remaining）"), *CondType);
    return nullptr;
}

// ============================================================================
// SetupTransitionCondition — 在转换 BoundGraph 中设置条件规则
// ============================================================================

bool USKAnimBlueprintTool::SetupTransitionCondition(UAnimStateTransitionNode* TransNode, const TSharedPtr<FJsonObject>& ConditionObj, FString& OutError)
{
    FString CondType;
    if (!ConditionObj->TryGetStringField(TEXT("type"), CondType))
    {
        OutError = TEXT("condition 缺少 type 字段");
        return false;
    }

    if (!TransNode->BoundGraph)
    {
        OutError = TEXT("转换 BoundGraph 为空");
        return false;
    }

    UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph);
    if (!TransGraph)
    {
        OutError = TEXT("BoundGraph 不是 UAnimationTransitionGraph 类型");
        return false;
    }

    UAnimGraphNode_TransitionResult* ResultNode = TransGraph->GetResultNode();
    if (!ResultNode)
    {
        OutError = TEXT("未找到 TransitionResult 节点");
        return false;
    }

    UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
    if (!CanEnterPin)
    {
        OutError = TEXT("未找到 bCanEnterTransition 引脚");
        return false;
    }

    // time_remaining：设自动规则 + 默认 true
    if (CondType == TEXT("time_remaining"))
    {
        TransNode->bAutomaticRuleBasedOnSequencePlayerInState = true;
        CanEnterPin->DefaultValue = TEXT("true");
        return true;
    }

    // 其他类型：构建节点链并连接到 TransitionResult
    int32 PosX = -400;
    int32 PosY = 0;
    UEdGraphPin* CondOut = CreateConditionOutput(TransGraph, ConditionObj, PosX, PosY, OutError);
    if (!CondOut)
    {
        return false;
    }

    CondOut->MakeLinkTo(CanEnterPin);
    return true;
}

// ============================================================================
// SetupBlendSpacePinConnections — 在状态内为 BlendSpace 连接变量引脚
// ============================================================================

void USKAnimBlueprintTool::SetupBlendSpacePinConnections(
    UAnimGraphNode_BlendSpacePlayer* BspNode,
    UEdGraph* StateGraph,
    const TSharedPtr<FJsonObject>& PinConns)
{
    int32 VarIdx = 0;

    auto CreateVarGetAndConnect = [&](const FString& VarName, const TCHAR* BspPinName)
    {
        if (VarName.IsEmpty()) return;

        UK2Node_VariableGet* VarGet = NewObject<UK2Node_VariableGet>(StateGraph);
        VarGet->CreateNewGuid();
        VarGet->VariableReference.SetSelfMember(FName(*VarName));
        VarGet->AllocateDefaultPins();
        StateGraph->AddNode(VarGet, false, false);
        VarGet->PostPlacedNewNode();
        VarGet->NodePosX = BspNode->NodePosX - 400;
        VarGet->NodePosY = BspNode->NodePosY + VarIdx * 250;

        UEdGraphPin* VarOutPin = VarGet->FindPin(FName(*VarName), EGPD_Output);
        UEdGraphPin* BspInPin = BspNode->FindPin(FName(BspPinName), EGPD_Input);
        if (VarOutPin && BspInPin)
        {
            VarOutPin->MakeLinkTo(BspInPin);
        }

        ++VarIdx;
    };

    FString XPinVar, YPinVar;
    PinConns->TryGetStringField(TEXT("X"), XPinVar);
    PinConns->TryGetStringField(TEXT("Y"), YPinVar);
    CreateVarGetAndConnect(XPinVar, TEXT("X"));
    CreateVarGetAndConnect(YPinVar, TEXT("Y"));
}

// ============================================================================
// HandleRenameNode — 重命名状态机或状态节点
// args: path (ABP路径), target (state_machine|state), old_name, new_name
// ============================================================================

FString USKAnimBlueprintTool::HandleRenameNode(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString Target;
    if (!Args->TryGetStringField(TEXT("target"), Target))
    {
        OutError = TEXT("缺少 target 参数 (state_machine 或 state)");
        return FString();
    }
    FString OldName;
    Args->TryGetStringField(TEXT("old_name"), OldName);
    FString NewName = Args->GetStringField(TEXT("new_name"));

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimBP, OutError);
    if (!SMNode || !SMNode->EditorStateMachineGraph) return FString();

    UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;

    if (Target == TEXT("state_machine"))
    {
        SMGraph->Rename(*NewName);
        AnimBP->Modify();
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
        Result->SetStringField(TEXT("target"), TEXT("state_machine"));
        Result->SetStringField(TEXT("new_name"), SMGraph->GetName());
        Result->SetBoolField(TEXT("success"), true);
        FString JsonResult;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonResult);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return JsonResult;
    }
    else if (Target == TEXT("state"))
    {
        if (NewName.IsEmpty())
        {
            OutError = TEXT("缺少 new_name 参数");
            return FString();
        }
        UAnimStateNode* StateNode = FindStateNode(SMGraph, OldName);
        if (!StateNode)
        {
            OutError = FString::Printf(TEXT("状态不存在: %s"), *OldName);
            return FString();
        }
        if (StateNode->BoundGraph)
        {
            StateNode->BoundGraph->Rename(*NewName);
            AnimBP->Modify();
            TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
            Result->SetStringField(TEXT("target"), TEXT("state"));
            Result->SetStringField(TEXT("old_name"), OldName);
            Result->SetStringField(TEXT("new_name"), StateNode->BoundGraph->GetName());
            Result->SetBoolField(TEXT("success"), true);
            FString JsonResult;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonResult);
            FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
            return JsonResult;
        }
    }

    OutError = FString::Printf(TEXT("不支持的 target: %s (支持 state_machine, state)"), *Target);
    return FString();
}

// ============================================================================
// HandleLayout — 自动排版状态机：Entry、状态网格、内部节点、过渡节点
// ============================================================================

FString USKAnimBlueprintTool::HandleLayout(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    const UEdGraphSchema* Schema = GetDefault<UAnimationStateMachineSchema>();

    // 收集所有状态机图
    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(AnimBP->FunctionGraphs);
    AllGraphs.Append(AnimBP->UbergraphPages);

    int32 TotalStates = 0;

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
            if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

            UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
            TArray<UAnimStateNode*> States;
            UAnimStateEntryNode* EntryNode = nullptr;
            TArray<UAnimStateTransitionNode*> Transitions;

            // —— 收集节点 ——
            for (UEdGraphNode* N : SMGraph->Nodes)
            {
                if (UAnimStateEntryNode* EN = Cast<UAnimStateEntryNode>(N))
                    EntryNode = EN;
                else if (UAnimStateNode* SN = Cast<UAnimStateNode>(N))
                    States.Add(SN);
                else if (UAnimStateTransitionNode* TN = Cast<UAnimStateTransitionNode>(N))
                    Transitions.Add(TN);
            }

            if (States.Num() == 0) continue;

            // —— 排序：Locomotion 始终第一 ——
            States.Sort([](const UAnimStateNode& A, const UAnimStateNode& B) {
                FString NA = A.GetNodeTitle(ENodeTitleType::ListView).ToString();
                FString NB = B.GetNodeTitle(ENodeTitleType::ListView).ToString();
                if (NA == TEXT("Locomotion")) return true;
                if (NB == TEXT("Locomotion")) return false;
                return NA < NB;
            });

            // —— 状态网格布局（4列，间距500x350）——
            static const int32 Cols = 4, ColSpacing = 500, RowSpacing = 350, StartX = 200, StartY = 0;
            for (int32 i = 0; i < States.Num(); ++i)
            {
                int32 NewX = StartX + (i % Cols) * ColSpacing;
                int32 NewY = StartY + (i / Cols) * RowSpacing;
                Schema->SetNodePosition(States[i], FVector2D(NewX, NewY));
            }

            // —— Entry 定位并连接第一个状态 ——
            if (EntryNode)
            {
                int32 CenterY = States[0]->NodePosY;
                Schema->SetNodePosition(EntryNode, FVector2D(-200, CenterY));

                UEdGraphPin* EntryOut = EntryNode->FindPin(TEXT("Entry"), EGPD_Output);
                UEdGraphPin* FirstIn = States[0]->FindPin(TEXT("In"), EGPD_Input);
                if (EntryOut && FirstIn)
                {
                    if (EntryOut->LinkedTo.Num() > 0)
                        EntryOut->BreakAllPinLinks();
                    EntryOut->MakeLinkTo(FirstIn);
                }
            }

            // —— 每个状态的内部节点排版 ——
            for (UAnimStateNode* State : States)
            {
                if (!State->BoundGraph) continue;

                UAnimGraphNode_StateResult* ResultNode = nullptr;
                TArray<UEdGraphNode*> InnerAnimNodes;   // SequencePlayer / BlendSpacePlayer
                TArray<UK2Node_VariableGet*> VarGets;

                for (UEdGraphNode* N : State->BoundGraph->Nodes)
                {
                    if (UAnimGraphNode_StateResult* SR = Cast<UAnimGraphNode_StateResult>(N))
                        ResultNode = SR;
                    else if (Cast<UAnimGraphNode_SequencePlayer>(N) || Cast<UAnimGraphNode_BlendSpacePlayer>(N))
                        InnerAnimNodes.Add(N);
                    else if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(N))
                        VarGets.Add(VG);
                }

                // ResultNode 放最右
                if (ResultNode)
                    Schema->SetNodePosition(ResultNode, FVector2D(400, 0));

                // AnimPlayer 放中心
                for (int32 j = 0; j < InnerAnimNodes.Num(); ++j)
                    Schema->SetNodePosition(InnerAnimNodes[j], FVector2D(0, j * 200));

                // VariableGet 放左侧
                for (int32 j = 0; j < VarGets.Num(); ++j)
                    Schema->SetNodePosition(VarGets[j], FVector2D(-400, -200 + j * 250));

                // —— 修复内部连线：AnimPlayer.Pose → ResultNode.Result ——
                if (ResultNode)
                {
                    UEdGraphPin* ResultIn = ResultNode->FindPin(TEXT("Result"), EGPD_Input);
                    for (UEdGraphNode* AnimNode : InnerAnimNodes)
                    {
                        UEdGraphPin* PoseOut = AnimNode->FindPin(TEXT("Pose"), EGPD_Output);
                        if (PoseOut && ResultIn)
                        {
                            bool bAlready = false;
                            for (UEdGraphPin* L : PoseOut->LinkedTo)
                                if (L == ResultIn) { bAlready = true; break; }
                            if (!bAlready)
                                PoseOut->MakeLinkTo(ResultIn);
                        }
                    }
                }

                // —— 修复 BlendSpace 参数连线：VariableGet → X/Y ——
                for (UEdGraphNode* AnimNode : InnerAnimNodes)
                {
                    UAnimGraphNode_BlendSpacePlayer* Bsp = Cast<UAnimGraphNode_BlendSpacePlayer>(AnimNode);
                    if (!Bsp) continue;
                    for (UK2Node_VariableGet* VG : VarGets)
                    {
                        FName VarName = VG->VariableReference.GetMemberName();
                        const TCHAR* PinName = nullptr;
                        if (VarName == TEXT("Angle")) PinName = TEXT("X");
                        else if (VarName == TEXT("Speed")) PinName = TEXT("Y");
                        if (!PinName) continue;

                        UEdGraphPin* VarOut = VG->GetValuePin();
                        UEdGraphPin* BspIn = Bsp->FindPin(FName(PinName), EGPD_Input);
                        if (VarOut && BspIn)
                        {
                            bool bAlready = false;
                            for (UEdGraphPin* L : VarOut->LinkedTo)
                                if (L == BspIn) { bAlready = true; break; }
                            if (!bAlready)
                                VarOut->MakeLinkTo(BspIn);
                        }
                    }
                }
            }

            // —— 过渡节点排版：放在源/目标状态中间 ——
            for (UAnimStateTransitionNode* Trans : Transitions)
            {
                if (Trans->BoundGraph)
                {
                    // 过渡图中的条件结果节点重置到 (0,0)
                    for (UEdGraphNode* N : Trans->BoundGraph->Nodes)
                    {
                        if (Cast<UAnimGraphNode_TransitionResult>(N))
                            Schema->SetNodePosition(N, FVector2D(0, 0));
                        else if (!Cast<UK2Node_CallFunction>(N) && !Cast<UK2Node_VariableGet>(N))
                            continue;
                        // 其他已由 CreateConditionOutput 放置，保持不变
                    }
                }
            }

            // —— 修复 AnimGraph 顶层连线：StateMachine → Root ——
            {
                UEdGraph* AnimGraph = Cast<UEdGraph>(SMNode->GetGraph());
                if (AnimGraph)
                {
                    UAnimGraphNode_Root* RootNode = nullptr;
                    for (UEdGraphNode* N : AnimGraph->Nodes)
                    {
                        RootNode = Cast<UAnimGraphNode_Root>(N);
                        if (RootNode) break;
                    }

                    if (RootNode)
                    {
                        UEdGraphPin* SMPoseOut = SMNode->FindPin(TEXT("Pose"), EGPD_Output);
                        UEdGraphPin* RootResultIn = RootNode->FindPin(TEXT("Result"), EGPD_Input);

                        if (SMPoseOut && RootResultIn)
                        {
                            // 断开 BlendSpacePlayer → Root 的旧连线（如果有）
                            if (RootResultIn->LinkedTo.Num() > 0)
                                RootResultIn->BreakAllPinLinks();

                            bool bAlready = false;
                            for (UEdGraphPin* L : SMPoseOut->LinkedTo)
                                if (L == RootResultIn) { bAlready = true; break; }
                            if (!bAlready)
                                SMPoseOut->MakeLinkTo(RootResultIn);
                        }

                        // Root 放到 StateMachine 右侧
                        Schema->SetNodePosition(RootNode, FVector2D(400, 0));
                    }
                }
            }

            TotalStates += States.Num();
        }
    }

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetNumberField(TEXT("states_arranged"), TotalStates);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleAddSlotNode — 在 AnimGraph 顶层添加 Slot 节点（在 StateMachine 和 Root 之间）
// ============================================================================

FString USKAnimBlueprintTool::HandleAddSlotNode(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString SlotName = TEXT("DefaultSlot");
    Args->TryGetStringField(TEXT("slot_name"), SlotName);
    bool bForce = false;
    Args->TryGetBoolField(TEXT("force"), bForce);

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    // 查找 AnimGraph 顶层（UAnimationGraph）
    UAnimationGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        AnimGraph = Cast<UAnimationGraph>(Graph);
        if (AnimGraph) break;
    }
    if (!AnimGraph)
    {
        for (UEdGraph* Graph : AnimBP->UbergraphPages)
        {
            AnimGraph = Cast<UAnimationGraph>(Graph);
            if (AnimGraph) break;
        }
    }
    if (!AnimGraph)
    {
        OutError = TEXT("未找到 AnimGraph");
        return FString();
    }

    // 查找已有 Slot 节点
    UAnimGraphNode_Slot* ExistingSlot = nullptr;
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        ExistingSlot = Cast<UAnimGraphNode_Slot>(Node);
        if (ExistingSlot) break;
    }
    if (ExistingSlot)
    {
        FString ExistingName = ExistingSlot->Node.SlotName.ToString();
        if (!bForce && ExistingName == SlotName)
        {
            // 已存在同名 Slot 节点且非强制模式，直接返回成功
            TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
            ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
            ResultObj->SetStringField(TEXT("slot_name"), SlotName);
            ResultObj->SetStringField(TEXT("node_id"), ExistingSlot->GetFName().ToString());
            ResultObj->SetStringField(TEXT("status"), TEXT("already_exists"));
            ResultObj->SetBoolField(TEXT("success"), true);
            FString Output;
            TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
                TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
            FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
            return Output;
        }
        // 强制模式：删除旧节点
        ExistingSlot->DestroyNode();
    }

    // 查找 StateMachine 和 Root 节点
    UAnimGraphNode_StateMachine* SMNode = nullptr;
    UAnimGraphNode_Root* RootNode = nullptr;
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        if (!SMNode)    SMNode    = Cast<UAnimGraphNode_StateMachine>(Node);
        if (!RootNode)  RootNode  = Cast<UAnimGraphNode_Root>(Node);
        if (SMNode && RootNode) break;
    }
    if (!SMNode)
    {
        OutError = TEXT("未找到 StateMachine 节点（请先添加状态机）");
        return FString();
    }
    if (!RootNode)
    {
        OutError = TEXT("未找到 Root 节点");
        return FString();
    }

    // 断开 StateMachine → Root 的旧连线
    UEdGraphPin* SMPoseOut = SMNode->FindPin(TEXT("Pose"), EGPD_Output);
    UEdGraphPin* RootResultIn = RootNode->FindPin(TEXT("Result"), EGPD_Input);
    if (SMPoseOut && RootResultIn)
    {
        RootResultIn->BreakAllPinLinks();
    }
    else
    {
        OutError = TEXT("StateMachine 缺少 Pose 输出引脚 或 Root 缺少 Result 输入引脚");
        return FString();
    }

    // 创建 Slot 节点
    FGraphNodeCreator<UAnimGraphNode_Slot> NodeCreator(*AnimGraph);
    UAnimGraphNode_Slot* SlotGraphNode = NodeCreator.CreateNode();
    NodeCreator.Finalize();

    // 设置 SlotName
    SlotGraphNode->Node.SlotName = FName(*SlotName);

    // 位置放在 StateMachine 和 Root 之间
    SlotGraphNode->NodePosX = (SMNode->NodePosX + RootNode->NodePosX) / 2;
    SlotGraphNode->NodePosY = SMNode->NodePosY;

    // 查找 Slot 的输入/输出引脚（遍历所有引脚，按方向分类）
    UEdGraphPin* SlotInputPin = nullptr;
    UEdGraphPin* SlotOutputPin = nullptr;
    for (UEdGraphPin* Pin : SlotGraphNode->Pins)
    {
        if (Pin->Direction == EGPD_Input && !SlotInputPin)
            SlotInputPin = Pin;
        if (Pin->Direction == EGPD_Output && !SlotOutputPin)
            SlotOutputPin = Pin;
    }

    // 连线：StateMachine.Pose → Slot.Input → Slot.Output → Root.Result
    if (SlotInputPin && SMPoseOut)
    {
        SMPoseOut->MakeLinkTo(SlotInputPin);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[AddSlot] SlotInputPin=%s SMPoseOut=%s"), SlotInputPin ? TEXT("ok") : TEXT("null"), SMPoseOut ? TEXT("ok") : TEXT("null"));
    }
    if (SlotOutputPin && RootResultIn)
    {
        SlotOutputPin->MakeLinkTo(RootResultIn);
    }

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("slot_name"), SlotName);
    ResultObj->SetStringField(TEXT("node_id"), SlotGraphNode->GetFName().ToString());
    ResultObj->SetStringField(TEXT("node_pos_x"), FString::FromInt(SlotGraphNode->NodePosX));
    ResultObj->SetStringField(TEXT("node_pos_y"), FString::FromInt(SlotGraphNode->NodePosY));
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleUpsertSkeletonSlot — 在 Skeleton 上登记或迁移 Slot 与 Slot Group
// ============================================================================

FString USKAnimBlueprintTool::HandleUpsertSkeletonSlot(
    const TSharedPtr<FJsonObject>& Args,
    FString& OutError)
{
    const FString SkeletonPath = Args->GetStringField(TEXT("path"));
    FString SlotName;
    if (!Args->TryGetStringField(TEXT("slot_name"), SlotName) || SlotName.IsEmpty())
    {
        OutError = TEXT("缺少 slot_name 参数");
        return FString();
    }

    FString SlotGroupName = TEXT("DefaultGroup");
    Args->TryGetStringField(TEXT("slot_group_name"), SlotGroupName);
    if (SlotGroupName.IsEmpty())
    {
        OutError = TEXT("slot_group_name 不能为空");
        return FString();
    }

    USkeleton* Skeleton = LoadAssetHelper<USkeleton>(SkeletonPath);
    if (!Skeleton)
    {
        OutError = FString::Printf(TEXT("骨架未找到: %s"), *SkeletonPath);
        return FString();
    }

    const FName SlotFName(*SlotName);
    const FName GroupFName(*SlotGroupName);
    const bool bAlreadyConfigured = Skeleton->ContainsSlotName(SlotFName)
        && Skeleton->GetSlotGroupName(SlotFName) == GroupFName;

    if (!bAlreadyConfigured)
    {
        Skeleton->Modify();
        Skeleton->AddSlotGroupName(GroupFName);
        Skeleton->SetSlotGroupName(SlotFName, GroupFName);
        Skeleton->MarkPackageDirty();
        if (!UEditorAssetLibrary::SaveAsset(SkeletonPath, false))
        {
            OutError = FString::Printf(TEXT("保存骨架失败: %s"), *SkeletonPath);
            return FString();
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("skeleton"), SkeletonPath);
    ResultObj->SetStringField(TEXT("slot_name"), SlotName);
    ResultObj->SetStringField(TEXT("slot_group_name"), SlotGroupName);
    ResultObj->SetStringField(TEXT("status"), bAlreadyConfigured ? TEXT("already_exists") : TEXT("updated"));
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleRemoveStateMachine — 删除指定的未连接状态机占位节点
// ============================================================================

FString USKAnimBlueprintTool::HandleRemoveStateMachine(
    const TSharedPtr<FJsonObject>& Args,
    FString& OutError)
{
    const FString AssetPath = Args->GetStringField(TEXT("path"));
    FString StateMachineName;
    if (!Args->TryGetStringField(TEXT("state_machine_name"), StateMachineName)
        || StateMachineName.IsEmpty())
    {
        OutError = TEXT("缺少 state_machine_name 参数");
        return FString();
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprint(AssetPath, OutError);
    if (!AnimBP) return FString();

    UAnimGraphNode_StateMachine* TargetNode = nullptr;
    TArray<UEdGraph*> AllGraphs;
    AnimBP->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachine* StateMachineNode = Cast<UAnimGraphNode_StateMachine>(Node);
            if (StateMachineNode && StateMachineNode->GetStateMachineName() == StateMachineName)
            {
                TargetNode = StateMachineNode;
                break;
            }
        }
        if (TargetNode) break;
    }

    if (!TargetNode)
    {
        OutError = FString::Printf(TEXT("未找到状态机: %s"), *StateMachineName);
        return FString();
    }

    bool bForce = false;
    Args->TryGetBoolField(TEXT("force"), bForce);
    for (const UEdGraphPin* Pin : TargetNode->Pins)
    {
        if (!bForce && Pin && Pin->LinkedTo.Num() > 0)
        {
            OutError = FString::Printf(
                TEXT("拒绝删除仍有连接的状态机: %s"),
                *StateMachineName);
            return FString();
        }
    }

    AnimBP->Modify();
    FBlueprintEditorUtils::RemoveNode(AnimBP, TargetNode, true);
    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    if (!UEditorAssetLibrary::SaveAsset(AssetPath, false))
    {
        OutError = FString::Printf(TEXT("保存动画蓝图失败: %s"), *AssetPath);
        return FString();
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("removed_state_machine"), StateMachineName);
    ResultObj->SetBoolField(TEXT("forced"), bForce);
    ResultObj->SetBoolField(TEXT("success"), true);
    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAnimBlueprintTool::HandleAddCurve(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));
    FString CurveName = Args->GetStringField(TEXT("curve_name"));

    UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (!AnimSeq)
    {
        OutError = FString::Printf(TEXT("动画未找到: %s"), *AssetPath);
        return FString();
    }

    IAnimationDataController& Controller = AnimSeq->GetController();
    FName CurveFName = FName(*CurveName);

    USkeleton* Skeleton = AnimSeq->GetSkeleton();
    if (!Skeleton)
    {
        OutError = TEXT("动画没有关联骨骼");
        return FString();
    }

    // 注册 SmartName 到骨骼
    FSmartName SmartName;
    Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, CurveFName, SmartName);

    // 选择曲线类型：int → RCIM_Constant 阶跃保持，默认 → RCIM_Linear
    FString CurveType;
    Args->TryGetStringField(TEXT("curve_type"), CurveType);
    const bool bIsIntegerCurve = CurveType.Equals(TEXT("int"), ESearchCase::IgnoreCase);

    FAnimationCurveIdentifier CurveId(SmartName, ERawCurveTrackTypes::RCT_Float);
    if (!CurveId.IsValid())
    {
        OutError = TEXT("无法获取曲线标识");
        return FString();
    }

    // 添加曲线
    Controller.OpenBracket(NSLOCTEXT("SekiroAIBridge", "AddCurve", "添加曲线"));
    {
        if (!Controller.AddCurve(CurveId))
        {
            bool bOverwrite = false;
            Args->TryGetBoolField(TEXT("overwrite"), bOverwrite);
            if (bOverwrite)
            {
                Controller.SetCurveKeys(CurveId, TArray<FRichCurveKey>());
            }
        }

        // 设置关键帧（整数曲线用 RCIM_Constant 阶跃保持）
        const ERichCurveInterpMode InterpMode = bIsIntegerCurve ? RCIM_Constant : RCIM_Linear;
        const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
        if (Args->TryGetArrayField(TEXT("keys"), KeysArray))
        {
            TArray<FRichCurveKey> Keys;
            for (const auto& KeyVal : *KeysArray)
            {
                const TSharedPtr<FJsonObject>* KeyObj = nullptr;
                if (!KeyVal->TryGetObject(KeyObj)) continue;
                FRichCurveKey Key;
                Key.Time = (*KeyObj)->GetNumberField(TEXT("time"));
                Key.Value = (*KeyObj)->GetNumberField(TEXT("value"));
                Key.InterpMode = InterpMode;
                Keys.Add(Key);
            }
            if (Keys.Num() > 0)
            {
                Controller.SetCurveKeys(CurveId, Keys);
            }
        }
    }
    Controller.CloseBracket();

    AnimSeq->PostEditChange();
    AnimSeq->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("animation"), AssetPath);
    ResultObj->SetStringField(TEXT("curve_name"), CurveName);
    ResultObj->SetStringField(TEXT("curve_type"), CurveType);
    ResultObj->SetBoolField(TEXT("success"), true);
    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAnimBlueprintTool::HandleSetAnimCurves(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    const TArray<TSharedPtr<FJsonValue>>* AnimationValues = nullptr;
    if (!Args->TryGetArrayField(TEXT("animations"), AnimationValues))
    {
        OutError = TEXT("缺少 animations 参数");
        return FString();
    }

    bool bDryRun = false;
    Args->TryGetBoolField(TEXT("dry_run"), bDryRun);

    bool bSave = true;
    Args->TryGetBoolField(TEXT("save"), bSave);

    int32 AnimationCount = 0;
    int32 MissingAnimations = 0;
    int32 CurvesRequested = 0;
    int32 CurvesWritten = 0;
    int32 CurvesSkipped = 0;
    int32 ModifiedAssets = 0;
    int32 SavedAssets = 0;

    for (const TSharedPtr<FJsonValue>& AnimationValue : *AnimationValues)
    {
        const TSharedPtr<FJsonObject>* AnimationObject = nullptr;
        if (!AnimationValue.IsValid() || !AnimationValue->TryGetObject(AnimationObject))
        {
            continue;
        }

        FString AssetPath;
        if (!(*AnimationObject)->TryGetStringField(TEXT("path"), AssetPath))
        {
            ++MissingAnimations;
            continue;
        }

        UAnimSequence* AnimSequence = LoadAssetHelper<UAnimSequence>(AssetPath);
        if (!AnimSequence)
        {
            ++MissingAnimations;
            UE_LOG(LogTemp, Warning, TEXT("[SetAnimCurves] 动画未找到: %s"), *AssetPath);
            continue;
        }

        USkeleton* Skeleton = AnimSequence->GetSkeleton();
        if (!Skeleton)
        {
            ++MissingAnimations;
            UE_LOG(LogTemp, Warning, TEXT("[SetAnimCurves] 动画没有关联骨骼: %s"), *AssetPath);
            continue;
        }

        const TArray<TSharedPtr<FJsonValue>>* CurveValues = nullptr;
        if (!(*AnimationObject)->TryGetArrayField(TEXT("curves"), CurveValues))
        {
            ++CurvesSkipped;
            continue;
        }

        ++AnimationCount;
        bool bModified = false;
        bool bBracketOpened = false;
        TUniquePtr<UE::Anim::Compression::FScopedCompressionGuard> CompressionGuard;
        IAnimationDataController& Controller = AnimSequence->GetController();

        for (const TSharedPtr<FJsonValue>& CurveValue : *CurveValues)
        {
            const TSharedPtr<FJsonObject>* CurveObject = nullptr;
            if (!CurveValue.IsValid() || !CurveValue->TryGetObject(CurveObject))
            {
                ++CurvesSkipped;
                continue;
            }

            FString CurveNameText;
            if (!(*CurveObject)->TryGetStringField(TEXT("name"), CurveNameText) || CurveNameText.IsEmpty())
            {
                ++CurvesSkipped;
                continue;
            }

            ++CurvesRequested;

            FString CurveType;
            (*CurveObject)->TryGetStringField(TEXT("curve_type"), CurveType);
            const bool bIntegerCurve = CurveType.Equals(TEXT("int"), ESearchCase::IgnoreCase);
            const ERichCurveInterpMode InterpMode = bIntegerCurve ? RCIM_Constant : RCIM_Linear;

            const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
            if (!(*CurveObject)->TryGetArrayField(TEXT("keys"), KeyValues))
            {
                ++CurvesSkipped;
                continue;
            }

            TArray<FRichCurveKey> Keys;
            if (!ReadFloatCurveKeysFromJson(*KeyValues, InterpMode, Keys))
            {
                ++CurvesSkipped;
                continue;
            }

            if (bDryRun)
            {
                ++CurvesWritten;
                bModified = true;
                continue;
            }

            if (!CompressionGuard.IsValid())
            {
                CompressionGuard = MakeUnique<UE::Anim::Compression::FScopedCompressionGuard>(AnimSequence);
            }

            FSmartName SmartName;
            Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*CurveNameText), SmartName);
            FAnimationCurveIdentifier CurveId(SmartName, ERawCurveTrackTypes::RCT_Float);
            if (!CurveId.IsValid())
            {
                ++CurvesSkipped;
                continue;
            }

            if (!bBracketOpened)
            {
                Controller.OpenBracket(NSLOCTEXT("SekiroAIBridge", "SetAnimCurves", "批量写入动画曲线"), false);
                bBracketOpened = true;
            }

            if (!Controller.AddCurve(CurveId))
            {
                Controller.SetCurveKeys(CurveId, TArray<FRichCurveKey>());
            }
            Controller.SetCurveKeys(CurveId, Keys);
            ++CurvesWritten;
            bModified = true;
        }

        if (bBracketOpened)
        {
            Controller.CloseBracket(false);
        }

        if (!bModified) continue;

        ++ModifiedAssets;
        if (bDryRun) continue;

        AnimSequence->MarkPackageDirty();

        if (bSave)
        {
            TArray<UPackage*> PackagesToSave;
            PackagesToSave.Add(AnimSequence->GetPackage());
            if (UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
            {
                ++SavedAssets;
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("dry_run"), bDryRun);
    ResultObj->SetNumberField(TEXT("animations"), AnimationCount);
    ResultObj->SetNumberField(TEXT("missing_animations"), MissingAnimations);
    ResultObj->SetNumberField(TEXT("curves_requested"), CurvesRequested);
    ResultObj->SetNumberField(TEXT("curves_written"), CurvesWritten);
    ResultObj->SetNumberField(TEXT("curves_skipped"), CurvesSkipped);
    ResultObj->SetNumberField(TEXT("modified_assets"), ModifiedAssets);
    ResultObj->SetNumberField(TEXT("saved_assets"), SavedAssets);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

FString USKAnimBlueprintTool::HandleRemoveAnimCurves(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString SearchPath;
    if (!Args->TryGetStringField(TEXT("path"), SearchPath))
    {
        SearchPath = TEXT("/Game");
    }

    TArray<FName> CurveNames;
    const TArray<TSharedPtr<FJsonValue>>* CurveNameValues = nullptr;
    if (Args->TryGetArrayField(TEXT("curve_names"), CurveNameValues))
    {
        for (const TSharedPtr<FJsonValue>& CurveNameValue : *CurveNameValues)
        {
            const FString CurveNameText = CurveNameValue.IsValid() ? CurveNameValue->AsString() : FString();
            if (!CurveNameText.IsEmpty())
            {
                CurveNames.Add(FName(*CurveNameText));
            }
        }
    }

    if (CurveNames.Num() <= 0)
    {
        CurveNames.Add(FName(TEXT("FrameFlags")));
        CurveNames.Add(FName(TEXT("CancelActions")));
        CurveNames.Add(FName(TEXT("AttackHitbox")));
    }

    bool bCandidateScan = true;
    Args->TryGetBoolField(TEXT("candidate_scan"), bCandidateScan);

    bool bDryRun = false;
    Args->TryGetBoolField(TEXT("dry_run"), bDryRun);

    bool bSave = true;
    Args->TryGetBoolField(TEXT("save"), bSave);

    int32 BatchSize = 0;
    int32 BatchIndex = 0;
    if (Args->HasTypedField<EJson::Number>(TEXT("batch_size")))
    {
        BatchSize = static_cast<int32>(Args->GetNumberField(TEXT("batch_size")));
    }
    if (Args->HasTypedField<EJson::Number>(TEXT("batch_index")))
    {
        BatchIndex = static_cast<int32>(Args->GetNumberField(TEXT("batch_index")));
    }

    TSet<FName> CandidatePackages;
    if (bCandidateScan)
    {
        FindCurveCandidatePackages(CurveNames, CandidatePackages);
    }

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*SearchPath));
    Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> AssetDataList;
    AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
    AssetDataList.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.PackageName.LexicalLess(Right.PackageName);
    });

    TArray<FAssetData> FilteredAssetDataList;
    for (const FAssetData& AssetData : AssetDataList)
    {
        if (bCandidateScan && !CandidatePackages.Contains(AssetData.PackageName))
        {
            continue;
        }

        FilteredAssetDataList.Add(AssetData);
    }

    const int32 TotalCandidates = FilteredAssetDataList.Num();
    int32 StartIndex = 0;
    int32 EndIndex = TotalCandidates;
    if (BatchSize > 0)
    {
        StartIndex = FMath::Clamp(BatchIndex, 0, FMath::Max(0, TotalCandidates)) * BatchSize;
        EndIndex = FMath::Min(StartIndex + BatchSize, TotalCandidates);
    }

    int32 ScannedAssets = 0;
    int32 ModifiedAssets = 0;
    int32 RemovedCurves = 0;
    int32 SavedAssets = 0;

    for (int32 AssetIndex = StartIndex; AssetIndex < EndIndex; ++AssetIndex)
    {
        UAnimSequence* AnimSequence = Cast<UAnimSequence>(FilteredAssetDataList[AssetIndex].GetAsset());
        if (!AnimSequence) continue;

        ++ScannedAssets;
        if (ScannedAssets == 1 || ScannedAssets % 25 == 0)
        {
            UE_LOG(LogTemp, Display, TEXT("[RemoveAnimCurves] Progress %d/%d: %s"),
                ScannedAssets,
                EndIndex - StartIndex,
                *AnimSequence->GetPathName());
        }

        TArray<FAnimationCurveIdentifier> CurveIdsToRemove;
        for (const FName& CurveName : CurveNames)
        {
            FAnimationCurveIdentifier CurveId;
            if (ResolveFloatCurveIdentifier(AnimSequence, CurveName, CurveId))
            {
                CurveIdsToRemove.Add(CurveId);
            }
        }

        if (CurveIdsToRemove.Num() <= 0) continue;

        RemovedCurves += CurveIdsToRemove.Num();
        if (bDryRun)
        {
            ++ModifiedAssets;
            continue;
        }

        TUniquePtr<UE::Anim::Compression::FScopedCompressionGuard> CompressionGuard =
            MakeUnique<UE::Anim::Compression::FScopedCompressionGuard>(AnimSequence);
        IAnimationDataController& Controller = AnimSequence->GetController();

        bool bModified = false;
        Controller.OpenBracket(NSLOCTEXT("SekiroAIBridge", "RemoveAnimCurves", "删除动画旧曲线"), false);
        for (const FAnimationCurveIdentifier& CurveId : CurveIdsToRemove)
        {
            if (Controller.RemoveCurve(CurveId, false))
            {
                bModified = true;
            }
        }
        Controller.CloseBracket(false);

        if (!bModified) continue;

        ++ModifiedAssets;
        AnimSequence->MarkPackageDirty();

        if (bSave)
        {
            TArray<UPackage*> PackagesToSave;
            PackagesToSave.Add(AnimSequence->GetPackage());
            if (UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
            {
                ++SavedAssets;
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("path"), SearchPath);
    ResultObj->SetBoolField(TEXT("candidate_scan"), bCandidateScan);
    ResultObj->SetBoolField(TEXT("dry_run"), bDryRun);
    ResultObj->SetNumberField(TEXT("total_anim_sequences"), AssetDataList.Num());
    ResultObj->SetNumberField(TEXT("candidate_anim_sequences"), TotalCandidates);
    ResultObj->SetNumberField(TEXT("scanned_assets"), ScannedAssets);
    ResultObj->SetNumberField(TEXT("modified_assets"), ModifiedAssets);
    ResultObj->SetNumberField(TEXT("removed_curves"), RemovedCurves);
    ResultObj->SetNumberField(TEXT("saved_assets"), SavedAssets);
    ResultObj->SetNumberField(TEXT("batch_start"), StartIndex);
    ResultObj->SetNumberField(TEXT("batch_end"), EndIndex);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// AnimBlueprintToJson — 序列化 AnimBlueprint 结构
// ============================================================================


// ============================================================================
// HandleBatchTaeCurves 鈥?鎵归噺澶勭悊 TAE JSON锛屽啓鍏?FrameFlags/CancelActions/AttackHitbox
// ============================================================================
FString USKAnimBlueprintTool::HandleBatchTaeCurves(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString TAEJsonPath = Args->GetStringField(TEXT("tae_json_path"));
    FString BasePath;
    if (!Args->TryGetStringField(TEXT("base_path"), BasePath))
        BasePath = TEXT("/Game/Characters/Sekiro/Animations");
    FString AssetPrefix;
    if (!Args->TryGetStringField(TEXT("asset_prefix"), AssetPrefix))
        AssetPrefix = TEXT("Anim_Sekiro");

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *TAEJsonPath))
    {
        OutError = FString::Printf(TEXT("Cannot read TAE JSON: %s"), *TAEJsonPath);
        return FString();
    }

    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        OutError = TEXT("JSON parse failed");
        return FString();
    }

    static const TMap<int32, int32> JTFrameFlags = {
        {7, 1<<0}, {89, 1<<1}, {19, 1<<2}, {119, 1<<3}, {137, 1<<4},
        {133, 1<<5}, {134, 1<<6}, {51, 1<<7}, {27, 1<<8}, {8, 1<<9},
        {12, 1<<10}, {90, 1<<11}, {91, 1<<12}, {32, 1<<13}, {31, 1<<14}, {55, 1<<15}
    };
    static const TMap<int32, int32> JTCancelActions = {
        {115, 1}, {26, 1}, {117, 2}, {25, 3}, {118, 4}, {154, 5}
    };
    static const TMap<int32, int32> AttackTypeMap = {
        {0, 1}, {2, 4}, {62, 5}, {64, 0}
    };

    auto DerivePrefix = [](const FString& Filename) -> FString {
        FString P = Filename.Replace(TEXT(".tae"), TEXT(""));
        FString Num = P.RightChop(1);
        while (Num.Len() < 3) Num = TEXT("0") + Num;
        return TEXT("a") + Num;
    };

    auto BuildKeys = [](const TArray<TSharedPtr<FJsonValue>>& Events,
                        int32 TotalFrames,
                        const TMap<int32, int32>& Mapping,
                        bool bIsBitFlag) -> TArray<FRichCurveKey>
    {
        TArray<int32> FV;
        FV.SetNumZeroed(TotalFrames + 1);
        for (const auto& EV : Events)
        {
            const TSharedPtr<FJsonObject>* EO = nullptr;
            if (!EV->TryGetObject(EO)) continue;
            const int32 Type = (*EO)->GetIntegerField(TEXT("Type"));
            const TSharedPtr<FJsonObject>* PO = nullptr;
            if (!(*EO)->TryGetObjectField(TEXT("Parameters"), PO)) continue;
            int32 MK = -1;
            if (Type == 0) MK = (*PO)->GetIntegerField(TEXT("JumpTableID"));
            else if (Type == 1 && !bIsBitFlag) MK = (*PO)->GetIntegerField(TEXT("AttackType"));
            else continue;
            const int32* VP = Mapping.Find(MK);
            if (!VP || *VP == 0) continue;
            const int32 S = (*EO)->GetIntegerField(TEXT("StartFrame"));
            const int32 E = FMath::Min((*EO)->GetIntegerField(TEXT("EndFrame")), TotalFrames);
            for (int32 f = S; f <= E; ++f)
                bIsBitFlag ? FV[f] |= *VP : FV[f] = *VP;
        }
        TArray<FRichCurveKey> Keys;
        int32 PV = 0;
        for (int32 f = 0; f <= TotalFrames; ++f)
        {
            if (FV[f] != PV)
            {
                FRichCurveKey K;
                K.Time = f / 30.0f; K.Value = (float)FV[f]; K.InterpMode = RCIM_Constant;
                Keys.Add(K); PV = FV[f];
            }
        }
        if (Keys.Num() == 0) { FRichCurveKey K; K.Time = 0; K.Value = 0; K.InterpMode = RCIM_Constant; Keys.Add(K); }
        return Keys;
    };

    int32 TA = 0, OK = 0, SK = 0, CA = 0, ER = 0;
    const TArray<TSharedPtr<FJsonValue>>* TFF = nullptr;
    RootObj->TryGetArrayField(TEXT("TAE_Files"), TFF);
    if (!TFF) { OutError = TEXT("TAE_Files not found"); return FString(); }

    for (const auto& TFV : *TFF)
    {
        const TSharedPtr<FJsonObject>* TFO = nullptr;
        if (!TFV->TryGetObject(TFO)) continue;
        const FString FN = (*TFO)->GetStringField(TEXT("FileName"));
        const FString AP = DerivePrefix(FN);
        const TArray<TSharedPtr<FJsonValue>>* AN = nullptr;
        if (!(*TFO)->TryGetArrayField(TEXT("Animations"), AN)) continue;

        for (const auto& AV : *AN)
        {
            const TSharedPtr<FJsonObject>* AO = nullptr;
            if (!AV->TryGetObject(AO)) continue;
            const int32 AID = (*AO)->GetIntegerField(TEXT("AnimID")); TA++;
            const TArray<TSharedPtr<FJsonValue>>* EV = nullptr;
            if (!(*AO)->TryGetArrayField(TEXT("Events"), EV) || EV->Num() == 0) { SK++; continue; }

            const FString ANm = FString::Printf(TEXT("%s_%s_%06d"), *AssetPrefix, *AP, AID);
            const FString PP = FString::Printf(TEXT("%s/%s"), *BasePath, *ANm);
            const FString FP = FString::Printf(TEXT("%s.%s"), *PP, *ANm);

            UAnimSequence* ASq = LoadObject<UAnimSequence>(nullptr, *FP);
            if (!ASq) { SK++; continue; }

            int32 TFr = 1;
            for (const auto& EVi : *EV)
            {
                const TSharedPtr<FJsonObject>* EO = nullptr;
                if (!EVi->TryGetObject(EO)) continue;
                TFr = FMath::Max3(TFr, (*EO)->GetIntegerField(TEXT("EndFrame")), (*EO)->GetIntegerField(TEXT("StartFrame")));
            }

            USkeleton* Sk = ASq->GetSkeleton();
            if (!Sk) { ER++; continue; }
            IAnimationDataController& Ctrl = ASq->GetController();

            FSmartName SFF, SCA, SAH;
            Sk->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName("FrameFlags"), SFF);
            Sk->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName("CancelActions"), SCA);
            Sk->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName("AttackHitbox"), SAH);

            FAnimationCurveIdentifier IFF(SFF, ERawCurveTrackTypes::RCT_Float);
            FAnimationCurveIdentifier ICA(SCA, ERawCurveTrackTypes::RCT_Float);
            FAnimationCurveIdentifier IAH(SAH, ERawCurveTrackTypes::RCT_Float);

            TArray<FRichCurveKey> KFF = BuildKeys(*EV, TFr, JTFrameFlags, true);
            TArray<FRichCurveKey> KCA = BuildKeys(*EV, TFr, JTCancelActions, false);
            TArray<FRichCurveKey> KAH = BuildKeys(*EV, TFr, AttackTypeMap, false);

            Ctrl.OpenBracket(NSLOCTEXT("SekiroAIBridge", "BatchTAE", "Batch TAE Curves"));
            if (!Ctrl.AddCurve(IFF)) Ctrl.SetCurveKeys(IFF, {});
            Ctrl.SetCurveKeys(IFF, KFF); CA++;
            if (!Ctrl.AddCurve(ICA)) Ctrl.SetCurveKeys(ICA, {});
            Ctrl.SetCurveKeys(ICA, KCA); CA++;
            if (!Ctrl.AddCurve(IAH)) Ctrl.SetCurveKeys(IAH, {});
            Ctrl.SetCurveKeys(IAH, KAH); CA++;
            Ctrl.CloseBracket();

            ASq->PostEditChange();
            ASq->MarkPackageDirty();
            OK++;

            if (OK % 100 == 0)
                UE_LOG(LogTemp, Log, TEXT("[BatchTAE] %d/%d anims, %d curves"), OK, TA, CA);
        }
    }

    // Save modified packages individually (avoid autosave/GC race)
    UE_LOG(LogTemp, Log, TEXT("[BatchTAE] Saving %d modified packages..."), OK);
    TArray<UPackage*> PackagesToSave;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        if (It->IsDirty() && It->GetName().StartsWith(TEXT("/Game/Characters/Sekiro/Animations/")))
        {
            PackagesToSave.Add(*It);
        }
    }
    for (UPackage* Pkg : PackagesToSave)
    {
        FString PackageFilename;
        if (FPackageName::TryConvertLongPackageNameToFilename(Pkg->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
        {
            {
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Standalone;
            SaveArgs.SaveFlags = SAVE_NoError;
            UPackage::SavePackage(Pkg, nullptr, *PackageFilename, SaveArgs);
        }
        }
    }

    TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
    Res->SetNumberField(TEXT("total"), TA);
    Res->SetNumberField(TEXT("processed"), OK);
    Res->SetNumberField(TEXT("skipped"), SK);
    Res->SetNumberField(TEXT("curves"), CA);
    Res->SetNumberField(TEXT("errors"), ER);
    Res->SetBoolField(TEXT("success"), true);

    FString Out;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Res.ToSharedRef(), W);
    return Out;
}

FString USKAnimBlueprintTool::AnimBlueprintToJson(UAnimBlueprint* AnimBP) const
{
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());
    Root->SetStringField(TEXT("name"), AnimBP->GetName());
    Root->SetStringField(TEXT("path"), AnimBP->GetPathName());
    Root->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetName() : TEXT("None"));
    Root->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton ? AnimBP->TargetSkeleton->GetPathName() : TEXT("None"));
    Root->SetStringField(TEXT("status"), AnimBP->Status == BS_UpToDate ? TEXT("UpToDate") : (AnimBP->Status == BS_Error ? TEXT("Error") : TEXT("Dirty")));

    // 遍历状态机（搜索 FunctionGraphs 和 UbergraphPages）
    TArray<TSharedPtr<FJsonValue>> StateMachinesArr;
    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(AnimBP->FunctionGraphs);
    AllGraphs.Append(AnimBP->UbergraphPages);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
            if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

            TSharedPtr<FJsonObject> SMObj = MakeShareable(new FJsonObject());
            SMObj->SetStringField(TEXT("name"), SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString());

            TArray<TSharedPtr<FJsonValue>> StatesArr;
            TArray<TSharedPtr<FJsonValue>> TransitionsArr;

            for (UEdGraphNode* SMGraphNode : SMNode->EditorStateMachineGraph->Nodes)
            {
                if (UAnimStateNode* State = Cast<UAnimStateNode>(SMGraphNode))
                {
                    TSharedPtr<FJsonObject> StateObj = MakeShareable(new FJsonObject());
                    StateObj->SetStringField(TEXT("name"), State->GetNodeTitle(ENodeTitleType::ListView).ToString());

                    // 状态内的动画节点
                    if (State->BoundGraph)
                    {
                        TArray<TSharedPtr<FJsonValue>> AnimNodesArr;
                        for (UEdGraphNode* AnimGraphNode : State->BoundGraph->Nodes)
                        {
                            if (AnimGraphNode->IsA<UAnimGraphNode_StateResult>()) continue;
                            TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
                            NodeObj->SetStringField(TEXT("type"), AnimGraphNode->GetClass()->GetName());
                            NodeObj->SetStringField(TEXT("name"), AnimGraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                            AnimNodesArr.Add(MakeShareable(new FJsonValueObject(NodeObj)));
                        }
                        StateObj->SetArrayField(TEXT("nodes"), AnimNodesArr);
                    }
                    StatesArr.Add(MakeShareable(new FJsonValueObject(StateObj)));
                }
                else if (UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(SMGraphNode))
                {
                    TSharedPtr<FJsonObject> TransObj = MakeShareable(new FJsonObject());
                    UAnimStateNodeBase* Prev = Trans->GetPreviousState();
                    UAnimStateNodeBase* Next = Trans->GetNextState();
                    TransObj->SetStringField(TEXT("from"), Prev ? Prev->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("?"));
                    TransObj->SetStringField(TEXT("to"), Next ? Next->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("?"));
                    TransObj->SetNumberField(TEXT("crossfade_duration"), Trans->CrossfadeDuration);
                    TransObj->SetBoolField(TEXT("bidirectional"), Trans->Bidirectional);
                    TransitionsArr.Add(MakeShareable(new FJsonValueObject(TransObj)));
                }
            }
            SMObj->SetArrayField(TEXT("states"), StatesArr);
            SMObj->SetArrayField(TEXT("transitions"), TransitionsArr);
            StateMachinesArr.Add(MakeShareable(new FJsonValueObject(SMObj)));
        }
    }
    Root->SetArrayField(TEXT("state_machines"), StateMachinesArr);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return Output;
}
