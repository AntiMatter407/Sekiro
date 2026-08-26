#include "SekiroBehaviorTreeIRLibrary.h"

#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "Editor.h"
#include "LuaEnv.h"
#include "LuaValue.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UnLuaLegacy.h"
#include "UnLuaModule.h"
#include "lua.hpp"

namespace SekiroBehaviorTreeIR
{
    const FName InvalidInput(TEXT("BT.IR.InvalidInput"));
    const FName InvalidLuaType(TEXT("BT.IR.InvalidLuaType"));
    const FName InvalidValueType(TEXT("BT.IR.InvalidValueType"));
    const FName DuplicateId(TEXT("BT.IR.DuplicateId"));
    const FName InvalidClass(TEXT("BT.IR.InvalidClass"));
    const FName InvalidAttachment(TEXT("BT.IR.InvalidAttachment"));
    const FName GraphCycle(TEXT("BT.IR.GraphCycle"));
    const FName MissingRoot(TEXT("BT.IR.MissingRoot"));

    struct FParseContext
    {
        lua_State* State = nullptr;                                     // 当前 UnLua 主状态
        FString ModuleName;                                             // 当前 Lua 模块名
        TArray<FSekiroBehaviorTreeDiagnostic>* Diagnostics = nullptr;   // 诊断接收数组
    };

    /**
     * 追加带 Lua 定位的中文错误，不抛异常也不修改 Lua 栈。
     *
     * @param Context 当前解析上下文，Diagnostics 必须有效。
     * @param Code 稳定机器错误码。
     * @param Message 面向作者的中文说明。
     * @param Path 出错字段的 IR 路径。
     * @param Location Lua 模块及行列。
     */
    void AddError(
        FParseContext& Context,
        const FName Code,
        const FString& Message,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location)
    {
        FSekiroBehaviorTreeDiagnostic& Diagnostic = Context.Diagnostics->AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Message = Message;
        Diagnostic.Path = Path;
        Diagnostic.SourceLocation = Location;
    }

