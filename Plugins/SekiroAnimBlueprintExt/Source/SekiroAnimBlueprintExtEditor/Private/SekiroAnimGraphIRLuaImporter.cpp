#include "SekiroAnimGraphIRLibrary.h"

#include "Containers/StringConv.h"
#include "LuaEnv.h"
#include "LuaValue.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UnLuaLegacy.h"
#include "UnLuaModule.h"
#include "lua.hpp"

namespace SekiroAnimGraphIRLua
{
    const FName EmptyModuleName(TEXT("IR.LuaEmptyModuleName"));
    const FName WrongThread(TEXT("IR.LuaWrongThread"));
    const FName ModuleUnavailable(TEXT("IR.LuaModuleUnavailable"));
    const FName EnvironmentUnavailable(TEXT("IR.LuaEnvironmentUnavailable"));
    const FName RequireFailed(TEXT("IR.LuaRequireFailed"));
    const FName ModuleNotTable(TEXT("IR.LuaModuleNotTable"));
    const FName MissingCompileIR(TEXT("IR.LuaMissingCompileIR"));
    const FName CompileFailed(TEXT("IR.LuaCompileFailed"));
    const FName CompileResultNotTable(TEXT("IR.LuaCompileResultNotTable"));
    const FName MissingRuleFunction(TEXT("IR.LuaMissingRuleFunction"));
    const FName MissingField(TEXT("IR.LuaMissingField"));
    const FName InvalidFieldType(TEXT("IR.LuaInvalidFieldType"));
    const FName InvalidEnumValue(TEXT("IR.LuaInvalidEnumValue"));
    const FName InvalidArray(TEXT("IR.LuaInvalidArray"));
    const FName InvalidNumber(TEXT("IR.LuaInvalidNumber"));

    struct FParseContext
    {
        lua_State* State = nullptr;                                     // 当前 UnLua 主状态
        FString LuaModuleName;                                          // 被编译的 Lua 模块名
        TArray<FSekiroAnimIRDiagnostic>* Diagnostics = nullptr;         // 解析诊断输出
    };

    /**
     * 把 Lua C API 类型码转换为稳定的人类可读名称，仅用于诊断文本。
     * 本函数不修改 Lua 栈，可在持有 Lua 环境的游戏线程调用。
     *
     * @param State 当前 Lua 状态，必须有效。
     * @param Type Lua C API 类型码。
     * @return 对应类型名称；未知类型由 Lua 自身返回兜底名称。
     */
    FString DescribeLuaType(lua_State* State, const int32 Type)
    {
        return UTF8_TO_TCHAR(lua_typename(State, Type));
    }

    /**
     * 向导入结果追加一条 Error 级 Lua 诊断，不输出日志也不抛出异常。
     * 当位置未声明模块名时自动使用本次编译模块名，保证错误可定位。
     *
     * @param Context 当前解析上下文及诊断数组所有权。
     * @param Code 稳定的 IR.Lua* 诊断代码。
     * @param Message 面向调用者的错误说明。
     * @param SubjectId 出错字段或实体的稳定路径。
     * @param SourceLocation 最接近错误字段的 Lua 源位置。
     */
    void AddLuaError(
        FParseContext& Context,
        const FName Code,
        const FString& Message,
        const FString& SubjectId,
        const FSekiroAnimIRSourceLocation& SourceLocation)
    {
        FSekiroAnimIRDiagnostic& Diagnostic = Context.Diagnostics->AddDefaulted_GetRef();
        Diagnostic.Code = Code;
        Diagnostic.Severity = ESekiroAnimIRDiagnosticSeverity::Error;
        Diagnostic.Message = Message;
        Diagnostic.SubjectId = SubjectId;
        Diagnostic.SourceLocation = SourceLocation;
        if (Diagnostic.SourceLocation.LuaModule.IsEmpty())
        {
            Diagnostic.SourceLocation.LuaModule = Context.LuaModuleName;
        }
    }

