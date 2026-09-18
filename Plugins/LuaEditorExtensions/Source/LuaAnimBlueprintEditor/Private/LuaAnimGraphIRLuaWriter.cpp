#include "LuaAnimGraphIRLuaWriter.h"

#include "LuaAnimGraphIRLibrary.h"
#include "UObject/UnrealType.h"

namespace LuaAnimGraphIRLuaWriterPrivate
{
    /**
     * 将任意 UTF-16 字符串编码为不依赖 Lua 长字符串分隔符的双引号字面量。
     * 函数仅构造文本，不访问 Lua VM 或 UObject，可在任意线程调用。
     *
     * @param Value 待转义的原始字符串；换行、制表、引号、反斜杠和 C0 控制字符会显式编码。
     * @return 包含外层双引号的有效 Lua 字符串字面量。
     */
    FString Quote(const FString& Value)
    {
        FString Result(TEXT("\""));
        for (const TCHAR Character : Value)
        {
            switch (Character)
            {
            case TEXT('\\'): Result += TEXT("\\\\"); break;
            case TEXT('"'): Result += TEXT("\\\""); break;
            case TEXT('\n'): Result += TEXT("\\n"); break;
            case TEXT('\r'): Result += TEXT("\\r"); break;
            case TEXT('\t'): Result += TEXT("\\t"); break;
            default:
                if (Character < 0x20)
                {
                    Result += FString::Printf(TEXT("\\%03d"), static_cast<int32>(Character));
                }
                else
                {
                    Result.AppendChar(Character);
                }
                break;
            }
        }
        Result += TEXT("\"");
        return Result;
    }

    /**
     * 返回指定层级的四空格缩进；只构造值字符串，可在任意线程调用。
     *
     * @param Depth 非负嵌套层级，每层对应四个空格。
     * @return 可直接前缀到当前 Lua 行的缩进字符串。
     */
    FString Indent(const int32 Depth)
    {
        return FString::ChrN(Depth * 4, TEXT(' '));
    }

    /**
     * 将反射枚举值写为 UnLua 可直接求值的 UE.EEnum.Member 表达式。
     * 本函数只用于 Lua 源码边界，不把枚举降级为业务字符串，也不修改输入 UEnum。
     *
     * @param Enum 定义原生枚举类型和成员名的反射对象。
     * @param RawValue 当前底层枚举数值。
     * @param OutText 追加 Lua 表达式的输出缓冲。
     * @return 数值对应可导出成员时返回 true，否则不写入并返回 false。
     */
    bool WriteEnumValue(
        const UEnum& Enum,
        const int64 RawValue,
        FString& OutText)
    {
        if (!Enum.IsValidEnumValue(RawValue)) return false;
        FString MemberName = Enum.GetNameStringByValue(RawValue);
        if (MemberName.IsEmpty()) return false;
        MemberName.Split(
            TEXT("::"),
            nullptr,
            &MemberName,
            ESearchCase::CaseSensitive,
            ESearchDir::FromEnd);
        OutText += TEXT("UE.") + Enum.GetName() + TEXT(".") + MemberName;
        return true;
    }

    bool WritePropertyValue(
        const FProperty& Property,
        const void* ValueAddress,
        const int32 Depth,
        FString& OutText);

    /**
     * 按反射声明顺序写出一个 USTRUCT table；IR 无 Map/Set，因此字段顺序稳定。
     * 函数不修改输入结构，不访问 UObject 实例，可在任意线程调用。
     *
     * @param Struct 定义字段顺序与字段类型的 UStruct 元数据。
     * @param StructAddress 与 Struct 匹配的只读值地址，不可为空。
     * @param Depth 当前 table 的缩进层级。
     * @param OutText 追加完整 Lua table 文本的输出缓冲。
     * @return 全部字段类型可写出时返回 true；遇到未支持反射类型时返回 false。
     */
    bool WriteStruct(
        const UStruct& Struct,
        const void* StructAddress,
        const int32 Depth,
        FString& OutText)
    {
        OutText += TEXT("{\n");
        for (TFieldIterator<FProperty> Iterator(&Struct); Iterator; ++Iterator)
        {
            const FProperty* Property = *Iterator;
            if (Property == nullptr) continue;
            OutText += Indent(Depth + 1) + Property->GetName() + TEXT(" = ");
            if (!WritePropertyValue(
                *Property,
                Property->ContainerPtrToValuePtr<void>(StructAddress),
                Depth + 1,
                OutText))
            {
                return false;
            }
            OutText += TEXT(",\n");
        }
        OutText += Indent(Depth) + TEXT("}");
        return true;
    }

