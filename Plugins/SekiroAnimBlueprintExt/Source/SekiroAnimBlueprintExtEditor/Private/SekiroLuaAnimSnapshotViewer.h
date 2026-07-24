#pragma once

#include "CoreMinimal.h"
#include "SekiroLuaAnimSnapshotModel.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableTextBox;
class SSekiroLuaAnimSnapshotTimeline;
class STextBlock;

/** Lua 动画 JSONL 快照的非技术向时间轴与层级查看器。 */
class SSekiroLuaAnimSnapshotViewer final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSekiroLuaAnimSnapshotViewer)
    {
    }
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

#if WITH_DEV_AUTOMATION_TESTS
    static bool DoesFrameMatchSearchForTesting(
        const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame,
        const FString& SearchText);
#endif

private:
    FReply HandleOpenFile();
    FReply HandleRefresh();
    FReply HandleLoadLatest();
    FReply HandleFitAll();
    FReply HandleResetView();
    void HandleFrameSearchChanged(const FText& SearchText);
    void LoadFile(const FString& FilePath);
    FString FindLatestSnapshotFile() const;
    void RefreshFrameFilter();
    void SelectFrameByArrayIndex(int32 FrameArrayIndex);
    void HandleTimelineSelection(int32 FrameArrayIndex);
    void HandleFrameSelectionChanged(
        TSharedPtr<FSekiroLuaAnimSnapshotFrame> Frame,
        ESelectInfo::Type SelectInfo);
    void HandleNodeSelectionChanged(
        TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
        ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow> GenerateFrameRow(
        TSharedPtr<FSekiroLuaAnimSnapshotFrame> Frame,
        const TSharedRef<STableViewBase>& OwnerTable) const;
    TSharedRef<ITableRow> GenerateNodeRow(
        TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
        const TSharedRef<STableViewBase>& OwnerTable) const;
    void GetNodeChildren(
        TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
        TArray<TSharedPtr<FSekiroLuaAnimSnapshotNode>>& OutChildren) const;
    void ExpandNodeRecursively(TSharedPtr<FSekiroLuaAnimSnapshotNode> Node);
    void UpdateStatus();
    void UpdateDetails();
    FString BuildNodeDetails() const;
    FString BuildFrameValueDetails() const;
    FString BuildTransitionDetails() const;

    FSekiroLuaAnimSnapshotDocument Document; // 当前文件的全部可用帧与解析警告
    TArray<TSharedPtr<FSekiroLuaAnimSnapshotFrame>> FilteredFrames; // 左侧搜索后的快照引用
    FString FrameSearchText; // 当前左侧快照搜索词
    int32 SelectedFrameArrayIndex = INDEX_NONE; // 当前帧在 Document.Frames 中的下标
    TSharedPtr<FSekiroLuaAnimSnapshotNode> SelectedNode; // 当前层级树节点
    TArray<TSharedPtr<FSekiroLuaAnimSnapshotNode>> NodeTreeRoots; // 当前帧供树控件稳定绑定的根节点
    TSharedPtr<SSekiroLuaAnimSnapshotTimeline> Timeline; // 主选择时间轴
    TSharedPtr<SListView<TSharedPtr<FSekiroLuaAnimSnapshotFrame>>> FrameList; // 无障碍辅助快照表
    TSharedPtr<STreeView<TSharedPtr<FSekiroLuaAnimSnapshotNode>>> NodeTree; // 当前帧节点层级树
    TSharedPtr<STextBlock> FilePathText; // 当前 JSONL 路径
    TSharedPtr<STextBlock> StatusText; // 加载结果和中文警告
    TSharedPtr<SMultiLineEditableTextBox> NodeDetailText; // 节点输入与输出详情
    TSharedPtr<SMultiLineEditableTextBox> FrameValueDetailText; // 本帧 AnimInstance 变量与曲线
    TSharedPtr<SMultiLineEditableTextBox> TransitionDetailText; // 本帧 Transition 分组详情
};
