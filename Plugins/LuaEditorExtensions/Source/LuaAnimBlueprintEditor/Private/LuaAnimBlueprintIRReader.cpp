#include "LuaAnimBlueprintIRReader.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Misc/SecureHash.h"
#include "Misc/EngineVersionComparison.h"
#include "LuaAnimGraphIRLibrary.h"
#include "LuaAnimGraphNodeRegistry.h"
#include "LuaAnimBlueprintExtension.h"
#include "LuaTransitionRuntimeLibrary.h"
#include "UObject/UObjectHash.h"

namespace LuaAnimBlueprintIRReaderPrivate
{
    const FName InvalidInput(TEXT("Reader.InvalidInput"));
    const FName WrongThread(TEXT("Reader.WrongThread"));
    const FName UnsupportedBlueprintStructure(TEXT("Reader.UnsupportedBlueprintStructure"));
    const FName EventGraphExcluded(TEXT("Reader.EventGraphExcluded"));
    const FName UnsupportedNode(TEXT("Reader.UnsupportedNode"));
    const FName UnsupportedDynamicPins(TEXT("Reader.UnsupportedDynamicPins"));
    const FName UnsupportedPropertyMapping(TEXT("Reader.UnsupportedPropertyMapping"));
    const FName GraphMismatch(TEXT("Reader.GraphMismatch"));
    const FName NodeMismatch(TEXT("Reader.NodeMismatch"));
    const FName LinkMismatch(TEXT("Reader.LinkMismatch"));
    const FName StateMachineMismatch(TEXT("Reader.StateMachineMismatch"));
    const FName UnsupportedTransitionRule(TEXT("Reader.UnsupportedTransitionRule"));
    const FName ValidationFailed(TEXT("Reader.ValidationFailed"));

    void AddError(
        TArray<FLuaAnimIRDiagnostic>& Diagnostics,
        FName Code,
        const FString& Message,
        const FString& SubjectId);

    void AddWarning(
        TArray<FLuaAnimIRDiagnostic>& Diagnostics,
        FName Code,
        const FString& Message,
        const FString& SubjectId);

    /** 在节点值快照中按注册名查找只读属性；不访问 UObject，可在任意线程调用，未找到时返回 nullptr。 */
    const FLuaAnimIRProperty* FindIRProperty(
        const FLuaAnimIRNode& Node,
        const FName PropertyName)
    {
        return Node.Properties.FindByPredicate(
            [PropertyName](const FLuaAnimIRProperty& Property)
            {
                return Property.Name == PropertyName;
            });
    }

    /** 在局部 IR 节点中查找或追加指定类型属性；调用方独占 Node，返回引用仅在 Properties 再次扩容前有效。 */
    FLuaAnimIRProperty& FindOrAddIRProperty(
        FLuaAnimIRNode& Node,
        const FName PropertyName,
        const ELuaAnimIRValueType ValueType)
    {
        FLuaAnimIRProperty* Existing = Node.Properties.FindByPredicate(
            [PropertyName](const FLuaAnimIRProperty& Property)
            {
                return Property.Name == PropertyName;
            });
        if (Existing != nullptr) return *Existing;
        FLuaAnimIRProperty& Added = Node.Properties.AddDefaulted_GetRef();
        Added.Name = PropertyName;
        Added.Value.Type = ValueType;
        Added.DeclarationOrder = Node.Properties.Num() - 1;
        return Added;
    }

    /**
     * 按显式 NodeFactory 反向契约刷新原生节点属性；SequencePlayer 直接读取运行时节点，其他注册节点严格校验语义基线。
     * 只能在游戏线程调用；仅修改局部 Node 值，不修改 NativeNode。失败时追加诊断并返回 false。
     */
    bool ReadRegisteredProperties(
        FLuaAnimIRNode& Node,
        const UEdGraphNode& NativeNode,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        const UAnimGraphNode_SequencePlayer* SequenceNode =
            Cast<UAnimGraphNode_SequencePlayer>(&NativeNode);
        if (SequenceNode != nullptr)
        {
            UAnimSequenceBase* Sequence = SequenceNode->Node.GetSequence();
            if (Sequence == nullptr)
            {
                AddError(Diagnostics, UnsupportedPropertyMapping, TEXT("SequencePlayer has no readable Sequence asset."), Node.Id);
                return false;
            }
            FLuaAnimIRProperty& SequenceProperty = FindOrAddIRProperty(
                Node, TEXT("Sequence"), ELuaAnimIRValueType::SoftObjectPath);
            SequenceProperty.Value.SoftObjectPathValue = FSoftObjectPath(Sequence);
            FLuaAnimIRProperty& LoopProperty = FindOrAddIRProperty(
                Node, TEXT("bLoopAnimation"), ELuaAnimIRValueType::Bool);
#if UE_VERSION_NEWER_THAN(5, 7, 0)
            LoopProperty.Value.BoolValue = SequenceNode->Node.IsLooping();
#else
            LoopProperty.Value.BoolValue = SequenceNode->Node.GetLoopAnimation();
#endif
            FLuaAnimIRProperty& PlayRateProperty = FindOrAddIRProperty(
                Node, TEXT("PlayRate"), ELuaAnimIRValueType::Float);
            PlayRateProperty.Value.FloatValue = SequenceNode->Node.GetPlayRate();
            FLuaAnimIRProperty& StartProperty = FindOrAddIRProperty(
                Node, TEXT("StartPosition"), ELuaAnimIRValueType::Float);
            StartProperty.Value.FloatValue = SequenceNode->Node.GetStartPosition();
            FLuaAnimIRProperty& GroupNameProperty = FindOrAddIRProperty(
                Node, TEXT("GroupName"), ELuaAnimIRValueType::Name);
            GroupNameProperty.Value.NameValue = SequenceNode->Node.GetGroupName();
            FLuaAnimIRProperty& GroupRoleProperty = FindOrAddIRProperty(
                Node, TEXT("GroupRole"), ELuaAnimIRValueType::Enum);
            GroupRoleProperty.Value.IntegerValue = static_cast<int64>(
                SequenceNode->Node.GetGroupRole());
            FLuaAnimIRProperty& GroupMethodProperty = FindOrAddIRProperty(
                Node, TEXT("GroupMethod"), ELuaAnimIRValueType::Enum);
            GroupMethodProperty.Value.IntegerValue = static_cast<int64>(
                SequenceNode->Node.GetGroupMethod());
            return true;
        }

        const FLuaAnimIRNodeContract* Contract = FLuaAnimGraphNodeRegistry::Find(Node.NodeType);
        if (Contract != nullptr)
        {
            for (const FLuaAnimIRPropertyContract& PropertyContract : Contract->Properties)
            {
                const FLuaAnimIRProperty* Property = FindIRProperty(Node, PropertyContract.Name);
                if ((PropertyContract.bRequired && Property == nullptr)
                    || (Property != nullptr && Property->Value.Type != PropertyContract.ValueType))
                {
                    AddError(
                        Diagnostics,
                        UnsupportedPropertyMapping,
                        FString::Printf(TEXT("Property '%s' does not match the registered reverse contract."), *PropertyContract.Name.ToString()),
                        Node.Id);
                    return false;
                }
            }
        }
        return true;
    }

