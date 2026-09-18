#pragma once

#include "AlphaBlend.h"
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#include "LuaAnimGraphIR.generated.h"

/** IR 属性值的显式类型标签。 */
UENUM(BlueprintType)
enum class ELuaAnimIRValueType : uint8
{
    Bool,
    Integer,
    Float,
    Name,
    String,
    SoftObjectPath,
    SoftClassPath,
    Enum,
    Struct,
};

/** Pin 在节点上的数据流方向。 */
UENUM(BlueprintType)
enum class ELuaAnimIRPinDirection : uint8
{
    Input,
    Output,
};

/** IR 诊断严重级别。 */
UENUM(BlueprintType)
enum class ELuaAnimIRDiagnosticSeverity : uint8
{
    Info,
    Warning,
    Error,
};

/** IR 最终生成的动画蓝图资产种类。 */
UENUM(BlueprintType)
enum class ELuaAnimIRBlueprintKind : uint8
{
    AnimBlueprint,
    AnimationLayerInterface,
};

/** Lua 源码中的稳定定位信息。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRSourceLocation
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
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ELuaAnimIRValueType Type = ELuaAnimIRValueType::Bool; // 当前有效值类型

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString StructValue;                  // UE 结构体确定性文本值
};

/** 节点的具名类型化属性。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRProperty
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // 注册属性名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRValue Value;              // 显式类型属性值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序
};

/** AnimGraph 节点持有的线程安全函数引用声明。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRNodeFunctionBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName PropertyName = NAME_None;       // UAnimGraphNode 上的 FMemberReference 属性名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName FunctionName = NAME_None;       // 动画蓝图自身或父类中的目标 UFunction 名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString PrototypeFunction;            // UE 用于校验签名的完整原型函数路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 节点内声明顺序
};

/** 节点 Pin 定义。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRPin
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 节点内稳定 Pin 名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ELuaAnimIRPinDirection Direction = ELuaAnimIRPinDirection::Input; // 数据流方向

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName DataType = NAME_None;           // 注册数据类型名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bAllowMultipleConnections = false; // 输入 Pin 是否允许多连接

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序
};

/** Link 的节点与 Pin 端点。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRPinEndpoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString NodeId;                       // 全局稳定节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString PinName;                      // 节点内稳定 Pin 名
};

/** 两个 Pin 之间的有向连接。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRLink
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定 Link ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRPinEndpoint Source;      // 输出端点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRPinEndpoint Target;      // 输入端点

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 与具体 UAnimGraphNode 解耦的节点描述。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定节点 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName NodeType = NAME_None;           // 由后续 NodeFactory 解析的注册名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftClassPath EditorNodeClass;        // 可选的反射节点类；设置后无需注册 NodeType

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString DisplayName;                  // 编辑器显示名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString OwnedGraphId;                 // 节点独占的内部 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRPin> Pins;         // Pin 定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRProperty> Properties; // 类型化属性

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRNodeFunctionBinding> FunctionBindings; // Anim Node Function 绑定

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 过渡编译设置。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRTransitionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    float BlendDuration = 0.2f;           // 混合时长，单位秒

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 PriorityOrder = 0;              // 同源状态过渡优先级

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    EAlphaBlendOption BlendMode = EAlphaBlendOption::Linear; // 引擎原生过渡混合模式
};

/** Transition Gate 的单个扁平 AST 节点。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRTransitionGateNode
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
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRTransitionGate
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 RootIndex = INDEX_NONE;          // Nodes 中的根节点索引

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRTransitionGateNode> Nodes; // 前序生成的 Gate 节点
};

/** 状态机中的状态。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRState
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
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 状态机中的有向过渡。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRTransition
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
    FLuaAnimIRTransitionSettings Settings; // 过渡编译设置

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRTransitionGate Gate;      // 原生 Transition Rule Gate

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 生成到 AnimBlueprint GeneratedClass 的成员变量。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRVariable
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // GeneratedClass 成员名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName DataType = NAME_None;           // Bool/Float/Byte/Enum

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath TypeObjectPath;        // Enum 时指向 UEnum，其余类型为空

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRValue DefaultValue;       // 与 DataType 对应的默认值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bTransient = true;               // 是否标记为 Blueprint Transient

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** Graph 内嵌的状态机定义。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRStateMachine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString EntryStateId;                 // Entry 指向的状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRState> States;     // 状态定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRTransition> Transitions; // 过渡定义
};

/** Graph 中未显式放置元素的编辑器自动排版风格。 */
UENUM(BlueprintType)
enum class ELuaAnimIRLayoutStyle : uint8
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
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRLayoutItem
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
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRLayoutGrid
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
    ELuaAnimIRLayoutStyle LayoutStyle = ELuaAnimIRLayoutStyle::Auto; // 分区风格覆盖

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRLayoutItem> Items; // 显式放置元素
};

