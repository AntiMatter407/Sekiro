#include "SekiroTAEImporter.h"
#include "SekiroAnimationNameMap.h"
#include "SekiroImportLog.h"
#include "Json.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// ============================================================================
// 公共 API
// ============================================================================

FSKAnimLogicImportResult FSekiroTAEImporter::ImportFromFile(const FString& JsonPath)
{
    FSKAnimLogicImportResult Result;

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *JsonPath))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[TAEImporter] 无法读取文件: %s"), *JsonPath);
        return Result;
    }
    return ImportFromString(JsonContent);
}

FSKAnimLogicImportResult FSekiroTAEImporter::ImportFromString(const FString& JsonContent)
{
    FSKAnimLogicImportResult Result;

    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObj))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[TAEImporter] JSON 解析失败"));
        return Result;
    }

    Result.TotalTaeFiles = RootObj->GetIntegerField(TEXT("TotalTaeFiles"));

    const TArray<TSharedPtr<FJsonValue>>* TaeFilesArr;
    if (!RootObj->TryGetArrayField(TEXT("TAE_Files"), TaeFilesArr))
    {
        UE_LOG(LogSekiroImport, Error, TEXT("[TAEImporter] JSON 缺少 TAE_Files 数组"));
        return Result;
    }

    // 遍历所有 TAE 文件
    for (const TSharedPtr<FJsonValue>& TaeFileVal : *TaeFilesArr)
    {
        const TSharedPtr<FJsonObject>& TaeFileObj = TaeFileVal->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* AnimsArr;
        if (!TaeFileObj->TryGetArrayField(TEXT("Animations"), AnimsArr)) continue;

        for (const TSharedPtr<FJsonValue>& AnimVal : *AnimsArr)
        {
            const TSharedPtr<FJsonObject>& AnimObj = AnimVal->AsObject();
            int32 AnimID = AnimObj->GetIntegerField(TEXT("AnimID"));

            // 通过 AnimationNameMap 获取可读名称
            FString RawName = FString::Printf(TEXT("Sekiro_a000_%06d"), AnimID);
            FString AnimName = FSekiroAnimationNameMap::Translate(RawName);
            // 未命中时使用类别前缀 + ID
            if (AnimName.IsEmpty())
                AnimName = FString::Printf(TEXT("%s_%06d"), *InferCategoryFromAnimID(AnimID), AnimID);

            FSKAnimationLogicIR AnimLogic = ParseAnimationEntry(AnimObj, AnimName);

            // 推断类别
            AnimLogic.InferredCategory = InferCategoryFromAnimID(AnimID);

            // 计算总帧数（从最后一个事件帧推断）
            for (const FSKTAEEventIR& Evt : AnimLogic.AllEvents)
            {
                AnimLogic.TotalFrames = FMath::Max(AnimLogic.TotalFrames, Evt.EndFrame);
            }

            Result.AnimLogicMap.Add(AnimID, AnimLogic);
            Result.TotalAnims++;
            Result.TotalEvents += AnimLogic.AllEvents.Num();
        }
    }

    // 构建状态机过渡规则
    BuildTransitions(Result.AnimLogicMap, Result.MainStateMachine);

    // 按类别分组
    for (const auto& Pair : Result.AnimLogicMap)
    {
        const FSKAnimationLogicIR& Logic = Pair.Value;
        FString Category = Logic.InferredCategory;
        if (!Result.MainStateMachine.AnimIDsByCategory.Contains(Category))
            Result.MainStateMachine.AnimIDsByCategory.Add(Category, {});
        Result.MainStateMachine.AnimIDsByCategory[Category].Add(Pair.Key);
    }

    UE_LOG(LogSekiroImport, Log, TEXT("[TAEImporter] 导入完成: %d 动画, %d 事件, %d 类别"),
        Result.TotalAnims, Result.TotalEvents, Result.MainStateMachine.AnimIDsByCategory.Num());

    return Result;
}

// ============================================================================
// 动画条目解析
// ============================================================================

