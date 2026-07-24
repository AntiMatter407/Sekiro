#include "SekiroLuaAnimSnapshotViewer.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "SekiroLuaAnimSnapshotLoader.h"
#include "SekiroLuaAnimSnapshotTimeline.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

namespace SekiroLuaAnimSnapshotViewerPrivate
{
/**
 * 把快照原因转换为面向非技术用户的中文标签。
 * 可在游戏线程 UI 构造或刷新阶段调用；未知值原样返回。
 */
FString GetReasonLabel(const FString& CaptureReason)
{
    if (CaptureReason.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) return TEXT("开始");
    if (CaptureReason.Equals(TEXT("Interval"), ESearchCase::IgnoreCase)) return TEXT("定时采样");
    if (CaptureReason.Equals(TEXT("StateChanged"), ESearchCase::IgnoreCase)) return TEXT("状态变化");
    return CaptureReason.IsEmpty() ? TEXT("未说明") : CaptureReason;
}

/**
 * 返回节点在树中的主显示名，优先 Lua 临时别名，其次节点类型。
 * 可在 Slate 行生成阶段调用；Node 可为空。返回始终可读的非空文本。
 */
FString GetNodeDisplayName(const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Node)
{
    if (!Node.IsValid()) return TEXT("未知节点");
    if (!Node->PoseAlias.IsEmpty()) return Node->PoseAlias;
    if (!Node->NodeType.IsEmpty()) return Node->NodeType;
    return TEXT("未命名节点");
}

/**
 * 追加带标题的字符串映射，键按字典序排列以保证快照之间容易比较。
 * 可在游戏线程刷新详情时调用；Values 只读，OutText 会追加内容且不被清空。
 */
void AppendStringMap(
    const FString& Title,
    const TMap<FString, FString>& Values,
    FString& OutText)
{
    OutText += Title + TEXT("\n");
    if (Values.IsEmpty())
    {
        OutText += TEXT("  无\n");
        return;
    }
    TArray<FString> Keys;
    Values.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
    {
        OutText += FString::Printf(TEXT("  %s = %s\n"), *Key, *Values[Key]);
    }
}

/**
 * 追加带标题的数值映射，键按字典序排列并以四位小数显示权重。
 * 可在游戏线程刷新详情时调用；Values 只读，OutText 会追加内容且不被清空。
 */
void AppendNumberMap(
    const FString& Title,
    const TMap<FString, double>& Values,
    FString& OutText)
{
    OutText += Title + TEXT("\n");
    if (Values.IsEmpty())
    {
        OutText += TEXT("  无\n");
        return;
    }
    TArray<FString> Keys;
    Values.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
    {
        OutText += FString::Printf(TEXT("  %s = %.4f\n"), *Key, Values[Key]);
    }
}

/**
 * 递归判断动画节点及其子节点是否包含搜索词，覆盖类型、状态、动画名、输入和输出推导。
 * 仅在游戏线程的列表过滤阶段调用；Node 可为空，SearchText 必须是已裁剪的非空文本。
 *
 * @param Node 当前节点，只读且可为空。
 * @param SearchText 不区分大小写的搜索词。
 * @return 当前子树任一可见字段命中时返回 true。
 */
bool DoesNodeMatchSearch(
    const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Node,
    const FString& SearchText)
{
    if (!Node.IsValid()) return false;
    if (Node->NodeType.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->MachineName.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->CurrentState.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->PreviousState.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->NativeAssetName.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->ResolvedAnimationName.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->PoseAlias.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->OutputKind.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->OutputDerivation.Contains(SearchText, ESearchCase::IgnoreCase)
        || Node->RawDebugLine.Contains(SearchText, ESearchCase::IgnoreCase))
    {
        return true;
    }
    for (const TPair<FString, FString>& Input : Node->Inputs)
    {
        if (Input.Key.Contains(SearchText, ESearchCase::IgnoreCase)
            || Input.Value.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    for (const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Child : Node->Children)
    {
        if (DoesNodeMatchSearch(Child, SearchText)) return true;
    }
    return false;
}

/**
 * 判断一帧的时间、原因、变化描述、动画数据、变量、曲线或 Transition 是否包含搜索词。
 * 仅在游戏线程的列表过滤阶段调用；Frame 可为空，搜索不改变时间轴和文档原始顺序。
 *
 * @param Frame 待匹配快照，只读且可为空。
 * @param SearchText 已裁剪搜索词；空字符串匹配所有快照。
 * @return 任一快照字段不区分大小写命中时返回 true。
 */
bool DoesFrameMatchSearch(
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame,
    const FString& SearchText)
{
    if (!Frame.IsValid()) return false;
    if (SearchText.IsEmpty()) return true;

    const FString BasicText = FString::Printf(
        TEXT("%lld %.3f %s %s %s %s %s"),
        Frame->FrameIndex,
        Frame->SessionElapsedSeconds,
        *Frame->UtcTimestamp,
        *Frame->CaptureReason,
        *GetReasonLabel(Frame->CaptureReason),
        *Frame->AnimInstancePath,
        *Frame->LuaModuleName);
    if (BasicText.Contains(SearchText, ESearchCase::IgnoreCase)
        || Frame->ChangeTitle.Contains(
            SearchText,
            ESearchCase::IgnoreCase)
        || Frame->ChangeDescription.Contains(
            SearchText,
            ESearchCase::IgnoreCase)
        || Frame->ChangeDetails.Contains(
            SearchText,
            ESearchCase::IgnoreCase))
    {
        return true;
    }

    for (const TPair<FString, FString>& Variable : Frame->Variables)
    {
        if (Variable.Key.Contains(SearchText, ESearchCase::IgnoreCase)
            || Variable.Value.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    for (const TPair<FString, double>& Curve : Frame->Curves)
    {
        if (Curve.Key.Contains(SearchText, ESearchCase::IgnoreCase)
            || FString::SanitizeFloat(Curve.Value).Contains(
                SearchText,
                ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    for (const FSekiroLuaAnimTransitionSample& Transition : Frame->Transitions)
    {
        const FString TransitionText = FString::Printf(
            TEXT("%s %s %s %s %s %s"),
            *Transition.TransitionId,
            *Transition.ExpressionLabel,
            *Transition.ParameterName,
            *Transition.ParameterValue,
            Transition.bExpressionResult ? TEXT("true 通过") : TEXT("false 未通过"),
            Transition.bRuleResult ? TEXT("true 通过") : TEXT("false 未通过"));
        if (TransitionText.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    for (const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Root : Frame->Roots)
    {
        if (DoesNodeMatchSearch(Root, SearchText)) return true;
    }
    return false;
}
}

/**
 * 创建快照查看器工具栏、时间轴、辅助快照表、节点树和只读详情区域。
 * 必须在游戏线程由 Nomad Tab 构造；构造后自动尝试加载 Saved/LuaAnimSnapshots 最新文件。
 */
void SSekiroLuaAnimSnapshotViewer::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("打开文件")))
                    .ToolTipText(FText::FromString(TEXT("选择一个 Lua 动画 JSONL 快照文件")))
                    .OnClicked(this, &SSekiroLuaAnimSnapshotViewer::HandleOpenFile)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("刷新")))
                    .ToolTipText(FText::FromString(TEXT("重新读取当前文件")))
                    .OnClicked(this, &SSekiroLuaAnimSnapshotViewer::HandleRefresh)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("读取最新")))
                    .ToolTipText(FText::FromString(TEXT("读取 Saved/LuaAnimSnapshots 中最新的 JSONL 文件")))
                    .OnClicked(this, &SSekiroLuaAnimSnapshotViewer::HandleLoadLatest)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("适配全部")))
                    .ToolTipText(FText::FromString(TEXT("让时间轴显示全部快照")))
                    .OnClicked(this, &SSekiroLuaAnimSnapshotViewer::HandleFitAll)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("重置视图")))
                    .ToolTipText(FText::FromString(TEXT("恢复默认五秒时间范围")))
                    .OnClicked(this, &SSekiroLuaAnimSnapshotViewer::HandleResetView)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SAssignNew(FilePathText, STextBlock)
                .Text(FText::FromString(TEXT("当前文件：尚未加载")))
                .ToolTipText(FText::FromString(TEXT("当前读取的 JSONL 快照文件")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 6.0f)
            [
                SAssignNew(StatusText, STextBlock)
                .Text(FText::FromString(TEXT("正在查找最新快照……")))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(Timeline, SSekiroLuaAnimSnapshotTimeline)
                .Frames(&Document.Frames)
                .OnSnapshotSelected(this, &SSekiroLuaAnimSnapshotViewer::HandleTimelineSelection)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f, 2.0f, 0.0f, 6.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(
                    TEXT("绿：开始    蓝：定时采样    橙：状态变化    黄：当前选择    滚轮缩放，中键或右键拖动平移")))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SSplitter)
                + SSplitter::Slot()
                .Value(0.22f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(2.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("快照与变化")))
                        .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(2.0f, 2.0f, 2.0f, 4.0f)
                    [
                        SNew(SSearchBox)
                        .HintText(FText::FromString(
                            TEXT("搜索时间、状态、动画、变量或 Transition")))
                        .OnTextChanged(
                            this,
                            &SSekiroLuaAnimSnapshotViewer::HandleFrameSearchChanged)
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SAssignNew(FrameList, SListView<TSharedPtr<FSekiroLuaAnimSnapshotFrame>>)
                        .ListItemsSource(&FilteredFrames)
                        .SelectionMode(ESelectionMode::Single)
                        .OnGenerateRow(this, &SSekiroLuaAnimSnapshotViewer::GenerateFrameRow)
                        .OnSelectionChanged(
                            this,
                            &SSekiroLuaAnimSnapshotViewer::HandleFrameSelectionChanged)
                    ]
                ]
                + SSplitter::Slot()
                .Value(0.36f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(6.0f, 2.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("当前动画层级")))
                        .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SAssignNew(NodeTree, STreeView<TSharedPtr<FSekiroLuaAnimSnapshotNode>>)
                        .TreeItemsSource(&NodeTreeRoots)
                        .SelectionMode(ESelectionMode::Single)
                        .OnGenerateRow(this, &SSekiroLuaAnimSnapshotViewer::GenerateNodeRow)
                        .OnGetChildren(this, &SSekiroLuaAnimSnapshotViewer::GetNodeChildren)
                        .OnSelectionChanged(
                            this,
                            &SSekiroLuaAnimSnapshotViewer::HandleNodeSelectionChanged)
                    ]
                ]
                + SSplitter::Slot()
                .Value(0.42f)
                [
                    SNew(SSplitter)
                    .Orientation(Orient_Vertical)
                    + SSplitter::Slot()
                    .Value(0.42f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(6.0f, 2.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("所选节点详情")))
                            .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(NodeDetailText, SMultiLineEditableTextBox)
                            .IsReadOnly(true)
                            .Text(FText::FromString(TEXT("请选择一个快照和节点。")))
                        ]
                    ]
                    + SSplitter::Slot()
                    .Value(0.36f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(6.0f, 2.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("本帧变量与曲线")))
                            .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(FrameValueDetailText, SMultiLineEditableTextBox)
                            .IsReadOnly(true)
                            .Text(FText::FromString(TEXT("请选择一个快照。")))
                        ]
                    ]
                    + SSplitter::Slot()
                    .Value(0.22f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(6.0f, 2.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("本帧 Transition")))
                            .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                        ]
                        + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                        [
                            SAssignNew(TransitionDetailText, SMultiLineEditableTextBox)
                            .IsReadOnly(true)
                            .Text(FText::FromString(TEXT("请选择一个快照。")))
                        ]
                    ]
                ]
            ]
        ]
    ];

    const FString LatestFile = FindLatestSnapshotFile();
    if (LatestFile.IsEmpty())
    {
        UpdateStatus();
    }
    else
    {
        LoadFile(LatestFile);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
/**
 * 测试专用：调用左侧快照列表的真实搜索匹配逻辑，不创建 Slate 控件。
 * 可在自动化测试线程入口调用；Frame 只读且可为空，SearchText 为空时匹配有效帧。
 *
 * @param Frame 待匹配快照。
 * @param SearchText 不区分大小写的搜索词。
 * @return 与实际查看器过滤规则相同的匹配结果。
 */
bool SSekiroLuaAnimSnapshotViewer::DoesFrameMatchSearchForTesting(
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame,
    const FString& SearchText)
{
    return SekiroLuaAnimSnapshotViewerPrivate::DoesFrameMatchSearch(
        Frame,
        SearchText.TrimStartAndEnd());
}
#endif

/**
 * 打开原生文件选择器并加载用户选中的 JSONL 文件。
 * 必须在游戏线程调用；取消选择不会改变当前文档。返回已处理的 Slate 回复。
 */
FReply SSekiroLuaAnimSnapshotViewer::HandleOpenFile()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform == nullptr) return FReply::Handled();

    const FString DefaultDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("LuaAnimSnapshots"));
    TArray<FString> SelectedFiles;
    const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(
        nullptr);
    const bool bOpened = DesktopPlatform->OpenFileDialog(
        ParentWindowHandle,
        TEXT("打开 Lua 动画快照"),
        DefaultDirectory,
        TEXT(""),
        TEXT("Lua 动画快照 (*.jsonl)|*.jsonl|所有文件 (*.*)|*.*"),
        EFileDialogFlags::None,
        SelectedFiles);
    if (bOpened && !SelectedFiles.IsEmpty()) LoadFile(SelectedFiles[0]);
    return FReply::Handled();
}

