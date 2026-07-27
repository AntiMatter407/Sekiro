#include "SekiroAnimBlueprintFactoryLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "IAnimationBlueprintEditorModule.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Logging/MessageLog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SekiroLuaAnimDebugRuntime.h"
#include "SekiroLuaAnimSnapshotViewer.h"
#include "SekiroLuaAnimBlueprintEditorBinding.h"
#include "SekiroLuaAnimBlueprintAutoCompileScheduler.h"
#include "SekiroLuaTransitionRuntimeLibrary.h"
#include "ToolMenus.h"
#include "UnLuaFunctionLibrary.h"
#include "UnLuaModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroAnimBlueprintExtEditor, Log, All);

namespace SekiroAnimBlueprintExtEditorPrivate
{
const FName LuaAnimSnapshotViewerTabName(TEXT("LuaAnimSnapshotViewer"));

/**
 * 将一条 Lua 动画蓝图结构化诊断格式化为 PIE 错误面板文本。
 * 仅处理内存中的诊断数据，不修改资产；来源未知时仍保留稳定错误代码和消息。
 *
 * @param Diagnostic 编译器返回的只读诊断。
 * @return 包含错误代码、消息及 Lua 模块行列的单行文本。
 */
FText FormatPIECompileDiagnostic(const FSekiroAnimIRDiagnostic& Diagnostic)
{
    const FString Source = Diagnostic.SourceLocation.LuaModule.IsEmpty()
        ? TEXT("UnknownLuaModule")
        : Diagnostic.SourceLocation.LuaModule;
    return FText::FromString(FString::Printf(
        TEXT("%s: %s (%s:%d:%d)"),
        *Diagnostic.Code.ToString(),
        *Diagnostic.Message,
        *Source,
        Diagnostic.SourceLocation.Line,
        Diagnostic.SourceLocation.Column));
}

/**
 * 将 PIE 前 Lua 动画蓝图编译失败作为一次性 Error 写入并打开 PIE Message Log。
 * 只能在游戏线程的 PreBeginPIE 回调中调用；本函数只报告已有诊断，不执行编译或重试。
 *
 * @param Diagnostics 本次同步编译产生的只读诊断；为空时输出通用失败信息。
 */
void ReportPIECompileFailure(const TArray<FSekiroAnimIRDiagnostic>& Diagnostics)
{
    check(IsInGameThread());
    FMessageLog PIEMessageLog(TEXT("PIE"));
    PIEMessageLog.NewPage(FText::FromString(TEXT("Lua AnimBlueprint Pre-PIE Compile")));
    PIEMessageLog.Error(FText::FromString(
        TEXT("Lua 动画蓝图在 PIE 开始前编译失败；本次运行保留上一次成功生成的动画类，运行时不会重试编译。")));

    bool bHasDetailedError = false;
    for (const FSekiroAnimIRDiagnostic& Diagnostic : Diagnostics)
    {
        const FText Message = FormatPIECompileDiagnostic(Diagnostic);
        if (Diagnostic.Severity == ESekiroAnimIRDiagnosticSeverity::Error)
        {
            PIEMessageLog.Error(Message);
            bHasDetailedError = true;
        }
        else
        {
            PIEMessageLog.Warning(Message);
        }
    }
    if (!bHasDetailedError)
    {
        PIEMessageLog.Error(FText::FromString(
            TEXT("编译器未返回具体错误诊断，请查看 Output Log 中的 LogSekiroLuaAnimBlueprintCompiler。")));
    }

    PIEMessageLog.Notify(
        FText::FromString(TEXT("Lua 动画蓝图编译失败")),
        EMessageSeverity::Error,
        true);
    PIEMessageLog.Open(EMessageSeverity::Error, true);
}
}

