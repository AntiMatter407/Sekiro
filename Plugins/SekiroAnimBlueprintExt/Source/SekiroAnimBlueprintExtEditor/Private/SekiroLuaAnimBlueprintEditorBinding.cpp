#include "SekiroLuaAnimBlueprintEditorBinding.h"

#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IAnimationBlueprintEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "SekiroAnimBlueprintFactoryLibrary.h"
#include "SekiroLuaAnimBlueprintExtension.h"
#include "Styling/AppStyle.h"
#include "UnLua.h"
#include "UnLuaModule.h"
#include "UnLuaSettings.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SekiroLuaAnimBlueprintEditorBinding"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroLuaAnimBlueprintEditorBinding, Log, All);

namespace
{
    const FName CheckLuaToolbarBlockName(TEXT("Sekiro.CheckLua"));
    const FName GenerateFromLuaToolbarBlockName(TEXT("Sekiro.GenerateFromLua"));
    const FName ExportToLuaToolbarBlockName(TEXT("Sekiro.ExportToLua"));
    const FName SyncStatusToolbarBlockName(TEXT("Sekiro.SyncStatus"));
    const FName EditorLuaDebugToolbarBlockName(TEXT("Sekiro.EditorLuaDebug"));
    const FName LuaModuleToolbarBlockName(TEXT("Sekiro.LuaModule"));
    const TCHAR* EditorLuaDebugConfigSection = TEXT("SekiroAnimBlueprintExtEditor.LuaDebug");
    const TCHAR* EditorLuaDebugConfigKey = TEXT("EnableEditorDebug");
    const FString LuaDebuggerModuleName(TEXT("Debug.LuaDebugger"));

    /**
     * 在当前编辑器 UnLua Env 中加载 LuaDebugger 模块并调用 Start 或 Stop。
     * 只能在游戏线程调用；会激活 UnLua，但不进入 PIE、不加载游戏地图。
     *
     * @param FunctionName LuaDebugger 导出的无参函数名，必须为 Start 或 Stop。
     * @return Lua 模块成功返回 true 时返回 true；环境、模块或函数不可用时返回 false。
     */
    bool CallLuaDebuggerControlFunction(const char* FunctionName)
    {
        if (!IsInGameThread() || FunctionName == nullptr) return false;

        IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
        if (UnLuaModule == nullptr) return false;
        if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);

        UnLua::FLuaEnv* LuaEnv = UnLuaModule->GetEnv();
        if (LuaEnv == nullptr) return false;
        lua_State* LuaState = LuaEnv->GetMainState();
        if (LuaState == nullptr) return false;

        const FTCHARToUTF8 LuaModuleNameUtf8(*LuaDebuggerModuleName);
        UnLua::FLuaRetValues RequireReturnValues =
            UnLua::Call(LuaState, "require", LuaModuleNameUtf8.Get());
        if (!RequireReturnValues.IsValid()
            || RequireReturnValues.Num() == 0
            || RequireReturnValues[0].GetType() != LUA_TTABLE)
        {
            return false;
        }