/**
 * 重新读取当前文件；尚未选择文件时改为尝试加载最新快照。
 * 必须在游戏线程调用。返回已处理的 Slate 回复。
 */
FReply SSekiroLuaAnimSnapshotViewer::HandleRefresh()
{
    if (Document.SourceFilePath.IsEmpty()) return HandleLoadLatest();
    LoadFile(Document.SourceFilePath);
    return FReply::Handled();
}

/**
 * 查找并加载 Saved/LuaAnimSnapshots 中修改时间最新的 JSONL 文件。
 * 必须在游戏线程调用；未找到时清空文档并显示中文提示。返回已处理的 Slate 回复。
 */
FReply SSekiroLuaAnimSnapshotViewer::HandleLoadLatest()
{
    const FString LatestFile = FindLatestSnapshotFile();
    if (LatestFile.IsEmpty())
    {
        Document = FSekiroLuaAnimSnapshotDocument();
        SelectedFrameArrayIndex = INDEX_NONE;
        SelectedNode.Reset();
        NodeTreeRoots.Reset();
        if (Timeline.IsValid()) Timeline->SetFrames(&Document.Frames);
        RefreshFrameFilter();
        if (NodeTree.IsValid())
        {
            NodeTree->ClearSelection();
            NodeTree->RequestTreeRefresh();
        }
        UpdateStatus();
        UpdateDetails();
        return FReply::Handled();
    }
    LoadFile(LatestFile);
    return FReply::Handled();
}

