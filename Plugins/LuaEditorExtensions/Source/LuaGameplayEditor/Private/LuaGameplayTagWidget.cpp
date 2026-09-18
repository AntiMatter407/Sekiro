#include "LuaGameplayTagWidget.h"

#include "LuaGameplayTagLibrary.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/**
 * 构建 GameplayTag 工具页；仅游戏线程，Arguments 为 Slate 标准构造参数，无项目默认路径。
 * 创建选择文件、目标包名、只读预览和生成操作；不在构造时读取文件或修改资产。
 */
void SLuaGameplayTagWidget::Construct(const FArguments& Arguments)
{
    Status = FText::FromString(TEXT("选择 Lua 层次表并填写目标资产包名，预览不会修改资产。"));
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("Lua → GameplayTag 数据表"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Lua 文件"))) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [ SAssignNew(SourceInput, SEditableTextBox).HintText(FText::FromString(TEXT("选择声明式 .lua 文件")))
                .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [ SNew(SButton).Text(FText::FromString(TEXT("选择…"))).OnClicked(this, &SLuaGameplayTagWidget::ChooseLuaFile) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("资产包名"))) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [ SAssignNew(AssetInput, SEditableTextBox).HintText(FText::FromString(TEXT("/Game/ 下的资产长包名，不含扩展名"))) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bRegisterSource = State == ECheckBoxState::Checked; })
            [ SNew(STextBlock).Text(FText::FromString(TEXT("保存后注册到 GameplayTagTableList 并刷新标签字典"))) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(FText::FromString(TEXT("预览标签"))).OnClicked(this, &SLuaGameplayTagWidget::Preview) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
            [ SNew(SButton).Text(FText::FromString(TEXT("生成 / 更新数据表"))).OnClicked(this, &SLuaGameplayTagWidget::Generate) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(FText::FromString(TEXT("在内容浏览器中定位"))).OnClicked(this, &SLuaGameplayTagWidget::BrowseAsset)
                .IsEnabled_Lambda([this]() { return !LastSavedObject.IsEmpty(); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [ SNew(STextBlock).Text_Lambda([this]() { return Status; }).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0)
        [ SNew(SSeparator) ]
        + SVerticalBox::Slot().FillHeight(1).Padding(8)
        [ SAssignNew(PreviewText, SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(false) ]
    ];
}

/** 打开原生 Lua 文件选择框；仅游戏线程。返回已处理事件，取消时保留原输入，不执行文件内容。 */
FReply SLuaGameplayTagWidget::ChooseLuaFile()
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { Status = FText::FromString(TEXT("当前平台没有文件选择器，请直接输入 Lua 路径。")); return FReply::Handled(); }
    TArray<FString> Files;
    const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
    if (Desktop->OpenFileDialog(ParentHandle, TEXT("选择 GameplayTag Lua 表"), TEXT(""), TEXT(""), TEXT("Lua (*.lua)|*.lua"), EFileDialogFlags::None, Files) && !Files.IsEmpty())
    {
        SourceInput->SetText(FText::FromString(Files[0]));
    }
    return FReply::Handled();
}

/** 通过公共接口重新读取并展示标签；仅游戏线程。返回已处理事件，失败清空旧预览，不保存任何内容。 */
FReply SLuaGameplayTagWidget::Preview()
{
    TArray<FLuaGameplayTagEntry> Tags;
    FString Error;
    if (!ULuaGameplayTagLibrary::PreviewGameplayTags(SourceInput->GetText().ToString(), Tags, Error))
    {
        PreviewText->SetText(FText::GetEmpty());
        Status = FText::FromString(Error);
        return FReply::Handled();
    }
    TArray<FString> Lines;
    for (const FLuaGameplayTagEntry& Entry : Tags) Lines.Add(Entry.Tag + TEXT("    ") + Entry.Comment);
    PreviewText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
    Status = FText::FromString(FString::Printf(TEXT("预览通过：共 %d 个标签（包括父节点），尚未写入资产。"), Tags.Num()));
    return FReply::Handled();
}

/** 再次读取源文件并生成数据表；仅游戏线程。返回已处理事件，反馈资产与配置分开保存的真实结果。 */
FReply SLuaGameplayTagWidget::Generate()
{
    FLuaGameplayTagGenerationResult Result;
    ULuaGameplayTagLibrary::GenerateGameplayTagTable(SourceInput->GetText().ToString(), AssetInput->GetText().ToString(), bRegisterSource, Result);
    Status = FText::FromString(Result.Message);
    if (Result.bAssetSaved) LastSavedObject = Result.AssetObjectPath;
    return FReply::Handled();
}

/** 在内容浏览器定位最近成功保存的表；仅游戏线程。返回已处理事件，资产不再存在时只反馈错误。 */
FReply SLuaGameplayTagWidget::BrowseAsset()
{
    UObject* Asset = LoadObject<UObject>(nullptr, *LastSavedObject);
    if (!Asset) { Status = FText::FromString(TEXT("无法加载最近生成的资产，请检查它是否已移动。")); return FReply::Handled(); }
    TArray<UObject*> Assets;
    Assets.Add(Asset);
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Assets);
    return FReply::Handled();
}

/** 输入改变后标记预览失效；仅游戏线程，不读取新路径，不影响之前保存的资产。 */
void SLuaGameplayTagWidget::InvalidatePreview()
{
    if (PreviewText) PreviewText->SetText(FText::GetEmpty());
    Status = FText::FromString(TEXT("Lua 来源已更改，请重新预览；生成时会重新读取文件。"));
}
