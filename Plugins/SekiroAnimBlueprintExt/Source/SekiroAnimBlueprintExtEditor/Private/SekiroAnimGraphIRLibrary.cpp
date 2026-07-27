#include "SekiroAnimGraphIRLibrary.h"

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Misc/PackageName.h"
#include "SekiroAnimGraphNodeRegistry.h"

const FName SekiroAnimGraphIRNames::PoseGraph(TEXT("Pose"));
const FName SekiroAnimGraphIRNames::StatePoseGraph(TEXT("StatePose"));
const FName SekiroAnimGraphIRNames::StateMachineGraph(TEXT("StateMachine"));
const FName SekiroAnimGraphIRNames::OutputPoseNode(TEXT("OutputPose"));
const FName SekiroAnimGraphIRNames::StateResultNode(TEXT("StateResult"));
const FName SekiroAnimGraphIRNames::SequencePlayerNode(TEXT("SequencePlayer"));
const FName SekiroAnimGraphIRNames::StateMachineNode(TEXT("StateMachine"));
const FName SekiroAnimGraphIRNames::InertializationNode(TEXT("Inertialization"));
const FName SekiroAnimGraphIRNames::LocalToComponentSpaceNode(TEXT("LocalToComponentSpace"));
const FName SekiroAnimGraphIRNames::ComponentToLocalSpaceNode(TEXT("ComponentToLocalSpace"));
const FName SekiroAnimGraphIRNames::OrientationWarpingNode(TEXT("OrientationWarping"));
const FName SekiroAnimGraphIRNames::FootPlacementNode(TEXT("FootPlacement"));
const FName SekiroAnimGraphIRNames::LegIKNode(TEXT("LegIK"));
const FName SekiroAnimGraphIRNames::TwoBoneIKNode(TEXT("TwoBoneIK"));
const FName SekiroAnimGraphIRNames::SaveCachedPoseNode(TEXT("SaveCachedPose"));
const FName SekiroAnimGraphIRNames::UseCachedPoseNode(TEXT("UseCachedPose"));
const FName SekiroAnimGraphIRNames::BoolPropertyGetterNode(TEXT("BoolPropertyGetter"));
const FName SekiroAnimGraphIRNames::FloatPropertyGetterNode(TEXT("FloatPropertyGetter"));
const FName SekiroAnimGraphIRNames::BytePropertyGetterNode(TEXT("BytePropertyGetter"));
const FName SekiroAnimGraphIRNames::EnumPropertyGetterNode(TEXT("EnumPropertyGetter"));
const FName SekiroAnimGraphIRNames::BlendListByBoolNode(TEXT("BlendListByBool"));
const FName SekiroAnimGraphIRNames::BlendListByEnumNode(TEXT("BlendListByEnum"));
const FName SekiroAnimGraphIRNames::SlotNode(TEXT("Slot"));
const FName SekiroAnimGraphIRNames::LayeredBlendPerBoneNode(TEXT("LayeredBlendPerBone"));
const FName SekiroAnimGraphIRNames::LinkedAnimLayerNode(TEXT("LinkedAnimLayer"));
const FName SekiroAnimGraphIRNames::LinkedAnimGraphNode(TEXT("LinkedAnimGraph"));
const FName SekiroAnimGraphIRNames::LinkedInputPoseNode(TEXT("LinkedInputPose"));
const FName SekiroAnimGraphIRNames::PoseData(TEXT("Pose"));
const FName SekiroAnimGraphIRNames::ComponentPoseData(TEXT("ComponentPose"));
const FName SekiroAnimGraphIRNames::BoolData(TEXT("Bool"));
const FName SekiroAnimGraphIRNames::FloatData(TEXT("Float"));
const FName SekiroAnimGraphIRNames::ByteData(TEXT("Byte"));
const FName SekiroAnimGraphIRNames::EnumData(TEXT("Enum"));

namespace SekiroAnimGraphIRValidation
{
    const FName UnsupportedSchemaVersion(TEXT("IR.UnsupportedSchemaVersion"));
    const FName EmptySourceModule(TEXT("IR.EmptySourceModule"));
    const FName MissingParentAnimInstanceClass(TEXT("IR.MissingParentAnimInstanceClass"));
    const FName MissingTargetSkeleton(TEXT("IR.MissingTargetSkeleton"));
    const FName InvalidTargetSkeletonPath(TEXT("IR.InvalidTargetSkeletonPath"));
    const FName EmptyLayerId(TEXT("IR.EmptyLayerId"));
    const FName DuplicateLayerId(TEXT("IR.DuplicateLayerId"));
    const FName EmptyGraphId(TEXT("IR.EmptyGraphId"));
    const FName DuplicateGraphId(TEXT("IR.DuplicateGraphId"));
    const FName EmptyGraphType(TEXT("IR.EmptyGraphType"));
    const FName EmptyNodeId(TEXT("IR.EmptyNodeId"));
    const FName DuplicateNodeId(TEXT("IR.DuplicateNodeId"));
    const FName EmptyNodeType(TEXT("IR.EmptyNodeType"));
    const FName UnknownNodeType(TEXT("IR.UnknownNodeType"));
    const FName NodeGraphTypeNotAllowed(TEXT("IR.NodeGraphTypeNotAllowed"));
    const FName MissingRegisteredPin(TEXT("IR.MissingRegisteredPin"));
    const FName UnexpectedPin(TEXT("IR.UnexpectedPin"));
    const FName PinContractMismatch(TEXT("IR.PinContractMismatch"));
    const FName UnknownNodeProperty(TEXT("IR.UnknownNodeProperty"));
    const FName MissingRequiredNodeProperty(TEXT("IR.MissingRequiredNodeProperty"));
    const FName NodePropertyTypeMismatch(TEXT("IR.NodePropertyTypeMismatch"));
    const FName RootNodeTypeMismatch(TEXT("IR.RootNodeTypeMismatch"));
    const FName UnexpectedOwnedGraph(TEXT("IR.UnexpectedOwnedGraph"));
    const FName EmptyLinkId(TEXT("IR.EmptyLinkId"));
    const FName DuplicateLinkId(TEXT("IR.DuplicateLinkId"));
    const FName EmptyStateId(TEXT("IR.EmptyStateId"));
    const FName DuplicateStateId(TEXT("IR.DuplicateStateId"));
    const FName EmptyTransitionId(TEXT("IR.EmptyTransitionId"));
    const FName DuplicateTransitionId(TEXT("IR.DuplicateTransitionId"));
    const FName EmptyPinName(TEXT("IR.EmptyPinName"));
    const FName DuplicatePinName(TEXT("IR.DuplicatePinName"));
    const FName EmptyPinDataType(TEXT("IR.EmptyPinDataType"));
    const FName EmptyPropertyName(TEXT("IR.EmptyPropertyName"));
    const FName DuplicatePropertyName(TEXT("IR.DuplicatePropertyName"));
    const FName MissingLayerRootGraph(TEXT("IR.MissingLayerRootGraph"));
    const FName LayerRootGraphNotFound(TEXT("IR.LayerRootGraphNotFound"));
    const FName LayerRootGraphTypeMismatch(TEXT("IR.LayerRootGraphTypeMismatch"));
    const FName StateMachineNodeOutsidePoseGraph(TEXT("IR.StateMachineNodeOutsidePoseGraph"));
    const FName StateMachineNodeMissingOwnedGraph(TEXT("IR.StateMachineNodeMissingOwnedGraph"));
    const FName OwnedGraphNotFound(TEXT("IR.OwnedGraphNotFound"));
    const FName OwnedGraphOutsideLayer(TEXT("IR.OwnedGraphOutsideLayer"));
    const FName OwnedGraphTypeMismatch(TEXT("IR.OwnedGraphTypeMismatch"));
    const FName StateMachineGraphMissingOwner(TEXT("IR.StateMachineGraphMissingOwner"));
    const FName StateMachineGraphMultipleOwners(TEXT("IR.StateMachineGraphMultipleOwners"));
    const FName MissingGraphRootNode(TEXT("IR.MissingGraphRootNode"));
    const FName GraphRootNodeNotFound(TEXT("IR.GraphRootNodeNotFound"));
    const FName LinkNodeNotFound(TEXT("IR.LinkNodeNotFound"));
    const FName LinkPinNotFound(TEXT("IR.LinkPinNotFound"));
    const FName LinkDirection(TEXT("IR.LinkDirection"));
    const FName LinkTypeMismatch(TEXT("IR.LinkTypeMismatch"));
    const FName MultipleInputLinks(TEXT("IR.MultipleInputLinks"));
    const FName PoseGraphCycle(TEXT("IR.PoseGraphCycle"));
    const FName MissingEntryState(TEXT("IR.MissingEntryState"));
    const FName EntryStateNotFound(TEXT("IR.EntryStateNotFound"));
    const FName StateGraphNotFound(TEXT("IR.StateGraphNotFound"));
    const FName StateGraphOutsideLayer(TEXT("IR.StateGraphOutsideLayer"));
    const FName StateGraphTypeMismatch(TEXT("IR.StateGraphTypeMismatch"));
    const FName StateGraphIsLayerRoot(TEXT("IR.StateGraphIsLayerRoot"));
    const FName StateGraphMissingOwner(TEXT("IR.StateGraphMissingOwner"));
    const FName StateGraphMultipleOwners(TEXT("IR.StateGraphMultipleOwners"));
    const FName GraphOwnershipCycle(TEXT("IR.GraphOwnershipCycle"));
    const FName TransitionStateNotFound(TEXT("IR.TransitionStateNotFound"));
    const FName EmptyTransitionKey(TEXT("IR.EmptyTransitionKey"));
    const FName DuplicateTransitionKey(TEXT("IR.DuplicateTransitionKey"));
    const FName InvalidTransitionKey(TEXT("IR.InvalidTransitionKey"));
    const FName EmptyTransitionRule(TEXT("IR.EmptyTransitionRule"));
    const FName InvalidTransitionRuleFunctionName(TEXT("IR.InvalidTransitionRuleFunctionName"));
    const FName DuplicateTransitionRuleFunctionName(TEXT("IR.DuplicateTransitionRuleFunctionName"));
    const FName InvalidTransitionBlendDuration(TEXT("IR.InvalidTransitionBlendDuration"));
    const FName DuplicateTransitionPriority(TEXT("IR.DuplicateTransitionPriority"));
    const FName InvalidVariable(TEXT("IR.InvalidVariable"));
    const FName DuplicateVariable(TEXT("IR.DuplicateVariable"));
    const FName InvalidTransitionGate(TEXT("IR.InvalidTransitionGate"));
    const FName InvalidLayoutStyle(TEXT("IR.InvalidLayoutStyle"));
    const FName InvalidLayoutGrid(TEXT("IR.InvalidLayoutGrid"));
    const FName DuplicateLayoutGrid(TEXT("IR.DuplicateLayoutGrid"));
    const FName InvalidLayoutItem(TEXT("IR.InvalidLayoutItem"));
    const FName DuplicateLayoutElement(TEXT("IR.DuplicateLayoutElement"));
    const FName OccupiedLayoutCell(TEXT("IR.OccupiedLayoutCell"));

