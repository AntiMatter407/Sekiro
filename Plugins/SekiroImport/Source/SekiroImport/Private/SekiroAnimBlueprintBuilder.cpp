#include "SekiroAnimBlueprintBuilder.h"
#include "SekiroAnimLogicIR.h"
#include "SekiroAnimLogicData.h"
#include "SekiroAnimNotifies.h"
#include "SekiroImportLog.h"
#include "SekiroImport.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/GarbageCollection.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"
#include "Misc/PackageName.h"

#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimBlueprintExtension.h"
#include "AnimationStateMachineGraph.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_TransitionResult.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Kismet2/Kismet2NameValidators.h"

// ============================================================================
// 辅助函数：通过 PinCategory 查找 Pose 引脚，避免硬编码引脚名
// ============================================================================

namespace
{
	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node) return nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			// UE5.2: FPoseLink 属性生成的引脚类别为 "struct"，而非 "pose"
			if (Pin && Pin->Direction == Direction &&
				(Pin->PinType.PinCategory == TEXT("struct") || Pin->PinType.PinCategory == TEXT("pose")))
				return Pin;
		}
		return nullptr;
	}

	void DebugLogNodePins(UEdGraphNode* Node, const TCHAR* NodeLabel)
	{
		if (!Node) { UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   %s: nullptr"), NodeLabel); return; }
		UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   %s Pins=%d:"), NodeLabel, Node->Pins.Num());
		for (UEdGraphPin* Pin : Node->Pins)
		{
			UE_LOG(LogSekiroImport, Log,
				TEXT("[AnimBPBuilder]     Pin[%s] Dir=%d Cat='%s'"),
				*Pin->PinName.ToString(), (int32)Pin->Direction, *Pin->PinType.PinCategory.ToString());
		}
		if (Node->Pins.Num() == 0)
		{
			UE_LOG(LogSekiroImport, Warning, TEXT("[AnimBPBuilder]   %s 没有引脚！"), NodeLabel);
		}
	}
}

// ============================================================================
// Build
// ============================================================================

FSekiroAnimBlueprintBuilder::FBuildResult FSekiroAnimBlueprintBuilder::Build(
    const FSKAnimLogicImportResult& IR,
    USkeleton* Skeleton,
    const TMap<int32, UAnimSequence*>& AnimSequences,
    const FString& OutputBasePath)
{
    FBuildResult Result;
    Result.bSuccess = false;

    if (!Skeleton)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] Skeleton is null"));
        return Result;
    }

    // ── Step 1: 为每段动画注入 AnimNotify ──────────────────
    TSet<UPackage*> ModifiedPackages;
    for (const auto& Pair : AnimSequences)
    {
        const FSKAnimationLogicIR* Logic = IR.AnimLogicMap.Find(Pair.Key);
        if (Logic && Pair.Value)
        {
            int32 Count = InjectNotifies(Pair.Value, *Logic);
            Result.NotifyCount += Count;
            if (Count > 0)
                ModifiedPackages.Add(Pair.Value->GetOutermost());
        }
    }

    // 保存修改过的 AnimSequence 包
    for (UPackage* Pkg : ModifiedPackages)
    {
        FString PkgName = Pkg->GetName();
        FString FilePath = FPackageName::LongPackageNameToFilename(
            PkgName, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        UPackage::SavePackage(Pkg, nullptr, *FilePath, SaveArgs);
    }
    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] 保存了 %d 个 AnimSequence 包的 Notify"), ModifiedPackages.Num());

    // ── Step 2: 生成 DataAsset ──────────────────────────────
    FString DataAssetPkgPath = FString::Printf(TEXT("%s/SK_AnimLogicData"), *OutputBasePath);
    USKAnimationLogicData* DataAsset = BuildDataAsset(IR, DataAssetPkgPath);
    if (DataAsset)
        Result.DataAssetPath = DataAssetPkgPath;

    // ── Step 3: 创建/加载 AnimBlueprint ──────────────────────
    FString BP_Path = FString::Printf(TEXT("%s/ABP_Sekiro"), *OutputBasePath);
    UAnimBlueprint* AnimBP = LoadOrCreateAnimBlueprint(BP_Path, Skeleton);
    if (!AnimBP)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] 加载/创建 AnimBlueprint 失败"));
        return Result;
    }
    Result.AnimBlueprintPath = BP_Path;

    // Reset loader to avoid "partially loaded" error on save
    ResetLoaders(AnimBP->GetOutermost());

    // ── Step 4: 构建状态机 ──────────────────────────────────
    if (BuildStateMachine(AnimBP, IR.MainStateMachine, AnimSequences))
    {
        Result.StateCount = IR.MainStateMachine.AnimIDsByCategory.Num();
        Result.TransitionCount = IR.MainStateMachine.Transitions.Num();
    }

    // Compile and save ABP
    FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
    FKismetEditorUtilities::CompileBlueprint(AnimBP);

    FString ABP_FilePath = FPackageName::LongPackageNameToFilename(
        BP_Path, FPackageName::GetAssetPackageExtension());
    UPackage* ABP_Package = AnimBP->GetOutermost();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    bool bSaved = UPackage::SavePackage(ABP_Package, AnimBP, *ABP_FilePath, SaveArgs);
    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] ABP %s %s"),
        bSaved ? TEXT("saved") : TEXT("save FAILED"), *BP_Path);

    Result.bSuccess = bSaved;

    UE_LOG(LogSekiroImport, Log,
        TEXT("[AnimBPBuilder] 完成: %s | States=%d Transitions=%d Notifies=%d"),
        *BP_Path, Result.StateCount, Result.TransitionCount, Result.NotifyCount);

    return Result;
}

// ============================================================================
// LoadOrCreateAnimBlueprint
// ============================================================================

static void PurgeAnimBlueprintGraphs(UAnimBlueprint* AnimBP);

static void SetAnimInstanceClass(UAnimBlueprint* AnimBP)
{
	// 按类路径动态加载，避免插件硬引用项目模块
	UClass* SKAnimClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Script/Sekiro.SKAnimInstance"));
	if (SKAnimClass)
	{
		AnimBP->ParentClass = SKAnimClass;
		UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] ParentClass set to USKAnimInstance"));
	}
	else
	{
		UE_LOG(LogSekiroImport, Warning, TEXT("[AnimBPBuilder] USekiroAnimInstance not found, keeping default ParentClass"));
	}
}

