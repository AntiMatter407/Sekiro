#pragma once

#include "Containers/ArrayView.h"
#include "LuaAnimGraphIR.h"

/** 节点在所属 Graph 中的根节点角色。 */
enum class ELuaAnimIRNodeRootRole : uint8
{
    None,
    GraphRoot,
};

/** 节点对内部 Graph 的所有权策略。 */
enum class ELuaAnimIROwnedGraphPolicy : uint8
{
    Forbidden,
    Required,
};

/** 权威 Pin 契约。 */
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRPinContract
{
    FString Name;                         // 节点内稳定 Pin 名
    ELuaAnimIRPinDirection Direction = ELuaAnimIRPinDirection::Input; // 数据流方向
    FName DataType = NAME_None;           // 注册数据类型名
    bool bAllowMultipleConnections = false; // 是否允许同一端点拥有多个连接
};

/** 权威节点属性契约。 */
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRPropertyContract
{
    FName Name = NAME_None;               // 注册属性名
    ELuaAnimIRValueType ValueType = ELuaAnimIRValueType::Bool; // 属性值类型
    bool bRequired = false;               // Lua IR 是否必须声明该属性
};

/** NodeType 的完整只读契约。 */
struct LUAANIMBLUEPRINTEDITOR_API FLuaAnimIRNodeContract
{
    FName NodeType = NAME_None;           // Lua IR 使用的稳定节点类型名
    FSoftClassPath EditorNodeClassPath;   // 后续 NodeFactory 使用的编辑器节点类路径
    TArray<FName> AllowedGraphTypes;       // 允许容纳该节点的 GraphType
    ELuaAnimIRNodeRootRole RootRole = ELuaAnimIRNodeRootRole::None; // 根节点角色
    ELuaAnimIROwnedGraphPolicy OwnedGraphPolicy = ELuaAnimIROwnedGraphPolicy::Forbidden; // 内部 Graph 策略
    FName OwnedGraphType = NAME_None;      // Required 策略要求的内部 GraphType
    TArray<FLuaAnimIRPinContract> Pins; // 权威 Pin 集合
    TArray<FLuaAnimIRPropertyContract> Properties; // 权威属性集合
    bool bDynamicPins = false;             // Pin 是否由目标函数反射动态生成
};

/** 提供内置动画节点类型的权威只读注册表。 */
class LUAANIMBLUEPRINTEDITOR_API FLuaAnimGraphNodeRegistry
{
public:
    // ── 只读查询 ──────────────────────────────────────────────
    static TConstArrayView<FLuaAnimIRNodeContract> GetContracts();
    static const FLuaAnimIRNodeContract* Find(FName NodeType);
};
