#include "SATAELogicBuilder.h"
#include "SATAELogicIR.h"
#include "SekiroAnimLogicData.h"

// ============================================================================
// 公共入口
// ============================================================================

USKAnimationLogicData* FSATAELogicBuilder::BuildDataAsset(const FSAAnimLogicImportResult& IR,
    UObject* Outer)
{
    if (!Outer)
    {
        UE_LOG(LogTemp, Error, TEXT("[AnimDataBuilder] Outer 为空，无法创建 DataAsset"));
        return nullptr;
    }

    USKAnimationLogicData* DA = NewObject<USKAnimationLogicData>(Outer, NAME_None, RF_Public | RF_Standalone);
    if (!DA)
    {
        UE_LOG(LogTemp, Error, TEXT("[AnimDataBuilder] NewObject 失败"));
        return nullptr;
    }

    BuildNameMaps(IR, DA);

    UE_LOG(LogTemp, Log, TEXT("[AnimDataBuilder] DataAsset 构建完成: %d 类别"),
        DA->CategoryAnimMap.Num());

    return DA;
}

// ============================================================================
// 名称映射构建
// CancelRules、AttackHitboxConfigs、SpEffectConfigs、AnimFrameFlags 已废弃。
// 相关数据改为每条 UAnimSequence 上的 Animation Curve：
//   "FrameFlags"     — 帧级行为标志位掩码 (int, ESKFrameFlag bits)
//   "CancelActions"  — 可取消的动作类型 (int, ESKCancelAction)
//   "AttackHitbox"   — 攻击框类型 (int, ESKAttackHitboxType)
// 写入曲线逻辑在导入管线的后续阶段完成。
// ============================================================================

void FSATAELogicBuilder::BuildNameMaps(const FSAAnimLogicImportResult& IR,
    USKAnimationLogicData* DataAsset)
{
    for (const TTuple<int32, FSAAnimationLogicIR>& Pair : IR.AnimLogicMap)
    {
        int32 AnimID = Pair.Key;
        const FSAAnimationLogicIR& Logic = Pair.Value;
        DataAsset->AnimPrefixMap.Add(AnimID, Logic.AnimPrefix);

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

        int32 UnderscoreIdx;
        if (Category.FindLastChar('_', UnderscoreIdx))
        {
            FString BaseCategory = Category.Left(UnderscoreIdx);
            AddToCategory(BaseCategory, AnimID);
        }

    // Build BehaviorParamMap from IR
    for (const TTuple<int32, FSAAnimBehaviorIR>& BehaviorPair : IR.BehaviorConfigs)
    {
        DataAsset->BehaviorParamMap.Add(BehaviorPair.Key, BehaviorPair.Value);
    }
    UE_LOG(LogTemp, Log, TEXT("[AnimDataBuilder] BehaviorParamMap: %d entries"), DataAsset->BehaviorParamMap.Num());
    }
}
