#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SATAELogicIR.h"
#include "SekiroAnimLogicData.generated.h"

// ════ 帧级标志位枚举（对应 TAE JumpTable ID）══════════════
// 打包为 UAnimSequence 上的一条 Integer Curve "FrameFlags"。
UENUM(BlueprintType, meta = (Bitflags))
enum class ESKFrameFlag : uint8
{
    DisableTurning      = 0,    // bit0,  JT=7
    DisableMovement     = 1,    // bit1,  JT=89
    DisableMapHit       = 2,    // bit2,  JT=19
    EnableParry         = 3,    // bit3,  JT=119
    DisableParry        = 4,    // bit4,  JT=137
    DisableSpecial      = 5,    // bit5,  JT=133
    DisableItem         = 6,    // bit6,  JT=134
    Invincible          = 7,    // bit7,  JT=51
    SetNoGravity        = 8,    // bit8,  JT=27
    FlagAsDodging       = 9,    // bit9,  JT=8
    InvokeDeath         = 10,   // bit10, JT=12
    LimitMoveSpeedWalk  = 11,   // bit11, JT=90
    LimitMoveSpeedDash  = 12,   // bit12, JT=91
    EnterMovement       = 13,   // bit13, JT=32
    ExitMovement        = 14,   // bit14, JT=31
    Staggered           = 15,   // bit15, JT=55
};

// ════ Cancel 动作枚举（对应 TAE JumpTable 取消事件）════════
// 打包为 UAnimSequence 上的一条 Integer Curve "CancelActions"。
// 值 = CancelActionID，0 = 无取消。
UENUM(BlueprintType)
enum class ESKCancelAction : uint8
{
    None        = 0,
    Attack      = 1,    // JT=115(R1CancelEnd), JT=26(GenericCancelStart)
    Guard       = 2,    // JT=117(L1CancelEnd)
    Dodge       = 3,    // JT=25(DodgeCancelStart)
    Prosthetic  = 4,    // JT=118(L2CancelEnd)
    Item        = 5,    // JT=154(ItemUseWindow)
};

// ════ 攻击框类型枚举（对应 TAE AttackBehavior）════════════
// 打包为 UAnimSequence 上的一条 Integer Curve "AttackHitbox"。
// 值 = 0 表示无攻击框，非 0 表示当前帧有活跃攻击框。
UENUM(BlueprintType)
enum class ESKAttackHitboxType : uint8
{
    None        = 0,
    Standard    = 1,    // 标准攻击
    Thrust      = 2,    // 突刺
    Sweep       = 3,    // 横扫
    ForwardR1   = 4,    // 前R1
    Plunging    = 5,    // 下落攻击
};

USTRUCT(BlueprintType)
struct FSKAnimIDList
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<int32> IDs;
};

UCLASS(BlueprintType)
class SEKIROASSETMANAGER_API USKAnimationLogicData : public UDataAsset
{
    GENERATED_BODY()

public:
    // AnimPrefixMap: AnimID → 动画前缀 (a000, a010, a200...)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
    TMap<int32, FString> AnimPrefixMap;

    // CategoryAnimMap: 类别名称 → AnimID 列表（用于 ResolveAnimID fallback）
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
    TMap<FString, FSKAnimIDList> CategoryAnimMap;

    // BehaviorParam: AnimID → behavior config (AtkParam/Bullet/SpEffect refs)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
    TMap<int32, FSAAnimBehaviorIR> BehaviorParamMap;

    // 运行时动画资产路径配置
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
    FString AnimAssetBasePath = TEXT("/Game/Characters/Sekiro/Animations");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meta")
    FString AnimAssetNamePrefix = TEXT("Anim_Sekiro");

    UFUNCTION(BlueprintCallable, Category = "Animation Logic")
    FString BuildAnimAssetPath(int32 AnimID) const;
};
