#include "LuaBehaviorTreeFactoryLibrary.h"

#include "AIGraph.h"
#include "AIGraphNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "LuaBehaviorTreeIRLibrary.h"
#include "LuaBehaviorTreeReflectionWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"

namespace LuaBehaviorTreeFactory
{
    const FName InvalidPackagePath(TEXT("BT.Factory.InvalidPackagePath"));
    const FName ExistingAssetTypeMismatch(TEXT("BT.Factory.ExistingAssetTypeMismatch"));
    const FName GraphConnectionFailed(TEXT("BT.Factory.GraphConnectionFailed"));
    const FName SaveFailed(TEXT("BT.Factory.SaveFailed"));
    const FName ObjectCreationFailed(TEXT("BT.Factory.ObjectCreationFailed"));
    const FName SubtreeCycle(TEXT("BT.Factory.SubtreeCycle"));
    const FName MissingAssetConfiguration(TEXT("BT.Factory.MissingAssetConfiguration"));
    const FName LuaModuleMetaKey(TEXT("LuaBehaviorTree.LuaModuleName"));
    const FName BlackboardPathMetaKey(TEXT("LuaBehaviorTree.BlackboardPackagePath"));
    const FName SourceModeMetaKey(TEXT("LuaBehaviorTree.SourceMode"));

    /**
     * 追加资产生成诊断，不记录日志。
     *
     * @param Diagnostics 诊断接收数组。
     * @param Code 稳定机器错误码。
     * @param Message 中文说明。
     * @param Path 资产路径或节点 ID。
     * @param Location Lua 源码定位。
     */
    void AddError(
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& Path,
        const FLuaBehaviorTreeSourceLocation& Location = FLuaBehaviorTreeSourceLocation())
    {
        FLuaBehaviorTreeDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Message = Message;
        Diagnostic.Path = Path;
        Diagnostic.SourceLocation = Location;
    }

    /**
     * 校验长包路径并拆出资产名。
     *
     * @param PackagePath /Game 或插件内容根下的长包路径，不带 ObjectPath 后缀。
     * @param OutAssetName 接收末段资产名。
     * @param Diagnostics 接收错误。
     * @return 路径合法且包含非空资产名时返回 true。
     */
    bool ValidatePackagePath(
        const FString& PackagePath,
        FString& OutAssetName,
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics)
    {
        OutAssetName = FPackageName::GetLongPackageAssetName(PackagePath);
        if (!FPackageName::IsValidLongPackageName(PackagePath) || OutAssetName.IsEmpty())
        {
            AddError(Diagnostics, InvalidPackagePath, TEXT("目标必须是合法 Unreal 长包路径，例如 /Game/AI/BT_Test。"), PackagePath);
            return false;
        }
        return true;
    }

    /**
     * 递归检查已加载 BehaviorTree 的 RunBehavior 依赖是否回到目标包。
     *
     * @param Tree 当前外部树。
     * @param TargetPackagePath 正在生成的 BehaviorTree 长包路径。
     * @param Visited 已检查资产集合，防止外部资产自身异常成环。
     * @return 当前树等于目标或任意子树依赖目标时返回 true。
     */
    bool ReferencesBehaviorTreePackage(
        const UBehaviorTree* Tree,
        const FString& TargetPackagePath,
        TSet<const UBehaviorTree*>& Visited)
    {
        if (!Tree) return false;
        if (Tree->GetOutermost()->GetName() == TargetPackagePath) return true;
        if (Visited.Contains(Tree)) return false;
        Visited.Add(Tree);

        TArray<const UBTCompositeNode*> PendingComposites;
        if (Tree->RootNode) PendingComposites.Add(Tree->RootNode);
        while (!PendingComposites.IsEmpty())
        {
            const UBTCompositeNode* Composite = PendingComposites.Pop(EAllowShrinking::No);
            for (const FBTCompositeChild& Child : Composite->Children)
            {
                if (Child.ChildComposite) PendingComposites.Add(Child.ChildComposite);
                const UBTTask_RunBehavior* RunBehavior = Cast<UBTTask_RunBehavior>(Child.ChildTask);
                if (RunBehavior && ReferencesBehaviorTreePackage(RunBehavior->GetSubtreeAsset(), TargetPackagePath, Visited))
                    return true;
            }
        }
        return false;
    }