FSKAnimationLogicIR FSekiroTAEImporter::ParseAnimationEntry(const TSharedPtr<FJsonObject>& AnimObj,
    const FString& AnimName)
{
    FSKAnimationLogicIR Logic;
    Logic.AnimID = AnimObj->GetIntegerField(TEXT("AnimID"));
    Logic.AnimName = AnimName;

    const TArray<TSharedPtr<FJsonValue>>* EventsArr;
    if (!AnimObj->TryGetArrayField(TEXT("Events"), EventsArr))
        return Logic;

    for (const TSharedPtr<FJsonValue>& EvtVal : *EventsArr)
    {
        const TSharedPtr<FJsonObject>& EvtObj = EvtVal->AsObject();

        FSKTAEEventIR Evt;
        Evt.Type = EvtObj->GetIntegerField(TEXT("Type"));
        Evt.TypeName = EvtObj->GetStringField(TEXT("TypeName"));
        Evt.StartTime = static_cast<float>(EvtObj->GetNumberField(TEXT("StartTime")));
        Evt.EndTime = static_cast<float>(EvtObj->GetNumberField(TEXT("EndTime")));
        Evt.StartFrame = EvtObj->GetIntegerField(TEXT("StartFrame"));
        Evt.EndFrame = EvtObj->GetIntegerField(TEXT("EndFrame"));
        Evt.Category = ClassifyEventType(Evt.Type, Evt.TypeName);

        // 提取命名参数
        const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
        if (EvtObj->TryGetObjectField(TEXT("Parameters"), ParamsObj))
        {
            for (const auto& ParamPair : (*ParamsObj)->Values)
            {
                FString ValueStr;
                const TSharedPtr<FJsonValue>& Val = ParamPair.Value;
                if (Val->Type == EJson::String)
                    ValueStr = Val->AsString();
                else if (Val->Type == EJson::Number)
                    ValueStr = FString::Printf(TEXT("%g"), Val->AsNumber());
                else if (Val->Type == EJson::Boolean)
                    ValueStr = Val->AsBool() ? TEXT("true") : TEXT("false");
                else if (Val->Type == EJson::Null)
                    ValueStr = TEXT("null");
                else
                    continue;

                Evt.Params.Add(ParamPair.Key, ValueStr);
            }
        }

        // 按类别分拣
        switch (Evt.Category)
        {
        case ESKTAEEventCategory::AttackBehavior:
        {
            FSKAttackHitboxIR Hitbox;
            Hitbox.StartFrame = Evt.StartFrame;
            Hitbox.EndFrame = Evt.EndFrame;
            if (Evt.Params.Contains(TEXT("BehaviorJudgeID")))
                Hitbox.BehaviorJudgeID = FCString::Atoi(*Evt.Params[TEXT("BehaviorJudgeID")]);
            if (Evt.Params.Contains(TEXT("AttackType")))
                Hitbox.AttackType = FCString::Atoi(*Evt.Params[TEXT("AttackType")]);
            if (Evt.Params.Contains(TEXT("Source")))
                Hitbox.Source = FCString::Atoi(*Evt.Params[TEXT("Source")]);
            Logic.AttackHitboxes.Add(Hitbox);
            break;
        }
        case ESKTAEEventCategory::AddSpEffect:
        {
            FSKSpEffectIR SpEff;
            SpEff.StartFrame = Evt.StartFrame;
            SpEff.EndFrame = Evt.EndFrame;
            if (Evt.Params.Contains(TEXT("SpEffectID")))
                SpEff.SpEffectID = FCString::Atoi(*Evt.Params[TEXT("SpEffectID")]);
            SpEff.bIsMultiplayerOnly = Evt.Type == 401;
            Logic.SpEffects.Add(SpEff);
            break;
        }
        case ESKTAEEventCategory::PlaySound:
        {
            FSKSEventIR Snd;
            Snd.Frame = Evt.StartFrame;
            if (Evt.Params.Contains(TEXT("SoundType")))
                Snd.SoundType = FCString::Atoi(*Evt.Params[TEXT("SoundType")]);
            if (Evt.Params.Contains(TEXT("SoundID")))
                Snd.SoundID = FCString::Atoi(*Evt.Params[TEXT("SoundID")]);
            if (Evt.Params.Contains(TEXT("DummyPolyID")))
                Snd.DummyPolyID = FCString::Atoi(*Evt.Params[TEXT("DummyPolyID")]);
            Logic.SoundEvents.Add(Snd);
            break;
        }
        case ESKTAEEventCategory::SpawnFFX:
        {
            FSKFFXEventIR FFX;
            FFX.StartFrame = Evt.StartFrame;
            FFX.EndFrame = Evt.EndFrame;
            if (Evt.Params.Contains(TEXT("FFXID")))
                FFX.FFXID = FCString::Atoi(*Evt.Params[TEXT("FFXID")]);
            if (Evt.Params.Contains(TEXT("DummyPolyID")))
                FFX.DummyPolyID = FCString::Atoi(*Evt.Params[TEXT("DummyPolyID")]);
            if (Evt.Params.Contains(TEXT("IsFollowDummyPoly")))
                FFX.bIsFollowDummyPoly = Evt.Params[TEXT("IsFollowDummyPoly")] == TEXT("true");
            if (Evt.Params.Contains(TEXT("IsRepeat")))
                FFX.bIsRepeat = Evt.Params[TEXT("IsRepeat")] == TEXT("true");
            Logic.FFXEvents.Add(FFX);
            break;
        }
        default:
            break;
        }

        Logic.AllEvents.Add(Evt);
    }

    // 从 JumpTable 事件提取取消窗口
    ExtractCancelWindows(Logic.AllEvents, Logic.CancelWindows);

    return Logic;
}