/** 让主时间轴显示全部帧；必须在游戏线程调用，返回已处理的 Slate 回复。 */
FReply SSekiroLuaAnimSnapshotViewer::HandleFitAll()
{
    if (Timeline.IsValid()) Timeline->FitAll();
    return FReply::Handled();
}

/** 恢复主时间轴默认五秒范围；必须在游戏线程调用，返回已处理的 Slate 回复。 */
FReply SSekiroLuaAnimSnapshotViewer::HandleResetView()
{
    if (Timeline.IsValid()) Timeline->ResetView();
    return FReply::Handled();
}

/**
 * 更新左侧快照搜索词并立即刷新过滤结果；搜索不改变时间轴、当前快照或文件内容。
 * 只能由 Slate 搜索框在游戏线程调用；SearchText 可为空，空值恢复全部快照。
 *
 * @param SearchText 搜索框当前完整文本。
 */
void SSekiroLuaAnimSnapshotViewer::HandleFrameSearchChanged(
    const FText& SearchText)
{
    FrameSearchText = SearchText.ToString().TrimStartAndEnd();
    RefreshFrameFilter();
    UpdateStatus();
}

/**
 * 容错加载指定文件并同步刷新时间轴、辅助表、树与详情。
 * 必须在游戏线程调用；FilePath 只读，解析失败会保留中文警告且不会抛弃其他有效行。
 */