    /**
     * 按 ClassPath 创建临时 UObject 并执行全部反射属性，用于资产变更前预检。
     *
     * @param IR 已通过结构校验的 IR。
     * @param BehaviorTreePackagePath 正在生成的树路径，用于 Subtree 循环检查。
     * @param Diagnostics 接收属性诊断。
     * @return 所有节点和 KeyType 均可创建且属性均可写时返回 true。
     */
    bool PreflightProperties(
        const FLuaBehaviorTreeIR& IR,
        const FString& BehaviorTreePackagePath,
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics)
    {
        UObject* TemporaryOuter = GetTransientPackage();
        bool bSuccess = true;
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            UClass* NodeClass = Node.ClassPath.TryLoadClass<UBTNode>();
            UObject* Instance = NodeClass ? NewObject<UObject>(TemporaryOuter, NodeClass) : nullptr;
            if (!Instance || !FLuaBehaviorTreeReflectionWriter::ApplyProperties(Instance, Node.Properties, IR.Values, Node.SourceLocation, Diagnostics))
                bSuccess = false;
            const UBTTask_RunBehavior* RunBehavior = Cast<UBTTask_RunBehavior>(Instance);
            if (RunBehavior)
            {
                TSet<const UBehaviorTree*> Visited;
                if (ReferencesBehaviorTreePackage(RunBehavior->GetSubtreeAsset(), BehaviorTreePackagePath, Visited))
                {
                    AddError(Diagnostics, SubtreeCycle, TEXT("RunBehavior 的外部 Subtree 依赖会回到当前目标行为树。"), Node.Id, Node.SourceLocation);
                    bSuccess = false;
                }
            }
        }
        for (const FLuaBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            UClass* NodeClass = Decorator.ClassPath.TryLoadClass<UBTDecorator>();
            UObject* Instance = NodeClass ? NewObject<UObject>(TemporaryOuter, NodeClass) : nullptr;
            if (!Instance || !FLuaBehaviorTreeReflectionWriter::ApplyProperties(Instance, Decorator.Properties, IR.Values, Decorator.SourceLocation, Diagnostics))
                bSuccess = false;
        }
        for (const FLuaBehaviorTreeIRNode& Service : IR.Services)
        {
            UClass* NodeClass = Service.ClassPath.TryLoadClass<UBTService>();
            UObject* Instance = NodeClass ? NewObject<UObject>(TemporaryOuter, NodeClass) : nullptr;
            if (!Instance || !FLuaBehaviorTreeReflectionWriter::ApplyProperties(Instance, Service.Properties, IR.Values, Service.SourceLocation, Diagnostics))
                bSuccess = false;
        }
        for (const FLuaBlackboardIRKey& Key : IR.BlackboardKeys)
        {
            UClass* KeyClass = Key.ClassPath.TryLoadClass<UBlackboardKeyType>();
            UObject* Instance = KeyClass ? NewObject<UObject>(TemporaryOuter, KeyClass) : nullptr;
            if (!Instance || !FLuaBehaviorTreeReflectionWriter::ApplyProperties(Instance, Key.Properties, IR.Values, Key.SourceLocation, Diagnostics))
                bSuccess = false;
        }
        return bSuccess;
    }

    /**
     * 获取同路径同类型资产，或在包内新建资产并登记 AssetRegistry。
     *
     * @param PackagePath 目标长包路径。
     * @param AssetClass 期望 UObject 类。
     * @param Diagnostics 接收类型冲突或创建失败诊断。
     * @param bOutCreated 成功时表示是否新建。
     * @return 可编辑目标资产；失败返回 nullptr。
     */
    UObject* FindOrCreateAsset(
        const FString& PackagePath,
        UClass* AssetClass,
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics,
        bool& bOutCreated)
    {
        bOutCreated = false;
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
        UPackage* Package = CreatePackage(*PackagePath);
        if (!Package) return nullptr;

        UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName);
        if (!Existing)
            Existing = LoadObject<UObject>(nullptr, *(PackagePath + TEXT(".") + AssetName));
        if (Existing)
        {
            if (!Existing->IsA(AssetClass))
            {
                AddError(Diagnostics, ExistingAssetTypeMismatch, TEXT("目标路径已存在其他类型资产。"), PackagePath);
                return nullptr;
            }
            Existing->Modify();
            return Existing;
        }

        UObject* Asset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        if (!Asset)
        {
            AddError(Diagnostics, ObjectCreationFailed, TEXT("无法创建目标资产。"), PackagePath);
            return nullptr;
        }
        FAssetRegistryModule::AssetCreated(Asset);
        bOutCreated = true;
        return Asset;
    }

    /**
     * 用反射 KeyType 类和属性重建 Blackboard Keys。
     *
     * @param IR 已校验 IR。
     * @param Blackboard 目标 Blackboard 资产。
     * @param Diagnostics 接收创建或属性错误。
     * @return 所有 Key 创建及属性写入成功时返回 true。
     */
    bool BuildBlackboard(
        const FLuaBehaviorTreeIR& IR,
        UBlackboardData* Blackboard,
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics)
    {
        Blackboard->Keys.Reset();
        Blackboard->Parent = IR.ParentBlackboard.IsNull()
            ? nullptr
            : Cast<UBlackboardData>(IR.ParentBlackboard.TryLoad());

        TArray<const FLuaBlackboardIRKey*> SortedKeys;
        for (const FLuaBlackboardIRKey& Key : IR.BlackboardKeys)
            SortedKeys.Add(&Key);
        SortedKeys.Sort([](const FLuaBlackboardIRKey& Left, const FLuaBlackboardIRKey& Right)
        {
            if (Left.DeclarationOrder != Right.DeclarationOrder) return Left.DeclarationOrder < Right.DeclarationOrder;
            return Left.Id < Right.Id;
        });

        for (const FLuaBlackboardIRKey* Key : SortedKeys)
        {
            UClass* KeyClass = Key->ClassPath.TryLoadClass<UBlackboardKeyType>();
            UBlackboardKeyType* KeyType = KeyClass ? NewObject<UBlackboardKeyType>(Blackboard, KeyClass, NAME_None, RF_Transactional) : nullptr;
            if (!KeyType)
            {
                AddError(Diagnostics, ObjectCreationFailed, TEXT("无法创建 Blackboard KeyType。"), Key->Id, Key->SourceLocation);
                return false;
            }
            if (!FLuaBehaviorTreeReflectionWriter::ApplyProperties(KeyType, Key->Properties, IR.Values, Key->SourceLocation, Diagnostics))
                return false;
            FBlackboardEntry& Entry = Blackboard->Keys.AddDefaulted_GetRef();
            Entry.EntryName = Key->Name;
            Entry.KeyType = KeyType;
            Entry.bInstanceSynced = Key->bInstanceSynced;
        }
        Blackboard->UpdateParentKeys();
        Blackboard->MarkPackageDirty();
        return true;
    }

    /**
     * 为一个运行时 BT 类选择 UE 编辑器 Graph shell。
     * 选择只依据基类类别；任意派生运行时类仍直接放入 NodeInstance，无注册表。
     *
     * @param Graph 节点所属 BehaviorTreeGraph。
     * @param NodeClass 已加载的 UBTCompositeNode 或 UBTTaskNode 子类。
     * @return 新建但尚未加入 Graph 的编辑器节点；类别无效时返回 nullptr。
     */
    UAIGraphNode* CreateMainGraphNode(UEdGraph* Graph, UClass* NodeClass)
    {
        const TCHAR* ShellClassPath = nullptr;
        if (NodeClass->IsChildOf(UBTComposite_SimpleParallel::StaticClass()))
            ShellClassPath = TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_SimpleParallel");
        else if (NodeClass->IsChildOf(UBTCompositeNode::StaticClass()))
            ShellClassPath = TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Composite");
        else if (NodeClass->IsChildOf(UBTTask_RunBehavior::StaticClass()))
            ShellClassPath = TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_SubtreeTask");
        else if (NodeClass->IsChildOf(UBTTaskNode::StaticClass()))
            ShellClassPath = TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Task");
        if (!ShellClassPath) return nullptr;
        UClass* ShellClass = LoadObject<UClass>(nullptr, ShellClassPath);
        return ShellClass ? NewObject<UAIGraphNode>(Graph, ShellClass) : nullptr;
    }

    /**
     * 将编辑器节点加入 Graph 并分配默认 Pin。
     *
     * @param Graph 目标 Graph。
     * @param GraphNode 新建编辑器节点。
     */
    void AddGraphNode(UEdGraph* Graph, UAIGraphNode* GraphNode)
    {
        Graph->AddNode(GraphNode, false, false);
        GraphNode->CreateNewGuid();
        GraphNode->PostPlacedNewNode();
        GraphNode->AllocateDefaultPins();
    }

    /**
     * 连接父输出 Pin 到子输入 Pin。
     *
     * @param Schema BehaviorTree Schema 默认对象。
     * @param Parent 父 Composite 或虚拟 Root。
     * @param Child 子主节点。
     * @return 找到对应方向 Pin 且 Schema 接受连接时返回 true。
     */
    bool ConnectMainNodes(
        const UEdGraphSchema* Schema,
        UAIGraphNode* Parent,
        UAIGraphNode* Child)
    {
        UEdGraphPin* ParentOutput = nullptr;
        UEdGraphPin* ChildInput = nullptr;
        for (UEdGraphPin* Pin : Parent->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                ParentOutput = Pin;
                break;
            }
        }
        for (UEdGraphPin* Pin : Child->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input)
            {
                ChildInput = Pin;
                break;
            }
        }
        return ParentOutput && ChildInput && Schema->TryCreateConnection(ParentOutput, ChildInput);
    }

    struct FLuaBehaviorTreeLayoutNode
    {
        const FLuaBehaviorTreeIRNode* Source = nullptr; // 对应的只读 IR 主节点
        TArray<FString> ChildIds;             // 按声明顺序排列的直接子节点 ID
        int32 AttachedNodeCount = 0;          // 挂载的 Decorator 与 Service 总数
        int32 Depth = 0;                      // IR 根为零的主节点深度
        double VisualWidth = 0.0;             // 主节点及附属节点估算视觉宽度
        double VisualHeight = 0.0;            // 主节点及附属节点估算视觉高度
        double ChildrenWidth = 0.0;           // 直接子树轮廓与间距的总宽度
        double SubtreeWidth = 0.0;            // 当前完整子树轮廓宽度
        double CenterX = 0.0;                 // Graph 坐标系中的节点水平中心
    };

    /**
     * 递归计算一个 IR 子树的确定性视觉轮廓和深度。
     * 本函数只读取已经建立的父子表，不访问 UObject；调用方必须先保证拓扑无环且父节点存在。
     * Decorator 与 Service 只按数量扩大通用视觉占用，不检查任何具体节点类型。
     *
     * @param NodeId 当前子树根的稳定 IR ID。
     * @param Depth 当前主节点深度，IR 根必须传入零。
     * @param LayoutNodes 输入并原地更新的布局节点表；函数不保留引用。
     * @return 当前子树轮廓宽度；节点缺失时返回负值。
     */
    double ComputeLayoutMetrics(
        const FString& NodeId,
        const int32 Depth,
        TMap<FString, FLuaBehaviorTreeLayoutNode>& LayoutNodes)
    {
        constexpr double BaseNodeWidth = 240.0;
        constexpr double BaseNodeHeight = 120.0;
        constexpr double AttachedWidthIncrement = 24.0;
        constexpr double AttachedRowHeight = 42.0;
        constexpr double SiblingSubtreeGap = 120.0;

        FLuaBehaviorTreeLayoutNode* LayoutNode = LayoutNodes.Find(NodeId);
        if (!LayoutNode) return -1.0;

        LayoutNode->Depth = Depth;
        LayoutNode->VisualWidth = BaseNodeWidth
            + LayoutNode->AttachedNodeCount * AttachedWidthIncrement;
        LayoutNode->VisualHeight = BaseNodeHeight
            + LayoutNode->AttachedNodeCount * AttachedRowHeight;
        LayoutNode->ChildrenWidth = 0.0;
        for (int32 ChildIndex = 0; ChildIndex < LayoutNode->ChildIds.Num(); ++ChildIndex)
        {
            const double ChildWidth = ComputeLayoutMetrics(
                LayoutNode->ChildIds[ChildIndex],
                Depth + 1,
                LayoutNodes);
            if (ChildWidth < 0.0) return -1.0;
            if (ChildIndex > 0) LayoutNode->ChildrenWidth += SiblingSubtreeGap;
            LayoutNode->ChildrenWidth += ChildWidth;
        }
        LayoutNode->SubtreeWidth = FMath::Max(
            LayoutNode->VisualWidth,
            LayoutNode->ChildrenWidth);
        return LayoutNode->SubtreeWidth;
    }

    /**
     * 在已计算轮廓宽度的子树内递归分配水平中心坐标。
     * 直接子树整体居中放入父轮廓，父节点位于该水平范围中心；兄弟轮廓之间保留固定空隙。
     * 本函数只修改临时布局数据，可从任意线程调用。
     *
     * @param NodeId 当前子树根的稳定 IR ID。
     * @param SubtreeLeft 当前子树轮廓左边界的 Graph X 坐标。
     * @param LayoutNodes 输入并原地更新的布局节点表。
     * @return 当前节点及全部后代完成分配时返回 true；节点缺失时返回 false。
     */
    bool AssignLayoutHorizontalPositions(
        const FString& NodeId,
        const double SubtreeLeft,
        TMap<FString, FLuaBehaviorTreeLayoutNode>& LayoutNodes)
    {
        constexpr double SiblingSubtreeGap = 120.0;

        FLuaBehaviorTreeLayoutNode* LayoutNode = LayoutNodes.Find(NodeId);
        if (!LayoutNode) return false;
        LayoutNode->CenterX = SubtreeLeft + LayoutNode->SubtreeWidth * 0.5;

        double ChildLeft = SubtreeLeft
            + (LayoutNode->SubtreeWidth - LayoutNode->ChildrenWidth) * 0.5;
        for (const FString& ChildId : LayoutNode->ChildIds)
        {
            FLuaBehaviorTreeLayoutNode* ChildLayout = LayoutNodes.Find(ChildId);
            if (!ChildLayout
                || !AssignLayoutHorizontalPositions(
                    ChildId,
                    ChildLeft,
                    LayoutNodes))
            {
                return false;
            }
            ChildLeft += ChildLayout->SubtreeWidth + SiblingSubtreeGap;
        }
        return true;
    }

    /**
     * 根据 IR 父子拓扑、声明顺序和附属节点数量生成确定性的 Graph 坐标。
     * 主节点按深度分层，父节点居中于直接子树轮廓，叶节点不按运行时类型特殊处理。
     * 本函数是纯 C++ 布局步骤，不创建或修改 UObject，可从任意线程调用。
     *
     * @param IR 已通过结构校验的行为树 IR。
     * @param OutNodePositions 接收每个主节点左上角 Graph 坐标；函数开始时重置。
     * @param OutVirtualRootPosition 接收编辑器虚拟 Root 左上角 Graph 坐标。
     * @return 拓扑完整且全部节点完成布局时返回 true，否则清空输出并返回 false。
     */
    bool BuildDeterministicLayout(
        const FLuaBehaviorTreeIR& IR,
        TMap<FString, FIntPoint>& OutNodePositions,
        FIntPoint& OutVirtualRootPosition)
    {
        constexpr double FirstMainLayerY = 220.0;
        constexpr double LayerGap = 140.0;
        constexpr double VirtualRootWidth = 200.0;

        OutNodePositions.Reset();
        OutVirtualRootPosition = FIntPoint::ZeroValue;
        TMap<FString, FLuaBehaviorTreeLayoutNode> LayoutNodes;
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            FLuaBehaviorTreeLayoutNode& LayoutNode = LayoutNodes.Add(Node.Id);
            LayoutNode.Source = &Node;
        }
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            if (Node.Id == IR.RootNodeId) continue;
            FLuaBehaviorTreeLayoutNode* ParentLayout = LayoutNodes.Find(Node.ParentId);
            if (!ParentLayout)
            {
                OutNodePositions.Reset();
                return false;
            }
            ParentLayout->ChildIds.Add(Node.Id);
        }
        for (const FLuaBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            FLuaBehaviorTreeLayoutNode* ParentLayout =
                LayoutNodes.Find(Decorator.ParentId);
            if (ParentLayout) ++ParentLayout->AttachedNodeCount;
        }
        for (const FLuaBehaviorTreeIRNode& Service : IR.Services)
        {
            FLuaBehaviorTreeLayoutNode* ParentLayout =
                LayoutNodes.Find(Service.ParentId);
            if (ParentLayout) ++ParentLayout->AttachedNodeCount;
        }
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            FLuaBehaviorTreeLayoutNode* LayoutNode = LayoutNodes.Find(Node.Id);
            if (!LayoutNode) continue;
            LayoutNode->ChildIds.Sort(
                [&LayoutNodes](const FString& LeftId, const FString& RightId)
                {
                    const FLuaBehaviorTreeLayoutNode* Left =
                        LayoutNodes.Find(LeftId);
                    const FLuaBehaviorTreeLayoutNode* Right =
                        LayoutNodes.Find(RightId);
                    if (!Left || !Left->Source) return false;
                    if (!Right || !Right->Source) return true;
                    if (Left->Source->DeclarationOrder
                        != Right->Source->DeclarationOrder)
                    {
                        return Left->Source->DeclarationOrder
                            < Right->Source->DeclarationOrder;
                    }
                    return LeftId < RightId;
                });
        }

        const double RootSubtreeWidth = ComputeLayoutMetrics(
            IR.RootNodeId,
            0,
            LayoutNodes);
        if (RootSubtreeWidth < 0.0
            || !AssignLayoutHorizontalPositions(
                IR.RootNodeId,
                RootSubtreeWidth * -0.5,
                LayoutNodes))
        {
            OutNodePositions.Reset();
            return false;
        }

        int32 MaximumDepth = 0;
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            const FLuaBehaviorTreeLayoutNode* LayoutNode =
                LayoutNodes.Find(Node.Id);
            if (LayoutNode) MaximumDepth = FMath::Max(MaximumDepth, LayoutNode->Depth);
        }
        TArray<double> LayerHeights;
        LayerHeights.Init(0.0, MaximumDepth + 1);
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            const FLuaBehaviorTreeLayoutNode* LayoutNode =
                LayoutNodes.Find(Node.Id);
            if (!LayoutNode) continue;
            LayerHeights[LayoutNode->Depth] = FMath::Max(
                LayerHeights[LayoutNode->Depth],
                LayoutNode->VisualHeight);
        }
        TArray<double> LayerY;
        LayerY.Init(FirstMainLayerY, MaximumDepth + 1);
        for (int32 Depth = 1; Depth <= MaximumDepth; ++Depth)
        {
            LayerY[Depth] = LayerY[Depth - 1]
                + LayerHeights[Depth - 1]
                + LayerGap;
        }

        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            const FLuaBehaviorTreeLayoutNode* LayoutNode =
                LayoutNodes.Find(Node.Id);
            if (!LayoutNode)
            {
                OutNodePositions.Reset();
                return false;
            }
            OutNodePositions.Add(
                Node.Id,
                FIntPoint(
                    FMath::RoundToInt(
                        LayoutNode->CenterX - LayoutNode->VisualWidth * 0.5),
                    FMath::RoundToInt(LayerY[LayoutNode->Depth])));
        }
        const FLuaBehaviorTreeLayoutNode* RootLayout =
            LayoutNodes.Find(IR.RootNodeId);
        if (!RootLayout)
        {
            OutNodePositions.Reset();
            return false;
        }
        OutVirtualRootPosition = FIntPoint(
            FMath::RoundToInt(RootLayout->CenterX - VirtualRootWidth * 0.5),
            0);
        return true;
    }

    /**
     * 从 IR 重建可在 BehaviorTreeEditor 打开的 Graph，并由 UBehaviorTreeGraph::UpdateAsset 生成运行时拓扑。
     *
     * @param IR 已校验 IR。
     * @param BehaviorTree 目标资产。
     * @param Blackboard 已生成的目标 Blackboard。
     * @param Diagnostics 接收节点创建、反射写入或连接错误。
     * @return Graph 和运行时树全部生成成功时返回 true。
     */
    bool BuildBehaviorTree(
        const FLuaBehaviorTreeIR& IR,
        UBehaviorTree* BehaviorTree,
        UBlackboardData* Blackboard,
        TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics)
    {
        BehaviorTree->BlackboardAsset = Blackboard;
        UClass* GraphClass = LoadObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraph"));
        UClass* SchemaClass = LoadObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.EdGraphSchema_BehaviorTree"));
        if (!GraphClass || !SchemaClass)
        {
            AddError(Diagnostics, ObjectCreationFailed, TEXT("BehaviorTreeEditor Graph 类型不可用。"), BehaviorTree->GetPathName());
            return false;
        }
        UEdGraph* Graph = BehaviorTree->BTGraph;
        if (Graph && !Graph->IsA(GraphClass)) Graph = nullptr;
        if (!Graph)
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(
                BehaviorTree,
                TEXT("BehaviorTreeGraph"),
                GraphClass,
                SchemaClass);
        }
        if (!Graph)
        {
            AddError(Diagnostics, ObjectCreationFailed, TEXT("无法创建 BehaviorTreeGraph。"), BehaviorTree->GetPathName());
            return false;
        }
        BehaviorTree->BTGraph = Graph;
        Graph->Modify();
        const TArray<TObjectPtr<UEdGraphNode>> ExistingNodes = Graph->Nodes;
        for (UEdGraphNode* ExistingNode : ExistingNodes)
        {
            if (ExistingNode) Graph->RemoveNode(ExistingNode);
        }
        const UEdGraphSchema* Schema = Graph->GetSchema();
        Schema->CreateDefaultNodesForGraph(*Graph);

        UAIGraphNode* RootGraphNode = nullptr;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            if (GraphNode && GraphNode->GetClass()->GetPathName() == TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Root"))
            {
                RootGraphNode = Cast<UAIGraphNode>(GraphNode);
                break;
            }
        }
        if (!RootGraphNode) return false;
        FObjectPropertyBase* BlackboardProperty = FindFProperty<FObjectPropertyBase>(RootGraphNode->GetClass(), TEXT("BlackboardAsset"));
        if (BlackboardProperty)
            BlackboardProperty->SetObjectPropertyValue_InContainer(RootGraphNode, Blackboard);

        TMap<FString, FIntPoint> NodePositions;
        FIntPoint VirtualRootPosition;
        if (!BuildDeterministicLayout(IR, NodePositions, VirtualRootPosition))
        {
            AddError(
                Diagnostics,
                ObjectCreationFailed,
                TEXT("无法为行为树生成确定性布局。"),
                IR.RootNodeId);
            return false;
        }
        RootGraphNode->NodePosX = VirtualRootPosition.X;
        RootGraphNode->NodePosY = VirtualRootPosition.Y;

        TArray<const FLuaBehaviorTreeIRNode*> SortedNodes;
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
            SortedNodes.Add(&Node);
        SortedNodes.Sort([](const FLuaBehaviorTreeIRNode& Left, const FLuaBehaviorTreeIRNode& Right)
        {
            if (Left.DeclarationOrder != Right.DeclarationOrder) return Left.DeclarationOrder < Right.DeclarationOrder;
            return Left.Id < Right.Id;
        });

        TMap<FString, UAIGraphNode*> GraphNodesById;
        for (const FLuaBehaviorTreeIRNode* Node : SortedNodes)
        {
            UClass* NodeClass = Node->ClassPath.TryLoadClass<UBTNode>();
            UAIGraphNode* GraphNode = NodeClass ? CreateMainGraphNode(Graph, NodeClass) : nullptr;
            UBTNode* NodeInstance = NodeClass ? NewObject<UBTNode>(BehaviorTree, NodeClass, NAME_None, RF_Transactional) : nullptr;
            if (!GraphNode || !NodeInstance)
            {
                AddError(Diagnostics, ObjectCreationFailed, TEXT("无法创建行为树节点。"), Node->Id, Node->SourceLocation);
                return false;
            }
            if (!FLuaBehaviorTreeReflectionWriter::ApplyProperties(NodeInstance, Node->Properties, IR.Values, Node->SourceLocation, Diagnostics))
                return false;
            GraphNode->NodeInstance = NodeInstance;
            const FIntPoint* NodePosition = NodePositions.Find(Node->Id);
            if (!NodePosition) return false;
            GraphNode->NodePosX = NodePosition->X;
            GraphNode->NodePosY = NodePosition->Y;
            AddGraphNode(Graph, GraphNode);
            GraphNodesById.Add(Node->Id, GraphNode);
        }

        for (const FLuaBehaviorTreeIRNode& Decorator : IR.Decorators)
        {
            UAIGraphNode* const* Parent = GraphNodesById.Find(Decorator.ParentId);
            UClass* NodeClass = Decorator.ClassPath.TryLoadClass<UBTDecorator>();
            UClass* ShellClass = LoadObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Decorator"));
            UAIGraphNode* GraphNode = ShellClass ? NewObject<UAIGraphNode>(Graph, ShellClass) : nullptr;
            UBTDecorator* NodeInstance = NodeClass ? NewObject<UBTDecorator>(BehaviorTree, NodeClass, NAME_None, RF_Transactional) : nullptr;
            if (!Parent || !GraphNode || !NodeInstance) return false;
            if (!FLuaBehaviorTreeReflectionWriter::ApplyProperties(NodeInstance, Decorator.Properties, IR.Values, Decorator.SourceLocation, Diagnostics))
                return false;
            GraphNode->NodeInstance = NodeInstance;
            (*Parent)->AddSubNode(GraphNode, Graph);
        }

        for (const FLuaBehaviorTreeIRNode& Service : IR.Services)
        {
            UAIGraphNode* const* Parent = GraphNodesById.Find(Service.ParentId);
            UClass* NodeClass = Service.ClassPath.TryLoadClass<UBTService>();
            UClass* ShellClass = LoadObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeGraphNode_Service"));
            UAIGraphNode* GraphNode = ShellClass ? NewObject<UAIGraphNode>(Graph, ShellClass) : nullptr;
            UBTService* NodeInstance = NodeClass ? NewObject<UBTService>(BehaviorTree, NodeClass, NAME_None, RF_Transactional) : nullptr;
            if (!Parent || !GraphNode || !NodeInstance) return false;
            if (!FLuaBehaviorTreeReflectionWriter::ApplyProperties(NodeInstance, Service.Properties, IR.Values, Service.SourceLocation, Diagnostics))
                return false;
            GraphNode->NodeInstance = NodeInstance;
            (*Parent)->AddSubNode(GraphNode, Graph);
        }

        UAIGraphNode* const* IRRootNode = GraphNodesById.Find(IR.RootNodeId);
        if (!IRRootNode || !ConnectMainNodes(Schema, RootGraphNode, *IRRootNode))
        {
            AddError(Diagnostics, GraphConnectionFailed, TEXT("无法连接虚拟 Root 与 IR 根 Composite。"), IR.RootNodeId);
            return false;
        }
        for (const FLuaBehaviorTreeIRNode& Node : IR.Nodes)
        {
            if (Node.Id == IR.RootNodeId) continue;
            UAIGraphNode* const* Parent = GraphNodesById.Find(Node.ParentId);
            UAIGraphNode* const* Child = GraphNodesById.Find(Node.Id);
            if (!Parent || !Child || !ConnectMainNodes(Schema, *Parent, *Child))
            {
                AddError(Diagnostics, GraphConnectionFailed, TEXT("行为树父子连接失败。"), Node.Id, Node.SourceLocation);
                return false;
            }
        }

        UAIGraph* AIGraph = Cast<UAIGraph>(Graph);
        if (!AIGraph) return false;
        AIGraph->UpdateAsset();
        BehaviorTree->MarkPackageDirty();
        return BehaviorTree->RootNode != nullptr;
    }

    /**
     * 将指定资产保存到其长包路径对应的 .uasset。
     *
     * @param Asset 待保存资产。
     * @param Diagnostics 接收保存错误。
     * @return SavePackage 成功时返回 true。
     */
    bool SaveAsset(UObject* Asset, TArray<FLuaBehaviorTreeDiagnostic>& Diagnostics)
    {
        UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
        if (!Package) return false;
        const FString Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(),
            FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
        {
            AddError(Diagnostics, SaveFailed, TEXT("资产保存失败。"), Package->GetName());
            return false;
        }
        return true;
    }
}

