#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSekiroAnimBlueprintExt, Log, All);

class FSekiroAnimBlueprintExtModule : public IModuleInterface
{
public:
    /** 作用：启动运行时模块并记录加载日志。@param 无。@return void，无返回值。 */
    virtual void StartupModule() override;
    /** 作用：关闭运行时模块并记录卸载日志。@param 无。@return void，无返回值。 */
    virtual void ShutdownModule() override;
};
