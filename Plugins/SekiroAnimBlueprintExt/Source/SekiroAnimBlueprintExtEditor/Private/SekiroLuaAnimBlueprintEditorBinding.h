#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"

class FMenuBuilder;
class FExtender;
class FMultiBox;
class FToolBarBuilder;
class FUICommandInfo;
class FUICommandList;
class IAnimationBlueprintEditor;
class UAnimBlueprint;

/** 将单个动画蓝图编辑器的 Lua 工具栏和模式化 Compile 行为绑定到其有效命令列表。 */
class FSekiroLuaAnimBlueprintEditorBinding final
    : public TSharedFromThis<FSekiroLuaAnimBlueprintEditorBinding>
{
public:
    static TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> Create(
        const TSharedRef<FUICommandList>& CommandList,
        const TSharedRef<IAnimationBlueprintEditor>& Editor);

    static TSharedRef<FSekiroLuaAnimBlueprintEditorBinding> CreateForTest(
        const TSharedRef<FUICommandList>& CommandList,
        UAnimBlueprint* AnimBlueprint,
        const TSharedPtr<const FUICommandInfo>& CompileCommand);

    ~FSekiroLuaAnimBlueprintEditorBinding();

    TSharedRef<FExtender> GetToolbarExtender();
    void FillToolbar(FToolBarBuilder& ToolbarBuilder);
    bool IsValid() const;
    bool UsesCommandList(const TSharedRef<FUICommandList>& CommandList) const;
    void RestoreOriginalCompileActions();

private:
    FSekiroLuaAnimBlueprintEditorBinding(
        const TSharedRef<FUICommandList>& CommandList,
        const TSharedPtr<IAnimationBlueprintEditor>& Editor,
        UAnimBlueprint* AnimBlueprint,
        const TSharedPtr<const FUICommandInfo>& CompileCommand);

    void Initialize();
    UAnimBlueprint* GetAnimBlueprint() const;
    void ExecuteModeAwareCompile(FUIAction OriginalAction);
    bool CanExecuteModeAwareCompile(FUIAction OriginalAction) const;
    void ExecuteCheckLua();
    void ExecuteGenerateFromLua();
    bool CanExecuteLuaAction() const;
    TSharedRef<SWidget> MakeSourceModeMenu();
    FText GetSourceModeLabel() const;
    void SetSourceMode(uint8 SourceModeValue);
    bool IsSourceMode(uint8 SourceModeValue) const;

    TWeakPtr<FUICommandList> CommandList; // 当前编辑器拥有的命令列表
    TWeakPtr<IAnimationBlueprintEditor> Editor; // 生产环境动画蓝图编辑器
    TWeakObjectPtr<UAnimBlueprint> TestAnimBlueprint; // 自动化测试直接提供的资产
    TSharedPtr<FExtender> ToolbarExtender; // 工具栏重建时复用的唯一扩展实例
    TWeakPtr<FMultiBox> ActiveToolbarMultiBox; // 当前重建周期唯一接收 Lua 控件的工具栏实例
    TWeakPtr<FMultiBox> ParentToolbarMultiBox; // 当前重建周期需要跳过的父级工具栏实例
    bool bActiveToolbarFilled = false; // 防止 UE 对同一可见 MultiBox 重复执行扩展委托
    FUIAction OriginalToolbarCompileAction; // UE 工具栏 Compile 原动作
    TSharedPtr<const FUICommandInfo> ToolbarCompileCommand; // 生产环境 Compile 或测试替代命令
    FUIAction OriginalKeyboardCompileAction; // UE F7 Compile 原动作
    TSharedPtr<FUICommandInfo> KeyboardCompileCommand; // 通过公开 InputBindingManager 查得的 BlueprintEditor.CompileBlueprint
    bool bActionsRestored = false; // 防止关闭阶段重复恢复命令
};
