#pragma once

#include "CoreMinimal.h"

/**
 * AI操作访问控制
 *
 * 管理高风险操作的确认机制。
 * 支持：
 * - 按操作类型弹出确认对话框
 * - "本次会话记住"选项，避免重复确认
 * - 操作请求日志记录
 */
class SEKIROAIBRIDGE_API FSKAccessControl
{
public:
    /** 确认结果 */
    enum class EConfirmationResult : uint8
    {
        Denied,             // 用户拒绝
        AllowOnce,          // 允许本次
        AllowAlwaysSession, // 本次会话始终允许
    };

    FSKAccessControl();

    /**
     * 请求用户确认操作
     * @param ToolName  工具名称
     * @param Summary   操作摘要（显示在对话框中）
     * @param Details   操作详情参数
     * @return 确认结果
     */
    EConfirmationResult RequestConfirmation(const FName& ToolName, const FString& Summary, const FString& Details);

    /** 检查操作是否已被本次会话记住（总是允许） */
    bool IsAlwaysAllowed(const FName& ToolName) const;

    /** 重置本次会话的所有确认缓存 */
    void ResetSessionCache();

    /** 获取会话中已被允许的操作列表 */
    TArray<FName> GetAllowedOperations() const;

private:
    /** 本次会话中已记住"总是允许"的操作 */
    TSet<FName> AlwaysAllowedSet;
};
