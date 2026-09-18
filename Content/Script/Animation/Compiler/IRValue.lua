-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- 构造 AnimGraph IR 的显式类型化属性值。
-- 每个构造函数只写入自身类型对应的字段，C++ 导入器不需要从字符串内容猜测类型。
---@class IRValue
local IRValue = {}
local ValueType = UE.ELuaAnimIRValueType

---构造带稳定类型标签的 IR Value 表。
---@param value_type SekiroAnimIRValueType ELuaAnimIRValueType 原生枚举值。
---@param field_name string 当前类型实际使用的值字段名。
---@param value any 写入对应值字段的 Lua 值。
---@return SekiroAnimIRValue ir_value 可由 C++ 导入器显式解析的类型化值。
local function make_value(value_type, field_name, value)
    return {
        Type = value_type,
        [field_name] = value,
    }
end

---构造 Bool 类型属性值。
---@param value boolean 待写入 IR 的布尔值。
---@return SekiroAnimIRValue ir_value Bool 类型 IR Value。
function IRValue.Bool(value)
    assert(type(value) == "boolean", "Bool IR value requires a boolean")
    return make_value(ValueType.Bool, "BoolValue", value)
end

---构造 Integer 类型属性值。
---@param value number 待写入 IR 的整数值。
---@return SekiroAnimIRValue ir_value Integer 类型 IR Value。
function IRValue.Integer(value)
    assert(type(value) == "number" and value % 1 == 0, "Integer IR value requires an integer")
    return make_value(ValueType.Integer, "IntegerValue", value)
end

---构造 Enum 类型属性值；目标反射属性负责提供实际 UEnum 类型并校验该数值。
---@param value number UnLua 暴露的原生 UENUM 值。
---@return SekiroAnimIRValue ir_value Enum 类型 IR Value。
function IRValue.Enum(value)
    assert(type(value) == "number" and value % 1 == 0, "Enum IR value requires a native enum value")
    return make_value(ValueType.Enum, "IntegerValue", value)
end

---构造 Float 类型属性值。
---@param value number 待写入 IR 的浮点值。
---@return SekiroAnimIRValue ir_value Float 类型 IR Value。
function IRValue.Float(value)
    assert(type(value) == "number", "Float IR value requires a number")
    return make_value(ValueType.Float, "FloatValue", value)
end

---构造 Name 类型属性值。
---@param value string 待写入 FName 的稳定文本。
---@return SekiroAnimIRValue ir_value Name 类型 IR Value。
function IRValue.Name(value)
    assert(type(value) == "string", "Name IR value requires a string")
    return make_value(ValueType.Name, "NameValue", value)
end

---构造 String 类型属性值。
---@param value string 待写入 IR 的普通文本。
---@return SekiroAnimIRValue ir_value String 类型 IR Value。
function IRValue.String(value)
    assert(type(value) == "string", "String IR value requires a string")
    return make_value(ValueType.String, "StringValue", value)
end

---构造 SoftObjectPath 类型属性值。
---@param value string UE 对象软路径，例如动画序列资产路径。
---@return SekiroAnimIRValue ir_value SoftObjectPath 类型 IR Value。
function IRValue.SoftObjectPath(value)
    assert(type(value) == "string" and value ~= "", "SoftObjectPath IR value requires a non-empty string")
    return make_value(ValueType.SoftObjectPath, "SoftObjectPathValue", value)
end

---构造 SoftClassPath 类型属性值。
---@param value string UE 类软路径，例如父 AnimInstance 类路径。
---@return SekiroAnimIRValue ir_value SoftClassPath 类型 IR Value。
function IRValue.SoftClassPath(value)
    assert(type(value) == "string" and value ~= "", "SoftClassPath IR value requires a non-empty string")
    return make_value(ValueType.SoftClassPath, "SoftClassPathValue", value)
end

---构造 Struct 类型属性值，由目标 FStructProperty 解析 UE 确定性文本。
---插件不解释项目语义；导入器会先在临时结构体中完整解析，成功后才写入节点。
---@param value string UE 结构体文本，例如 `(Min=0.0,Max=1.0)`。
---@return SekiroAnimIRValue ir_value Struct 类型 IR Value。
function IRValue.Struct(value)
    assert(type(value) == "string" and value ~= "", "Struct IR value requires a non-empty string")
    return make_value(ValueType.Struct, "StructValue", value)
end

---按照节点注册契约把普通 Lua 值转换为显式类型 IR Value。
---该入口只在编译期处理节点属性赋值，不执行运行时隐式类型转换。
---@param value_type SekiroAnimIRValueType 节点注册表声明的属性类型。
---@param value boolean|number|string Lua 动画蓝图直接赋给节点属性的值。
---@return SekiroAnimIRValue ir_value 可交给 C++ 导入器的显式类型值。
function IRValue.From(value_type, value)
    local constructors = {
        [ValueType.Bool] = IRValue.Bool,
        [ValueType.Integer] = IRValue.Integer,
        [ValueType.Enum] = IRValue.Enum,
        [ValueType.Float] = IRValue.Float,
        [ValueType.Name] = IRValue.Name,
        [ValueType.String] = IRValue.String,
        [ValueType.SoftObjectPath] = IRValue.SoftObjectPath,
        [ValueType.SoftClassPath] = IRValue.SoftClassPath,
        [ValueType.Struct] = IRValue.Struct,
    }
    local constructor = constructors[value_type]
    assert(constructor ~= nil, string.format("Unsupported IR value type '%s'", tostring(value_type)))
    return constructor(value)
end

---根据普通 Lua 标量生成类型化 IR Value，供反射节点属性使用。
---字符串默认保持 String；需要明确的 FName、对象或类路径时仍可直接传入对应 IRValue 构造结果。
---@param value boolean|number|string|SekiroAnimIRValue 待转换的 Lua 标量或已类型化 IR Value。
---@return SekiroAnimIRValue ir_value 可由 C++ 反射属性写入器消费的类型化值。
function IRValue.Infer(value)
    if type(value) == "table" and value.Type ~= nil then
        return value
    end
    if type(value) == "boolean" then
        return IRValue.Bool(value)
    end
    if type(value) == "number" then
        if value % 1 == 0 then
            return IRValue.Integer(value)
        end
        return IRValue.Float(value)
    end
    if type(value) == "string" then
        return IRValue.String(value)
    end
    error(string.format(
        "Reflection Property requires boolean, number, string or typed IRValue, got '%s'",
        type(value)))
end

return IRValue
