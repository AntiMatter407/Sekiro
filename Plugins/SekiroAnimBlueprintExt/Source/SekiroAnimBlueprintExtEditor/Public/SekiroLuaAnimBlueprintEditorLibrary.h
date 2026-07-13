#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SekiroLuaAnimBlueprintEditorLibrary.generated.h"

class UAnimBlueprint;
class UBlueprint;

UCLASS()
class SEKIROANIMBLUEPRINTEXTEDITOR_API USekiroLuaAnimBlueprintEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 自动配置第一阶段极简 Lua 动画蓝图宿主链。
     * 动画图的有效输出链不再需要手工承载状态机业务，只保留 Lua 宿主、方向扭转、惯性化和输出节点。
     * 方向扭转与惯性化仍由现有运行时节点执行，待 Lua Pose Graph 提供等价节点后再迁移。
     * @param AnimBlueprint UAnimBlueprint*，需要修改的动画蓝图，不能为空。
     * @param AnimationGraphName FName，目标动画图名称，不能为空名称。
     * @param LayerName FName，Lua 宿主及方向扭转节点读取的动画层名称。
     * @return bool，必要节点创建或复用、确定性连接并成功触发蓝图编译时返回 true，否则返回 false。
     */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool ConnectLuaAnimBlueprintHostToGraph(UAnimBlueprint* AnimBlueprint, FName AnimationGraphName, FName LayerName);

    /**
     * 配置指定 Lua 动画层方向扭转节点使用的骨骼和插值参数。
     * @param AnimBlueprint UAnimBlueprint*，包含目标方向扭转节点的动画蓝图，不能为空。
     * @param AnimationGraphName FName，目标动画图名称，不能为空名称。
     * @param LayerName FName，需要配置的 Lua 动画层名称。
     * @param SpineBoneNames const TArray<FName>&，参与方向分配的脊柱骨骼名称数组，空名称会被忽略。
     * @param IKFootRootBoneName FName，IK 足部层级根骨骼名称。
     * @param IKFootBoneNames const TArray<FName>&，参与方向扭转的 IK 足部骨骼名称数组，空名称会被忽略。
     * @param DistributedBoneOrientationAlpha float，方向旋转分配到脊柱骨骼的比例，最终限制在 [0, 1]。
     * @param RotationInterpSpeed float，方向扭转角度插值速度，最终限制为非负值。
     * @return bool，找到目标节点、写入配置并成功触发蓝图编译时返回 true，否则返回 false。
     */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool ConfigureLuaAnimOrientationWarping(
        UAnimBlueprint* AnimBlueprint,
        FName AnimationGraphName,
        FName LayerName,
        const TArray<FName>& SpineBoneNames,
        FName IKFootRootBoneName,
        const TArray<FName>& IKFootBoneNames,
        float DistributedBoneOrientationAlpha,
        float RotationInterpSpeed);

    /**
     * 为蓝图实现 UnLua 接口，并将 GetModuleName 的返回值设置为指定 Lua 模块名。
     * @param Blueprint UBlueprint*，需要绑定到 Lua 的蓝图，不能为空且必须已有生成类。
     * @param LuaModuleName const FString&，相对于 Content/Script 的 Lua 模块名，不能为空字符串。
     * @return bool，接口及模块名配置完成并成功触发蓝图编译时返回 true，否则返回 false。
     */
    UFUNCTION(BlueprintCallable, Category = "Sekiro|Lua Animation")
    static bool BindBlueprintToLuaModule(UBlueprint* Blueprint, const FString& LuaModuleName);
};
