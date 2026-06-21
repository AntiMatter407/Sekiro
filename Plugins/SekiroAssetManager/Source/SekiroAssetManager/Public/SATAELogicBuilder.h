#pragma once

#include "CoreMinimal.h"

class USKAnimationLogicData;
struct FSAAnimLogicImportResult;

// ============================================================================
// 帧级行为标志位掩码（共享定义，SATAEImporter.cpp 和 SATAELogicBuilder.cpp 共用）
// ============================================================================
namespace SekiroJTFlags
{
    static constexpr uint32 Bit_DisableTurning      = 1 << 0;              // JumpTableID 7
    static constexpr uint32 Bit_DisableMovement     = 1 << 1;              // JumpTableID 89
    static constexpr uint32 Bit_DisableMapHit       = 1 << 2;              // JumpTableID 19
    static constexpr uint32 Bit_EnableParry         = 1 << 3;              // JumpTableID 119
    static constexpr uint32 Bit_DisableParry        = 1 << 4;              // JumpTableID 137
    static constexpr uint32 Bit_DisableSpecial      = 1 << 5;              // JumpTableID 133
    static constexpr uint32 Bit_DisableItem         = 1 << 6;              // JumpTableID 134
    static constexpr uint32 Bit_Invincible          = 1 << 7;              // JumpTableID 51
    static constexpr uint32 Bit_SetNoGravity        = 1 << 8;              // JumpTableID 27
    static constexpr uint32 Bit_FlagAsDodging       = 1 << 9;              // JumpTableID 8
    static constexpr uint32 Bit_InvokeDeath         = 1 << 10;             // JumpTableID 12
    static constexpr uint32 Bit_LimitMoveSpeedWalk  = 1 << 11;             // JumpTableID 90
    static constexpr uint32 Bit_LimitMoveSpeedDash  = 1 << 12;             // JumpTableID 91
    static constexpr uint32 Bit_EnterMovement       = 1 << 13;             // JumpTableID 32
    static constexpr uint32 Bit_ExitMovement        = 1 << 14;             // JumpTableID 31
    static constexpr uint32 Bit_Staggered           = 1 << 15;             // JumpTableID 55
}

/// IR → DataAsset 构建器：将 ABIR 序列化为 USKAnimationLogicData 资产
class SEKIROASSETMANAGER_API FSATAELogicBuilder
{
public:
    /// 从 IR 构建 DataAsset（Outer 必须为非瞬态 Package）
    static USKAnimationLogicData* BuildDataAsset(const FSAAnimLogicImportResult& IR, UObject* Outer);

private:
    /// 取消窗口 → CancelRules
    static void BuildCancelRules(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// 攻击盒 → AttackHitboxConfigs（多盒支持）
    static void BuildAttackHitboxes(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// SpEffect → SpEffectConfigs
    static void BuildSpEffects(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// JumpTableFlags → AnimFrameData（关键帧压缩存储）
    static void BuildFrameFlags(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);

    /// InferredCategory → CategoryAnimMap
    static void BuildNameMaps(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);
};
