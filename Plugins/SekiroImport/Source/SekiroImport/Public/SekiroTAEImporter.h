#pragma once

#include "CoreMinimal.h"
#include "SekiroAnimLogicIR.h"

/// TAE JSON 导入器：将 Phase 1 输出的 Sekiro_TAE_Logic.json 解析为 ABIR
class SEKIROIMPORT_API FSekiroTAEImporter
{
public:
    /// 从 JSON 文件导入 TAE 逻辑数据
    static FSKAnimLogicImportResult ImportFromFile(const FString& JsonPath);

    /// 从 JSON 字符串导入
    static FSKAnimLogicImportResult ImportFromString(const FString& JsonContent);

    /// 分析动画类别归属
    static FString InferCategoryFromAnimID(int32 AnimID);

    /// 将 JumpTableID 映射为动作枚举
    static ESKJumpTableAction MapJumpTableToAction(int32 JumpTableID);

    /// 分类 TAE Event Type
    static ESKTAEEventCategory ClassifyEventType(int32 Type, const FString& TypeName);

private:
    /// 解析单个动画的 JSON 对象
    static FSKAnimationLogicIR ParseAnimationEntry(const TSharedPtr<FJsonObject>& AnimObj,
        const FString& AnimName);

    /// 从 JumpTable 事件提取取消窗口
    static void ExtractCancelWindows(const TArray<FSKTAEEventIR>& Events,
        TArray<FSKCancelWindowIR>& OutWindows);

    /// 从 JumpTable 事件提取帧级行为标志
    static void ExtractFrameFlags(const TArray<FSKTAEEventIR>& Events,
        TMap<int32, int32>& OutFrameFlags);

    /// 构建状态机过渡规则
    static void BuildTransitions(const TMap<int32, FSKAnimationLogicIR>& AnimLogicMap,
        FSKStateMachineIR& OutSM);
};