        UnLua::FLuaTable ModuleTable(LuaEnv, RequireReturnValues[0]);
        UnLua::FLuaRetValues FunctionReturnValues = ModuleTable.Call(FunctionName);
        return FunctionReturnValues.IsValid()
            && FunctionReturnValues.Num() > 0
            && FunctionReturnValues[0].GetType() == LUA_TBOOLEAN
            && FunctionReturnValues[0].Value<bool>();
    }

    /**
     * 将编辑器 Lua 调试开关保存到本用户的 EditorPerProjectUserSettings。
     * 只能在游戏线程调用；不修改项目 DefaultConfig，也不标记任何资产。
     *
     * @param bEnabled true 表示编辑器阶段开启 9966 调试端口，false 表示只在 PIE 由 Main.lua 开启。
     */
    void SaveEditorLuaDebugEnabled(const bool bEnabled)
    {
        if (GConfig == nullptr) return;
        GConfig->SetBool(
            EditorLuaDebugConfigSection,
            EditorLuaDebugConfigKey,
            bEnabled,
            GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }

    /**
     * 将结构化诊断逐条写入编辑器日志，使 Lua 模块、行号和列号可直接用于 IDE 定位。
     * 可在 Check、Generate 或模式化 Compile 后于游戏线程调用；函数不修改资产。
     *
     * @param Operation 当前工具操作显示名。
     * @param AnimBlueprint 诊断所属动画蓝图，可为空。
     * @param Diagnostics 工厂返回的只读诊断集合。
     */
    void LogDiagnostics(
        const TCHAR* Operation,
        const UAnimBlueprint* AnimBlueprint,
        const TArray<FSekiroAnimIRDiagnostic>& Diagnostics)
    {
        for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
        {
            if (Diagnostic.Severity == ESekiroAnimIRDiagnosticSeverity::Error)
            {
                UE_LOG(
                    LogSekiroLuaAnimBlueprintEditorBinding,
                    Error,
                    TEXT("%s [%s] %s: %s (%s:%d:%d)"),
                    Operation,
                    AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"),
                    *Diagnostic.Code.ToString(),
                    *Diagnostic.Message,
                    *Diagnostic.SourceLocation.LuaModule,
                    Diagnostic.SourceLocation.Line,
                    Diagnostic.SourceLocation.Column);
            }
            else
            {
                UE_LOG(
                    LogSekiroLuaAnimBlueprintEditorBinding,
                    Warning,
                    TEXT("%s [%s] %s: %s (%s:%d:%d)"),
                    Operation,
                    AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"),
                    *Diagnostic.Code.ToString(),
                    *Diagnostic.Message,
                    *Diagnostic.SourceLocation.LuaModule,
                    Diagnostic.SourceLocation.Line,
                    Diagnostic.SourceLocation.Column);
            }
        }
    }
}

/**
 * 在 UnLua Env 创建前把持久化开关投影到本进程 UUnLuaSettings::StartupModuleName。
 * 只修改内存默认对象，不保存 UnLua 项目配置；非游戏线程不得调用。
 */
void FSekiroLuaAnimBlueprintEditorBinding::PrepareEditorLuaDebugBeforeEnvCreation()
{
    UUnLuaSettings* UnLuaSettings = GetMutableDefault<UUnLuaSettings>();
    if (UnLuaSettings == nullptr) return;

    if (IsEditorLuaDebugEnabled())
    {
        if (UnLuaSettings->StartupModuleName.IsEmpty()
            || UnLuaSettings->StartupModuleName == LuaDebuggerModuleName)
        {
            UnLuaSettings->StartupModuleName = LuaDebuggerModuleName;
        }
    }
    else if (UnLuaSettings->StartupModuleName == LuaDebuggerModuleName)
    {
        UnLuaSettings->StartupModuleName.Reset();
    }
}

/**
 * 把已保存的编辑器 Lua 调试开关应用到当前 UnLua Env。
 * 开关关闭时不会为了 Stop 而加载调试模块；只能在游戏线程调用。
 *
 * @return 开关关闭时返回 true；开关开启时仅在 LuaDebugger.Start 成功后返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::ApplyEditorLuaDebugSetting()
{
    return !IsEditorLuaDebugEnabled()
        || CallLuaDebuggerControlFunction("Start");
}

/**
 * 读取本用户的编辑器 Lua 调试持久化开关。
 * 本函数只读取 EditorPerProjectUserSettings，缺少配置时默认关闭。
 *
 * @return 允许非 PIE 编辑器环境开启 Lua 调试端口时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::IsEditorLuaDebugEnabled()
{
    bool bEnabled = false;
    if (GConfig != nullptr)
    {
        GConfig->GetBool(
            EditorLuaDebugConfigSection,
            EditorLuaDebugConfigKey,
            bEnabled,
            GEditorPerProjectIni);
    }
    return bEnabled;
}

/**
 * 创建生产环境编辑器绑定，只添加 Lua 手动操作，不查询或重绑定原生 Compile/F7。
 * 必须由官方动画蓝图工具栏扩展回调在游戏线程调用；返回对象不拥有 Editor 或 CommandList。
 *
 * @param CommandList 动画蓝图编辑器的有效 Toolkit 命令列表。
 * @param Editor 当前动画蓝图编辑器共享引用。
 * @return 已完成命令包装、可直接填充工具栏的绑定对象。
 */