    /**
     * 读取 PascalCase table 字段中的严格 Lua string，不接受数字隐式转换。
     * 函数结束时恢复进入时的 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 的有效栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 用于诊断的完整字段路径。
     * @param SourceLocation 字段所属实体的 Lua 位置。
     * @param OutValue 接收 UTF-8 转换后的 FString。
     * @return 字段存在且类型为 string 时返回 true，否则追加诊断并返回 false。
     */
    bool ReadStringField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FString& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 AbsoluteTableIndex = lua_absindex(Context.State, TableIndex);
        const int32 Type = lua_getfield(Context.State, AbsoluteTableIndex, FieldName);
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TSTRING)
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be string, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        size_t Utf8Length = 0;
        const char* Utf8Value = lua_tolstring(Context.State, -1, &Utf8Length);
        if (Utf8Length > static_cast<size_t>(MAX_int32))
        {
            AddLuaError(Context, InvalidNumber, FString::Printf(TEXT("String field '%s' exceeds supported length."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        const FUTF8ToTCHAR ConvertedValue(Utf8Value, static_cast<int32>(Utf8Length));
        OutValue = FString(ConvertedValue.Length(), ConvertedValue.Get());
        return true;
    }

    /**
     * 读取严格 Lua string 并转换为 FName，不加载或注册任何 UObject。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收名称值。
     * @return string 字段读取成功时返回 true，否则返回 false。
     */
    bool ReadNameField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FName& OutValue)
    {
        FString StringValue;
        if (!ReadStringField(Context, TableIndex, FieldName, FieldPath, SourceLocation, StringValue)) return false;
        OutValue = FName(*StringValue);
        return true;
    }

    /**
     * 读取严格 Lua boolean 字段，不接受 0、1 或 nil 代替布尔值。
     * 函数结束时恢复 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收布尔值。
     * @return 字段为 boolean 时返回 true，否则追加诊断并返回 false。
     */
    bool ReadBoolField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        bool& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TBOOLEAN)
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be boolean, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        OutValue = lua_toboolean(Context.State, -1) != 0;
        return true;
    }

    /**
     * 读取严格 Lua integer 并执行 int32 范围检查。
     * 函数不允许浮点数即使其数学值为整数，结束时恢复 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收 int32 值。
     * @return 字段是范围内 integer 时返回 true，否则追加诊断并返回 false。
     */
    bool ReadInt32Field(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        int32& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TNUMBER || !lua_isinteger(Context.State, -1))
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be integer, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        const lua_Integer Value = lua_tointeger(Context.State, -1);
        if (Value < MIN_int32 || Value > MAX_int32)
        {
            AddLuaError(Context, InvalidNumber, FString::Printf(TEXT("Field '%s' is outside int32 range."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }

        OutValue = static_cast<int32>(Value);
        return true;
    }

    /**
     * 读取严格 Lua integer 到 int64，不接受浮点表示。
     * 函数结束时恢复 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收 int64 值。
     * @return 字段是 integer 时返回 true，否则追加诊断并返回 false。
     */
    bool ReadInt64Field(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        int64& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TNUMBER || !lua_isinteger(Context.State, -1))
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be integer, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        OutValue = static_cast<int64>(lua_tointeger(Context.State, -1));
        return true;
    }

    /**
     * 读取有限 Lua number 到 double，整数和浮点表示均可接受。
     * 函数拒绝 NaN 与无穷值，并在结束时恢复 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收 double 值。
     * @return 字段是有限 number 时返回 true，否则追加诊断并返回 false。
     */
    bool ReadDoubleField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        double& OutValue)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TNUMBER)
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be number, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        const double Value = static_cast<double>(lua_tonumber(Context.State, -1));
        if (!FMath::IsFinite(Value))
        {
            AddLuaError(Context, InvalidNumber, FString::Printf(TEXT("Field '%s' must be finite."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }

        OutValue = Value;
        return true;
    }

    /**
     * 读取有限 Lua number 到 float，并检查转换后的有限性。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 字段所属实体位置。
     * @param OutValue 接收 float 值。
     * @return number 可安全表示为有限 float 时返回 true，否则返回 false。
     */
    bool ReadFloatField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        float& OutValue)
    {
        double DoubleValue = 0.0;
        if (!ReadDoubleField(Context, TableIndex, FieldName, FieldPath, SourceLocation, DoubleValue)) return false;

        const float FloatValue = static_cast<float>(DoubleValue);
        if (!FMath::IsFinite(FloatValue))
        {
            AddLuaError(Context, InvalidNumber, FString::Printf(TEXT("Field '%s' is outside finite float range."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }

        OutValue = FloatValue;
        return true;
    }

    /**
     * 从父 table 读取完整 SourceLocation 子 table。
     * LuaModule、Line、Column 均为必填字段；失败时使用调用方位置报告错误。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldPath SourceLocation 的完整路径。
     * @param FallbackLocation SourceLocation 自身无效时使用的位置。
     * @param OutLocation 接收解析结果。
     * @return 子 table 及其三个字段均合法时返回 true，否则返回 false。
     */
    bool ParseSourceLocationField(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& FallbackLocation,
        FSekiroAnimIRSourceLocation& OutLocation)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "SourceLocation");
        if (Type == LUA_TNIL)
        {
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, FallbackLocation);
            return false;
        }
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be table, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                FallbackLocation);
            return false;
        }

        const int32 LocationIndex = lua_absindex(Context.State, -1);
        return ReadStringField(Context, LocationIndex, "LuaModule", FieldPath + TEXT(".LuaModule"), FallbackLocation, OutLocation.LuaModule)
            && ReadInt32Field(Context, LocationIndex, "Line", FieldPath + TEXT(".Line"), FallbackLocation, OutLocation.Line)
            && ReadInt32Field(Context, LocationIndex, "Column", FieldPath + TEXT(".Column"), FallbackLocation, OutLocation.Column);
    }

    template<typename ElementType>
    using FElementParser = bool (*)(
        FParseContext&,
        int32,
        const FString&,
        const FSekiroAnimIRSourceLocation&,
        ElementType&);

    /**
     * 读取严格 1-based Lua array，并逐项调用显式元素解析器。
     * 数组字段必须是 table，1..rawlen 范围内不得出现 nil；函数结束时恢复 Lua 栈顶。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex 父 table 栈索引。
     * @param FieldName UTF-8 数组字段名。
     * @param FieldPath 完整字段路径。
     * @param SourceLocation 数组所属实体位置。
     * @param OutArray 接收值语义元素，函数开始时会清空。
     * @param ParseElement 负责单个 table 元素的显式解析函数。
     * @return 数组及所有元素均合法时返回 true，否则追加诊断并返回 false。
     */
    template<typename ElementType>
    bool ParseArrayField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& FieldPath,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        TArray<ElementType>& OutArray,
        const FElementParser<ElementType> ParseElement,
        const bool bOptional = false)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };

        OutArray.Reset();
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type == LUA_TNIL)
        {
            if (bOptional) return true;
            AddLuaError(Context, MissingField, FString::Printf(TEXT("Missing required field '%s'."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be a 1-based array table, got %s."), *FieldPath, *DescribeLuaType(Context.State, Type)),
                FieldPath,
                SourceLocation);
            return false;
        }

        const int32 ArrayIndex = lua_absindex(Context.State, -1);
        const size_t ArrayLength = lua_rawlen(Context.State, ArrayIndex);
        if (ArrayLength > static_cast<size_t>(MAX_int32))
        {
            AddLuaError(Context, InvalidArray, FString::Printf(TEXT("Array '%s' exceeds supported length."), *FieldPath), FieldPath, SourceLocation);
            return false;
        }

        lua_pushnil(Context.State);
        while (lua_next(Context.State, ArrayIndex) != 0)
        {
            const bool bIntegerKey = lua_type(Context.State, -2) == LUA_TNUMBER && lua_isinteger(Context.State, -2);
            const lua_Integer Key = bIntegerKey ? lua_tointeger(Context.State, -2) : 0;
            if (!bIntegerKey || Key < 1 || Key > static_cast<lua_Integer>(ArrayLength))
            {
                AddLuaError(
                    Context,
                    InvalidArray,
                    FString::Printf(TEXT("Field '%s' must contain only contiguous 1-based integer keys."), *FieldPath),
                    FieldPath,
                    SourceLocation);
                return false;
            }
            lua_pop(Context.State, 1);
        }

        OutArray.Reserve(static_cast<int32>(ArrayLength));
        for (int32 ElementIndex = 1; ElementIndex <= static_cast<int32>(ArrayLength); ++ElementIndex)
        {
            const int32 ElementTypeCode = lua_rawgeti(Context.State, ArrayIndex, ElementIndex);
            const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *FieldPath, ElementIndex);
            if (ElementTypeCode != LUA_TTABLE)
            {
                AddLuaError(
                    Context,
                    ElementTypeCode == LUA_TNIL ? InvalidArray : InvalidFieldType,
                    FString::Printf(TEXT("Array element '%s' must be table, got %s."), *ElementPath, *DescribeLuaType(Context.State, ElementTypeCode)),
                    ElementPath,
                    SourceLocation);
                return false;
            }

            ElementType& Element = OutArray.AddDefaulted_GetRef();
            if (!ParseElement(Context, lua_absindex(Context.State, -1), ElementPath, SourceLocation, Element)) return false;
            lua_pop(Context.State, 1);
        }

        return true;
    }

    bool ParsePin(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRPin& OutPin);
    bool ParseProperty(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRProperty& OutProperty);
    bool ParseLink(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRLink& OutLink);
    bool ParseNode(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRNode& OutNode);
    bool ParseState(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRState& OutState);
    bool ParseTransition(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRTransition& OutTransition);
    bool ParseGraph(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRGraph& OutGraph);
    bool ParseLayer(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRLayer& OutLayer);
    bool ParseVariable(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRVariable& OutVariable);
    bool ParseTransitionGateNode(FParseContext& Context, int32 TableIndex, const FString& Path, const FSekiroAnimIRSourceLocation& SourceLocation, FSekiroAnimIRTransitionGateNode& OutNode);

    /**
     * 解析 Value.Type 及其对应的唯一有效值字段。
     * 支持 Bool、Integer、Float、Name、String、SoftObjectPath、SoftClassPath 稳定标签。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Value table 栈索引。
     * @param Path Value 完整路径。
     * @param SourceLocation 所属节点位置。
     * @param OutValue 接收类型化属性值。
     * @return 类型标签和对应值字段均合法时返回 true，否则返回 false。
     */
    bool ParseValue(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRValue& OutValue)
    {
        FString TypeName;
        if (!ReadStringField(Context, TableIndex, "Type", Path + TEXT(".Type"), SourceLocation, TypeName)) return false;

        if (TypeName == TEXT("Bool"))
        {
            OutValue.Type = ESekiroAnimIRValueType::Bool;
            return ReadBoolField(Context, TableIndex, "BoolValue", Path + TEXT(".BoolValue"), SourceLocation, OutValue.BoolValue);
        }
        if (TypeName == TEXT("Integer"))
        {
            OutValue.Type = ESekiroAnimIRValueType::Integer;
            return ReadInt64Field(Context, TableIndex, "IntegerValue", Path + TEXT(".IntegerValue"), SourceLocation, OutValue.IntegerValue);
        }
        if (TypeName == TEXT("Float"))
        {
            OutValue.Type = ESekiroAnimIRValueType::Float;
            return ReadDoubleField(Context, TableIndex, "FloatValue", Path + TEXT(".FloatValue"), SourceLocation, OutValue.FloatValue);
        }
        if (TypeName == TEXT("Name"))
        {
            OutValue.Type = ESekiroAnimIRValueType::Name;
            return ReadNameField(Context, TableIndex, "NameValue", Path + TEXT(".NameValue"), SourceLocation, OutValue.NameValue);
        }
        if (TypeName == TEXT("String"))
        {
            OutValue.Type = ESekiroAnimIRValueType::String;
            return ReadStringField(Context, TableIndex, "StringValue", Path + TEXT(".StringValue"), SourceLocation, OutValue.StringValue);
        }
        if (TypeName == TEXT("SoftObjectPath"))
        {
            FString PathValue;
            OutValue.Type = ESekiroAnimIRValueType::SoftObjectPath;
            if (!ReadStringField(Context, TableIndex, "SoftObjectPathValue", Path + TEXT(".SoftObjectPathValue"), SourceLocation, PathValue)) return false;
            OutValue.SoftObjectPathValue = FSoftObjectPath(PathValue);
            return true;
        }
        if (TypeName == TEXT("SoftClassPath"))
        {
            FString PathValue;
            OutValue.Type = ESekiroAnimIRValueType::SoftClassPath;
            if (!ReadStringField(Context, TableIndex, "SoftClassPathValue", Path + TEXT(".SoftClassPathValue"), SourceLocation, PathValue)) return false;
            OutValue.SoftClassPathValue = FSoftClassPath(PathValue);
            return true;
        }

        AddLuaError(
            Context,
            InvalidEnumValue,
            FString::Printf(TEXT("Field '%s.Type' has unsupported value '%s'."), *Path, *TypeName),
            Path + TEXT(".Type"),
            SourceLocation);
        return false;
    }

    /**
     * 解析节点属性及其显式 Value table。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Property table 栈索引。
     * @param Path Property 完整路径。
     * @param SourceLocation 所属节点位置。
     * @param OutProperty 接收属性结果。
     * @return 所有字段合法时返回 true，否则返回 false。
     */
    bool ParseProperty(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRProperty& OutProperty)
    {
        if (!ReadNameField(Context, TableIndex, "Name", Path + TEXT(".Name"), SourceLocation, OutProperty.Name)
            || !ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), SourceLocation, OutProperty.DeclarationOrder))
        {
            return false;
        }

        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "Value");
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                Type == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s.Value' must be table, got %s."), *Path, *DescribeLuaType(Context.State, Type)),
                Path + TEXT(".Value"),
                SourceLocation);
            return false;
        }

        return ParseValue(Context, lua_absindex(Context.State, -1), Path + TEXT(".Value"), SourceLocation, OutProperty.Value);
    }

    /**
     * 解析 Pin 名称、方向、数据类型、连接基数和声明顺序。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Pin table 栈索引。
     * @param Path Pin 完整路径。
     * @param SourceLocation 所属节点位置。
     * @param OutPin 接收 Pin 结果。
     * @return 所有字段及 Direction 标签合法时返回 true，否则返回 false。
     */
    bool ParsePin(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRPin& OutPin)
    {
        FString Direction;
        if (!ReadStringField(Context, TableIndex, "Name", Path + TEXT(".Name"), SourceLocation, OutPin.Name)
            || !ReadStringField(Context, TableIndex, "Direction", Path + TEXT(".Direction"), SourceLocation, Direction)
            || !ReadNameField(Context, TableIndex, "DataType", Path + TEXT(".DataType"), SourceLocation, OutPin.DataType)
            || !ReadBoolField(Context, TableIndex, "bAllowMultipleConnections", Path + TEXT(".bAllowMultipleConnections"), SourceLocation, OutPin.bAllowMultipleConnections)
            || !ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), SourceLocation, OutPin.DeclarationOrder))
        {
            return false;
        }

        if (Direction == TEXT("Input"))
        {
            OutPin.Direction = ESekiroAnimIRPinDirection::Input;
            return true;
        }
        if (Direction == TEXT("Output"))
        {
            OutPin.Direction = ESekiroAnimIRPinDirection::Output;
            return true;
        }

        AddLuaError(
            Context,
            InvalidEnumValue,
            FString::Printf(TEXT("Field '%s.Direction' has unsupported value '%s'."), *Path, *Direction),
            Path + TEXT(".Direction"),
            SourceLocation);
        return false;
    }

    /**
     * 解析 Link 的 Source 或 Target PinEndpoint 子 table。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Link table 栈索引。
     * @param FieldName Source 或 Target 字段名。
     * @param Path 端点完整路径。
     * @param SourceLocation Link 声明位置。
     * @param OutEndpoint 接收节点与 Pin 标识。
     * @return 端点 table 及字段合法时返回 true，否则返回 false。
     */
    bool ParseEndpointField(
        FParseContext& Context,
        const int32 TableIndex,
        const char* FieldName,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRPinEndpoint& OutEndpoint)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), FieldName);
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                Type == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be table, got %s."), *Path, *DescribeLuaType(Context.State, Type)),
                Path,
                SourceLocation);
            return false;
        }

        const int32 EndpointIndex = lua_absindex(Context.State, -1);
        return ReadStringField(Context, EndpointIndex, "NodeId", Path + TEXT(".NodeId"), SourceLocation, OutEndpoint.NodeId)
            && ReadStringField(Context, EndpointIndex, "PinName", Path + TEXT(".PinName"), SourceLocation, OutEndpoint.PinName);
    }

    /**
     * 解析有向 Link 及其两个端点和源码位置。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Link table 栈索引。
     * @param Path Link 完整路径。
     * @param SourceLocation 父 Graph 位置，供 Link 位置本身损坏时兜底。
     * @param OutLink 接收 Link 结果。
     * @return 所有字段合法时返回 true，否则返回 false。
     */
    bool ParseLink(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRLink& OutLink)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutLink.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutLink.SourceLocation, OutLink.Id)
            && ParseEndpointField(Context, TableIndex, "Source", Path + TEXT(".Source"), OutLink.SourceLocation, OutLink.Source)
            && ParseEndpointField(Context, TableIndex, "Target", Path + TEXT(".Target"), OutLink.SourceLocation, OutLink.Target)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutLink.SourceLocation, OutLink.DeclarationOrder);
    }

    /**
     * 解析通用节点、Owned Graph 引用、Pin 数组、Property 数组和源码位置。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Node table 栈索引。
     * @param Path Node 完整路径。
     * @param SourceLocation 父 Graph 位置。
     * @param OutNode 接收节点结果。
     * @return 节点及全部子项合法时返回 true，否则返回 false。
     */
    bool ParseNode(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRNode& OutNode)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutNode.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutNode.SourceLocation, OutNode.Id)
            && ReadNameField(Context, TableIndex, "NodeType", Path + TEXT(".NodeType"), OutNode.SourceLocation, OutNode.NodeType)
            && ReadStringField(Context, TableIndex, "DisplayName", Path + TEXT(".DisplayName"), OutNode.SourceLocation, OutNode.DisplayName)
            && ReadStringField(Context, TableIndex, "OwnedGraphId", Path + TEXT(".OwnedGraphId"), OutNode.SourceLocation, OutNode.OwnedGraphId)
            && ParseArrayField(Context, TableIndex, "Pins", Path + TEXT(".Pins"), OutNode.SourceLocation, OutNode.Pins, &ParsePin)
            && ParseArrayField(Context, TableIndex, "Properties", Path + TEXT(".Properties"), OutNode.SourceLocation, OutNode.Properties, &ParseProperty)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutNode.SourceLocation, OutNode.DeclarationOrder);
    }

    /**
     * 解析状态定义及其 Pose Graph 引用。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex State table 栈索引。
     * @param Path State 完整路径。
     * @param SourceLocation 父 Graph 位置。
     * @param OutState 接收状态结果。
     * @return 所有字段合法时返回 true，否则返回 false。
     */
    bool ParseState(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRState& OutState)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutState.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutState.SourceLocation, OutState.Id)
            && ReadStringField(Context, TableIndex, "Name", Path + TEXT(".Name"), OutState.SourceLocation, OutState.Name)
            && ReadStringField(Context, TableIndex, "GraphId", Path + TEXT(".GraphId"), OutState.SourceLocation, OutState.GraphId)
            && ReadBoolField(Context, TableIndex, "bAlwaysResetOnEntry", Path + TEXT(".bAlwaysResetOnEntry"), OutState.SourceLocation, OutState.bAlwaysResetOnEntry)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutState.SourceLocation, OutState.DeclarationOrder);
    }

    /**
     * 解析 Transition.Settings 子 table。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Transition table 栈索引。
     * @param Path Settings 完整路径。
     * @param SourceLocation Transition 声明位置。
     * @param OutSettings 接收混合参数。
     * @return Settings table 及全部字段合法时返回 true，否则返回 false。
     */
    bool ParseTransitionSettingsField(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRTransitionSettings& OutSettings)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "Settings");
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                Type == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be table, got %s."), *Path, *DescribeLuaType(Context.State, Type)),
                Path,
                SourceLocation);
            return false;
        }

        const int32 SettingsIndex = lua_absindex(Context.State, -1);
        return ReadFloatField(Context, SettingsIndex, "BlendDuration", Path + TEXT(".BlendDuration"), SourceLocation, OutSettings.BlendDuration)
            && ReadInt32Field(Context, SettingsIndex, "PriorityOrder", Path + TEXT(".PriorityOrder"), SourceLocation, OutSettings.PriorityOrder)
            && ReadNameField(Context, SettingsIndex, "BlendMode", Path + TEXT(".BlendMode"), SourceLocation, OutSettings.BlendMode);
    }

    /**
     * 读取 Gate 节点的连续整数 Children 数组。
     * 函数仅操作当前 Lua 栈并在返回前恢复，必须在导入器所在游戏线程调用。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Gate 节点 table 栈索引。
     * @param Path Children 字段完整路径。
     * @param SourceLocation Transition 源位置。
     * @param OutChildren 接收零基 Gate 节点索引。
     * @return 字段是连续整数数组时返回 true，否则追加诊断并返回 false。
     */
    bool ParseIntegerArrayField(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        TArray<int32>& OutChildren)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "Children");
        if (Type != LUA_TTABLE)
        {
            AddLuaError(Context, Type == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be an integer array."), *Path), Path, SourceLocation);
            return false;
        }

        const int32 ArrayIndex = lua_absindex(Context.State, -1);
        const int32 ArrayLength = static_cast<int32>(lua_rawlen(Context.State, ArrayIndex));
        OutChildren.Reset(ArrayLength);
        for (int32 Index = 1; Index <= ArrayLength; ++Index)
        {
            lua_rawgeti(Context.State, ArrayIndex, Index);
            if (!lua_isinteger(Context.State, -1))
            {
                AddLuaError(Context, InvalidFieldType,
                    FString::Printf(TEXT("Field '%s[%d]' must be integer."), *Path, Index), Path, SourceLocation);
                return false;
            }
            OutChildren.Add(static_cast<int32>(lua_tointeger(Context.State, -1)));
            lua_pop(Context.State, 1);
        }
        return true;
    }

    /** 解析一个扁平 Transition Gate AST 节点；仅在游戏线程导入期间调用。 */
    bool ParseTransitionGateNode(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRTransitionGateNode& OutNode)
    {
        return ReadNameField(Context, TableIndex, "Type", Path + TEXT(".Type"), SourceLocation, OutNode.Type)
            && ReadNameField(Context, TableIndex, "Name", Path + TEXT(".Name"), SourceLocation, OutNode.Name)
            && ReadFloatField(Context, TableIndex, "Threshold", Path + TEXT(".Threshold"), SourceLocation, OutNode.Threshold)
            && ParseIntegerArrayField(Context, TableIndex, Path + TEXT(".Children"), SourceLocation, OutNode.Children);
    }

    /**
     * 解析可选 Transition.Gate；旧 IR 没有该字段时保留空 Gate，从而继续表示仅 Lua bool。
     * 只能在导入器持有 Lua 栈的游戏线程调用。
     */
    bool ParseTransitionGateField(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRTransitionGate& OutGate)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "Gate");
        if (Type == LUA_TNIL) return true;
        if (Type != LUA_TTABLE)
        {
            AddLuaError(Context, InvalidFieldType, FString::Printf(TEXT("Field '%s' must be table."), *Path), Path, SourceLocation);
            return false;
        }
        const int32 GateIndex = lua_absindex(Context.State, -1);
        return ReadInt32Field(Context, GateIndex, "RootIndex", Path + TEXT(".RootIndex"), SourceLocation, OutGate.RootIndex)
            && ParseArrayField(Context, GateIndex, "Nodes", Path + TEXT(".Nodes"), SourceLocation, OutGate.Nodes, &ParseTransitionGateNode);
    }

    /**
     * 解析状态机有向 Transition 的稳定 ID、语义 Key、规则函数名和混合设置。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Transition table 栈索引。
     * @param Path Transition 完整路径。
     * @param SourceLocation 父 Graph 位置。
     * @param OutTransition 接收过渡结果。
     * @return 所有字段合法时返回 true，否则返回 false。
     */
    bool ParseTransition(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRTransition& OutTransition)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutTransition.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutTransition.SourceLocation, OutTransition.Id)
            && ReadStringField(Context, TableIndex, "Key", Path + TEXT(".Key"), OutTransition.SourceLocation, OutTransition.Key)
            && ReadStringField(Context, TableIndex, "SourceStateId", Path + TEXT(".SourceStateId"), OutTransition.SourceLocation, OutTransition.SourceStateId)
            && ReadStringField(Context, TableIndex, "TargetStateId", Path + TEXT(".TargetStateId"), OutTransition.SourceLocation, OutTransition.TargetStateId)
            && ReadNameField(Context, TableIndex, "RuleFunctionName", Path + TEXT(".RuleFunctionName"), OutTransition.SourceLocation, OutTransition.RuleFunctionName)
            && ParseTransitionSettingsField(Context, TableIndex, Path + TEXT(".Settings"), OutTransition.SourceLocation, OutTransition.Settings)
            && ParseTransitionGateField(Context, TableIndex, Path + TEXT(".Gate"), OutTransition.SourceLocation, OutTransition.Gate)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutTransition.SourceLocation, OutTransition.DeclarationOrder);
    }

    /**
     * 解析 Graph.StateMachine 子 table，即使普通 Pose Graph 也要求提供空规范结构。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Graph table 栈索引。
     * @param Path StateMachine 完整路径。
     * @param SourceLocation Graph 声明位置。
     * @param OutStateMachine 接收状态机拓扑。
     * @return table、EntryStateId 和两个数组均合法时返回 true，否则返回 false。
     */
    bool ParseStateMachineField(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRStateMachine& OutStateMachine)
    {
        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 Type = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "StateMachine");
        if (Type != LUA_TTABLE)
        {
            AddLuaError(
                Context,
                Type == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s' must be table, got %s."), *Path, *DescribeLuaType(Context.State, Type)),
                Path,
                SourceLocation);
            return false;
        }

        const int32 StateMachineIndex = lua_absindex(Context.State, -1);
        return ReadStringField(Context, StateMachineIndex, "EntryStateId", Path + TEXT(".EntryStateId"), SourceLocation, OutStateMachine.EntryStateId)
            && ParseArrayField(Context, StateMachineIndex, "States", Path + TEXT(".States"), SourceLocation, OutStateMachine.States, &ParseState)
            && ParseArrayField(Context, StateMachineIndex, "Transitions", Path + TEXT(".Transitions"), SourceLocation, OutStateMachine.Transitions, &ParseTransition);
    }

    /**
     * 解析 Graph、通用节点连接以及可选语义由 GraphType 决定的状态机内容。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Graph table 栈索引。
     * @param Path Graph 完整路径。
     * @param SourceLocation 父 Layer 位置。
     * @param OutGraph 接收 Graph 结果。
     * @return Graph 及全部子结构合法时返回 true，否则返回 false。
     */
    bool ParseGraph(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRGraph& OutGraph)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutGraph.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutGraph.SourceLocation, OutGraph.Id)
            && ReadStringField(Context, TableIndex, "Name", Path + TEXT(".Name"), OutGraph.SourceLocation, OutGraph.Name)
            && ReadNameField(Context, TableIndex, "GraphType", Path + TEXT(".GraphType"), OutGraph.SourceLocation, OutGraph.GraphType)
            && ReadStringField(Context, TableIndex, "RootNodeId", Path + TEXT(".RootNodeId"), OutGraph.SourceLocation, OutGraph.RootNodeId)
            && ParseArrayField(Context, TableIndex, "Nodes", Path + TEXT(".Nodes"), OutGraph.SourceLocation, OutGraph.Nodes, &ParseNode)
            && ParseArrayField(Context, TableIndex, "Links", Path + TEXT(".Links"), OutGraph.SourceLocation, OutGraph.Links, &ParseLink)
            && ParseStateMachineField(Context, TableIndex, Path + TEXT(".StateMachine"), OutGraph.SourceLocation, OutGraph.StateMachine)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutGraph.SourceLocation, OutGraph.DeclarationOrder);
    }

    /**
     * 解析动画 Layer 及其 Graph 数组。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Layer table 栈索引。
     * @param Path Layer 完整路径。
     * @param SourceLocation Blueprint 位置。
     * @param OutLayer 接收 Layer 结果。
     * @return Layer 及全部 Graph 合法时返回 true，否则返回 false。
     */
    bool ParseLayer(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRLayer& OutLayer)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutLayer.SourceLocation)) return false;
        return ReadStringField(Context, TableIndex, "Id", Path + TEXT(".Id"), OutLayer.SourceLocation, OutLayer.Id)
            && ReadStringField(Context, TableIndex, "Name", Path + TEXT(".Name"), OutLayer.SourceLocation, OutLayer.Name)
            && ReadStringField(Context, TableIndex, "RootGraphId", Path + TEXT(".RootGraphId"), OutLayer.SourceLocation, OutLayer.RootGraphId)
            && ParseArrayField(Context, TableIndex, "Graphs", Path + TEXT(".Graphs"), OutLayer.SourceLocation, OutLayer.Graphs, &ParseGraph)
            && ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutLayer.SourceLocation, OutLayer.DeclarationOrder);
    }

    /**
     * 解析 GeneratedClass 成员变量声明及显式默认值。
     * 仅在 Lua IR 导入的游戏线程调用，不加载 Enum 软路径。
     */
    bool ParseVariable(
        FParseContext& Context,
        const int32 TableIndex,
        const FString& Path,
        const FSekiroAnimIRSourceLocation& SourceLocation,
        FSekiroAnimIRVariable& OutVariable)
    {
        if (!ParseSourceLocationField(Context, TableIndex, Path + TEXT(".SourceLocation"), SourceLocation, OutVariable.SourceLocation)) return false;
        FString TypeObjectPath;
        if (!ReadNameField(Context, TableIndex, "Name", Path + TEXT(".Name"), OutVariable.SourceLocation, OutVariable.Name)
            || !ReadNameField(Context, TableIndex, "DataType", Path + TEXT(".DataType"), OutVariable.SourceLocation, OutVariable.DataType)
            || !ReadStringField(Context, TableIndex, "TypeObjectPath", Path + TEXT(".TypeObjectPath"), OutVariable.SourceLocation, TypeObjectPath)
            || !ReadBoolField(Context, TableIndex, "bTransient", Path + TEXT(".bTransient"), OutVariable.SourceLocation, OutVariable.bTransient)
            || !ReadInt32Field(Context, TableIndex, "DeclarationOrder", Path + TEXT(".DeclarationOrder"), OutVariable.SourceLocation, OutVariable.DeclarationOrder))
        {
            return false;
        }

        const int32 InitialTop = lua_gettop(Context.State);
        ON_SCOPE_EXIT { lua_settop(Context.State, InitialTop); };
        const int32 ValueType = lua_getfield(Context.State, lua_absindex(Context.State, TableIndex), "DefaultValue");
        if (ValueType != LUA_TTABLE)
        {
            AddLuaError(Context, ValueType == LUA_TNIL ? MissingField : InvalidFieldType,
                FString::Printf(TEXT("Field '%s.DefaultValue' must be table."), *Path), Path, OutVariable.SourceLocation);
            return false;
        }
        OutVariable.TypeObjectPath = FSoftObjectPath(TypeObjectPath);
        return ParseValue(Context, lua_absindex(Context.State, -1), Path + TEXT(".DefaultValue"), OutVariable.SourceLocation, OutVariable.DefaultValue);
    }

    /**
     * 把 CompileIR 返回的根 table 显式转换为 FSekiroAnimBlueprintIR。
     * 本函数只做类型化读取，不运行 Validator、不加载软路径对象，结束时保持 Lua 栈不变。
     *
     * @param Context 当前解析上下文。
     * @param TableIndex Blueprint 根 table 栈索引。
     * @param OutBlueprint 接收完整 IR；调用方应传入已重置对象。
     * @return 所有规范字段均解析成功时返回 true，否则返回 false。
     */
    bool ParseBlueprint(
        FParseContext& Context,
        const int32 TableIndex,
        FSekiroAnimBlueprintIR& OutBlueprint)
    {
        FSekiroAnimIRSourceLocation FallbackLocation;
        FallbackLocation.LuaModule = Context.LuaModuleName;
        if (!ParseSourceLocationField(Context, TableIndex, TEXT("Blueprint.SourceLocation"), FallbackLocation, OutBlueprint.SourceLocation)) return false;

        FString ParentClassPath;
        FString TargetSkeletonPath;
        if (!ReadInt32Field(Context, TableIndex, "SchemaVersion", TEXT("Blueprint.SchemaVersion"), OutBlueprint.SourceLocation, OutBlueprint.SchemaVersion)
            || !ReadStringField(Context, TableIndex, "SourceModule", TEXT("Blueprint.SourceModule"), OutBlueprint.SourceLocation, OutBlueprint.SourceModule)
            || !ReadStringField(Context, TableIndex, "ParentAnimInstanceClass", TEXT("Blueprint.ParentAnimInstanceClass"), OutBlueprint.SourceLocation, ParentClassPath)
            || !ReadStringField(Context, TableIndex, "TargetSkeleton", TEXT("Blueprint.TargetSkeleton"), OutBlueprint.SourceLocation, TargetSkeletonPath)
            || !ParseArrayField(Context, TableIndex, "Variables", TEXT("Blueprint.Variables"), OutBlueprint.SourceLocation, OutBlueprint.Variables, &ParseVariable, true)
            || !ParseArrayField(Context, TableIndex, "Layers", TEXT("Blueprint.Layers"), OutBlueprint.SourceLocation, OutBlueprint.Layers, &ParseLayer))
        {
            return false;
        }

        OutBlueprint.ParentAnimInstanceClass = FSoftClassPath(ParentClassPath);
        OutBlueprint.TargetSkeleton = FSoftObjectPath(TargetSkeletonPath);
        return true;
    }

    /**
     * 验证 IR 声明的每个 RuleFunctionName 都可从 require 导出 table 或其 __index 继承链解析为 Lua function。
     * 函数只检查函数存在性，不调用规则、不创建 UObject；必须在持有当前 UnLua Env 的游戏线程执行，并在每次查询后恢复 Lua 栈。
     *
     * @param Context 当前 Lua 状态、模块名和诊断输出。
     * @param ModuleTableIndex require 返回 table 的有效栈索引。
     * @param Blueprint 已通过结构 Validator 的只读 IR。
     * @return 所有 Transition Rule 均解析为 function 时返回 true，否则追加稳定诊断并返回 false。
     */
    bool ValidateTransitionRuleFunctions(
        FParseContext& Context,
        const int32 ModuleTableIndex,
        const FSekiroAnimBlueprintIR& Blueprint)
    {
        bool bAllRulesValid = true;
        const int32 AbsoluteModuleIndex = lua_absindex(Context.State, ModuleTableIndex);
        for (const FSekiroAnimIRLayer& Layer : Blueprint.Layers)
        {
            for (const FSekiroAnimIRGraph& Graph : Layer.Graphs)
            {
                for (const FSekiroAnimIRTransition& Transition : Graph.StateMachine.Transitions)
                {
                    const int32 InitialTop = lua_gettop(Context.State);
                    const FString RuleName = Transition.RuleFunctionName.ToString();
                    const FTCHARToUTF8 RuleNameUtf8(*RuleName);
                    const int32 RuleType = lua_getfield(
                        Context.State,
                        AbsoluteModuleIndex,
                        RuleNameUtf8.Get());
                    lua_settop(Context.State, InitialTop);
                    if (RuleType == LUA_TFUNCTION) continue;

                    AddLuaError(
                        Context,
                        MissingRuleFunction,
                        FString::Printf(
                            TEXT("Lua module '%s' must export Transition Rule function '%s'."),
                            *Context.LuaModuleName,
                            *RuleName),
                        Transition.Id + TEXT(".RuleFunctionName"),
                        Transition.SourceLocation);
                    bAllRulesValid = false;
                }
            }
        }

        return bAllRulesValid;
    }
}