/**
 * 仅导入并校验 Lua 行为树，不创建或修改资产。
 *
 * @param LuaModuleName 交给 require 的 Lua 模块名。
 * @param OutDiagnostics 接收导入、结构与类继承诊断。
 * @return 模块可完整编译为合法 IR 时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::CheckLuaBehaviorTree(
    const FString& LuaModuleName,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    FLuaBehaviorTreeIR IR;
    return ULuaBehaviorTreeIRLibrary::CompileLuaModule(LuaModuleName, IR, OutDiagnostics);
}

/**
 * 从 Lua 模块完成严格导入、预检和原生 Blackboard/BehaviorTree 生成。
 * 只能在编辑器游戏线程调用；所有反射属性在触碰目标资产前先用临时对象预检。
 *
 * @param LuaModuleName 交给 require 的 Lua 模块名。
 * @param BlackboardPackagePath Blackboard 长包路径。
 * @param BehaviorTreePackagePath BehaviorTree 长包路径。
 * @param bSaveAssets 为 true 时立即写入两个 .uasset。
 * @param OutBlackboard 成功时接收生成或更新的 Blackboard。
 * @param OutBehaviorTree 成功时接收生成或更新的 BehaviorTree。
 * @param OutDiagnostics 接收全部结构化中文诊断。
 * @return 两个资产生成且按请求保存成功时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::GenerateFromLua(
    const FString& LuaModuleName,
    const FString& BlackboardPackagePath,
    const FString& BehaviorTreePackagePath,
    const bool bSaveAssets,
    UBlackboardData*& OutBlackboard,
    UBehaviorTree*& OutBehaviorTree,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    FLuaBehaviorTreeIR IR;
    if (!ULuaBehaviorTreeIRLibrary::CompileLuaModuleFresh(
            LuaModuleName,
            IR,
            OutDiagnostics))
    {
        return false;
    }
    return GenerateFromIR(
        IR,
        BlackboardPackagePath,
        BehaviorTreePackagePath,
        bSaveAssets,
        OutBlackboard,
        OutBehaviorTree,
        OutDiagnostics);
}

/**
 * 从已构造 IR 生成原生资产，供自动化测试、其他脚本前端和 Lua 入口复用。
 * 先完成结构校验与临时实例属性预检，再更新目标包；不依赖项目 Source 或固定资产路径。
 *
 * @param IR 强类型通用行为树 IR。
 * @param BlackboardPackagePath Blackboard 长包路径。
 * @param BehaviorTreePackagePath BehaviorTree 长包路径。
 * @param bSaveAssets 为 true 时立即写入磁盘。
 * @param OutBlackboard 成功时接收目标 Blackboard。
 * @param OutBehaviorTree 成功时接收目标 BehaviorTree。
 * @param OutDiagnostics 接收所有错误。
 * @return 生成和可选保存全部成功时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::GenerateFromIR(
    const FLuaBehaviorTreeIR& IR,
    const FString& BlackboardPackagePath,
    const FString& BehaviorTreePackagePath,
    const bool bSaveAssets,
    UBlackboardData*& OutBlackboard,
    UBehaviorTree*& OutBehaviorTree,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace LuaBehaviorTreeFactory;

    OutBlackboard = nullptr;
    OutBehaviorTree = nullptr;
    OutDiagnostics.Reset();
    FString BlackboardAssetName;
    FString BehaviorTreeAssetName;
    if (!IsInGameThread()
        || !ValidatePackagePath(BlackboardPackagePath, BlackboardAssetName, OutDiagnostics)
        || !ValidatePackagePath(BehaviorTreePackagePath, BehaviorTreeAssetName, OutDiagnostics)
        || !ULuaBehaviorTreeIRLibrary::Validate(IR, OutDiagnostics)
        || !PreflightProperties(IR, BehaviorTreePackagePath, OutDiagnostics))
    {
        return false;
    }

    bool bBlackboardCreated = false;
    bool bBehaviorTreeCreated = false;
    OutBlackboard = Cast<UBlackboardData>(FindOrCreateAsset(
        BlackboardPackagePath,
        UBlackboardData::StaticClass(),
        OutDiagnostics,
        bBlackboardCreated));
    OutBehaviorTree = Cast<UBehaviorTree>(FindOrCreateAsset(
        BehaviorTreePackagePath,
        UBehaviorTree::StaticClass(),
        OutDiagnostics,
        bBehaviorTreeCreated));
    if (!OutBlackboard || !OutBehaviorTree) return false;

    if (!BuildBlackboard(IR, OutBlackboard, OutDiagnostics)
        || !BuildBehaviorTree(IR, OutBehaviorTree, OutBlackboard, OutDiagnostics))
    {
        return false;
    }
    if (bSaveAssets
        && (!SaveAsset(OutBlackboard, OutDiagnostics) || !SaveAsset(OutBehaviorTree, OutDiagnostics)))
    {
        return false;
    }
    return true;
}

/**
 * 从 UBehaviorTree 所属包的 UMetaData 读取每资产 Lua 模块与 Blackboard 路径配置。
 * 只能在游戏线程调用；缺少元数据字段不是错误，输出对应空字符串。
 *
 * @param BehaviorTree 待读取资产，不能为空。
 * @param OutConfiguration 接收配置值；函数开始时重置。
 * @return 资产及其 Package/MetaData 有效时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
    UBehaviorTree* BehaviorTree,
    FLuaBehaviorTreeAssetConfiguration& OutConfiguration)
{
    using namespace LuaBehaviorTreeFactory;

    OutConfiguration = FLuaBehaviorTreeAssetConfiguration();
    if (!IsInGameThread() || !BehaviorTree) return false;
    UPackage* Package = BehaviorTree->GetOutermost();
#if UE_VERSION_NEWER_THAN(5, 7, 0)
    FMetaData* MetaData = Package ? &Package->GetMetaData() : nullptr;
#else
    UMetaData* MetaData = Package ? Package->GetMetaData() : nullptr;
#endif
    if (!MetaData) return false;

    OutConfiguration.LuaModuleName =
        MetaData->GetValue(BehaviorTree, LuaModuleMetaKey);
    OutConfiguration.BlackboardPackagePath =
        MetaData->GetValue(BehaviorTree, BlackboardPathMetaKey);
    const FString SourceModeValue =
        MetaData->GetValue(BehaviorTree, SourceModeMetaKey);
    OutConfiguration.SourceMode = SourceModeValue.Equals(
        TEXT("Lua"),
        ESearchCase::IgnoreCase)
        ? ELuaBehaviorTreeSourceMode::Lua
        : ELuaBehaviorTreeSourceMode::BehaviorTree;
    return true;
}

/**
 * 把每资产 Lua 配置写入 BehaviorTree Package 的 UMetaData，并标记包 Dirty。
 * 只能在编辑器游戏线程调用；函数不检查 Lua 模块是否存在，也不创建 Blackboard。
 *
 * @param BehaviorTree 目标资产，不能为空且必须属于 Package。
 * @param Configuration 要持久化的模块名和可选 Blackboard 长包路径。
 * @return 元数据写入成功时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
    UBehaviorTree* BehaviorTree,
    const FLuaBehaviorTreeAssetConfiguration& Configuration)
{
    using namespace LuaBehaviorTreeFactory;

    if (!IsInGameThread() || !BehaviorTree) return false;
    UPackage* Package = BehaviorTree->GetOutermost();
#if UE_VERSION_NEWER_THAN(5, 7, 0)
    FMetaData* MetaData = Package ? &Package->GetMetaData() : nullptr;
#else
    UMetaData* MetaData = Package ? Package->GetMetaData() : nullptr;
#endif
    if (!MetaData) return false;

    BehaviorTree->Modify();
    MetaData->SetValue(
        BehaviorTree,
        LuaModuleMetaKey,
        *Configuration.LuaModuleName);
    MetaData->SetValue(
        BehaviorTree,
        BlackboardPathMetaKey,
        *Configuration.BlackboardPackagePath);
    MetaData->SetValue(
        BehaviorTree,
        SourceModeMetaKey,
        Configuration.SourceMode == ELuaBehaviorTreeSourceMode::Lua
            ? TEXT("Lua")
            : TEXT("BehaviorTree"));
    Package->MarkPackageDirty();
    return true;
}

/**
 * 按同目录命名约定从 BehaviorTree 长包路径推导 Blackboard 长包路径。
 * 资产名以 BT_ 开头时替换为 BB_；否则在原资产名前添加 BB_。
 * 本函数只处理字符串，不访问文件系统或 AssetRegistry，可从任意线程调用。
 *
 * @param BehaviorTreePackagePath BehaviorTree 长包路径。
 * @return 推导后的长包路径；输入不是合法长包路径时返回空字符串。
 */