TSharedRef<FSekiroLuaAnimBlueprintEditorBinding>
FSekiroLuaAnimBlueprintEditorBinding::Create(
    const TSharedRef<FUICommandList>& CommandList,
    const TSharedRef<IAnimationBlueprintEditor>& Editor)
{
    return MakeShareable(new FSekiroLuaAnimBlueprintEditorBinding(
        CommandList,
        Editor,
        nullptr));
}

/**
 * 创建不依赖实际编辑器窗口的测试绑定，不触碰传入命令列表的任何原生动作。
 * 只能在游戏线程测试中调用；绑定不拥有 AnimBlueprint。
 *
 * @param CommandList 测试命令列表；已映射命令必须保持原样。
 * @param AnimBlueprint 测试目标动画蓝图，可为空以验证禁用行为。
 * @return 只提供 Lua 工具栏的测试绑定对象。
 */
TSharedRef<FSekiroLuaAnimBlueprintEditorBinding>
FSekiroLuaAnimBlueprintEditorBinding::CreateForTest(
    const TSharedRef<FUICommandList>& CommandList,
    UAnimBlueprint* AnimBlueprint)
{
    return MakeShareable(new FSekiroLuaAnimBlueprintEditorBinding(
        CommandList,
        nullptr,
        AnimBlueprint));
}

/**
 * 返回当前动画蓝图编辑器唯一的工具栏扩展实例，供 UE 多次重建工具栏时重复使用。
 * 必须在游戏线程调用；首次调用创建并配置扩展，后续调用返回同一共享实例。
 * 不假设 UE 会创建固定数量或固定顺序的 MultiBox，具体去重由 FillToolbar 针对目标 MultiBox 完成。
 *
 * @return 已绑定 FillToolbar 委托的稳定 Extender，共享所有权由绑定和编辑器共同持有。
 */
TSharedRef<FExtender> FSekiroLuaAnimBlueprintEditorBinding::GetToolbarExtender()
{
    if (!ToolbarExtender.IsValid())
    {
        ToolbarExtender = MakeShared<FExtender>();
        ToolbarExtender->AddToolBarExtension(
            TEXT("Compile"),
            EExtensionHook::After,
            CommandList.Pin(),
            FToolBarExtensionDelegate::CreateSP(
                SharedThis(this),
                &FSekiroLuaAnimBlueprintEditorBinding::FillToolbar));
    }
    return ToolbarExtender.ToSharedRef();
}

/**
 * 在原生 Compile 区段后添加 Check Lua、双向同步、同步状态、模块配置与调试控件。
 * 只能在工具栏构建阶段于游戏线程调用；首次收到的有效 MultiBox 会立即填充，不依赖构建顺序。
 * 绑定通过弱引用记录每个已填充 MultiBox；同一实例重复回调时跳过，失效实例会被及时清理。
 *
 * @param ToolbarBuilder 当前动画蓝图编辑器工具栏构建器，仅在调用期间有效。
 */
