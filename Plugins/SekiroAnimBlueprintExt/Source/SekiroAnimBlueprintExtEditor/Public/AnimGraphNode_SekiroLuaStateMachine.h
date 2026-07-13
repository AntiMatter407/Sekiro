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

    /**
     * 获取节点在动画蓝图图表中显示的标题。
     * @param TitleType ENodeTitleType::Type，编辑器请求的标题显示形式。
     * @return FText，Lua 动画蓝图宿主节点标题。
     */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

    /**
     * 获取鼠标悬停节点时显示的说明文本。
     * 参数：无。
     * @return FText，节点用途说明。
     */
    virtual FText GetTooltipText() const override;

    /**
     * 获取节点标题栏颜色。
     * 参数：无。
     * @return FLinearColor，节点标题栏使用的线性颜色。
     */
    virtual FLinearColor GetNodeTitleColor() const override;

    /**
     * 获取节点在旧版节点菜单中的分类路径。
     * 参数：无。
     * @return FString，节点菜单分类路径。
     */
    virtual FString GetNodeCategory() const override;

    /**
     * 获取节点在动画蓝图动作菜单中的本地化分类路径。
     * 参数：无。
     * @return FText，本地化后的节点菜单分类路径。
     */
    virtual FText GetMenuCategory() const override;

    /**
     * 声明输出 Pose 支持惯性化请求属性，使下游惯性化节点能够接收 Lua 状态切换请求。
     * @param OutAttributes FNodeAttributeArray&，用于追加输出链路属性的数组。
     * @return void，无返回值。
     */
    virtual void GetOutputLinkAttributes(FNodeAttributeArray& OutAttributes) const override;
};
