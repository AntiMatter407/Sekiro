#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#include "SekiroAnimGraphIR.generated.h"

/** IR 属性值的显式类型标签。 */
UENUM(BlueprintType)
enum class ESekiroAnimIRValueType : uint8
{
    Bool,
    Integer,
    Float,
    Name,
    String,
    SoftObjectPath,
    SoftClassPath,
};

/** Pin 在节点上的数据流方向。 */
UENUM(BlueprintType)
enum class ESekiroAnimIRPinDirection : uint8
{
    Input,
    Output,
};

/** IR 诊断严重级别。 */
UENUM(BlueprintType)
enum class ESekiroAnimIRDiagnosticSeverity : uint8
{
    Info,
    Warning,
    Error,
};

/** Lua 源码中的稳定定位信息。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRSourceLocation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString LuaModule;                    // Lua 模块名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 Line = 0;                       // 一基行号，未知时为 0

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 Column = 0;                     // 一基列号，未知时为 0
};

/** 不依赖 JSON 文本的类型化属性值。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ESekiroAnimIRValueType Type = ESekiroAnimIRValueType::Bool; // 当前有效值类型

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool BoolValue = false;               // Bool 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int64 IntegerValue = 0;               // Integer 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    double FloatValue = 0.0;              // Float 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName NameValue = NAME_None;           // Name 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString StringValue;                  // String 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath SoftObjectPathValue;  // SoftObjectPath 值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftClassPath SoftClassPathValue;    // SoftClassPath 值
};

/** 节点的具名类型化属性。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRProperty
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // 注册属性名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRValue Value;              // 显式类型属性值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序
};

/** 节点 Pin 定义。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRPin
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 节点内稳定 Pin 名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ESekiroAnimIRPinDirection Direction = ESekiroAnimIRPinDirection::Input; // 数据流方向

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName DataType = NAME_None;           // 注册数据类型名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bAllowMultipleConnections = false; // 输入 Pin 是否允许多连接

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序
};

/** Link 的节点与 Pin 端点。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRPinEndpoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString NodeId;                       // 全局稳定节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString PinName;                      // 节点内稳定 Pin 名
};

/** 两个 Pin 之间的有向连接。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRLink
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定 Link ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRPinEndpoint Source;      // 输出端点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRPinEndpoint Target;      // 输入端点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 与具体 UAnimGraphNode 解耦的节点描述。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName NodeType = NAME_None;           // 由后续 NodeFactory 解析的注册名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString DisplayName;                  // 编辑器显示名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString OwnedGraphId;                 // 节点独占的内部 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRPin> Pins;         // Pin 定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRProperty> Properties; // 类型化属性

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 过渡编译设置。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRTransitionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    float BlendDuration = 0.2f;           // 混合时长，单位秒

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 PriorityOrder = 0;              // 同源状态过渡优先级

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName BlendMode = TEXT("Linear");    // 注册混合模式名
};

/** Transition Gate 的单个扁平 AST 节点。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRTransitionGateNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Type = NAME_None;               // LuaBool/BoolProperty/TimeRemainingLessEqual/CurveGreaterEqual/All/Any/Not

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // Bool 属性名或 Curve 名；其他节点可为 None

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    float Threshold = 0.0f;               // 时间或曲线比较阈值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bExpectedBool = false;            // BoolProperty 叶节点期望值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<int32> Children;               // 子 Gate 节点索引
};

/** Transition Gate 的扁平 AST。RootIndex 为 -1 表示没有原生 Gate。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRTransitionGate
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 RootIndex = INDEX_NONE;          // Nodes 中的根节点索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRTransitionGateNode> Nodes; // 前序生成的 Gate 节点
};

/** 状态机中的状态。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 编辑器状态名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString GraphId;                      // 状态 Pose 子 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bAlwaysResetOnEntry = false;     // 进入状态时是否重置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 状态机中的有向过渡。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRTransition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定过渡 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Key;                          // 状态机内唯一语义键

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString SourceStateId;                // 起始状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString TargetStateId;                // 目标状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName RuleFunctionName = NAME_None;   // 过渡规则函数名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRTransitionSettings Settings; // 过渡编译设置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRTransitionGate Gate;      // 原生 Transition Rule Gate

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 生成到 AnimBlueprint GeneratedClass 的成员变量。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRVariable
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // GeneratedClass 成员名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName DataType = NAME_None;           // Bool/Float/Byte/Enum

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath TypeObjectPath;        // Enum 时指向 UEnum，其余类型为空

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRValue DefaultValue;       // 与 DataType 对应的默认值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bTransient = true;               // 是否标记为 Blueprint Transient

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** Graph 内嵌的状态机定义。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRStateMachine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString EntryStateId;                 // Entry 指向的状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRState> States;     // 状态定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRTransition> Transitions; // 过渡定义
};

/** Graph 中未显式放置元素的编辑器自动排版风格。 */
UENUM(BlueprintType)
enum class ESekiroAnimIRLayoutStyle : uint8
{
    Auto,
    LeftToRight,
    RightToLeft,
    TopToBottom,
    BottomToTop,
    CompactGrid,
    Radial,
    HierarchicalBlocks,
};

