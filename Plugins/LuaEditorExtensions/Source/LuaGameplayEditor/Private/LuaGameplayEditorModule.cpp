#include "LuaGameplayEditorModule.h"

#include "LuaGameplayTagWidget.h"
#include "LuaGameplayUIImportWidget.h"
#include "Framework/Docking/TabManager.h"
#include "Internationalization/PolyglotTextData.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace LuaGameplayEditor
{
    const FName TabId(TEXT("LuaGameplayTools"));
    const FName TagToolId(TEXT("GameplayTags"));
    const FName UIImportToolId(TEXT("CombatUIImport"));

    /**
     * 创建随 UE 编辑器语言更新的中英文文本；仅在模块启动的游戏线程调用。
     * @param Key 本模块命名空间内唯一且非空的稳定文本键。
     * @param English 英文源文案，也是尚无翻译的语言使用的回退文案。
     * @param Chinese 中文文案，覆盖 zh 及其地区/书写形式的回退。
     * @return 注册到引擎本地化管理器的 FText；不修改当前语言，不依赖外部 locres。
     */
    FText MakeMenuText(const TCHAR* Key, const TCHAR* English, const TCHAR* Chinese)
    {
        FPolyglotTextData Data(ELocalizedTextSourceCategory::Editor, TEXT("LuaGameplayEditor"), Key, English, TEXT("en"));
        Data.AddLocalizedString(TEXT("zh"), Chinese);
        Data.AddLocalizedString(TEXT("zh-Hans"), Chinese);
        Data.AddLocalizedString(TEXT("zh-Hant"), Chinese);
        return Data.GetText();
    }
}

IMPLEMENT_MODULE(FLuaGameplayEditorModule, LuaGameplayEditor)

/** 模块启动时注册内置工具、停靠窗口与菜单；游戏线程。命令行模式不访问 Slate，但公共生成接口仍可用。 */
void FLuaGameplayEditorModule::StartupModule()
{
    if (IsRunningCommandlet()) return;
    MenuLabel = LuaGameplayEditor::MakeMenuText(TEXT("RootMenu"), TEXT("LuaGameplay"), TEXT("Lua玩法"));
    MenuToolTip = LuaGameplayEditor::MakeMenuText(TEXT("RootMenuToolTip"), TEXT("Lua gameplay authoring tools"), TEXT("Lua 玩法配置与资产生成工具"));
    const FText TagImportLabel = LuaGameplayEditor::MakeMenuText(TEXT("GameplayTagImport"), TEXT("Import GameplayTags from Lua"), TEXT("GameplayTag 的 Lua 导入"));
    RegisterTool(LuaGameplayEditor::TagToolId, TagImportLabel, FLuaGameplayToolWidgetFactory::CreateLambda([]()
    {
        return StaticCastSharedRef<SWidget>(SNew(SLuaGameplayTagWidget));
    }));
    const FText UIImportLabel = LuaGameplayEditor::MakeMenuText(TEXT("CombatUIImport"), TEXT("Import Combat UI Assets"), TEXT("原版战斗 UI 资源导入"));
    RegisterTool(LuaGameplayEditor::UIImportToolId, UIImportLabel, FLuaGameplayToolWidgetFactory::CreateLambda([]()
    {
        return StaticCastSharedRef<SWidget>(SNew(SLuaGameplayUIImportWidget));
    }));
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LuaGameplayEditor::TabId, FOnSpawnTab::CreateRaw(this, &FLuaGameplayEditorModule::SpawnToolsTab))
        .SetDisplayName(MenuLabel).SetMenuType(ETabSpawnerMenuType::Hidden);
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLuaGameplayEditorModule::RegisterMenus));
}

/** 卸载时移除菜单、窗口工厂及引用；游戏线程。扩展模块应先注销其工厂，防止卸载后回调。 */
void FLuaGameplayEditorModule::ShutdownModule()
{
    if (!IsRunningCommandlet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        TSharedPtr<SDockTab> ExistingTab = FGlobalTabmanager::Get()->FindExistingLiveTab(LuaGameplayEditor::TabId);
        if (ExistingTab) ExistingTab->RequestCloseTab();
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LuaGameplayEditor::TabId);
    }
    RootContent.Reset();
    Tools.Reset();
}

/** 游戏线程获取并按需加载模块；返回模块借用引用，使用者不能在模块卸载后保留使用。 */
FLuaGameplayEditorModule& FLuaGameplayEditorModule::Get()
{
    check(IsInGameThread());
    return FModuleManager::LoadModuleChecked<FLuaGameplayEditorModule>(TEXT("LuaGameplayEditor"));
}

/**
 * 注册额外工具页及其顶层菜单下拉项；仅游戏线程，下次展开菜单时自动读取最新列表。
 * @param ToolId 非 None 且唯一的稳定标识。
 * @param DisplayName 非空的菜单项/工具页名称，扩展提供方负责提供可本地化的 FText。
 * @param Factory 非空界面工厂，复制保存；扩展模块卸载前必须 UnregisterTool。
 * @return 注册成功 true；无效或重复时 false，不替换既有工具。
 */
bool FLuaGameplayEditorModule::RegisterTool(FName ToolId, const FText& DisplayName, FLuaGameplayToolWidgetFactory Factory)
{
    if (!IsInGameThread() || ToolId.IsNone() || DisplayName.IsEmpty() || !Factory.IsBound()) return false;
    for (const FToolRegistration& Tool : Tools)
    {
        if (Tool.Id == ToolId) return false;
    }
    Tools.Add({ ToolId, DisplayName, MoveTemp(Factory) });
    if (ActiveTool.IsNone()) ActiveTool = ToolId;
    RebuildToolsContent();
    return true;
}