    /**
     * 向验证结果追加一条错误诊断，不执行日志输出或资产加载。
     * 可在任意线程调用，但调用方必须独占 OutDiagnostics。
     *
     * @param OutDiagnostics 接收诊断的可变数组。
     * @param Code 面向工具链的稳定错误代码。
     * @param Message 面向人的错误说明。
     * @param SubjectId 关联实体的稳定 ID，可为空。
     * @param SourceLocation Lua 声明位置，未知字段保持零值。
     */
    void AddError(
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics,
        const FName Code,
        const FString& Message,
        const FString& SubjectId,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRDiagnostic& Diagnostic = OutDiagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ESekiroAnimIRDiagnosticSeverity::Error;
        Diagnostic.Message = Message;
        Diagnostic.SubjectId = SubjectId;
        Diagnostic.SourceLocation = SourceLocation;
    }

    /**
     * 校验一个实体 ID 是否非空且在对应全局类型域内唯一，并记录成功注册的 ID。
     * 可在任意线程调用；函数只修改 SeenIds 与 OutDiagnostics，不持有输入引用。
     *
     * @param Id 待注册的大小写敏感稳定 ID。
     * @param EmptyCode 空 ID 的诊断代码。
     * @param DuplicateCode 重复 ID 的诊断代码。
     * @param EntityLabel 诊断消息中的实体类型名。
     * @param SourceLocation 实体的 Lua 声明位置。
     * @param SeenIds 对应实体类型已注册 ID 集合。
     * @param OutDiagnostics 接收错误诊断的数组。
     * @return ID 非空且首次注册时返回 true，否则返回 false。
     */
    bool RegisterStableId(
        const FString& Id,
        const FName EmptyCode,
        const FName DuplicateCode,
        const TCHAR* EntityLabel,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        TSet<FString>& SeenIds,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
    {
        if (Id.IsEmpty())
        {
            AddError(
                OutDiagnostics,
                EmptyCode,
                FString::Printf(TEXT("%s ID must not be empty."), EntityLabel),
                Id,
                SourceLocation);
            return false;
        }

        if (SeenIds.Contains(Id))
        {
            AddError(
                OutDiagnostics,
                DuplicateCode,
                FString::Printf(TEXT("Duplicate %s ID '%s'."), EntityLabel, *Id),
                Id,
                SourceLocation);
            return false;
        }

        SeenIds.Add(Id);
        return true;
    }

    /**
     * 在节点的 Lua IR 声明内按大小写敏感稳定名称查找 Pin。
     * 可在任意线程调用；函数不修改节点，返回指针仅在节点 Pins 未被改动期间有效。
     *
     * @param Node 待查询节点。
     * @param PinName 目标稳定 Pin 名。
     * @return 找到时返回节点数组内 Pin 指针，否则返回 nullptr。
     */
    const FSekiroAnimIRPin* FindDeclaredPin(const FSekiroAnimIRNode& Node, const FString& PinName)
    {
        for (const FSekiroAnimIRPin& Pin : Node.Pins)
        {
            if (Pin.Name.Equals(PinName, ESearchCase::CaseSensitive)) return &Pin;
        }

        return nullptr;
    }

    /**
     * 在权威节点契约内按大小写敏感稳定名称查找 Pin。
     * 本函数只读注册表值，不加载 UObject，可在任意线程调用。
     *
     * @param Contract 待查询的只读节点契约。
     * @param PinName Lua Link 或 IR Pin 声明使用的稳定名称。
     * @return 找到时返回契约数组内常量指针，否则返回 nullptr；指针随 Contract 有效。
     */
    const FSekiroAnimIRPinContract* FindPinContract(
        const FSekiroAnimIRNodeContract& Contract,
        const FString& PinName)
    {
        for (const FSekiroAnimIRPinContract& PinContract : Contract.Pins)
        {
            if (PinContract.Name.Equals(PinName, ESearchCase::CaseSensitive)) return &PinContract;
        }

        return nullptr;
    }

    /**
     * 在权威节点契约内按 FName 语义查找属性。
     * 本函数只读注册表值，不加载 UObject，可在任意线程调用。
     *
     * @param Contract 待查询的只读节点契约。
     * @param PropertyName Lua IR 声明的注册属性名。
     * @return 找到时返回契约数组内常量指针，否则返回 nullptr；指针随 Contract 有效。
     */
    const FSekiroAnimIRPropertyContract* FindPropertyContract(
        const FSekiroAnimIRNodeContract& Contract,
        const FName PropertyName)
    {
        for (const FSekiroAnimIRPropertyContract& PropertyContract : Contract.Properties)
        {
            if (PropertyContract.Name == PropertyName) return &PropertyContract;
        }

        return nullptr;
    }

    /**
     * 判断节点契约是否允许出现在指定 GraphType 中。
     * 本函数只比较稳定 FName，不访问 UObject，可在任意线程调用。
     *
     * @param Contract 待检查的只读节点契约。
     * @param GraphType 节点所在 Graph 的注册类型名。
     * @return GraphType 存在于契约白名单时返回 true，否则返回 false。
     */
    bool IsGraphTypeAllowed(const FSekiroAnimIRNodeContract& Contract, const FName GraphType)
    {
        return Contract.AllowedGraphTypes.Contains(GraphType);
    }

    /**
     * 查询指定 Pose GraphType 唯一允许的根节点契约。
     * 本函数遍历只读注册表，不加载 UObject，可在任意线程调用。
     *
     * @param GraphType Pose 或 StatePose 注册类型名。
     * @return 找到允许该 GraphType 的 GraphRoot 契约时返回常量指针，否则返回 nullptr。
     */
    const FSekiroAnimIRNodeContract* FindRootContract(const FName GraphType)
    {
        for (const FSekiroAnimIRNodeContract& Contract : FSekiroAnimGraphNodeRegistry::GetContracts())
        {
            if (Contract.RootRole == ESekiroAnimIRNodeRootRole::GraphRoot
                && IsGraphTypeAllowed(Contract, GraphType))
            {
                return &Contract;
            }
        }

        return nullptr;
    }

    /**
     * 判断 Graph 类型是否承载可连接、可求值的 Pose 节点拓扑。
     * 本函数只比较注册名，不访问 UObject，可在任意线程调用。
     *
     * @param GraphType 待判断的 Graph 注册类型名。
     * @return 主 Pose Graph 或状态专属 StatePose Graph 返回 true，其他类型返回 false。
     */
    bool IsPoseGraphType(const FName GraphType)
    {
        return GraphType == SekiroAnimGraphIRNames::PoseGraph
            || GraphType == SekiroAnimGraphIRNames::StatePoseGraph;
    }

    /**
     * 判断字符串是否可作为跨平台稳定的 Lua 标识符。
     * 标识符限定为 ASCII 字母、数字与下划线，首字符不能为数字，并拒绝 Lua 5.4 保留字。
     * 函数不访问 Lua VM、不分配 UObject，可在任意线程调用。
     *
     * @param Identifier 待检查的 Key 或规则函数名，按大小写敏感语义处理。
     * @return 字符串非空、满足词法规则且不是 Lua 保留字时返回 true，否则返回 false。
     */
    bool IsValidLuaIdentifier(const FString& Identifier)
    {
        if (Identifier.IsEmpty()) return false;

        const TCHAR FirstCharacter = Identifier[0];
        const bool bValidFirstCharacter = FirstCharacter == TEXT('_')
            || (FirstCharacter >= TEXT('A') && FirstCharacter <= TEXT('Z'))
            || (FirstCharacter >= TEXT('a') && FirstCharacter <= TEXT('z'));
        if (!bValidFirstCharacter) return false;

        for (int32 CharacterIndex = 1; CharacterIndex < Identifier.Len(); ++CharacterIndex)
        {
            const TCHAR Character = Identifier[CharacterIndex];
            const bool bValidCharacter = Character == TEXT('_')
                || (Character >= TEXT('A') && Character <= TEXT('Z'))
                || (Character >= TEXT('a') && Character <= TEXT('z'))
                || (Character >= TEXT('0') && Character <= TEXT('9'));
            if (!bValidCharacter) return false;
        }

        static const TCHAR* ReservedWords[] =
        {
            TEXT("and"), TEXT("break"), TEXT("do"), TEXT("else"), TEXT("elseif"), TEXT("end"),
            TEXT("false"), TEXT("for"), TEXT("function"), TEXT("goto"), TEXT("if"), TEXT("in"),
            TEXT("local"), TEXT("nil"), TEXT("not"), TEXT("or"), TEXT("repeat"), TEXT("return"),
            TEXT("then"), TEXT("true"), TEXT("until"), TEXT("while")
        };
        for (const TCHAR* ReservedWord : ReservedWords)
        {
            if (Identifier.Equals(ReservedWord, ESearchCase::CaseSensitive)) return false;
        }

        return true;
    }

    /**
     * 深度优先访问 Pose Graph 的节点依赖，用三色标记检测回边。
     * 可在任意线程调用；函数只修改调用方提供的 VisitStates，不修改 IR。
     *
     * @param NodeId 当前访问的稳定节点 ID。
     * @param Adjacency 输出节点到输入节点的邻接表。
     * @param VisitStates 节点访问状态，1 表示访问中，2 表示已完成。
     * @return 当前节点可到达回边时返回 true，否则返回 false。
     */
    bool VisitPoseNode(
        const FString& NodeId,
        const TMap<FString, TArray<FString>>& Adjacency,
        TMap<FString, uint8>& VisitStates)
    {
        const uint8* ExistingState = VisitStates.Find(NodeId);
        if (ExistingState && *ExistingState == 1) return true;
        if (ExistingState && *ExistingState == 2) return false;

        VisitStates.Add(NodeId, 1);
        const TArray<FString>* Targets = Adjacency.Find(NodeId);
        if (Targets)
        {
            for (const FString& TargetId : *Targets)
            {
                if (VisitPoseNode(TargetId, Adjacency, VisitStates)) return true;
            }
        }

        VisitStates.Add(NodeId, 2);
        return false;
    }

    /**
     * 基于 Graph 内可解析的有向 Link 检测 Pose 依赖环。
     * 可在任意线程调用；函数分配临时邻接表，不修改 Graph。
     *
     * @param Graph 待检查的 Pose Graph。
     * @return 存在任意有向环时返回 true，否则返回 false。
     */
    bool HasPoseCycle(const FSekiroAnimIRGraph& Graph)
    {
        TSet<FString> NodeIds;
        TMap<FString, TArray<FString>> Adjacency;
        TMap<FString, uint8> VisitStates;

        for (const FSekiroAnimIRNode& Node : Graph.Nodes)
        {
            if (!Node.Id.IsEmpty()) NodeIds.Add(Node.Id);
        }

        for (const FSekiroAnimIRLink& Link : Graph.Links)
        {
            if (NodeIds.Contains(Link.Source.NodeId) && NodeIds.Contains(Link.Target.NodeId))
            {
                Adjacency.FindOrAdd(Link.Source.NodeId).Add(Link.Target.NodeId);
            }
        }

        for (const FSekiroAnimIRNode& Node : Graph.Nodes)
        {
            if (!Node.Id.IsEmpty() && VisitPoseNode(Node.Id, Adjacency, VisitStates)) return true;
        }

        return false;
    }

    /**
     * 深度优先访问 Graph 所有权边，并用三色标记查找当前路径上的回边。
     * 本函数只读取邻接表并修改调用方提供的访问状态，可在任意线程调用。
     *
     * @param GraphId 当前访问的稳定 Graph ID。
     * @param Adjacency Graph 到其直接拥有 Graph 的邻接表。
     * @param VisitStates Graph 访问状态，1 表示访问中，2 表示已完成。
     * @param OutCycleGraphId 检测到回边时接收环上的稳定 Graph ID；未检测到时保持不变。
     * @return 当前 Graph 可到达所有权环时返回 true，否则返回 false。
     */
    bool VisitGraphOwnership(
        const FString& GraphId,
        const TMap<FString, TArray<FString>>& Adjacency,
        TMap<FString, uint8>& VisitStates,
        FString& OutCycleGraphId)
    {
        const uint8* ExistingState = VisitStates.Find(GraphId);
        if (ExistingState && *ExistingState == 1)
        {
            OutCycleGraphId = GraphId;
            return true;
        }
        if (ExistingState && *ExistingState == 2) return false;

        VisitStates.Add(GraphId, 1);
        const TArray<FString>* OwnedGraphIds = Adjacency.Find(GraphId);
        if (OwnedGraphIds)
        {
            for (const FString& OwnedGraphId : *OwnedGraphIds)
            {
                if (VisitGraphOwnership(OwnedGraphId, Adjacency, VisitStates, OutCycleGraphId)) return true;
            }
        }

        VisitStates.Add(GraphId, 2);
        return false;
    }

    /**
     * 按 Graph 的规范顺序检测 Node 与 State 共同建立的所有权拓扑是否为 DAG。
     * 本函数不验证边的类型或跨层引用；调用方应只传入已解析到当前 Layer 的边。
     * 可在任意线程调用，不修改 Graph 数组和邻接表。
     *
     * @param Graphs 当前 Layer 内按稳定 ID 规范化后的 Graph 集合。
     * @param Adjacency Graph 所有权邻接表。
     * @param OutCycleGraphId 检测到环时接收环上的稳定 Graph ID；无环时清空。
     * @return 存在至少一个 Graph 所有权环时返回 true，否则返回 false。
     */
    bool HasGraphOwnershipCycle(
        const TArray<FSekiroAnimIRGraph>& Graphs,
        const TMap<FString, TArray<FString>>& Adjacency,
        FString& OutCycleGraphId)
    {
        OutCycleGraphId.Reset();
        TMap<FString, uint8> VisitStates;
        for (const FSekiroAnimIRGraph& Graph : Graphs)
        {
            if (!Graph.Id.IsEmpty()
                && VisitGraphOwnership(Graph.Id, Adjacency, VisitStates, OutCycleGraphId))
            {
                return true;
            }
        }

        return false;
    }
}

/**
 * 将 Blueprint IR 的所有集合原地转换为确定性规范顺序。
 * 身份集合以大小写敏感稳定 ID 为主键、声明顺序为次键；Pin 与属性以声明顺序为主键。
 * Transition 先按显式优先级、再按声明顺序和稳定 ID 排列，确保规范化不会改变转换语义。
 * 函数不验证引用、不加载对象，也不改变字段值。可在任意线程调用，但调用方必须独占 Blueprint。
 *
 * @param Blueprint 要原地规范化的 IR；返回后所有嵌套集合均为规范顺序。
 */
void USekiroAnimGraphIRLibrary::Canonicalize(FSekiroAnimBlueprintIR& Blueprint)
{
    Blueprint.ImplementedInterfaces.Sort([](const FSoftClassPath& Left, const FSoftClassPath& Right)
    {
        return Left.ToString() < Right.ToString();
    });
    Blueprint.Variables.Sort([](const FSekiroAnimIRVariable& Left, const FSekiroAnimIRVariable& Right)
    {
        if (Left.DeclarationOrder != Right.DeclarationOrder) return Left.DeclarationOrder < Right.DeclarationOrder;
        return Left.Name.LexicalLess(Right.Name);
    });
    Blueprint.Layers.Sort([](const FSekiroAnimIRLayer& Left, const FSekiroAnimIRLayer& Right)
    {
        const int32 IdComparison = Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive);
        if (IdComparison != 0) return IdComparison < 0;
        return Left.DeclarationOrder < Right.DeclarationOrder;
    });

