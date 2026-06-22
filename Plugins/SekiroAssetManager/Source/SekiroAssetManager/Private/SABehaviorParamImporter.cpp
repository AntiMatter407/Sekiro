#include "SABehaviorParamImporter.h"
#include "SekiroCombatData.h"
#include "Json.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// ============================================================================
// 公共 API
// ============================================================================

USKCombatData* FSABehaviorParamImporter::ImportFromFile(const FString& JsonPath, UObject* Outer)
{
    if (!Outer)
    {
        UE_LOG(LogTemp, Error, TEXT("[BehavParamImporter] Outer 为空，无法创建 DataAsset"));
        return nullptr;
    }

    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[BehavParamImporter] 无法读取文件: %s"), *JsonPath);
        return nullptr;
    }

    return ImportFromString(JsonContent, Outer);
}

USKCombatData* FSABehaviorParamImporter::ImportFromString(const FString& JsonContent, UObject* Outer)
{
    if (!Outer)
    {
        UE_LOG(LogTemp, Error, TEXT("[BehavParamImporter] Outer 为空，无法创建 DataAsset"));
        return nullptr;
    }

    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, RootObj))
    {
        UE_LOG(LogTemp, Error, TEXT("[BehavParamImporter] JSON 解析失败"));
        return nullptr;
    }

    USKCombatData* DataAsset = NewObject<USKCombatData>(Outer, NAME_None, RF_Public | RF_Standalone);
    if (!DataAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("[BehavParamImporter] NewObject 失败"));
        return nullptr;
    }

    ParseComboChainJson(RootObj, DataAsset);

    UE_LOG(LogTemp, Log, TEXT("[BehavParamImporter] 导入完成: %d 条连段条目"),
        DataAsset->ComboChain.Num());

    return DataAsset;
}

// ============================================================================
// 内部解析
// ============================================================================

void FSABehaviorParamImporter::ParseComboChainJson(const TSharedPtr<FJsonObject>& Root,
    USKCombatData* DataAsset)
{
    // 顶层是 AnimID（字符串键）-> 派生规则对象的映射
    for (const auto& Pair : Root->Values)
    {
        // 键是 AnimID 字符串
        int32 AnimID = FCString::Atoi(*Pair.Key);

        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!Pair.Value->TryGetObject(EntryObj))
            continue;

        FSKComboEntry Entry;

        // 读取各字段（JSON 中字段名为小驼峰风格）
        if ((*EntryObj)->HasField(TEXT("NextOnR1")))
            Entry.NextOnR1 = (*EntryObj)->GetIntegerField(TEXT("NextOnR1"));
        if ((*EntryObj)->HasField(TEXT("NextOnCharged")))
            Entry.NextOnCharged = (*EntryObj)->GetIntegerField(TEXT("NextOnCharged"));
        if ((*EntryObj)->HasField(TEXT("NextOnGuard")))
            Entry.NextOnGuard = (*EntryObj)->GetIntegerField(TEXT("NextOnGuard"));
        if ((*EntryObj)->HasField(TEXT("NextOnDodge")))
            Entry.NextOnDodge = (*EntryObj)->GetIntegerField(TEXT("NextOnDodge"));
        if ((*EntryObj)->HasField(TEXT("NextOnCounter")))
            Entry.NextOnCounter = (*EntryObj)->GetIntegerField(TEXT("NextOnCounter"));
        if ((*EntryObj)->HasField(TEXT("NextOnJump")))
            Entry.NextOnJump = (*EntryObj)->GetIntegerField(TEXT("NextOnJump"));
        if ((*EntryObj)->HasField(TEXT("bIsComboEnd")))
            Entry.bIsComboEnd = (*EntryObj)->GetBoolField(TEXT("bIsComboEnd"));

        DataAsset->ComboChain.Add(AnimID, Entry);
    }
}
