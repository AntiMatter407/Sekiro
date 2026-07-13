#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSekiroAnimBlueprintExtEditor, Log, All);

class FSekiroAnimBlueprintExtEditorModule : public IModuleInterface
{
public:
    /**
     * 初始化 Sekiro 动画蓝图扩展编辑器模块及其日志。
     * 参数：无。
     * @return void，无返回值。
     */
    virtual void StartupModule() override;

    /**
     * 关闭 Sekiro 动画蓝图扩展编辑器模块并释放模块级资源。
     * 参数：无。
     * @return void，无返回值。
     */
    virtual void ShutdownModule() override;
};