/** 注销扩展页及菜单项并销毁其打开内容；仅游戏线程。ToolId 为已注册标识，返回是否实际移除，未知标识不修改其他工具。 */
bool FLuaGameplayEditorModule::UnregisterTool(FName ToolId)
{
    if (!IsInGameThread()) return false;
    const int32 Removed = Tools.RemoveAll([ToolId](const FToolRegistration& Tool) { return Tool.Id == ToolId; });
    if (!Removed) return false;
    if (ActiveTool == ToolId) ActiveTool = Tools.IsEmpty() ? NAME_None : Tools[0].Id;
    RebuildToolsContent();
    return true;
}

/** 打开或聚焦唯一 LuaGameplay 工具窗口；游戏线程，命令行模式忽略，不生成资产。 */
void FLuaGameplayEditorModule::OpenToolsWindow()
{
    if (!IsInGameThread() || IsRunningCommandlet()) return;
    FGlobalTabmanager::Get()->TryInvokeTab(LuaGameplayEditor::TabId);
}

/**
 * 直接打开已注册的功能页；仅游戏线程、非命令行模式，不自动生成资产。
 * @param ToolId 菜单绑定的稳定工具标识；若扩展已卸载或标识无效，则忽略请求。
 * 无返回值。同一页已打开时仅聚焦窗口，保留该页未提交的输入；切换功能沿用工具页重建流程。
 */
void FLuaGameplayEditorModule::OpenTool(FName ToolId)
{
    if (!IsInGameThread() || IsRunningCommandlet()) return;
    for (const FToolRegistration& Tool : Tools)
    {
        if (Tool.Id != ToolId) continue;
        if (ActiveTool != ToolId) SelectTool(ToolId);
        OpenToolsWindow();
        return;
    }
}

/** 将 LuaGameplay 作为独立顶层下拉菜单加入主菜单栏；游戏线程，挂在帮助前，菜单由模块所有权集中移除。 */
void FLuaGameplayEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu"));
    FToolMenuEntry& Entry = Menu->FindOrAddSection(NAME_None).AddSubMenu(
        TEXT("LuaGameplay"), MenuLabel, MenuToolTip,
        FNewToolMenuDelegate::CreateRaw(this, &FLuaGameplayEditorModule::BuildToolMenu));
    Entry.InsertPosition = FToolMenuInsert(TEXT("Help"), EToolMenuInsertType::Before);
}

/**
 * 每次展开下拉菜单时按当前注册表生成入口；仅游戏线程，不保存菜单指针，不生成资产。
 * @param Menu ToolMenus 创建的临时子菜单，非空时追加本模块拥有的功能项；空指针时无操作。
 * 无返回值。功能卸载后不会再生成该项；已展开菜单的旧回调仍由 OpenTool 校验标识。
 */
void FLuaGameplayEditorModule::BuildToolMenu(UToolMenu* Menu)
{
    if (!IsInGameThread() || !Menu) return;
    FToolMenuOwnerScoped Owner(this);
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("LuaGameplayTools"));
    for (const FToolRegistration& Tool : Tools)
    {
        Section.AddMenuEntry(Tool.Id, Tool.DisplayName, FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FLuaGameplayEditorModule::OpenTool, Tool.Id)));
    }
}

/** 创建停靠页；游戏线程。Args 为 Slate 调用上下文，返回新窗口，内容生命周期由窗口管理。 */
TSharedRef<SDockTab> FLuaGameplayEditorModule::SpawnToolsTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
    RootContent = Root;
    RebuildToolsContent();
    return SNew(SDockTab).TabRole(ETabRole::NomadTab)
        [ SNew(SBox).MinDesiredWidth(760).MinDesiredHeight(480)[ Root ] ];
}

/** 重建工具导航及当前页；游戏线程。无窗口时无操作，重新构建会释放旧页及其暂存输入，不修改资产。 */
void FLuaGameplayEditorModule::RebuildToolsContent()
{
    TSharedPtr<SVerticalBox> Root = RootContent.Pin();
    if (!Root) return;
    Root->ClearChildren();
    TSharedRef<SHorizontalBox> Navigation = SNew(SHorizontalBox);
    for (const FToolRegistration& Tool : Tools)
    {
        const FName ToolId = Tool.Id;
        Navigation->AddSlot().AutoWidth().Padding(4)
        [ SNew(SButton).Text(Tool.DisplayName).IsEnabled(ToolId != ActiveTool)
            .OnClicked_Lambda([this, ToolId]() { SelectTool(ToolId); return FReply::Handled(); }) ];
    }
    Root->AddSlot().AutoHeight()[ Navigation ];
    for (const FToolRegistration& Tool : Tools)
    {
        if (Tool.Id == ActiveTool && Tool.Factory.IsBound())
        {
            Root->AddSlot().FillHeight(1)[ Tool.Factory.Execute() ];
            return;
        }
    }
    Root->AddSlot().AutoHeight()[ SNew(STextBlock).Text(FText::FromString(TEXT("当前没有已注册的 Gameplay 工具。"))) ];
}

/** 切换当前工具页；游戏线程。ToolId 为导航按钮绑定的注册标识，不创建业务资产。 */
void FLuaGameplayEditorModule::SelectTool(FName ToolId)
{
    ActiveTool = ToolId;
    RebuildToolsContent();
}