    /** 将 IR 稳定 Pin 名转换为 UE5.2 原生 Pin 名；只读取节点值，可在任意线程调用，空字符串表示缺少必要语义。 */
    FString ResolveNativePinName(
        const FLuaAnimIRNode& Node,
        const FString& IRPinName)
    {
        const bool bVariableGetter =
            Node.EditorNodeClass.ToString()
                == TEXT("/Script/BlueprintGraph.K2Node_VariableGet")
            || Node.NodeType == LuaAnimGraphIRNames::BoolPropertyGetterNode
            || Node.NodeType == LuaAnimGraphIRNames::FloatPropertyGetterNode
            || Node.NodeType == LuaAnimGraphIRNames::BytePropertyGetterNode
            || Node.NodeType == LuaAnimGraphIRNames::EnumPropertyGetterNode;
        if (bVariableGetter)
        {
            const FLuaAnimIRProperty* PropertyName = FindIRProperty(Node, TEXT("PropertyName"));
            return PropertyName != nullptr ? PropertyName->Value.NameValue.ToString() : FString();
        }
        if (Node.NodeType == LuaAnimGraphIRNames::BlendListByBoolNode)
        {
            if (IRPinName == TEXT("TruePose")) return TEXT("BlendPose_0");
            if (IRPinName == TEXT("FalsePose")) return TEXT("BlendPose_1");
            if (IRPinName == TEXT("ActiveValue")) return TEXT("bActiveValue");
        }
        if (Node.NodeType == LuaAnimGraphIRNames::LayeredBlendPerBoneNode)
        {
            if (IRPinName == TEXT("BlendPose")) return TEXT("BlendPoses_0");
            if (IRPinName == TEXT("BlendWeight")) return TEXT("BlendWeights_0");
        }
        return IRPinName;
    }

    /**
     * 追加一条稳定的 Reader 错误诊断。
     * 本函数仅修改调用方独占的值数组，可在任意线程调用。
     *
     * @param Diagnostics 接收诊断的数组。
     * @param Code 稳定错误代码。
     * @param Message 面向编辑器用户的错误说明。
     * @param SubjectId 发生错误的资产、Graph 或节点标识。
     * @return 无返回值。
     */
    void AddError(
        TArray<FLuaAnimIRDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& SubjectId)
    {
        FLuaAnimIRDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ELuaAnimIRDiagnosticSeverity::Error;
        Diagnostic.Message = Message;
        Diagnostic.SubjectId = SubjectId;
    }

    /**
     * 追加一条不会中止 Reader 的结构化警告。
     * 本函数仅修改调用方独占的值数组，可在任意线程调用。
     *
     * @param Diagnostics 接收诊断的数组。
     * @param Code 稳定警告代码。
     * @param Message 面向编辑器用户的警告说明。
     * @param SubjectId 触发警告的资产或 Graph 标识。
     * @return 无返回值。
     */
    void AddWarning(
        TArray<FLuaAnimIRDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& SubjectId)
    {
        FLuaAnimIRDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ELuaAnimIRDiagnosticSeverity::Warning;
        Diagnostic.Message = Message;
        Diagnostic.SubjectId = SubjectId;
    }

    /**
     * 复现 NodeFactory 的稳定 Guid 算法，以可靠关联 LastGeneratedIR 与原生对象。
     * 函数只处理字符串和栈内 MD5 状态，可在任意线程调用。
     *
     * @param Domain 实体命名域。
     * @param StableId IR 稳定 ID。
     * @return 与工厂生成结果一致的 Guid。
     */
    FGuid MakeStableGuid(const TCHAR* Domain, const FString& StableId)
    {
        const FString Source = FString(Domain) + TEXT("|") + StableId;
        const FTCHARToUTF8 Utf8(*Source);
        FMD5 Md5;
        Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        uint8 Digest[16];
        Md5.Final(Digest);
        FGuid Result;
        FMemory::Memcpy(&Result, Digest, sizeof(FGuid));
        return Result;
    }

    /**
     * 将注册契约声明复制为 IR Pin 数组，避免从显示文本猜测类型。
     * 本函数只复制值类型，可在任意线程调用。
     *
     * @param Contract 节点权威契约。
     * @param OutPins 接收 Pin 定义的数组，调用前会清空。
     * @return 无返回值。
     */
    void CopyContractPins(
        const FLuaAnimIRNodeContract& Contract,
        TArray<FLuaAnimIRPin>& OutPins)
    {
        OutPins.Reset(Contract.Pins.Num());
        for (int32 Index = 0; Index < Contract.Pins.Num(); ++Index)
        {
            const FLuaAnimIRPinContract& Source = Contract.Pins[Index];
            FLuaAnimIRPin& Target = OutPins.AddDefaulted_GetRef();
            Target.Name = Source.Name;
            Target.Direction = Source.Direction;
            Target.DataType = Source.DataType;
            Target.bAllowMultipleConnections = Source.bAllowMultipleConnections;
            Target.DeclarationOrder = Index;
        }
    }

