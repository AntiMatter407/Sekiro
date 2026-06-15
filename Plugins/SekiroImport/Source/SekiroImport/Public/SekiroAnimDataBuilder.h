#pragma once

#include "CoreMinimal.h"

class USKAnimationLogicData;
struct FSKAnimLogicImportResult;

/// IR → DataAsset 构建器：将 ABIR 序列化为 USKAnimationLogicData 资产
class SEKIROIMPORT_API FSekiroAnimDataBuilder
{
public:
    /// 从 IR 构建 DataAsset（Outer 必须为非瞬态 Package）
    static USKAnimationLogicData* BuildDataAsset(const FSKAnimLogicImportResult& IR, UObject* Outer);

private:
    /// 取消窗口 → CancelRules
    static void BuildCancelRules(const FSKAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// 攻击盒 → AttackHitboxConfigs（多盒支持）
    static void BuildAttackHitboxes(const FSKAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// SpEffect → SpEffectConfigs
    static void BuildSpEffects(const FSKAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// JumpTableFlags → AnimFrameData（关键帧压缩存储）
    static void BuildFrameFlags(const FSKAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// AnimName / InferredCategory → AnimNameMap / CategoryAnimMap
    static void BuildNameMaps(const FSKAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);
};