void SSekiroLuaAnimSnapshotViewer::LoadFile(const FString& FilePath)
{
    FSekiroLuaAnimSnapshotLoader::LoadFile(FilePath, Document);
    SelectedFrameArrayIndex = INDEX_NONE;
    SelectedNode.Reset();
    NodeTreeRoots.Reset();
    if (Timeline.IsValid()) Timeline->SetFrames(&Document.Frames);
    RefreshFrameFilter();
    if (NodeTree.IsValid())
    {
        NodeTree->ClearSelection();
        NodeTree->RequestTreeRefresh();
    }
    UpdateStatus();
    if (!Document.Frames.IsEmpty()) SelectFrameByArrayIndex(0);
    else UpdateDetails();
}

/**
 * 使用当前搜索词重建左侧引用数组，并在仍命中时恢复当前帧的列表选择。
 * 只能在游戏线程调用；不会复制快照内容，也不会改变 Document.Frames 或时间轴下标。
 */
void SSekiroLuaAnimSnapshotViewer::RefreshFrameFilter()
{
    FilteredFrames.Reset();
    for (const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame : Document.Frames)
    {
        if (SekiroLuaAnimSnapshotViewerPrivate::DoesFrameMatchSearch(
            Frame,
            FrameSearchText))
        {
            FilteredFrames.Add(Frame);
        }
    }
    if (!FrameList.IsValid()) return;

    FrameList->ClearSelection();
    FrameList->RequestListRefresh();
    if (Document.Frames.IsValidIndex(SelectedFrameArrayIndex))
    {
        const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& SelectedFrame =
            Document.Frames[SelectedFrameArrayIndex];
        if (FilteredFrames.Contains(SelectedFrame))
        {
            FrameList->SetSelection(SelectedFrame, ESelectInfo::Direct);
            FrameList->RequestScrollIntoView(SelectedFrame);
        }
    }
}

/**
 * 按文件修改时间查找 Saved/LuaAnimSnapshots 下最新 JSONL，不递归扫描。
 * 可在游戏线程调用并只读文件系统。返回绝对路径；未找到返回空字符串。
 */