// ============================================================================
// 取消窗口提取
// ============================================================================

void FSekiroTAEImporter::ExtractCancelWindows(const TArray<FSKTAEEventIR>& Events,
    TArray<FSKCancelWindowIR>& OutWindows)
{
    // 收集所有 JumpTable 事件及其类型
    struct FCancelEvent
    {
        ESKJumpTableAction Action;
        int32 StartFrame;
        int32 EndFrame;
    };
    TArray<FCancelEvent> CancelStarts;
    TArray<FCancelEvent> CancelEnds;
    TMap<ESKJumpTableAction, int32> ActiveWindows; // Action → StartFrame

    for (const FSKTAEEventIR& Evt : Events)
    {
        if (Evt.Category != ESKTAEEventCategory::JumpTable)
            continue;

        int32 JumpTableID = 0;
        if (Evt.Params.Contains(TEXT("JumpTableID")))
            JumpTableID = FCString::Atoi(*Evt.Params[TEXT("JumpTableID")]);

        ESKJumpTableAction Action = MapJumpTableToAction(JumpTableID);

        if (Action == ESKJumpTableAction::None)
            continue;

        // 判断是 CancelStart 还是 CancelEnd
        FString TypeNameLower = Evt.TypeName.ToLower();
        bool bIsCancelStart = TypeNameLower.Contains(TEXT("cancelstart")) ||
            (JumpTableID == 1 || JumpTableID == 9 || JumpTableID == 21 ||
             JumpTableID == 25 || JumpTableID == 30 || JumpTableID == 105 ||
             JumpTableID == 111 || JumpTableID == 120);
        bool bIsCancelEnd = TypeNameLower.Contains(TEXT("cancelend")) ||
            TypeNameLower.Contains(TEXT("invokeanimcancelend")) ||
            (JumpTableID == 34 || JumpTableID == 115 || JumpTableID == 116 ||
             JumpTableID == 117 || JumpTableID == 118 || JumpTableID == 107 ||
             JumpTableID == 112 || JumpTableID == 121);

        if (bIsCancelStart)
        {
            FSKCancelWindowIR Window;
            Window.StartFrame = Evt.StartFrame;
            Window.EndFrame = Evt.EndFrame;
            Window.JumpAction = Action;
            Window.TargetAction = NAME_None;

            switch (Action)
            {
            case ESKJumpTableAction::AnimCancelStart_R1:
                Window.TargetAction = TEXT("Attack"); break;
            case ESKJumpTableAction::AnimCancelStart_L1:
            case ESKJumpTableAction::AnimCancelStart_Guard:
                Window.TargetAction = TEXT("Guard"); break;
            case ESKJumpTableAction::AnimCancelStart_Dodge:
            case ESKJumpTableAction::AnimCancelStart_Emergency:
                Window.TargetAction = TEXT("Dodge"); break;
            case ESKJumpTableAction::AnimCancelStart_Item:
                Window.TargetAction = TEXT("Item"); break;
            case ESKJumpTableAction::AnimCancelStart_L2:
                Window.TargetAction = TEXT("Prosthetic"); break;
            default:
                Window.TargetAction = FName(*FString::Printf(TEXT("Action_%d"), JumpTableID));
                break;
            }

            OutWindows.Add(Window);
        }
    }
}

