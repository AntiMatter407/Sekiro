#include "SekiroLuaBehaviorTreeExtEditorModule.h"

#include "BehaviorTree/BehaviorTree.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "SekiroBehaviorTreeFactoryLibrary.h"
#include "SekiroBehaviorTreeExporterLibrary.h"
#include "SekiroBehaviorTreeIR.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SekiroLuaBehaviorTreeExtEditor"

namespace
{
    const FName BehaviorTreeToolbarName(
        TEXT("AssetEditor.Behavior Tree.ToolBar.BehaviorTree"));
    const FName MessageLogName(TEXT("SekiroLuaBehaviorTree"));

    /**
     * 从工具菜单上下文解析当前行为树编辑器正在编辑的资产。
     * 本函数只读取上下文且只能在编辑器游戏线程调用；找不到行为树时返回 nullptr。
     *
     * @param Menu 正在动态构造的行为树工具栏菜单。
     * @return 当前编辑的第一个 UBehaviorTree，或 nullptr。
     */
    UBehaviorTree* ResolveBehaviorTree(UToolMenu* Menu)
    {
        if (!Menu) return nullptr;
        const UAssetEditorToolkitMenuContext* ToolkitContext =
            Menu->FindContext<UAssetEditorToolkitMenuContext>();
        if (!ToolkitContext) return nullptr;

        const TArray<UObject*> EditingObjects = ToolkitContext->GetEditingObjects();
        for (UObject* EditingObject : EditingObjects)
        {
            UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(EditingObject);
            if (BehaviorTree) return BehaviorTree;
        }
        return nullptr;
    }

    /**
     * 把结构化诊断写入 Message Log，并显示一次简短的编辑器通知。
     * 只能在编辑器游戏线程调用；本函数不修改资产，也不会打开 PIE。
     *
     * @param ActionName 用户触发的操作名称。
     * @param bSucceeded 操作是否成功。
     * @param Diagnostics 要展示的完整诊断集合。
     */
    void ReportDiagnostics(
        const FText& ActionName,
        const bool bSucceeded,
        const TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics)
    {
        FMessageLog MessageLog(MessageLogName);
        for (const FSekiroBehaviorTreeDiagnostic& Diagnostic : Diagnostics)
        {
            const FText Text = FText::FromString(FString::Printf(
                TEXT("[%s] %s (%s)"),
                *Diagnostic.Code.ToString(),
                *Diagnostic.Message,
                *Diagnostic.Path));
            if (Diagnostic.Severity == ESekiroBehaviorTreeDiagnosticSeverity::Error)
                MessageLog.Error(Text);
            else if (Diagnostic.Severity == ESekiroBehaviorTreeDiagnosticSeverity::Warning)
                MessageLog.Warning(Text);
            else
                MessageLog.Info(Text);
        }
        if (!bSucceeded) MessageLog.Open(EMessageSeverity::Error, true);

        const FText NotificationText = FText::Format(
            bSucceeded
                ? LOCTEXT("ActionSucceeded", "{0}成功。")
                : LOCTEXT("ActionFailed", "{0}失败，请查看 Message Log。"),
            ActionName);
        FNotificationInfo NotificationInfo(NotificationText);
        NotificationInfo.ExpireDuration = bSucceeded ? 3.0f : 6.0f;
        TSharedPtr<SNotificationItem> Notification =
            FSlateNotificationManager::Get().AddNotification(NotificationInfo);
        if (Notification.IsValid())
        {
            Notification->SetCompletionState(
                bSucceeded
                    ? SNotificationItem::CS_Success
                    : SNotificationItem::CS_Fail);
        }
    }