UAnimBlueprint* FSekiroAnimBlueprintBuilder::LoadOrCreateAnimBlueprint(const FString& PackagePath, USkeleton* Skeleton)
{
		// Always create fresh ABP to avoid UObject naming conflicts
		if (UPackage* OldPackage = FindPackage(nullptr, *PackagePath))
		{
			ResetLoaders(OldPackage);
			TArray<UObject*> OldObjects;
			GetObjectsWithOuter(OldPackage, OldObjects, true);
			for (UObject* Obj : OldObjects)
			{
				Obj->ClearFlags(RF_Standalone | RF_Public);
			}
			OldPackage->ClearFlags(RF_Standalone | RF_Public);
			OldPackage->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		// Delete old .uasset file to ensure SavePackage can write
		FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		if (FPaths::FileExists(FilePath))
		{
			if (!IFileManager::Get().Delete(*FilePath, false, true, true))
			{
				UE_LOG(LogSekiroImport, Warning, TEXT("[AnimBPBuilder] Failed to delete old ABP file: %s"), *FilePath);
			}
			else
			{
				UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] Deleted old ABP file: %s"), *FilePath);
			}
		}

		UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] Creating fresh AnimBlueprint: %s"), *PackagePath);
		return CreateAnimBlueprint(PackagePath, Skeleton);
	}

	// ============================================================================
	// LoadExistingAnimBlueprint
// ============================================================================

UAnimBlueprint* FSekiroAnimBlueprintBuilder::LoadExistingAnimBlueprint(const FString& PackagePath)
{
    FString ObjectName = FPaths::GetBaseFilename(PackagePath);

    // 先检查内存中是否已加载
    UAnimBlueprint* AnimBP = FindObject<UAnimBlueprint>(nullptr, *PackagePath);
    if (AnimBP)
        return AnimBP;

    // 尝试从磁盘加载
    FString FilePath = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    if (!FPaths::FileExists(FilePath))
        return nullptr;

    UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
    if (!Package)
        return nullptr;

    AnimBP = FindObject<UAnimBlueprint>(Package, *ObjectName);
    return AnimBP;
}

// ============================================================================
// CreateAnimBlueprint
// ============================================================================

UAnimBlueprint* FSekiroAnimBlueprintBuilder::CreateAnimBlueprint(const FString& PackagePath, USkeleton* Skeleton)
{
    FString ObjectName = FPaths::GetBaseFilename(PackagePath);

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] CreatePackage failed: %s"), *PackagePath);
        return nullptr;
    }

    UAnimBlueprint* AnimBP = NewObject<UAnimBlueprint>(
        Package, UAnimBlueprint::StaticClass(), *ObjectName,
        RF_Public | RF_Standalone | RF_Transactional);

    if (!AnimBP)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] NewObject<UAnimBlueprint> failed"));
        return nullptr;
    }

    AnimBP->TargetSkeleton = Skeleton;
    AnimBP->bIsNewlyCreated = true;
    SetAnimInstanceClass(AnimBP);
    AnimBP->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(AnimBP);

    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] AnimBlueprint created (save deferred): %s"), *PackagePath);

    return AnimBP;
}

// ============================================================================
// BuildStateMachine
// ============================================================================

static UEdGraph* FindOrCreateAnimGraph(UAnimBlueprint* AnimBP)
{
    const FName AnimGraphName = UEdGraphSchema_K2::GN_AnimGraph;
    UEdGraph* Existing = FindObject<UEdGraph>(AnimBP, *AnimGraphName.ToString());
    if (Existing)
        return Existing;

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        AnimBP, AnimGraphName, UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());
    if (NewGraph)
    {
        // 确保 AnimGraph 被添加到 AnimBlueprint 的 FunctionGraphs，以便编译器能够发现
        AnimBP->FunctionGraphs.AddUnique(NewGraph);

        // AnimationGraphSchema 创建默认节点 (Root 节点)
        const UAnimationGraphSchema* Schema = Cast<UAnimationGraphSchema>(NewGraph->GetSchema());
        if (Schema)
            Schema->CreateDefaultNodesForGraph(*NewGraph);
    }
    return NewGraph;
}

static void CleanStateMachineGraph(UAnimationStateMachineGraph* SMGraph)
{
    TArray<UAnimStateNode*> OldStates;
    TArray<UAnimStateTransitionNode*> OldTransitions;
    SMGraph->GetNodesOfClass<UAnimStateNode>(OldStates);
    SMGraph->GetNodesOfClass<UAnimStateTransitionNode>(OldTransitions);

    for (UAnimStateTransitionNode* Trans : OldTransitions)
    {
        if (UEdGraph* TransBoundGraph = Trans->GetBoundGraph())
        {
            TransBoundGraph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
        }
        Trans->BreakAllNodeLinks();
        SMGraph->RemoveNode(Trans, true);
    }

    for (UAnimStateNode* State : OldStates)
    {
        UEdGraph* BoundGraph = State->GetBoundGraph();
        State->ClearBoundGraph();
        if (BoundGraph)
        {
            BoundGraph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
            SMGraph->SubGraphs.Remove(BoundGraph);
        }
        State->BreakAllNodeLinks();
        State->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
        SMGraph->RemoveNode(State, false);
    }
}

// ── 彻底清空 AnimBlueprint 的所有图（用于重建前清理）──
static void PurgeAnimBlueprintGraphs(UAnimBlueprint* AnimBP)
{
    TArray<UEdGraph*> AllGraphs;
    AnimBP->GetAllGraphs(AllGraphs);
    for (UEdGraph* G : AllGraphs)
    {
        TArray<UEdGraphNode*> Nodes;
        G->GetNodesOfClass(Nodes);
        for (UEdGraphNode* N : Nodes)
        {
            N->BreakAllNodeLinks();
            G->RemoveNode(N, true);
        }
        // 递归清理子图（状态机子图、状态绑定图等）
        TArray<UEdGraph*> Children;
        G->GetAllChildrenGraphs(Children);
        for (UEdGraph* Child : Children)
        {
            TArray<UEdGraphNode*> ChildNodes;
            Child->GetNodesOfClass(ChildNodes);
            for (UEdGraphNode* CN : ChildNodes)
            {
                CN->BreakAllNodeLinks();
                Child->RemoveNode(CN, true);
            }
        }
    }

    AnimBP->FunctionGraphs.Empty();
    AnimBP->UbergraphPages.Empty();
    AnimBP->ImplementedInterfaces.Empty();

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] 清空 AnimBlueprint 全部旧图"));
}