// ============================================================================
// 过渡规则构建
// ============================================================================

void FSekiroTAEImporter::BuildTransitions(const TMap<int32, FSKAnimationLogicIR>& AnimLogicMap,
    FSKStateMachineIR& OutSM)
{
    OutSM.StateMachineName = TEXT("MainSM");

    // 构建按类别分组的 AnimID 列表（供后续查找目标状态）
    TMap<FString, TArray<int32>> CategoryAnimIDs;
    for (const auto& Pair : AnimLogicMap)
    {
        const FString& Cat = Pair.Value.InferredCategory;
        if (!CategoryAnimIDs.Contains(Cat))
            CategoryAnimIDs.Add(Cat, {});
        CategoryAnimIDs[Cat].Add(Pair.Key);
    }
    for (auto& CatPair : CategoryAnimIDs)
    {
        CatPair.Value.Sort();
    }

    for (const auto& Pair : AnimLogicMap)
    {
        const FSKAnimationLogicIR& Logic = Pair.Value;
        FString StateName = FString::Printf(TEXT("%s_%d"), *Logic.InferredCategory, Logic.AnimID);

        // 从 CancelWindow 生成过渡
        for (const FSKCancelWindowIR& Window : Logic.CancelWindows)
        {
            FSKSMTransitionIR Transition;
            Transition.FromAnimID = Logic.AnimID;
            Transition.FromState = FName(*StateName);
            Transition.CancelWindowStart = Window.StartFrame;
            Transition.CancelWindowEnd = Window.EndFrame;
            Transition.TriggerAction = Window.JumpAction;
            Transition.CrossfadeDuration = 0.1f;

            // 确定目标类别
            FString TargetCategory;
            switch (Window.JumpAction)
            {
            case ESKJumpTableAction::AnimCancelStart_R1:
                TargetCategory = TEXT("Attack"); Transition.Priority = 10; break;
            case ESKJumpTableAction::AnimCancelStart_L1:
            case ESKJumpTableAction::AnimCancelStart_Guard:
                TargetCategory = TEXT("Deflect"); Transition.Priority = 20; break;
            case ESKJumpTableAction::AnimCancelStart_Dodge:
            case ESKJumpTableAction::AnimCancelStart_Emergency:
                TargetCategory = TEXT("Dodge"); Transition.Priority = 25; break;
            case ESKJumpTableAction::AnimCancelStart_Item:
                TargetCategory = TEXT("Item"); Transition.Priority = 5; break;
            case ESKJumpTableAction::AnimCancelStart_L2:
                TargetCategory = TEXT("Prosthetic"); Transition.Priority = 8; break;
            default: break;
            }

            if (TargetCategory.IsEmpty()) continue;

            // 从目标类别中选第一个动画作为默认目标
            const TArray<int32>* TargetIDs = CategoryAnimIDs.Find(TargetCategory);
            if (TargetIDs && TargetIDs->Num() > 0)
                Transition.ToAnimID = (*TargetIDs)[0];

            Transition.ToState = FName(*FString::Printf(TEXT("%s_%d"), *TargetCategory, Transition.ToAnimID));
            OutSM.Transitions.Add(Transition);
        }
    }

    UE_LOG(LogSekiroImport, Log, TEXT("[TAEImporter] 构建 %d 条过渡规则"), OutSM.Transitions.Num());
}