/** Graph 元素在 UE 画布上的精确像素坐标。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRLayoutPosition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString ElementId;                    // 当前 Graph 内节点或状态 ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 X = 0;                          // UE Graph 画布横坐标

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 Y = 0;                          // UE Graph 画布纵坐标
};

/** Graph 级编辑器布局元数据。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRGraphLayout
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ELuaAnimIRLayoutStyle Style = ELuaAnimIRLayoutStyle::Auto; // 未显式放置元素的自动风格

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRLayoutGrid> Grids; // 显式布局分区

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRLayoutPosition> Positions; // 优先于 Grid 和自动布局的精确像素坐标
};

/** 单个动画 Graph。GraphType 是注册名，不绑定 UAnimGraphNode 类型。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRGraph
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
    TArray<FLuaAnimIRNode> Nodes;       // 节点定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRLink> Links;       // Pin 连接

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRStateMachine StateMachine; // StateMachine Graph 数据

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRGraphLayout Layout;       // 仅供编辑器生成节点坐标

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** Animation Layer 函数的单个输入参数；输出始终是本地空间 Pose。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRFunctionParameter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName Name = NAME_None;               // 函数参数名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName DataType = NAME_None;           // 注册数据类型名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath TypeObjectPath;        // Object/Class/Enum 参数的类型对象路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bIsPose = false;                 // 是否为 Pose 输入参数

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** AnimBlueprint 动画层。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Id;                           // 全局稳定 Layer ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString Name;                         // 动画层名称

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FName FunctionName = NAME_None;       // 原生 Animation Layer 函数名；为空时使用 Name

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftClassPath InterfaceClass;        // Override 所属 Animation Layer Interface

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    bool bOverride = false;               // 是否覆盖接口或父类已有 Layer

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRFunctionParameter> Parameters; // Layer 输入签名

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString RootGraphId;                  // 动画层入口 Graph ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRGraph> Graphs;     // 本层 Graph

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 DeclarationOrder = 0;           // 源码声明顺序

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // 声明位置
};

/** 一份可 Canonicalize 和验证的完整 AnimBlueprint IR。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimBlueprintIR
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    int32 SchemaVersion = 4;              // IR Schema 版本

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FString SourceModule;                 // 生成 IR 的 Lua 模块

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    ELuaAnimIRBlueprintKind BlueprintKind = ELuaAnimIRBlueprintKind::AnimBlueprint; // 目标资产种类

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftClassPath ParentAnimInstanceClass; // 父 AnimInstance 类软路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FSoftObjectPath TargetSkeleton;        // 目标 Skeleton 资产软路径

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRProperty> InheritedDefaults; // 显式覆盖的父类 UPROPERTY 默认值

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRVariable> Variables; // GeneratedClass 成员变量

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FSoftClassPath> ImplementedInterfaces; // 实现的 Animation Layer Interface 类

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    TArray<FLuaAnimIRLayer> Layers;     // 动画层定义

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // Blueprint 声明位置
};

/** Validator 返回的稳定结构化诊断。 */
USTRUCT(BlueprintType)
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRDiagnostic
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FName Code = NAME_None;               // 稳定错误代码

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    ELuaAnimIRDiagnosticSeverity Severity = ELuaAnimIRDiagnosticSeverity::Error; // 严重级别

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FString Message;                      // 人类可读消息

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FString SubjectId;                    // 关联实体稳定 ID

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim Graph IR")
    FLuaAnimIRSourceLocation SourceLocation; // Lua 源位置
};

/** 内置注册名；未来工厂可注册更多 Graph、Node 和 Pin 数据类型。 */
namespace LuaAnimGraphIRNames
{
    LUAANIMBLUEPRINTEDITOR_API extern const FName PoseGraph;
    LUAANIMBLUEPRINTEDITOR_API extern const FName StatePoseGraph;
    LUAANIMBLUEPRINTEDITOR_API extern const FName StateMachineGraph;
    LUAANIMBLUEPRINTEDITOR_API extern const FName OutputPoseNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName StateResultNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName SequencePlayerNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName StateMachineNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName InertializationNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LocalToComponentSpaceNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName ComponentToLocalSpaceNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName OrientationWarpingNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName FootPlacementNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LegIKNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName TwoBoneIKNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName SaveCachedPoseNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName UseCachedPoseNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName BoolPropertyGetterNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName FloatPropertyGetterNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName BytePropertyGetterNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName EnumPropertyGetterNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName BlendListByBoolNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName BlendListByEnumNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName SlotNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LayeredBlendPerBoneNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LinkedAnimLayerNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LinkedAnimGraphNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName LinkedInputPoseNode;
    LUAANIMBLUEPRINTEDITOR_API extern const FName PoseData;
    LUAANIMBLUEPRINTEDITOR_API extern const FName ComponentPoseData;
    LUAANIMBLUEPRINTEDITOR_API extern const FName BoolData;
    LUAANIMBLUEPRINTEDITOR_API extern const FName FloatData;
    LUAANIMBLUEPRINTEDITOR_API extern const FName ByteData;
    LUAANIMBLUEPRINTEDITOR_API extern const FName EnumData;
}
