#pragma once

#include "Modules/ModuleManager.h"

class FSekiroLuaBehaviorTreeExtEditorModule final : public IModuleInterface
{
public:
    /** 注册编辑器工具栏扩展。 */
    virtual void StartupModule() override;

    /** 注销编辑器工具栏扩展。 */
    virtual void ShutdownModule() override;

private:
    /** 延迟注册 Behavior Tree 工具栏菜单。 */
    void RegisterMenus();
};
