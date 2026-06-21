#include "SATAELogicBuilder.h"
#include "SATAELogicIR.h"
#include "SekiroAnimLogicData.h"

// ============================================================================
// 帧级行为标志位掩码（定义�?SATAELogicBuilder.h �?SekiroJTFlags 命名空间�?
// ============================================================================
using namespace SekiroJTFlags;

namespace
{
    /// 将位掩码转换�?FSKFrameFlags 位域
    FSKFrameFlags BitmaskToFrameFlags(uint32 Mask)
    {
        FSKFrameFlags Flags;
        Flags.bDisableTurning      = (Mask & Bit_DisableTurning)      != 0;
        Flags.bDisableMovement     = (Mask & Bit_DisableMovement)     != 0;
        Flags.bDisableMapHit       = (Mask & Bit_DisableMapHit)       != 0;
        Flags.bEnableParry         = (Mask & Bit_EnableParry)         != 0;
        Flags.bDisableParry        = (Mask & Bit_DisableParry)        != 0;
        Flags.bDisableSpecial      = (Mask & Bit_DisableSpecial)      != 0;
        Flags.bDisableItem         = (Mask & Bit_DisableItem)         != 0;
        Flags.bInvincible          = (Mask & Bit_Invincible)          != 0;
        Flags.bSetNoGravity        = (Mask & Bit_SetNoGravity)        != 0;
        Flags.bFlagAsDodging       = (Mask & Bit_FlagAsDodging)       != 0;
        Flags.bInvokeDeath         = (Mask & Bit_InvokeDeath)         != 0;
        Flags.bLimitMoveSpeedWalk  = (Mask & Bit_LimitMoveSpeedWalk)  != 0;
        Flags.bLimitMoveSpeedDash  = (Mask & Bit_LimitMoveSpeedDash)  != 0;
        Flags.bEnterMovement       = (Mask & Bit_EnterMovement)       != 0;
        Flags.bExitMovement        = (Mask & Bit_ExitMovement)        != 0;
        Flags.bStaggered           = (Mask & Bit_Staggered)           != 0;
        return Flags;
    }
}

// ============================================================================
// 公共入口
// ============================================================================

USKAnimationLogicData* FSATAELogicBuilder::BuildDataAsset(const FSAAnimLogicImportResult& IR,
    UObject* Outer)
{
    if (!Outer)
    {
        UE_LOG(LogTemp, Error, TEXT("[AnimDataBuilder] Outer 为空，无法创�?DataAsset"));
        return nullptr;
    }

    USKAnimationLogicData* DA = NewObject<USKAnimationLogicData>(Outer, NAME_None, RF_Public | RF_Standalone);
    if (!DA)
    {
        UE_LOG(LogTemp, Error, TEXT("[AnimDataBuilder] NewObject 失败"));
        return nullptr;
    }

    BuildCancelRules(IR, DA);
    BuildAttackHitboxes(IR, DA);
    BuildSpEffects(IR, DA);
    BuildFrameFlags(IR, DA);
    BuildNameMaps(IR, DA);

    UE_LOG(LogTemp, Log, TEXT("[AnimDataBuilder] DataAsset 构建完成: %d 取消规则, %d 攻击�? %d SpEffect, %d 帧标�? %d 类别"),
        DA->CancelRules.Num(), DA->AttackHitboxConfigs.Num(), DA->SpEffectConfigs.Num(),
        DA->AnimFrameFlags.Num(), DA->CategoryAnimMap.Num());

    return DA;
}

// ============================================================================
// 取消规则构建
// ============================================================================

void FSATAELogicBuilder::BuildCancelRules(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;

        if (Logic.CancelWindows.Num() == 0)
            continue;

        FSKCancelRuleList RuleList;
        for (const FSACancelWindowIR& Win : Logic.CancelWindows)
        {
            FSKCancelRule Rule;
            Rule.StartFrame = Win.StartFrame;
            Rule.EndFrame = Win.EndFrame;
            Rule.TargetAction = Win.TargetAction;
            Rule.CrossfadeDuration = Win.CrossfadeDuration;
            Rule.Priority = 0;
            RuleList.Rules.Add(Rule);
        }
        DataAsset->CancelRules.Add(AnimID, RuleList);
    }
}