static UAnimGraphNode_StateMachine* FindOrCreateStateMachineNode(UEdGraph* AnimGraph)
{
    // 确保 AnimBlueprint 注册了 StateMachine Extension（编译器需要它来发现动画节点）
    if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(AnimGraph)))
    {
        if (UClass* ExtClass = LoadClass<UAnimBlueprintExtension>(nullptr, TEXT("/Script/AnimGraph.AnimBlueprintExtension_StateMachine")))
        {
            UAnimBlueprintExtension::RequestExtension(AnimBP, ExtClass);
        }
    }

    // 彻底移除旧的 StateMachine 节点（连同其子图），避免名称冲突
    TArray<UAnimGraphNode_StateMachine*> ExistingNodes;
    AnimGraph->GetNodesOfClass<UAnimGraphNode_StateMachine>(ExistingNodes);
    for (UAnimGraphNode_StateMachine* OldSM : ExistingNodes)
    {
        int32 RemovedSubObjs = 0;
        if (OldSM->EditorStateMachineGraph)
        {
            TArray<UObject*> SubObjects;
            GetObjectsWithOuter(OldSM->EditorStateMachineGraph, SubObjects, true);
            RemovedSubObjs = SubObjects.Num();
            for (UObject* SubObj : SubObjects)
            {
                SubObj->ClearFlags(RF_Standalone | RF_Public);
                SubObj->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
            }
            CleanStateMachineGraph(OldSM->EditorStateMachineGraph);
        }
        OldSM->BreakAllNodeLinks();
        AnimGraph->RemoveNode(OldSM, true);
        UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] Removed old StateMachine node and %d sub-objects"), RemovedSubObjs);
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    // 创建新 StateMachine 节点
    UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(
        AnimGraph, UAnimGraphNode_StateMachine::StaticClass(), NAME_None, RF_Transactional);
    SMNode->CreateNewGuid();
    AnimGraph->AddNode(SMNode, false, false);
    SMNode->PostPlacedNewNode();
    SMNode->AllocateDefaultPins();

    SMNode->NodePosX = -200;
    SMNode->NodePosY = 0;
    SMNode->SnapToGrid(16);

    // 连接 StateMachine 输出到 Root 输入 — 使用 PinCategory 查找
    UEdGraphPin* SMPosePin = FindPosePin(SMNode, EGPD_Output);
    if (SMPosePin)
    {
        TArray<UAnimGraphNode_Root*> RootNodes;
        AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes);
        for (UAnimGraphNode_Root* Root : RootNodes)
        {
            UEdGraphPin* RootInputPin = FindPosePin(Root, EGPD_Input);
            if (RootInputPin)
            {
                RootInputPin->BreakAllPinLinks();
                SMPosePin->MakeLinkTo(RootInputPin);
                UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] SM→Root 连线: '%s' → '%s'"),
                    *SMPosePin->PinName.ToString(), *RootInputPin->PinName.ToString());
                break;
            }
        }
    }

    // 调试输出所有引脚
    DebugLogNodePins(SMNode, TEXT("StateMachine"));
    TArray<UAnimGraphNode_Root*> RootNodes2;
    AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes2);
    for (auto* R : RootNodes2) DebugLogNodePins(R, TEXT("Root"));

    return SMNode;
}

static UAnimStateNode* CreateStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName,
    int32 PosX, int32 PosY, UAnimSequence* DefaultSequence)
{
    UAnimStateNode* StateNode = NewObject<UAnimStateNode>(
        SMGraph, UAnimStateNode::StaticClass(), *StateName, RF_Transactional);
    StateNode->CreateNewGuid();
    SMGraph->AddNode(StateNode, false, false);
    StateNode->PostPlacedNewNode();
    StateNode->AllocateDefaultPins();

    StateNode->NodePosX = PosX;
    StateNode->NodePosY = PosY;
    StateNode->SnapToGrid(16);

    // 获取 BoundGraph 并在其中放入 SequencePlayer
    UEdGraph* BoundGraph = StateNode->GetBoundGraph();
    if (BoundGraph && DefaultSequence)
    {
        // 查找 StateResult 节点（PostPlacedNewNode 已创建）
        TArray<UAnimGraphNode_StateResult*> ResultNodes;
        BoundGraph->GetNodesOfClass<UAnimGraphNode_StateResult>(ResultNodes);
        UAnimGraphNode_StateResult* ResultNode = ResultNodes.Num() > 0 ? ResultNodes[0] : nullptr;

        UAnimGraphNode_SequencePlayer* SeqNode = NewObject<UAnimGraphNode_SequencePlayer>(
            BoundGraph, UAnimGraphNode_SequencePlayer::StaticClass(), NAME_None, RF_Transactional);
        SeqNode->CreateNewGuid();
        BoundGraph->AddNode(SeqNode, false, false);
        SeqNode->PostPlacedNewNode();
        SeqNode->AllocateDefaultPins();
        SeqNode->SetAnimationAsset(DefaultSequence);

        SeqNode->NodePosX = -300;
        SeqNode->NodePosY = 0;

        // 连接 SequencePlayer 输出 → StateResult 输入（用 PinCategory 查找）
        if (ResultNode)
        {
            UEdGraphPin* SeqOut = FindPosePin(SeqNode, EGPD_Output);
            UEdGraphPin* ResultIn = FindPosePin(ResultNode, EGPD_Input);
            if (SeqOut && ResultIn)
            {
                SeqOut->MakeLinkTo(ResultIn);
                UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   %s: SeqOut '%s' → ResultIn '%s'"),
                    *StateName, *SeqOut->PinName.ToString(), *ResultIn->PinName.ToString());
            }
            DebugLogNodePins(SeqNode, TEXT("SequencePlayer"));
            DebugLogNodePins(ResultNode, TEXT("StateResult"));
        }
    }

    return StateNode;
}

