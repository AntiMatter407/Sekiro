#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;

class SSekiroGameplayTagWidget : public SCompoundWidget
{
public:
    // ── 界面构建 ──
    SLATE_BEGIN_ARGS(SSekiroGameplayTagWidget) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Arguments);

private:
    // ── 用户操作 ──
    FReply ChooseLuaFile();
    FReply Preview();
    FReply Generate();
    FReply BrowseAsset();
    void InvalidatePreview();

    TSharedPtr<SEditableTextBox> SourceInput; // 用户指定的 Lua 文件
    TSharedPtr<SEditableTextBox> AssetInput; // 用户指定的长包名
    TSharedPtr<SMultiLineEditableTextBox> PreviewText; // 只读标签及说明列表
    FText Status; // 最近一次操作结果
    FString LastSavedObject; // 可定位的最近成功保存资产
    bool bRegisterSource = true; // 生成后是否加入标签字典
};
