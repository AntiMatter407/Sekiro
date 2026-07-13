#include "SekiroLuaAnimBlueprintEditorLibrary.h"

#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_SekiroLuaOrientationWarping.h"
#include "AnimGraphNode_SekiroLuaStateMachine.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UnLuaInterface.h"

/** 仅供编辑器模块使用的图表查找、节点创建、配置和蓝图收尾辅助函数。 */
namespace
{
    /**
     * 按名称查找动画蓝图内的动画图。
     * @param AnimBlueprint UAnimBlueprint*，待搜索的动画蓝图，可为空。
     * @param AnimationGraphName FName，目标动画图名称。
     * @return UEdGraph*，找到的动画图；参数无效或未找到时返回 nullptr。
     */
    UEdGraph* FindSekiroAnimationGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName)
    {
        if (!AnimBlueprint) return nullptr;

        TArray<UEdGraph*> Graphs;
        AnimBlueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph && Graph->GetFName() == AnimationGraphName)
            {
                return Graph;
            }
        }

        return nullptr;
    }

    /**
     * 查找图表内第一个指定类型的节点。
     * @tparam GraphNodeType UEdGraphNode 派生类型，目标节点类型。
     * @param Graph UEdGraph*，待搜索的图表，可为空。
     * @return GraphNodeType*，找到的第一个目标节点；图表为空或未找到时返回 nullptr。
     */
    template <typename GraphNodeType>
    GraphNodeType* FindFirstSekiroGraphNode(UEdGraph* Graph)
    {
        if (!Graph) return nullptr;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (GraphNodeType* TypedNode = Cast<GraphNodeType>(Node)) return TypedNode;
        }

        return nullptr;
    }

    /**
     * 按动画层名称查找 Lua 动画蓝图宿主节点。
     * @param Graph UEdGraph*，待搜索的动画图，可为空。
     * @param LayerName FName，宿主节点读取的动画层名称；未找到同层节点时仅复用图中唯一的宿主节点。
     * @return UAnimGraphNode_SekiroLuaStateMachine*，同层或唯一的宿主节点；图表为空、没有宿主或存在多个歧义宿主时返回 nullptr。
     */
    UAnimGraphNode_SekiroLuaStateMachine* FindSekiroLuaAnimBlueprintHostNode(
        UEdGraph* Graph,
        FName LayerName)
    {
        if (!Graph) return nullptr;

        UAnimGraphNode_SekiroLuaStateMachine* SoleHostNode = nullptr;
        int32 HostNodeCount = 0;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_SekiroLuaStateMachine* HostNode =
                Cast<UAnimGraphNode_SekiroLuaStateMachine>(Node);
            if (!HostNode) continue;

            if (HostNode->Node.LayerName == LayerName) return HostNode;
            SoleHostNode = HostNode;
            ++HostNodeCount;
        }

        return HostNodeCount == 1 ? SoleHostNode : nullptr;
    }

    /**
     * 在图表内创建指定类型节点并设置编辑器坐标。
     * @tparam GraphNodeType UEdGraphNode 派生类型，需要创建的节点类型。
     * @param Graph UEdGraph*，接收新节点的图表，不能为空。
     * @param NodePosX int32，新节点的横向编辑器坐标。
     * @param NodePosY int32，新节点的纵向编辑器坐标。
     * @return GraphNodeType*，完成创建的节点指针。
     */
    template <typename GraphNodeType>
    GraphNodeType* CreateSekiroGraphNode(UEdGraph* Graph, int32 NodePosX, int32 NodePosY)
    {
        check(Graph);

        FGraphNodeCreator<GraphNodeType> NodeCreator(*Graph);
        GraphNodeType* NewNode = NodeCreator.CreateNode();
        NewNode->NodePosX = NodePosX;
        NewNode->NodePosY = NodePosY;
        NodeCreator.Finalize();
        return NewNode;
    }

    /**
     * 按方向查找动画节点真正承载 Pose 的图表 Pin，忽略同方向的普通暴露属性 Pin。
     * @param Node UEdGraphNode*，待搜索的动画节点，可为空。
     * @param Direction EEdGraphPinDirection，目标 Pin 的输入或输出方向。
     * @return UEdGraphPin*，找到的 Pose Pin；节点为空或不存在目标 Pin 时返回 nullptr。
     */
    UEdGraphPin* FindSekiroPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        if (!Node) return nullptr;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != Direction) continue;

            const UScriptStruct* PinStruct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
            if (PinStruct && PinStruct->IsChildOf(FPoseLinkBase::StaticStruct())) return Pin;
        }

        return nullptr;
    }

    /**
     * 判断两个图表 Pin 是否已经直接连接。
     * @param FirstPin const UEdGraphPin*，待检查的第一个 Pin，可为空。
     * @param SecondPin const UEdGraphPin*，待检查的第二个 Pin，可为空。
     * @return bool，两个有效 Pin 已直接连接时返回 true，否则返回 false。
     */
    bool AreSekiroGraphPinsLinked(const UEdGraphPin* FirstPin, const UEdGraphPin* SecondPin)
    {
        if (!FirstPin || !SecondPin) return false;

        for (const UEdGraphPin* LinkedPin : FirstPin->LinkedTo)
        {
            if (LinkedPin == SecondPin) return true;
        }

        return false;
    }

    /**
     * 为指定 Lua 动画层查找最合适的方向扭转节点。
     * @param Graph UEdGraph*，待搜索的动画图，可为空。
     * @param HostOutputPin const UEdGraphPin*，Lua 宿主输出 Pose Pin；可为空，非空时优先返回与其相连的节点。
     * @param LayerName FName，候选方向扭转节点的动画层名称。
     * @return UAnimGraphNode_SekiroLuaOrientationWarping*，与宿主相连或同层的节点；未找到时返回 nullptr。
     */
    UAnimGraphNode_SekiroLuaOrientationWarping* FindSekiroLuaOrientationWarpingNode(
        UEdGraph* Graph,
        const UEdGraphPin* HostOutputPin,
        FName LayerName)
    {
        if (!Graph) return nullptr;

        UAnimGraphNode_SekiroLuaOrientationWarping* LayerCandidate = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_SekiroLuaOrientationWarping* OrientationWarpingNode =
                Cast<UAnimGraphNode_SekiroLuaOrientationWarping>(Node);
            if (!OrientationWarpingNode) continue;

            UEdGraphPin* InputPin = FindSekiroPosePin(OrientationWarpingNode, EGPD_Input);
            if (AreSekiroGraphPinsLinked(InputPin, HostOutputPin))
            {
                return OrientationWarpingNode;
            }

            if (!LayerCandidate && OrientationWarpingNode->Node.LayerName == LayerName)
            {
                LayerCandidate = OrientationWarpingNode;
            }
        }

        return LayerCandidate;
    }

    /**
     * 根据现有 Pose 连接关系查找最适合复用的惯性化节点。
     * @param Graph UEdGraph*，待搜索的动画图，可为空。
     * @param OrientationWarpingOutputPin const UEdGraphPin*，方向扭转输出 Pose Pin，可为空。
     * @param HostOutputPin const UEdGraphPin*，Lua 宿主输出 Pose Pin，可为空。
     * @param RootInputPin const UEdGraphPin*，动画图输出节点输入 Pose Pin，可为空。
     * @return UAnimGraphNode_Inertialization*，按方向扭转、Root、Lua 连接顺序选出的节点；未找到时返回 nullptr。
     */
    UAnimGraphNode_Inertialization* FindSekiroInertializationNode(
        UEdGraph* Graph,
        const UEdGraphPin* OrientationWarpingOutputPin,
        const UEdGraphPin* HostOutputPin,
        const UEdGraphPin* RootInputPin)
    {
        if (!Graph) return nullptr;

        UAnimGraphNode_Inertialization* HostLinkedCandidate = nullptr;
        UAnimGraphNode_Inertialization* RootLinkedCandidate = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_Inertialization* InertializationNode = Cast<UAnimGraphNode_Inertialization>(Node);
            if (!InertializationNode) continue;

            UEdGraphPin* InputPin = FindSekiroPosePin(InertializationNode, EGPD_Input);
            UEdGraphPin* OutputPin = FindSekiroPosePin(InertializationNode, EGPD_Output);
            if (AreSekiroGraphPinsLinked(InputPin, OrientationWarpingOutputPin))
            {
                return InertializationNode;
            }
            if (!HostLinkedCandidate && AreSekiroGraphPinsLinked(InputPin, HostOutputPin))
            {
                HostLinkedCandidate = InertializationNode;
            }
            if (!RootLinkedCandidate && AreSekiroGraphPinsLinked(OutputPin, RootInputPin))
            {
                RootLinkedCandidate = InertializationNode;
            }
        }

        if (RootLinkedCandidate) return RootLinkedCandidate;
        return HostLinkedCandidate;
    }

    /**
     * 检查动画图 Schema 是否允许连接一对 Pose Pin，但不修改当前图表。
     * @param Graph UEdGraph*，Pin 所属动画图，不能为空。
     * @param OutputPin const UEdGraphPin*，待连接的上游输出 Pose Pin，不能为空。
     * @param InputPin const UEdGraphPin*，待连接的下游输入 Pose Pin，不能为空。
     * @return bool，Schema 允许连接或图表没有 Schema 时返回 true，参数无效或明确拒绝时返回 false。
     */
    bool CanConnectSekiroPosePins(
        UEdGraph* Graph,
        const UEdGraphPin* OutputPin,
        const UEdGraphPin* InputPin)
    {
        if (!Graph || !OutputPin || !InputPin) return false;
        if (AreSekiroGraphPinsLinked(OutputPin, InputPin)) return true;

        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (!Schema) return true;

        const FPinConnectionResponse Response = Schema->CanCreateConnection(OutputPin, InputPin);
        return Response.Response != CONNECT_RESPONSE_DISALLOW;
    }

    /**
     * 使用动画图 Schema 连接一对 Pose Pin；无 Schema 时直接建立链接。
     * @param Graph UEdGraph*，Pin 所属动画图，不能为空。
     * @param OutputPin UEdGraphPin*，上游输出 Pose Pin，不能为空。
     * @param InputPin UEdGraphPin*，下游输入 Pose Pin，不能为空。
     * @return bool，连接建立成功时返回 true，否则返回 false。
     */
    bool ConnectSekiroPosePins(UEdGraph* Graph, UEdGraphPin* OutputPin, UEdGraphPin* InputPin)
    {
        if (!Graph || !OutputPin || !InputPin) return false;
        if (AreSekiroGraphPinsLinked(OutputPin, InputPin)) return true;

        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (Schema) return Schema->TryCreateConnection(OutputPin, InputPin);

        OutputPin->MakeLinkTo(InputPin);
        return true;
    }

    /**
     * 将一组下游 Pose 输入 Pin 纳入编辑器事务并断开旧连接，为确定性重连做准备。
     * 仅清理输入 Pin，避免破坏宿主或中间节点输出 Pose 的其他合法消费者。
     * @param InputPosePins const TArray<UEdGraphPin*>&，需要清理旧连接的下游输入 Pose Pin 数组，空指针会被忽略。
     * @return void，无返回值。
     */
    void ResetSekiroPoseInputLinks(const TArray<UEdGraphPin*>& InputPosePins)
    {
        for (UEdGraphPin* InputPosePin : InputPosePins)
        {
            if (!InputPosePin) continue;

            InputPosePin->Modify();
            InputPosePin->BreakAllPinLinks();
        }
    }

    /**
     * 将有效骨骼名称转换成动画节点使用的骨骼引用数组。
     * @param BoneNames const TArray<FName>&，输入骨骼名称数组，空名称会被忽略。
     * @param OutBoneReferences TArray<FBoneReference>&，接收重建结果的骨骼引用数组。
     * @return void，无返回值。
     */
    void SetSekiroBoneReferences(const TArray<FName>& BoneNames, TArray<FBoneReference>& OutBoneReferences)
    {
        OutBoneReferences.Reset(BoneNames.Num());
        for (const FName& BoneName : BoneNames)
        {
            if (BoneName.IsNone()) continue;

            FBoneReference BoneReference;
            BoneReference.BoneName = BoneName;
            OutBoneReferences.Add(BoneReference);
        }
    }

    /**
     * 标记蓝图结构发生变化并触发一次编辑器编译。
     * @param Blueprint UBlueprint*，需要标记并编译的蓝图，不能为空。
     * @return void，无返回值。
     */
    void FinalizeSekiroBlueprintChanges(UBlueprint* Blueprint)
    {
        check(Blueprint);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }

    /**
     * 查找蓝图实现 UnLua 接口时生成的 GetModuleName 函数图。
     * @param Blueprint UBlueprint*，待搜索的蓝图，可为空。
     * @return UEdGraph*，GetModuleName 函数图；接口或函数图不存在时返回 nullptr。
     */
    UEdGraph* FindSekiroLuaModuleNameGraph(UBlueprint* Blueprint)
    {
        if (!Blueprint) return nullptr;

        FBPInterfaceDescription* InterfaceDescription = Blueprint->ImplementedInterfaces.FindByPredicate(
            [](const FBPInterfaceDescription& Description)
            {
                return Description.Interface == UUnLuaInterface::StaticClass();
            });
        if (!InterfaceDescription) return nullptr;

        const FName ModuleNameFunctionName(TEXT("GetModuleName"));
        for (UEdGraph* InterfaceGraph : InterfaceDescription->Graphs)
        {
            if (InterfaceGraph && InterfaceGraph->GetFName() == ModuleNameFunctionName) return InterfaceGraph;
        }

        return nullptr;
    }

    /**
     * 查找函数图中的函数返回节点。
     * @param FunctionGraph UEdGraph*，待搜索的函数图，可为空。
     * @return UK2Node_FunctionResult*，找到的函数返回节点；未找到时返回 nullptr。
     */
    UK2Node_FunctionResult* FindSekiroFunctionResultNode(UEdGraph* FunctionGraph)
    {
        return FindFirstSekiroGraphNode<UK2Node_FunctionResult>(FunctionGraph);
    }
}