static UAnimStateNode* CreateBlendSpaceStateNode(
    UAnimationStateMachineGraph* SMGraph, const FString& StateName,
    int32 PosX, int32 PosY, UBlendSpace1D* BlendSpace)
{
    UAnimStateNode* StateNode = NewObject<UAnimStateNode>(
        SMGraph, UAnimStateNode::StaticClass(), *StateName, RF_Transactional);
    StateNode->CreateNewGuid();
    SMGraph->AddNode(StateNode, false, false);

    // Manually create BoundGraph instead of calling PostPlacedNewNode()
    // to avoid RenameGraphWithSuggestion(..., TEXT("State")) naming conflict
    UEdGraph* BoundGraph = FBlueprintEditorUtils::CreateNewGraph(
        StateNode, NAME_None, UAnimationStateGraph::StaticClass(), UAnimationStateGraphSchema::StaticClass());
    check(BoundGraph);
    StateNode->BoundGraph = BoundGraph;

    // Use state name as suggestion (not "State") for unique naming
    {
        TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(StateNode);
        FBlueprintEditorUtils::RenameGraphWithSuggestion(BoundGraph, NameValidator, *StateName);
    }

    // Initialize anim graph (creates StateResult default nodes)
    const UEdGraphSchema* BoundSchema = BoundGraph->GetSchema();
    BoundSchema->CreateDefaultNodesForGraph(*BoundGraph);

    // Register as child graph of parent state machine graph
    if (SMGraph->SubGraphs.Find(BoundGraph) == INDEX_NONE)
    {
        SMGraph->SubGraphs.Add(BoundGraph);
    }

    StateNode->AllocateDefaultPins();

    StateNode->NodePosX = PosX;
    StateNode->NodePosY = PosY;
    StateNode->SnapToGrid(16);

    if (BlendSpace)
    {
        TArray<UAnimGraphNode_StateResult*> ResultNodes;
        BoundGraph->GetNodesOfClass<UAnimGraphNode_StateResult>(ResultNodes);
        UAnimGraphNode_StateResult* ResultNode = ResultNodes.Num() > 0 ? ResultNodes[0] : nullptr;

        UAnimGraphNode_BlendSpacePlayer* BSNode = NewObject<UAnimGraphNode_BlendSpacePlayer>(
            BoundGraph, UAnimGraphNode_BlendSpacePlayer::StaticClass(), NAME_None, RF_Transactional);
        BSNode->CreateNewGuid();
        BoundGraph->AddNode(BSNode, false, false);
        BSNode->PostPlacedNewNode();
        BSNode->AllocateDefaultPins();
        BSNode->SetAnimationAsset(BlendSpace);

        BSNode->NodePosX = -300;
        BSNode->NodePosY = 0;

        if (ResultNode)
        {
            UEdGraphPin* BSOut = FindPosePin(BSNode, EGPD_Output);
            UEdGraphPin* ResultIn = FindPosePin(ResultNode, EGPD_Input);
            if (BSOut && ResultIn)
            {
                BSOut->MakeLinkTo(ResultIn);
                UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   %s: BlendSpacePlayer '%s' -> ResultIn '%s'"),
                    *StateName, *BSOut->PinName.ToString(), *ResultIn->PinName.ToString());
            }
        }
    }

    return StateNode;
}

static UAnimStateTransitionNode* CreateTransition(UAnimStateNode* From, UAnimStateNode* To,
    const FSKSMTransitionIR& TransIR)
{
    UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(
        From->GetGraph(), UAnimStateTransitionNode::StaticClass(), NAME_None, RF_Transactional);
    TransNode->CreateNewGuid();
    From->GetGraph()->AddNode(TransNode, false, false);

    // 顺序至关重要：先 PostPlacedNewNode（创建 BoundGraph），再 AllocateDefaultPins（创建引脚），最后 CreateConnections（连线）
    TransNode->PostPlacedNewNode();
    TransNode->AllocateDefaultPins();
    TransNode->CreateConnections(From, To);

    TransNode->PriorityOrder = TransIR.Priority;
    TransNode->CrossfadeDuration = FMath::Max(0.01f, TransIR.CrossfadeDuration);
    TransNode->bAutomaticRuleBasedOnSequencePlayerInState = true;

    TransNode->NodePosX = (From->NodePosX + To->NodePosX) / 2;
    TransNode->NodePosY = (From->NodePosY + To->NodePosY) / 2 + 150;

    return TransNode;
}

