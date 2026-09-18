#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class FMenuBuilder;
class FExtender;
class FMultiBox;
class FToolBarBuilder;
class FUICommandList;
class IAnimationBlueprintEditor;
class UAnimBlueprint;
class ULuaAnimBlueprintExtension;

/** 将单个动画蓝图编辑器的 Lua 手动同步工具栏绑定到其有效命令列表。 */
class FLuaAnimBlueprintEditorBinding final
    : public TSharedFromThis<FLuaAnimBlueprintEditorBinding>
{
public:
    /** 在创建 UnLua Env 前根据持久化开关设置编辑器调试启动模块。 */
    static void PrepareEditorLuaDebugBeforeEnvCreation();

    /** 将当前持久化开关应用到已创建的 UnLua Env。 */
    static bool ApplyEditorLuaDebugSetting();

    /** 返回是否允许在非 PIE 编辑器阶段启动 Lua 调试端口。 */
    static bool IsEditorLuaDebugEnabled();

    static TSharedRef<FLuaAnimBlueprintEditorBinding> Create(
        const TSharedRef<FUICommandList>& CommandList,
        const TSharedRef<IAnimationBlueprintEditor>& Editor);

    static TSharedRef<FLuaAnimBlueprintEditorBinding> CreateForTest(
        const TSharedRef<FUICommandList>& CommandList,
        UAnimBlueprint* AnimBlueprint);

    TSharedRef<FExtender> GetToolbarExtender();
    void FillToolbar(FToolBarBuilder& ToolbarBuilder);
    bool IsValid() const;
    bool UsesCommandList(const TSharedRef<FUICommandList>& CommandList) const;

#if WITH_DEV_AUTOMATION_TESTS
    /** 供自动化测试验证工具栏共用的 Lua 动作启用条件。 */
    bool CanExecuteLuaActionForTest() const { return CanExecuteLuaAction(); }
#endif

private:
    FLuaAnimBlueprintEditorBinding(
        const TSharedRef<FUICommandList>& CommandList,
        const TSharedPtr<IAnimationBlueprintEditor>& Editor,
        UAnimBlueprint* AnimBlueprint);

    UAnimBlueprint* GetAnimBlueprint() const;
    void ExecuteCheckLua();
    void ExecuteGenerateFromLua();
    void ExecuteExportToLua();
    void ExecuteRefreshSyncStatus();
    bool CanExecuteLuaAction() const;
    bool CanEditLuaModule() const;
    ULuaAnimBlueprintExtension* EnsureLocalLuaExtension();
    FText GetLuaModuleNameText() const;
    FText GetSyncStatusText() const;
    void CommitLuaModuleName(
        const FText& ModuleNameText,
        ETextCommit::Type CommitType);
    void ExecuteToggleEditorLuaDebug();
    bool CanToggleEditorLuaDebug() const;
    bool IsEditorLuaDebugChecked() const;
    FText GetEditorLuaDebugLabel() const;

    TWeakPtr<FUICommandList> CommandList; // 当前编辑器拥有的命令列表
    TWeakPtr<IAnimationBlueprintEditor> Editor; // 生产环境动画蓝图编辑器
    TWeakObjectPtr<UAnimBlueprint> TestAnimBlueprint; // 自动化测试直接提供的资产
    TSharedPtr<FExtender> ToolbarExtender; // 工具栏重建时复用的唯一扩展实例
    TArray<TWeakPtr<FMultiBox>> FilledToolbarMultiBoxes; // 已注入 Lua 控件的工具栏实例
};