FString SSekiroLuaAnimSnapshotViewer::FindLatestSnapshotFile() const
{
    FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LuaAnimSnapshots"));
    FPaths::NormalizeDirectoryName(Directory);
    TArray<FString> FileNames;
    IFileManager::Get().FindFiles(FileNames, *Directory, TEXT("jsonl"));

    FString LatestPath;
    FDateTime LatestTime = FDateTime::MinValue();
    for (const FString& FileName : FileNames)
    {
        const FString CandidatePath = FPaths::ConvertRelativePathToFull(
            FPaths::Combine(Directory, FileName));
        const FDateTime CandidateTime = IFileManager::Get().GetTimeStamp(*CandidatePath);
        if (LatestPath.IsEmpty() || CandidateTime > LatestTime)
        {
            LatestPath = CandidatePath;
            LatestTime = CandidateTime;
        }
    }
    return LatestPath;
}

/**
 * 统一更新时间轴、辅助表、节点树和详情的当前帧。
 * 必须在游戏线程调用；FrameArrayIndex 越界时清除选择。
 */
void SSekiroLuaAnimSnapshotViewer::SelectFrameByArrayIndex(const int32 FrameArrayIndex)
{
    if (!Document.Frames.IsValidIndex(FrameArrayIndex))
    {
        SelectedFrameArrayIndex = INDEX_NONE;
        SelectedNode.Reset();
        NodeTreeRoots.Reset();
        if (Timeline.IsValid()) Timeline->SetSelectedFrameIndex(INDEX_NONE);
        if (NodeTree.IsValid())
        {
            NodeTree->ClearSelection();
            NodeTree->RequestTreeRefresh();
        }
        UpdateDetails();
        return;
    }

    SelectedFrameArrayIndex = FrameArrayIndex;
    SelectedNode.Reset();
    if (Timeline.IsValid()) Timeline->SetSelectedFrameIndex(FrameArrayIndex);
    if (FrameList.IsValid())
    {
        const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame =
            Document.Frames[FrameArrayIndex];
        if (FilteredFrames.Contains(Frame))
        {
            FrameList->SetSelection(Frame, ESelectInfo::Direct);
            FrameList->RequestScrollIntoView(Frame);
        }
        else FrameList->ClearSelection();
    }
    if (NodeTree.IsValid())
    {
        NodeTree->ClearSelection();
        NodeTreeRoots = Document.Frames[FrameArrayIndex]->Roots;
        NodeTree->RequestTreeRefresh();
        for (const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Root : NodeTreeRoots)
        {
            ExpandNodeRecursively(Root);
        }
    }
    UpdateDetails();
}

/**
 * 接收时间轴点选的真实帧数组下标并切换查看内容。
 * 必须在游戏线程调用；越界值由统一选择函数处理。
 */
void SSekiroLuaAnimSnapshotViewer::HandleTimelineSelection(const int32 FrameArrayIndex)
{
    SelectFrameByArrayIndex(FrameArrayIndex);
}

/**
 * 接收辅助快照表选择并反向同步主时间轴。
 * 必须在游戏线程调用；Frame 可为空，SelectInfo 仅由 Slate 提供且不改变行为。
 */
void SSekiroLuaAnimSnapshotViewer::HandleFrameSelectionChanged(
    TSharedPtr<FSekiroLuaAnimSnapshotFrame> Frame,
    const ESelectInfo::Type SelectInfo)
{
    if (!Frame.IsValid()) return;
    const int32 FrameArrayIndex = Document.Frames.IndexOfByKey(Frame);
    if (FrameArrayIndex != SelectedFrameArrayIndex) SelectFrameByArrayIndex(FrameArrayIndex);
}

/**
 * 接收当前层级树节点选择并刷新只读节点详情。
 * 必须在游戏线程调用；Node 可为空，用于清除详情，SelectInfo 仅由 Slate 提供。
 */
void SSekiroLuaAnimSnapshotViewer::HandleNodeSelectionChanged(
    TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
    const ESelectInfo::Type SelectInfo)
{
    SelectedNode = Node;
    UpdateDetails();
}

/**
 * 为左侧快照表生成独立卡片，以原因色、帧号、单句变化摘要和 UTC 明确分隔记录。
 * 由 Slate 游戏线程按需调用；OwnerTable 拥有返回行。返回始终有效的表格行。
 */
