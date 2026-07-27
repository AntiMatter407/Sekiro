#include "SekiroLuaAnimBlueprintEditorBinding.h"

#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IAnimationBlueprintEditor.h"
#include "Misc/ConfigCacheIni.h"
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
    const FName EditorLuaDebugToolbarBlockName(TEXT("Sekiro.EditorLuaDebug"));
    const FName SourceModeToolbarBlockName(TEXT("Sekiro.SourceMode"));
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
 * 创建生产环境编辑器绑定并立即捕获、包装该编辑器已经注册的两个原生 Compile 动作。
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
    const TSharedPtr<FUICommandInfo> ToolbarCompileCommand =
        FInputBindingManager::Get().FindCommandInContext(
            TEXT("FullBlueprintEditor"),
            TEXT("Compile"));
    TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> Binding =
        MakeShareable(new FSekiroLuaAnimBlueprintEditorBinding(
            CommandList,
            Editor,
            nullptr,
            ToolbarCompileCommand));
    Binding->Initialize();
    return Binding;
}

/**
 * 创建不依赖实际编辑器窗口的测试绑定，仍在传入 FUICommandList 上执行真实 ExecuteAction 路径。
 * 只能在游戏线程测试中调用；绑定不拥有 AnimBlueprint。
 *
 * @param CommandList 已预先映射原生 Compile 动作的测试命令列表。
 * @param AnimBlueprint 测试目标动画蓝图，可为空以验证禁用行为。
 * @param CompileCommand 测试命令列表中代表 Compile 的命令信息，不要求使用编辑器私有命令类型。
 * @return 已包装命令的测试绑定对象。
 */
TSharedRef<FSekiroLuaAnimBlueprintEditorBinding>
FSekiroLuaAnimBlueprintEditorBinding::CreateForTest(
    const TSharedRef<FUICommandList>& CommandList,
    UAnimBlueprint* AnimBlueprint,
    const TSharedPtr<const FUICommandInfo>& CompileCommand)
{
    TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> Binding =
        MakeShareable(new FSekiroLuaAnimBlueprintEditorBinding(
            CommandList,
            nullptr,
            AnimBlueprint,
            CompileCommand));
    Binding->Initialize();
    return Binding;
}

/**
 * 析构绑定时恢复捕获的 UE 原生 Compile 动作，避免模块卸载后命令列表保留插件委托。
 * 析构应发生在游戏线程；命令列表已销毁时安全跳过。
 */
FSekiroLuaAnimBlueprintEditorBinding::~FSekiroLuaAnimBlueprintEditorBinding()
{
    RestoreOriginalCompileActions();
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
 * 在原生 Compile 区段后添加 Check Lua、Generate From Lua 与持久化 Source Mode 下拉控件。
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
        LOCTEXT("GenerateFromLuaLabel", "Generate From Lua"),
        LOCTEXT("GenerateFromLuaTooltip", "Transactionally rebuild this Animation Blueprint Graph from the latest valid Lua IR."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));
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
    ToolbarBuilder.AddComboButton(
        FUIAction(
            FExecuteAction(),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction)),
        FOnGetContent::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::MakeSourceModeMenu),
        TAttribute<FText>::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::GetSourceModeLabel),
        LOCTEXT("SourceModeTooltip", "Choose whether Compile and F7 use the native Blueprint Graph or Lua as the source."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "BlueprintEditor.SwitchToScriptingMode"),
        false,
        SourceModeToolbarBlockName);
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
 * 将 FullBlueprintEditor 工具栏 Compile 与 BlueprintEditor F7 命令恢复为绑定创建时捕获的原动作。
 * 可重复调用；必须在游戏线程执行，命令列表已销毁时安全跳过。
 */
void FSekiroLuaAnimBlueprintEditorBinding::RestoreOriginalCompileActions()
{
    if (bActionsRestored) return;
    bActionsRestored = true;
    const TSharedPtr<FUICommandList> PinnedCommands = CommandList.Pin();
    if (!PinnedCommands.IsValid()) return;

    if (ToolbarCompileCommand.IsValid())
    {
        PinnedCommands->MapAction(
            ToolbarCompileCommand,
            OriginalToolbarCompileAction);
    }
    if (KeyboardCompileCommand.IsValid())
    {
        PinnedCommands->MapAction(
            KeyboardCompileCommand,
            OriginalKeyboardCompileAction);
    }
}