/** 一个节点或状态在布局分区内的逻辑单元格。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRLayoutItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString ElementId;                    // 当前 Graph 内节点或状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 Column = 0;                     // 分区内非负列索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 Row = 0;                        // 分区内非负行索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 ColumnSpan = 1;                 // 横向占用单元格数

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 RowSpan = 1;                    // 纵向占用单元格数

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 分区内声明顺序
};

/** Graph 画布上的一个独立布局分区。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRLayoutGrid
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // Graph 内唯一分区名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 RegionColumn = 0;               // 画布区域列

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 RegionRow = 0;                  // 画布区域行

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 CellWidth = 360;                // 单元格横向像素间距

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 CellHeight = 220;               // 单元格纵向像素间距

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ESekiroAnimIRLayoutStyle LayoutStyle = ESekiroAnimIRLayoutStyle::Auto; // 分区风格覆盖

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRLayoutItem> Items; // 显式放置元素
};

/** Graph 级编辑器布局元数据。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRGraphLayout
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ESekiroAnimIRLayoutStyle Style = ESekiroAnimIRLayoutStyle::HierarchicalBlocks; // 未显式放置元素的自动风格

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRLayoutGrid> Grids; // 显式布局分区
};

/** 单个动画 Graph。GraphType 是注册名，不绑定 UAnimGraphNode 类型。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRGraph
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 编辑器 Graph 名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName GraphType = NAME_None;          // 注册 Graph 类型名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString RootNodeId;                   // Pose Graph 根节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRNode> Nodes;       // 节点定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRLink> Links;       // Pin 连接

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRStateMachine StateMachine; // StateMachine Graph 数据

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRGraphLayout Layout;       // 仅供编辑器生成节点坐标

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** AnimBlueprint 动画层。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定 Layer ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 动画层名称

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString RootGraphId;                  // 动画层入口 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRGraph> Graphs;     // 本层 Graph

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 一份可 Canonicalize 和验证的完整 AnimBlueprint IR。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimBlueprintIR
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 SchemaVersion = 2;              // IR Schema 版本

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString SourceModule;                 // 生成 IR 的 Lua 模块

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftClassPath ParentAnimInstanceClass; // 父 AnimInstance 类软路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath TargetSkeleton;        // 目标 Skeleton 资产软路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRVariable> Variables; // GeneratedClass 成员变量

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSekiroAnimIRLayer> Layers;     // 动画层定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // Blueprint 声明位置
};

/** Validator 返回的稳定结构化诊断。 */
USTRUCT(BlueprintType)
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRDiagnostic
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FName Code = NAME_None;               // 稳定错误代码

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    ESekiroAnimIRDiagnosticSeverity Severity = ESekiroAnimIRDiagnosticSeverity::Error; // 严重级别

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FString Message;                      // 人类可读消息

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FString SubjectId;                    // 关联实体稳定 ID

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FSekiroAnimIRSourceLocation SourceLocation; // Lua 源位置
};

/** 内置注册名；未来工厂可注册更多 Graph、Node 和 Pin 数据类型。 */
namespace SekiroAnimGraphIRNames
{
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName PoseGraph;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName StatePoseGraph;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName StateMachineGraph;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName OutputPoseNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName StateResultNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName SequencePlayerNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName StateMachineNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName InertializationNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName LocalToComponentSpaceNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName ComponentToLocalSpaceNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName OrientationWarpingNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName FootPlacementNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName LegIKNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName TwoBoneIKNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName SaveCachedPoseNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName UseCachedPoseNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName BoolPropertyGetterNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName FloatPropertyGetterNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName BytePropertyGetterNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName EnumPropertyGetterNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName BlendListByBoolNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName BlendListByEnumNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName SlotNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName LayeredBlendPerBoneNode;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName PoseData;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName ComponentPoseData;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName BoolData;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName FloatData;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName ByteData;
    SEKIROANIMBLUEPRINTEXTEDITOR_API extern const FName EnumData;
}