bool FSekiroAnimBlueprintBuilder::BuildStateMachine(UAnimBlueprint* AnimBP,
    const FSKStateMachineIR& SM,
    const TMap<int32, UAnimSequence*>& AnimSequences)
{
    if (SM.AnimIDsByCategory.Num() == 0)
    {
        UE_LOG(LogSekiroImport, Warning, TEXT("[AnimBPBuilder] 状态机无类别数据，跳过构建"));
        return true;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("SekiroImport", "BuildStateMachine", "Build Sekiro State Machine"));
    AnimBP->Modify();

    // ── Step 1: 找到或创建 AnimGraph ─────────────────────
    UEdGraph* AnimGraph = FindOrCreateAnimGraph(AnimBP);
    if (!AnimGraph)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] 无法找到/创建 AnimGraph"));
        return false;
    }

    // ── Step 2: 找到或创建 StateMachine 节点 ─────────────
    UAnimGraphNode_StateMachine* SMNode = FindOrCreateStateMachineNode(AnimGraph);
    if (!SMNode)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] 无法创建 StateMachine 节点"));
        return false;
    }

    UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
    if (!SMGraph)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] EditorStateMachineGraph 为空"));
        return false;
    }

    // ── Step 3: 按三层架构创建状态节点 ─────────────────
    // Layer → Category 映射
    struct FLayerConfig {
        FString Name;
        int32 PosX;
        int32 Priority;
    };
    TMap<FString, FLayerConfig> CategoryLayer;
    for (const auto& CatPair : SM.AnimIDsByCategory)
    {
        const FString& Cat = CatPair.Key;
        FLayerConfig Cfg;
        if (Cat == TEXT("Idle") || Cat == TEXT("Walk") || Cat == TEXT("Jog") || Cat == TEXT("Run") || Cat == TEXT("Sprint") || Cat == TEXT("Locomotion"))
        {
            Cfg.Name = TEXT("Locomotion");
            Cfg.PosX = 0;
            Cfg.Priority = 0;
        }
        else if (Cat == TEXT("Hit") || Cat == TEXT("Death") || Cat == TEXT("Knockback") || Cat == TEXT("Deathblow") || Cat == TEXT("Resurrection"))
        {
            Cfg.Name = TEXT("Reaction");
            Cfg.PosX = 1000;
            Cfg.Priority = 8;
        }
        else
        {
            Cfg.Name = TEXT("Combat");
            Cfg.PosX = 500;
            Cfg.Priority = 2;
        }
        CategoryLayer.Add(Cat, Cfg);
    }

    // 每层独立 Y 计数器，避免层间重叠
    TMap<FString, int32> LayerYOffset;
    static const int32 StateSpacingY = 200;

    // 只创建有 AnimSequence 匹配的状态（跳过空状态）
    struct FValidCategory
    {
        FString Name;
        TArray<int32> AnimIDs;
        FString LayerName;
        int32 PosX;
    };
    TArray<FValidCategory> ValidCategories;

    for (const auto& CatPair : SM.AnimIDsByCategory)
    {
        const FString& Category = CatPair.Key;
        const TArray<int32>& AnimIDs = CatPair.Value;

        // 检查是否有匹配的动画序列
        bool bHasAnim = false;
        for (int32 ID : AnimIDs)
        {
            if (AnimSequences.Find(ID)) { bHasAnim = true; break; }
        }
        if (!bHasAnim)
        {
            UE_LOG(LogSekiroImport, Verbose, TEXT("[AnimBPBuilder]   跳过空状态: %s (0/%d matched)"), *Category, AnimIDs.Num());
            continue;
        }

        const FLayerConfig* Layer = CategoryLayer.Find(Category);
        FValidCategory VC;
        VC.Name = Category;
        VC.AnimIDs = AnimIDs;
        VC.LayerName = Layer ? Layer->Name : TEXT("Combat");
        VC.PosX = Layer ? Layer->PosX : 500;
        ValidCategories.Add(VC);
    }

    TMap<FString, UAnimStateNode*> CategoryStateMap;
    bool bHasLocomotion = false;
    for (const FValidCategory& VC : ValidCategories)
    {
        if (VC.LayerName == TEXT("Locomotion"))
        {
            bHasLocomotion = true;
            continue;
        }

        UAnimSequence* DefaultSeq = nullptr;
        for (int32 ID : VC.AnimIDs)
        {
            if (const UAnimSequence* const* Found = AnimSequences.Find(ID))
            {
                DefaultSeq = const_cast<UAnimSequence*>(*Found);
                break;
            }
        }

        int32 StateIdx = LayerYOffset.FindOrAdd(VC.LayerName, 0);
        int32 PosY = StateIdx * StateSpacingY;
        LayerYOffset[VC.LayerName] = StateIdx + 1;

        UAnimStateNode* State = CreateStateNode(SMGraph, VC.Name, VC.PosX, PosY, DefaultSeq);
        CategoryStateMap.Add(VC.Name, State);

        UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   [%s] %s (x=%d,y=%d)"),
            *VC.LayerName, *VC.Name, VC.PosX, PosY);
    }

    // Create Locomotion BlendSpace and state (replaces Idle/Walk/Jog/Run/Sprint)
    if (bHasLocomotion)
    {
        UBlendSpace1D* LocomotionBS = BuildLocomotionBlendSpace(
            AnimBP, AnimBP->TargetSkeleton, AnimSequences, SM.AnimIDsByCategory);
        if (LocomotionBS)
        {
            int32 LocoPosX = 0;
            int32 LocoPosY = 0;
            UAnimStateNode* LocoState = CreateBlendSpaceStateNode(
                SMGraph, TEXT("Locomotion"), LocoPosX, LocoPosY, LocomotionBS);
            if (LocoState)
            {
                CategoryStateMap.Add(TEXT("Locomotion"), LocoState);
                // Alias individual locomotion categories for transition lookup
                CategoryStateMap.Add(TEXT("Idle"), LocoState);
                CategoryStateMap.Add(TEXT("Walk"), LocoState);
                CategoryStateMap.Add(TEXT("Jog"), LocoState);
                CategoryStateMap.Add(TEXT("Run"), LocoState);
                CategoryStateMap.Add(TEXT("Sprint"), LocoState);
                UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder]   [Locomotion] BlendSpace state (x=%d,y=%d)"), LocoPosX, LocoPosY);
            }
        }
    }

    // ── Step 4: 连接 Entry → Locomotion::Idle ──────────────
    TArray<UAnimStateEntryNode*> EntryNodes;
    SMGraph->GetNodesOfClass<UAnimStateEntryNode>(EntryNodes);
    if (EntryNodes.Num() > 0)
    {
        UEdGraphPin* EntryOut = FindPosePin(EntryNodes[0], EGPD_Output);
        if (!EntryOut) EntryOut = EntryNodes[0]->FindPin(TEXT("Entry"), EGPD_Output);
        if (EntryOut)
        {
            UAnimStateNode*const* EntryLookup = CategoryStateMap.Find(TEXT("Locomotion"));
            if (!EntryLookup)
                EntryLookup = CategoryStateMap.Find(TEXT("Idle"));
            if (!EntryLookup && CategoryStateMap.Num() > 0) EntryLookup = &CategoryStateMap.begin().Value();
            if (EntryLookup && *EntryLookup)
            {
                UEdGraphPin* InPin = (*EntryLookup)->GetInputPin();
                if (InPin) EntryOut->MakeLinkTo(InPin);
            }
        }
    }

    // ── Step 5: 创建过渡（三层优先级） ────────────────────
    int32 TransitionCount = 0;
    TArray<UAnimStateNode*> AllStates;
    // Deduplicate: locomotion aliases (Idle/Walk/Jog/Run/Sprint) all point to same BlendSpace state
    {
        TSet<UAnimStateNode*> UniqueStates;
        for (auto& Pair : CategoryStateMap)
            UniqueStates.Add(Pair.Value);
        AllStates = UniqueStates.Array();
    }

    // 优先级表（设计文档 4.2 节）
    static const TMap<FString, int32> ActionPriority = {
        {TEXT("Deathblow"), 10}, {TEXT("Resurrection"), 10},
        {TEXT("Death"), 9}, {TEXT("Knockback"), 8},
        {TEXT("Hit"), 8}, {TEXT("Dodge"), 7},
        {TEXT("Deflect"), 6}, {TEXT("Guard"), 5},
        {TEXT("Prosthetic"), 4}, {TEXT("ItemUse"), 3},
        {TEXT("Attack"), 2}, {TEXT("Quickstep"), 1},
    };

    auto GetPrio = [](const FString& Cat) -> int32 {
        const int32* P = ActionPriority.Find(Cat);
        return P ? *P : 0;
    };

    // 5a. IR 中的显式过渡
    for (const FSKSMTransitionIR& TransIR : SM.Transitions)
    {
        FString FromCat = TransIR.FromState.ToString();
        FString ToCat = TransIR.ToState.ToString();
        // strip AnimID suffix if present
        {
            int32 LastUnderscore = -1;
            if (FromCat.FindLastChar('_', LastUnderscore))
            {
                FString Tail = FromCat.RightChop(LastUnderscore + 1);
                bool bIsNum = true;
                for (TCHAR C : Tail) { if (!FChar::IsDigit(C)) { bIsNum = false; break; } }
                if (bIsNum) FromCat = FromCat.Left(LastUnderscore);
            }
            if (ToCat.FindLastChar('_', LastUnderscore))
            {
                FString Tail = ToCat.RightChop(LastUnderscore + 1);
                bool bIsNum = true;
                for (TCHAR C : Tail) { if (!FChar::IsDigit(C)) { bIsNum = false; break; } }
                if (bIsNum) ToCat = ToCat.Left(LastUnderscore);
            }
        }

        UAnimStateNode** FromNode = CategoryStateMap.Find(FromCat);
        UAnimStateNode** ToNode = CategoryStateMap.Find(ToCat);
        if (FromNode && ToNode && *FromNode && *ToNode && *FromNode != *ToNode)
        {
            UAnimStateTransitionNode* T = CreateTransition(*FromNode, *ToNode, TransIR);
            T->PriorityOrder = FMath::Max(TransIR.Priority, GetPrio(ToCat));
            T->CrossfadeDuration = FMath::Clamp(TransIR.CrossfadeDuration, 0.03f, 0.15f);
            TransitionCount++;
        }
    }

    // 5b. 无显式过渡时，构建"任何→更高优先级"过渡网
    if (TransitionCount == 0 && AllStates.Num() > 1)
    {
        for (int32 i = 0; i < AllStates.Num(); ++i)
        {
            for (int32 j = 0; j < AllStates.Num(); ++j)
            {
                if (i == j) continue;
                FString FromCat = AllStates[i]->GetStateName();
                FString ToCat = AllStates[j]->GetStateName();
                int32 ToPrio = GetPrio(ToCat);
                int32 FromPrio = GetPrio(FromCat);

                // 只在高优先级打断低优先级时创建过渡
                if (ToPrio > FromPrio || FromPrio == 0)
                {
                    FSKSMTransitionIR DefaultTrans;
                    DefaultTrans.Priority = ToPrio;
                    DefaultTrans.CrossfadeDuration = (ToPrio >= 8) ? 0.05f : 0.1f;
                    UAnimStateTransitionNode* T = CreateTransition(AllStates[i], AllStates[j], DefaultTrans);
                    T->PriorityOrder = ToPrio;
                    T->CrossfadeDuration = DefaultTrans.CrossfadeDuration;
                    TransitionCount++;
                }
            }
        }

        // 兜底: 同层之间允许双向过渡（TimeRemaining < 0.1 自动规则）
        for (int32 i = 0; i < AllStates.Num(); ++i)
        {
            for (int32 j = 0; j < AllStates.Num(); ++j)
            {
                if (i == j) continue;
                FString FromCat = AllStates[i]->GetStateName();
                FString ToCat = AllStates[j]->GetStateName();
                const FLayerConfig* FromLayer = CategoryLayer.Find(FromCat);
                const FLayerConfig* ToLayer = CategoryLayer.Find(ToCat);
                if (FromLayer && ToLayer && FromLayer->Name == ToLayer->Name)
                {
                    int32 ExistCount = 0;
                    // Check if transition already exists from i → j
                    for (UEdGraphPin* Pin : AllStates[i]->Pins)
                    {
                        if (Pin->Direction == EGPD_Output)
                        {
                            for (UEdGraphPin* Linked : Pin->LinkedTo)
                            {
                                if (Linked->GetOwningNode() == AllStates[j]) { ExistCount++; break; }
                            }
                        }
                    }
                    if (ExistCount == 0)
                    {
                        FSKSMTransitionIR PeerTrans;
                        PeerTrans.Priority = 1;
                        PeerTrans.CrossfadeDuration = 0.1f;
                        UAnimStateTransitionNode* T = CreateTransition(AllStates[i], AllStates[j], PeerTrans);
                        T->PriorityOrder = 1;
                        T->CrossfadeDuration = 0.1f;
                        TransitionCount++;
                    }
                }
            }
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] StateMachine 构建完成: %d 状态, %d 过渡"),
        AllStates.Num(), TransitionCount);

    // ── Step 6: 标记修改 + 编译 + 保存 ──────────────────
    AnimBP->Modify();

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    FKismetEditorUtilities::CompileBlueprint(AnimBP);

    FString PackageName = AnimBP->GetOutermost()->GetName();
    FString FilePath = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    bool bSaved = UPackage::SavePackage(AnimBP->GetOutermost(), AnimBP, *FilePath, SaveArgs);
    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] AnimBlueprint %s %s after SM build"),
        bSaved ? TEXT("saved") : TEXT("save FAILED"), *PackageName);

    return true;
}