    /**
     * 从 table 读取必填字符串，严格拒绝 nil 与隐式数字转换。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Lua table 栈索引。
     * @param FieldName 字段名。
     * @param Path 诊断路径。
     * @param Location 诊断源码位置。
     * @param OutValue 成功时接收 UTF-8 转换后的字符串。
     * @return 字段存在且类型严格为 string 时返回 true。
     */
    bool ReadString(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location,
        FString& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        if (lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName) != LUA_TSTRING)
        {
            AddError(Context, InvalidLuaType, TEXT("字段必须是字符串。"), Path, Location);
            return false;
        }
        OutValue = UTF8_TO_TCHAR(lua_tostring(Context.State, -1));
        return true;
    }

    /**
     * 从 table 读取必填整数，拒绝小数和超出 int32 范围的值。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Lua table 栈索引。
     * @param FieldName 字段名。
     * @param Path 诊断路径。
     * @param Location 诊断源码位置。
     * @param OutValue 成功时接收 int32 值。
     * @return 字段为 Lua integer 且可表示为 int32 时返回 true。
     */
    bool ReadInteger(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location,
        int32& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        if (lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName) != LUA_TNUMBER
            || !lua_isinteger(Context.State, -1))
        {
            AddError(Context, InvalidLuaType, TEXT("字段必须是整数。"), Path, Location);
            return false;
        }
        const lua_Integer Value = lua_tointeger(Context.State, -1);
        if (Value < MIN_int32 || Value > MAX_int32)
        {
            AddError(Context, InvalidLuaType, TEXT("整数超出 int32 范围。"), Path, Location);
            return false;
        }
        OutValue = static_cast<int32>(Value);
        return true;
    }

    /**
     * 读取可选布尔字段，缺失时保留调用方默认值。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Lua table 栈索引。
     * @param FieldName 字段名。
     * @param Path 诊断路径。
     * @param Location 诊断源码位置。
     * @param OutValue 成功时接收布尔值。
     * @return 字段缺失或严格为 boolean 时返回 true。
     */
    bool ReadOptionalBool(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location,
        bool& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL) return true;
        if (Type != LUA_TBOOLEAN)
        {
            AddError(Context, InvalidLuaType, TEXT("字段必须是布尔值。"), Path, Location);
            return false;
        }
        OutValue = lua_toboolean(Context.State, -1) != 0;
        return true;
    }

    /**
     * 解析可选 SourceLocation；模块名缺失时使用当前 require 模块，行列缺失时为零。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 所属对象 table 栈索引。
     * @param OutLocation 接收规范化定位。
     * @return 字段缺失或内容合法时返回 true。
     */
    bool ParseLocation(
        FParseContext& Context,
        const int32 TableIndex,
        FSekiroBehaviorTreeSourceLocation& OutLocation)
    {
        OutLocation.LuaModule = Context.ModuleName;
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "SourceLocation");
        if (Type == LUA_TNIL) return true;
        if (Type != LUA_TTABLE)
        {
            AddError(Context, InvalidLuaType, TEXT("SourceLocation 必须是 table。"), TEXT("SourceLocation"), OutLocation);
            return false;
        }
        const int32 LocationIndex = lua_absindex(Context.State, -1);
        return ReadString(Context, LocationIndex, "LuaModule", TEXT("SourceLocation.LuaModule"), OutLocation, OutLocation.LuaModule)
            && ReadInteger(Context, LocationIndex, "Line", TEXT("SourceLocation.Line"), OutLocation, OutLocation.Line)
            && ReadInteger(Context, LocationIndex, "Column", TEXT("SourceLocation.Column"), OutLocation, OutLocation.Column);
    }

    /**
     * 递归解析带 Type 标签的 Lua 值到扁平值池，所有子索引只指向先前或随后成功创建的池项。
     * 本函数严格依赖显式标签，不根据 Lua number/string 猜测目标 FProperty。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 强类型值 table 栈索引。
     * @param Path 完整诊断路径。
     * @param Location 所属属性源码位置。
     * @param OutIR 接收值池。
     * @param OutIndex 成功时接收根值索引。
     * @return 整个递归值结构合法时返回 true。
     */
    bool ParseValue(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location,
        FSekiroBehaviorTreeIR& OutIR,
        int32& OutIndex)
    {
        FString TypeName;
        if (!ReadString(Context, TableIndex, "Type", Path + TEXT(".Type"), Location, TypeName)) return false;

        FSekiroBehaviorTreeIRValue Value;
        if (TypeName == TEXT("Bool")) Value.Type = ESekiroBehaviorTreeValueType::Bool;
        else if (TypeName == TEXT("Integer")) Value.Type = ESekiroBehaviorTreeValueType::Integer;
        else if (TypeName == TEXT("Float")) Value.Type = ESekiroBehaviorTreeValueType::Float;
        else if (TypeName == TEXT("String")) Value.Type = ESekiroBehaviorTreeValueType::String;
        else if (TypeName == TEXT("Name")) Value.Type = ESekiroBehaviorTreeValueType::Name;
        else if (TypeName == TEXT("Text")) Value.Type = ESekiroBehaviorTreeValueType::Text;
        else if (TypeName == TEXT("Enum")) Value.Type = ESekiroBehaviorTreeValueType::Enum;
        else if (TypeName == TEXT("Object")) Value.Type = ESekiroBehaviorTreeValueType::Object;
        else if (TypeName == TEXT("SoftObject")) Value.Type = ESekiroBehaviorTreeValueType::SoftObject;
        else if (TypeName == TEXT("Class")) Value.Type = ESekiroBehaviorTreeValueType::Class;
        else if (TypeName == TEXT("SoftClass")) Value.Type = ESekiroBehaviorTreeValueType::SoftClass;
        else if (TypeName == TEXT("Struct")) Value.Type = ESekiroBehaviorTreeValueType::Struct;
        else if (TypeName == TEXT("Array")) Value.Type = ESekiroBehaviorTreeValueType::Array;
        else
        {
            AddError(Context, InvalidValueType, FString::Printf(TEXT("未知强类型值标签：%s。"), *TypeName), Path, Location);
            return false;
        }

        OutIndex = OutIR.Values.Add(Value);
        FSekiroBehaviorTreeIRValue& StoredValue = OutIR.Values[OutIndex];
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 RawType = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "Value");

        if (StoredValue.Type == ESekiroBehaviorTreeValueType::Bool)
        {
            if (RawType != LUA_TBOOLEAN)
            {
                AddError(Context, InvalidLuaType, TEXT("Bool.Value 必须是 boolean。"), Path, Location);
                return false;
            }
            StoredValue.BoolValue = lua_toboolean(Context.State, -1) != 0;
            return true;
        }
        if (StoredValue.Type == ESekiroBehaviorTreeValueType::Integer)
        {
            if (RawType != LUA_TNUMBER || !lua_isinteger(Context.State, -1))
            {
                AddError(Context, InvalidLuaType, TEXT("Integer.Value 必须是整数。"), Path, Location);
                return false;
            }
            StoredValue.IntegerValue = static_cast<int64>(lua_tointeger(Context.State, -1));
            return true;
        }
        if (StoredValue.Type == ESekiroBehaviorTreeValueType::Float)
        {
            if (RawType != LUA_TNUMBER)
            {
                AddError(Context, InvalidLuaType, TEXT("Float.Value 必须是数值。"), Path, Location);
                return false;
            }
            StoredValue.FloatValue = static_cast<double>(lua_tonumber(Context.State, -1));
            return true;
        }
        if (StoredValue.Type != ESekiroBehaviorTreeValueType::Struct
            && StoredValue.Type != ESekiroBehaviorTreeValueType::Array)
        {
            if (RawType != LUA_TSTRING)
            {
                AddError(Context, InvalidLuaType, TEXT("字符串类强类型值的 Value 必须是 string。"), Path, Location);
                return false;
            }
            StoredValue.StringValue = UTF8_TO_TCHAR(lua_tostring(Context.State, -1));
            return true;
        }
        if (RawType != LUA_TTABLE)
        {
            AddError(Context, InvalidLuaType, TEXT("Struct/Array.Value 必须是 table。"), Path, Location);
            return false;
        }

        const int32 ValueTableIndex = lua_absindex(Context.State, -1);
        if (StoredValue.Type == ESekiroBehaviorTreeValueType::Array)
        {
            const int32 Count = static_cast<int32>(lua_rawlen(Context.State, ValueTableIndex));
            for (int32 ItemIndex = 1; ItemIndex <= Count; ++ItemIndex)
            {
                lua_rawgeti(Context.State, ValueTableIndex, ItemIndex);
                if (lua_type(Context.State, -1) != LUA_TTABLE)
                {
                    AddError(Context, InvalidLuaType, TEXT("Array 元素必须是强类型值 table。"), Path, Location);
                    return false;
                }
                int32 ChildValueIndex = INDEX_NONE;
                if (!ParseValue(Context, lua_absindex(Context.State, -1), FString::Printf(TEXT("%s[%d]"), *Path, ItemIndex), Location, OutIR, ChildValueIndex))
                    return false;
                OutIR.Values[OutIndex].ArrayItems.Add(ChildValueIndex);
                lua_pop(Context.State, 1);
            }
            return true;
        }

        lua_pushnil(Context.State);
        while (lua_next(Context.State, ValueTableIndex) != 0)
        {
            if (lua_type(Context.State, -2) != LUA_TSTRING || lua_type(Context.State, -1) != LUA_TTABLE)
            {
                AddError(Context, InvalidLuaType, TEXT("Struct 字段名必须是 string，字段值必须是强类型值 table。"), Path, Location);
                return false;
            }
            const FString FieldName = UTF8_TO_TCHAR(lua_tostring(Context.State, -2));
            int32 ChildValueIndex = INDEX_NONE;
            if (!ParseValue(Context, lua_absindex(Context.State, -1), Path + TEXT(".") + FieldName, Location, OutIR, ChildValueIndex))
                return false;
            OutIR.Values[OutIndex].StructFields.Add(FName(*FieldName), ChildValueIndex);
            lua_pop(Context.State, 1);
        }
        return true;
    }

    /**
     * 解析 Properties 数组，每个元素必须包含 Name、DeclarationOrder 与显式类型 Value。
     *
     * @param Context 当前解析上下文。
     * @param OwnerTableIndex 节点或 Key table 栈索引。
     * @param Path 所属对象路径。
     * @param Location 所属对象源码位置。
     * @param OutIR 接收共享值池。
     * @param OutProperties 接收属性数组。
     * @return 数组结构与所有值合法时返回 true。
     */
    bool ParseProperties(
        FParseContext& Context,
        const int32 OwnerTableIndex,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location,
        FSekiroBehaviorTreeIR& OutIR,
        TArray<FSekiroBehaviorTreeIRProperty>& OutProperties)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, OwnerTableIndex), "Properties");
        if (Type != LUA_TTABLE)
        {
            AddError(Context, InvalidLuaType, TEXT("Properties 必须是数组 table。"), Path + TEXT(".Properties"), Location);
            return false;
        }
        const int32 PropertiesIndex = lua_absindex(Context.State, -1);
        const int32 Count = static_cast<int32>(lua_rawlen(Context.State, PropertiesIndex));
        for (int32 PropertyIndex = 1; PropertyIndex <= Count; ++PropertyIndex)
        {
            lua_rawgeti(Context.State, PropertiesIndex, PropertyIndex);
            if (lua_type(Context.State, -1) != LUA_TTABLE)
            {
                AddError(Context, InvalidLuaType, TEXT("Properties 元素必须是 table。"), Path, Location);
                return false;
            }
            const int32 PropertyTableIndex = lua_absindex(Context.State, -1);
            FSekiroBehaviorTreeIRProperty& Property = OutProperties.AddDefaulted_GetRef();
            FString PropertyName;
            if (!ReadString(Context, PropertyTableIndex, "Name", Path + TEXT(".Properties.Name"), Location, PropertyName)
                || !ReadInteger(Context, PropertyTableIndex, "DeclarationOrder", Path + TEXT(".Properties.DeclarationOrder"), Location, Property.DeclarationOrder))
            {
                return false;
            }
            Property.Name = FName(*PropertyName);
            if (lua_getfield(Context.State, PropertyTableIndex, "Value") != LUA_TTABLE)
            {
                AddError(Context, InvalidLuaType, TEXT("Property.Value 必须是强类型值 table。"), Path, Location);
                return false;
            }
            if (!ParseValue(Context, lua_absindex(Context.State, -1), Path + TEXT(".Properties.") + PropertyName, Location, OutIR, Property.ValueIndex))
                return false;
            lua_pop(Context.State, 2);
        }
        return true;
    }

    /**
     * 解析一种 Node 数组；角色不由 Lua 指定，后续只按 UClass 继承关系判定。
     *
     * @param Context 当前解析上下文。
     * @param RootTableIndex IR 根 table 栈索引。
     * @param FieldName 数组字段名。
     * @param OutIR 接收共享值池。
     * @param OutNodes 接收节点数组。
     * @return 所有节点必填字段及属性合法时返回 true。
     */
    bool ParseNodes(
        FParseContext& Context,
        const int32 RootTableIndex,
        const char* FieldName,
        FSekiroBehaviorTreeIR& OutIR,
        TArray<FSekiroBehaviorTreeIRNode>& OutNodes)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        if (lua_getfield(Context.State, lua_absindex(Context.State, RootTableIndex), FieldName) != LUA_TTABLE)
        {
            AddError(Context, InvalidLuaType, TEXT("节点集合必须是数组 table。"), UTF8_TO_TCHAR(FieldName), FSekiroBehaviorTreeSourceLocation());
            return false;
        }
        const int32 ArrayIndex = lua_absindex(Context.State, -1);
        const int32 Count = static_cast<int32>(lua_rawlen(Context.State, ArrayIndex));
        for (int32 NodeIndex = 1; NodeIndex <= Count; ++NodeIndex)
        {
            lua_rawgeti(Context.State, ArrayIndex, NodeIndex);
            if (lua_type(Context.State, -1) != LUA_TTABLE)
            {
                AddError(Context, InvalidLuaType, TEXT("节点数组元素必须是 table。"), UTF8_TO_TCHAR(FieldName), FSekiroBehaviorTreeSourceLocation());
                return false;
            }
            const int32 NodeTableIndex = lua_absindex(Context.State, -1);
            FSekiroBehaviorTreeIRNode& Node = OutNodes.AddDefaulted_GetRef();
            if (!ParseLocation(Context, NodeTableIndex, Node.SourceLocation)) return false;
            FString ClassPath;
            if (!ReadString(Context, NodeTableIndex, "Id", TEXT("Node.Id"), Node.SourceLocation, Node.Id)
                || !ReadString(Context, NodeTableIndex, "ParentId", TEXT("Node.ParentId"), Node.SourceLocation, Node.ParentId)
                || !ReadString(Context, NodeTableIndex, "ClassPath", TEXT("Node.ClassPath"), Node.SourceLocation, ClassPath)
                || !ReadString(Context, NodeTableIndex, "DisplayName", TEXT("Node.DisplayName"), Node.SourceLocation, Node.DisplayName)
                || !ReadInteger(Context, NodeTableIndex, "DeclarationOrder", TEXT("Node.DeclarationOrder"), Node.SourceLocation, Node.DeclarationOrder)
                || !ParseProperties(Context, NodeTableIndex, Node.Id, Node.SourceLocation, OutIR, Node.Properties))
            {
                return false;
            }
            Node.ClassPath = FSoftClassPath(ClassPath);
            lua_pop(Context.State, 1);
        }
        return true;
    }

    /**
     * 解析 BlackboardKeys 数组及每个 KeyType 的反射属性。
     *
     * @param Context 当前解析上下文。
     * @param RootTableIndex IR 根 table 栈索引。
     * @param OutIR 接收完整 IR。
     * @return 数组和全部 Key 合法时返回 true。
     */
    bool ParseBlackboardKeys(
        FParseContext& Context,
        const int32 RootTableIndex,
        FSekiroBehaviorTreeIR& OutIR)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        if (lua_getfield(Context.State, lua_absindex(Context.State, RootTableIndex), "BlackboardKeys") != LUA_TTABLE)
        {
            AddError(Context, InvalidLuaType, TEXT("BlackboardKeys 必须是数组 table。"), TEXT("BlackboardKeys"), FSekiroBehaviorTreeSourceLocation());
            return false;
        }
        const int32 ArrayIndex = lua_absindex(Context.State, -1);
        const int32 Count = static_cast<int32>(lua_rawlen(Context.State, ArrayIndex));
        for (int32 KeyIndex = 1; KeyIndex <= Count; ++KeyIndex)
        {
            lua_rawgeti(Context.State, ArrayIndex, KeyIndex);
            if (lua_type(Context.State, -1) != LUA_TTABLE)
            {
                AddError(Context, InvalidLuaType, TEXT("BlackboardKeys 元素必须是 table。"), TEXT("BlackboardKeys"), FSekiroBehaviorTreeSourceLocation());
                return false;
            }
            const int32 KeyTableIndex = lua_absindex(Context.State, -1);
            FSekiroBlackboardIRKey& Key = OutIR.BlackboardKeys.AddDefaulted_GetRef();
            if (!ParseLocation(Context, KeyTableIndex, Key.SourceLocation)) return false;
            FString KeyName;
            FString ClassPath;
            if (!ReadString(Context, KeyTableIndex, "Id", TEXT("BlackboardKey.Id"), Key.SourceLocation, Key.Id)
                || !ReadString(Context, KeyTableIndex, "Name", TEXT("BlackboardKey.Name"), Key.SourceLocation, KeyName)
                || !ReadString(Context, KeyTableIndex, "ClassPath", TEXT("BlackboardKey.ClassPath"), Key.SourceLocation, ClassPath)
                || !ReadInteger(Context, KeyTableIndex, "DeclarationOrder", TEXT("BlackboardKey.DeclarationOrder"), Key.SourceLocation, Key.DeclarationOrder)
                || !ReadOptionalBool(Context, KeyTableIndex, "bInstanceSynced", TEXT("BlackboardKey.bInstanceSynced"), Key.SourceLocation, Key.bInstanceSynced)
                || !ParseProperties(Context, KeyTableIndex, Key.Id, Key.SourceLocation, OutIR, Key.Properties))
            {
                return false;
            }
            Key.Name = FName(*KeyName);
            Key.ClassPath = FSoftClassPath(ClassPath);
            lua_pop(Context.State, 1);
        }
        return true;
    }

    /**
     * 将 CompileIR 返回 table 转为值语义 IR，不访问资产也不执行反射写入。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex IR 根 table 栈索引。
     * @param OutIR 接收解析结果。
     * @return 根字段与所有集合均严格合法时返回 true。
     */
    bool ParseIR(FParseContext& Context, const int32 TableIndex, FSekiroBehaviorTreeIR& OutIR)
    {
        FString ParentBlackboard;
        return ReadInteger(Context, TableIndex, "SchemaVersion", TEXT("IR.SchemaVersion"), FSekiroBehaviorTreeSourceLocation(), OutIR.SchemaVersion)
            && ReadString(Context, TableIndex, "SourceModule", TEXT("IR.SourceModule"), FSekiroBehaviorTreeSourceLocation(), OutIR.SourceModule)
            && ReadString(Context, TableIndex, "RootNodeId", TEXT("IR.RootNodeId"), FSekiroBehaviorTreeSourceLocation(), OutIR.RootNodeId)
            && ReadString(Context, TableIndex, "ParentBlackboard", TEXT("IR.ParentBlackboard"), FSekiroBehaviorTreeSourceLocation(), ParentBlackboard)
            && ParseNodes(Context, TableIndex, "Nodes", OutIR, OutIR.Nodes)
            && ParseNodes(Context, TableIndex, "Decorators", OutIR, OutIR.Decorators)
            && ParseNodes(Context, TableIndex, "Services", OutIR, OutIR.Services)
            && ParseBlackboardKeys(Context, TableIndex, OutIR)
            && (OutIR.ParentBlackboard = FSoftObjectPath(ParentBlackboard), true);
    }

    /**
     * 为结构校验追加中文错误。
     *
     * @param Diagnostics 诊断接收数组。
     * @param Code 稳定错误码。
     * @param Message 中文说明。
     * @param Path 相关 IR 路径或 ID。
     * @param Location 对应源码定位。
     */
    void AddValidationError(
        TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics,
        const FName Code,
        const FString& Message,
        const FString& Path,
        const FSekiroBehaviorTreeSourceLocation& Location)
    {
        FSekiroBehaviorTreeDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Message = Message;
        Diagnostic.Path = Path;
        Diagnostic.SourceLocation = Location;
    }

    /**
     * 验证一组属性的根值池索引，并把所有无效引用追加为诊断。
     *
     * @param IR 提供值池的只读 IR。
     * @param Properties 待检查属性。
     * @param OwnerId 属性所属节点或 Key ID。
     * @param Location 所属源码定位。
     * @param Diagnostics 诊断接收数组。
     * @return 所有根索引有效时返回 true。
     */
    bool ValidatePropertyIndices(
        const FSekiroBehaviorTreeIR& IR,
        const TArray<FSekiroBehaviorTreeIRProperty>& Properties,
        const FString& OwnerId,
        const FSekiroBehaviorTreeSourceLocation& Location,
        TArray<FSekiroBehaviorTreeDiagnostic>& Diagnostics)
    {
        bool bValid = true;
        for (const FSekiroBehaviorTreeIRProperty& Property : Properties)
        {
            if (!IR.Values.IsValidIndex(Property.ValueIndex))
            {
                AddValidationError(Diagnostics, InvalidInput, TEXT("属性引用了无效值池索引。"), OwnerId, Location);
                bValid = false;
            }
        }
        return bValid;
    }

    /**
     * 对主节点父链执行深度优先环检测。
     *
     * @param NodeId 当前节点 ID。
     * @param ParentById 节点到父 ID 的映射。
     * @param Visiting 当前递归栈。
     * @param Visited 已完成集合。
     * @return 发现环时返回 true。
     */
    bool HasParentCycle(
        const FString& NodeId,
        const TMap<FString, FString>& ParentById,
        TSet<FString>& Visiting,
        TSet<FString>& Visited)
    {
        if (Visiting.Contains(NodeId)) return true;
        if (Visited.Contains(NodeId)) return false;
        Visiting.Add(NodeId);
        const FString* ParentId = ParentById.Find(NodeId);
        if (ParentId && !ParentId->IsEmpty() && HasParentCycle(*ParentId, ParentById, Visiting, Visited)) return true;
        Visiting.Remove(NodeId);
        Visited.Add(NodeId);
        return false;
    }

    /**
     * 仅从当前 UnLua 环境的 package.loaded 清除一个精确模块键。
     * 本函数不执行 require、不触碰其他模块，也不创建或修改资产；只能在游戏线程且非 PIE/SIE 调用。
     *
     * @param LuaModuleName 要清除缓存的完整 require 模块名。
     * @param OutDiagnostics 接收环境或输入错误；调用方负责预先清空。
     * @return 精确缓存键已设为 nil 时返回 true，环境不可用或处于 PIE/SIE 时返回 false。
     */
    bool InvalidateLuaModuleCache(
        const FString& LuaModuleName,
        TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
    {
        FParseContext Context;
        Context.ModuleName = LuaModuleName;
        Context.Diagnostics = &OutDiagnostics;
        FSekiroBehaviorTreeSourceLocation Location;
        Location.LuaModule = LuaModuleName;
        if (LuaModuleName.IsEmpty()
            || !IsInGameThread()
            || (GEditor
                && (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)))
        {
            AddError(
                Context,
                InvalidInput,
                TEXT("刷新 Lua 模块只能在非 PIE/SIE 的编辑器游戏线程执行。"),
                LuaModuleName,
                Location);
            return false;
        }

        IUnLuaModule* UnLuaModule =
            FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
        if (!UnLuaModule)
        {
            AddError(
                Context,
                InvalidInput,
                TEXT("无法加载 UnLua 模块。"),
                LuaModuleName,
                Location);
            return false;
        }
        if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
        UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv();
        if (!Environment)
        {
            AddError(
                Context,
                InvalidInput,
                TEXT("UnLua 环境不可用。"),
                LuaModuleName,
                Location);
            return false;
        }

        lua_State* State = Environment->GetMainState();
        const int32 InitialTop = lua_gettop(State);
        ON_SCOPE_EXIT { lua_settop(State, InitialTop); };
        if (lua_getglobal(State, "package") != LUA_TTABLE
            || lua_getfield(State, -1, "loaded") != LUA_TTABLE)
        {
            AddError(
                Context,
                InvalidInput,
                TEXT("Lua package.loaded 表不可用。"),
                LuaModuleName,
                Location);
            return false;
        }
        lua_pushnil(State);
        lua_setfield(State, -2, TCHAR_TO_UTF8(*LuaModuleName));
        return true;
    }
}

