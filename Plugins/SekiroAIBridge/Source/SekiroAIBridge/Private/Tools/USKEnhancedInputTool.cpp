#include "Tools/USKEnhancedInputTool.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

FString USKEnhancedInputTool::GetToolDescription() const
{
    return TEXT("EnhancedInput操作：创建InputAction/InputMappingContext资产、管理按键映射、查询详情。");
}

FString USKEnhancedInputTool::GetInputSchemaJson() const
{
    return TEXT("{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"create_input_action\",\"create_mapping_context\",\"map_key\",\"unmap_key\",\"get_info\",\"configure_triggers\"]},\"path\":{\"type\":\"string\",\"description\":\"Asset path\"},\"value_type\":{\"type\":\"string\",\"enum\":[\"bool\",\"axis1d\",\"axis2d\",\"axis3d\"]},\"context_path\":{\"type\":\"string\"},\"action_path\":{\"type\":\"string\"},\"key\":{\"type\":\"string\"},\"triggers\":{\"type\":\"array\"},\"modifiers\":{\"type\":\"array\"}},\"required\":[\"action\",\"path\"]}");
}

bool USKEnhancedInputTool::RequiresConfirmation() const
{
    return CurrentAction == TEXT("map_key") || CurrentAction == TEXT("unmap_key");
}

FString USKEnhancedInputTool::GetConfirmationSummary(const FString& ArgsJson) const
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
    {
        FString Action = ArgsObj->GetStringField(TEXT("action"));
        FString Path = ArgsObj->GetStringField(TEXT("path"));
        return FString::Printf(TEXT("EnhancedInput操作: %s -> %s"), *Action, *Path);
    }
    return TEXT("EnhancedInput操作");
}

FString USKEnhancedInputTool::Execute(const FString& ArgsJson, FString& OutError)
{
    TSharedPtr<FJsonObject> ArgsObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgsJson);
    if (!FJsonSerializer::Deserialize(Reader, ArgsObj) || !ArgsObj.IsValid())
    {
        OutError = TEXT("无法解析参数JSON");
        return FString();
    }

    FString Action;
    if (!ArgsObj->TryGetStringField(TEXT("action"), Action))
    {
        OutError = TEXT("缺少 action 参数");
        return FString();
    }

    CurrentAction = Action;

    if (Action == TEXT("create_input_action"))    return HandleCreateInputAction(ArgsObj, OutError);
    if (Action == TEXT("create_mapping_context"))  return HandleCreateMappingContext(ArgsObj, OutError);
    if (Action == TEXT("map_key"))                 return HandleMapKey(ArgsObj, OutError);
    if (Action == TEXT("unmap_key"))               return HandleUnmapKey(ArgsObj, OutError);
    if (Action == TEXT("get_info"))                return HandleGetInfo(ArgsObj, OutError);
    if (Action == TEXT("configure_triggers"))      return HandleConfigureTriggers(ArgsObj, OutError);

    OutError = FString::Printf(TEXT("未知操作: %s"), *Action);
    return FString();
}

// ============================================================================
// HandleCreateInputAction
// ============================================================================

