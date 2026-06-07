#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UToolMenu;

class FSekiroImportModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /// 打开Sekiro导入对话框
    static void OpenImportDialog();
    /// 填充Sekiro顶级菜单
    static void PopulateSekiroMenu(UToolMenu* Menu);
    /// 创建可覆盖的Package（处理已存在的部分加载包）
    static UPackage* CreatePackageForOverwrite(const FString& PackagePath);

private:
    /// 注册菜单
    void RegisterMenus();
    void UnregisterMenus();

    /// ToolMenus句柄
    TArray<FDelegateHandle> MenuExtenderHandles;
};