// ============================================================================
// 分类与映射
// ============================================================================

ESKTAEEventCategory FSekiroTAEImporter::ClassifyEventType(int32 Type, const FString& TypeName)
{
    // 核心战斗事件
    if (Type == 0)   return ESKTAEEventCategory::JumpTable;
    if (Type == 1)   return ESKTAEEventCategory::AttackBehavior;
    if (Type == 2)   return ESKTAEEventCategory::BulletBehavior;
    if (Type == 5)   return ESKTAEEventCategory::CommonBehavior;

    // 状态效果
    if (Type == 66 || Type == 67) return ESKTAEEventCategory::AddSpEffect;

    // 视觉特效
    if ((Type >= 95 && Type <= 123) || Type == 108)
        return ESKTAEEventCategory::SpawnFFX;

    // 音效
    if (Type >= 128 && Type <= 132) return ESKTAEEventCategory::PlaySound;

    // 镜头震动
    if (Type >= 144 && Type <= 147) return ESKTAEEventCategory::RumbleCam;

    // 混合
    if (Type == 16) return ESKTAEEventCategory::Blend;

    // 行为标志
    if (Type == 300 || Type == 301) return ESKTAEEventCategory::BehaviorFlag;

    // 面部表情
    if (Type == 607) return ESKTAEEventCategory::FacialExpression;

    // 镜头
    FString Lower = TypeName.ToLower();
    if (Lower.Contains(TEXT("camera"))) return ESKTAEEventCategory::CameraModule;

    // 音效类
    if (Lower.Contains(TEXT("sound")) || Lower.Contains(TEXT("sfx")) || Lower.Contains(TEXT("foot")))
        return ESKTAEEventCategory::PlaySound;

    // 特效类
    if (Lower.Contains(TEXT("ffx")) || Lower.Contains(TEXT("spawnone")) || Lower.Contains(TEXT("spawnffx")))
        return ESKTAEEventCategory::SpawnFFX;

    // 转向速度
    if (Lower.Contains(TEXT("turn")) && Lower.Contains(TEXT("speed")))
        return ESKTAEEventCategory::SetTurnSpeed;

    return ESKTAEEventCategory::Other;
}

ESKJumpTableAction FSekiroTAEImporter::MapJumpTableToAction(int32 JumpTableID)
{
    switch (JumpTableID)
    {
    case 1:  return ESKJumpTableAction::AnimCancelStart_R1;
    case 5:  return ESKJumpTableAction::EnableParry;
    case 7:  return ESKJumpTableAction::DisableTurning;
    case 8:  return ESKJumpTableAction::FlagAsDodging;
    case 9:  return ESKJumpTableAction::AnimCancelStart_L1;
    case 12: return ESKJumpTableAction::InvokeDeath;
    case 19: return ESKJumpTableAction::DisableMapHit;
    case 21: return ESKJumpTableAction::AnimCancelStart_Guard;
    case 25: return ESKJumpTableAction::AnimCancelStart_Dodge;
    case 27: return ESKJumpTableAction::SetNoGravity;
    case 30: return ESKJumpTableAction::AnimCancelStart_Item;
    case 34: return ESKJumpTableAction::AnimCancelEnd_General;
    case 87: return ESKJumpTableAction::InvokeAttackAction;
    case 89: return ESKJumpTableAction::DisableAllMovement;
    case 90: return ESKJumpTableAction::LimitMoveSpeedWalk;
    case 91: return ESKJumpTableAction::LimitMoveSpeedDash;
    case 103: return ESKJumpTableAction::AnimCancelEnd_L2;
    case 105: return ESKJumpTableAction::AnimCancelStart_L2;
    case 107: return ESKJumpTableAction::AnimCancelEnd_Item;
    case 111: return ESKJumpTableAction::AnimCancelStart_Emergency;
    case 112: return ESKJumpTableAction::AnimCancelEnd_Emergency;
    case 113: return ESKJumpTableAction::SetHeightCorrection;
    case 115: return ESKJumpTableAction::AnimCancelEnd_R1;
    case 117: return ESKJumpTableAction::AnimCancelEnd_L1;
    case 118: return ESKJumpTableAction::AnimCancelEnd_L2;
    case 119: return ESKJumpTableAction::EnableParry;
    case 121: return ESKJumpTableAction::AnimCancelEnd_General;
    default:  return ESKJumpTableAction::None;
    }
}