void FSekiroLuaAnimBlueprintEditorBinding::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
    const TSharedRef<FMultiBox> MultiBox = ToolbarBuilder.GetMultiBox();
    FilledToolbarMultiBoxes.RemoveAll([](const TWeakPtr<FMultiBox>& Candidate)
    {
        return !Candidate.IsValid();
    });
    for (const TWeakPtr<FMultiBox>& Candidate : FilledToolbarMultiBoxes)
    {
        const TSharedPtr<FMultiBox> FilledMultiBox = Candidate.Pin();
        if (FilledMultiBox.Get() == &MultiBox.Get()) return;
    }
    FilledToolbarMultiBoxes.Add(MultiBox);

    ToolbarBuilder.AddToolBarButton(
        FUIAction(
            FExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::ExecuteCheckLua),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction)),
        CheckLuaToolbarBlockName,
        LOCTEXT("CheckLuaLabel", "Check Lua"),
        LOCTEXT("CheckLuaTooltip", "Load and validate Lua, then cache IR without changing the Animation Blueprint Graph."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Check"));
    ToolbarBuilder.AddToolBarButton(
        FUIAction(
            FExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::ExecuteGenerateFromLua),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction)),
        GenerateFromLuaToolbarBlockName,
        LOCTEXT("GenerateFromLuaLabel", "Lua → AnimBlueprint"),
        LOCTEXT("GenerateFromLuaTooltip", "Explicitly import Lua IR, rebuild the Graph, and run one native compile without saving. Compile/F7 and PIE never run this action automatically."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));
    ToolbarBuilder.AddToolBarButton(
        FUIAction(
            FExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::ExecuteExportToLua),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction)),
        ExportToLuaToolbarBlockName,
        LOCTEXT("ExportToLuaLabel", "AnimBlueprint → Lua"),
        LOCTEXT("ExportToLuaTooltip", "Explicitly export the current Graph to the generated Lua exchange module without changing the Graph."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));
    ToolbarBuilder.AddToolBarButton(
        FUIAction(
            FExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::ExecuteRefreshSyncStatus),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction)),
        SyncStatusToolbarBlockName,
        TAttribute<FText>::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::GetSyncStatusText),
        LOCTEXT("SyncStatusTooltip", "Read both Canonical IR sources and refresh synchronization status without changing the Graph or Lua file."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));
    ToolbarBuilder.AddWidget(
        SNew(SEditableTextBox)
        .MinDesiredWidth(240.0f)
        .Text(this, &FSekiroLuaAnimBlueprintEditorBinding::GetLuaModuleNameText)
        .HintText(LOCTEXT("LuaModuleHint", "Lua module"))
        .ToolTipText(LOCTEXT(
            "LuaModuleTooltip",
            "Lua module owned by this AnimBlueprint. Inherited values are shown until this child saves an override."))
        .IsEnabled(this, &FSekiroLuaAnimBlueprintEditorBinding::CanEditLuaModule)
        .OnTextCommitted(this, &FSekiroLuaAnimBlueprintEditorBinding::CommitLuaModuleName),
        LuaModuleToolbarBlockName);
    ToolbarBuilder.AddToolBarButton(
        FUIAction(
            FExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::ExecuteToggleEditorLuaDebug),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanToggleEditorLuaDebug),
            FIsActionChecked::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::IsEditorLuaDebugChecked)),
        EditorLuaDebugToolbarBlockName,
        TAttribute<FText>::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::GetEditorLuaDebugLabel),
        LOCTEXT(
            "EditorLuaDebugTooltip",
            "When enabled, listen on Lua debug port 9966 before PIE so Check Lua and CompileIR breakpoints can be hit. When disabled, Main.lua starts debugging after PIE begins."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
        EUserInterfaceActionType::ToggleButton);
}

/**
 * 判断绑定依赖的命令列表是否仍然存在，用于模块清理已经关闭的动画蓝图编辑器记录。
 * 本函数不访问 UObject，可在游戏线程的工具栏扩展回调中调用。
 *
 * @return 命令列表仍有效时返回 true，否则返回 false。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::IsValid() const
{
    return CommandList.IsValid();
}

/**
 * 判断本绑定是否已经包装指定命令列表，用于工具栏重复重建时复用绑定并避免包装自身。
 *
 * @param InCommandList 待比较的命令列表。
 * @return 两个共享引用指向同一命令列表时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::UsesCommandList(
    const TSharedRef<FUICommandList>& InCommandList) const
{
    return CommandList.Pin() == InCommandList;
}

/**
 * 保存生产或测试上下文的弱引用，不查询、捕获或重绑定命令列表中的 Compile/F7。
 *
 * @param InCommandList 有效 Toolkit 或测试命令列表。
 * @param InEditor 生产环境编辑器，可为空。
 * @param AnimBlueprint 测试环境资产；生产环境应为空。
 */
FSekiroLuaAnimBlueprintEditorBinding::FSekiroLuaAnimBlueprintEditorBinding(
    const TSharedRef<FUICommandList>& InCommandList,
    const TSharedPtr<IAnimationBlueprintEditor>& InEditor,
    UAnimBlueprint* AnimBlueprint)
    : CommandList(InCommandList)
    , Editor(InEditor)
    , TestAnimBlueprint(AnimBlueprint)
{
}

/**
 * 返回绑定当前编辑器正在编辑的唯一 UAnimBlueprint；测试绑定则返回显式弱引用。
 * 只能在游戏线程读取编辑器状态。
 *
 * @return 有效动画蓝图，编辑器关闭、编辑多个对象或对象失效时返回 nullptr。
 */
UAnimBlueprint* FSekiroLuaAnimBlueprintEditorBinding::GetAnimBlueprint() const
{
    const TSharedPtr<IAnimationBlueprintEditor> PinnedEditor = Editor.Pin();
    if (PinnedEditor.IsValid()) return Cast<UAnimBlueprint>(PinnedEditor->GetBlueprintObj());
    return TestAnimBlueprint.Get();
}