    for (FSekiroAnimIRLayer& Layer : Blueprint.Layers)
    {
        Layer.Parameters.Sort([](
            const FSekiroAnimIRFunctionParameter& Left,
            const FSekiroAnimIRFunctionParameter& Right)
        {
            if (Left.DeclarationOrder != Right.DeclarationOrder)
            {
                return Left.DeclarationOrder < Right.DeclarationOrder;
            }
            return Left.Name.LexicalLess(Right.Name);
        });

        Layer.Graphs.Sort([](const FSekiroAnimIRGraph& Left, const FSekiroAnimIRGraph& Right)
        {
            const int32 IdComparison = Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive);
            if (IdComparison != 0) return IdComparison < 0;
            return Left.DeclarationOrder < Right.DeclarationOrder;
        });

        for (FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            Graph.Nodes.Sort([](const FSekiroAnimIRNode& Left, const FSekiroAnimIRNode& Right)
            {
                const int32 IdComparison = Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive);
                if (IdComparison != 0) return IdComparison < 0;
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            for (FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                Node.Pins.Sort([](const FSekiroAnimIRPin& Left, const FSekiroAnimIRPin& Right)
                {
                    if (Left.DeclarationOrder != Right.DeclarationOrder)
                    {
                        return Left.DeclarationOrder < Right.DeclarationOrder;
                    }

                    const int32 NameComparison = Left.Name.Compare(Right.Name, ESearchCase::CaseSensitive);
                    if (NameComparison != 0) return NameComparison < 0;
                    return static_cast<uint8>(Left.Direction) < static_cast<uint8>(Right.Direction);
                });

                Node.Properties.Sort([](const FSekiroAnimIRProperty& Left, const FSekiroAnimIRProperty& Right)
                {
                    if (Left.DeclarationOrder != Right.DeclarationOrder)
                    {
                        return Left.DeclarationOrder < Right.DeclarationOrder;
                    }

                    return Left.Name.LexicalLess(Right.Name);
                });
            }

            Graph.Links.Sort([](const FSekiroAnimIRLink& Left, const FSekiroAnimIRLink& Right)
            {
                const int32 IdComparison = Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive);
                if (IdComparison != 0) return IdComparison < 0;
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            Graph.Layout.Grids.Sort([](const FSekiroAnimIRLayoutGrid& Left, const FSekiroAnimIRLayoutGrid& Right)
            {
                return Left.Name.Compare(Right.Name, ESearchCase::CaseSensitive) < 0;
            });
            for (FSekiroAnimIRLayoutGrid& Grid : Graph.Layout.Grids)
            {
                Grid.Items.Sort([](const FSekiroAnimIRLayoutItem& Left, const FSekiroAnimIRLayoutItem& Right)
                {
                    if (Left.DeclarationOrder != Right.DeclarationOrder)
                    {
                        return Left.DeclarationOrder < Right.DeclarationOrder;
                    }
                    return Left.ElementId.Compare(Right.ElementId, ESearchCase::CaseSensitive) < 0;
                });
            }

            Graph.StateMachine.States.Sort([](const FSekiroAnimIRState& Left, const FSekiroAnimIRState& Right)
            {
                const int32 IdComparison = Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive);
                if (IdComparison != 0) return IdComparison < 0;
                return Left.DeclarationOrder < Right.DeclarationOrder;
            });

            Graph.StateMachine.Transitions.Sort([](
                const FSekiroAnimIRTransition& Left,
                const FSekiroAnimIRTransition& Right)
            {
                if (Left.Settings.PriorityOrder != Right.Settings.PriorityOrder)
                {
                    return Left.Settings.PriorityOrder < Right.Settings.PriorityOrder;
                }
                if (Left.DeclarationOrder != Right.DeclarationOrder)
                {
                    return Left.DeclarationOrder < Right.DeclarationOrder;
                }

                return Left.Id.Compare(Right.Id, ESearchCase::CaseSensitive) < 0;
            });
        }
    }
}