/**
 * 保存生产或测试上下文的弱引用；命令捕获延迟到 Initialize，确保 SharedThis 已经可用。
 *
 * @param InCommandList 有效 Toolkit 或测试命令列表。
 * @param InEditor 生产环境编辑器，可为空。
 * @param AnimBlueprint 测试环境资产；生产环境应为空。
 * @param CompileCommand 要包装的工具栏 Compile 命令；测试可传任意已映射命令。
 */
FSekiroLuaAnimBlueprintEditorBinding::FSekiroLuaAnimBlueprintEditorBinding(
    const TSharedRef<FUICommandList>& InCommandList,
    const TSharedPtr<IAnimationBlueprintEditor>& InEditor,
    UAnimBlueprint* AnimBlueprint,
    const TSharedPtr<const FUICommandInfo>& CompileCommand)
    : CommandList(InCommandList)
    , Editor(InEditor)
    , TestAnimBlueprint(AnimBlueprint)
    , ToolbarCompileCommand(CompileCommand)
{
}

/**
 * 捕获当前命令列表中的原生 Compile 动作，再用模式化代理分别覆盖工具栏 Compile 和 F7。
 * 必须恰好调用一次且在游戏线程执行；缺失原动作时保留空动作并让对应命令不可执行。
 */
void FSekiroLuaAnimBlueprintEditorBinding::Initialize()
{
    const TSharedPtr<FUICommandList> PinnedCommands = CommandList.Pin();
    if (!PinnedCommands.IsValid()) return;

    const FUIAction* ToolbarAction =
        ToolbarCompileCommand.IsValid()
            ? PinnedCommands->GetActionForCommand(ToolbarCompileCommand)
            : nullptr;
    KeyboardCompileCommand = FInputBindingManager::Get().FindCommandInContext(
        TEXT("BlueprintEditor"),
        TEXT("CompileBlueprint"));
    const FUIAction* KeyboardAction = KeyboardCompileCommand.IsValid()
        ? PinnedCommands->GetActionForCommand(KeyboardCompileCommand)
        : nullptr;
    if (ToolbarAction != nullptr) OriginalToolbarCompileAction = *ToolbarAction;
    if (KeyboardAction != nullptr) OriginalKeyboardCompileAction = *KeyboardAction;

    if (ToolbarCompileCommand.IsValid())
    {
        PinnedCommands->MapAction(
            ToolbarCompileCommand,
            FUIAction(
                FExecuteAction::CreateSP(
                    this,
                    &FSekiroLuaAnimBlueprintEditorBinding::ExecuteModeAwareCompile,
                    OriginalToolbarCompileAction),
                FCanExecuteAction::CreateSP(
                    this,
                    &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteModeAwareCompile,
                    OriginalToolbarCompileAction)));
    }
    if (KeyboardCompileCommand.IsValid())
    {
        PinnedCommands->MapAction(
            KeyboardCompileCommand,
            FUIAction(
                FExecuteAction::CreateSP(
                    this,
                    &FSekiroLuaAnimBlueprintEditorBinding::ExecuteModeAwareCompile,
                    OriginalKeyboardCompileAction),
                FCanExecuteAction::CreateSP(
                    this,
                    &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteModeAwareCompile,
                    OriginalKeyboardCompileAction)));
    }
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Verbose,
        TEXT("Bound Check Lua, Generate From Lua and mode-aware Compile commands."));
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
 * 根据持久化 SourceMode 执行普通 Compile：Native 直接委托原动作；Lua 源过期时先 Check 和 Generate，
 * 源未变化时直接执行原生编译，避免设置修改触发整图重建。Lua 前置阶段失败时绝不调用原动作；
 * 原生动作返回后根据 Blueprint 状态提交成功或失败元数据。
 *
 * @param OriginalAction 本次命令在包装前捕获的 UE 原生动作副本。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteModeAwareCompile(FUIAction OriginalAction)
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    USekiroLuaAnimBlueprintExtension* Extension = EnsureLocalLuaExtension();
    if (Extension == nullptr
        || Extension->SourceMode == ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint)
    {
        OriginalAction.Execute();
        return;
    }

    const bool bRequiresLuaGraphGeneration =
        Extension->bSourceDirty
        || Extension->CompilerVersion
            != USekiroLuaAnimBlueprintExtension::CurrentCompilerVersion;
    if (bRequiresLuaGraphGeneration)
    {
        TArray<FSekiroAnimIRDiagnostic> Diagnostics;
        if (!USekiroAnimBlueprintFactoryLibrary::CheckLuaAnimBlueprint(
                AnimBlueprint,
                Diagnostics))
        {
            LogDiagnostics(TEXT("Check Lua"), AnimBlueprint, Diagnostics);
            return;
        }
        if (!USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
                AnimBlueprint,
                Diagnostics))
        {
            LogDiagnostics(TEXT("Generate From Lua"), AnimBlueprint, Diagnostics);
            return;
        }
    }

    OriginalAction.Execute();
    Extension = USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (Extension == nullptr) return;
    Extension->Modify();
    if (AnimBlueprint->Status != BS_Error && AnimBlueprint->GeneratedClass != nullptr)
    {
        Extension->MarkCompileSucceeded();
    }
    else
    {
        Extension->MarkCompileFailed(
            TEXT("UE native AnimBlueprint compilation failed after Generate From Lua."));
    }
    AnimBlueprint->GetOutermost()->MarkPackageDirty();
}

