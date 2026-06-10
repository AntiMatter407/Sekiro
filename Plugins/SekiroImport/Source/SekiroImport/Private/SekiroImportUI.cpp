#include "SekiroImportUI.h"
#include "SekiroImportSettings.h"
#include "SekiroImportPipeline.h"
#include "SekiroImportLog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "EditorStyleSet.h"
#include "DesktopPlatformModule.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SekiroImportDialog"

// ============================================================================
// 构造
// ============================================================================

void SSekiroImportDialog::Construct(const FArguments& InArgs)
{
    StatusLog = TEXT("就绪");

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SVerticalBox)

            // ---- 标题 ----
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Title", "Sekiro Asset Import"))
                .Font(FAppStyle::GetFontStyle("HeadingMedium"))
            ]

            // ---- 源文件 ----
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SourceFiles", "源文件"))
                .Font(FAppStyle::GetFontStyle("NormalFontBold"))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SBox).WidthOverride(100)
                    [ SNew(STextBlock).Text(LOCTEXT("ModelJsonLabel", "模型JSON:")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                [
                    SAssignNew(ModelJsonPathBox, SEditableTextBox)
                    .HintText(LOCTEXT("ModelJsonHint", "e.g. Extracted/Sekiro_model_hkx.json"))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Browse", "浏览..."))
                    .OnClicked(this, &SSekiroImportDialog::OnBrowseModelJson)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SBox).WidthOverride(100)
                    [ SNew(STextBlock).Text(LOCTEXT("AnimJsonLabel", "动画JSON:")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                [
                    SAssignNew(AnimationJsonPathBox, SEditableTextBox)
                    .HintText(LOCTEXT("AnimJsonHint", "e.g. Extracted/Sekiro_animations.json"))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Browse", "浏览..."))
                    .OnClicked(this, &SSekiroImportDialog::OnBrowseAnimationJson)
                ]
            ]

            // ---- 输出设置 ----
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("OutputSettings", "输出设置"))
                .Font(FAppStyle::GetFontStyle("NormalFontBold"))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SBox).WidthOverride(100)
                    [ SNew(STextBlock).Text(LOCTEXT("OutputPathLabel", "内容路径:")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SAssignNew(OutputPathBox, SEditableTextBox)
                    .HintText(LOCTEXT("OutputPathHint", "/Game/Characters/Sekiro"))
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SBox).WidthOverride(100)
                    [ SNew(STextBlock).Text(LOCTEXT("SkeletonNameLabel", "骨架名称:")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SAssignNew(SkeletonNameBox, SEditableTextBox)
                    .HintText(LOCTEXT("SkeletonNameHint", "Sekiro_Skeleton"))
                ]
            ]

            // ---- 导入选项 ----
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ImportOptions", "导入选项"))
                .Font(FAppStyle::GetFontStyle("NormalFontBold"))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
                [
                    SAssignNew(ImportSkeletonCheckBox, SCheckBox)
                    [ SNew(STextBlock).Text(LOCTEXT("ImportSkeleton", "导入骨架")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
                [
                    SAssignNew(ImportMeshCheckBox, SCheckBox)
                    [ SNew(STextBlock).Text(LOCTEXT("ImportMesh", "导入网格体")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
                [
                    SAssignNew(ImportMaterialsCheckBox, SCheckBox)
                    [ SNew(STextBlock).Text(LOCTEXT("ImportMaterials", "导入材质")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(ImportAnimationsCheckBox, SCheckBox)
                    [ SNew(STextBlock).Text(LOCTEXT("ImportAnimations", "导入动画")) ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [
                    SNew(STextBlock).Text(LOCTEXT("MaxAnimsLabel", "最大动画数:"))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SAssignNew(MaxAnimationsSpinBox, SSpinBox<int32>)
                    .MinValue(0)
                    .MaxValue(9999)
                    .MinDesiredWidth(80)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("MaxAnimsHint", "(0=全部)"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
            ]

            // ---- 状态日志 ----
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Status", "状态"))
                .Font(FAppStyle::GetFontStyle("NormalFontBold"))
            ]

            + SVerticalBox::Slot().FillHeight(1).Padding(0, 0, 0, 8)
            [
                SNew(SBox).MinDesiredHeight(120)
                [
                    SAssignNew(StatusLogBox, SMultiLineEditableTextBox)
                    .IsReadOnly(true)
                    .AlwaysShowScrollbars(true)
                    .Text(FText::FromString(StatusLog))
                ]
            ]

            // ---- 按钮 ----
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("StartImport", "开始导入"))
                    .OnClicked(this, &SSekiroImportDialog::OnImport)
                    .ButtonColorAndOpacity(FLinearColor(0.2f, 0.6f, 0.2f))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Close", "关闭"))
                    .OnClicked_Lambda([]() -> FReply {
                        if (TSharedPtr<SWindow> Win = FSlateApplication::Get().GetActiveTopLevelWindow())
                        {
                            Win->RequestDestroyWindow();
                        }
                        return FReply::Handled();
                    })
                ]
            ]
        ]
    ];

    // 控件创建完毕后加载配置
    LoadSettings();
}

// ============================================================================
// 配置加载/保存
// ============================================================================

USekiroImportSettings* SSekiroImportDialog::GetSettings()
{
    return GetMutableDefault<USekiroImportSettings>();
}

void SSekiroImportDialog::LoadSettings()
{
    USekiroImportSettings* Settings = GetSettings();
    if (!Settings) return;

    ModelJsonPathBox->SetText(FText::FromString(Settings->ModelJsonPath));
    AnimationJsonPathBox->SetText(FText::FromString(Settings->AnimationJsonPath));
    OutputPathBox->SetText(FText::FromString(Settings->OutputBasePath));
    SkeletonNameBox->SetText(FText::FromString(Settings->SkeletonName));
    ImportSkeletonCheckBox->SetIsChecked(Settings->bImportSkeleton ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    ImportMeshCheckBox->SetIsChecked(Settings->bImportSkeletalMesh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    ImportMaterialsCheckBox->SetIsChecked(Settings->bImportMaterials ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    ImportAnimationsCheckBox->SetIsChecked(Settings->bImportAnimations ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    MaxAnimationsSpinBox->SetValue(Settings->MaxAnimations);
}

void SSekiroImportDialog::SaveSettings()
{
    USekiroImportSettings* Settings = GetSettings();
    if (!Settings) return;

    Settings->ModelJsonPath = ModelJsonPathBox->GetText().ToString();
    Settings->AnimationJsonPath = AnimationJsonPathBox->GetText().ToString();
    Settings->OutputBasePath = OutputPathBox->GetText().ToString();
    Settings->SkeletonName = SkeletonNameBox->GetText().ToString();
    Settings->bImportSkeleton = ImportSkeletonCheckBox->IsChecked();
    Settings->bImportSkeletalMesh = ImportMeshCheckBox->IsChecked();
    Settings->bImportMaterials = ImportMaterialsCheckBox->IsChecked();
    Settings->bImportAnimations = ImportAnimationsCheckBox->IsChecked();
    Settings->MaxAnimations = MaxAnimationsSpinBox->GetValue();

    Settings->SaveConfig();
}

// ============================================================================
// 文件浏览
// ============================================================================

static FString BrowseForJsonFile(const TCHAR* Title)
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform) return FString();

    TArray<FString> OutFiles;
    const FString FileTypes = TEXT("JSON Files (*.json)|*.json");
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
        Title,
        FPaths::ProjectDir(),
        TEXT(""),
        FileTypes,
        EFileDialogFlags::None,
        OutFiles
    );

    return (bOpened && OutFiles.Num() > 0) ? OutFiles[0] : FString();
}

FReply SSekiroImportDialog::OnBrowseModelJson()
{
    FString FilePath = BrowseForJsonFile(TEXT("选择模型JSON文件"));
    if (!FilePath.IsEmpty())
    {
        ModelJsonPathBox->SetText(FText::FromString(FilePath));
    }
    return FReply::Handled();
}

FReply SSekiroImportDialog::OnBrowseAnimationJson()
{
    FString FilePath = BrowseForJsonFile(TEXT("选择动画JSON文件"));
    if (!FilePath.IsEmpty())
    {
        AnimationJsonPathBox->SetText(FText::FromString(FilePath));
    }
    return FReply::Handled();
}

// ============================================================================
// 状态日志
// ============================================================================

void SSekiroImportDialog::AppendStatus(const FString& Message)
{
    StatusLog += Message + TEXT("\n");
    if (StatusLogBox.IsValid())
    {
        StatusLogBox->SetText(FText::FromString(StatusLog));
    }
}

// ============================================================================
// 导入执行
// ============================================================================

FReply SSekiroImportDialog::OnImport()
{
    // 收集配置
    SaveSettings();
    USekiroImportSettings* Settings = GetSettings();
    if (!Settings) return FReply::Handled();

    // 验证路径
    if (Settings->ModelJsonPath.IsEmpty() && Settings->AnimationJsonPath.IsEmpty())
    {
        StatusLog.Empty();
        AppendStatus(TEXT("错误: 请至少指定一个JSON文件路径"));
        return FReply::Handled();
    }

    // 清空日志
    StatusLog.Empty();
    AppendStatus(TEXT("=== 开始导入 ==="));

    // 绑定进度回调
    FSekiroImportPipeline::OnProgress.BindSP(this, &SSekiroImportDialog::AppendStatus);

    // 执行导入
    FSekiroImportPipeline::FImportResult Result = FSekiroImportPipeline::Run(*Settings);

    // 解绑回调
    FSekiroImportPipeline::OnProgress.Unbind();

    // 显示结果
    if (Result.bSuccess)
    {
        AppendStatus(TEXT(""));
        AppendStatus(TEXT("*** 导入成功! ***"));
    }
    else
    {
        AppendStatus(TEXT(""));
        AppendStatus(TEXT("*** 导入完成，但有错误: ***"));
        for (const FString& Err : Result.Errors)
        {
            AppendStatus(FString::Printf(TEXT("  - %s"), *Err));
        }
    }

    AppendStatus(FString::Printf(TEXT("骨架: %s"), Result.Skeleton ? TEXT("✓") : TEXT("✗")));
    AppendStatus(FString::Printf(TEXT("网格体: %s"), Result.SkeletalMesh ? TEXT("✓") : TEXT("✗")));
    AppendStatus(FString::Printf(TEXT("材质: %d 个"), Result.Materials.Num()));
    AppendStatus(FString::Printf(TEXT("动画: %d 个"), Result.Animations.Num()));

    return FReply::Handled();
}

// ============================================================================
// 模态窗口
// ============================================================================

void SSekiroImportDialog::OpenModal()
{
    // 获取主窗口作为父窗口
    TSharedPtr<SWindow> ParentWindow;
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
    {
        IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
        ParentWindow = MainFrame.GetParentWindow();
    }

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("DialogTitle", "Sekiro Asset Import"))
        .ClientSize(FVector2D(640, 620))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .SizingRule(ESizingRule::FixedSize);

    TSharedRef<SSekiroImportDialog> Dialog = SNew(SSekiroImportDialog);

    Window->SetContent(Dialog);

    FSlateApplication::Get().AddModalWindow(Window, ParentWindow);
}

#undef LOCTEXT_NAMESPACE
