#include "LuaGameplayUIImportWidget.h"

#include "LuaGameplayUIImportLibrary.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/**
 * 构建原版战斗 UI 资源导入页；仅游戏线程。Arguments 为 Slate 构造参数，不读取文件或创建资产。
 * 来源根和映射来自用户 JSON，不预置本机安装路径、素材名或项目资源位置。
 */
void SLuaGameplayUIImportWidget::Construct(const FArguments& Arguments)
{
    Status = FText::FromString(TEXT("选择已提取 PNG 的 JSON 清单。预览会检查尺寸和所有权，不修改原版安装目录。"));
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("原版战斗 UI 资源导入 / Import Combat UI Assets"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("JSON: Version=1、SourceRoot、Textures；条目包含 SourceFile、AssetPackagePath、sRGB。\n本工具导入 PNG 为 UI 纹理，不负责原包解码、图集裁切、Widget 或材质生成。"))).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("清单 / Manifest"))) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [ SAssignNew(ManifestInput, SEditableTextBox).HintText(FText::FromString(TEXT("选择 .json 资源清单")))
                .OnTextChanged_Lambda([this](const FText&) { InvalidatePreview(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [ SNew(SButton).Text(FText::FromString(TEXT("选择…"))).OnClicked(this, &SLuaGameplayUIImportWidget::ChooseManifest) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(FText::FromString(TEXT("预览并校验 / Preview"))).OnClicked(this, &SLuaGameplayUIImportWidget::PreviewManifest) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
            [ SNew(SButton).Text(FText::FromString(TEXT("导入 / 更新纹理 / Import"))).OnClicked(this, &SLuaGameplayUIImportWidget::ImportManifest) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(FText::FromString(TEXT("定位已导入资产"))).OnClicked(this, &SLuaGameplayUIImportWidget::BrowseImportedAssets)
                .IsEnabled_Lambda([this]() { return !ImportedAssets.IsEmpty(); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [ SNew(STextBlock).Text_Lambda([this]() { return Status; }).AutoWrapText(true) ]
        + SVerticalBox::Slot().FillHeight(1).Padding(8)
        [ SAssignNew(PreviewText, SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(false) ]
    ];
}

/** 弹出 JSON 文件选择框；游戏线程。返回已处理事件，取消不更改来源，不执行清单内容。 */
FReply SLuaGameplayUIImportWidget::ChooseManifest()
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { Status = FText::FromString(TEXT("文件选择器不可用，请直接输入清单路径。")); return FReply::Handled(); }
    TArray<FString> Files;
    const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
    if (Desktop->OpenFileDialog(ParentHandle, TEXT("选择 UI 纹理导入清单"), TEXT(""), TEXT(""), TEXT("JSON (*.json)|*.json"), EFileDialogFlags::None, Files) && !Files.IsEmpty())
    {
        ManifestInput->SetText(FText::FromString(Files[0]));
    }
    return FReply::Handled();
}

/** 重新读取清单并展示每项来源、实测尺寸、alpha 和目标；游戏线程。返回已处理事件，失败清空旧预览，不写资产。 */
FReply SLuaGameplayUIImportWidget::PreviewManifest()
{
    TArray<FLuaUITextureImportEntry> Entries;
    FString Error;
    if (!ULuaGameplayUIImportLibrary::PreviewUITextureManifest(ManifestInput->GetText().ToString(), Entries, Error))
    {
        PreviewText->SetText(FText::GetEmpty());
        Status = FText::FromString(Error);
        return FReply::Handled();
    }
    TArray<FString> Lines;
    for (const FLuaUITextureImportEntry& Entry : Entries)
    {
        Lines.Add(FString::Printf(TEXT("[%s] %s   %d × %d   sRGB=%s   alpha=%s\n  %s\n  → %s\n  PNG SHA1: %s\n"),
            Entry.bUpdatesExistingAsset ? TEXT("更新") : TEXT("新增"), *Entry.SourceSymbol, Entry.Width, Entry.Height,
            Entry.bSRGB ? TEXT("true") : TEXT("false"), *Entry.AlphaMode, *Entry.SourceFile, *Entry.AssetPackagePath, *Entry.PNGSHA1));
    }
    PreviewText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
    Status = FText::FromString(FString::Printf(TEXT("%d 项全部通过预检；PNG alpha 原样保留，图集矩形只保存为元数据。尚未导入。"), Entries.Num()));
    return FReply::Handled();
}

/** 通过通用接口完成整清单导入；游戏线程。返回已处理事件，显示部分保存状态，不把中途失败报告为整体成功。 */
FReply SLuaGameplayUIImportWidget::ImportManifest()
{
    FLuaUITextureImportResult Result;
    ULuaGameplayUIImportLibrary::ImportUITextureManifest(ManifestInput->GetText().ToString(), Result);
    Status = FText::FromString(Result.Message);
    ImportedAssets = Result.ImportedAssetPaths;
    return FReply::Handled();
}

/** 定位本页最近一次导入成功保存的资产；游戏线程。返回已处理事件，忽略已删除对象，不更改资产内容。 */
FReply SLuaGameplayUIImportWidget::BrowseImportedAssets()
{
    TArray<UObject*> Assets;
    for (const FString& Path : ImportedAssets)
    {
        if (UObject* Asset = LoadObject<UObject>(nullptr, *Path)) Assets.Add(Asset);
    }
    if (!Assets.IsEmpty()) FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Assets);
    return FReply::Handled();
}

/** 清单路径变化时使预览失效；游戏线程，无参数、无外部副作用，不触及此前导入的资产。 */
void SLuaGameplayUIImportWidget::InvalidatePreview()
{
    if (PreviewText) PreviewText->SetText(FText::GetEmpty());
    Status = FText::FromString(TEXT("清单已更改，请重新预览。实际导入时会重新读取所有输入。"));
}