/** 管理 Lua 动画源码监听、PIE 前同步编译以及官方动画蓝图编辑器工具栏扩展。 */
class FSekiroAnimBlueprintExtEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterScriptWatcher();
    void UnregisterScriptWatcher();
    void HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
    void HandleBlueprintPreCompile(UBlueprint* BlueprintToCompile);
    void HandlePreBeginPIE(bool bIsSimulatingInEditor);
    void HandleEndPIE(bool bIsSimulatingInEditor);
    TSharedRef<FExtender> ExtendAnimationBlueprintToolbar(
        const TSharedRef<FUICommandList> CommandList,
        TSharedRef<IAnimationBlueprintEditor> Editor);
    TSharedRef<SDockTab> SpawnLuaAnimSnapshotViewerTab(const FSpawnTabArgs& SpawnTabArgs);
    void RegisterLuaAnimSnapshotViewerMenus();

    static void MarkPendingSourceChanges(
        TWeakPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> WeakScheduler);

    TSharedPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> Scheduler; // 事件驱动标脏请求合并器
    FString WatchedAnimationScriptRoot; // 注册和注销使用的 ScriptRoot/Animation 目录
    FDelegateHandle DirectoryWatcherHandle; // DirectoryWatcher 回调句柄
    FDelegateHandle BlueprintPreCompileHandle; // 动画蓝图编译前运行缓存失效委托句柄
    FDelegateHandle PreBeginPIEHandle; // PIE 前同步编译委托句柄
    FDelegateHandle EndPIEHandle; // PIE 结束自动关闭全部 Lua 动画调试委托句柄
    FDelegateHandle ToolbarExtenderHandle; // 官方动画蓝图编辑器工具栏扩展句柄
    TArray<TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding>> EditorBindings; // 每个命令列表唯一的安全绑定
    TAtomic<bool> bShuttingDown = false; // 阻止关闭阶段继续接收变化
};

/**
 * 激活编辑器 UnLua Env，按用户开关决定是否立即启动 Lua 调试，再注册源码 watcher、PIE 委托和工具栏。
 * 由模块管理器在编辑器启动时于游戏线程调用；调试器失败只记录日志，不阻止编辑器启动。
 */
void FSekiroAnimBlueprintExtEditorModule::StartupModule()
{
    bShuttingDown = false;

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        SekiroAnimBlueprintExtEditorPrivate::LuaAnimSnapshotViewerTabName,
        FOnSpawnTab::CreateRaw(
            this,
            &FSekiroAnimBlueprintExtEditorModule::SpawnLuaAnimSnapshotViewerTab))
        .SetDisplayName(FText::FromString(TEXT("Lua Anim Snapshot Viewer")))
        .SetTooltipText(FText::FromString(TEXT("查看 Lua 动画蓝图运行快照")))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this,
            &FSekiroAnimBlueprintExtEditorModule::RegisterLuaAnimSnapshotViewerMenus));

    FSekiroLuaAnimBlueprintEditorBinding::PrepareEditorLuaDebugBeforeEnvCreation();
    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (UnLuaModule != nullptr)
    {
        if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
        UnLuaModule->GetEnv();
        if (!FSekiroLuaAnimBlueprintEditorBinding::ApplyEditorLuaDebugSetting())
        {
            UE_LOG(
                LogSekiroAnimBlueprintExtEditor,
                Warning,
                TEXT("Editor Lua debugging is enabled, but the listener could not be started; editor startup will continue."));
        }
    }
    else
    {
        UE_LOG(
            LogSekiroAnimBlueprintExtEditor,
            Warning,
            TEXT("UnLua module is unavailable; editor Lua debugger listener was not started."));
    }

    Scheduler = MakeShared<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe>();
    RegisterScriptWatcher();
    if (GEditor != nullptr)
    {
        BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(
            this,
            &FSekiroAnimBlueprintExtEditorModule::HandleBlueprintPreCompile);
    }
    PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(
        this,
        &FSekiroAnimBlueprintExtEditorModule::HandlePreBeginPIE);
    EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(
        this,
        &FSekiroAnimBlueprintExtEditorModule::HandleEndPIE);

    IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule =
        FModuleManager::LoadModuleChecked<IAnimationBlueprintEditorModule>(
            TEXT("AnimationBlueprintEditor"));
    IAnimationBlueprintEditorModule::FAnimationBlueprintEditorToolbarExtender ToolbarDelegate =
        IAnimationBlueprintEditorModule::FAnimationBlueprintEditorToolbarExtender::CreateRaw(
            this,
            &FSekiroAnimBlueprintExtEditorModule::ExtendAnimationBlueprintToolbar);
    ToolbarExtenderHandle = ToolbarDelegate.GetHandle();
    AnimationBlueprintEditorModule.GetAllAnimationBlueprintEditorToolbarExtenders().Add(
        ToolbarDelegate);
}

