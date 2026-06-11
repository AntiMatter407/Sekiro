#include "Tools/USKAnimBlueprintTool.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimationGraphSchema.h"
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

FString USKAnimBlueprintTool::GetToolDescription() const
{
    return TEXT("动画蓝图操作：创建AnimBP、管理状态机（状态/转换）、添加动画节点（SequencePlayer/BlendSpacePlayer）、查询结构、编译。");
}

FString USKAnimBlueprintTool::GetInputSchemaJson() const
{
    return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create\",\"add_state\",\"add_transition\",\"add_node\",\"get_info\",\"compile\"]},\"path\":{\"type\":\"string\",\"description\":\"AnimBlueprint asset path\"},\"skeleton_path\":{\"type\":\"string\",\"description\":\"Target skeleton path\"},\"state_name\":{\"type\":\"string\"},\"from_state\":{\"type\":\"string\"},\"to_state\":{\"type\":\"string\"},\"crossfade_duration\":{\"type\":\"number\",\"default\":0.2},\"blend_mode\":{\"type\":\"string\",\"enum\":[\"linear\",\"cubic\",\"hermite_cubic\",\"sinusoidal\",\"quadratic_in_out\",\"cubic_in_out\",\"quartic_in_out\",\"quintic_in_out\",\"circular_in_out\",\"exp_in_out\",\"custom\"]},\"bidirectional\":{\"type\":\"boolean\",\"default\":false},\"node_type\":{\"type\":\"string\",\"enum\":[\"sequence_player\",\"blend_space_player\"]},\"asset_path\":{\"type\":\"string\",\"description\":\"Animation asset path (AnimSequence/BlendSpace)\"},\"play_rate\":{\"type\":\"number\",\"default\":1.0},\"loop\":{\"type\":\"boolean\",\"default\":true}},\"required\":[\"action\",\"path\"]}");
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

    if (Action == TEXT("create"))          return HandleCreate(ArgsObj, OutError);
    if (Action == TEXT("add_state"))       return HandleAddState(ArgsObj, OutError);
    if (Action == TEXT("add_transition"))  return HandleAddTransition(ArgsObj, OutError);
    if (Action == TEXT("add_node"))        return HandleAddAnimNode(ArgsObj, OutError);
    if (Action == TEXT("get_info"))        return HandleGetInfo(ArgsObj, OutError);
    if (Action == TEXT("compile"))         return HandleCompile(ArgsObj, OutError);

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

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
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
        OutError = FString::Printf(TEXT("AnimBlueprint已存在: %s"), *AssetPath);
        return FString();
    }

    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = TEXT("创建Package失败");
        return FString();
    }

    // 用 UAnimBlueprint 类型创建
    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        UAnimInstance::StaticClass(),
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

        UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AnimAssetPath);
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

        UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
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
// LoadAnimBlueprint — 加载 AnimBlueprint
// ============================================================================

UAnimBlueprint* USKAnimBlueprintTool::LoadAnimBlueprint(const FString& AssetPath, FString& OutError)
{
    UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
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
