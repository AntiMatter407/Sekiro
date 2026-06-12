#include "Tools/USKAnimBlueprintTool.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
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

FString USKAnimBlueprintTool::GetToolDescription() const
{
    return TEXT("动画蓝图操作（通用接口）：创建/编译AnimBP（支持自定义parent_class）、管理状态机（状态/转换）、添加动画节点（SequencePlayer/BlendSpacePlayer）、设置AnimGraph根节点、创建BlendSpace资产、查询结构。");
}

FString USKAnimBlueprintTool::GetInputSchemaJson() const
{
    return TEXT("{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"add_state\",\"add_transition\",\"add_node\",\"get_info\",\"compile\",\"setup_anim_graph\",\"create_blend_space\",\"set_anim_class\"]},"
            "\"path\":{\"type\":\"string\",\"description\":\"AnimBlueprint或BlendSpace资产路径\"},"
            "\"skeleton_path\":{\"type\":\"string\",\"description\":\"目标骨架路径\"},"
            "\"parent_class\":{\"type\":\"string\",\"description\":\"可选：AnimInstance父类脚本路径，如/Script/ModuleName.ClassName\"},"
            "\"state_name\":{\"type\":\"string\"},"
            "\"from_state\":{\"type\":\"string\"},\"to_state\":{\"type\":\"string\"},"
            "\"crossfade_duration\":{\"type\":\"number\",\"default\":0.2},"
            "\"blend_mode\":{\"type\":\"string\",\"enum\":[\"linear\",\"cubic\",\"hermite_cubic\",\"sinusoidal\",\"quadratic_in_out\",\"cubic_in_out\",\"quartic_in_out\",\"quintic_in_out\",\"circular_in_out\",\"exp_in_out\",\"custom\"]},"
            "\"bidirectional\":{\"type\":\"boolean\",\"default\":false},"
            "\"node_type\":{\"type\":\"string\",\"enum\":[\"sequence_player\",\"blend_space_player\"]},"
            "\"asset_path\":{\"type\":\"string\",\"description\":\"动画资产路径（AnimSequence/BlendSpace）\"},"
            "\"play_rate\":{\"type\":\"number\",\"default\":1.0},"
            "\"loop\":{\"type\":\"boolean\",\"default\":true},"
            "\"blend_space_path\":{\"type\":\"string\",\"description\":\"setup_anim_graph: BlendSpace资产路径\"},"
            "\"axes\":{\"type\":\"array\",\"description\":\"create_blend_space: 坐标轴 [{name,min,max,grid}]\",\"items\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"min\":{\"type\":\"number\"},\"max\":{\"type\":\"number\"},\"grid\":{\"type\":\"integer\"}}}},"
            "\"samples\":{\"type\":\"array\",\"description\":\"create_blend_space: 样本 [{anim_path,x,y}]\",\"items\":{\"type\":\"object\",\"properties\":{\"anim_path\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}}}},"
            "\"character_bp_path\":{\"type\":\"string\",\"description\":\"set_anim_class: 角色Blueprint路径\"},"
            "\"anim_bp_path\":{\"type\":\"string\",\"description\":\"set_anim_class: AnimBlueprint路径（或直接用path参数）\"},"
            "\"mesh_component_name\":{\"type\":\"string\",\"description\":\"set_anim_class: Mesh组件变量名，默认Mesh\",\"default\":\"Mesh\"}"
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
    if (Action == TEXT("add_node"))            return HandleAddAnimNode(ArgsObj, OutError);
    if (Action == TEXT("get_info"))            return HandleGetInfo(ArgsObj, OutError);
    if (Action == TEXT("compile"))             return HandleCompile(ArgsObj, OutError);
    if (Action == TEXT("setup_anim_graph"))    return HandleSetupAnimGraph(ArgsObj, OutError);
    if (Action == TEXT("create_blend_space"))  return HandleCreateBlendSpace(ArgsObj, OutError);
    if (Action == TEXT("set_anim_class"))      return HandleSetAnimClass(ArgsObj, OutError);

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
    Args->TryGetNumberField(TEXT("x"), PosX);
    Args->TryGetNumberField(TEXT("y"), PosY);

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
        UEdGraphPin* EntryOut = EntryNode->FindPin(TEXT("Out"), EGPD_Output);
        UEdGraphPin* StateIn = StateNode->FindPin(TEXT("In"), EGPD_Input);
        if (EntryOut && StateIn)
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

    // 连接状态
    TransNode->CreateConnections(FromNode, ToNode);

    AnimBP->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("blueprint"), AssetPath);
    ResultObj->SetStringField(TEXT("from_state"), FromState);
    ResultObj->SetStringField(TEXT("to_state"), ToState);
    ResultObj->SetNumberField(TEXT("crossfade_duration"), CrossfadeDuration);
    ResultObj->SetBoolField(TEXT("bidirectional"), bBidirectional);
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
    Args->TryGetNumberField(TEXT("x"), PosX);
    Args->TryGetNumberField(TEXT("y"), PosY);

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
        UEdGraphPin* InputPose = ResultNode->FindPin(TEXT("Pose"), EGPD_Input);
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
// AnimBlueprintToJson — 序列化 AnimBlueprint 结构
// ============================================================================

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