/**
 * 停止接收 Lua 文件变化，并注销 watcher 与 PIE 生命周期委托。
 * 同时恢复所有编辑器原始 Compile 动作并移除工具栏扩展；已排队任务通过弱引用失效。
 */
void FSekiroAnimBlueprintExtEditorModule::ShutdownModule()
{
    bShuttingDown = true;
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    if (FSlateApplication::IsInitialized())
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
            SekiroAnimBlueprintExtEditorPrivate::LuaAnimSnapshotViewerTabName);
    }
    UnregisterScriptWatcher();
    if (BlueprintPreCompileHandle.IsValid() && GEditor != nullptr)
    {
        GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
        BlueprintPreCompileHandle.Reset();
    }
    if (PreBeginPIEHandle.IsValid())
    {
        FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
        PreBeginPIEHandle.Reset();
    }
    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }
    if (ToolbarExtenderHandle.IsValid()
        && FModuleManager::Get().IsModuleLoaded(TEXT("AnimationBlueprintEditor")))
    {
        IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule =
            FModuleManager::GetModuleChecked<IAnimationBlueprintEditorModule>(
                TEXT("AnimationBlueprintEditor"));
        TArray<IAnimationBlueprintEditorModule::FAnimationBlueprintEditorToolbarExtender>&
            Extenders =
                AnimationBlueprintEditorModule.GetAllAnimationBlueprintEditorToolbarExtenders();
        Extenders.RemoveAll([this](
            const IAnimationBlueprintEditorModule::FAnimationBlueprintEditorToolbarExtender&
                Extender)
        {
            return Extender.GetHandle() == ToolbarExtenderHandle;
        });
        ToolbarExtenderHandle.Reset();
    }
    for (const TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding>& Binding : EditorBindings)
    {
        if (Binding.IsValid()) Binding->RestoreOriginalCompileActions();
    }
    EditorBindings.Reset();
    if (Scheduler.IsValid()) Scheduler->Reset();
    Scheduler.Reset();
}

/**
 * 为一个实际动画蓝图编辑器创建或复用命令绑定，并把 Lua 编译、调试与来源控件追加到原生 Compile 区段。
 * 只能由 IAnimationBlueprintEditorModule 在游戏线程调用；不会在 ToolMenus 动态构建阶段查询 ToolkitCommands。
 *
 * @param CommandList 官方回调提供的当前编辑器有效命令列表。
 * @param Editor 当前动画蓝图编辑器共享引用，绑定内部只保留弱引用。
 * @return 包含 Lua 工具栏填充委托的新 Extender。
 */
TSharedRef<FExtender> FSekiroAnimBlueprintExtEditorModule::ExtendAnimationBlueprintToolbar(
    const TSharedRef<FUICommandList> CommandList,
    TSharedRef<IAnimationBlueprintEditor> Editor)
{
    EditorBindings.RemoveAll([](
        const TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding>& Candidate)
    {
        return !Candidate.IsValid() || !Candidate->IsValid();
    });

    TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding> Binding;
    for (const TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding>& Candidate : EditorBindings)
    {
        if (Candidate.IsValid() && Candidate->UsesCommandList(CommandList))
        {
            Binding = Candidate;
            break;
        }
    }
    if (!Binding.IsValid())
    {
        Binding = FSekiroLuaAnimBlueprintEditorBinding::Create(CommandList, Editor);
        EditorBindings.Add(Binding);
    }

    return Binding->GetToolbarExtender();
}

/**
 * 为 Nomad Tab 创建独立 Lua 动画快照查看器。
 * 只能由全局 TabManager 在游戏线程调用；SpawnTabArgs 仅描述本次生成请求且不被保留。
 * 返回由 TabManager 管理生命周期的有效 DockTab。
 */
TSharedRef<SDockTab> FSekiroAnimBlueprintExtEditorModule::SpawnLuaAnimSnapshotViewerTab(
    const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SSekiroLuaAnimSnapshotViewer)
        ];
}

/**
 * 在 Level Editor 的 Window 菜单注册快照查看器入口，动作只负责唤起已注册 Nomad Tab。
 * 由 ToolMenus 启动回调在游戏线程调用；菜单项归本模块所有并在 ShutdownModule 完整注销。
 */