FString USKEnhancedInputTool::HandleCreateInputAction(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    // 解析路径
    int32 LastSlash;
    if (!AssetPath.FindLastChar('/', LastSlash))
    {
        OutError = TEXT("无效的资产路径");
        return FString();
    }
    FString PackagePath = AssetPath.Left(LastSlash);
    FString AssetName = AssetPath.RightChop(LastSlash + 1);

    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        OutError = FString::Printf(TEXT("资产已存在: %s"), *AssetPath);
        return FString();
    }

    // 创建 UInputAction
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UInputAction::StaticClass(), nullptr);
    if (!NewAsset)
    {
        OutError = FString::Printf(TEXT("创建InputAction失败: %s"), *AssetPath);
        return FString();
    }

    UInputAction* IA = Cast<UInputAction>(NewAsset);

    // 设置 ValueType
    FString ValueTypeStr;
    if (Args->TryGetStringField(TEXT("value_type"), ValueTypeStr))
    {
        if (ValueTypeStr == TEXT("bool"))      IA->ValueType = EInputActionValueType::Boolean;
        else if (ValueTypeStr == TEXT("axis1d")) IA->ValueType = EInputActionValueType::Axis1D;
        else if (ValueTypeStr == TEXT("axis2d")) IA->ValueType = EInputActionValueType::Axis2D;
        else if (ValueTypeStr == TEXT("axis3d")) IA->ValueType = EInputActionValueType::Axis3D;
    }

    // 设置 bool 属性
    bool bConsumeInput = true;
    if (Args->TryGetBoolField(TEXT("b_consume_input"), bConsumeInput))
        IA->bConsumeInput = bConsumeInput;
    bool bTriggerWhenPaused = false;
    if (Args->TryGetBoolField(TEXT("b_trigger_when_paused"), bTriggerWhenPaused))
        IA->bTriggerWhenPaused = bTriggerWhenPaused;

    // 添加 Triggers
    const TArray<TSharedPtr<FJsonValue>>* TriggersArray;
    if (Args->TryGetArrayField(TEXT("triggers"), TriggersArray))
    {
        for (const TSharedPtr<FJsonValue>& TrigValue : *TriggersArray)
        {
            const TSharedPtr<FJsonObject>* TrigObj;
            if (!TrigValue->TryGetObject(TrigObj)) continue;

            FString TrigType;
            if (!(*TrigObj)->TryGetStringField(TEXT("type"), TrigType)) continue;

            UClass* TriggerClass = FindTriggerClass(TrigType);
            if (!TriggerClass) continue;

            UInputTrigger* Trigger = NewObject<UInputTrigger>(IA, TriggerClass);
            if (!Trigger) continue;

            // 设置触发器特有属性
            if (UInputTriggerHold* HoldTrig = Cast<UInputTriggerHold>(Trigger))
            {
                double HoldTime;
                if ((*TrigObj)->TryGetNumberField(TEXT("hold_time_threshold"), HoldTime))
                    HoldTrig->HoldTimeThreshold = (float)HoldTime;
            }
            else if (UInputTriggerTap* TapTrig = Cast<UInputTriggerTap>(Trigger))
            {
                double TapTime;
                if ((*TrigObj)->TryGetNumberField(TEXT("tap_release_time_threshold"), TapTime))
                    TapTrig->TapReleaseTimeThreshold = (float)TapTime;
            }
            else if (UInputTriggerPulse* PulseTrig = Cast<UInputTriggerPulse>(Trigger))
            {
                double Interval;
                if ((*TrigObj)->TryGetNumberField(TEXT("interval"), Interval))
                    PulseTrig->Interval = (float)Interval;
            }
            else if (UInputTriggerChordAction* ChordTrig = Cast<UInputTriggerChordAction>(Trigger))
            {
                FString ChordActionPath;
                if ((*TrigObj)->TryGetStringField(TEXT("chord_action_path"), ChordActionPath))
                {
                    UInputAction* ChordAction = LoadObject<UInputAction>(nullptr, *ChordActionPath);
                    if (ChordAction) ChordTrig->ChordAction = ChordAction;
                }
            }

            IA->Triggers.Add(Trigger);
        }
    }

    // 添加 Modifiers
    const TArray<TSharedPtr<FJsonValue>>* ModifiersArray;
    if (Args->TryGetArrayField(TEXT("modifiers"), ModifiersArray))
    {
        for (const TSharedPtr<FJsonValue>& ModValue : *ModifiersArray)
        {
            const TSharedPtr<FJsonObject>* ModObj;
            if (!ModValue->TryGetObject(ModObj)) continue;

            FString ModType;
            if (!(*ModObj)->TryGetStringField(TEXT("type"), ModType)) continue;

            UClass* ModifierClass = FindModifierClass(ModType);
            if (!ModifierClass) continue;

            UInputModifier* Modifier = NewObject<UInputModifier>(IA, ModifierClass);
            if (!Modifier) continue;

            // 设置修改器特有属性
            if (UInputModifierDeadZone* DeadZone = Cast<UInputModifierDeadZone>(Modifier))
            {
                double Lower, Upper;
                if ((*ModObj)->TryGetNumberField(TEXT("lower_threshold"), Lower))
                    DeadZone->LowerThreshold = (float)Lower;
                if ((*ModObj)->TryGetNumberField(TEXT("upper_threshold"), Upper))
                    DeadZone->UpperThreshold = (float)Upper;
                FString DeadZoneType;
                if ((*ModObj)->TryGetStringField(TEXT("deadzone_type"), DeadZoneType))
                {
                    DeadZone->Type = (DeadZoneType == TEXT("radial")) ? EDeadZoneType::Radial : EDeadZoneType::Axial;
                }
            }
            else if (UInputModifierScalar* ScalarMod = Cast<UInputModifierScalar>(Modifier))
            {
                const TSharedPtr<FJsonObject>* ScalarObj;
                if ((*ModObj)->TryGetObjectField(TEXT("scalar"), ScalarObj))
                {
                    double X = 1.0, Y = 1.0, Z = 1.0;
                    (*ScalarObj)->TryGetNumberField(TEXT("x"), X);
                    (*ScalarObj)->TryGetNumberField(TEXT("y"), Y);
                    (*ScalarObj)->TryGetNumberField(TEXT("z"), Z);
                    ScalarMod->Scalar = FVector((float)X, (float)Y, (float)Z);
                }
            }
            else if (UInputModifierNegate* NegateMod = Cast<UInputModifierNegate>(Modifier))
            {
                bool bX = true, bY = true, bZ = true;
                const TSharedPtr<FJsonObject>* NegateObj;
                if ((*ModObj)->TryGetObjectField(TEXT("negate"), NegateObj))
                {
                    (*NegateObj)->TryGetBoolField(TEXT("x"), bX);
                    (*NegateObj)->TryGetBoolField(TEXT("y"), bY);
                    (*NegateObj)->TryGetBoolField(TEXT("z"), bZ);
                    NegateMod->bX = bX; NegateMod->bY = bY; NegateMod->bZ = bZ;
                }
            }
            else if (UInputModifierSwizzleAxis* SwizzleMod = Cast<UInputModifierSwizzleAxis>(Modifier))
            {
                FString Order;
                if ((*ModObj)->TryGetStringField(TEXT("order"), Order))
                {
                    if (Order == TEXT("YXZ")) SwizzleMod->Order = EInputAxisSwizzle::YXZ;
                    else if (Order == TEXT("ZXY")) SwizzleMod->Order = EInputAxisSwizzle::ZXY;
                    else if (Order == TEXT("ZYX")) SwizzleMod->Order = EInputAxisSwizzle::ZYX;
                    else if (Order == TEXT("XZY")) SwizzleMod->Order = EInputAxisSwizzle::XZY;
                    else if (Order == TEXT("YZX")) SwizzleMod->Order = EInputAxisSwizzle::YZX;
                }
            }
            else if (UInputModifierResponseCurveExponential* ExpMod = Cast<UInputModifierResponseCurveExponential>(Modifier))
            {
                const TSharedPtr<FJsonObject>* ExpObj;
                if ((*ModObj)->TryGetObjectField(TEXT("exponent"), ExpObj))
                {
                    double EX = 1.0, EY = 1.0, EZ = 1.0;
                    (*ExpObj)->TryGetNumberField(TEXT("x"), EX);
                    (*ExpObj)->TryGetNumberField(TEXT("y"), EY);
                    (*ExpObj)->TryGetNumberField(TEXT("z"), EZ);
                    ExpMod->CurveExponent = FVector((float)EX, (float)EY, (float)EZ);
                }
            }

            IA->Modifiers.Add(Modifier);
        }
    }

    IA->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), AssetName);
    ResultObj->SetStringField(TEXT("class"), TEXT("InputAction"));
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleCreateMappingContext
// ============================================================================