bool USekiroLuaAnimBlueprintEditorLibrary::ConnectLuaAnimBlueprintHostToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName)
{
    if (!AnimBlueprint || AnimationGraphName.IsNone()) return false;

    UEdGraph* AnimationGraph = FindSekiroAnimationGraph(AnimBlueprint, AnimationGraphName);
    if (!AnimationGraph) return false;

    UAnimGraphNode_Root* RootNode = FindFirstSekiroGraphNode<UAnimGraphNode_Root>(AnimationGraph);
    if (!RootNode) return false;

    AnimBlueprint->Modify();
    AnimationGraph->Modify();

    UAnimGraphNode_SekiroLuaStateMachine* HostNode =
        FindSekiroLuaAnimBlueprintHostNode(AnimationGraph, LayerName);
    if (!HostNode)
    {
        HostNode = CreateSekiroGraphNode<UAnimGraphNode_SekiroLuaStateMachine>(
            AnimationGraph,
            RootNode->NodePosX - 900,
            RootNode->NodePosY);
    }

    HostNode->Modify();
    HostNode->Node.LayerName = LayerName;

    UEdGraphPin* RootInputPin = FindSekiroPosePin(RootNode, EGPD_Input);
    UEdGraphPin* HostOutputPin = FindSekiroPosePin(HostNode, EGPD_Output);
    if (!RootInputPin || !HostOutputPin) return false;

    UAnimGraphNode_SekiroLuaOrientationWarping* OrientationWarpingNode =
        FindSekiroLuaOrientationWarpingNode(AnimationGraph, HostOutputPin, LayerName);
    if (!OrientationWarpingNode)
    {
        OrientationWarpingNode = CreateSekiroGraphNode<UAnimGraphNode_SekiroLuaOrientationWarping>(
            AnimationGraph,
            RootNode->NodePosX - 600,
            RootNode->NodePosY);
    }

    OrientationWarpingNode->Modify();
    OrientationWarpingNode->Node.LayerName = LayerName;
    OrientationWarpingNode->Node.Mode = EWarpingEvaluationMode::Manual;

    UEdGraphPin* OrientationWarpingInputPin = FindSekiroPosePin(OrientationWarpingNode, EGPD_Input);
    UEdGraphPin* OrientationWarpingOutputPin = FindSekiroPosePin(OrientationWarpingNode, EGPD_Output);
    if (!OrientationWarpingInputPin || !OrientationWarpingOutputPin) return false;

    UAnimGraphNode_Inertialization* InertializationNode = FindSekiroInertializationNode(
        AnimationGraph,
        OrientationWarpingOutputPin,
        HostOutputPin,
        RootInputPin);
    if (!InertializationNode)
    {
        InertializationNode = CreateSekiroGraphNode<UAnimGraphNode_Inertialization>(
            AnimationGraph,
            RootNode->NodePosX - 300,
            RootNode->NodePosY);
    }

    UEdGraphPin* InertializationInputPin = FindSekiroPosePin(InertializationNode, EGPD_Input);
    UEdGraphPin* InertializationOutputPin = FindSekiroPosePin(InertializationNode, EGPD_Output);
    if (!InertializationInputPin || !InertializationOutputPin) return false;

    RootNode->Modify();
    InertializationNode->Modify();

    InertializationNode->NodePosX = RootNode->NodePosX - 300;
    InertializationNode->NodePosY = RootNode->NodePosY;
    OrientationWarpingNode->NodePosX = RootNode->NodePosX - 600;
    OrientationWarpingNode->NodePosY = RootNode->NodePosY;
    HostNode->NodePosX = RootNode->NodePosX - 900;
    HostNode->NodePosY = RootNode->NodePosY;

    if (!CanConnectSekiroPosePins(AnimationGraph, HostOutputPin, OrientationWarpingInputPin)) return false;
    if (!CanConnectSekiroPosePins(AnimationGraph, OrientationWarpingOutputPin, InertializationInputPin)) return false;
    if (!CanConnectSekiroPosePins(AnimationGraph, InertializationOutputPin, RootInputPin)) return false;

    const bool bHostGraphAlreadyConnected =
        AreSekiroGraphPinsLinked(HostOutputPin, OrientationWarpingInputPin)
        && AreSekiroGraphPinsLinked(OrientationWarpingOutputPin, InertializationInputPin)
        && AreSekiroGraphPinsLinked(InertializationOutputPin, RootInputPin);
    if (!bHostGraphAlreadyConnected)
    {
        ResetSekiroPoseInputLinks({
            RootInputPin,
            OrientationWarpingInputPin,
            InertializationInputPin});

        if (!ConnectSekiroPosePins(AnimationGraph, HostOutputPin, OrientationWarpingInputPin)) return false;
        if (!ConnectSekiroPosePins(AnimationGraph, OrientationWarpingOutputPin, InertializationInputPin)) return false;
        if (!ConnectSekiroPosePins(AnimationGraph, InertializationOutputPin, RootInputPin)) return false;
    }

    FinalizeSekiroBlueprintChanges(AnimBlueprint);
    return true;
}