/**
 * 通过当前编辑器 UnLua Env require 模块并调用无参 CompileIR，将显式标签 Lua table 导入强类型 IR。
 * 只允许游戏线程调用；本函数不创建、覆盖或保存资产，失败时仅返回诊断。
 *
 * @param LuaModuleName 交给 Lua require 的模块名，不能为空。
 * @param OutIR 接收值语义 IR；函数开始时重置。
 * @param OutDiagnostics 接收结构化中文诊断；函数开始时清空。
 * @return require、CompileIR、严格导入和结构校验全部成功时返回 true。
 */
bool USekiroBehaviorTreeIRLibrary::CompileLuaModule(
    const FString& LuaModuleName,
    FSekiroBehaviorTreeIR& OutIR,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace SekiroBehaviorTreeIR;

    OutIR = FSekiroBehaviorTreeIR();
    OutDiagnostics.Reset();
    FParseContext Context;
    Context.ModuleName = LuaModuleName;
    Context.Diagnostics = &OutDiagnostics;
    FSekiroBehaviorTreeSourceLocation Location;
    Location.LuaModule = LuaModuleName;

    if (LuaModuleName.IsEmpty() || !IsInGameThread())
    {
        AddError(Context, InvalidInput, TEXT("Lua 模块名不能为空，且编译入口只能在游戏线程调用。"), LuaModuleName, Location);
        return false;
    }
    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (!UnLuaModule)
    {
        AddError(Context, InvalidInput, TEXT("无法加载 UnLua 模块。"), LuaModuleName, Location);
        return false;
    }
    if (!UnLuaModule->IsActive()) UnLuaModule->SetActive(true);
    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv();
    if (!Environment)
    {
        AddError(Context, InvalidInput, TEXT("UnLua 环境不可用。"), LuaModuleName, Location);
        return false;
    }

    Context.State = Environment->GetMainState();
    UnLua::FLuaRetValues RequiredValues = UnLua::Call(Context.State, "require", TCHAR_TO_UTF8(*LuaModuleName));
    if (!RequiredValues.IsValid() || RequiredValues.Num() < 1 || RequiredValues[0].GetType() != LUA_TTABLE)
    {
        AddError(Context, InvalidInput, TEXT("Lua 模块加载失败或未返回 table。"), LuaModuleName, Location);
        return false;
    }
    UnLua::FLuaTable ModuleTable(Environment, RequiredValues[0]);
    UnLua::FLuaRetValues CompiledValues = ModuleTable.Call("CompileIR");
    if (!CompiledValues.IsValid() || CompiledValues.Num() < 1 || CompiledValues[0].GetType() != LUA_TTABLE)
    {
        AddError(Context, InvalidInput, TEXT("模块必须提供 CompileIR() 并返回 table。"), LuaModuleName, Location);
        return false;
    }
    if (!ParseIR(Context, CompiledValues[0].GetIndex(), OutIR)) return false;
    return Validate(OutIR, OutDiagnostics);
}