    /**
     * 判断当前资产是否允许执行显式 Lua 检查或双向同步。
     * PIE/SIE 期间禁止结构与文件操作，LuaModuleName 为空时也禁用按钮。
     *
     * @param BehaviorTree 目标行为树弱引用。
     * @return 资产有效、模块已配置且未运行 PIE/SIE 时返回 true。
     */
    bool CanRunAssetAction(const TWeakObjectPtr<UBehaviorTree> BehaviorTree)
    {
        if (!BehaviorTree.IsValid()
            || !GEditor
            || GEditor->PlayWorld
            || GEditor->bIsSimulatingInEditor)
        {
            return false;
        }
        FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
        return USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                BehaviorTree.Get(),
                Configuration)
            && !Configuration.LuaModuleName.IsEmpty();
    }

    /**
     * 向行为树模式工具栏动态添加 Source Mode、Lua 模块输入框和三个显式操作按钮。
     * 每次构造都从 UAssetEditorToolkitMenuContext 获取当前资产，因此多个编辑器窗口互不串用配置。
     *
     * @param Menu 当前工具栏菜单。
     */
    void BuildBehaviorTreeToolbar(UToolMenu* Menu)
    {
        UBehaviorTree* BehaviorTree = ResolveBehaviorTree(Menu);
        if (!BehaviorTree) return;

        const TWeakObjectPtr<UBehaviorTree> WeakBehaviorTree(BehaviorTree);
        FToolMenuSection& Section = Menu->AddSection(
            TEXT("SekiroLuaBehaviorTree"),
            LOCTEXT("ToolbarSection", "Lua Behavior Tree"));

        const TSharedRef<SComboButton> SourceModeCombo =
            SNew(SComboButton)
            .ToolTipText(LOCTEXT(
                "SourceModeTooltip",
                "仅记录最近选择或同步来源；切换不会导入、导出、编译或保存。"))
            .IsEnabled_Lambda([WeakBehaviorTree]()
            {
                return WeakBehaviorTree.IsValid()
                    && GEditor
                    && !GEditor->PlayWorld
                    && !GEditor->bIsSimulatingInEditor;
            })
            .OnGetMenuContent_Lambda([WeakBehaviorTree]()
            {
                FMenuBuilder MenuBuilder(true, nullptr);
                MenuBuilder.AddMenuEntry(
                    LOCTEXT("SourceBehaviorTree", "Source: BehaviorTree"),
                    LOCTEXT("SourceBehaviorTreeTooltip", "仅记录 BehaviorTree 为最近来源。"),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([WeakBehaviorTree]()
                    {
                        FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                        USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                            WeakBehaviorTree.Get(),
                            Configuration);
                        Configuration.SourceMode =
                            ESekiroLuaBehaviorTreeSourceMode::BehaviorTree;
                        USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
                            WeakBehaviorTree.Get(),
                            Configuration);
                    })));
                MenuBuilder.AddMenuEntry(
                    LOCTEXT("SourceLua", "Source: Lua"),
                    LOCTEXT("SourceLuaTooltip", "仅记录 Lua 为最近来源。"),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([WeakBehaviorTree]()
                    {
                        FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                        USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                            WeakBehaviorTree.Get(),
                            Configuration);
                        Configuration.SourceMode = ESekiroLuaBehaviorTreeSourceMode::Lua;
                        USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
                            WeakBehaviorTree.Get(),
                            Configuration);
                    })));
                return MenuBuilder.MakeWidget();
            })
            .ButtonContent()
            [
                SNew(STextBlock)
                .Text_Lambda([WeakBehaviorTree]()
                {
                    FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                    USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                        WeakBehaviorTree.Get(),
                        Configuration);
                    return Configuration.SourceMode
                        == ESekiroLuaBehaviorTreeSourceMode::Lua
                        ? LOCTEXT("CurrentSourceLua", "Source: Lua")
                        : LOCTEXT("CurrentSourceBehaviorTree", "Source: BehaviorTree");
                })
            ];
        Section.AddEntry(FToolMenuEntry::InitWidget(
            TEXT("SekiroLuaSourceMode"),
            SourceModeCombo,
            LOCTEXT("SourceModeLabel", "Source Mode"),
            true,
            false));

        const TSharedRef<SEditableTextBox> ModuleTextBox =
            SNew(SEditableTextBox)
            .MinDesiredWidth(220.0f)
            .HintText(LOCTEXT("LuaModuleHint", "LuaModuleName"))
            .ToolTipText(LOCTEXT(
                "LuaModuleTooltip",
                "为当前 Behavior Tree 资产保存 Lua require 模块名。"))
            .IsEnabled_Lambda([WeakBehaviorTree]()
            {
                return WeakBehaviorTree.IsValid()
                    && GEditor
                    && !GEditor->PlayWorld
                    && !GEditor->bIsSimulatingInEditor;
            })
            .Text_Lambda([WeakBehaviorTree]()
            {
                FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                    WeakBehaviorTree.Get(),
                    Configuration);
                return FText::FromString(Configuration.LuaModuleName);
            })
            .OnTextCommitted_Lambda(
                [WeakBehaviorTree](const FText& Text, ETextCommit::Type)
                {
                    UBehaviorTree* CurrentBehaviorTree = WeakBehaviorTree.Get();
                    if (!CurrentBehaviorTree) return;
                    FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                    USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                        CurrentBehaviorTree,
                        Configuration);
                    Configuration.LuaModuleName = Text.ToString().TrimStartAndEnd();
                    USekiroBehaviorTreeFactoryLibrary::SetLuaAssetConfiguration(
                        CurrentBehaviorTree,
                        Configuration);
                });

        Section.AddEntry(FToolMenuEntry::InitWidget(
            TEXT("SekiroLuaModuleName"),
            ModuleTextBox,
            LOCTEXT("LuaModuleLabel", "Lua Module"),
            true,
            false));

        const FUIAction CheckAction(
            FExecuteAction::CreateLambda([WeakBehaviorTree]()
            {
                TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
                const bool bSucceeded =
                    USekiroBehaviorTreeFactoryLibrary::CheckConfiguredBehaviorTree(
                        WeakBehaviorTree.Get(),
                        Diagnostics);
                ReportDiagnostics(
                    LOCTEXT("CheckAction", "Lua 行为树检查"),
                    bSucceeded,
                    Diagnostics);
            }),
            FCanExecuteAction::CreateLambda(
                [WeakBehaviorTree]()
                {
                    return CanRunAssetAction(WeakBehaviorTree);
                }));
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("SekiroLuaCheck"),
            CheckAction,
            LOCTEXT("CheckLabel", "Check Lua"),
            LOCTEXT("CheckTooltip", "校验当前资产绑定的 Lua 行为树，不修改资产。"),
            FSlateIcon()));

        const FUIAction ImportAction(
            FExecuteAction::CreateLambda([WeakBehaviorTree]()
            {
                UBlackboardData* Blackboard = nullptr;
                UBehaviorTree* GeneratedBehaviorTree = nullptr;
                TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
                const bool bSucceeded =
                    USekiroBehaviorTreeFactoryLibrary::GenerateConfiguredBehaviorTree(
                        WeakBehaviorTree.Get(),
                        true,
                        Blackboard,
                        GeneratedBehaviorTree,
                        Diagnostics);
                ReportDiagnostics(
                    LOCTEXT("ImportAction", "Lua → BehaviorTree"),
                    bSucceeded,
                    Diagnostics);
            }),
            FCanExecuteAction::CreateLambda(
                [WeakBehaviorTree]()
                {
                    return CanRunAssetAction(WeakBehaviorTree);
                }));
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("SekiroLuaImport"),
            ImportAction,
            LOCTEXT("ImportLabel", "Lua → BehaviorTree"),
            LOCTEXT(
                "ImportTooltip",
                "仅在点击时使用 Lua 原地重建并保存 BehaviorTree 与 Blackboard。"),
            FSlateIcon()));

        const FUIAction ExportAction(
            FExecuteAction::CreateLambda([WeakBehaviorTree]()
            {
                FSekiroLuaBehaviorTreeAssetConfiguration Configuration;
                USekiroBehaviorTreeFactoryLibrary::GetLuaAssetConfiguration(
                    WeakBehaviorTree.Get(),
                    Configuration);
                const EAppReturnType::Type Confirmation = FMessageDialog::Open(
                    EAppMsgType::YesNo,
                    LOCTEXT(
                        "ExportConfirmation",
                        "将把当前 BehaviorTree 写入对应 Lua 文件；若文件已存在会覆盖。是否继续？"));
                if (Confirmation != EAppReturnType::Yes) return;

                FString FilePath;
                TArray<FSekiroBehaviorTreeDiagnostic> Diagnostics;
                const bool bSucceeded =
                    USekiroBehaviorTreeExporterLibrary::ExportBehaviorTreeToLua(
                        WeakBehaviorTree.Get(),
                        Configuration.LuaModuleName,
                        true,
                        FilePath,
                        Diagnostics);
                ReportDiagnostics(
                    LOCTEXT("ExportAction", "BehaviorTree → Lua"),
                    bSucceeded,
                    Diagnostics);
            }),
            FCanExecuteAction::CreateLambda([WeakBehaviorTree]()
            {
                return CanRunAssetAction(WeakBehaviorTree);
            }));
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("SekiroLuaExport"),
            ExportAction,
            LOCTEXT("ExportLabel", "BehaviorTree → Lua"),
            LOCTEXT(
                "ExportTooltip",
                "仅在点击并确认时把当前资产写入 Lua，并执行 round-trip 校验。"),
            FSlateIcon()));
    }
}