    /**
     * 按节点类软路径反查唯一注册契约，不接受未注册的反射节点。
     * 本函数只查询进程内只读注册表，可在任意线程调用。
     *
     * @param Node 待识别的原生编辑器节点。
     * @return 类路径唯一匹配时返回契约，否则返回 nullptr。
     */
    const FLuaAnimIRNodeContract* FindContractForNativeNode(const UEdGraphNode& Node)
    {
        const FString ClassPath = Node.GetClass()->GetPathName();
        const FLuaAnimIRNodeContract* Match = nullptr;
        for (const FLuaAnimIRNodeContract& Contract : FLuaAnimGraphNodeRegistry::GetContracts())
        {
            if (Contract.EditorNodeClassPath.ToString() != ClassPath) continue;
            if (Match != nullptr) return nullptr;
            Match = &Contract;
        }
        return Match;
    }

    /**
     * 在 Blueprint 编辑器正式登记的全部 Graph 中建立 GraphGuid 到 Graph 的只读索引。
     * 不能遍历全部嵌套 UObject，否则原生编译临时生成的 `__AnimFunc_MERGED` Graph 会与源 Graph
     * 共享 Guid 并造成伪冲突。只能在游戏线程调用，因为函数读取 Blueprint Graph 集合。
     *
     * @param Blueprint 待读资产。
     * @param OutGraphs 接收非零且唯一 Guid 的 Graph。
     * @param Diagnostics 接收重复 Guid 诊断。
     * @return 所有 GraphGuid 唯一时返回 true。
     */
    bool IndexGraphs(
        const UAnimBlueprint& Blueprint,
        TMap<FGuid, UEdGraph*>& OutGraphs,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        TArray<UEdGraph*> Graphs;
        Blueprint.GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph == nullptr || !Graph->GraphGuid.IsValid()) continue;
            if (OutGraphs.Contains(Graph->GraphGuid))
            {
                AddError(
                    Diagnostics,
                    GraphMismatch,
                    FString::Printf(TEXT("Duplicate native GraphGuid '%s'."), *Graph->GraphGuid.ToString()),
                    Graph->GetPathName());
                return false;
            }
            OutGraphs.Add(Graph->GraphGuid, Graph);
        }
        return true;
    }

    /**
     * 检查 EventGraph 是否仅包含工厂生成的 BlueprintUpdateAnimation Lua bridge。
     * EventGraph 不进入动画 IR；存在原生业务节点时追加警告并继续读取 AnimGraph，
     * 从而允许编辑器为准的 AnimBlueprint 导出动画图，但不声称 Lua 包含 EventGraph 逻辑。
     * 只能在游戏线程调用，只读 Graph 节点与函数引用。
     *
     * @param Blueprint 待读资产。
     * @param Diagnostics 接收 EventGraph 未被导出的警告。
     * @return 始终返回 true；EventGraph 不再阻断 AnimGraph 读取。
     */
    bool ValidateEventGraph(
        const UAnimBlueprint& Blueprint,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        const UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(&Blueprint);
        if (EventGraph == nullptr || EventGraph->Nodes.IsEmpty()) return true;

        int32 EventCount = 0;
        int32 BridgeCallCount = 0;
        int32 OtherCount = 0;
        for (const UEdGraphNode* Node : EventGraph->Nodes)
        {
            if (Node == nullptr) continue;
            const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            const UFunction* CalledFunction = CallNode != nullptr
                ? CallNode->GetTargetFunction()
                : nullptr;
            if (EventNode != nullptr
                && EventNode->EventReference.GetMemberName()
                    == GET_FUNCTION_NAME_CHECKED(UAnimInstance, BlueprintUpdateAnimation))
            {
                ++EventCount;
            }
            else if (CalledFunction != nullptr
                && CalledFunction->GetOwnerClass()
                    == ULuaTransitionRuntimeLibrary::StaticClass()
                && CalledFunction->GetFName()
                    == GET_FUNCTION_NAME_CHECKED(
                        ULuaTransitionRuntimeLibrary,
                        EvaluateBlueprintUpdateAnimation))
            {
                ++BridgeCallCount;
            }
            else if (Node->IsA<UK2Node_Self>())
            {
                // 固定 bridge 的 Target 输入。
            }
            else
            {
                ++OtherCount;
            }
        }

        if (EventCount == 1 && BridgeCallCount == 1 && OtherCount == 0) return true;
        AddWarning(
            Diagnostics,
            EventGraphExcluded,
            TEXT("EventGraph contains native or non-standard nodes. They remain owned by the AnimBlueprint and are intentionally excluded from generated Lua."),
            EventGraph->GetPathName());
        return true;
    }

    /**
     * 用当前原生节点坐标替换 Graph 的精确位置数组。
     * 只能在游戏线程调用；函数只读节点并修改局部 IR 副本。
     *
     * @param Graph 待更新的 IR Graph。
     * @param NativeNodes 已通过 Guid 可靠关联的 IR ID 到节点映射。
     * @return 无返回值。
     */
    void ReplaceNodePositions(
        FLuaAnimIRGraph& Graph,
        const TMap<FString, UEdGraphNode*>& NativeNodes)
    {
        Graph.Layout.Positions.Reset();
        for (FLuaAnimIRNode& Node : Graph.Nodes)
        {
            const UEdGraphNode* const* NativeNode = NativeNodes.Find(Node.Id);
            if (NativeNode == nullptr || *NativeNode == nullptr) continue;
            FLuaAnimIRLayoutPosition& Position = Graph.Layout.Positions.AddDefaulted_GetRef();
            Position.ElementId = Node.Id;
            Position.X = (*NativeNode)->NodePosX;
            Position.Y = (*NativeNode)->NodePosY;
        }
    }

    /**
     * 校验 Pose Graph 节点集合、类、Pin 约束和连接数量，并读取实际坐标。
     * LastGeneratedIR 为注册节点提供反向属性契约，也为反射节点提供稳定类、Pin 与属性基线；
     * Reader 不从显示文本或任意原生反射字段猜测新的业务语义。
     * 只能在游戏线程调用，NativeGraph 在调用期间保持只读。
     *
     * @param Graph 待更新的局部 IR Graph。
     * @param NativeGraph 与 GraphGuid 匹配的原生 Graph。
     * @param Diagnostics 接收严格失败诊断。
     * @return 原生结构与基线完全可对应时返回 true。
     */
    bool ReadBaselinePoseGraph(
        FLuaAnimIRGraph& Graph,
        const UEdGraph& NativeGraph,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        TMap<FString, UEdGraphNode*> NativeNodes;
        TMap<const UEdGraphNode*, FString> IdsByNode;
        for (FLuaAnimIRNode& Node : Graph.Nodes)
        {
            const FLuaAnimIRNodeContract* Contract = FLuaAnimGraphNodeRegistry::Find(Node.NodeType);
            const bool bReflectedBaseline =
                Contract == nullptr && !Node.EditorNodeClass.IsNull();
            if (Contract == nullptr && !bReflectedBaseline)
            {
                AddError(
                    Diagnostics,
                    UnsupportedNode,
                    TEXT("Baseline contains neither a registered NodeType nor a reflected editor node class."),
                    Node.Id);
                return false;
            }
            if (Contract != nullptr && Contract->bDynamicPins)
            {
                AddError(Diagnostics, UnsupportedDynamicPins, TEXT("Dynamic-pin nodes are not supported by the first reader version."), Node.Id);
                return false;
            }
            UEdGraphNode* Match = nullptr;
            const FGuid ExpectedGuid = MakeStableGuid(TEXT("Node"), Node.Id);
            for (UEdGraphNode* Candidate : NativeGraph.Nodes)
            {
                if (Candidate != nullptr && Candidate->NodeGuid == ExpectedGuid)
                {
                    Match = Candidate;
                    break;
                }
            }
            const FSoftClassPath& ExpectedClassPath = Contract != nullptr
                ? Contract->EditorNodeClassPath
                : Node.EditorNodeClass;
            UClass* ExpectedClass = ExpectedClassPath.ResolveClass();
            if (ExpectedClass == nullptr)
            {
                ExpectedClass = ExpectedClassPath.TryLoadClass<UEdGraphNode>();
            }
            if (Match == nullptr || ExpectedClass == nullptr || Match->GetClass() != ExpectedClass)
            {
                AddError(Diagnostics, NodeMismatch, TEXT("A baseline node cannot be matched to the expected native class and NodeGuid."), Node.Id);
                return false;
            }
            NativeNodes.Add(Node.Id, Match);
            IdsByNode.Add(Match, Node.Id);
            if (Contract != nullptr
                && !ReadRegisteredProperties(Node, *Match, Diagnostics))
            {
                return false;
            }
        }

        int32 SupportedNativeCount = 0;
        for (UEdGraphNode* NativeNode : NativeGraph.Nodes)
        {
            if (NativeNode == nullptr) continue;
            if (!IdsByNode.Contains(NativeNode))
            {
                AddError(Diagnostics, UnsupportedNode, TEXT("Native Pose Graph contains a node not owned by the semantic baseline."), NativeNode->GetPathName());
                return false;
            }
            ++SupportedNativeCount;
        }
        if (SupportedNativeCount != Graph.Nodes.Num())
        {
            AddError(Diagnostics, NodeMismatch, TEXT("Native Pose Graph node count differs from LastGeneratedIR."), Graph.Id);
            return false;
        }

        TMap<FString, FString> BaselineLinkIds;
        for (const FLuaAnimIRLink& Link : Graph.Links)
        {
            const FString Key = Link.Source.NodeId + TEXT("|") + Link.Source.PinName
                + TEXT("|") + Link.Target.NodeId + TEXT("|") + Link.Target.PinName;
            BaselineLinkIds.Add(Key, Link.Id);
        }
        TArray<FLuaAnimIRLink> ReadLinks;
        TSet<FString> ReadLinkIds;
        for (const FLuaAnimIRNode& SourceIRNode : Graph.Nodes)
        {
            UEdGraphNode* const* SourceNativeNode = NativeNodes.Find(SourceIRNode.Id);
            if (SourceNativeNode == nullptr || *SourceNativeNode == nullptr) continue;
            for (const FLuaAnimIRPin& SourceIRPin : SourceIRNode.Pins)
            {
                if (SourceIRPin.Direction != ELuaAnimIRPinDirection::Output) continue;
                const FString SourceNativePinName = ResolveNativePinName(SourceIRNode, SourceIRPin.Name);
                UEdGraphPin* SourcePin = (*SourceNativeNode)->FindPin(SourceNativePinName, EGPD_Output);
                if (SourcePin == nullptr)
                {
                    AddError(Diagnostics, LinkMismatch, TEXT("A registered output Pin cannot be mapped to the native node."), SourceIRNode.Id + TEXT(".") + SourceIRPin.Name);
                    return false;
                }
                for (UEdGraphPin* LinkedPin : SourcePin->LinkedTo)
                {
                    UEdGraphNode* TargetNativeNode = LinkedPin != nullptr ? LinkedPin->GetOwningNode() : nullptr;
                    const FString* TargetNodeId = IdsByNode.Find(TargetNativeNode);
                    if (LinkedPin == nullptr || TargetNodeId == nullptr) continue;
                    const FLuaAnimIRNode* TargetIRNode = Graph.Nodes.FindByPredicate(
                        [TargetNodeId](const FLuaAnimIRNode& Candidate)
                        {
                            return Candidate.Id == *TargetNodeId;
                        });
                    if (TargetIRNode == nullptr) return false;
                    const FLuaAnimIRPin* TargetIRPin = TargetIRNode->Pins.FindByPredicate(
                        [TargetIRNode, LinkedPin](const FLuaAnimIRPin& Candidate)
                        {
                            return Candidate.Direction == ELuaAnimIRPinDirection::Input
                                && ResolveNativePinName(*TargetIRNode, Candidate.Name) == LinkedPin->PinName.ToString();
                        });
                    if (TargetIRPin == nullptr)
                    {
                        AddError(Diagnostics, LinkMismatch, TEXT("A linked native input Pin is outside the registered Pin contract."), LinkedPin->GetName());
                        return false;
                    }
                    FLuaAnimIRLink& ReadLink = ReadLinks.AddDefaulted_GetRef();
                    ReadLink.Source.NodeId = SourceIRNode.Id;
                    ReadLink.Source.PinName = SourceIRPin.Name;
                    ReadLink.Target.NodeId = *TargetNodeId;
                    ReadLink.Target.PinName = TargetIRPin->Name;
                    const FString Key = ReadLink.Source.NodeId + TEXT("|") + ReadLink.Source.PinName
                        + TEXT("|") + ReadLink.Target.NodeId + TEXT("|") + ReadLink.Target.PinName;
                    const FString* BaselineId = BaselineLinkIds.Find(Key);
                    ReadLink.Id = BaselineId != nullptr
                        ? *BaselineId
                        : TEXT("Link.") + FMD5::HashAnsiString(*Key);
                    ReadLink.DeclarationOrder = ReadLinks.Num() - 1;
                    if (ReadLinkIds.Contains(ReadLink.Id))
                    {
                        AddError(Diagnostics, LinkMismatch, TEXT("Deterministic Link ID collision while reading native topology."), ReadLink.Id);
                        return false;
                    }
                    ReadLinkIds.Add(ReadLink.Id);
                }
            }
        }
        Graph.Links = MoveTemp(ReadLinks);

        ReplaceNodePositions(Graph, NativeNodes);
        return true;
    }

    /**
     * 校验状态机状态、Entry、Transition 拓扑及强类型规则基线，并读取状态实际坐标和设置。
     * 无法由稳定 Guid 与 LastGeneratedIR 可靠对应的规则会整体失败。
     * 只能在游戏线程调用，只读原生状态机 Graph。
     *
     * @param Graph 待更新的 StateMachine IR Graph。
     * @param NativeGraph 对应原生状态机 Graph。
     * @param Diagnostics 接收严格失败诊断。
     * @return 状态机可完整读取时返回 true。
     */
    bool ReadBaselineStateMachineGraph(
        FLuaAnimIRGraph& Graph,
        const UAnimationStateMachineGraph& NativeGraph,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        TMap<FString, UAnimStateNode*> States;
        TMap<const UAnimStateNodeBase*, FString> StateIds;
        Graph.Layout.Positions.Reset();
        for (FLuaAnimIRState& State : Graph.StateMachine.States)
        {
            UAnimStateNode* Match = nullptr;
            const FGuid ExpectedGuid = MakeStableGuid(TEXT("State"), State.Id);
            for (UEdGraphNode* Candidate : NativeGraph.Nodes)
            {
                UAnimStateNode* StateNode = Cast<UAnimStateNode>(Candidate);
                if (StateNode != nullptr && StateNode->NodeGuid == ExpectedGuid)
                {
                    Match = StateNode;
                    break;
                }
            }
            if (Match == nullptr || Match->BoundGraph == nullptr)
            {
                AddError(Diagnostics, StateMachineMismatch, TEXT("A baseline state cannot be matched by stable NodeGuid."), State.Id);
                return false;
            }
            State.Name = Match->GetStateName();
            State.bAlwaysResetOnEntry = Match->bAlwaysResetOnEntry;
            States.Add(State.Id, Match);
            StateIds.Add(Match, State.Id);
            FLuaAnimIRLayoutPosition& Position = Graph.Layout.Positions.AddDefaulted_GetRef();
            Position.ElementId = State.Id;
            Position.X = Match->NodePosX;
            Position.Y = Match->NodePosY;
        }

        const UEdGraphNode* EntryTarget = NativeGraph.EntryNode != nullptr
            ? NativeGraph.EntryNode->GetOutputNode()
            : nullptr;
        const FString* EntryId = StateIds.Find(Cast<UAnimStateNodeBase>(EntryTarget));
        if (EntryId == nullptr || *EntryId != Graph.StateMachine.EntryStateId)
        {
            AddError(Diagnostics, StateMachineMismatch, TEXT("Native Entry target differs from LastGeneratedIR."), Graph.Id);
            return false;
        }

        int32 NativeTransitionCount = 0;
        for (FLuaAnimIRTransition& Transition : Graph.StateMachine.Transitions)
        {
            UAnimStateTransitionNode* Match = nullptr;
            const FGuid ExpectedGuid = MakeStableGuid(TEXT("Transition"), Transition.Id);
            for (UEdGraphNode* Candidate : NativeGraph.Nodes)
            {
                UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Candidate);
                if (TransitionNode != nullptr && TransitionNode->NodeGuid == ExpectedGuid)
                {
                    Match = TransitionNode;
                    break;
                }
            }
            if (Match == nullptr
                || StateIds.FindRef(Match->GetPreviousState()) != Transition.SourceStateId
                || StateIds.FindRef(Match->GetNextState()) != Transition.TargetStateId)
            {
                AddError(Diagnostics, StateMachineMismatch, TEXT("A Transition cannot be matched to its baseline topology."), Transition.Id);
                return false;
            }
            if (Match->BoundGraph == nullptr
                || (Transition.Gate.RootIndex == INDEX_NONE && Transition.RuleFunctionName.IsNone()))
            {
                AddError(Diagnostics, UnsupportedTransitionRule, TEXT("Transition Rule is not represented by the current strongly typed baseline AST."), Transition.Id);
                return false;
            }
            Transition.Settings.BlendDuration = Match->CrossfadeDuration;
            Transition.Settings.PriorityOrder = Match->PriorityOrder;
            if (Match->BlendMode != EAlphaBlendOption::Linear)
            {
                AddError(Diagnostics, UnsupportedTransitionRule, TEXT("Only the registered Linear transition blend mode is supported."), Transition.Id);
                return false;
            }
            Transition.Settings.BlendMode = EAlphaBlendOption::Linear;
        }

        for (UEdGraphNode* Candidate : NativeGraph.Nodes)
        {
            if (Cast<UAnimStateTransitionNode>(Candidate) != nullptr) ++NativeTransitionCount;
            else if (Cast<UAnimStateNode>(Candidate) == nullptr && Cast<UAnimStateEntryNode>(Candidate) == nullptr)
            {
                AddError(Diagnostics, UnsupportedNode, TEXT("StateMachine contains an unsupported conduit or custom node."), Candidate->GetPathName());
                return false;
            }
        }
        if (NativeTransitionCount != Graph.StateMachine.Transitions.Num())
        {
            AddError(Diagnostics, StateMachineMismatch, TEXT("Native Transition count differs from LastGeneratedIR."), Graph.Id);
            return false;
        }
        return true;
    }

    /**
     * 从资产当前成员变量描述读取 Validator 支持的 Bool/Float/Byte/Enum 变量。
     * 不支持的类型会明确失败，不从 GeneratedClass 猜测声明来源。
     * 只能在游戏线程调用，只读 Blueprint.NewVariables。
     *
     * @param Blueprint 待读资产。
     * @param OutVariables 接收完整变量 IR。
     * @param Diagnostics 接收不支持类型诊断。
     * @return 全部变量均可表示时返回 true。
     */
    bool ReadVariables(
        const UAnimBlueprint& Blueprint,
        TArray<FLuaAnimIRVariable>& OutVariables,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        OutVariables.Reset(Blueprint.NewVariables.Num());
        for (int32 Index = 0; Index < Blueprint.NewVariables.Num(); ++Index)
        {
            const FBPVariableDescription& Source = Blueprint.NewVariables[Index];
            FLuaAnimIRVariable& Variable = OutVariables.AddDefaulted_GetRef();
            Variable.Name = Source.VarName;
            Variable.DeclarationOrder = Index;
            Variable.bTransient = (Source.PropertyFlags & CPF_Transient) != 0;
            const FName Category = Source.VarType.PinCategory;
            if (Category == UEdGraphSchema_K2::PC_Boolean)
            {
                Variable.DataType = TEXT("Bool");
                Variable.DefaultValue.Type = ELuaAnimIRValueType::Bool;
                Variable.DefaultValue.BoolValue = Source.DefaultValue.ToBool();
            }
            else if (Category == UEdGraphSchema_K2::PC_Real)
            {
                Variable.DataType = TEXT("Float");
                Variable.DefaultValue.Type = ELuaAnimIRValueType::Float;
                Variable.DefaultValue.FloatValue = FCString::Atod(*Source.DefaultValue);
            }
            else if (Category == UEdGraphSchema_K2::PC_Byte)
            {
                const UObject* TypeObject = Source.VarType.PinSubCategoryObject.Get();
                Variable.DataType = TypeObject != nullptr ? FName(TEXT("Enum")) : FName(TEXT("Byte"));
                Variable.TypeObjectPath = TypeObject != nullptr ? FSoftObjectPath(TypeObject->GetPathName()) : FSoftObjectPath();
                Variable.DefaultValue.Type = ELuaAnimIRValueType::Integer;
                Variable.DefaultValue.IntegerValue = FCString::Atoi64(*Source.DefaultValue);
            }
            else
            {
                AddError(Diagnostics, UnsupportedBlueprintStructure, TEXT("Member variable type is outside the current IR contract."), Source.VarName.ToString());
                return false;
            }
        }
        return true;
    }

    /**
     * 读取带 LastGeneratedIR 的资产，并以稳定 Guid 校验当前原生 Graph 后刷新布局和资产级字段。
     * 只能在游戏线程调用；Candidate 是局部值副本，资产始终只读。
     *
     * @param Blueprint 待读资产。
     * @param Baseline 扩展保存的最近一次生成 IR。
     * @param Candidate 接收完整候选 IR。
     * @param Diagnostics 接收严格失败诊断。
     * @return 所有 Graph 均可靠对应时返回 true。
     */
    bool ReadFromBaseline(
        const UAnimBlueprint& Blueprint,
        const FLuaAnimBlueprintIR& Baseline,
        FLuaAnimBlueprintIR& Candidate,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        Candidate = Baseline;
        Candidate.ParentAnimInstanceClass = Blueprint.ParentClass != nullptr
            ? FSoftClassPath(Blueprint.ParentClass->GetPathName())
            : FSoftClassPath();
        Candidate.TargetSkeleton = Blueprint.TargetSkeleton != nullptr
            ? FSoftObjectPath(Blueprint.TargetSkeleton->GetPathName())
            : FSoftObjectPath();
        Candidate.BlueprintKind = Blueprint.BlueprintType == BPTYPE_Interface
            ? ELuaAnimIRBlueprintKind::AnimationLayerInterface
            : ELuaAnimIRBlueprintKind::AnimBlueprint;
        if (!ReadVariables(Blueprint, Candidate.Variables, Diagnostics)) return false;

        Candidate.ImplementedInterfaces.Reset();
        for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
        {
            if (Interface.Interface != nullptr)
            {
                Candidate.ImplementedInterfaces.Add(FSoftClassPath(Interface.Interface->GetPathName()));
            }
        }

        TMap<FGuid, UEdGraph*> NativeGraphs;
        if (!IndexGraphs(Blueprint, NativeGraphs, Diagnostics)) return false;
        for (FLuaAnimIRLayer& Layer : Candidate.Layers)
        {
            for (FLuaAnimIRGraph& Graph : Layer.Graphs)
            {
                UEdGraph* const* NativeGraph = NativeGraphs.Find(MakeStableGuid(TEXT("Graph"), Graph.Id));
                if (NativeGraph == nullptr || *NativeGraph == nullptr)
                {
                    AddError(Diagnostics, GraphMismatch, TEXT("IR Graph cannot be matched by stable GraphGuid."), Graph.Id);
                    return false;
                }
                if (Graph.GraphType == LuaAnimGraphIRNames::StateMachineGraph)
                {
                    const UAnimationStateMachineGraph* StateMachine = Cast<UAnimationStateMachineGraph>(*NativeGraph);
                    if (StateMachine == nullptr || !ReadBaselineStateMachineGraph(Graph, *StateMachine, Diagnostics)) return false;
                }
                else
                {
                    const UAnimationGraph* PoseGraph = Cast<UAnimationGraph>(*NativeGraph);
                    if (PoseGraph == nullptr || !ReadBaselinePoseGraph(Graph, *PoseGraph, Diagnostics)) return false;
                }
            }
        }
        return true;
    }

    /**
     * 在没有 LastGeneratedIR 时读取单个静态 Pose Graph，并从 GraphGuid/NodeGuid 生成确定性 ID。
     * 只能在游戏线程调用；动态 Pin、Owned Graph、无效或碰撞 Guid 会整体失败，原生 Graph 始终只读。
     */
    bool ReadPoseGraphWithoutBaseline(
        const UAnimationGraph& NativeGraph,
        FLuaAnimIRGraph& Graph,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        Graph.Id = TEXT("Graph.Native.") + NativeGraph.GraphGuid.ToString(EGuidFormats::Digits);
        Graph.Name = NativeGraph.GetName();
        Graph.GraphType = LuaAnimGraphIRNames::PoseGraph;
        TMap<const UEdGraphNode*, FString> NodeIds;
        TSet<FString> UsedIds;
        for (UEdGraphNode* NativeNode : NativeGraph.Nodes)
        {
            if (NativeNode == nullptr) continue;
            const FLuaAnimIRNodeContract* Contract = FindContractForNativeNode(*NativeNode);
            if (Contract == nullptr)
            {
                AddError(Diagnostics, UnsupportedNode, TEXT("Native Pose Graph contains an unregistered or ambiguous node class."), NativeNode->GetPathName());
                return false;
            }
            if (Contract->bDynamicPins || Contract->OwnedGraphPolicy == ELuaAnimIROwnedGraphPolicy::Required)
            {
                AddError(Diagnostics, UnsupportedDynamicPins, TEXT("Without LastGeneratedIR, dynamic Pins and owned Graph nodes cannot receive reliable semantic IDs."), NativeNode->GetPathName());
                return false;
            }
            if (!NativeNode->NodeGuid.IsValid())
            {
                AddError(Diagnostics, NodeMismatch, TEXT("A native node has no valid NodeGuid for deterministic ID generation."), NativeNode->GetPathName());
                return false;
            }
            FLuaAnimIRNode& Node = Graph.Nodes.AddDefaulted_GetRef();
            Node.Id = TEXT("Node.Native.") + NativeNode->NodeGuid.ToString(EGuidFormats::Digits);
            if (UsedIds.Contains(Node.Id))
            {
                AddError(Diagnostics, NodeMismatch, TEXT("Deterministic native Node ID collision."), Node.Id);
                return false;
            }
            UsedIds.Add(Node.Id);
            NodeIds.Add(NativeNode, Node.Id);
            Node.NodeType = Contract->NodeType;
            Node.EditorNodeClass = FSoftClassPath(NativeNode->GetClass());
            Node.DisplayName = NativeNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
            Node.DeclarationOrder = Graph.Nodes.Num() - 1;
            CopyContractPins(*Contract, Node.Pins);
            if (!ReadRegisteredProperties(Node, *NativeNode, Diagnostics)) return false;
            FLuaAnimIRLayoutPosition& Position = Graph.Layout.Positions.AddDefaulted_GetRef();
            Position.ElementId = Node.Id;
            Position.X = NativeNode->NodePosX;
            Position.Y = NativeNode->NodePosY;
            if (Contract->RootRole == ELuaAnimIRNodeRootRole::GraphRoot)
            {
                if (!Graph.RootNodeId.IsEmpty())
                {
                    AddError(Diagnostics, NodeMismatch, TEXT("Native Pose Graph contains multiple registered root nodes."), Graph.Id);
                    return false;
                }
                Graph.RootNodeId = Node.Id;
            }
        }

        for (const FLuaAnimIRNode& SourceNode : Graph.Nodes)
        {
            const UEdGraphNode* SourceNative = nullptr;
            for (const TPair<const UEdGraphNode*, FString>& Pair : NodeIds)
            {
                if (Pair.Value == SourceNode.Id)
                {
                    SourceNative = Pair.Key;
                    break;
                }
            }
            if (SourceNative == nullptr) continue;
            for (const FLuaAnimIRPin& SourcePinDefinition : SourceNode.Pins)
            {
                if (SourcePinDefinition.Direction != ELuaAnimIRPinDirection::Output) continue;
                const FString NativePinName = ResolveNativePinName(SourceNode, SourcePinDefinition.Name);
                UEdGraphPin* SourcePin = SourceNative->FindPin(NativePinName, EGPD_Output);
                if (SourcePin == nullptr)
                {
                    AddError(Diagnostics, LinkMismatch, TEXT("Registered output Pin is absent on native node."), SourceNode.Id + TEXT(".") + SourcePinDefinition.Name);
                    return false;
                }
                for (UEdGraphPin* LinkedPin : SourcePin->LinkedTo)
                {
                    const FString* TargetId = LinkedPin != nullptr ? NodeIds.Find(LinkedPin->GetOwningNode()) : nullptr;
                    if (TargetId == nullptr) continue;
                    const FLuaAnimIRNode* TargetNode = Graph.Nodes.FindByPredicate(
                        [TargetId](const FLuaAnimIRNode& Candidate) { return Candidate.Id == *TargetId; });
                    const FLuaAnimIRPin* TargetPinDefinition = TargetNode != nullptr
                        ? TargetNode->Pins.FindByPredicate(
                            [TargetNode, LinkedPin](const FLuaAnimIRPin& Candidate)
                            {
                                return Candidate.Direction == ELuaAnimIRPinDirection::Input
                                    && ResolveNativePinName(*TargetNode, Candidate.Name) == LinkedPin->PinName.ToString();
                            })
                        : nullptr;
                    if (TargetPinDefinition == nullptr)
                    {
                        AddError(Diagnostics, LinkMismatch, TEXT("Linked native input Pin is outside the registered contract."), LinkedPin->GetName());
                        return false;
                    }
                    FLuaAnimIRLink& Link = Graph.Links.AddDefaulted_GetRef();
                    Link.Source.NodeId = SourceNode.Id;
                    Link.Source.PinName = SourcePinDefinition.Name;
                    Link.Target.NodeId = *TargetId;
                    Link.Target.PinName = TargetPinDefinition->Name;
                    const FString Key = Link.Source.NodeId + TEXT("|") + Link.Source.PinName
                        + TEXT("|") + Link.Target.NodeId + TEXT("|") + Link.Target.PinName;
                    Link.Id = TEXT("Link.Native.") + FMD5::HashAnsiString(*Key);
                    Link.DeclarationOrder = Graph.Links.Num() - 1;
                }
            }
        }
        return !Graph.RootNodeId.IsEmpty();
    }

    /**
     * 读取无语义基线的单层标准 AnimBlueprint；首版仅接受唯一静态 Animation Graph，限制通过结构化诊断暴露。
     * 只能在游戏线程调用；Candidate 为调用方独占局部值，资产不会被修改、编译或标脏。
     */
    bool ReadWithoutBaseline(
        const UAnimBlueprint& Blueprint,
        FLuaAnimBlueprintIR& Candidate,
        TArray<FLuaAnimIRDiagnostic>& Diagnostics)
    {
        Candidate.BlueprintKind = Blueprint.BlueprintType == BPTYPE_Interface
            ? ELuaAnimIRBlueprintKind::AnimationLayerInterface
            : ELuaAnimIRBlueprintKind::AnimBlueprint;
        Candidate.ParentAnimInstanceClass = Blueprint.ParentClass != nullptr
            ? FSoftClassPath(Blueprint.ParentClass)
            : FSoftClassPath();
        Candidate.TargetSkeleton = Blueprint.TargetSkeleton != nullptr
            ? FSoftObjectPath(Blueprint.TargetSkeleton)
            : FSoftObjectPath();
        if (!ReadVariables(Blueprint, Candidate.Variables, Diagnostics)) return false;

        const UAnimationGraph* MainGraph = nullptr;
        for (UEdGraph* FunctionGraph : Blueprint.FunctionGraphs)
        {
            const UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(FunctionGraph);
            if (AnimationGraph == nullptr) continue;
            if (MainGraph != nullptr)
            {
                AddError(Diagnostics, UnsupportedBlueprintStructure, TEXT("Without LastGeneratedIR, multiple Animation Layer graphs cannot receive reliable layer semantics."), Blueprint.GetPathName());
                return false;
            }
            MainGraph = AnimationGraph;
        }
        if (MainGraph == nullptr)
        {
            AddError(Diagnostics, UnsupportedBlueprintStructure, TEXT("AnimBlueprint has no readable main Animation Graph."), Blueprint.GetPathName());
            return false;
        }
        FLuaAnimIRLayer& Layer = Candidate.Layers.AddDefaulted_GetRef();
        Layer.Id = TEXT("Layer.Native.") + MainGraph->GraphGuid.ToString(EGuidFormats::Digits);
        Layer.Name = TEXT("Main");
        Layer.FunctionName = MainGraph->GetFName();
        FLuaAnimIRGraph& Graph = Layer.Graphs.AddDefaulted_GetRef();
        if (!ReadPoseGraphWithoutBaseline(*MainGraph, Graph, Diagnostics)) return false;
        Layer.RootGraphId = Graph.Id;
        return true;
    }
}