void FSekiroAnimBlueprintExtEditorModule::RegisterLuaAnimSnapshotViewerMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
    if (WindowMenu == nullptr) return;

    FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));
    Section.AddMenuEntry(
        TEXT("LuaAnimSnapshotViewer"),
        FText::FromString(TEXT("Lua Anim Snapshot Viewer")),
        FText::FromString(TEXT("打开 Lua 动画蓝图运行快照查看器")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(
                SekiroAnimBlueprintExtEditorPrivate::LuaAnimSnapshotViewerTabName);
        })));
}

/**
 * 从 UnLua 获取绝对 Script Root，仅递归监听 Animation 子树；回调只标记源 Dirty，不读取或编译 Lua。
 * 必须在游戏线程调用。注册失败只记录警告，不影响编辑器 Compile 和 PreBeginPIE 入口。
 */
void FSekiroAnimBlueprintExtEditorModule::RegisterScriptWatcher()
{
    WatchedAnimationScriptRoot = FPaths::Combine(
        UUnLuaFunctionLibrary::GetScriptRootPath(),
        TEXT("Animation"));
    FPaths::NormalizeDirectoryName(WatchedAnimationScriptRoot);
    if (!IFileManager::Get().DirectoryExists(*WatchedAnimationScriptRoot))
    {
        UE_LOG(
            LogSekiroAnimBlueprintExtEditor,
            Warning,
            TEXT("Lua animation script root '%s' does not exist; source dirty tracking is disabled."),
            *WatchedAnimationScriptRoot);
        WatchedAnimationScriptRoot.Reset();
        return;
    }

    FDirectoryWatcherModule& WatcherModule =
        FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
    IDirectoryWatcher* DirectoryWatcher = WatcherModule.Get();
    if (DirectoryWatcher == nullptr
        || !DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
            WatchedAnimationScriptRoot,
            IDirectoryWatcher::FDirectoryChanged::CreateRaw(
                this,
                &FSekiroAnimBlueprintExtEditorModule::HandleDirectoryChanged),
            DirectoryWatcherHandle))
    {
        UE_LOG(
            LogSekiroAnimBlueprintExtEditor,
            Warning,
            TEXT("Failed to watch Lua animation script root '%s'."),
            *WatchedAnimationScriptRoot);
        DirectoryWatcherHandle.Reset();
        WatchedAnimationScriptRoot.Reset();
        return;
    }

    UE_LOG(
        LogSekiroAnimBlueprintExtEditor,
        Display,
        TEXT("Watching Lua animation scripts for dirty state changes: %s"),
        *WatchedAnimationScriptRoot);
}

/**
 * 使用注册时保存的路径和句柄注销 DirectoryWatcher；未注册或模块已卸载时安全跳过。
 * 必须在游戏线程调用，不主动卸载 DirectoryWatcher 模块。
 */