/**
 * 清除目标模块的 require 缓存后重新导入强类型 IR，保证显式文件同步验证读取刚写入内容。
 * 只允许非 PIE/SIE 的编辑器游戏线程调用；仅清除精确 package.loaded 键，不影响其他 Lua 模块。
 *
 * @param LuaModuleName 需要强制重新加载的完整 require 模块名。
 * @param OutIR 接收新文件编译出的值语义 IR；函数开始时重置。
 * @param OutDiagnostics 接收缓存刷新、导入和结构校验诊断；函数开始时清空。
 * @return 缓存刷新和完整 CompileLuaModule 均成功时返回 true。
 */
bool USekiroBehaviorTreeIRLibrary::CompileLuaModuleFresh(
    const FString& LuaModuleName,
    FSekiroBehaviorTreeIR& OutIR,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    OutIR = FSekiroBehaviorTreeIR();
    OutDiagnostics.Reset();
    if (!SekiroBehaviorTreeIR::InvalidateLuaModuleCache(
            LuaModuleName,
            OutDiagnostics))
    {
        return false;
    }
    return CompileLuaModule(LuaModuleName, OutIR, OutDiagnostics);
}

/**
 * 校验稳定 ID、节点继承关系、父子类别、值池索引、根节点和父链无环。
 * 会同步加载 ClassPath 以验证继承关系，但不创建 UObject、不修改资产；只能在游戏线程调用。
 *
 * @param IR 待校验的只读 IR。
 * @param OutDiagnostics 追加校验诊断；调用方可预先包含导入诊断。
 * @return 没有发现错误时返回 true。
 */