/**
 * 执行纯 Lua 检查并输出定位诊断；不会生成 Graph、调用原生编译或保存资产。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteCheckLua()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    if (EnsureLocalLuaExtension() == nullptr) return;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bSucceeded = USekiroAnimBlueprintFactoryLibrary::CheckLuaAnimBlueprint(
        AnimBlueprint,
        Diagnostics);
    LogDiagnostics(TEXT("Check Lua"), AnimBlueprint, Diagnostics);
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("Check Lua %s for '%s'."),
        bSucceeded ? TEXT("succeeded") : TEXT("failed"),
        AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"));
}

/**
 * 显式导入 Lua IR、事务性重建 Graph 并执行一次原生编译；不自动保存资产。
 * 若当前 Blueprint 侧相对同步点已改变，会在任何结构修改前要求用户确认。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteGenerateFromLua()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    USekiroLuaAnimBlueprintExtension* Extension = EnsureLocalLuaExtension();
    if (Extension == nullptr) return;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    if (!USekiroAnimBlueprintFactoryLibrary::RefreshLuaAnimBlueprintSyncStatus(
        AnimBlueprint,
        Diagnostics))
    {
        LogDiagnostics(TEXT("Refresh Sync Status"), AnimBlueprint, Diagnostics);
        return;
    }
    if ((Extension->SyncStatus == ESekiroLuaAnimBlueprintSyncStatus::BlueprintChanged
            || Extension->SyncStatus == ESekiroLuaAnimBlueprintSyncStatus::BothChanged)
        && FMessageDialog::Open(
            EAppMsgType::YesNo,
            LOCTEXT(
                "ConfirmLuaImportOverwrite",
                "The Animation Blueprint Graph changed after the last synchronization. Importing Lua will overwrite those Blueprint-side changes. Continue?"))
            != EAppReturnType::Yes)
    {
        return;
    }

    const bool bSucceeded = USekiroAnimBlueprintFactoryLibrary::CompileLuaAnimBlueprintInPlace(
        AnimBlueprint,
        false,
        Diagnostics);
    LogDiagnostics(TEXT("Lua → AnimBlueprint"), AnimBlueprint, Diagnostics);
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("Lua → AnimBlueprint %s for '%s'."),
        bSucceeded ? TEXT("succeeded") : TEXT("failed"),
        AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"));
}

/**
 * 显式把当前 Graph 安全导出到 generated Lua 交换模块；Lua 侧相对同步点改变时先确认覆盖。
 * 取消发生在 Writer 或文件操作之前，因此不会修改 Graph、文件、同步基线或 package。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteExportToLua()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    USekiroLuaAnimBlueprintExtension* Extension = EnsureLocalLuaExtension();
    if (Extension == nullptr) return;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    if (!USekiroAnimBlueprintFactoryLibrary::RefreshLuaAnimBlueprintSyncStatus(
        AnimBlueprint,
        Diagnostics))
    {
        LogDiagnostics(TEXT("Refresh Sync Status"), AnimBlueprint, Diagnostics);
        return;
    }
    if ((Extension->SyncStatus == ESekiroLuaAnimBlueprintSyncStatus::LuaChanged
            || Extension->SyncStatus == ESekiroLuaAnimBlueprintSyncStatus::BothChanged)
        && FMessageDialog::Open(
            EAppMsgType::YesNo,
            LOCTEXT(
                "ConfirmLuaExportOverwrite",
                "The generated Lua exchange module changed after the last synchronization. Exporting the Animation Blueprint will overwrite those Lua-side changes. Continue?"))
            != EAppReturnType::Yes)
    {
        return;
    }

    FString GeneratedModuleName;
    const bool bSucceeded = USekiroAnimBlueprintFactoryLibrary::AnimBlueprintToLua(
        AnimBlueprint,
        GeneratedModuleName,
        Diagnostics,
        true);
    LogDiagnostics(TEXT("AnimBlueprint → Lua"), AnimBlueprint, Diagnostics);
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("AnimBlueprint → Lua %s for '%s'%s."),
        bSucceeded ? TEXT("succeeded") : TEXT("failed"),
        AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"),
        GeneratedModuleName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" as '%s'"), *GeneratedModuleName));
}

/** 只读刷新并记录当前 Graph/Lua 同步状态，不触发导入、导出、编译或保存。 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteRefreshSyncStatus()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    if (EnsureLocalLuaExtension() == nullptr) return;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bSucceeded =
        USekiroAnimBlueprintFactoryLibrary::RefreshLuaAnimBlueprintSyncStatus(
            AnimBlueprint,
            Diagnostics);
    LogDiagnostics(TEXT("Refresh Sync Status"), AnimBlueprint, Diagnostics);
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("Sync status refresh %s for '%s'."),
        bSucceeded ? TEXT("succeeded") : TEXT("failed"),
        AnimBlueprint != nullptr ? *AnimBlueprint->GetPathName() : TEXT("None"));
}

/**
 * 判断 Lua 工具按钮和模式菜单是否可操作；PIE 中禁止结构生成以保护活动 AnimInstance。
 *
 * @return 目标具有 Lua 模块且当前不在 PIE/SIE 时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction() const
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::FindEffective(AnimBlueprint);
    return AnimBlueprint != nullptr
        && Extension != nullptr
        && !Extension->LuaModuleName.IsEmpty()
        && (GEditor == nullptr || GEditor->PlayWorld == nullptr);
}

/**
 * 判断当前编辑器是否允许配置 Lua 模块；任何标准 AnimBlueprint 都可显式接管，
 * 已继承 Lua 父资产的子类无需预先拥有本地扩展。函数不读取或创建 Lua Env。
 *
 * @return 目标动画蓝图有效且当前不在 PIE/SIE 时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::CanEditLuaModule() const
{
    return GetAnimBlueprint() != nullptr
        && (GEditor == nullptr || GEditor->PlayWorld == nullptr);
}

/**
 * 确保继承 Lua 动画源的子 AnimBlueprint 拥有独立本地扩展。
 * 本地扩展有效时直接返回；否则通过 UE 反射读取最近父 AnimBlueprint 的模块名，
 * 再调用通用配置 API 创建本地副本。不会根据类名、资产目录或模块命名约定进行推导。
 * 必须在非 PIE 的游戏线程调用；首次派生会标记当前子资产待保存，但不修改父资产。
 *
 * @return 当前资产的有效本地扩展；没有本地或继承 Lua 源、配置失败时返回 nullptr。
 */
