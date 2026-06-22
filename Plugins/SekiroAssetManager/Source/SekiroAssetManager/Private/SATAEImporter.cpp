#include "SATAEImporter.h"
#include "SATAELogicBuilder.h"     // 共享 Bit_* 位掩码常�?
#include "Json.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// ============================================================================
// 帧级行为标志位掩码（定义�?SATAELogicBuilder.h �?SekiroJTFlags 命名空间�?
// ============================================================================
using namespace SekiroJTFlags;

// ============================================================================
// 公共 API
// ============================================================================

FSAAnimLogicImportResult FSATAEImporter::ImportFromFile(const FString& JsonPath)
{
    FSAAnimLogicImportResult Result;

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[TAEImporter] 无法读取文件: %s"), *JsonPath);
        return Result;
    }
    return ImportFromString(JsonContent);
}

FSAAnimLogicImportResult FSATAEImporter::ImportFromString(const FString& JsonContent)
{
    FSAAnimLogicImportResult Result;

    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObj))
    {
        UE_LOG(LogTemp, Error, TEXT("[TAEImporter] JSON 解析失败"));
        return Result;
    }

    Result.TotalTaeFiles = RootObj->GetIntegerField(TEXT("TotalTaeFiles"));

    const TArray<TSharedPtr<FJsonValue>>* TaeFilesArr;
    if (!RootObj->TryGetArrayField(TEXT("TAE_Files"), TaeFilesArr))
    {
        UE_LOG(LogTemp, Error, TEXT("[TAEImporter] JSON 缺少 TAE_Files 数组"));
        return Result;
    }

    // 遍历所�?TAE 文件
    for (const TSharedPtr<FJsonValue>& TaeFileVal : *TaeFilesArr)
    {
        const TSharedPtr<FJsonObject>& TaeFileObj = TaeFileVal->AsObject();
        // �?TAE 文件名推导动画前缀: a00.tae �?a000, a10.tae �?a010, a250.tae �?a250
        FString TaeFileName = TaeFileObj->GetStringField(TEXT("FileName"));
        FString AnimPrefix = TaeFileName.Replace(TEXT(".tae"), TEXT(""));
        // 数字部分零填充到3�? a00→a000, a10→a010, a100→a100
        {
            FString NumPart = AnimPrefix.RightChop(1);
            while (NumPart.Len() < 3) NumPart = TEXT("0") + NumPart;
            AnimPrefix = TEXT("a") + NumPart;
        }const TArray<TSharedPtr<FJsonValue>>* AnimsArr;
        if (!TaeFileObj->TryGetArrayField(TEXT("Animations"), AnimsArr)) continue;

        for (const TSharedPtr<FJsonValue>& AnimVal : *AnimsArr)
        {
            const TSharedPtr<FJsonObject>& AnimObj = AnimVal->AsObject();
            int32 AnimID = AnimObj->GetIntegerField(TEXT("AnimID"));

            // AnimID 直查，前缀�?TAE 文件名推�?
            FString AnimName = FString::Printf(TEXT("Sekiro_%s_%06d"), *AnimPrefix, AnimID);

            FSAAnimationLogicIR AnimLogic = ParseAnimationEntry(AnimObj, AnimName);

            // 推断类别
            AnimLogic.InferredCategory = InferCategoryFromAnimID(AnimID);
            AnimLogic.AnimPrefix = AnimPrefix;
            AnimLogic.AnimPrefix = AnimPrefix;

            // 计算总帧数（从最后一个事件帧推断�?
            for (const FSATAEEventIR& Evt : AnimLogic.AllEvents)
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

    // 按类别分�?
    for (const auto& Pair : Result.AnimLogicMap)
    {
        const FSAAnimationLogicIR& Logic = Pair.Value;
        FString Category = Logic.InferredCategory;
        if (!Result.MainStateMachine.AnimIDsByCategory.Contains(Category))
            Result.MainStateMachine.AnimIDsByCategory.Add(Category, {});
        Result.MainStateMachine.AnimIDsByCategory[Category].Add(Pair.Key);
    }

    UE_LOG(LogTemp, Log, TEXT("[TAEImporter] 导入完成: %d 动画, %d 事件, %d 类别"),
        Result.TotalAnims, Result.TotalEvents, Result.MainStateMachine.AnimIDsByCategory.Num());

    return Result;
}

// ============================================================================
// 动画条目解析
// ============================================================================

FSAAnimationLogicIR FSATAEImporter::ParseAnimationEntry(const TSharedPtr<FJsonObject>& AnimObj,
    const FString& AnimName)
{
    FSAAnimationLogicIR Logic;
    Logic.AnimID = AnimObj->GetIntegerField(TEXT("AnimID"));
    Logic.AnimName = AnimName;

    const TArray<TSharedPtr<FJsonValue>>* EventsArr;
    if (!AnimObj->TryGetArrayField(TEXT("Events"), EventsArr))
        return Logic;

    for (const TSharedPtr<FJsonValue>& EvtVal : *EventsArr)
    {
        const TSharedPtr<FJsonObject>& EvtObj = EvtVal->AsObject();

        FSATAEEventIR Evt;
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

        // 按类别分�?
        switch (Evt.Category)
        {
        case ESKTAEEventCategory::AttackBehavior:
        {
            FSAAttackHitboxIR Hitbox;
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
            FSASpEffectIR SpEff;
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
            FSASEventIR Snd;
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
            FSAFFXEventIR FFX;
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

    // �?JumpTable 事件提取取消窗口
    ExtractCancelWindows(Logic.AllEvents, Logic.CancelWindows);

    // �?JumpTable 事件提取帧级行为标志
    ExtractFrameFlags(Logic.AllEvents, Logic.JumpTableFlags);

    return Logic;
}

// ============================================================================
// 取消窗口提取
// ============================================================================

void FSATAEImporter::ExtractCancelWindows(const TArray<FSATAEEventIR>& Events,
    TArray<FSACancelWindowIR>& OutWindows)
{
    // 映射表：JT ID → (TargetAction, Crossfade)
    struct FCancelMapEntry {
        FName TargetAction;
        float Crossfade;
    };
    static const TMap<int32, FCancelMapEntry> CancelMap = {
        // R1/Attack 取消窗口
        {115, {TEXT("Attack"), 0.1f}},     // AnimCancelEnd_R1
        {26,  {TEXT("Attack"), 0.1f}},     // GenericCancelStart（通用取消→攻击连段）

        // L1/Guard 取消窗口
        {117, {TEXT("Guard"), 0.1f}},      // AnimCancelEnd_L1

        // L2/Prosthetic 取消窗口
        {118, {TEXT("Prosthetic"), 0.1f}}, // AnimCancelEnd_L2

        // □/Dodge 取消窗口
        {25,  {TEXT("Dodge"), 0.15f}},     // AnimCancelStart_Dodge

        // ○/Item 取消窗口
        {154, {TEXT("Item"), 0.1f}},       // ItemUseWindow
    };

    for (const FSATAEEventIR& Evt : Events)
    {
        if (Evt.Category != ESKTAEEventCategory::JumpTable)
            continue;

        int32 JumpTableID = 0;
        if (Evt.Params.Contains(TEXT("JumpTableID")))
            JumpTableID = FCString::Atoi(*Evt.Params[TEXT("JumpTableID")]);

        const FCancelMapEntry* Entry = CancelMap.Find(JumpTableID);
        if (!Entry)
            continue;

        FSACancelWindowIR Window;
        Window.StartFrame = Evt.StartFrame;
        Window.EndFrame = Evt.EndFrame;
        Window.TargetAction = Entry->TargetAction;
        Window.JumpAction = MapJumpTableToAction(JumpTableID);
        Window.CrossfadeDuration = Entry->Crossfade;
        OutWindows.Add(Window);
    }
}

// ============================================================================
// 帧级行为标志提取
// ============================================================================

void FSATAEImporter::ExtractFrameFlags(const TArray<FSATAEEventIR>& Events,
    TMap<int32, int32>& OutFrameFlags)
{
    for (const FSATAEEventIR& Evt : Events)
    {
        if (Evt.Category != ESKTAEEventCategory::JumpTable)
            continue;

        int32 JumpTableID = 0;
        if (Evt.Params.Contains(TEXT("JumpTableID")))
            JumpTableID = FCString::Atoi(*Evt.Params[TEXT("JumpTableID")]);

        uint32 Mask = 0;
        switch (JumpTableID)
        {
        case 7:   Mask = Bit_DisableTurning;    break;              // DisableTurning
        case 89:  Mask = Bit_DisableMovement;   break;              // DisableAllMovement
        case 19:  Mask = Bit_DisableMapHit;     break;              // DisableMapHit
        case 119: Mask = Bit_EnableParry;       break;              // EnableParry
        case 137: Mask = Bit_DisableParry;      break;              // DisableParry
        case 133: Mask = Bit_DisableSpecial;    break;              // DisableSpecial
        case 134: Mask = Bit_DisableItem;       break;              // DisableItem
        case 51:  Mask = Bit_Invincible;        break;              // InvincibilityFrame
        case 27:  Mask = Bit_SetNoGravity;      break;              // SetNoGravity
        case 8:   Mask = Bit_FlagAsDodging;     break;              // FlagAsDodging
        case 12:  Mask = Bit_InvokeDeath;       break;              // InvokeDeath
        case 90:  Mask = Bit_LimitMoveSpeedWalk; break;             // LimitMoveSpeedWalk
        case 91:  Mask = Bit_LimitMoveSpeedDash; break;             // LimitMoveSpeedDash
        case 32:  Mask = Bit_EnterMovement;     break;              // EnterMovement
        case 31:  Mask = Bit_ExitMovement;      break;              // ExitMovement
        case 55:  Mask = Bit_Staggered;         break;              // StaggerFlag
        default:  continue;
        }

        // 在事件帧范围内设置标志位（OR 累积到已有值）
        for (int32 Frame = Evt.StartFrame; Frame <= Evt.EndFrame && Frame < 3000; ++Frame)
        {
            int32* Existing = OutFrameFlags.Find(Frame);
            if (Existing)
                *Existing |= static_cast<int32>(Mask);
            else
                OutFrameFlags.Add(Frame, static_cast<int32>(Mask));
        }
    }
}

// ============================================================================
// 过渡规则构建
// ============================================================================

void FSATAEImporter::BuildTransitions(const TMap<int32, FSAAnimationLogicIR>& AnimLogicMap,
    FSAStateMachineIR& OutSM)
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
        const FSAAnimationLogicIR& Logic = Pair.Value;
        FString StateName = FString::Printf(TEXT("%s_%d"), *Logic.InferredCategory, Logic.AnimID);

        // �?CancelWindow 生成过渡
        for (const FSACancelWindowIR& Window : Logic.CancelWindows)
        {
            FSASMTransitionIR Transition;
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

            // 从目标类别中选第一个动画作为默认目�?
            const TArray<int32>* TargetIDs = CategoryAnimIDs.Find(TargetCategory);
            if (TargetIDs && TargetIDs->Num() > 0)
                Transition.ToAnimID = (*TargetIDs)[0];

            Transition.ToState = FName(*FString::Printf(TEXT("%s_%d"), *TargetCategory, Transition.ToAnimID));
            OutSM.Transitions.Add(Transition);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[TAEImporter] 构建 %d 条过渡规则"), OutSM.Transitions.Num());
}

// ============================================================================
// 分类与映�?
// ============================================================================

ESKTAEEventCategory FSATAEImporter::ClassifyEventType(int32 Type, const FString& TypeName)
{
    // 核心战斗事件
    if (Type == 0)   return ESKTAEEventCategory::JumpTable;
    if (Type == 1)   return ESKTAEEventCategory::AttackBehavior;
    if (Type == 2)   return ESKTAEEventCategory::BulletBehavior;
    if (Type == 5)   return ESKTAEEventCategory::CommonBehavior;

    // 状态效�?
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

    // 音效�?
    if (Lower.Contains(TEXT("sound")) || Lower.Contains(TEXT("sfx")) || Lower.Contains(TEXT("foot")))
        return ESKTAEEventCategory::PlaySound;

    // 特效�?
    if (Lower.Contains(TEXT("ffx")) || Lower.Contains(TEXT("spawnone")) || Lower.Contains(TEXT("spawnffx")))
        return ESKTAEEventCategory::SpawnFFX;

    // 转向速度
    if (Lower.Contains(TEXT("turn")) && Lower.Contains(TEXT("speed")))
        return ESKTAEEventCategory::SetTurnSpeed;

    return ESKTAEEventCategory::Other;
}

ESKJumpTableAction FSATAEImporter::MapJumpTableToAction(int32 JumpTableID)
{
    switch (JumpTableID)
    {
    case 1:  return ESKJumpTableAction::AnimCancelStart_R1;
    case 3:  return ESKJumpTableAction::SetTurnSpeed;
    case 5:  return ESKJumpTableAction::EnableParry;
    case 7:  return ESKJumpTableAction::DisableTurning;
    case 8:  return ESKJumpTableAction::FlagAsDodging;
    case 9:  return ESKJumpTableAction::AnimCancelStart_L1;
    case 11: return ESKJumpTableAction::SwitchHKSLayer;
    case 12: return ESKJumpTableAction::InvokeDeath;
    case 19: return ESKJumpTableAction::DisableMapHit;
    case 21: return ESKJumpTableAction::AnimCancelStart_Guard;
    case 25: return ESKJumpTableAction::AnimCancelStart_Dodge;
    case 26: return ESKJumpTableAction::GenericCancelStart;
    case 27: return ESKJumpTableAction::SetNoGravity;
    case 28: return ESKJumpTableAction::SetMoveSpeedNormal;
    case 30: return ESKJumpTableAction::AnimCancelStart_Item;
    case 31: return ESKJumpTableAction::ExitMovement;
    case 32: return ESKJumpTableAction::EnterMovement;
    case 34: return ESKJumpTableAction::AnimCancelEnd_General;
    case 50: return ESKJumpTableAction::ActionRestriction;
    case 51: return ESKJumpTableAction::InvincibilityFrame;
    case 55: return ESKJumpTableAction::StaggerFlag;
    case 63: return ESKJumpTableAction::SpecialActionFlag;
    case 65: return ESKJumpTableAction::LookAtTarget;
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
    case 133: return ESKJumpTableAction::DisableSpecial;
    case 134: return ESKJumpTableAction::DisableItem;
    case 137: return ESKJumpTableAction::DisableParry;
    case 154: return ESKJumpTableAction::ItemUseWindow;
    case 16:  return ESKJumpTableAction::JTID_16;
    case 24:  return ESKJumpTableAction::JTID_24;
    case 39:  return ESKJumpTableAction::JTID_39;
    case 54:  return ESKJumpTableAction::JTID_54;
    case 56:  return ESKJumpTableAction::JTID_56;
    case 69:  return ESKJumpTableAction::JTID_69;
    case 72:  return ESKJumpTableAction::JTID_72;
    case 95:  return ESKJumpTableAction::JTID_95;
    case 110: return ESKJumpTableAction::JTID_110;
    case 125: return ESKJumpTableAction::JTID_125;
    case 126: return ESKJumpTableAction::JTID_126;
    case 127: return ESKJumpTableAction::JTID_127;
    case 128: return ESKJumpTableAction::JTID_128;
    case 132: return ESKJumpTableAction::JTID_132;
    case 136: return ESKJumpTableAction::JTID_136;
    case 140: return ESKJumpTableAction::JTID_140;
    case 141: return ESKJumpTableAction::JTID_141;
    case 143: return ESKJumpTableAction::JTID_143;
    case 145: return ESKJumpTableAction::JTID_145;
    case 146: return ESKJumpTableAction::JTID_146;
    case 148: return ESKJumpTableAction::JTID_148;
    case 149: return ESKJumpTableAction::JTID_149;
    case 150: return ESKJumpTableAction::JTID_150;
    case 151: return ESKJumpTableAction::JTID_151;
    case 155: return ESKJumpTableAction::JTID_155;
    case 156: return ESKJumpTableAction::JTID_156;
    case 157: return ESKJumpTableAction::JTID_157;
    case 158: return ESKJumpTableAction::JTID_158;
    default:  return ESKJumpTableAction::None;
    }
}
FString FSATAEImporter::InferCategoryFromAnimID(int32 AnimID)
{
    int32 HighPart = AnimID / 10000;
    int32 MidPart = (AnimID / 100) % 100;

    if (HighPart == 0)
    {
        if (AnimID < 14)   return TEXT("Locomotion_Idle");
        if (AnimID < 100)  return TEXT("Locomotion");
        // 移动方向: 个位数 0=Fwd, 1=Bwd, 2=L, 3=R
        if (AnimID < 200) { int32 Tens = (AnimID / 10) % 10; return FString::Printf(TEXT("Locomotion_Walk%s"), Tens == 0 ? TEXT("_Fwd") : Tens == 1 ? TEXT("_Bwd") : Tens == 2 ? TEXT("_L") : Tens == 3 ? TEXT("_R") : TEXT("")); }
        if (AnimID < 300) { int32 Tens = (AnimID / 10) % 10; return FString::Printf(TEXT("Locomotion_Jog%s"), Tens == 0 ? TEXT("_Fwd") : Tens == 1 ? TEXT("_Bwd") : Tens == 2 ? TEXT("_L") : Tens == 3 ? TEXT("_R") : TEXT("")); }
        if (AnimID < 400) { int32 Tens = (AnimID / 10) % 10; return FString::Printf(TEXT("Locomotion_Run%s"), Tens == 0 ? TEXT("_Fwd") : Tens == 1 ? TEXT("_Bwd") : Tens == 2 ? TEXT("_L") : Tens == 3 ? TEXT("_R") : TEXT("")); }
        if (AnimID < 500) { int32 Tens = (AnimID / 10) % 10; return FString::Printf(TEXT("Locomotion_Sprint%s"), Tens == 0 ? TEXT("_Fwd") : Tens == 1 ? TEXT("_Bwd") : Tens == 2 ? TEXT("_L") : Tens == 3 ? TEXT("_R") : TEXT("")); }
        // 500-599: 过渡动画; 600-699: Jump
        if (AnimID < 600)  return TEXT("Locomotion");
        if (AnimID < 700)  { int32 Tens = (AnimID / 10) % 10; return FString::Printf(TEXT("Jump%s"), Tens == 0 ? TEXT("_Fwd") : Tens == 1 ? TEXT("_Bwd") : Tens == 2 ? TEXT("_L") : Tens == 3 ? TEXT("_R") : TEXT("")); }
        if (AnimID < 1000) return TEXT("Locomotion");
        if (AnimID >= 5000 && AnimID < 6000) return TEXT("Combat");
        return TEXT("Locomotion");
    }

    // 20xxxx: 攻击
    if (HighPart >= 20 && HighPart < 30) return TEXT("Attack");

    // 30xxxx: 防御体系 (Guard / Deflect / Dodge / Mikiri / Grab)
    if (HighPart >= 30 && HighPart < 40)
    {
        if (MidPart < 10) return TEXT("Guard");
        if (MidPart < 20) return TEXT("Deflect");
        if (MidPart < 30) return TEXT("Mikiri");
        if (MidPart < 32) { int32 Digit = AnimID % 10; return FString::Printf(TEXT("Dodge%s"), Digit == 0 ? TEXT("_Fwd") : Digit == 1 ? TEXT("_Bwd") : Digit == 2 ? TEXT("_L") : Digit == 3 ? TEXT("_R") : TEXT("")); }
        if (MidPart < 40) return TEXT("Sweep");
        if (MidPart < 50) return TEXT("Grab");
        return TEXT("Defense");
    }

    // 40xxxx: 受击/死亡
    if (HighPart >= 40 && HighPart < 50)
    {
        if (MidPart < 20) return TEXT("Hit");
        if (MidPart < 40) return TEXT("Knockback");
        if (MidPart < 60) return TEXT("Death");
        return TEXT("HitReaction");
    }

    // 50xxxx: 忍杀
    if (HighPart >= 50 && HighPart < 60) return TEXT("Deathblow");

    // 60xxxx: 回生/特殊
    if (HighPart >= 60 && HighPart < 70) return TEXT("Resurrection");

    // 70xxxx: 义手忍具
    if (HighPart >= 70 && HighPart < 80) return TEXT("Prosthetic");

    // 80xxxx: 钩绳
    if (HighPart >= 80 && HighPart < 90) return TEXT("Grapple");

    // 90xxxx: 道具
    if (HighPart >= 90 && HighPart < 100) return TEXT("Item");

    // cxxxx+: 战斗艺术/特殊
    if (HighPart >= 100)
    {
        if (HighPart >= 110 && HighPart < 120) return TEXT("CombatArt");
        if (HighPart >= 121 && HighPart < 130) return TEXT("CombatArt");
        return TEXT("CombatArt");
    }

    return TEXT("Other");
}
