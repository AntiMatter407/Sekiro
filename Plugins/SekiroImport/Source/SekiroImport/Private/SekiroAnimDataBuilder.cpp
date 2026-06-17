#include "SekiroAnimDataBuilder.h"
#include "SekiroAnimLogicIR.h"
#include "SekiroAnimLogicData.h"
#include "SekiroImportLog.h"

// ============================================================================
// 帧级行为标志位掩码（定义在 SekiroAnimDataBuilder.h 的 SekiroJTFlags 命名空间）
// ============================================================================
using namespace SekiroJTFlags;

namespace
{
    /// 将位掩码转换为 FSKFrameFlags 位域
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

USKAnimationLogicData* FSekiroAnimDataBuilder::BuildDataAsset(const FSKAnimLogicImportResult& IR,
    UObject* Outer)
{
    if (!Outer)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimDataBuilder] Outer 为空，无法创建 DataAsset"));
        return nullptr;
    }

    USKAnimationLogicData* DA = NewObject<USKAnimationLogicData>(Outer, NAME_None, RF_Public | RF_Standalone);
    if (!DA)
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[AnimDataBuilder] NewObject 失败"));
        return nullptr;
    }

    BuildCancelRules(IR, DA);
    BuildAttackHitboxes(IR, DA);
    BuildSpEffects(IR, DA);
    BuildFrameFlags(IR, DA);
    BuildNameMaps(IR, DA);

    UE_LOG(LogSekiroImport, Log, TEXT("[AnimDataBuilder] DataAsset 构建完成: %d 取消规则, %d 攻击盒, %d SpEffect, %d 帧标志, %d 类别"),
        DA->CancelRules.Num(), DA->AttackHitboxConfigs.Num(), DA->SpEffectConfigs.Num(),
        DA->AnimFrameFlags.Num(), DA->CategoryAnimMap.Num());

    return DA;
}

// ============================================================================
// 取消规则构建
// ============================================================================

void FSekiroAnimDataBuilder::BuildCancelRules(const FSKAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSKAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSKAnimationLogicIR& Logic = Pair.Value;

        if (Logic.CancelWindows.Num() == 0)
            continue;

        FSKCancelRuleList RuleList;
        for (const FSKCancelWindowIR& Win : Logic.CancelWindows)
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
// 攻击盒构建（多盒支持）
// ============================================================================

void FSekiroAnimDataBuilder::BuildAttackHitboxes(const FSKAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSKAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSKAnimationLogicIR& Logic = Pair.Value;

        if (Logic.AttackHitboxes.Num() == 0)
            continue;

        FSKAttackHitboxList HitboxList;
        for (const FSKAttackHitboxIR& Hitbox : Logic.AttackHitboxes)
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

void FSekiroAnimDataBuilder::BuildSpEffects(const FSKAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSKAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSKAnimationLogicIR& Logic = Pair.Value;

        if (Logic.SpEffects.Num() == 0)
            continue;

        // 当前单 SpEffect 模式：取第一个状态效果
        const FSKSpEffectIR& SpEff = Logic.SpEffects[0];
        FSKSpEffectConfig Cfg;
        Cfg.SpEffectID = SpEff.SpEffectID;
        Cfg.StartFrame = SpEff.StartFrame;
        Cfg.EndFrame = SpEff.EndFrame;
        DataAsset->SpEffectConfigs.Add(AnimID, Cfg);
    }
}

// ============================================================================
// 帧级标志构建（关键帧压缩）
// ============================================================================

void FSekiroAnimDataBuilder::BuildFrameFlags(const FSKAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSKAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSKAnimationLogicIR& Logic = Pair.Value;

        if (Logic.JumpTableFlags.Num() == 0)
            continue;

        // 收集所有帧号并排序
        TArray<int32> SortedFrames;
        Logic.JumpTableFlags.GetKeys(SortedFrames);
        SortedFrames.Sort();

        // 遍历帧，仅在位掩码变化时记录关键帧
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

void FSekiroAnimDataBuilder::BuildNameMaps(const FSKAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSKAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSKAnimationLogicIR& Logic = Pair.Value;

        // AnimID → 可读名称
        DataAsset->AnimNameMap.Add(AnimID, Logic.AnimName);

        // 类别 → AnimID 列表
        const FString& Category = Logic.InferredCategory;
        FSKAnimIDList* IDList = DataAsset->CategoryAnimMap.Find(Category);
        if (!IDList)
        {
            DataAsset->CategoryAnimMap.Add(Category, FSKAnimIDList());
            IDList = DataAsset->CategoryAnimMap.Find(Category);
        }
        if (IDList)
        {
            IDList->IDs.Add(AnimID);
        }
    }
}