void FSekiroAnimBlueprintExtEditorModule::UnregisterScriptWatcher()
{
    if (!DirectoryWatcherHandle.IsValid() || WatchedAnimationScriptRoot.IsEmpty()) return;
    if (FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
    {
        FDirectoryWatcherModule& WatcherModule =
            FModuleManager::GetModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
        IDirectoryWatcher* DirectoryWatcher = WatcherModule.Get();
        if (DirectoryWatcher != nullptr)
        {
            DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(
                WatchedAnimationScriptRoot,
                DirectoryWatcherHandle);
        }
    }

    DirectoryWatcherHandle.Reset();
    WatchedAnimationScriptRoot.Reset();
}

/**
 * 过滤 DirectoryWatcher 的 Lua Added、Modified、Removed 事件，并事件驱动派发一次游戏线程标脏任务。
 * 回调可能来自非游戏线程，不访问 UObject；多个尚未消费的事件会合并，但不会等待防抖或触发编译。
 *
 * @param FileChanges 本批文件系统变化，由 DirectoryWatcher 在回调期间拥有。
 */
void FSekiroAnimBlueprintExtEditorModule::HandleDirectoryChanged(
    const TArray<FFileChangeData>& FileChanges)
{
    if (bShuttingDown || !Scheduler.IsValid()) return;
    if (!Scheduler->QueueFileChanges(FileChanges)) return;

    const TWeakPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe>
        WeakScheduler(Scheduler);
    AsyncTask(ENamedThreads::GameThread, [WeakScheduler]()
    {
        MarkPendingSourceChanges(WeakScheduler);
    });
}

/**
 * 在动画蓝图销毁并重建生成字段前清除 Lua 更新反射缓存，避免编辑器预览继续访问旧 FProperty。
 * 只能由 UEditorEngine 的 BlueprintPreCompile 事件在游戏线程调用；非动画蓝图编译不会改变缓存。
 *
 * @param BlueprintToCompile 即将编译的蓝图；允许为空，只有 UAnimBlueprint 会触发缓存失效。
 */
void FSekiroAnimBlueprintExtEditorModule::HandleBlueprintPreCompile(
    UBlueprint* BlueprintToCompile)
{
    check(IsInGameThread());
    if (!IsValid(Cast<UAnimBlueprint>(BlueprintToCompile))) return;
    USekiroLuaTransitionRuntimeLibrary::ResetRuntimeCachesForPIESession();
}

/**
 * 在 PIE/SIE 创建 PlayWorld 前关闭全部 Lua 动画采样、重置运行缓存，再编译已加载 Dirty Lua AnimBlueprint。
 * 只能在游戏线程调用；失败会保留各资产上一次成功 GeneratedClass 和 Dirty 状态，但本委托不取消 PIE。
 *
 * @param bIsSimulatingInEditor true 表示 SIE，false 表示 PIE；两种模式采用同一编译规则。
 */
void FSekiroAnimBlueprintExtEditorModule::HandlePreBeginPIE(
    const bool bIsSimulatingInEditor)
{
    USekiroLuaTransitionRuntimeLibrary::ResetRuntimeCachesForPIESession();
    FSekiroLuaAnimDebugRuntime::ResetForPIEStart();
    MarkPendingSourceChanges(Scheduler);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bSucceeded =
        USekiroAnimBlueprintFactoryLibrary::CompileDirtyLoadedLuaAnimBlueprints(
            false,
            Diagnostics);
    if (!bSucceeded)
    {
        SekiroAnimBlueprintExtEditorPrivate::ReportPIECompileFailure(Diagnostics);
        UE_LOG(
            LogSekiroAnimBlueprintExtEditor,
            Error,
            TEXT("Lua AnimBlueprint pre-PIE compilation failed; detailed Error entries were opened in the PIE Message Log, and runtime compilation retry is disabled."));
    }
}

/**
 * 在 PIE/SIE 完全结束时关闭实时层级 Debug 与 Snapshot，并 Flush JSONL 后释放文件句柄。
 * 只能由 FEditorDelegates::EndPIE 在游戏线程调用；参数仅标识 SIE/PIE，两种模式采用相同行为。
 * 回调清除本次运行的实时帧和实例缓存，但保留最后快照路径供查看器读取。
 *
 * @param bIsSimulatingInEditor true 表示刚结束 SIE，false 表示刚结束 PIE。
 */
void FSekiroAnimBlueprintExtEditorModule::HandleEndPIE(
    const bool bIsSimulatingInEditor)
{
    static_cast<void>(bIsSimulatingInEditor);
    USekiroLuaTransitionRuntimeLibrary::ResetRuntimeCachesForPIESession();
    FSekiroLuaAnimDebugRuntime::ResetForPIEEnd();
}

/**
 * 在游戏线程消费一次已合并 Lua 文件变化，并将全部已加载 Lua 动画蓝图标记 Source Dirty。
 * Animation 目录内脚本可能是共享基类或节点模块，因此当前依赖图建立前采用保守全量标脏；不会执行 HotReload 或编译。
 *
 * @param WeakScheduler 线程安全请求合并器弱引用；模块关闭后失效并安全跳过。
 */
void FSekiroAnimBlueprintExtEditorModule::MarkPendingSourceChanges(
    const TWeakPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> WeakScheduler)
{
    const TSharedPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> PinnedScheduler =
        WeakScheduler.Pin();
    if (!PinnedScheduler.IsValid() || !PinnedScheduler->ConsumeDirtyRequest()) return;

    const int32 DirtyCount =
        USekiroAnimBlueprintFactoryLibrary::MarkLoadedLuaAnimBlueprintsDirty(
            TEXT("Lua animation source changed; compile the Animation Blueprint or start PIE."));
    UE_LOG(
        LogSekiroAnimBlueprintExtEditor,
        Verbose,
        TEXT("Lua animation source changed; marked %d loaded Lua AnimBlueprint asset(s) dirty."),
        DirtyCount);
}

IMPLEMENT_MODULE(FSekiroAnimBlueprintExtEditorModule, SekiroAnimBlueprintExtEditor)
