#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

class USekiroImportSettings;

/// Sekiro导入Slate对话框
class SEKIROIMPORT_API SSekiroImportDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSekiroImportDialog) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /// 打开模态导入对话框
    static void OpenModal();

private:
    /// 加载配置到UI控件
    void LoadSettings();
    /// 保存UI控件值到配置
    void SaveSettings();

    /// 浏览模型JSON文件
    FReply OnBrowseModelJson();
    /// 浏览动画JSON文件
    FReply OnBrowseAnimationJson();
    /// 开始导入
    FReply OnImport();
    /// 追加状态日志
    void AppendStatus(const FString& Message);

    /// 获取配置对象
    USekiroImportSettings* GetSettings();

    // --- UI控件引用 ---
    TSharedPtr<SEditableTextBox> ModelJsonPathBox;
    TSharedPtr<SEditableTextBox> AnimationJsonPathBox;
    TSharedPtr<SEditableTextBox> OutputPathBox;
    TSharedPtr<SEditableTextBox> SkeletonNameBox;
    TSharedPtr<SCheckBox> ImportSkeletonCheckBox;
    TSharedPtr<SCheckBox> ImportMeshCheckBox;
    TSharedPtr<SCheckBox> ImportMaterialsCheckBox;
    TSharedPtr<SCheckBox> ImportAnimationsCheckBox;
    TSharedPtr<SSpinBox<int32>> MaxAnimationsSpinBox;
    TSharedPtr<SMultiLineEditableTextBox> StatusLogBox;

    FString StatusLog;
};