FString USKEnhancedInputTool::HandleCreateMappingContext(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    int32 LastSlash;
    if (!AssetPath.FindLastChar('/', LastSlash))
    {
        OutError = TEXT("无效的资产路径");
        return FString();
    }
    FString PackagePath = AssetPath.Left(LastSlash);
    FString AssetName = AssetPath.RightChop(LastSlash + 1);

    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        OutError = FString::Printf(TEXT("资产已存在: %s"), *AssetPath);
        return FString();
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UInputMappingContext::StaticClass(), nullptr);
    if (!NewAsset)
    {
        OutError = FString::Printf(TEXT("创建InputMappingContext失败: %s"), *AssetPath);
        return FString();
    }

    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetStringField(TEXT("name"), AssetName);
    ResultObj->SetStringField(TEXT("class"), TEXT("InputMappingContext"));
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleMapKey
// ============================================================================

FString USKEnhancedInputTool::HandleMapKey(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString ContextPath, ActionPath, KeyName;
    if (!Args->TryGetStringField(TEXT("context_path"), ContextPath))
    {
        OutError = TEXT("缺少 context_path 参数");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("action_path"), ActionPath))
    {
        OutError = TEXT("缺少 action_path 参数");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("key"), KeyName))
    {
        OutError = TEXT("缺少 key 参数");
        return FString();
    }

    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ContextPath);
    if (!IMC)
    {
        OutError = FString::Printf(TEXT("InputMappingContext未找到: %s"), *ContextPath);
        return FString();
    }

    UInputAction* IA = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (!IA)
    {
        OutError = FString::Printf(TEXT("InputAction未找到: %s"), *ActionPath);
        return FString();
    }

    FKey Key(*KeyName);
    if (!Key.IsValid())
    {
        OutError = FString::Printf(TEXT("无效的按键名: %s"), *KeyName);
        return FString();
    }

    // 移除同 Action+Key 的已有映射（LoadObject 每次返回不同指针导致 MapKey 的指针比对失效，需用路径+按键名字符串比对去重）
    const FName ActionName = IA->GetFName();
    for (int32 i = IMC->GetMappings().Num() - 1; i >= 0; --i)
    {
        const FEnhancedActionKeyMapping& Existing = IMC->GetMappings()[i];
        if (Existing.Key == Key && Existing.Action && Existing.Action->GetFName() == ActionName)
        {
            IMC->UnmapKey(Existing.Action, Existing.Key);
        }
    }

    // MapKey 返回映射的引用，可直接修改 Triggers/Modifiers 覆盖
    FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA, Key);

    // 可选的映射级 triggers/modifiers 覆盖
    // 注意: MapKey 返回已有映射时不清除旧值, 需先清空再添加
    const TArray<TSharedPtr<FJsonValue>>* TriggersArray;
    if (Args->TryGetArrayField(TEXT("triggers"), TriggersArray))
    {
        Mapping.Triggers.Empty();
        for (const TSharedPtr<FJsonValue>& TrigValue : *TriggersArray)
        {
            const TSharedPtr<FJsonObject>* TrigObj;
            if (TrigValue->TryGetObject(TrigObj))
            {
                FString TrigType;
                if ((*TrigObj)->TryGetStringField(TEXT("type"), TrigType))
                {
                    UClass* TriggerClass = FindTriggerClass(TrigType);
                    if (TriggerClass)
                    {
                        UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TriggerClass);
                        if (Trigger) Mapping.Triggers.Add(Trigger);
                    }
                }
            }
            else
            {
                // 简单字符串格式: ["Pressed", "Released"]
                FString TrigType;
                if (TrigValue->TryGetString(TrigType))
                {
                    UClass* TriggerClass = FindTriggerClass(TrigType);
                    if (TriggerClass)
                    {
                        UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TriggerClass);
                        if (Trigger) Mapping.Triggers.Add(Trigger);
                    }
                }
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ModifiersArray;
    if (Args->TryGetArrayField(TEXT("modifiers"), ModifiersArray))
    {
        Mapping.Modifiers.Empty();
        for (const TSharedPtr<FJsonValue>& ModValue : *ModifiersArray)
        {
            const TSharedPtr<FJsonObject>* ModObj;
            if (ModValue->TryGetObject(ModObj))
            {
                FString ModType;
                if (!(*ModObj)->TryGetStringField(TEXT("type"), ModType)) continue;
                UClass* ModifierClass = FindModifierClass(ModType);
                if (!ModifierClass) continue;

                UInputModifier* Modifier = NewObject<UInputModifier>(IMC, ModifierClass);
                if (!Modifier) continue;

                // SwizzleAxis
                if (UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
                {
                    FString Order;
                    if ((*ModObj)->TryGetStringField(TEXT("order"), Order))
                    {
                        if (Order == TEXT("YXZ")) Swizzle->Order = EInputAxisSwizzle::YXZ;
                        else if (Order == TEXT("ZXY")) Swizzle->Order = EInputAxisSwizzle::ZXY;
                        else if (Order == TEXT("ZYX")) Swizzle->Order = EInputAxisSwizzle::ZYX;
                        else if (Order == TEXT("XZY")) Swizzle->Order = EInputAxisSwizzle::XZY;
                        else if (Order == TEXT("YZX")) Swizzle->Order = EInputAxisSwizzle::YZX;
                    }
                }
                // Negate
                else if (UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Modifier))
                {
                    bool bX = false, bY = false, bZ = false;
                    (*ModObj)->TryGetBoolField(TEXT("x"), bX);
                    (*ModObj)->TryGetBoolField(TEXT("y"), bY);
                    (*ModObj)->TryGetBoolField(TEXT("z"), bZ);
                    if (bX || bY || bZ) { Negate->bX = bX; Negate->bY = bY; Negate->bZ = bZ; }
                }

                Mapping.Modifiers.Add(Modifier);
            }
            else
            {
                // 简单字符串格式: ["SwizzleAxis", "Negate"]
                FString ModType;
                if (ModValue->TryGetString(ModType))
                {
                    UClass* ModifierClass = FindModifierClass(ModType);
                    if (ModifierClass)
                    {
                        UInputModifier* Modifier = NewObject<UInputModifier>(IMC, ModifierClass);
                        if (Modifier) Mapping.Modifiers.Add(Modifier);
                    }
                }
            }
        }
    }

    IMC->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(ContextPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("context"), ContextPath);
    ResultObj->SetStringField(TEXT("action"), ActionPath);
    ResultObj->SetStringField(TEXT("key"), KeyName);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleUnmapKey
// ============================================================================

FString USKEnhancedInputTool::HandleUnmapKey(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString ContextPath, ActionPath, KeyName;
    if (!Args->TryGetStringField(TEXT("context_path"), ContextPath))
    {
        OutError = TEXT("缺少 context_path 参数");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("action_path"), ActionPath))
    {
        OutError = TEXT("缺少 action_path 参数");
        return FString();
    }
    if (!Args->TryGetStringField(TEXT("key"), KeyName))
    {
        OutError = TEXT("缺少 key 参数");
        return FString();
    }

    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ContextPath);
    if (!IMC)
    {
        OutError = FString::Printf(TEXT("InputMappingContext未找到: %s"), *ContextPath);
        return FString();
    }

    UInputAction* IA = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (!IA)
    {
        OutError = FString::Printf(TEXT("InputAction未找到: %s"), *ActionPath);
        return FString();
    }

    FKey Key(*KeyName);
    if (!Key.IsValid())
    {
        OutError = FString::Printf(TEXT("无效的按键名: %s"), *KeyName);
        return FString();
    }

    IMC->UnmapKey(IA, Key);
    IMC->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(ContextPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("context"), ContextPath);
    ResultObj->SetStringField(TEXT("key"), KeyName);
    ResultObj->SetBoolField(TEXT("success"), true);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// HandleGetInfo