FString ULuaBehaviorTreeFactoryLibrary::DeriveBlackboardPackagePath(
    const FString& BehaviorTreePackagePath)
{
    if (!FPackageName::IsValidLongPackageName(BehaviorTreePackagePath))
        return FString();

    const FString AssetName =
        FPackageName::GetLongPackageAssetName(BehaviorTreePackagePath);
    const FString PackageDirectory =
        FPackageName::GetLongPackagePath(BehaviorTreePackagePath);
    const FString BlackboardName = AssetName.StartsWith(TEXT("BT_"))
        ? TEXT("BB_") + AssetName.RightChop(3)
        : TEXT("BB_") + AssetName;
    return PackageDirectory / BlackboardName;
}

/**
 * 使用资产元数据中的 LuaModuleName 执行纯导入与校验，不修改 BehaviorTree 或 Blackboard。
 * 只能在游戏线程调用；空模块配置返回结构化 MissingAssetConfiguration 诊断。
 *
 * @param BehaviorTree 已配置或待配置的目标资产。
 * @param OutDiagnostics 接收全部导入与配置诊断。
 * @return 配置存在且 Lua IR 合法时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::CheckConfiguredBehaviorTree(
    UBehaviorTree* BehaviorTree,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace LuaBehaviorTreeFactory;

    OutDiagnostics.Reset();
    FLuaBehaviorTreeAssetConfiguration Configuration;
    if (!GetLuaAssetConfiguration(BehaviorTree, Configuration)
        || Configuration.LuaModuleName.IsEmpty())
    {
        AddError(
            OutDiagnostics,
            MissingAssetConfiguration,
            TEXT("行为树尚未配置 LuaModuleName。"),
            BehaviorTree ? BehaviorTree->GetPathName() : TEXT("None"));
        return false;
    }
    FLuaBehaviorTreeIR IR;
    return ULuaBehaviorTreeIRLibrary::CompileLuaModuleFresh(
        Configuration.LuaModuleName,
        IR,
        OutDiagnostics);
}

/**
 * 使用当前资产配置原地重建 BehaviorTree，并优先复用当前 BlackboardAsset。
 * 当前树没有 Blackboard 时依次使用已保存路径和 BT_→BB_ 推导路径；成功后回写实际路径。
 * 只能在编辑器游戏线程调用，不启动 PIE；bSaveAssets=false 时仅标记相关包 Dirty。
 *
 * @param BehaviorTree 要原地更新的目标资产。
 * @param bSaveAssets 是否立即保存 BehaviorTree 与 Blackboard 包。
 * @param OutBlackboard 接收实际生成或复用的 Blackboard。
 * @param OutGeneratedBehaviorTree 接收工厂返回资产，成功时与输入位于同一路径。
 * @param OutDiagnostics 接收配置、导入、反射和 Graph 诊断。
 * @return 配置合法且两个资产完成生成及可选保存时返回 true。
 */