bool USekiroLuaAnimBlueprintEditorLibrary::ConfigureLuaAnimOrientationWarping(
    UAnimBlueprint* AnimBlueprint,
    FName AnimationGraphName,
    FName LayerName,
    const TArray<FName>& SpineBoneNames,
    FName IKFootRootBoneName,
    const TArray<FName>& IKFootBoneNames,
    float DistributedBoneOrientationAlpha,
    float RotationInterpSpeed)
{
    if (!AnimBlueprint || AnimationGraphName.IsNone()) return false;

    UEdGraph* AnimationGraph = FindSekiroAnimationGraph(AnimBlueprint, AnimationGraphName);
    if (!AnimationGraph) return false;

    UAnimGraphNode_SekiroLuaOrientationWarping* OrientationWarpingNode =
        FindSekiroLuaOrientationWarpingNode(AnimationGraph, nullptr, LayerName);
    if (!OrientationWarpingNode || OrientationWarpingNode->Node.LayerName != LayerName) return false;

    AnimBlueprint->Modify();
    AnimationGraph->Modify();
    OrientationWarpingNode->Modify();

    OrientationWarpingNode->Node.LayerName = LayerName;
    OrientationWarpingNode->Node.Mode = EWarpingEvaluationMode::Manual;
    OrientationWarpingNode->Node.RotationAxis = EAxis::Z;
    OrientationWarpingNode->Node.DistributedBoneOrientationAlpha = FMath::Clamp(
        FMath::IsFinite(DistributedBoneOrientationAlpha) ? DistributedBoneOrientationAlpha : 0.0f,
        0.0f,
        1.0f);
    OrientationWarpingNode->Node.RotationInterpSpeed = FMath::Max(
        0.0f,
        FMath::IsFinite(RotationInterpSpeed) ? RotationInterpSpeed : 0.0f);
    SetSekiroBoneReferences(SpineBoneNames, OrientationWarpingNode->Node.SpineBones);
    OrientationWarpingNode->Node.IKFootRootBone.BoneName = IKFootRootBoneName;
    SetSekiroBoneReferences(IKFootBoneNames, OrientationWarpingNode->Node.IKFootBones);

    FinalizeSekiroBlueprintChanges(AnimBlueprint);
    return true;
}