bool USekiroBehaviorTreeIRLibrary::Validate(
    const FSekiroBehaviorTreeIR& IR,
    TArray<FSekiroBehaviorTreeDiagnostic>& OutDiagnostics)
{
    using namespace SekiroBehaviorTreeIR;

    bool bValid = true;
    TSet<FString> AllIds;
    TMap<FString, const FSekiroBehaviorTreeIRNode*> MainNodes;
    TMap<FString, FString> ParentById;
    if (IR.SchemaVersion != 1)
    {
        AddValidationError(OutDiagnostics, InvalidInput, TEXT("当前仅支持 SchemaVersion=1。"), TEXT("SchemaVersion"), FSekiroBehaviorTreeSourceLocation());
        bValid = false;
    }
    if (!IR.ParentBlackboard.IsNull() && !Cast<UBlackboardData>(IR.ParentBlackboard.TryLoad()))
    {
        AddValidationError(OutDiagnostics, InvalidClass, TEXT("ParentBlackboard 无法加载为 UBlackboardData。"), IR.ParentBlackboard.ToString(), FSekiroBehaviorTreeSourceLocation());
        bValid = false;
    }

    for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
    {
        if (Node.Id.IsEmpty() || AllIds.Contains(Node.Id))
        {
            AddValidationError(OutDiagnostics, DuplicateId, TEXT("节点稳定 ID 为空或重复。"), Node.Id, Node.SourceLocation);
            bValid = false;
        }
        AllIds.Add(Node.Id);
        MainNodes.Add(Node.Id, &Node);
        ParentById.Add(Node.Id, Node.ParentId);

        UClass* NodeClass = Node.ClassPath.TryLoadClass<UBTNode>();
        if (!NodeClass || (!NodeClass->IsChildOf(UBTCompositeNode::StaticClass()) && !NodeClass->IsChildOf(UBTTaskNode::StaticClass())))
        {
            AddValidationError(OutDiagnostics, InvalidClass, TEXT("主节点类必须继承 UBTCompositeNode 或 UBTTaskNode。"), Node.ClassPath.ToString(), Node.SourceLocation);
            bValid = false;
        }
    }

    const FSekiroBehaviorTreeIRNode* const* RootNode = MainNodes.Find(IR.RootNodeId);
    UClass* RootClass = RootNode ? (*RootNode)->ClassPath.TryLoadClass<UBTNode>() : nullptr;
    if (!RootNode || !RootClass || !RootClass->IsChildOf(UBTCompositeNode::StaticClass()))
    {
        AddValidationError(OutDiagnostics, MissingRoot, TEXT("RootNodeId 必须指向 Composite 主节点。"), IR.RootNodeId, FSekiroBehaviorTreeSourceLocation());
        bValid = false;
    }

    for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
    {
        if (Node.Id == IR.RootNodeId)
        {
            if (!Node.ParentId.IsEmpty())
            {
                AddValidationError(OutDiagnostics, InvalidAttachment, TEXT("根节点 ParentId 必须为空。"), Node.Id, Node.SourceLocation);
                bValid = false;
            }
            continue;
        }
        const FSekiroBehaviorTreeIRNode* const* ParentNode = MainNodes.Find(Node.ParentId);
        UClass* ParentClass = ParentNode ? (*ParentNode)->ClassPath.TryLoadClass<UBTNode>() : nullptr;
        if (!ParentNode || !ParentClass || !ParentClass->IsChildOf(UBTCompositeNode::StaticClass()))
        {
            AddValidationError(OutDiagnostics, InvalidAttachment, TEXT("主节点只能挂到存在的 Composite 父节点。"), Node.Id, Node.SourceLocation);
            bValid = false;
        }
    }

    for (const FSekiroBehaviorTreeIRNode& Decorator : IR.Decorators)
    {
        UClass* DecoratorClass = Decorator.ClassPath.TryLoadClass<UBTDecorator>();
        if (Decorator.Id.IsEmpty() || AllIds.Contains(Decorator.Id))
        {
            AddValidationError(OutDiagnostics, DuplicateId, TEXT("Decorator 稳定 ID 为空或重复。"), Decorator.Id, Decorator.SourceLocation);
            bValid = false;
        }
        AllIds.Add(Decorator.Id);
        if (!DecoratorClass || !DecoratorClass->IsChildOf(UBTDecorator::StaticClass()) || !MainNodes.Contains(Decorator.ParentId))
        {
            AddValidationError(OutDiagnostics, InvalidAttachment, TEXT("Decorator 类或挂载目标无效。"), Decorator.Id, Decorator.SourceLocation);
            bValid = false;
        }
    }

    for (const FSekiroBehaviorTreeIRNode& Service : IR.Services)
    {
        UClass* ServiceClass = Service.ClassPath.TryLoadClass<UBTService>();
        if (Service.Id.IsEmpty() || AllIds.Contains(Service.Id))
        {
            AddValidationError(OutDiagnostics, DuplicateId, TEXT("Service 稳定 ID 为空或重复。"), Service.Id, Service.SourceLocation);
            bValid = false;
        }
        AllIds.Add(Service.Id);
        if (!ServiceClass || !ServiceClass->IsChildOf(UBTService::StaticClass()) || !MainNodes.Contains(Service.ParentId))
        {
            AddValidationError(OutDiagnostics, InvalidAttachment, TEXT("Service 类或挂载目标无效。"), Service.Id, Service.SourceLocation);
            bValid = false;
        }
    }

    TSet<FName> BlackboardKeyNames;
    for (const FSekiroBlackboardIRKey& Key : IR.BlackboardKeys)
    {
        UClass* KeyClass = Key.ClassPath.TryLoadClass<UBlackboardKeyType>();
        if (Key.Id.IsEmpty() || AllIds.Contains(Key.Id) || Key.Name.IsNone())
        {
            AddValidationError(OutDiagnostics, DuplicateId, TEXT("Blackboard Key ID 重复、为空或名称无效。"), Key.Id, Key.SourceLocation);
            bValid = false;
        }
        AllIds.Add(Key.Id);
        if (BlackboardKeyNames.Contains(Key.Name))
        {
            AddValidationError(OutDiagnostics, DuplicateId, TEXT("Blackboard Key 名称重复。"), Key.Name.ToString(), Key.SourceLocation);
            bValid = false;
        }
        BlackboardKeyNames.Add(Key.Name);
        if (!KeyClass || !KeyClass->IsChildOf(UBlackboardKeyType::StaticClass()))
        {
            AddValidationError(OutDiagnostics, InvalidClass, TEXT("Blackboard Key 类必须继承 UBlackboardKeyType。"), Key.ClassPath.ToString(), Key.SourceLocation);
            bValid = false;
        }
    }

    TSet<FString> Visiting;
    TSet<FString> Visited;
    for (const TPair<FString, FString>& Pair : ParentById)
    {
        if (HasParentCycle(Pair.Key, ParentById, Visiting, Visited))
        {
            AddValidationError(OutDiagnostics, GraphCycle, TEXT("行为树父链存在环。"), Pair.Key, FSekiroBehaviorTreeSourceLocation());
            bValid = false;
            break;
        }
    }

    for (const FSekiroBehaviorTreeIRNode& Node : IR.Nodes)
        bValid &= ValidatePropertyIndices(IR, Node.Properties, Node.Id, Node.SourceLocation, OutDiagnostics);
    for (const FSekiroBehaviorTreeIRNode& Decorator : IR.Decorators)
        bValid &= ValidatePropertyIndices(IR, Decorator.Properties, Decorator.Id, Decorator.SourceLocation, OutDiagnostics);
    for (const FSekiroBehaviorTreeIRNode& Service : IR.Services)
        bValid &= ValidatePropertyIndices(IR, Service.Properties, Service.Id, Service.SourceLocation, OutDiagnostics);
    for (const FSekiroBlackboardIRKey& Key : IR.BlackboardKeys)
        bValid &= ValidatePropertyIndices(IR, Key.Properties, Key.Id, Key.SourceLocation, OutDiagnostics);
    for (int32 ValueIndex = 0; ValueIndex < IR.Values.Num(); ++ValueIndex)
    {
        const FSekiroBehaviorTreeIRValue& Value = IR.Values[ValueIndex];
        for (const TPair<FName, int32>& FieldPair : Value.StructFields)
        {
            if (!IR.Values.IsValidIndex(FieldPair.Value))
            {
                AddValidationError(OutDiagnostics, InvalidInput, TEXT("Struct 字段引用了无效值池索引。"), FString::FromInt(ValueIndex), FSekiroBehaviorTreeSourceLocation());
                bValid = false;
            }
        }
        for (const int32 ChildIndex : Value.ArrayItems)
        {
            if (!IR.Values.IsValidIndex(ChildIndex))
            {
                AddValidationError(OutDiagnostics, InvalidInput, TEXT("Array 元素引用了无效值池索引。"), FString::FromInt(ValueIndex), FSekiroBehaviorTreeSourceLocation());
                bValid = false;
            }
        }
    }
    return bValid;
}
