#pragma once

#include "CoreMinimal.h"

class USKAnimationLogicData;

// ════ 帧级行为标志位掩码（SATAEImporter.cpp 和后续写曲线用）═════
namespace SekiroJTFlags
{
    static constexpr uint32 Bit_DisableTurning      = 1 << 0;
    static constexpr uint32 Bit_DisableMovement     = 1 << 1;
    static constexpr uint32 Bit_DisableMapHit       = 1 << 2;
    static constexpr uint32 Bit_EnableParry         = 1 << 3;
    static constexpr uint32 Bit_DisableParry        = 1 << 4;
    static constexpr uint32 Bit_DisableSpecial      = 1 << 5;
    static constexpr uint32 Bit_DisableItem         = 1 << 6;
    static constexpr uint32 Bit_Invincible          = 1 << 7;
    static constexpr uint32 Bit_SetNoGravity        = 1 << 8;
    static constexpr uint32 Bit_FlagAsDodging       = 1 << 9;
    static constexpr uint32 Bit_InvokeDeath         = 1 << 10;
    static constexpr uint32 Bit_LimitMoveSpeedWalk  = 1 << 11;
    static constexpr uint32 Bit_LimitMoveSpeedDash  = 1 << 12;
    static constexpr uint32 Bit_EnterMovement       = 1 << 13;
    static constexpr uint32 Bit_ExitMovement        = 1 << 14;
    static constexpr uint32 Bit_Staggered           = 1 << 15;
}
struct FSAAnimLogicImportResult;

/// IR → DataAsset 构建器：将 ABIR 序列化为 USKAnimationLogicData 资产
class SEKIROASSETMANAGER_API FSATAELogicBuilder
{
public:
    static USKAnimationLogicData* BuildDataAsset(const FSAAnimLogicImportResult& IR, UObject* Outer);

private:
    static void BuildNameMaps(const FSAAnimLogicImportResult& IR, USKAnimationLogicData* DataAsset);
};