bool USekiroLuaAnimBlueprintEditorLibrary::BindBlueprintToLuaModule(UBlueprint* Blueprint, const FString& LuaModuleName)
{
    if (!Blueprint || !Blueprint->GeneratedClass || LuaModuleName.IsEmpty()) return false;

    if (!Blueprint->GeneratedClass->ImplementsInterface(UUnLuaInterface::StaticClass()))
    {
        const FTopLevelAssetPath InterfaceClassPathName(UUnLuaInterface::StaticClass());
        const bool bImplemented = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClassPathName);
        if (!bImplemented) return false;
    }

    UEdGraph* GetModuleNameGraph = FindSekiroLuaModuleNameGraph(Blueprint);
    UK2Node_FunctionResult* ReturnNode = FindSekiroFunctionResultNode(GetModuleNameGraph);
    UEdGraphPin* ModuleNamePin = ReturnNode
        ? ReturnNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Input)
        : nullptr;
    if (!GetModuleNameGraph || !ReturnNode || !ModuleNamePin) return false;

    Blueprint->Modify();
    GetModuleNameGraph->Modify();
    ReturnNode->Modify();

    ModuleNamePin->Modify();
    ModuleNamePin->DefaultValue = LuaModuleName;
    FinalizeSekiroBlueprintChanges(Blueprint);
    return true;
}