/**
 * 验证一份 AnimBlueprint IR 的稳定身份、引用、Pin 契约、Pose DAG 与状态机拓扑。
 * 函数先规范化局部副本以保证诊断顺序稳定，不修改输入、不加载软路径且不访问编辑器对象。
 * 可在任意线程调用，调用方必须独占 OutDiagnostics；函数会先清空该数组。
 *
 * @param Blueprint 待验证的只读 IR。
 * @param OutDiagnostics 接收按规范遍历顺序生成的结构化诊断。
 * @return 没有 Error 级诊断时返回 true，否则返回 false。
 */
bool USekiroAnimGraphIRLibrary::Validate(
    const FSekiroAnimBlueprintIR& Blueprint,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimGraphIRValidation;

    OutDiagnostics.Reset();
    FSekiroAnimBlueprintIR CanonicalBlueprint = Blueprint;
    Canonicalize(CanonicalBlueprint);

    TSet<FString> LayerIds;
    TSet<FString> GraphIds;
    TSet<FString> NodeIds;
    TSet<FString> LinkIds;
    TSet<FString> StateIds;
    TSet<FString> TransitionIds;
    TSet<FName> TransitionRuleFunctionNames;
    TSet<FName> VariableNames;

    if (CanonicalBlueprint.SchemaVersion != 2 && CanonicalBlueprint.SchemaVersion != 3)
    {
        AddError(
            OutDiagnostics,
            UnsupportedSchemaVersion,
            FString::Printf(
                TEXT("Unsupported AnimGraph IR schema version %d; expected 2 or 3."),
                CanonicalBlueprint.SchemaVersion),
            CanonicalBlueprint.SourceModule,
            CanonicalBlueprint.SourceLocation);
    }

    if (CanonicalBlueprint.SourceModule.IsEmpty())
    {
        AddError(
            OutDiagnostics,
            EmptySourceModule,
            TEXT("AnimGraph IR SourceModule must not be empty."),
            FString(),
            CanonicalBlueprint.SourceLocation);
    }

    for (const FSekiroAnimIRVariable& Variable : CanonicalBlueprint.Variables)
    {
        const bool bSupportedType = Variable.DataType == TEXT("Bool")
            || Variable.DataType == TEXT("Float")
            || Variable.DataType == TEXT("Byte")
            || Variable.DataType == TEXT("Enum");
        const bool bDefaultMatches = (Variable.DataType == TEXT("Bool") && Variable.DefaultValue.Type == ESekiroAnimIRValueType::Bool)
            || (Variable.DataType == TEXT("Float") && Variable.DefaultValue.Type == ESekiroAnimIRValueType::Float)
            || ((Variable.DataType == TEXT("Byte") || Variable.DataType == TEXT("Enum"))
                && Variable.DefaultValue.Type == ESekiroAnimIRValueType::Integer
                && Variable.DefaultValue.IntegerValue >= 0
                && Variable.DefaultValue.IntegerValue <= MAX_uint8);
        if (Variable.Name.IsNone() || !bSupportedType || !bDefaultMatches
            || (Variable.DataType == TEXT("Enum") && !Variable.TypeObjectPath.IsValid()))
        {
            AddError(OutDiagnostics, InvalidVariable,
                FString::Printf(TEXT("Variable '%s' has an invalid type, default value, or Enum path."), *Variable.Name.ToString()),
                Variable.Name.ToString(), Variable.SourceLocation);
        }
        if (VariableNames.Contains(Variable.Name))
        {
            AddError(OutDiagnostics, DuplicateVariable,
                FString::Printf(TEXT("AnimBlueprint contains duplicate Variable '%s'."), *Variable.Name.ToString()),
                Variable.Name.ToString(), Variable.SourceLocation);
        }
        VariableNames.Add(Variable.Name);
    }

    if (CanonicalBlueprint.BlueprintKind == ESekiroAnimIRBlueprintKind::AnimBlueprint
        && CanonicalBlueprint.ParentAnimInstanceClass.IsNull())
    {
        AddError(
            OutDiagnostics,
            MissingParentAnimInstanceClass,
            TEXT("AnimGraph IR must declare a parent AnimInstance class."),
            CanonicalBlueprint.SourceModule,
            CanonicalBlueprint.SourceLocation);
    }

    if (CanonicalBlueprint.BlueprintKind == ESekiroAnimIRBlueprintKind::AnimationLayerInterface)
    {
        if (!CanonicalBlueprint.TargetSkeleton.IsNull())
        {
            AddError(
                OutDiagnostics,
                InvalidTargetSkeletonPath,
                TEXT("Animation Layer Interface IR must not declare a TargetSkeleton."),
                CanonicalBlueprint.SourceModule,
                CanonicalBlueprint.SourceLocation);
        }
    }
    else if (CanonicalBlueprint.TargetSkeleton.IsNull())
    {
        AddError(
            OutDiagnostics,
            MissingTargetSkeleton,
            TEXT("AnimGraph IR must declare a TargetSkeleton asset path."),
            CanonicalBlueprint.SourceModule,
            CanonicalBlueprint.SourceLocation);
    }
    else
    {
        const FString TargetSkeletonPath = CanonicalBlueprint.TargetSkeleton.ToString();
        const bool bIsValidTargetSkeletonPath = CanonicalBlueprint.TargetSkeleton.IsValid()
            && CanonicalBlueprint.TargetSkeleton.IsAsset()
            && !CanonicalBlueprint.TargetSkeleton.GetAssetFName().IsNone()
            && FPackageName::IsValidObjectPath(TargetSkeletonPath);
        if (!bIsValidTargetSkeletonPath)
        {
            AddError(
                OutDiagnostics,
                InvalidTargetSkeletonPath,
                FString::Printf(
                    TEXT("AnimGraph IR TargetSkeleton '%s' must be a valid top-level asset object path."),
                    *TargetSkeletonPath),
                CanonicalBlueprint.SourceModule,
                CanonicalBlueprint.SourceLocation);
        }
    }

    for (const FSekiroAnimIRLayer& Layer : CanonicalBlueprint.Layers)
    {
        RegisterStableId(
            Layer.Id,
            EmptyLayerId,
            DuplicateLayerId,
            TEXT("Layer"),
            Layer.SourceLocation,
            LayerIds,
            OutDiagnostics);

        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            RegisterStableId(
                Graph.Id,
                EmptyGraphId,
                DuplicateGraphId,
                TEXT("Graph"),
                Graph.SourceLocation,
                GraphIds,
                OutDiagnostics);

            if (Graph.GraphType.IsNone())
            {
                AddError(
                    OutDiagnostics,
                    EmptyGraphType,
                    FString::Printf(TEXT("Graph '%s' has no GraphType."), *Graph.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }

            const uint8 LayoutStyleValue = static_cast<uint8>(Graph.Layout.Style);
            if (LayoutStyleValue > static_cast<uint8>(ESekiroAnimIRLayoutStyle::HierarchicalBlocks))
            {
                AddError(OutDiagnostics, InvalidLayoutStyle,
                    FString::Printf(TEXT("Graph '%s' has an invalid LayoutStyle value."), *Graph.Id),
                    Graph.Id, Graph.SourceLocation);
            }

            TSet<FString> LayoutElementIds;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes) LayoutElementIds.Add(Node.Id);
            for (const FSekiroAnimIRState& State : Graph.StateMachine.States) LayoutElementIds.Add(State.Id);
            TSet<FString> LayoutGridNames;
            TSet<FString> PlacedElementIds;
            for (const FSekiroAnimIRLayoutGrid& Grid : Graph.Layout.Grids)
            {
                if (Grid.Name.IsEmpty() || Grid.RegionColumn < 0 || Grid.RegionRow < 0
                    || Grid.CellWidth <= 0 || Grid.CellHeight <= 0)
                {
                    AddError(OutDiagnostics, InvalidLayoutGrid,
                        FString::Printf(TEXT("Graph '%s' contains invalid Layout Grid '%s'."), *Graph.Id, *Grid.Name),
                        Graph.Id, Graph.SourceLocation);
                }
                if (LayoutGridNames.Contains(Grid.Name))
                {
                    AddError(OutDiagnostics, DuplicateLayoutGrid,
                        FString::Printf(TEXT("Graph '%s' contains duplicate Layout Grid '%s'."), *Graph.Id, *Grid.Name),
                        Graph.Id, Graph.SourceLocation);
                }
                LayoutGridNames.Add(Grid.Name);

                TSet<FString> OccupiedCells;
                for (const FSekiroAnimIRLayoutItem& Item : Grid.Items)
                {
                    if (!LayoutElementIds.Contains(Item.ElementId) || Item.Column < 0 || Item.Row < 0
                        || Item.ColumnSpan <= 0 || Item.RowSpan <= 0)
                    {
                        AddError(OutDiagnostics, InvalidLayoutItem,
                            FString::Printf(TEXT("Layout Grid '%s' contains invalid element '%s'."), *Grid.Name, *Item.ElementId),
                            Item.ElementId, Graph.SourceLocation);
                    }
                    if (PlacedElementIds.Contains(Item.ElementId))
                    {
                        AddError(OutDiagnostics, DuplicateLayoutElement,
                            FString::Printf(TEXT("Layout element '%s' is placed more than once."), *Item.ElementId),
                            Item.ElementId, Graph.SourceLocation);
                    }
                    PlacedElementIds.Add(Item.ElementId);
                    for (int32 ColumnOffset = 0; ColumnOffset < Item.ColumnSpan; ++ColumnOffset)
                    {
                        for (int32 RowOffset = 0; RowOffset < Item.RowSpan; ++RowOffset)
                        {
                            const FString Cell = FString::Printf(TEXT("%d:%d"),
                                Item.Column + ColumnOffset, Item.Row + RowOffset);
                            if (OccupiedCells.Contains(Cell))
                            {
                                AddError(OutDiagnostics, OccupiedLayoutCell,
                                    FString::Printf(TEXT("Layout Grid '%s' cell %s is occupied more than once."), *Grid.Name, *Cell),
                                    Item.ElementId, Graph.SourceLocation);
                            }
                            OccupiedCells.Add(Cell);
                        }
                    }
                }
            }

            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                RegisterStableId(
                    Node.Id,
                    EmptyNodeId,
                    DuplicateNodeId,
                    TEXT("Node"),
                    Node.SourceLocation,
                    NodeIds,
                    OutDiagnostics);

                const FSekiroAnimIRNodeContract* NodeContract = nullptr;
                if (Node.NodeType.IsNone())
                {
                    AddError(
                        OutDiagnostics,
                        EmptyNodeType,
                        FString::Printf(TEXT("Node '%s' has no NodeType."), *Node.Id),
                        Node.Id,
                        Node.SourceLocation);
                }
                else
                {
                    NodeContract = FSekiroAnimGraphNodeRegistry::Find(Node.NodeType);
                    if (!NodeContract && Node.EditorNodeClass.IsNull())
                    {
                        AddError(
                            OutDiagnostics,
                            UnknownNodeType,
                            FString::Printf(
                                TEXT("Node '%s' declares unknown NodeType '%s'."),
                                *Node.Id,
                                *Node.NodeType.ToString()),
                            Node.Id,
                            Node.SourceLocation);
                    }
                }

                if (!Node.EditorNodeClass.IsNull()
                    && NodeContract == nullptr
                    && (!Node.OwnedGraphId.IsEmpty() || Node.Id == Graph.RootNodeId))
                {
                    AddError(
                        OutDiagnostics,
                        UnexpectedOwnedGraph,
                        TEXT("Reflection nodes cannot own a Graph or replace a schema-owned root node."),
                        Node.Id,
                        Node.SourceLocation);
                }

                if (NodeContract)
                {
                    if (!IsGraphTypeAllowed(*NodeContract, Graph.GraphType))
                    {
                        AddError(
                            OutDiagnostics,
                            NodeGraphTypeNotAllowed,
                            FString::Printf(
                                TEXT("NodeType '%s' is not allowed in GraphType '%s'."),
                                *Node.NodeType.ToString(),
                                *Graph.GraphType.ToString()),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    if (NodeContract->RootRole == ESekiroAnimIRNodeRootRole::GraphRoot
                        && (Graph.RootNodeId.IsEmpty() || Node.Id != Graph.RootNodeId))
                    {
                        AddError(
                            OutDiagnostics,
                            RootNodeTypeMismatch,
                            FString::Printf(
                                TEXT("Root-only NodeType '%s' must be the RootNodeId of Graph '%s'."),
                                *Node.NodeType.ToString(),
                                *Graph.Id),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    if (NodeContract->OwnedGraphPolicy == ESekiroAnimIROwnedGraphPolicy::Forbidden
                        && !Node.OwnedGraphId.IsEmpty())
                    {
                        AddError(
                            OutDiagnostics,
                            UnexpectedOwnedGraph,
                            FString::Printf(
                                TEXT("NodeType '%s' does not allow OwnedGraphId '%s'."),
                                *Node.NodeType.ToString(),
                                *Node.OwnedGraphId),
                            Node.Id,
                            Node.SourceLocation);
                    }
                }

                TSet<FString> PinNames;
                for (const FSekiroAnimIRPin& Pin : Node.Pins)
                {
                    if (Pin.Name.IsEmpty())
                    {
                        AddError(
                            OutDiagnostics,
                            EmptyPinName,
                            FString::Printf(TEXT("Node '%s' contains an empty Pin name."), *Node.Id),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else if (PinNames.Contains(Pin.Name))
                    {
                        AddError(
                            OutDiagnostics,
                            DuplicatePinName,
                            FString::Printf(TEXT("Node '%s' contains duplicate Pin '%s'."), *Node.Id, *Pin.Name),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        PinNames.Add(Pin.Name);
                    }

                    if (Pin.DataType.IsNone())
                    {
                        AddError(
                            OutDiagnostics,
                            EmptyPinDataType,
                            FString::Printf(TEXT("Pin '%s.%s' has no DataType."), *Node.Id, *Pin.Name),
                            Node.Id,
                            Node.SourceLocation);
                    }

                    if (NodeContract && !NodeContract->bDynamicPins && !Pin.Name.IsEmpty())
                    {
                        const FSekiroAnimIRPinContract* PinContract = FindPinContract(*NodeContract, Pin.Name);
                        if (!PinContract)
                        {
                            AddError(
                                OutDiagnostics,
                                UnexpectedPin,
                                FString::Printf(
                                    TEXT("Node '%s' declares unregistered Pin '%s'."),
                                    *Node.Id,
                                    *Pin.Name),
                                Node.Id,
                                Node.SourceLocation);
                        }
                        else if (Pin.Direction != PinContract->Direction
                            || Pin.DataType != PinContract->DataType
                            || Pin.bAllowMultipleConnections != PinContract->bAllowMultipleConnections)
                        {
                            AddError(
                                OutDiagnostics,
                                PinContractMismatch,
                                FString::Printf(
                                    TEXT("Pin '%s.%s' does not match the registered direction, data type, or connection policy."),
                                    *Node.Id,
                                    *Pin.Name),
                                Node.Id,
                                Node.SourceLocation);
                        }
                    }
                }

                if (NodeContract)
                {
                    for (const FSekiroAnimIRPinContract& PinContract : NodeContract->Pins)
                    {
                        if (!FindDeclaredPin(Node, PinContract.Name))
                        {
                            AddError(
                                OutDiagnostics,
                                MissingRegisteredPin,
                                FString::Printf(
                                    TEXT("Node '%s' is missing registered Pin '%s'."),
                                    *Node.Id,
                                    *PinContract.Name),
                                Node.Id,
                                Node.SourceLocation);
                        }
                    }
                }

                TSet<FName> PropertyNames;
                for (const FSekiroAnimIRProperty& Property : Node.Properties)
                {
                    if (Property.Name.IsNone())
                    {
                        AddError(
                            OutDiagnostics,
                            EmptyPropertyName,
                            FString::Printf(TEXT("Node '%s' contains an empty Property name."), *Node.Id),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else if (PropertyNames.Contains(Property.Name))
                    {
                        AddError(
                            OutDiagnostics,
                            DuplicatePropertyName,
                            FString::Printf(TEXT("Node '%s' contains duplicate Property '%s'."), *Node.Id, *Property.Name.ToString()),
                            Node.Id,
                            Node.SourceLocation);
                    }
                    else
                    {
                        PropertyNames.Add(Property.Name);
                    }

                    if (NodeContract && !Property.Name.IsNone())
                    {
                        const FSekiroAnimIRPropertyContract* PropertyContract =
                            FindPropertyContract(*NodeContract, Property.Name);
                        if (!PropertyContract)
                        {
                            AddError(
                                OutDiagnostics,
                                UnknownNodeProperty,
                                FString::Printf(
                                    TEXT("Node '%s' declares unregistered Property '%s'."),
                                    *Node.Id,
                                    *Property.Name.ToString()),
                                Node.Id,
                                Node.SourceLocation);
                        }
                        else if (Property.Value.Type != PropertyContract->ValueType)
                        {
                            AddError(
                                OutDiagnostics,
                                NodePropertyTypeMismatch,
                                FString::Printf(
                                    TEXT("Property '%s.%s' does not match the registered value type."),
                                    *Node.Id,
                                    *Property.Name.ToString()),
                                Node.Id,
                                Node.SourceLocation);
                        }
                    }
                }

                if (NodeContract)
                {
                    for (const FSekiroAnimIRPropertyContract& PropertyContract : NodeContract->Properties)
                    {
                        if (PropertyContract.bRequired && !PropertyNames.Contains(PropertyContract.Name))
                        {
                            AddError(
                                OutDiagnostics,
                                MissingRequiredNodeProperty,
                                FString::Printf(
                                    TEXT("Node '%s' is missing required Property '%s'."),
                                    *Node.Id,
                                    *PropertyContract.Name.ToString()),
                                Node.Id,
                                Node.SourceLocation);
                        }
                    }
                }
            }

            for (const FSekiroAnimIRLink& Link : Graph.Links)
            {
                RegisterStableId(
                    Link.Id,
                    EmptyLinkId,
                    DuplicateLinkId,
                    TEXT("Link"),
                    Link.SourceLocation,
                    LinkIds,
                    OutDiagnostics);
            }

            for (const FSekiroAnimIRState& State : Graph.StateMachine.States)
            {
                RegisterStableId(
                    State.Id,
                    EmptyStateId,
                    DuplicateStateId,
                    TEXT("State"),
                    State.SourceLocation,
                    StateIds,
                    OutDiagnostics);
            }

            for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
            {
                RegisterStableId(
                    Transition.Id,
                    EmptyTransitionId,
                    DuplicateTransitionId,
                    TEXT("Transition"),
                    Transition.SourceLocation,
                    TransitionIds,
                    OutDiagnostics);
            }
        }
    }

    for (const FSekiroAnimIRLayer& Layer : CanonicalBlueprint.Layers)
    {
        TMap<FString, const FSekiroAnimIRGraph*> LayerGraphsById;
        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            if (!Graph.Id.IsEmpty() && !LayerGraphsById.Contains(Graph.Id))
            {
                LayerGraphsById.Add(Graph.Id, &Graph);
            }
        }

        if (Layer.RootGraphId.IsEmpty())
        {
            AddError(
                OutDiagnostics,
                MissingLayerRootGraph,
                FString::Printf(TEXT("Layer '%s' has no RootGraphId."), *Layer.Id),
                Layer.Id,
                Layer.SourceLocation);
        }
        else
        {
            const FSekiroAnimIRGraph* const* RootGraphResult = LayerGraphsById.Find(Layer.RootGraphId);
            if (!RootGraphResult)
            {
                AddError(
                    OutDiagnostics,
                    LayerRootGraphNotFound,
                    FString::Printf(
                        TEXT("Layer '%s' references missing root Graph '%s'."),
                        *Layer.Id,
                        *Layer.RootGraphId),
                    Layer.Id,
                    Layer.SourceLocation);
            }
            else if ((*RootGraphResult)->GraphType != SekiroAnimGraphIRNames::PoseGraph)
            {
                AddError(
                    OutDiagnostics,
                    LayerRootGraphTypeMismatch,
                    FString::Printf(TEXT("Layer '%s' root Graph must be a Pose Graph."), *Layer.Id),
                    Layer.Id,
                    Layer.SourceLocation);
            }
        }

        TMap<FString, int32> StateMachineOwnerCounts;
        TMap<FString, int32> StateGraphOwnerCounts;
        TMap<FString, TArray<FString>> GraphOwnershipAdjacency;
        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                if (!Graph.Id.IsEmpty()
                    && !Node.OwnedGraphId.IsEmpty()
                    && LayerGraphsById.Contains(Node.OwnedGraphId))
                {
                    GraphOwnershipAdjacency.FindOrAdd(Graph.Id).Add(Node.OwnedGraphId);
                }

                if (Node.NodeType != SekiroAnimGraphIRNames::StateMachineNode) continue;

                if (!IsPoseGraphType(Graph.GraphType))
                {
                    AddError(
                        OutDiagnostics,
                        StateMachineNodeOutsidePoseGraph,
                        FString::Printf(
                            TEXT("StateMachine Node '%s' must be declared in a Pose or StatePose Graph."),
                            *Node.Id),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                if (Node.OwnedGraphId.IsEmpty())
                {
                    AddError(
                        OutDiagnostics,
                        StateMachineNodeMissingOwnedGraph,
                        FString::Printf(TEXT("StateMachine Node '%s' has no OwnedGraphId."), *Node.Id),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                const FSekiroAnimIRGraph* const* OwnedGraphResult = LayerGraphsById.Find(Node.OwnedGraphId);
                if (!OwnedGraphResult)
                {
                    bool bExistsOutsideLayer = false;
                    for (const FSekiroAnimIRLayer& CandidateLayer : CanonicalBlueprint.Layers)
                    {
                        if (CandidateLayer.Id == Layer.Id) continue;
                        for (const FSekiroAnimIRGraph& CandidateGraph : CandidateLayer.Graphs)
                        {
                            if (CandidateGraph.Id == Node.OwnedGraphId)
                            {
                                bExistsOutsideLayer = true;
                                break;
                            }
                        }
                        if (bExistsOutsideLayer) break;
                    }

                    AddError(
                        OutDiagnostics,
                        bExistsOutsideLayer ? OwnedGraphOutsideLayer : OwnedGraphNotFound,
                        bExistsOutsideLayer
                            ? FString::Printf(
                                TEXT("StateMachine Node '%s' references owned Graph '%s' outside Layer '%s'."),
                                *Node.Id,
                                *Node.OwnedGraphId,
                                *Layer.Id)
                            : FString::Printf(
                                TEXT("StateMachine Node '%s' references missing owned Graph '%s'."),
                                *Node.Id,
                                *Node.OwnedGraphId),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                if ((*OwnedGraphResult)->GraphType != SekiroAnimGraphIRNames::StateMachineGraph)
                {
                    AddError(
                        OutDiagnostics,
                        OwnedGraphTypeMismatch,
                        FString::Printf(TEXT("StateMachine Node '%s' must own a StateMachine Graph."), *Node.Id),
                        Node.Id,
                        Node.SourceLocation);
                    continue;
                }

                ++StateMachineOwnerCounts.FindOrAdd(Node.OwnedGraphId);
            }
        }

        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            if (Graph.GraphType != SekiroAnimGraphIRNames::StateMachineGraph) continue;

            const int32 OwnerCount = StateMachineOwnerCounts.FindRef(Graph.Id);
            if (OwnerCount == 0)
            {
                AddError(
                    OutDiagnostics,
                    StateMachineGraphMissingOwner,
                    FString::Printf(TEXT("StateMachine Graph '%s' has no owning StateMachine Node in Layer '%s'."), *Graph.Id, *Layer.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }
            else if (OwnerCount > 1)
            {
                AddError(
                    OutDiagnostics,
                    StateMachineGraphMultipleOwners,
                    FString::Printf(
                        TEXT("StateMachine Graph '%s' has %d owning StateMachine Nodes in Layer '%s'."),
                        *Graph.Id,
                        OwnerCount,
                        *Layer.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }
        }

        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            TMap<FString, const FSekiroAnimIRNode*> NodesById;
            for (const FSekiroAnimIRNode& Node : Graph.Nodes)
            {
                if (!Node.Id.IsEmpty() && !NodesById.Contains(Node.Id)) NodesById.Add(Node.Id, &Node);
            }

            const bool bIsPoseGraph = IsPoseGraphType(Graph.GraphType);
            if (bIsPoseGraph)
            {
                if (Graph.RootNodeId.IsEmpty())
                {
                    AddError(
                        OutDiagnostics,
                        MissingGraphRootNode,
                        FString::Printf(TEXT("Pose-bearing Graph '%s' has no RootNodeId."), *Graph.Id),
                        Graph.Id,
                        Graph.SourceLocation);
                }
                else if (!NodesById.Contains(Graph.RootNodeId))
                {
                    AddError(
                        OutDiagnostics,
                        GraphRootNodeNotFound,
                        FString::Printf(
                            TEXT("Pose-bearing Graph '%s' references missing root Node '%s'."),
                            *Graph.Id,
                            *Graph.RootNodeId),
                        Graph.Id,
                        Graph.SourceLocation);
                }
                else
                {
                    const FSekiroAnimIRNode* const* RootNodeResult = NodesById.Find(Graph.RootNodeId);
                    const FSekiroAnimIRNodeContract* ExpectedRootContract = FindRootContract(Graph.GraphType);
                    if (RootNodeResult
                        && ExpectedRootContract
                        && (*RootNodeResult)->NodeType != ExpectedRootContract->NodeType)
                    {
                        AddError(
                            OutDiagnostics,
                            RootNodeTypeMismatch,
                            FString::Printf(
                                TEXT("GraphType '%s' requires root NodeType '%s', but Node '%s' declares '%s'."),
                                *Graph.GraphType.ToString(),
                                *ExpectedRootContract->NodeType.ToString(),
                                *Graph.RootNodeId,
                                *(*RootNodeResult)->NodeType.ToString()),
                            Graph.RootNodeId,
                            (*RootNodeResult)->SourceLocation);
                    }
                }
            }

            TMap<FString, int32> InputConnectionCounts;
            for (const FSekiroAnimIRLink& Link : Graph.Links)
            {
                const FSekiroAnimIRNode* const* SourceNodeResult = NodesById.Find(Link.Source.NodeId);
                const FSekiroAnimIRNode* const* TargetNodeResult = NodesById.Find(Link.Target.NodeId);
                if (!SourceNodeResult || !TargetNodeResult)
                {
                    AddError(
                        OutDiagnostics,
                        LinkNodeNotFound,
                        FString::Printf(TEXT("Link '%s' references a Node outside Graph '%s'."), *Link.Id, *Graph.Id),
                        Link.Id,
                        Link.SourceLocation);
                    continue;
                }

                const FSekiroAnimIRNodeContract* SourceContract =
                    FSekiroAnimGraphNodeRegistry::Find((*SourceNodeResult)->NodeType);
                const FSekiroAnimIRNodeContract* TargetContract =
                    FSekiroAnimGraphNodeRegistry::Find((*TargetNodeResult)->NodeType);
                const bool bReflectiveSource =
                    !(*SourceNodeResult)->EditorNodeClass.IsNull() && SourceContract == nullptr;
                const bool bReflectiveTarget =
                    !(*TargetNodeResult)->EditorNodeClass.IsNull() && TargetContract == nullptr;
                const FSekiroAnimIRPinContract* SourcePin = SourceContract
                    ? FindPinContract(*SourceContract, Link.Source.PinName)
                    : nullptr;
                const FSekiroAnimIRPinContract* TargetPin = TargetContract
                    ? FindPinContract(*TargetContract, Link.Target.PinName)
                    : nullptr;
                const FSekiroAnimIRPin* DynamicSourcePin =
                    SourceContract != nullptr && SourceContract->bDynamicPins
                    ? FindDeclaredPin(**SourceNodeResult, Link.Source.PinName)
                    : nullptr;
                const FSekiroAnimIRPin* DynamicTargetPin =
                    TargetContract != nullptr && TargetContract->bDynamicPins
                    ? FindDeclaredPin(**TargetNodeResult, Link.Target.PinName)
                    : nullptr;
                if ((!SourcePin && !DynamicSourcePin && !bReflectiveSource)
                    || (!TargetPin && !DynamicTargetPin && !bReflectiveTarget))
                {
                    AddError(
                        OutDiagnostics,
                        LinkPinNotFound,
                        FString::Printf(TEXT("Link '%s' references a missing Pin."), *Link.Id),
                        Link.Id,
                        Link.SourceLocation);
                    continue;
                }

                if ((SourcePin && SourcePin->Direction != ESekiroAnimIRPinDirection::Output)
                    || (DynamicSourcePin
                        && DynamicSourcePin->Direction != ESekiroAnimIRPinDirection::Output)
                    || (TargetPin && TargetPin->Direction != ESekiroAnimIRPinDirection::Input)
                    || (DynamicTargetPin
                        && DynamicTargetPin->Direction != ESekiroAnimIRPinDirection::Input))
                {
                    AddError(
                        OutDiagnostics,
                        LinkDirection,
                        FString::Printf(TEXT("Link '%s' must connect Output to Input."), *Link.Id),
                        Link.Id,
                        Link.SourceLocation);
                }

                if (SourcePin && TargetPin && SourcePin->DataType != TargetPin->DataType)
                {
                    AddError(
                        OutDiagnostics,
                        LinkTypeMismatch,
                        FString::Printf(TEXT("Link '%s' connects incompatible Pin data types."), *Link.Id),
                        Link.Id,
                        Link.SourceLocation);
                }
                else
                {
                    const FName SourceDataType = SourcePin != nullptr
                        ? SourcePin->DataType
                        : DynamicSourcePin != nullptr ? DynamicSourcePin->DataType : NAME_None;
                    const FName TargetDataType = TargetPin != nullptr
                        ? TargetPin->DataType
                        : DynamicTargetPin != nullptr ? DynamicTargetPin->DataType : NAME_None;
                    if (!SourceDataType.IsNone()
                        && !TargetDataType.IsNone()
                        && SourceDataType != TargetDataType)
                    {
                        AddError(
                            OutDiagnostics,
                            LinkTypeMismatch,
                            FString::Printf(
                                TEXT("Link '%s' connects incompatible dynamic Pin data types."),
                                *Link.Id),
                            Link.Id,
                            Link.SourceLocation);
                    }
                }

                const FString InputKey = Link.Target.NodeId + TEXT("\x1f") + Link.Target.PinName;
                int32& InputCount = InputConnectionCounts.FindOrAdd(InputKey);
                ++InputCount;
                if (InputCount > 1
                    && ((TargetPin != nullptr && !TargetPin->bAllowMultipleConnections)
                        || (DynamicTargetPin != nullptr
                            && !DynamicTargetPin->bAllowMultipleConnections)))
                {
                    AddError(
                        OutDiagnostics,
                        MultipleInputLinks,
                        FString::Printf(TEXT("Input Pin '%s.%s' has multiple Links."), *Link.Target.NodeId, *Link.Target.PinName),
                        Link.Target.NodeId,
                        Link.SourceLocation);
                }
            }

            if (bIsPoseGraph && HasPoseCycle(Graph))
            {
                AddError(
                    OutDiagnostics,
                    PoseGraphCycle,
                    FString::Printf(TEXT("Pose Graph '%s' contains a cycle."), *Graph.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }

            if (Graph.GraphType == SekiroAnimGraphIRNames::StateMachineGraph)
            {
                TSet<FString> MachineStateIds;
                TSet<FString> TransitionKeys;
                TMap<FString, TSet<int32>> TransitionPrioritiesBySourceState;
                for (const FSekiroAnimIRState& State : Graph.StateMachine.States)
                {
                    if (!State.Id.IsEmpty()) MachineStateIds.Add(State.Id);

                    if (!State.GraphId.IsEmpty() && State.GraphId == Layer.RootGraphId)
                    {
                        AddError(
                            OutDiagnostics,
                            StateGraphIsLayerRoot,
                            FString::Printf(
                                TEXT("State '%s' cannot use Layer root Graph '%s' as its State Graph."),
                                *State.Id,
                                *State.GraphId),
                            State.Id,
                            State.SourceLocation);
                    }

                    const FSekiroAnimIRGraph* const* StateGraphResult = LayerGraphsById.Find(State.GraphId);
                    if (!StateGraphResult)
                    {
                        bool bExistsOutsideLayer = false;
                        for (const FSekiroAnimIRLayer& CandidateLayer : CanonicalBlueprint.Layers)
                        {
                            if (CandidateLayer.Id == Layer.Id) continue;
                            for (const FSekiroAnimIRGraph& CandidateGraph : CandidateLayer.Graphs)
                            {
                                if (CandidateGraph.Id == State.GraphId)
                                {
                                    bExistsOutsideLayer = true;
                                    break;
                                }
                            }
                            if (bExistsOutsideLayer) break;
                        }

                        AddError(
                            OutDiagnostics,
                            bExistsOutsideLayer ? StateGraphOutsideLayer : StateGraphNotFound,
                            bExistsOutsideLayer
                                ? FString::Printf(
                                    TEXT("State '%s' references Graph '%s' outside Layer '%s'."),
                                    *State.Id,
                                    *State.GraphId,
                                    *Layer.Id)
                                : FString::Printf(
                                    TEXT("State '%s' references missing Graph '%s'."),
                                    *State.Id,
                                    *State.GraphId),
                            State.Id,
                            State.SourceLocation);
                    }
                    else
                    {
                        if (!Graph.Id.IsEmpty() && !State.GraphId.IsEmpty())
                        {
                            GraphOwnershipAdjacency.FindOrAdd(Graph.Id).Add(State.GraphId);
                        }

                        if ((*StateGraphResult)->GraphType != SekiroAnimGraphIRNames::StatePoseGraph)
                        {
                            AddError(
                                OutDiagnostics,
                                StateGraphTypeMismatch,
                                FString::Printf(TEXT("State '%s' must reference a StatePose Graph."), *State.Id),
                                State.Id,
                                State.SourceLocation);
                        }
                        else
                        {
                            ++StateGraphOwnerCounts.FindOrAdd(State.GraphId);
                        }
                    }
                }

                if (Graph.StateMachine.EntryStateId.IsEmpty())
                {
                    AddError(
                        OutDiagnostics,
                        MissingEntryState,
                        FString::Printf(TEXT("StateMachine Graph '%s' has no EntryStateId."), *Graph.Id),
                        Graph.Id,
                        Graph.SourceLocation);
                }
                else if (!MachineStateIds.Contains(Graph.StateMachine.EntryStateId))
                {
                    AddError(
                        OutDiagnostics,
                        EntryStateNotFound,
                        FString::Printf(
                            TEXT("StateMachine Graph '%s' references missing Entry State '%s'."),
                            *Graph.Id,
                            *Graph.StateMachine.EntryStateId),
                        Graph.Id,
                        Graph.SourceLocation);
                }

                for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    if (!MachineStateIds.Contains(Transition.SourceStateId)
                        || !MachineStateIds.Contains(Transition.TargetStateId))
                    {
                        AddError(
                            OutDiagnostics,
                            TransitionStateNotFound,
                            FString::Printf(TEXT("Transition '%s' references a missing State."), *Transition.Id),
                            Transition.Id,
                            Transition.SourceLocation);
                    }

                    if (Transition.Key.IsEmpty())
                    {
                        AddError(
                            OutDiagnostics,
                            EmptyTransitionKey,
                            FString::Printf(TEXT("Transition '%s' has no Key."), *Transition.Id),
                            Transition.Id,
                            Transition.SourceLocation);
                    }
                    else
                    {
                        if (!IsValidLuaIdentifier(Transition.Key))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidTransitionKey,
                                FString::Printf(
                                    TEXT("Transition '%s' Key '%s' is not a valid Lua identifier."),
                                    *Transition.Id,
                                    *Transition.Key),
                                Transition.Id,
                                Transition.SourceLocation);
                        }

                        if (TransitionKeys.Contains(Transition.Key))
                        {
                            AddError(
                                OutDiagnostics,
                                DuplicateTransitionKey,
                                FString::Printf(
                                    TEXT("StateMachine Graph '%s' contains duplicate Transition Key '%s'."),
                                    *Graph.Id,
                                    *Transition.Key),
                                Transition.Id,
                                Transition.SourceLocation);
                        }
                        else
                        {
                            TransitionKeys.Add(Transition.Key);
                        }
                    }

                    const FSekiroAnimIRTransitionGate& Gate = Transition.Gate;
                    const bool bHasNativeGate = Gate.RootIndex != INDEX_NONE;
                    if (Transition.RuleFunctionName.IsNone() && !bHasNativeGate)
                    {
                        AddError(
                            OutDiagnostics,
                            EmptyTransitionRule,
                            FString::Printf(
                                TEXT("Transition '%s' has neither RuleFunctionName nor native Gate."),
                                *Transition.Id),
                            Transition.Id,
                            Transition.SourceLocation);
                    }
                    else if (!Transition.RuleFunctionName.IsNone())
                    {
                        const FString RuleFunctionName = Transition.RuleFunctionName.ToString();
                        if (!IsValidLuaIdentifier(RuleFunctionName))
                        {
                            AddError(
                                OutDiagnostics,
                                InvalidTransitionRuleFunctionName,
                                FString::Printf(
                                    TEXT("Transition '%s' RuleFunctionName '%s' is not a valid Lua identifier."),
                                    *Transition.Id,
                                    *RuleFunctionName),
                                Transition.Id,
                                Transition.SourceLocation);
                        }

                        if (TransitionRuleFunctionNames.Contains(Transition.RuleFunctionName))
                        {
                            AddError(
                                OutDiagnostics,
                                DuplicateTransitionRuleFunctionName,
                                FString::Printf(
                                    TEXT("AnimBlueprint contains duplicate Transition RuleFunctionName '%s'."),
                                    *RuleFunctionName),
                                Transition.Id,
                                Transition.SourceLocation);
                        }
                        else
                        {
                            TransitionRuleFunctionNames.Add(Transition.RuleFunctionName);
                        }
                    }

                    if (Transition.Settings.BlendDuration < 0.0f)
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTransitionBlendDuration,
                            FString::Printf(TEXT("Transition '%s' has a negative BlendDuration."), *Transition.Id),
                            Transition.Id,
                            Transition.SourceLocation);
                    }

                    if (Gate.RootIndex != INDEX_NONE)
                    {
                        if (!Gate.Nodes.IsValidIndex(Gate.RootIndex))
                        {
                            AddError(OutDiagnostics, InvalidTransitionGate,
                                TEXT("Transition Gate root index is outside Nodes."), Transition.Id, Transition.SourceLocation);
                        }
                        for (int32 GateIndex = 0; GateIndex < Gate.Nodes.Num(); ++GateIndex)
                        {
                            const FSekiroAnimIRTransitionGateNode& GateNode = Gate.Nodes[GateIndex];
                            const bool bKnownType = GateNode.Type == TEXT("LuaBool")
                                || GateNode.Type == TEXT("BoolProperty")
                                || GateNode.Type == TEXT("TimeRemainingLessEqual")
                                || GateNode.Type == TEXT("CurveGreaterEqual")
                                || GateNode.Type == TEXT("All")
                                || GateNode.Type == TEXT("Any")
                                || GateNode.Type == TEXT("Not");
                            bool bChildrenValid = true;
                            for (const int32 ChildIndex : GateNode.Children)
                            {
                                if (!Gate.Nodes.IsValidIndex(ChildIndex) || ChildIndex >= GateIndex) bChildrenValid = false;
                            }
                            const bool bArityValid = ((GateNode.Type == TEXT("LuaBool")
                                    || GateNode.Type == TEXT("BoolProperty")
                                    || GateNode.Type == TEXT("TimeRemainingLessEqual")
                                    || GateNode.Type == TEXT("CurveGreaterEqual")) && GateNode.Children.Num() == 0)
                                || ((GateNode.Type == TEXT("All") || GateNode.Type == TEXT("Any")) && GateNode.Children.Num() > 0)
                                || (GateNode.Type == TEXT("Not") && GateNode.Children.Num() == 1);
                            if (!bKnownType || !bChildrenValid || !bArityValid
                                || (GateNode.Type == TEXT("LuaBool") && Transition.RuleFunctionName.IsNone())
                                || (GateNode.Type == TEXT("BoolProperty") && GateNode.Name.IsNone())
                                || (GateNode.Type == TEXT("CurveGreaterEqual") && GateNode.Name.IsNone())
                                || (GateNode.Type == TEXT("TimeRemainingLessEqual") && GateNode.Threshold < 0.0f))
                            {
                                AddError(OutDiagnostics, InvalidTransitionGate,
                                    FString::Printf(TEXT("Transition Gate node %d is invalid."), GateIndex),
                                    Transition.Id, Transition.SourceLocation);
                            }
                        }
                    }
                    else if (!Gate.Nodes.IsEmpty())
                    {
                        AddError(
                            OutDiagnostics,
                            InvalidTransitionGate,
                            TEXT("Transition Gate without a root must not contain Nodes."),
                            Transition.Id,
                            Transition.SourceLocation);
                    }

                    if (!Transition.SourceStateId.IsEmpty())
                    {
                        TSet<int32>& SourcePriorities = TransitionPrioritiesBySourceState.FindOrAdd(Transition.SourceStateId);
                        if (SourcePriorities.Contains(Transition.Settings.PriorityOrder))
                        {
                            AddError(
                                OutDiagnostics,
                                DuplicateTransitionPriority,
                                FString::Printf(
                                    TEXT("State '%s' has more than one Transition with PriorityOrder %d."),
                                    *Transition.SourceStateId,
                                    Transition.Settings.PriorityOrder),
                                Transition.Id,
                                Transition.SourceLocation);
                        }
                        else
                        {
                            SourcePriorities.Add(Transition.Settings.PriorityOrder);
                        }
                    }
                }
            }
        }

        for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
        {
            if (Graph.GraphType != SekiroAnimGraphIRNames::StatePoseGraph) continue;

            const int32 OwnerCount = StateGraphOwnerCounts.FindRef(Graph.Id);
            if (OwnerCount == 0)
            {
                AddError(
                    OutDiagnostics,
                    StateGraphMissingOwner,
                    FString::Printf(
                        TEXT("StatePose Graph '%s' has no owning State in Layer '%s'."),
                        *Graph.Id,
                        *Layer.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }
            else if (OwnerCount > 1)
            {
                AddError(
                    OutDiagnostics,
                    StateGraphMultipleOwners,
                    FString::Printf(
                        TEXT("StatePose Graph '%s' has %d owning States in Layer '%s'."),
                        *Graph.Id,
                        OwnerCount,
                        *Layer.Id),
                    Graph.Id,
                    Graph.SourceLocation);
            }
        }

        FString CycleGraphId;
        if (HasGraphOwnershipCycle(Layer.Graphs, GraphOwnershipAdjacency, CycleGraphId))
        {
            const FSekiroAnimIRGraph* const* CycleGraphResult = LayerGraphsById.Find(CycleGraphId);
            const FSekiroAnimIRSourceLocation& CycleSourceLocation = CycleGraphResult
                ? (*CycleGraphResult)->SourceLocation
                : Layer.SourceLocation;
            AddError(
                OutDiagnostics,
                GraphOwnershipCycle,
                FString::Printf(
                    TEXT("Layer '%s' contains a Graph ownership cycle through Graph '%s'."),
                    *Layer.Id,
                    *CycleGraphId),
                CycleGraphId,
                CycleSourceLocation);
        }
    }

    return !OutDiagnostics.ContainsByPredicate([](const FSekiroAnimIRDiagnostic& Diagnostic)
    {
        return Diagnostic.Severity == ESekiroAnimIRDiagnosticSeverity::Error;
    });
}