/**
 * 严格、只读地将标准 UAnimBlueprint 转换为 Canonical IR。
 * 当前首版优先使用资产扩展的 LastGeneratedIR 作为 NodeGuid/GraphGuid 语义基线；无基线的复杂节点、属性、
 * 动态 Pin 与 Transition Rule 会明确失败，绝不猜测或返回部分结果。无基线读取成功后会从资产扩展恢复
 * Lua 交换模块身份；没有扩展的标准蓝图使用资产路径作为只读来源标识，保证 IR 可进入统一验证流程。
 * 函数不调用 Modify、Compile 或 Save，只能在游戏线程调用。
 *
 * @param AnimBlueprint 待读标准动画蓝图，不可为空。
 * @param OutBlueprint 成功时接收 Canonicalize 且 Validate 通过的完整 IR；失败时重置为空值。
 * @param OutDiagnostics 接收 Reader 与 Validator 的结构化诊断。
 * @return 完整读取并验证成功时返回 true，否则返回 false。
 */
bool FLuaAnimBlueprintIRReader::Read(
    const UAnimBlueprint* AnimBlueprint,
    FLuaAnimBlueprintIR& OutBlueprint,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimBlueprintIRReaderPrivate;
    OutBlueprint = FLuaAnimBlueprintIR();
    OutDiagnostics.Reset();
    if (!IsInGameThread())
    {
        AddError(OutDiagnostics, WrongThread, TEXT("AnimBlueprint IR reader must run on the game thread."), TEXT("Blueprint"));
        return false;
    }
    if (AnimBlueprint == nullptr)
    {
        AddError(OutDiagnostics, InvalidInput, TEXT("AnimBlueprint is null."), TEXT("Blueprint"));
        return false;
    }
    if (!ValidateEventGraph(*AnimBlueprint, OutDiagnostics)) return false;

    const ULuaAnimBlueprintExtension* Extension =
        ULuaAnimBlueprintExtension::Find(AnimBlueprint);
    FLuaAnimBlueprintIR Candidate;
    const bool bRead = Extension != nullptr && Extension->bHasLastGeneratedIR
        ? ReadFromBaseline(*AnimBlueprint, Extension->LastGeneratedIR, Candidate, OutDiagnostics)
        : ReadWithoutBaseline(*AnimBlueprint, Candidate, OutDiagnostics);
    if (!bRead) return false;
    if (Candidate.SourceModule.IsEmpty())
    {
        const FString ExchangeSourceModule = Extension != nullptr
            ? Extension->GetExchangeLuaModuleName()
            : FString();
        Candidate.SourceModule = !ExchangeSourceModule.IsEmpty()
            ? ExchangeSourceModule
            : AnimBlueprint->GetPathName();
    }
    ULuaAnimGraphIRLibrary::Canonicalize(Candidate);
    TArray<FLuaAnimIRDiagnostic> ValidationDiagnostics;
    if (!ULuaAnimGraphIRLibrary::Validate(Candidate, ValidationDiagnostics))
    {
        OutDiagnostics.Append(ValidationDiagnostics);
        AddError(OutDiagnostics, ValidationFailed, TEXT("Read IR did not pass the authoritative validator."), AnimBlueprint->GetPathName());
        return false;
    }
    OutDiagnostics.Append(ValidationDiagnostics);
    OutBlueprint = MoveTemp(Candidate);
    return true;
}
