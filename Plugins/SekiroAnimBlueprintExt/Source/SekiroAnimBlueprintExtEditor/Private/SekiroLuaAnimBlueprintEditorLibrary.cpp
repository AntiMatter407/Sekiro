#include "SekiroLuaAnimBlueprintEditorLibrary.h"

#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SekiroLuaStateMachine.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UnLuaInterface.h"

namespace
{
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

    UAnimGraphNode_Root* FindSekiroRootNode(UEdGraph* Graph)
    {
        if (!Graph) return nullptr;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
            {
                return RootNode;
            }
        }

        return nullptr;
    }

    UAnimGraphNode_SekiroLuaStateMachine* FindSekiroLuaAnimBlueprintHostNode(UEdGraph* Graph)
    {
        if (!Graph) return nullptr;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimGraphNode_SekiroLuaStateMachine* LuaNode = Cast<UAnimGraphNode_SekiroLuaStateMachine>(Node))
            {
                return LuaNode;
            }
        }

        return nullptr;
    }

    UEdGraphPin* FindSekiroPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        if (!Node) return nullptr;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == Direction)
            {
                return Pin;
            }
        }

        return nullptr;
    }
}

bool USekiroLuaAnimBlueprintEditorLibrary::ConnectLuaAnimBlueprintHostToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName)
{
    if (!AnimBlueprint || AnimationGraphName.IsNone()) return false;

    UEdGraph* AnimationGraph = FindSekiroAnimationGraph(AnimBlueprint, AnimationGraphName);
    UAnimGraphNode_Root* RootNode = FindSekiroRootNode(AnimationGraph);
    if (!AnimationGraph || !RootNode) return false;

    AnimBlueprint->Modify();
    AnimationGraph->Modify();

    UAnimGraphNode_SekiroLuaStateMachine* LuaNode = FindSekiroLuaAnimBlueprintHostNode(AnimationGraph);
    if (!LuaNode)
    {
        FGraphNodeCreator<UAnimGraphNode_SekiroLuaStateMachine> NodeCreator(*AnimationGraph);
        LuaNode = NodeCreator.CreateNode();
        LuaNode->NodePosX = RootNode->NodePosX - 300;
        LuaNode->NodePosY = RootNode->NodePosY;
        NodeCreator.Finalize();
    }

    if (!LuaNode) return false;

    LuaNode->Modify();
    LuaNode->Node.LayerName = LayerName;

    UEdGraphPin* RootInputPin = FindSekiroPosePin(RootNode, EGPD_Input);
    UEdGraphPin* LuaOutputPin = FindSekiroPosePin(LuaNode, EGPD_Output);
    if (!RootInputPin || !LuaOutputPin) return false;

    RootInputPin->Modify();
    LuaOutputPin->Modify();
    RootInputPin->BreakAllPinLinks();

    bool bConnected = false;
    const UEdGraphSchema* Schema = AnimationGraph->GetSchema();
    if (Schema)
    {
        bConnected = Schema->TryCreateConnection(LuaOutputPin, RootInputPin);
    }
    else
    {
        LuaOutputPin->MakeLinkTo(RootInputPin);
        bConnected = true;
    }

    if (!bConnected) return false;

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
    return true;
}

bool USekiroLuaAnimBlueprintEditorLibrary::ConnectLuaStateMachineToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName)
{
    return ConnectLuaAnimBlueprintHostToGraph(AnimBlueprint, AnimationGraphName, LayerName);
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

    FBPInterfaceDescription* InterfaceDescription = Blueprint->ImplementedInterfaces.FindByPredicate([](const FBPInterfaceDescription& Description)
    {
        return Description.Interface == UUnLuaInterface::StaticClass();
    });
    if (!InterfaceDescription || InterfaceDescription->Graphs.Num() == 0) return false;

    UEdGraph* GetModuleNameGraph = InterfaceDescription->Graphs[0];
    if (!GetModuleNameGraph || GetModuleNameGraph->Nodes.Num() < 2) return false;

    UEdGraphNode* ReturnNode = GetModuleNameGraph->Nodes[1];
    if (!ReturnNode || ReturnNode->Pins.Num() < 2) return false;

    Blueprint->Modify();
    GetModuleNameGraph->Modify();
    ReturnNode->Modify();

    ReturnNode->Pins[1]->DefaultValue = LuaModuleName;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return true;
}
