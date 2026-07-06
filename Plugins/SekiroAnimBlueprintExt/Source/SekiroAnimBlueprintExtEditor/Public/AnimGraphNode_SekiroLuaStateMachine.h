#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "AnimNodes/AnimNode_SekiroLuaStateMachine.h"
#include "AnimGraphNode_SekiroLuaStateMachine.generated.h"

UCLASS()
class SEKIROANIMBLUEPRINTEXTEDITOR_API UAnimGraphNode_SekiroLuaStateMachine : public UAnimGraphNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = Settings)
    FAnimNode_SekiroLuaStateMachine Node; // Lua 状态机运行时节点

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual FString GetNodeCategory() const override;
    virtual FText GetMenuCategory() const override;
};
