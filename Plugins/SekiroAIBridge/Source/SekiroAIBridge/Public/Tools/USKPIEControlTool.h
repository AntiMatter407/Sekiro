#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ISKAITool.h"
#include "USKPIEControlTool.generated.h"

/**
 * PIE（Play In Editor）控制工具
 *
 * 支持启动/停止/暂停/恢复 PIE 会话，支持多种运行模式：
 * - selected: 当前视口内 PIE
 * - standalone: 独立进程
 * - mobile/vulkan/vr: 预览模式
 * - simulate: 模拟模式（无玩家）
 * - 多人模式：指定客户端数和网络模式
 */
UCLASS()
class SEKIROAIBRIDGE_API USKPIEControlTool : public UObject, public ISKAIToolInterface
{
	GENERATED_BODY()

public:
	virtual FName GetToolName() const override { return FName(TEXT("pie.control")); }
	virtual FString GetToolDescription() const override;
	virtual FString GetInputSchemaJson() const override;
	virtual FString Execute(const FString& ArgsJson, FString& OutError) override;
	virtual bool RequiresConfirmation() const override { return true; }
	virtual FString GetConfirmationSummary(const FString& ArgsJson) const override;

private:
	FString HandleStart(const TSharedPtr<FJsonObject>& Args, FString& OutError);
	FString HandleStop(FString& OutError);
	FString HandlePause(FString& OutError);
	FString HandleResume(FString& OutError);
	FString HandleStatus();
	FString HandleLateJoin(FString& OutError);
};
