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
 * 动画蓝图操作工具（通用接口）
 *
 * 支持：创建/编译 AnimBlueprint（可选 parent_class）、状态机管理（状态/转换）、
 * 动画节点操作（SequencePlayer/BlendSpacePlayer）、AnimGraph 根节点连接、
 * BlendSpace 资产创建。
 *
 * C++ 层只提供通用接口，具体工作流（路径、类名、参数）由脚本层编排。
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
    FString HandleDeleteTransition(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleAddAnimNode(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCompile(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleSetupAnimGraph(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleCreateBlendSpace(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleSetAnimClass(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleLayout(const TSharedPtr<FJsonObject>& Args, FString& OutError);
    FString HandleRenameNode(const TSharedPtr<FJsonObject>& Args, FString& OutError);

    UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath, FString& OutError);
    UAnimGraphNode_StateMachine* FindOrCreateStateMachineNode(UAnimBlueprint* AnimBP, FString& OutError);
    UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName) const;
    FString AnimBlueprintToJson(UAnimBlueprint* AnimBP) const;

    // 设置转换规则条件（bool/not_bool/float_compare/and/time_remaining）
    bool SetupTransitionCondition(UAnimStateTransitionNode* TransNode, const TSharedPtr<FJsonObject>& ConditionObj, FString& OutError);
    // 递归构建条件节点链，返回最终 bool 输出引脚（供 and 组合使用）
    UEdGraphPin* CreateConditionOutput(class UAnimationTransitionGraph* TransGraph, const TSharedPtr<FJsonObject>& ConditionObj, int32& NodePosX, int32& NodePosY, FString& OutError);
    // 在状态内为 BlendSpacePlayer 连接参数引脚（X→Angle, Y→Speed）
    void SetupBlendSpacePinConnections(class UAnimGraphNode_BlendSpacePlayer* BspNode, UEdGraph* StateGraph, const TSharedPtr<FJsonObject>& PinConns);
};