/**
 * 注册行为树编辑器工具栏扩展。
 * 模块启动发生在编辑器主线程；实际菜单注册延迟到 ToolMenus 可用时执行。
 */
void FSekiroLuaBehaviorTreeExtEditorModule::StartupModule()
{
    FMessageLogModule& MessageLogModule =
        FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
    MessageLogModule.RegisterLogListing(
        MessageLogName,
        LOCTEXT("MessageLogLabel", "Lua Behavior Tree"));
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this,
            &FSekiroLuaBehaviorTreeExtEditorModule::RegisterMenus));
}

/**
 * 注销本模块持有的 ToolMenus 回调和菜单条目。
 * 模块卸载发生在编辑器主线程；关闭阶段 ToolMenus 不可用时跳过对象注销。
 */
void FSekiroLuaBehaviorTreeExtEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    if (UToolMenus::IsToolMenuUIEnabled())
        UToolMenus::UnregisterOwner(this);
    if (FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
    {
        FMessageLogModule& MessageLogModule =
            FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
        MessageLogModule.UnregisterLogListing(MessageLogName);
    }
}

/**
 * 把动态 Lua 控件挂到 BehaviorTree 工作流模式工具栏。
 * 只能在 ToolMenus 启动完成后的编辑器主线程调用；重复注册由 Owner 作用域管理。
 */
void FSekiroLuaBehaviorTreeExtEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(BehaviorTreeToolbarName);
    Toolbar->AddDynamicSection(
        TEXT("SekiroLuaBehaviorTree"),
        FNewToolMenuDelegate::CreateStatic(&BuildBehaviorTreeToolbar));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(
    FSekiroLuaBehaviorTreeExtEditorModule,
    SekiroLuaBehaviorTreeExtEditor)
