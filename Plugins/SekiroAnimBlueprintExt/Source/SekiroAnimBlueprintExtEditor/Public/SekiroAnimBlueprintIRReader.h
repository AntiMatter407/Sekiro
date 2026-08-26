#pragma once

#include "CoreMinimal.h"
#include "SekiroAnimGraphIR.h"

class UAnimBlueprint;

/** 将现有标准 AnimBlueprint 严格、只读地反向转换为规范化 IR。 */
class SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimBlueprintIRReader
{
public:
    /** 读取资产；任何不支持结构都会使整个操作失败且不返回部分 IR。 */
    static bool Read(
        const UAnimBlueprint* AnimBlueprint,
        FSekiroAnimBlueprintIR& OutBlueprint,
        TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics);
};