    /**
     * 将支持的 IR 反射属性写为 Lua 标量、数组或子 table。
     * 函数不改变值内存，可在任意线程调用；遇到未知类型会保留已写前缀并返回 false。
     *
     * @param Property 待写字段的反射属性描述。
     * @param ValueAddress 指向 Property 实际值的只读地址，不可为空。
     * @param Depth 当前值的缩进层级。
     * @param OutText 追加 Lua 表达式的输出缓冲。
     * @return 属性可无损表达时返回 true，否则返回 false。
     */
    bool WritePropertyValue(
        const FProperty& Property,
        const void* ValueAddress,
        const int32 Depth,
        FString& OutText)
    {
        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
        {
            OutText += BoolProperty->GetPropertyValue(ValueAddress) ? TEXT("true") : TEXT("false");
            return true;
        }
        if (const FIntProperty* IntProperty = CastField<FIntProperty>(&Property))
        {
            OutText += LexToString(IntProperty->GetPropertyValue(ValueAddress));
            return true;
        }
        if (const FInt64Property* Int64Property = CastField<FInt64Property>(&Property))
        {
            OutText += LexToString(Int64Property->GetPropertyValue(ValueAddress));
            return true;
        }
        if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(&Property))
        {
            OutText += FString::Printf(TEXT("%.9g"), static_cast<double>(FloatProperty->GetPropertyValue(ValueAddress)));
            return true;
        }
        if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(&Property))
        {
            OutText += FString::Printf(TEXT("%.17g"), DoubleProperty->GetPropertyValue(ValueAddress));
            return true;
        }
        if (const FStrProperty* StringProperty = CastField<FStrProperty>(&Property))
        {
            OutText += Quote(StringProperty->GetPropertyValue(ValueAddress));
            return true;
        }
        if (const FNameProperty* NameProperty = CastField<FNameProperty>(&Property))
        {
            OutText += Quote(NameProperty->GetPropertyValue(ValueAddress).ToString());
            return true;
        }
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
        {
            const int64 RawValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValueAddress);
            return WriteEnumValue(*EnumProperty->GetEnum(), RawValue, OutText);
        }
        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(&Property))
        {
            if (ByteProperty->Enum != nullptr)
            {
                return WriteEnumValue(
                    *ByteProperty->Enum,
                    ByteProperty->GetPropertyValue(ValueAddress),
                    OutText);
            }
            else
            {
                OutText += LexToString(ByteProperty->GetPropertyValue(ValueAddress));
            }
            return true;
        }
        if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
        {
            if (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get())
            {
                OutText += Quote(static_cast<const FSoftObjectPath*>(ValueAddress)->ToString());
                return true;
            }
            if (StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get())
            {
                OutText += Quote(static_cast<const FSoftClassPath*>(ValueAddress)->ToString());
                return true;
            }
            return WriteStruct(*StructProperty->Struct, ValueAddress, Depth, OutText);
        }
        if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
        {
            FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
            OutText += TEXT("{\n");
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
            {
                OutText += Indent(Depth + 1);
                if (!WritePropertyValue(*ArrayProperty->Inner, Helper.GetRawPtr(Index), Depth + 1, OutText)) return false;
                OutText += TEXT(",\n");
            }
            OutText += Indent(Depth) + TEXT("}");
            return true;
        }
        return false;
    }
}

/**
 * 将动画蓝图 IR 复制、Canonicalize、Validate 后写为确定性纯数据 Lua 模块。
 * 函数不访问 UObject 或文件系统，可在任意线程调用；失败时清空 OutLuaText 并返回 Validator 诊断。
 *
 * @param Blueprint 待写出的完整 IR；调用方保留所有权且不会被修改。
 * @param OutLuaText 成功时接收包含 CompileIR() 的完整模块文本，失败时为空。
 * @param OutDiagnostics 接收规范 Validator 诊断；调用开始时清空。
 * @return IR 合法且所有字段类型均可无损表达时返回 true，否则返回 false。
 */
bool FLuaAnimGraphIRLuaWriter::WriteModule(
    const FLuaAnimBlueprintIR& Blueprint,
    FString& OutLuaText,
    TArray<FLuaAnimIRDiagnostic>& OutDiagnostics)
{
    using namespace LuaAnimGraphIRLuaWriterPrivate;
    OutLuaText.Reset();
    OutDiagnostics.Reset();
    FLuaAnimBlueprintIR Canonical = Blueprint;
    ULuaAnimGraphIRLibrary::Canonicalize(Canonical);
    if (!ULuaAnimGraphIRLibrary::Validate(Canonical, OutDiagnostics)) return false;

    FString IRText;
    if (!WriteStruct(*FLuaAnimBlueprintIR::StaticStruct(), &Canonical, 0, IRText))
    {
        FLuaAnimIRDiagnostic& Diagnostic = OutDiagnostics.AddDefaulted_GetRef();
        Diagnostic.Code = TEXT("Writer.UnsupportedPropertyType");
        Diagnostic.Severity = ELuaAnimIRDiagnosticSeverity::Error;
        Diagnostic.Message = TEXT("IR contains a reflected property type that the deterministic Lua writer does not support.");
        return false;
    }
    OutLuaText = TEXT("-- Lua 类型：动画蓝图编译描述模块。此文件由 LuaAnimBlueprint 生成，请勿手工修改。\n")
        TEXT("local IR = ") + IRText
        + TEXT("\n\n")
        TEXT("-- 交换模块只保存 IR；Transition Rule 仍由 SourceModule 提供，以保持原运行时行为。\n")
        TEXT("local RuntimeModule = require(IR.SourceModule)\n")
        TEXT("local Module = setmetatable({}, { __index = RuntimeModule })\n\n")
        TEXT("---返回写出时已经规范化并验证的完整动画蓝图 IR。\n")
        TEXT("---@return table ir 可由 Lua AnimGraph IR Importer 直接读取的纯数据表。\n")
        TEXT("function Module.CompileIR()\n")
        TEXT("    return IR\n")
        TEXT("end\n\n")
        TEXT("return Module\n");
    OutLuaText.ReplaceInline(TEXT("\n"), TEXT("\r\n"), ESearchCase::CaseSensitive);
    return true;
}
