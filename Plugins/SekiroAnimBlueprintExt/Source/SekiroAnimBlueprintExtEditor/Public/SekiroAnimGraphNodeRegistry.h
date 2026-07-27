#pragma once

#include "Containers/ArrayView.h"
#include "SekiroAnimGraphIR.h"

/** 节点在所属 Graph 中的根节点角色。 */
enum class ESekiroAnimIRNodeRootRole : uint8
{
    None,
    GraphRoot,
};

/** 节点对内部 Graph 的所有权策略。 */
enum class ESekiroAnimIROwnedGraphPolicy : uint8
{
    Forbidden,
    Required,
};

/** 权威 Pin 契约。 */
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRPinContract
{
    FString Name;                         // 节点内稳定 Pin 名
    ESekiroAnimIRPinDirection Direction = ESekiroAnimIRPinDirection::Input; // 数据流方向
    FName DataType = NAME_None;           // 注册数据类型名
    bool bAllowMultipleConnections = false; // 是否允许同一端点拥有多个连接
};

/** 权威节点属性契约。 */
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRPropertyContract
{
    FName Name = NAME_None;               // 注册属性名
    ESekiroAnimIRValueType ValueType = ESekiroAnimIRValueType::Bool; // 属性值类型
    bool bRequired = false;               // Lua IR 是否必须声明该属性
};

/** NodeType 的完整只读契约。 */
struct SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimIRNodeContract
{
    FName NodeType = NAME_None;           // Lua IR 使用的稳定节点类型名
    FSoftClassPath EditorNodeClassPath;   // 后续 NodeFactory 使用的编辑器节点类路径
    TArray<FName> AllowedGraphTypes;       // 允许容纳该节点的 GraphType
    ESekiroAnimIRNodeRootRole RootRole = ESekiroAnimIRNodeRootRole::None; // 根节点角色
    ESekiroAnimIROwnedGraphPolicy OwnedGraphPolicy = ESekiroAnimIROwnedGraphPolicy::Forbidden; // 内部 Graph 策略
    FName OwnedGraphType = NAME_None;      // Required 策略要求的内部 GraphType
    TArray<FSekiroAnimIRPinContract> Pins; // 权威 Pin 集合
    TArray<FSekiroAnimIRPropertyContract> Properties; // 权威属性集合
    bool bDynamicPins = false;             // Pin 是否由目标函数反射动态生成
};

/** 提供内置动画节点类型的权威只读注册表。 */
class SEKIROANIMBLUEPRINTEXTEDITOR_API FSekiroAnimGraphNodeRegistry
{
public:
    // ── 只读查询 ──────────────────────────────────────────────
    static TConstArrayView<FSekiroAnimIRNodeContract> GetContracts();
    static const FSekiroAnimIRNodeContract* Find(FName NodeType);
};
