#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKAnimBlueprintTool.generated.h"

class FJsonObject;
class UAnimBlueprint;
class UEdGraph;
class UAnimGraphNode_StateMachine;
class UAnimationStateMachineGraph;
class UAnimStateNode;
class UAnimStateTransitionNode;

/**
 * 动画蓝图操作工具
 *
 * 支持创建动画蓝图、管理状态机（添加状态/转换）、
 * 以及向状态的动画图中添加动画节点（SequencePlayer/BlendSpacePlayer）。
 *
 * 风险：高（修改动画蓝图图结构，可能影响动画逻辑）。
 */
UCLASS()
class SEKIROAIBRIDGE_API USKAnimBlueprintTool : public UObject, public ISKAIToolInterface
{
    GENERATED_BODY()

public:
    virtual FName GetToolName() const override { return FName(TEXT("anim_blueprint")); }
    virtual FString GetToolDescription() const override;
    virtual FString GetInputSchemaJson() const override;
    virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
    virtual bool RequiresConfirmation() const override { return true; }
    virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

private:
    FString HandleCreate(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddState(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddTransition(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddAnimNode(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCompile(const TSharedPtr<FJsonObject>& Args, FString& OutError);

    UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath, FString& OutError);
    UAnimGraphNode_StateMachine* FindOrCreateStateMachineNode(UAnimBlueprint* AnimBP, FString& OutError);
    UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName) const;
    FString AnimBlueprintToJson(UAnimBlueprint* AnimBP) const;
};