FString FSekiroTAEImporter::InferCategoryFromAnimID(int32 AnimID)
{
    // 基于 FSekiroAnimationNameMap 的类别推断逻辑
    int32 HighPart = AnimID / 10000;
    int32 MidPart = (AnimID / 100) % 100;

    // a000 区域 (0-9999): 按百位细分
    if (HighPart == 0)
    {
        if (AnimID < 14)   return TEXT("Idle");        // 0-13: Idle_Default, Idle_Combat
        if (AnimID < 100)  return TEXT("Common");       // 14-99: 其他通用
        if (AnimID < 200)  return TEXT("Walk");         // 100-199: Walk_Fwd/Bwd/L/R
        if (AnimID < 300)  return TEXT("Jog");          // 200-299: Jog_Fwd/L/R
        if (AnimID < 400)  return TEXT("Sprint");       // 300-399: Sprint_Fwd/L/R
        if (AnimID < 500)  return TEXT("Run");          // 400-499: Run_Fast_Fwd/L/R
        if (AnimID < 700)  return TEXT("Locomotion");   // 500-699: Sprint_To_Idle 等过渡
        if (AnimID < 1000) return TEXT("Locomotion");   // 700-999: 其他移动
        // 5000-5999: WeaponPose / Turn — 属于战斗姿态，非移动
        if (AnimID >= 5000 && AnimID < 6000) return TEXT("Combat");
        return TEXT("Locomotion");                      // 1000-4999, 6000-9999
    }

    // a00_ 20xxxx: 攻击
    if (HighPart >= 20 && HighPart < 30) return TEXT("Attack");

    // a00_ 30xxxx: 防御/弹刀
    if (HighPart >= 30 && HighPart < 40)
    {
        if (MidPart < 10) return TEXT("Guard");
        if (MidPart < 20) return TEXT("Deflect");
        if (MidPart < 30) return TEXT("Mikiri");
        if (MidPart < 40) return TEXT("Sweep");
        if (MidPart < 50) return TEXT("Grab");
        return TEXT("Defense");
    }

    // a00_ 40xxxx: 受击/死亡
    if (HighPart >= 40 && HighPart < 50)
    {
        if (MidPart < 20) return TEXT("Hit");
        if (MidPart < 40) return TEXT("Knockback");
        if (MidPart < 60) return TEXT("Death");
        return TEXT("HitReaction");
    }

    // a00_ 50xxxx: 忍杀
    if (HighPart >= 50 && HighPart < 60) return TEXT("Deathblow");

    // a00_ 60xxxx: 回生/特殊
    if (HighPart >= 60 && HighPart < 70) return TEXT("Resurrection");

    // a00_ 70xxxx: 义手
    if (HighPart >= 70 && HighPart < 80) return TEXT("Prosthetic");

    // a00_ 80xxxx: 钩绳
    if (HighPart >= 80 && HighPart < 90) return TEXT("Grapple");

    // a00_ 90xxxx: 道具
    if (HighPart >= 90 && HighPart < 100) return TEXT("Item");

    // cxxxx 区域: 战斗艺术/特殊
    if (HighPart >= 100)
    {
        if (HighPart >= 110 && HighPart < 120) return TEXT("CombatArt");
        if (HighPart >= 121 && HighPart < 130) return TEXT("CombatArt");
        return TEXT("CombatArt");
    }

    return TEXT("Other");
}