USekiroLuaAnimBlueprintExtension*
FSekiroLuaAnimBlueprintEditorBinding::EnsureLocalLuaExtension()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    USekiroLuaAnimBlueprintExtension* LocalExtension =
        USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (LocalExtension != nullptr && !LocalExtension->LuaModuleName.IsEmpty())
    {
        return LocalExtension;
    }

    const USekiroLuaAnimBlueprintExtension* EffectiveExtension =
        USekiroLuaAnimBlueprintExtension::FindEffective(AnimBlueprint);
    if (EffectiveExtension == nullptr
        || EffectiveExtension->LuaModuleName.IsEmpty())
    {
        return nullptr;
    }

    const FString InheritedModuleName = EffectiveExtension->LuaModuleName;
    if (!USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
            AnimBlueprint,
            InheritedModuleName))
    {
        return nullptr;
    }

    return USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
}

/**
 * 返回工具栏显示的 Lua 模块名；本地未配置时显示最近父 AnimBlueprint 的有效模块，
 * 让用户在创建本地覆写前仍能确认继承来源。函数只读反射元数据。
 *
 * @return 当前或继承的模块名；继承链没有 Lua 源时返回空文本。
 */
FText FSekiroLuaAnimBlueprintEditorBinding::GetLuaModuleNameText() const
{
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::FindEffective(GetAnimBlueprint());
    return Extension != nullptr
        ? FText::FromString(Extension->LuaModuleName)
        : FText::GetEmpty();
}

/**
 * 将扩展同步枚举转换为紧凑、统一的英文工具栏标签；函数只读扩展内存状态。
 *
 * @return 当前状态标签；没有本地扩展时返回 Never Synchronized。
 */