TSharedRef<ITableRow> SSekiroLuaAnimSnapshotViewer::GenerateFrameRow(
    TSharedPtr<FSekiroLuaAnimSnapshotFrame> Frame,
    const TSharedRef<STableViewBase>& OwnerTable) const
{
    FString HeaderText(TEXT("无效快照"));
    FString DescriptionText(TEXT("无法读取这条快照。"));
    FString TimestampText;
    FString ToolTipText = DescriptionText;
    FLinearColor AccentColor(0.55f, 0.55f, 0.55f, 1.0f);
    FLinearColor CardColor(0.12f, 0.12f, 0.12f, 0.92f);
    if (Frame.IsValid())
    {
        HeaderText = FString::Printf(
            TEXT("#%lld    %.3f 秒    %s"),
            Frame->FrameIndex,
            Frame->SessionElapsedSeconds,
            *Frame->ChangeTitle);
        DescriptionText = Frame->ChangeDescription;
        TimestampText = Frame->UtcTimestamp;
        if (Frame->CaptureReason.Equals(
            TEXT("Start"),
            ESearchCase::IgnoreCase))
        {
            AccentColor = FLinearColor(0.25f, 0.80f, 0.35f, 1.0f);
        }
        else if (Frame->CaptureReason.Equals(
            TEXT("StateChanged"),
            ESearchCase::IgnoreCase))
        {
            AccentColor = FLinearColor(1.0f, 0.55f, 0.15f, 1.0f);
        }
        else if (Frame->CaptureReason.Equals(
            TEXT("Interval"),
            ESearchCase::IgnoreCase))
        {
            AccentColor = FLinearColor(0.25f, 0.55f, 1.0f, 1.0f);
        }
        CardColor = Frame->FrameIndex % 2 == 0
            ? FLinearColor(0.10f, 0.10f, 0.10f, 0.94f)
            : FLinearColor(0.16f, 0.16f, 0.16f, 0.94f);
        ToolTipText = FString::Printf(
            TEXT("#%lld  %.3fs  %s\n%s\n采样原因：%s\n%s"),
            Frame->FrameIndex,
            Frame->SessionElapsedSeconds,
            *Frame->ChangeTitle,
            *Frame->ChangeDetails,
            *SekiroLuaAnimSnapshotViewerPrivate::GetReasonLabel(Frame->CaptureReason),
            *Frame->UtcTimestamp);
    }
    return SNew(STableRow<TSharedPtr<FSekiroLuaAnimSnapshotFrame>>, OwnerTable)
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(CardColor)
        .Padding(FMargin(8.0f, 6.0f))
        .ToolTipText(FText::FromString(ToolTipText))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(HeaderText))
                .ColorAndOpacity(AccentColor)
                .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f, 0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(DescriptionText))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TimestampText))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
        ]
    ];
}

/**
 * 为节点树生成一行，集中显示别名、类型、状态机上下文、权重和解析动画或输出种类。
 * 由 Slate 游戏线程按需调用；OwnerTable 拥有返回行。返回始终有效的树行。
 */
TSharedRef<ITableRow> SSekiroLuaAnimSnapshotViewer::GenerateNodeRow(
    TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
    const TSharedRef<STableViewBase>& OwnerTable) const
{
    FString RowText = SekiroLuaAnimSnapshotViewerPrivate::GetNodeDisplayName(Node);
    if (Node.IsValid())
    {
        RowText += FString::Printf(
            TEXT("  [%s]  权重 %.3f"),
            Node->NodeType.IsEmpty() ? TEXT("未知类型") : *Node->NodeType,
            Node->AbsoluteWeight);
        if (!Node->CurrentState.IsEmpty())
        {
            RowText += FString::Printf(
                TEXT("  状态 %s"),
                *Node->CurrentState);
            if (!Node->PreviousState.IsEmpty())
            {
                RowText += FString::Printf(
                    TEXT(" ← %s (混合 %.3f)"),
                    *Node->PreviousState,
                    Node->BlendAlpha);
            }
        }
        const FString OutputText = !Node->ResolvedAnimationName.IsEmpty()
            ? Node->ResolvedAnimationName
            : Node->OutputKind;
        if (!OutputText.IsEmpty()) RowText += TEXT("  输出 ") + OutputText;
    }
    return SNew(STableRow<TSharedPtr<FSekiroLuaAnimSnapshotNode>>, OwnerTable)
    [
        SNew(STextBlock)
        .Text(FText::FromString(RowText))
        .ToolTipText(FText::FromString(RowText))
    ];
}

/**
 * 向 STreeView 提供指定节点的直接 Children，不复制节点内容。
 * 由 Slate 游戏线程按需调用；Node 可为空，OutChildren 会追加有效子节点引用。
 */
void SSekiroLuaAnimSnapshotViewer::GetNodeChildren(
    TSharedPtr<FSekiroLuaAnimSnapshotNode> Node,
    TArray<TSharedPtr<FSekiroLuaAnimSnapshotNode>>& OutChildren) const
{
    if (Node.IsValid()) OutChildren.Append(Node->Children);
}

/**
 * 默认展开一个节点的完整活跃子树，保证选中快照后无需逐层点击即可看到深层节点和 Montage。
 * 必须在游戏线程且 NodeTree 已构造时调用；Node 可为空，本函数只改变 Slate 展开状态。
 *
 * @param Node 当前待展开节点；其 Children 会按快照层级递归处理。
 */
