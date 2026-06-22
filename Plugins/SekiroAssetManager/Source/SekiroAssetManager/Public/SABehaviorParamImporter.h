#pragma once

#include "CoreMinimal.h"

class USKCombatData;

/// BehaviorParam_PC.param ComboChain 数据导入器
/// 将预解析的 ComboChain.json（由 Python 管线脚本生成）导入为 USKCombatData 资产
class SEKIROASSETMANAGER_API FSABehaviorParamImporter
{
public:
    /// 从 JSON 文件导入 ComboChain 数据到 USKCombatData
    static USKCombatData* ImportFromFile(const FString& JsonPath, UObject* Outer);

    /// 从 JSON 字符串导入 ComboChain 数据到 USKCombatData
    static USKCombatData* ImportFromString(const FString& JsonContent, UObject* Outer);

private:
    /// 解析 ComboChain JSON 填充 DataAsset
    static void ParseComboChainJson(const TSharedPtr<FJsonObject>& Root, USKCombatData* DataAsset);
};