// ============================================================================

FString USKEnhancedInputTool::HandleGetInfo(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    // 先尝试 UInputAction
    if (UInputAction* IA = LoadObject<UInputAction>(nullptr, *AssetPath))
    {
        return InputActionToJson(IA);
    }

    // 再尝试 UInputMappingContext
    if (UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *AssetPath))
    {
        return MappingContextToJson(IMC);
    }

    OutError = FString::Printf(TEXT("EnhancedInput资产未找到: %s"), *AssetPath);
    return FString();
}

// ============================================================================
// HandleConfigureTriggers
// ============================================================================

FString USKEnhancedInputTool::HandleConfigureTriggers(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
    FString AssetPath = Args->GetStringField(TEXT("path"));

    UInputAction* IA = LoadObject<UInputAction>(nullptr, *AssetPath);
    if (!IA)
    {
        OutError = FString::Printf(TEXT("InputAction未找到: %s"), *AssetPath);
        return FString();
    }

    // 解析 triggers 数组
    const TArray<TSharedPtr<FJsonValue>>* TriggersArray;
    if (!Args->TryGetArrayField(TEXT("triggers"), TriggersArray))
    {
        OutError = TEXT("缺少 triggers 参数（数组）");
        return FString();
    }

    // 清空现有 triggers
    IA->Triggers.Empty();

    // 添加新 triggers
    for (const TSharedPtr<FJsonValue>& TrigValue : *TriggersArray)
    {
        FString TrigType;
        if (TrigValue->TryGetString(TrigType))
        {
            UClass* TriggerClass = FindTriggerClass(TrigType);
            if (TriggerClass)
            {
                UInputTrigger* Trigger = NewObject<UInputTrigger>(IA, TriggerClass);
                if (Trigger)
                    IA->Triggers.Add(Trigger);
            }
        }
    }

    IA->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, false);

    TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
    ResultObj->SetStringField(TEXT("path"), AssetPath);
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetNumberField(TEXT("trigger_count"), IA->Triggers.Num());

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// Helper: Trigger class lookup
// ============================================================================

