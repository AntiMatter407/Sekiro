#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "USKAIBridgeSettings.generated.h"

/**
 * AI桥接设置（项目设置 > 插件 > Sekiro AI Bridge）
 *
 * 可通过 UE编辑器菜单 Edit > Project Settings > Plugins > Sekiro AI Bridge 配置。
 */
UCLASS(config=Editor, defaultconfig, meta=(DisplayName="Sekiro AI Bridge"))
class SEKIROAIBRIDGE_API USKAIBridgeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    USKAIBridgeSettings();

    /** 获取设置实例 */
    static USKAIBridgeSettings* Get();

    // ---- UDeveloperSettings 接口 ----
    virtual FName GetCategoryName() const override;
#if WITH_EDITOR
    virtual FText GetSectionText() const override;
#endif

    /** 监听端口号（默认 9877） */
    UPROPERTY(config, EditAnywhere, Category="Server", meta=(ClampMin="1024", ClampMax="65535"))
    int32 ServerPort;

    /** 是否在编辑器启动时自动开启服务端 */
    UPROPERTY(config, EditAnywhere, Category="Server")
    bool bAutoStartServer;

    /** 绑定地址（默认 127.0.0.1，仅本地访问） */
    UPROPERTY(config, EditAnywhere, Category="Server")
    FString BindAddress;

    /** 预共享密钥（空表示无认证） */
    UPROPERTY(config, EditAnywhere, Category="Security", meta=(PasswordField=true))
    FString PreSharedKey;

    /** 确认策略：Always=每次确认，Session=会话内记住，None=永不确认（危险！） */
    UPROPERTY(config, EditAnywhere, Category="Security")
    FString ConfirmationPolicy;

    /** 是否为只读模式（拒绝所有高风险操作） */
    UPROPERTY(config, EditAnywhere, Category="Security")
    bool bReadOnlyMode;
};