// ============================================================================
// 攻击盒构建（多盒支持�?
// ============================================================================

void FSATAELogicBuilder::BuildAttackHitboxes(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;

        if (Logic.AttackHitboxes.Num() == 0)
            continue;

        FSKAttackHitboxList HitboxList;
        for (const FSAAttackHitboxIR& Hitbox : Logic.AttackHitboxes)
        {
            FSKAttackHitboxConfig Cfg;
            Cfg.StartFrame = Hitbox.StartFrame;
            Cfg.EndFrame = Hitbox.EndFrame;
            Cfg.BehaviorJudgeID = Hitbox.BehaviorJudgeID;
            Cfg.AttackType = Hitbox.AttackType;
            HitboxList.Hitboxes.Add(Cfg);
        }
        DataAsset->AttackHitboxConfigs.Add(AnimID, HitboxList);
    }
}

// ============================================================================
// SpEffect 构建
// ============================================================================

void FSATAELogicBuilder::BuildSpEffects(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;

        if (Logic.SpEffects.Num() == 0)
            continue;

        // 当前�?SpEffect 模式：取第一个状态效�?
        const FSASpEffectIR& SpEff = Logic.SpEffects[0];
        FSKSpEffectConfig Cfg;
        Cfg.SpEffectID = SpEff.SpEffectID;
        Cfg.StartFrame = SpEff.StartFrame;
        Cfg.EndFrame = SpEff.EndFrame;
        DataAsset->SpEffectConfigs.Add(AnimID, Cfg);
    }
}

// ============================================================================
// 帧级标志构建（关键帧压缩�?
// ============================================================================

void FSATAELogicBuilder::BuildFrameFlags(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;

        if (Logic.JumpTableFlags.Num() == 0)
            continue;

        // 收集所有帧号并排序
        TArray<int32> SortedFrames;
        Logic.JumpTableFlags.GetKeys(SortedFrames);
        SortedFrames.Sort();

        // 遍历帧，仅在位掩码变化时记录关键�?
        FSKAnimFrameData FrameData;
        int32 PrevMask = -1;
        for (int32 Frame : SortedFrames)
        {
            int32 Mask = Logic.JumpTableFlags[Frame];
            if (Mask != PrevMask)
            {
                FrameData.KeyFrames.Add(Frame);
                FrameData.Flags.Add(BitmaskToFrameFlags(static_cast<uint32>(Mask)));
                PrevMask = Mask;
            }
        }

        if (FrameData.KeyFrames.Num() > 0)
        {
            DataAsset->AnimFrameFlags.Add(AnimID, FrameData);
        }
    }
}

// ============================================================================
// 名称映射构建
// ============================================================================

void FSATAELogicBuilder::BuildNameMaps(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;
        // AnimID → 动画前缀
        DataAsset->AnimPrefixMap.Add(AnimID, Logic.AnimPrefix);

        // 类别 → AnimID 列表（方向子类同时加入基础类别作为fallback）
        const FString& Category = Logic.InferredCategory;
        auto AddToCategory = [DataAsset](const FString& Cat, int32 ID)
        {
            FSKAnimIDList* IDList = DataAsset->CategoryAnimMap.Find(Cat);
            if (!IDList)
            {
                DataAsset->CategoryAnimMap.Add(Cat, FSKAnimIDList());
                IDList = DataAsset->CategoryAnimMap.Find(Cat);
            }
            if (IDList)
            {
                IDList->IDs.AddUnique(ID);
            }
        };
        AddToCategory(Category, AnimID);
        // 方向子类同时加入基础类别（如 Dodge_Fwd → 也加入 Dodge）
        int32 UnderscoreIdx;
        if (Category.FindLastChar(TEXT("_"), UnderscoreIdx))
        {
            FString BaseCategory = Category.Left(UnderscoreIdx);
            AddToCategory(BaseCategory, AnimID);
        }
        }
    }
}