// ============================================================================
// BuildLocomotionBlendSpace
// ============================================================================

UBlendSpace1D* FSekiroAnimBlueprintBuilder::BuildLocomotionBlendSpace(
    UAnimBlueprint* AnimBP, USkeleton* Skeleton,
    const TMap<int32, UAnimSequence*>& AnimSequences,
    const TMap<FString, TArray<int32>>& CategoryAnimIDs)
{
    // Create as standalone asset (not sub-object of ABP), so Editor can open it independently
    FString ABPPackagePath = AnimBP->GetOutermost()->GetName();
    FString BSPackagePath = ABPPackagePath.Replace(TEXT("ABP_Sekiro"), TEXT("SK_Locomotion_BS"));
    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(BSPackagePath);
    if (!Package)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimBPBuilder] Failed to create BlendSpace package: %s"), *BSPackagePath);
        return nullptr;
    }

    UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>(
        Package, UBlendSpace1D::StaticClass(), TEXT("SK_Locomotion_BS"),
        RF_Public | RF_Standalone | RF_Transactional);
    BlendSpace->SetSkeleton(Skeleton);

    // Reflectively set BlendParameters[0]: DisplayName="Speed", Min=0, Max=600, GridNum=4
    // BlendParameters[3] is a fixed C array -> FStructProperty ArrayDim=3 (not TArray)
    FStructProperty* BlendParamsProp = CastField<FStructProperty>(
        UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters")));
    if (BlendParamsProp)
    {
        void* ElemPtr = BlendParamsProp->ContainerPtrToValuePtr<void>(BlendSpace, 0);
        FProperty* DisplayNameProp = BlendParamsProp->Struct->FindPropertyByName(TEXT("DisplayName"));
        FProperty* MinProp = BlendParamsProp->Struct->FindPropertyByName(TEXT("Min"));
        FProperty* MaxProp = BlendParamsProp->Struct->FindPropertyByName(TEXT("Max"));
        FProperty* GridNumProp = BlendParamsProp->Struct->FindPropertyByName(TEXT("GridNum"));
        if (DisplayNameProp) { FStrProperty* SP = CastField<FStrProperty>(DisplayNameProp); if (SP) SP->SetPropertyValue_InContainer(ElemPtr, TEXT("Speed")); }
        if (MinProp) { FFloatProperty* FP = CastField<FFloatProperty>(MinProp); if (FP) FP->SetPropertyValue_InContainer(ElemPtr, 0.f); }
        if (MaxProp) { FFloatProperty* FP = CastField<FFloatProperty>(MaxProp); if (FP) FP->SetPropertyValue_InContainer(ElemPtr, 600.f); }
        if (GridNumProp) { FIntProperty* IP = CastField<FIntProperty>(GridNumProp); if (IP) IP->SetPropertyValue_InContainer(ElemPtr, 4); }
    }

    // Pick first matching animation from each specific category for each speed
    // This guarantees correct animation for each locomotion tier after InferCategoryFromAnimID fix
    static const TPair<FString, float> CategorySpeedMap[] = {
        {TEXT("Idle"),    0.f},
        {TEXT("Walk"),    150.f},
        {TEXT("Jog"),     350.f},
        {TEXT("Run"),     500.f},
        {TEXT("Sprint"),  600.f},
    };

    for (const auto& Pair : CategorySpeedMap)
    {
        const TArray<int32>* IDs = CategoryAnimIDs.Find(Pair.Key);
        if (!IDs) continue;

        for (int32 ID : *IDs)
        {
            UAnimSequence* const* Found = AnimSequences.Find(ID);
            if (Found && *Found)
            {
                BlendSpace->AddSample(*Found, FVector(Pair.Value, 0.f, 0.f));
                UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] BlendSpace sample: Cat=%s AnimID=%d Speed=%.0f [%s]"),
                    *Pair.Key, ID, Pair.Value, *(*Found)->GetName());
                break; // 取每个类别的第一个匹配动画
            }
        }
    }

    // 补足：如果某个类别缺失，从 Locomotion 类目按 AnimID 排序取剩余动画填充
    if (BlendSpace->GetNumberOfBlendSamples() < 3)
    {
        const TArray<int32>* LocoIDs = CategoryAnimIDs.Find(TEXT("Locomotion"));
        if (LocoIDs)
        {
            TArray<int32> Sorted = *LocoIDs;
            Sorted.Sort();
            const float FallbackSpeeds[] = {150.f, 350.f, 500.f, 600.f};
            int32 FallbackIdx = 0;
            for (int32 ID : Sorted)
            {
                if (BlendSpace->GetNumberOfBlendSamples() >= 5) break;
                UAnimSequence* const* Found = AnimSequences.Find(ID);
                if (Found && *Found)
                {
                    BlendSpace->AddSample(*Found, FVector(FallbackSpeeds[FMath::Min(FallbackIdx, 3)], 0.f, 0.f));
                    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] BlendSpace fallback[%d]: AnimID=%d Speed=%.0f [%s]"),
                        FallbackIdx, ID, FallbackSpeeds[FMath::Min(FallbackIdx, 3)], *(*Found)->GetName());
                    FallbackIdx++;
                }
            }
        }
    }

    BlendSpace->ResampleData();

    // Set AxisToScaleAnimation = BSA_X via reflection (protected member)
    FByteProperty* AxisToScaleProp = CastField<FByteProperty>(
        UBlendSpace::StaticClass()->FindPropertyByName(TEXT("AxisToScaleAnimation")));
    if (AxisToScaleProp)
    {
        AxisToScaleProp->SetPropertyValue_InContainer(BlendSpace, (uint8)EBlendSpaceAxis::BSA_X);
    }

    FAssetRegistryModule::AssetCreated(BlendSpace);
    BlendSpace->MarkPackageDirty();

    FString FilePath = FPackageName::LongPackageNameToFilename(
        BSPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    bool bSaved = UPackage::SavePackage(Package, BlendSpace, *FilePath, SaveArgs);

    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] BlendSpace %s: %s (%d samples)"),
        bSaved ? TEXT("saved") : TEXT("save FAILED"),
        *BSPackagePath, BlendSpace->GetBlendSamples().Num());
    return BlendSpace;
}