UClass* USKEnhancedInputTool::FindTriggerClass(const FString& TypeName) const
{
    if (TypeName == TEXT("Pressed"))       return UInputTriggerPressed::StaticClass();
    if (TypeName == TEXT("Down"))          return UInputTriggerDown::StaticClass();
    if (TypeName == TEXT("Released"))      return UInputTriggerReleased::StaticClass();
    if (TypeName == TEXT("Hold"))          return UInputTriggerHold::StaticClass();
    if (TypeName == TEXT("Tap"))           return UInputTriggerTap::StaticClass();
    if (TypeName == TEXT("Pulse"))         return UInputTriggerPulse::StaticClass();
    if (TypeName == TEXT("ChordedAction")) return UInputTriggerChordAction::StaticClass();
    return nullptr;
}

// ============================================================================
// Helper: Modifier class lookup
// ============================================================================

UClass* USKEnhancedInputTool::FindModifierClass(const FString& TypeName) const
{
    if (TypeName == TEXT("DeadZone"))                    return UInputModifierDeadZone::StaticClass();
    if (TypeName == TEXT("Scalar"))                      return UInputModifierScalar::StaticClass();
    if (TypeName == TEXT("Negate"))                      return UInputModifierNegate::StaticClass();
    if (TypeName == TEXT("SwizzleAxis"))                 return UInputModifierSwizzleAxis::StaticClass();
    if (TypeName == TEXT("Smooth"))                      return UInputModifierSmooth::StaticClass();
    if (TypeName == TEXT("ResponseCurveExponential"))   return UInputModifierResponseCurveExponential::StaticClass();
    if (TypeName == TEXT("ScaleByDeltaTime"))           return UInputModifierScaleByDeltaTime::StaticClass();
    return nullptr;
}