void SSekiroLuaAnimSnapshotViewer::ExpandNodeRecursively(
    TSharedPtr<FSekiroLuaAnimSnapshotNode> Node)
{
    if (!NodeTree.IsValid() || !Node.IsValid()) return;
    NodeTree->SetItemExpansion(Node, true);
    for (const TSharedPtr<FSekiroLuaAnimSnapshotNode>& Child : Node->Children)
    {
        ExpandNodeRecursively(Child);
    }
}

/**
 * 根据当前文档刷新文件路径与加载状态；警告汇总后显示首条具体原因。
 * 必须在游戏线程且控件构造完成后调用，不改变选择或数据。
 */
void SSekiroLuaAnimSnapshotViewer::UpdateStatus()
{
    if (FilePathText.IsValid())
    {
        const FString PathLabel = Document.SourceFilePath.IsEmpty()
            ? TEXT("当前文件：尚未加载")
            : TEXT("当前文件：") + Document.SourceFilePath;
        FilePathText->SetText(FText::FromString(PathLabel));
    }
    if (!StatusText.IsValid()) return;

    FString Status;
    if (Document.SourceFilePath.IsEmpty())
    {
        Status = TEXT("Saved/LuaAnimSnapshots 中没有找到快照。可以点击“打开文件”手动选择。");
    }
    else if (Document.Frames.IsEmpty())
    {
        Status = TEXT("文件中没有可用快照。");
    }
    else
    {
        Status = FString::Printf(TEXT("已加载 %d 个快照。"), Document.Frames.Num());
        if (!FrameSearchText.IsEmpty())
        {
            Status += FString::Printf(
                TEXT("  搜索结果：%d 个。"),
                FilteredFrames.Num());
        }
    }
    if (!Document.Warnings.IsEmpty())
    {
        Status += FString::Printf(
            TEXT("  有 %d 条解析提示：%s"),
            Document.Warnings.Num(),
            *Document.Warnings[0]);
    }
    StatusText->SetText(FText::FromString(Status));
}

/**
 * 按当前帧和节点刷新节点、变量曲线及 Transition 三块只读详情文本。
 * 必须在游戏线程且详情控件构造完成后调用，不改变数据或选择。
 */
void SSekiroLuaAnimSnapshotViewer::UpdateDetails()
{
    if (NodeDetailText.IsValid())
    {
        NodeDetailText->SetText(FText::FromString(BuildNodeDetails()));
    }
    if (FrameValueDetailText.IsValid())
    {
        FrameValueDetailText->SetText(FText::FromString(BuildFrameValueDetails()));
    }
    if (TransitionDetailText.IsValid())
    {
        TransitionDetailText->SetText(FText::FromString(BuildTransitionDetails()));
    }
}

/**
 * 为当前所选节点生成输入、状态权重、输出推导和原始调试行文本。
 * 必须在游戏线程调用，只读当前选择。未选择节点时返回明确中文提示。
 */
FString SSekiroLuaAnimSnapshotViewer::BuildNodeDetails() const
{
    if (!Document.Frames.IsValidIndex(SelectedFrameArrayIndex))
    {
        return TEXT("请选择一个快照。");
    }
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame =
        Document.Frames[SelectedFrameArrayIndex];
    if (!SelectedNode.IsValid())
    {
        return FString::Printf(
            TEXT("帧 #%lld，%.3f 秒\nLua 模块：%s\n动画实例：%s\n\n请选择左侧动画层级中的节点。"),
            Frame->FrameIndex,
            Frame->SessionElapsedSeconds,
            *Frame->LuaModuleName,
            *Frame->AnimInstancePath);
    }

    FString Details = FString::Printf(
        TEXT("别名：%s\n节点类型：%s\n节点权重：%.4f\nLua 动画名：%s\n原生资产名：%s\n输出类型：%s\n\n"),
        *SekiroLuaAnimSnapshotViewerPrivate::GetNodeDisplayName(SelectedNode),
        *SelectedNode->NodeType,
        SelectedNode->AbsoluteWeight,
        *SelectedNode->ResolvedAnimationName,
        *SelectedNode->NativeAssetName,
        *SelectedNode->OutputKind);
    SekiroLuaAnimSnapshotViewerPrivate::AppendStringMap(
        TEXT("输入参数"),
        SelectedNode->Inputs,
        Details);
    Details += TEXT("\n");
    SekiroLuaAnimSnapshotViewerPrivate::AppendNumberMap(
        TEXT("状态权重"),
        SelectedNode->StateWeights,
        Details);
    Details += FString::Printf(
        TEXT("\n输出是如何得到的\n%s\n\n原始调试内容\n%s"),
        SelectedNode->OutputDerivation.IsEmpty()
            ? TEXT("未记录")
            : *SelectedNode->OutputDerivation,
        SelectedNode->RawDebugLine.IsEmpty()
            ? TEXT("未记录")
            : *SelectedNode->RawDebugLine);
    return Details;
}

