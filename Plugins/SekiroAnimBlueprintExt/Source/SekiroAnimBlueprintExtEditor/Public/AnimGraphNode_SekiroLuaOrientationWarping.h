#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNodes/AnimNode_SekiroLuaOrientationWarping.h"
#include "AnimGraphNode_SekiroLuaOrientationWarping.generated.h"

UCLASS()
class SEKIROANIMBLUEPRINTEXTEDITOR_API UAnimGraphNode_SekiroLuaOrientationWarping : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = Settings)
    FAnimNode_SekiroLuaOrientationWarping Node; // Lua 策略驱动的原生方向扭转节点

    /**
     * 获取节点在动画蓝图图表中显示的标题。
     * @param TitleType ENodeTitleType::Type，编辑器请求的标题显示形式。
     * @return FText，Lua 方向扭转节点标题。
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
     * 在动画蓝图编译期间检查方向扭转所需的脊柱和 IK 足部骨骼配置。
     * @param ForSkeleton USkeleton*，当前动画蓝图使用的骨架，可为空。
     * @param MessageLog FCompilerResultsLog&，用于输出编译警告的结果日志。
     * @return void，无返回值。
     */
    virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;

protected:
    /**
     * 获取骨骼控制节点在编辑器中显示的控制器名称。
     * 参数：无。
     * @return FText，Lua 方向扭转控制器名称。
     */
    virtual FText GetControllerDescription() const override;

    /**
     * 获取该编辑器节点持有的运行时骨骼控制节点。
     * 参数：无。
     * @return const FAnimNode_SkeletalControlBase*，指向 Node 成员的只读基类指针。
     */
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override;
};
