#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;

class SLuaGameplayUIImportWidget : public SCompoundWidget
{
public:
    // ── 窗口构建 ──
    SLATE_BEGIN_ARGS(SLuaGameplayUIImportWidget) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Arguments);

private:
    // ── 清单操作 ──
    FReply ChooseManifest();
    FReply PreviewManifest();
    FReply ImportManifest();
    FReply BrowseImportedAssets();
    void InvalidatePreview();

    TSharedPtr<SEditableTextBox> ManifestInput; // 用户指定的 JSON 清单路径
    TSharedPtr<SMultiLineEditableTextBox> PreviewText; // 只读候选与校验信息
    FText Status; // 最近操作的真实结果
    TArray<FString> ImportedAssets; // 最近一次导入中实际保存的对象路径
};