// ============================================================================
// InjectNotifies
// ============================================================================

int32 FSekiroAnimBlueprintBuilder::InjectNotifies(UAnimSequence* Sequence, const FSKAnimationLogicIR& Logic)
{
    if (!Sequence) return 0;

    // 清理已有 Notify
    CleanupNotifies(Sequence);

    int32 Count = 0;

    for (const FSKTAEEventIR& Event : Logic.AllEvents)
    {
        float Time = Event.StartTime;
        if (Time < 0.f) Time = 0.f;

        // 尝试创建 Notify (UAnimNotify) 或 NotifyState (UAnimNotifyState)
        UAnimNotify* Notify = nullptr;
        UAnimNotifyState* NotifyState = CreateNotifyStateForEvent(Event, Sequence);

        if (NotifyState)
        {
            // Type 300/301 等区间事件 → UAnimNotifyState
            FAnimNotifyEvent& NewEvent = Sequence->Notifies.AddDefaulted_GetRef();
            NewEvent.NotifyStateClass = NotifyState;
            NewEvent.SetTime(Time);
            NewEvent.Duration = FMath::Max(0.001f, Event.EndTime - Event.StartTime);
            NewEvent.NotifyName = FName(*Event.TypeName);
            Count++;
        }
        else if ((Notify = CreateNotifyForEvent(Event, Sequence)) != nullptr)
        {
            // 瞬时事件 → UAnimNotify
            FAnimNotifyEvent& NewEvent = Sequence->Notifies.AddDefaulted_GetRef();
            NewEvent.Notify = Notify;
            NewEvent.SetTime(Time);
            NewEvent.NotifyName = FName(*Event.TypeName);
            Count++;
        }
    }

    if (Count > 0)
    {
        Sequence->MarkPackageDirty();
        UE_LOG(LogSekiroImport, Verbose, TEXT("[AnimBPBuilder] %s: %d notifies injected"),
            *Logic.AnimName, Count);
    }

    return Count;
}

