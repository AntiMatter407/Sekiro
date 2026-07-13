#include "AnimGraphNode_SekiroLuaStateMachine.h"

#include "Animation/AnimNode_Inertialization.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_SekiroLuaStateMachine"

FText UAnimGraphNode_SekiroLuaStateMachine::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("NodeTitle", "Sekiro Lua Anim Blueprint Host");
}

FText UAnimGraphNode_SekiroLuaStateMachine::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "Hosts a Lua-authored animation blueprint flow and evaluates the resulting Sekiro animation snapshot.");
}

FLinearColor UAnimGraphNode_SekiroLuaStateMachine::GetNodeTitleColor() const
{
    return FLinearColor(0.16f, 0.42f, 0.52f);
}

FString UAnimGraphNode_SekiroLuaStateMachine::GetNodeCategory() const
{
    return TEXT("Sekiro|Lua Animation");
}

FText UAnimGraphNode_SekiroLuaStateMachine::GetMenuCategory() const
{
    return LOCTEXT("MenuCategory", "Sekiro|Lua Animation");
}

void UAnimGraphNode_SekiroLuaStateMachine::GetOutputLinkAttributes(FNodeAttributeArray& OutAttributes) const
{
    Super::GetOutputLinkAttributes(OutAttributes);
    OutAttributes.Add(UE::Anim::IInertializationRequester::Attribute);
}

#undef LOCTEXT_NAMESPACE