// ============================================================================
// Helper: Serialize InputAction
// ============================================================================

FString USKEnhancedInputTool::InputActionToJson(UInputAction* IA) const
{
    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetStringField(TEXT("name"), IA->GetName());
    Obj->SetStringField(TEXT("path"), IA->GetPathName());
    Obj->SetStringField(TEXT("class"), TEXT("InputAction"));
    Obj->SetBoolField(TEXT("b_consume_input"), IA->bConsumeInput);
    Obj->SetBoolField(TEXT("b_trigger_when_paused"), IA->bTriggerWhenPaused);

    // ValueType → 字符串
    switch (IA->ValueType)
    {
    case EInputActionValueType::Boolean: Obj->SetStringField(TEXT("value_type"), TEXT("bool")); break;
    case EInputActionValueType::Axis1D:  Obj->SetStringField(TEXT("value_type"), TEXT("axis1d")); break;
    case EInputActionValueType::Axis2D:  Obj->SetStringField(TEXT("value_type"), TEXT("axis2d")); break;
    case EInputActionValueType::Axis3D:  Obj->SetStringField(TEXT("value_type"), TEXT("axis3d")); break;
    }

    // Triggers
    TArray<TSharedPtr<FJsonValue>> TriggersArray;
    for (const TObjectPtr<UInputTrigger>& Trig : IA->Triggers)
    {
        if (Trig)
            TriggersArray.Add(MakeShareable(new FJsonValueString(Trig->GetClass()->GetName())));
    }
    Obj->SetArrayField(TEXT("triggers"), TriggersArray);

    // Modifiers
    TArray<TSharedPtr<FJsonValue>> ModifiersArray;
    for (const TObjectPtr<UInputModifier>& Mod : IA->Modifiers)
    {
        if (Mod)
            ModifiersArray.Add(MakeShareable(new FJsonValueString(Mod->GetClass()->GetName())));
    }
    Obj->SetArrayField(TEXT("modifiers"), ModifiersArray);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Output;
}

// ============================================================================
// Helper: Serialize InputMappingContext
// ============================================================================

FString USKEnhancedInputTool::MappingContextToJson(UInputMappingContext* IMC) const
{
    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetStringField(TEXT("name"), IMC->GetName());
    Obj->SetStringField(TEXT("path"), IMC->GetPathName());
    Obj->SetStringField(TEXT("class"), TEXT("InputMappingContext"));

    TArray<TSharedPtr<FJsonValue>> MappingsArray;
    for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
    {
        TSharedPtr<FJsonObject> MapObj = MakeShareable(new FJsonObject());
        MapObj->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("null"));
        MapObj->SetStringField(TEXT("key"), Mapping.Key.ToString());
        MappingsArray.Add(MakeShareable(new FJsonValueObject(MapObj)));
    }
    Obj->SetArrayField(TEXT("mappings"), MappingsArray);
    Obj->SetNumberField(TEXT("mapping_count"), IMC->GetMappings().Num());

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Output;
}
