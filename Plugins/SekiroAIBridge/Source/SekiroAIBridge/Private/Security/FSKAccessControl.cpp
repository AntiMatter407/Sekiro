#include "Security/FSKAccessControl.h"
#include "SekiroAIBridgeLog.h"
#include "Misc/MessageDialog.h"
#include "Internationalization/Text.h"

FSKAccessControl::FSKAccessControl()
{
}

FSKAccessControl::EConfirmationResult FSKAccessControl::RequestConfirmation(
    const FName& ToolName, const FString& Summary, const FString& Details)
{
    // 检查是否已被本次会话永久允许
    if (AlwaysAllowedSet.Contains(ToolName))
    {
        UE_LOG(LogSekiroAIBridge, Log, TEXT("[访问控制] %s: 会话级自动允许"), *ToolName.ToString());
        return EConfirmationResult::AllowAlwaysSession;
    }

    // 构建对话消息
    FText Title = FText::FromString(TEXT("AI Bridge - 操作确认"));
    FString MessageStr = FString::Printf(
        TEXT("外部AI请求执行操作：\n\n工具: %s\n摘要: %s\n\n参数详情:\n%s\n\n是否允许？\n\n[是] = 仅此一次\n[全是] = 本次会话全部允许\n[否] = 拒绝"),
        *ToolName.ToString(),
        *Summary,
        *Details
    );
    FText Message = FText::FromString(MessageStr);

    EAppReturnType::Type Return = FMessageDialog::Open(EAppMsgType::YesNoYesAll, Message, Title);

    switch (Return)
    {
    case EAppReturnType::YesAll:
        AlwaysAllowedSet.Add(ToolName);
        UE_LOG(LogSekiroAIBridge, Log, TEXT("[访问控制] %s: 用户选择'本次会话始终允许'"), *ToolName.ToString());
        return EConfirmationResult::AllowAlwaysSession;

    case EAppReturnType::Yes:
        UE_LOG(LogSekiroAIBridge, Log, TEXT("[访问控制] %s: 用户允许（仅此一次）"), *ToolName.ToString());
        return EConfirmationResult::AllowOnce;

    default:
        UE_LOG(LogSekiroAIBridge, Log, TEXT("[访问控制] %s: 用户拒绝"), *ToolName.ToString());
        return EConfirmationResult::Denied;
    }
}

bool FSKAccessControl::IsAlwaysAllowed(const FName& ToolName) const
{
    return AlwaysAllowedSet.Contains(ToolName);
}

void FSKAccessControl::ResetSessionCache()
{
    AlwaysAllowedSet.Empty();
    UE_LOG(LogSekiroAIBridge, Log, TEXT("[访问控制] 会话缓存已重置"));
}

TArray<FName> FSKAccessControl::GetAllowedOperations() const
{
    return AlwaysAllowedSet.Array();
}