/**
 * 保留原生 Compile 的 CanExecute 约束；Lua 模式还要求目标存在有效 Lua 扩展。
 *
 * @param OriginalAction 包装前的 UE 原生动作。
 * @return 原动作允许执行且当前模式所需元数据有效时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::CanExecuteModeAwareCompile(
    FUIAction OriginalAction) const
{
    if (!OriginalAction.CanExecute()) return false;
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::FindEffective(AnimBlueprint);
    return Extension == nullptr
        || Extension->SourceMode == ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint
        || !Extension->LuaModuleName.IsEmpty();
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
 * 使用有效缓存或自动 Check 的结果事务性重建 Graph，并输出定位诊断；不调用原生编译或保存资产。
 */
void FSekiroLuaAnimBlueprintEditorBinding::ExecuteGenerateFromLua()
{
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    if (EnsureLocalLuaExtension() == nullptr) return;
    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bSucceeded =
        USekiroAnimBlueprintFactoryLibrary::GenerateLuaAnimBlueprintGraph(
            AnimBlueprint,
            Diagnostics);
    LogDiagnostics(TEXT("Generate From Lua"), AnimBlueprint, Diagnostics);
    UE_LOG(
        LogSekiroLuaAnimBlueprintEditorBinding,
        Display,
        TEXT("Generate From Lua %s for '%s'."),
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
    const ESekiroLuaAnimBlueprintSourceMode InheritedSourceMode =
        EffectiveExtension->SourceMode;
    if (!USekiroAnimBlueprintFactoryLibrary::ConfigureLuaAnimBlueprintSource(
            AnimBlueprint,
            InheritedModuleName))
    {
        return nullptr;
    }

    LocalExtension = USekiroLuaAnimBlueprintExtension::Find(AnimBlueprint);
    if (LocalExtension != nullptr)
    {
        LocalExtension->SourceMode = InheritedSourceMode;
    }
    return LocalExtension;
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

/**
 * 构建 Source Mode 单选菜单；菜单项通过扩展 UPROPERTY 持久化到当前动画蓝图资产。
 *
 * @return 新建的 Slate 菜单控件。
 */
TSharedRef<SWidget> FSekiroLuaAnimBlueprintEditorBinding::MakeSourceModeMenu()
{
    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.AddMenuEntry(
        LOCTEXT("NativeModeLabel", "Native Blueprint"),
        LOCTEXT("NativeModeTooltip", "Compile and F7 use the current Blueprint Graph without running Lua."),
        FSlateIcon(),
        FUIAction(
            FExecuteAction::CreateSP(
                this,
                &FSekiroLuaAnimBlueprintEditorBinding::SetSourceMode,
                static_cast<uint8>(ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint)),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction),
            FIsActionChecked::CreateSP(
                this,
                &FSekiroLuaAnimBlueprintEditorBinding::IsSourceMode,
                static_cast<uint8>(ESekiroLuaAnimBlueprintSourceMode::NativeBlueprint))),
        NAME_None,
        EUserInterfaceActionType::RadioButton);
    MenuBuilder.AddMenuEntry(
        LOCTEXT("LuaModeLabel", "Lua"),
        LOCTEXT("LuaModeTooltip", "Compile and F7 run Check Lua, Generate From Lua, then one native compile."),
        FSlateIcon(),
        FUIAction(
            FExecuteAction::CreateSP(
                this,
                &FSekiroLuaAnimBlueprintEditorBinding::SetSourceMode,
                static_cast<uint8>(ESekiroLuaAnimBlueprintSourceMode::Lua)),
            FCanExecuteAction::CreateSP(this, &FSekiroLuaAnimBlueprintEditorBinding::CanExecuteLuaAction),
            FIsActionChecked::CreateSP(
                this,
                &FSekiroLuaAnimBlueprintEditorBinding::IsSourceMode,
                static_cast<uint8>(ESekiroLuaAnimBlueprintSourceMode::Lua))),
        NAME_None,
        EUserInterfaceActionType::RadioButton);
    return MenuBuilder.MakeWidget();
}

/**
 * 返回工具栏 Source Mode 控件的当前短标签。
 *
 * @return Lua 模式返回“Source: Lua”，否则返回“Source: Native”。
 */
FText FSekiroLuaAnimBlueprintEditorBinding::GetSourceModeLabel() const
{
    const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::FindEffective(AnimBlueprint);
    return Extension != nullptr
        && Extension->SourceMode == ESekiroLuaAnimBlueprintSourceMode::Lua
        ? LOCTEXT("SourceLuaLabel", "Source: Lua")
        : LOCTEXT("SourceNativeLabel", "Source: Native");
}

/**
 * 在编辑器事务中更新持久化 SourceMode，并标记动画蓝图 package 待保存。
 * 切换到 Lua 时同步关闭多线程动画更新；函数不检查 Lua 或修改 Graph。
 *
 * @param SourceModeValue ESekiroLuaAnimBlueprintSourceMode 的 uint8 值，非法值被忽略。
 */
void FSekiroLuaAnimBlueprintEditorBinding::SetSourceMode(const uint8 SourceModeValue)
{
    if (SourceModeValue > static_cast<uint8>(ESekiroLuaAnimBlueprintSourceMode::Lua)) return;
    UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    USekiroLuaAnimBlueprintExtension* Extension = EnsureLocalLuaExtension();
    if (AnimBlueprint == nullptr || Extension == nullptr) return;

    AnimBlueprint->Modify();
    Extension->Modify();
    Extension->SourceMode =
        static_cast<ESekiroLuaAnimBlueprintSourceMode>(SourceModeValue);
    if (Extension->SourceMode == ESekiroLuaAnimBlueprintSourceMode::Lua)
    {
        AnimBlueprint->bUseMultiThreadedAnimationUpdate = false;
    }
    AnimBlueprint->GetOutermost()->MarkPackageDirty();
}

/**
 * 判断当前持久化模式是否与单选菜单项一致。
 *
 * @param SourceModeValue ESekiroLuaAnimBlueprintSourceMode 的 uint8 值。
 * @return 当前扩展模式等于参数时返回 true。
 */
bool FSekiroLuaAnimBlueprintEditorBinding::IsSourceMode(const uint8 SourceModeValue) const
{
    const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
    const USekiroLuaAnimBlueprintExtension* Extension =
        USekiroLuaAnimBlueprintExtension::FindEffective(AnimBlueprint);
    return Extension != nullptr
        && static_cast<uint8>(Extension->SourceMode) == SourceModeValue;
}

#undef LOCTEXT_NAMESPACE
