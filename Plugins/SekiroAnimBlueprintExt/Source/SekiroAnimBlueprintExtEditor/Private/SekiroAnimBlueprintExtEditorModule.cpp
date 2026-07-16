#include "SekiroAnimBlueprintFactoryLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "IAnimationBlueprintEditorModule.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SekiroLuaAnimBlueprintEditorBinding.h"
#include "SekiroLuaAnimBlueprintAutoCompileScheduler.h"
#include "UnLuaFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogSekiroAnimBlueprintExtEditor, Log, All);

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
    void HandlePreBeginPIE(bool bIsSimulatingInEditor);
    TSharedRef<FExtender> ExtendAnimationBlueprintToolbar(
        const TSharedRef<FUICommandList> CommandList,
        TSharedRef<IAnimationBlueprintEditor> Editor);

    static void MarkPendingSourceChanges(
        TWeakPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> WeakScheduler);

    TSharedPtr<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe> Scheduler; // 事件驱动标脏请求合并器
    FString WatchedAnimationScriptRoot; // 注册和注销使用的 ScriptRoot/Animation 目录
    FDelegateHandle DirectoryWatcherHandle; // DirectoryWatcher 回调句柄
    FDelegateHandle PreBeginPIEHandle; // PIE 前同步编译委托句柄
    FDelegateHandle ToolbarExtenderHandle; // 官方动画蓝图编辑器工具栏扩展句柄
    TArray<TSharedPtr<FSekiroLuaAnimBlueprintEditorBinding>> EditorBindings; // 每个命令列表唯一的安全绑定
    TAtomic<bool> bShuttingDown = false; // 阻止关闭阶段继续接收变化
};

/**
 * 注册 Lua Animation 目录 watcher、PreBeginPIE 委托和官方动画蓝图工具栏扩展。
 * 由模块管理器在编辑器启动时于游戏线程调用；工具栏回调提供有效 ToolkitCommands 后才包装 Compile/F7。
 */
void FSekiroAnimBlueprintExtEditorModule::StartupModule()
{
    bShuttingDown = false;
    Scheduler = MakeShared<FSekiroLuaAnimBlueprintAutoCompileScheduler, ESPMode::ThreadSafe>();
    RegisterScriptWatcher();
    PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(
        this,
        &FSekiroAnimBlueprintExtEditorModule::HandlePreBeginPIE);

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
 * 停止接收 Lua 文件变化，并注销 watcher 与 PreBeginPIE 委托。
 * 同时恢复所有编辑器原始 Compile 动作并移除工具栏扩展；已排队任务通过弱引用失效。
 */
void FSekiroAnimBlueprintExtEditorModule::ShutdownModule()
{
    bShuttingDown = true;
    UnregisterScriptWatcher();
    if (PreBeginPIEHandle.IsValid())
    {
        FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
        PreBeginPIEHandle.Reset();
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
 * 为一个实际动画蓝图编辑器创建或复用命令绑定，并把三个 Lua 控件追加到原生 Compile 区段。
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
 * 在 PIE/SIE 创建 PlayWorld 前同步消费尚未派发的源变化，并编译全部已加载 Dirty Lua AnimBlueprint。
 * 只能在游戏线程调用；失败会保留各资产上一次成功 GeneratedClass 和 Dirty 状态，但本委托不取消 PIE。
 *
 * @param bIsSimulatingInEditor true 表示 SIE，false 表示 PIE；两种模式采用同一编译规则。
 */
void FSekiroAnimBlueprintExtEditorModule::HandlePreBeginPIE(
    const bool bIsSimulatingInEditor)
{
    MarkPendingSourceChanges(Scheduler);

    TArray<FSekiroAnimIRDiagnostic> Diagnostics;
    const bool bSucceeded =
        USekiroAnimBlueprintFactoryLibrary::CompileDirtyLoadedLuaAnimBlueprints(
            false,
            Diagnostics);
    if (!bSucceeded)
    {
        UE_LOG(
            LogSekiroAnimBlueprintExtEditor,
            Error,
            TEXT("One or more dirty Lua AnimBlueprints failed to compile before PIE; previous generated classes remain active."));
    }
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
