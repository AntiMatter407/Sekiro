#pragma once

#include "CoreMinimal.h"
#include "SekiroAnimLogicIR.h"

class UAnimBlueprint;
class UAnimSequence;
class USkeleton;
class USKAnimationLogicData;
class UAnimNotify;
class UBlendSpace1D;

/// AnimBlueprint 生成器：从 ABIR 创建/更新 UE AnimBlueprint 及相关资产
/// 依赖 Editor 模块（UnrealEd, AnimGraph, BlueprintGraph）
class SEKIROIMPORT_API FSekiroAnimBlueprintBuilder
{
public:
    struct FBuildResult
    {
        bool bSuccess = false;
        FString AnimBlueprintPath;
        FString DataAssetPath;
        int32 StateCount = 0;
        int32 TransitionCount = 0;
        int32 NotifyCount = 0;
    };

    /// 从 ABIR 构建/更新 AnimBlueprint（自动检测已存在则加载，否则创建）
    /// @param IR 动画逻辑中间表示
    /// @param Skeleton 目标骨架资产
    /// @param AnimSequences AnimID → UAnimSequence 映射（需已导入）
    /// @param OutputBasePath UE 内容路径，如 /Game/Characters/Sekiro/
    static FBuildResult Build(
        const FSKAnimLogicImportResult& IR,
        USkeleton* Skeleton,
        const TMap<int32, UAnimSequence*>& AnimSequences,
        const FString& OutputBasePath);

    /// 仅生成 DataAsset（不创建 AnimBlueprint）
    static USKAnimationLogicData* BuildDataAsset(
        const FSKAnimLogicImportResult& IR,
        const FString& PackagePath);

    /// 移除未在 IR 中出现的动画的旧 Notify
    static int32 CleanupNotifies(UAnimSequence* Sequence);

private:
    /// 创建或加载 AnimBlueprint 资产（优先加载已有）
    static UAnimBlueprint* LoadOrCreateAnimBlueprint(const FString& PackagePath, USkeleton* Skeleton);

    /// 加载已有 AnimBlueprint 资产
    static UAnimBlueprint* LoadExistingAnimBlueprint(const FString& PackagePath);

    /// 创建新 AnimBlueprint 资产
    static UAnimBlueprint* CreateAnimBlueprint(const FString& PackagePath, USkeleton* Skeleton);

    /// 在 AnimBlueprint 中构建状态机
    static bool BuildStateMachine(UAnimBlueprint* AnimBP,
        const FSKStateMachineIR& SM,
        const TMap<int32, UAnimSequence*>& AnimSequences);

    /// 创建 Locomotion BlendSpace1D（Speed 轴 0→600）
    /// @param CategoryAnimIDs 分类→AnimID 映射（来自 FSKStateMachineIR）
    static UBlendSpace1D* BuildLocomotionBlendSpace(
        UAnimBlueprint* AnimBP, USkeleton* Skeleton,
        const TMap<int32, UAnimSequence*>& AnimSequences,
        const TMap<FString, TArray<int32>>& CategoryAnimIDs);

    /// 为单个动画序列注入 AnimNotify（从 TAE 事件）
    static int32 InjectNotifies(UAnimSequence* Sequence, const FSKAnimationLogicIR& Logic);

    /// 生成 AnimNotify 模板
    static UAnimNotify* CreateNotifyForEvent(const FSKTAEEventIR& Event, UObject* Outer);

    /// 生成 AnimNotifyState 模板（区间事件）
    static UAnimNotifyState* CreateNotifyStateForEvent(const FSKTAEEventIR& Event, UObject* Outer);
};