/**
 * 在编辑器游戏线程中通过 UnLua require 指定模块，调用其无参 CompileIR，并导入规范 Lua table。
 * 本函数不读取 JSON、不访问项目资源，也不生成 AnimBlueprint 资产；解析成功后调用 Validate，验证成功时再规范化输出。
 * 非 PIE 编辑器进程中会按需激活 UnLua，并保持模块激活以供后续编译复用；本函数不会在 Lua wrapper 存活时关闭 Env。
 * Require 与 CompileIR 的 FLuaRetValues 在各自作用域结束时按逆序弹出返回值，所有显式字段读取也恢复局部栈顶。
 *
 * @param LuaModuleName 交给 Lua require 的模块名，不是文件系统路径，不能为空。
 * @param OutBlueprint 接收导入后的值语义 IR；函数开始时重置，失败时保留已解析前缀供诊断查看。
 * @param OutDiagnostics 接收解析或 Validator 诊断；函数开始时清空，不保留外部引用。
 * @return Lua 调用、显式解析和 Validator 全部成功时返回 true，否则返回 false。
 */
bool USekiroAnimGraphIRLibrary::CompileLuaModule(
    const FString& LuaModuleName,
    FSekiroAnimBlueprintIR& OutBlueprint,
    TArray<FSekiroAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace SekiroAnimGraphIRLua;

    OutBlueprint = FSekiroAnimBlueprintIR();
    OutDiagnostics.Reset();

    FParseContext Context;
    Context.LuaModuleName = LuaModuleName;
    Context.Diagnostics = &OutDiagnostics;

    FSekiroAnimIRSourceLocation ModuleLocation;
    ModuleLocation.LuaModule = LuaModuleName;
    if (LuaModuleName.IsEmpty())
    {
        AddLuaError(Context, EmptyModuleName, TEXT("Lua module name must not be empty."), LuaModuleName, ModuleLocation);
        return false;
    }
    if (!IsInGameThread())
    {
        AddLuaError(Context, WrongThread, TEXT("CompileLuaModule must run on the game thread."), LuaModuleName, ModuleLocation);
        return false;
    }

    IUnLuaModule* UnLuaModule = FModuleManager::LoadModulePtr<IUnLuaModule>(TEXT("UnLua"));
    if (!UnLuaModule)
    {
        AddLuaError(Context, ModuleUnavailable, TEXT("UnLua module could not be loaded."), LuaModuleName, ModuleLocation);
        return false;
    }

    if (!UnLuaModule->IsActive())
    {
        UnLuaModule->SetActive(true);
    }

    UnLua::FLuaEnv* Environment = UnLuaModule->GetEnv();
    if (!Environment)
    {
        AddLuaError(Context, EnvironmentUnavailable, TEXT("UnLua environment is not active."), LuaModuleName, ModuleLocation);
        return false;
    }

    Context.State = Environment->GetMainState();
    UnLua::FLuaRetValues RequiredValues = UnLua::Call(Context.State, "require", TCHAR_TO_UTF8(*LuaModuleName));
    if (!RequiredValues.IsValid() || RequiredValues.Num() < 1)
    {
        AddLuaError(Context, RequireFailed, FString::Printf(TEXT("require('%s') failed or returned no value."), *LuaModuleName), LuaModuleName, ModuleLocation);
        return false;
    }
    if (RequiredValues[0].GetType() != LUA_TTABLE)
    {
        AddLuaError(
            Context,
            ModuleNotTable,
            FString::Printf(TEXT("Lua module '%s' must return table, got %s."), *LuaModuleName, *DescribeLuaType(Context.State, RequiredValues[0].GetType())),
            LuaModuleName,
            ModuleLocation);
        return false;
    }

    UnLua::FLuaTable ModuleTable(Environment, RequiredValues[0]);
    const int32 InitialTop = lua_gettop(Context.State);
    const int32 CompileIRType = lua_getfield(Context.State, ModuleTable.GetIndex(), "CompileIR");
    lua_settop(Context.State, InitialTop);
    if (CompileIRType != LUA_TFUNCTION)
    {
        AddLuaError(Context, MissingCompileIR, FString::Printf(TEXT("Lua module '%s' must provide function CompileIR."), *LuaModuleName), LuaModuleName, ModuleLocation);
        return false;
    }

    UnLua::FLuaRetValues CompiledValues = ModuleTable.Call("CompileIR");
    if (!CompiledValues.IsValid() || CompiledValues.Num() < 1)
    {
        AddLuaError(Context, CompileFailed, FString::Printf(TEXT("%s.CompileIR() failed or returned no value."), *LuaModuleName), LuaModuleName, ModuleLocation);
        return false;
    }
    if (CompiledValues[0].GetType() != LUA_TTABLE)
    {
        AddLuaError(
            Context,
            CompileResultNotTable,
            FString::Printf(TEXT("%s.CompileIR() must return table, got %s."), *LuaModuleName, *DescribeLuaType(Context.State, CompiledValues[0].GetType())),
            LuaModuleName,
            ModuleLocation);
        return false;
    }

    if (!ParseBlueprint(Context, CompiledValues[0].GetIndex(), OutBlueprint)) return false;
    if (!Validate(OutBlueprint, OutDiagnostics)) return false;
    if (!ValidateTransitionRuleFunctions(Context, ModuleTable.GetIndex(), OutBlueprint)) return false;

    Canonicalize(OutBlueprint);
    return true;
}
