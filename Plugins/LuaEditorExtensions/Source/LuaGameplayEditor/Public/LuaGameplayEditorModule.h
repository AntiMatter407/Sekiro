#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Widgets/SWidget.h"

class SDockTab;
class SVerticalBox;
class UToolMenu;
class FSpawnTabArgs;

/** 注册一个工具页的 Slate 内容工厂；只能在游戏线程调用，不接受空工厂。 */
DECLARE_DELEGATE_RetVal(TSharedRef<SWidget>, FLuaGameplayToolWidgetFactory);

class LUAGAMEPLAYEDITOR_API FLuaGameplayEditorModule : public IModuleInterface
{
public:
    // ── 模块与扩展接口 ──
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    static FLuaGameplayEditorModule& Get();
    bool RegisterTool(FName ToolId, const FText& DisplayName, FLuaGameplayToolWidgetFactory Factory);
    bool UnregisterTool(FName ToolId);
    void OpenToolsWindow();
    void OpenTool(FName ToolId);

private:
    // ── 窗口内容 ──
    struct FToolRegistration
    {
        FName Id; // 扩展提供的唯一标识
        FText DisplayName; // 工具页显示名
        FLuaGameplayToolWidgetFactory Factory; // 按需创建界面的工厂
    };

    void RegisterMenus();
    void BuildToolMenu(UToolMenu* Menu);
    TSharedRef<SDockTab> SpawnToolsTab(const FSpawnTabArgs& Args);
    void RebuildToolsContent();
    void SelectTool(FName ToolId);

    TArray<FToolRegistration> Tools; // 按注册顺序显示工具
    TWeakPtr<SVerticalBox> RootContent; // 打开窗口的容器，不延长窗口生命周期
    FName ActiveTool; // 当前工具页标识
    FText MenuLabel; // 随编辑器语言切换的顶层菜单和窗口名称
    FText MenuToolTip; // 顶层菜单的本地化用途说明
};