bool ULuaBehaviorTreeFactoryLibrary::GenerateConfiguredBehaviorTree(
    UBehaviorTree* BehaviorTree,
    const bool bSaveAssets,
    UBlackboardData*& OutBlackboard,
    UBehaviorTree*& OutGeneratedBehaviorTree,
    TArray<FLuaBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace LuaBehaviorTreeFactory;

    OutBlackboard = nullptr;
    OutGeneratedBehaviorTree = nullptr;
    OutDiagnostics.Reset();
    FLuaBehaviorTreeAssetConfiguration Configuration;
    if (!GetLuaAssetConfiguration(BehaviorTree, Configuration)
        || Configuration.LuaModuleName.IsEmpty())
    {
        AddError(
            OutDiagnostics,
            MissingAssetConfiguration,
            TEXT("行为树尚未配置 LuaModuleName。"),
            BehaviorTree ? BehaviorTree->GetPathName() : TEXT("None"));
        return false;
    }

    const FString BehaviorTreePackagePath =
        BehaviorTree->GetOutermost()->GetName();
    FString BlackboardPackagePath;
    if (BehaviorTree->BlackboardAsset)
        BlackboardPackagePath = BehaviorTree->BlackboardAsset->GetOutermost()->GetName();
    else if (!Configuration.BlackboardPackagePath.IsEmpty())
        BlackboardPackagePath = Configuration.BlackboardPackagePath;
    else
        BlackboardPackagePath = DeriveBlackboardPackagePath(BehaviorTreePackagePath);

    if (!GenerateFromLua(
            Configuration.LuaModuleName,
            BlackboardPackagePath,
            BehaviorTreePackagePath,
            bSaveAssets,
            OutBlackboard,
            OutGeneratedBehaviorTree,
            OutDiagnostics))
    {
        return false;
    }

    Configuration.BlackboardPackagePath = BlackboardPackagePath;
    Configuration.SourceMode = ELuaBehaviorTreeSourceMode::Lua;
    if (!SetLuaAssetConfiguration(OutGeneratedBehaviorTree, Configuration))
    {
        AddError(
            OutDiagnostics,
            MissingAssetConfiguration,
            TEXT("生成成功，但无法回写行为树 Lua 资产配置。"),
            OutGeneratedBehaviorTree->GetPathName());
        return false;
    }
    OutGeneratedBehaviorTree->PostEditChange();
    if (bSaveAssets && !SaveAsset(OutGeneratedBehaviorTree, OutDiagnostics))
        return false;
    return true;
}