UAnimNotify* FSekiroAnimBlueprintBuilder::CreateNotifyForEvent(const FSKTAEEventIR& Event, UObject* Outer)
{
    switch (Event.Category)
    {
    case ESKTAEEventCategory::AttackBehavior:
    {
        UAnimNotify_SKAttackHitbox* N = NewObject<UAnimNotify_SKAttackHitbox>(Outer);
        if (Event.Params.Contains(TEXT("BehaviorJudgeID")))
            N->BehaviorJudgeID = FCString::Atoi(*Event.Params[TEXT("BehaviorJudgeID")]);
        if (Event.Params.Contains(TEXT("AttackType")))
            N->AttackType = FCString::Atoi(*Event.Params[TEXT("AttackType")]);
        if (Event.Params.Contains(TEXT("Source")))
            N->Source = FCString::Atoi(*Event.Params[TEXT("Source")]);
        return N;
    }

    case ESKTAEEventCategory::BulletBehavior:
    {
        UAnimNotify_SKBulletBehavior* N = NewObject<UAnimNotify_SKBulletBehavior>(Outer);
        if (Event.Params.Contains(TEXT("DummyPolyID")))
            N->DummyPolyID = FCString::Atoi(*Event.Params[TEXT("DummyPolyID")]);
        if (Event.Params.Contains(TEXT("BehaviorJudgeID")))
            N->BehaviorJudgeID = FCString::Atoi(*Event.Params[TEXT("BehaviorJudgeID")]);
        return N;
    }

    case ESKTAEEventCategory::AddSpEffect:
    {
        UAnimNotify_SKSpEffect* N = NewObject<UAnimNotify_SKSpEffect>(Outer);
        if (Event.Params.Contains(TEXT("SpEffectID")))
            N->SpEffectID = FCString::Atoi(*Event.Params[TEXT("SpEffectID")]);
        return N;
    }

    case ESKTAEEventCategory::RumbleCam:
    {
        UAnimNotify_SKRumbleCam* N = NewObject<UAnimNotify_SKRumbleCam>(Outer);
        if (Event.Params.Contains(TEXT("RumbleCamID")))
            N->RumbleCamID = FCString::Atoi(*Event.Params[TEXT("RumbleCamID")]);
        return N;
    }

    case ESKTAEEventCategory::PlaySound:
    case ESKTAEEventCategory::SpawnFFX:
    case ESKTAEEventCategory::FootStep:
        // 使用 UE 内置 Notify（运行时通过参数驱动）
        // 这里创建占位标记，实际运行时由 AnimLogicData 驱动
        return nullptr;

    case ESKTAEEventCategory::BehaviorFlag:
        // 由 CreateNotifyStateForEvent 处理
        return nullptr;

    default:
        return nullptr;
    }
}

UAnimNotifyState* FSekiroAnimBlueprintBuilder::CreateNotifyStateForEvent(const FSKTAEEventIR& Event, UObject* Outer)
{
    switch (Event.Category)
    {
    case ESKTAEEventCategory::BehaviorFlag:
    {
        UAnimNotifyState_SKBehaviorFlag* NS = NewObject<UAnimNotifyState_SKBehaviorFlag>(Outer);
        NS->bEnable = (Event.Type == 300);
        if (Event.Params.Contains(TEXT("FlagType")))
            NS->FlagName = FName(*Event.Params[TEXT("FlagType")]);
        return NS;
    }
    default:
        return nullptr;
    }
}

int32 FSekiroAnimBlueprintBuilder::CleanupNotifies(UAnimSequence* Sequence)
{
    if (!Sequence) return 0;

    int32 Removed = 0;
    // 移除所有以 "SK " 开头的旧 Notify（保留用户自定义的）
    TArray<FAnimNotifyEvent>& Notifies = Sequence->Notifies;
    for (int32 i = Notifies.Num() - 1; i >= 0; --i)
    {
        if (Notifies[i].Notify && Notifies[i].Notify->GetClass()->GetName().StartsWith(TEXT("SK")))
        {
            // 使用 Controller 移除更安全
            // Controller.RemoveNotify(Notifies[i]);
            Notifies.RemoveAt(i);
            Removed++;
        }
    }

    if (Removed > 0)
        Sequence->MarkPackageDirty();

    return Removed;
}

// ============================================================================
// BuildDataAsset
// ============================================================================

USKAnimationLogicData* FSekiroAnimBlueprintBuilder::BuildDataAsset(
    const FSKAnimLogicImportResult& IR,
    const FString& PackagePath)
{
    FString PackageName = PackagePath;
    FString ObjectName = FPaths::GetBaseFilename(PackagePath);

    UPackage* Package = FSekiroImportModule::CreatePackageForOverwrite(PackageName);
    if (!Package) return nullptr;

    USKAnimationLogicData* DataAsset = NewObject<USKAnimationLogicData>(
        Package, USKAnimationLogicData::StaticClass(), *ObjectName,
        RF_Public | RF_Standalone | RF_Transactional);

    if (!DataAsset) return nullptr;

    // 填充数据
    for (const auto& Pair : IR.AnimLogicMap)
    {
        const FSKAnimationLogicIR& Logic = Pair.Value;

        // 名称映射
        DataAsset->AnimNameMap.Add(Pair.Key, Logic.AnimName);

        // 取消规则（导出所有取消窗口）
        if (Logic.CancelWindows.Num() > 0)
        {
            FSKCancelRuleList RuleList;
            for (const FSKCancelWindowIR& Win : Logic.CancelWindows)
            {
                FSKCancelRule Rule;
                Rule.StartFrame = Win.StartFrame;
                Rule.EndFrame = Win.EndFrame;
                Rule.TargetAction = Win.TargetAction;
                Rule.CrossfadeDuration = Win.CrossfadeDuration;
                Rule.Priority = 10;
                RuleList.Rules.Add(Rule);
            }
            DataAsset->CancelRules.Add(Pair.Key, RuleList);
        }

        // 攻击盒（取第一个）
        if (Logic.AttackHitboxes.Num() > 0)
        {
            FSKAttackHitboxConfig Cfg;
            const FSKAttackHitboxIR& Hit = Logic.AttackHitboxes[0];
            Cfg.StartFrame = Hit.StartFrame;
            Cfg.EndFrame = Hit.EndFrame;
            Cfg.BehaviorJudgeID = Hit.BehaviorJudgeID;
            Cfg.AttackType = Hit.AttackType;
            FSKAttackHitboxList HitboxList;
            HitboxList.Hitboxes.Add(Cfg);
            DataAsset->AttackHitboxConfigs.Add(Pair.Key, HitboxList);
        }

        // SpEffect（全部）
        for (const FSKSpEffectIR& Sp : Logic.SpEffects)
        {
            FSKSpEffectConfig Cfg;
            Cfg.SpEffectID = Sp.SpEffectID;
            Cfg.StartFrame = Sp.StartFrame;
            Cfg.EndFrame = Sp.EndFrame;
            // Multi-map: one anim may have multiple SpEffects
            DataAsset->SpEffectConfigs.Add(
                Pair.Key * 10000 + Sp.SpEffectID % 10000, Cfg);
        }
    }

    // 类别映射
    for (const auto& CatPair : IR.MainStateMachine.AnimIDsByCategory)
    {
        FSKAnimIDList List;
        List.IDs = CatPair.Value;
        DataAsset->CategoryAnimMap.Add(CatPair.Key, List);
    }

    DataAsset->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(DataAsset);

    // 保存
    FString FilePath = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, DataAsset, *FilePath, SaveArgs);

    UE_LOG(LogSekiroImport, Log, TEXT("[AnimBPBuilder] DataAsset saved: %s (%d anims)"),
        *PackageName, DataAsset->AnimNameMap.Num());

    return DataAsset;
}
