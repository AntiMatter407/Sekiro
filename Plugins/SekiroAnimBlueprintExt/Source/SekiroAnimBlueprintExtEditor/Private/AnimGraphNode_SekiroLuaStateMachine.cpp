#include "AnimGraphNode_SekiroLuaStateMachine.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_SekiroLuaStateMachine"

FText UAnimGraphNode_SekiroLuaStateMachine::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("NodeTitle", "Sekiro Lua State Machine");
}

FText UAnimGraphNode_SekiroLuaStateMachine::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "Evaluates animation states from a Lua-driven Sekiro animation snapshot.");
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

#undef LOCTEXT_NAMESPACE