FText FSekiroLuaAnimBlueprintEditorBinding::GetSyncStatusText() const
{
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::Find(GetAnimBlueprint());
    const ESekiroLuaAnimBlueprintSyncStatus Status = Extension != nullptr
        ? Extension->SyncStatus
        : ESekiroLuaAnimBlueprintSyncStatus::NeverSynchronized;
    switch (Status)
    {
    case ESekiroLuaAnimBlueprintSyncStatus::InSync:
        return LOCTEXT("SyncStatusInSync", "Sync: In Sync");
    case ESekiroLuaAnimBlueprintSyncStatus::BlueprintChanged:
        return LOCTEXT("SyncStatusBlueprintChanged", "Sync: Blueprint Changed");
    case ESekiroLuaAnimBlueprintSyncStatus::LuaChanged:
        return LOCTEXT("SyncStatusLuaChanged", "Sync: Lua Changed");
    case ESekiroLuaAnimBlueprintSyncStatus::BothChanged:
        return LOCTEXT("SyncStatusBothChanged", "Sync: Both Changed");
    case ESekiroLuaAnimBlueprintSyncStatus::Error:
        return LOCTEXT("SyncStatusError", "Sync: Error");
    default:
        return LOCTEXT("SyncStatusNever", "Sync: Never Synchronized");
    }
}

/**
 * 将工具栏输入的模块名配置为当前 AnimBlueprint 自己的 Lua 源。
 * 输入只做首尾空白清理，不根据项目路径或父类名称改写；空值保持现有配置。
 * 必须在非 PIE 的游戏线程调用，成功后由通用配置 API 标记当前资产待生成和保存。
 *
 * @param ModuleNameText 用户提交的 UnLua require 模块名。
 * @param CommitType Slate 提交原因；当前所有提交类型使用相同配置语义。
 */
void FSekiroLuaAnimBlueprintEditorBinding::CommitLuaModuleName(
    const FText& ModuleNameText,
    ETextCommit::Type CommitType)
{
    static_cast<void>(CommitType);
    const FString ModuleName = ModuleNameText.ToString().TrimStartAndEnd();
    if (ModuleName.IsEmpty()) return;

    USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
        GetAnimBlueprint(),
        ModuleName);
}

/**
 * 切换本用户的编辑器 Lua 调试开关，并立即启动或停止 9966 监听。
 * 只能由非 PIE 工具栏动作在游戏线程调用；Lua 操作失败时不保存新状态。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteToggleEditorLuaDebug()
{
    const bool bEnableEditorDebug = !IsEditorLuaDebugEnabled();
    const char* ControlFunctionName = bEnableEditorDebug ? "Start" : "Stop";
    if (!CallLuaDebuggerControlFunction(ControlFunctionName))
    {
        UE_LOG(
            LogSekiroLuaAnimBlueprintEditorBinding,
            Error,
            TEXT("Failed to %s editor Lua debugging on port 9966."),
            bEnableEditorDebug ? TEXT("start") : TEXT("stop"));
        return;
    }

    SaveEditorLuaDebugEnabled(bEnableEditorDebug);
    PrepareEditorLuaDebugBeforeEnvCreation();
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("Editor Lua debugging is now %s; the setting is stored per user."),
        bEnableEditorDebug ? TEXT("enabled") : TEXT("disabled"));
}

/**
 * 判断当前是否允许切换编辑器 Lua 调试生命周期。
 * 本函数只读取 PIE/SIE 状态，不访问 Lua Env。
 *
 * @return 未运行 PIE/SIE 时返回 true，否则返回 false。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::CanToggleEditorLuaDebug() const
{
    return GEditor == nullptr || GEditor->PlayWorld == nullptr;
}

/**
 * 返回编辑器 Lua 调试按钮的勾选状态。
 *
 * @return 当前持久化开关开启时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::IsEditorLuaDebugChecked() const
{
    return IsEditorLuaDebugEnabled();
}

/**
 * 构建编辑器 Lua 调试按钮的动态短标签。
 *
 * @return 开关开启时返回“Editor Debug: On”，否则返回“Editor Debug: Off”。
 */
FText FSekiroLuaAnimBlueprintEditorBinding::GetEditorLuaDebugLabel() const
{
    return IsEditorLuaDebugEnabled()
        ? LOCTEXT("EditorLuaDebugOnLabel", "Editor Debug: On")
        : LOCTEXT("EditorLuaDebugOffLabel", "Editor Debug: Off");
}

#undef LOCTEXT_NAMESPACE