/**
 * 为当前快照生成全部 Blueprint 可见 AnimInstance 变量及当前实际曲线值文本。
 * 必须在游戏线程调用，只读已经反序列化的帧；键按字典序显示，旧 Schema 缺字段时给出明确提示。
 *
 * @return 可直接放入只读文本框的多行详情；未选择帧时返回提示。
 */
FString SSekiroLuaAnimSnapshotViewer::BuildFrameValueDetails() const
{
    if (!Document.Frames.IsValidIndex(SelectedFrameArrayIndex))
    {
        return TEXT("请选择一个快照。");
    }
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame =
        Document.Frames[SelectedFrameArrayIndex];
    FString Details = FString::Printf(
        TEXT("动画蓝图变量：%d 项\n曲线：%d 项\n\n"),
        Frame->Variables.Num(),
        Frame->Curves.Num());
    SekiroLuaAnimSnapshotViewerPrivate::AppendStringMap(
        TEXT("变量"),
        Frame->Variables,
        Details);
    Details += TEXT("\n");
    SekiroLuaAnimSnapshotViewerPrivate::AppendNumberMap(
        TEXT("当前曲线值"),
        Frame->Curves,
        Details);
    return Details;
}

/**
 * 按 TransitionId 对本帧表达式采样分组，显示最终结果、参数值和求值时间。
 * 必须在游戏线程调用，只读当前帧。没有帧或记录时返回明确中文提示。
 */
FString SSekiroLuaAnimSnapshotViewer::BuildTransitionDetails() const
{
    if (!Document.Frames.IsValidIndex(SelectedFrameArrayIndex))
    {
        return TEXT("请选择一个快照。");
    }
    const TSharedPtr<FSekiroLuaAnimSnapshotFrame>& Frame =
        Document.Frames[SelectedFrameArrayIndex];
    if (Frame->Transitions.IsEmpty()) return TEXT("本帧没有 Transition 求值记录。");

    TArray<FSekiroLuaAnimTransitionSample> SortedSamples = Frame->Transitions;
    SortedSamples.StableSort([](
        const FSekiroLuaAnimTransitionSample& Left,
        const FSekiroLuaAnimTransitionSample& Right)
    {
        return Left.TransitionId < Right.TransitionId;
    });

    FString Details;
    FString PreviousTransitionId;
    for (const FSekiroLuaAnimTransitionSample& Sample : SortedSamples)
    {
        const FString TransitionId = Sample.TransitionId.IsEmpty()
            ? TEXT("未命名 Transition")
            : Sample.TransitionId;
        if (TransitionId != PreviousTransitionId)
        {
            bool bFinalRuleResult = Sample.bRuleResult;
            for (const FSekiroLuaAnimTransitionSample& Candidate : SortedSamples)
            {
                if (Candidate.TransitionId != Sample.TransitionId) continue;
                bFinalRuleResult = Candidate.bRuleResult;
                if (Candidate.bIsFinal) break;
            }
            if (!Details.IsEmpty()) Details += TEXT("\n");
            Details += FString::Printf(
                TEXT("[%s]  最终结果：%s\n"),
                *TransitionId,
                bFinalRuleResult ? TEXT("通过") : TEXT("不通过"));
            PreviousTransitionId = TransitionId;
        }
        Details += FString::Printf(
            TEXT("  %s：%s = %s (%s) → %s%s\n"),
            Sample.ExpressionLabel.IsEmpty() ? TEXT("表达式") : *Sample.ExpressionLabel,
            Sample.ParameterName.IsEmpty() ? TEXT("无参数") : *Sample.ParameterName,
            Sample.ParameterValue.IsEmpty() ? TEXT("未记录") : *Sample.ParameterValue,
            Sample.ParameterType.IsEmpty() ? TEXT("未知类型") : *Sample.ParameterType,
            Sample.bExpressionResult ? TEXT("true") : TEXT("false"),
            Sample.bIsFinal ? TEXT("（最终采样）") : TEXT(""));
        if (!Sample.ExpectedValue.IsEmpty())
        {
            Details += TEXT("    期望值：") + Sample.ExpectedValue + TEXT("\n");
        }
        if (!Sample.Threshold.IsEmpty())
        {
            Details += TEXT("    阈值：") + Sample.Threshold + TEXT("\n");
        }
        if (!Sample.EvaluatedUtcTimestamp.IsEmpty())
        {
            Details += TEXT("    求值时间：") + Sample.EvaluatedUtcTimestamp + TEXT("\n");
        }
    }
    return Details;
}
