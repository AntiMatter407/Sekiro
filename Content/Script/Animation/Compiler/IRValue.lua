-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- 构造 AnimGraph IR 的显式类型化属性值。
-- 每个构造函数只写入自身类型对应的字段，C++ 导入器不需要从字符串内容猜测类型。
---@class IRValue
local IRValue = {}

---构造带稳定类型标签的 IR Value 表。
---@param value_type SekiroAnimIRValueType 与 ESekiroAnimIRValueType 对应的稳定类型名。
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
    return make_value("Bool", "BoolValue", value)
end

---构造 Integer 类型属性值。
---@param value number 待写入 IR 的整数值。
---@return SekiroAnimIRValue ir_value Integer 类型 IR Value。
function IRValue.Integer(value)
    assert(type(value) == "number" and value % 1 == 0, "Integer IR value requires an integer")
    return make_value("Integer", "IntegerValue", value)
end

---构造 Float 类型属性值。
---@param value number 待写入 IR 的浮点值。
---@return SekiroAnimIRValue ir_value Float 类型 IR Value。
function IRValue.Float(value)
    assert(type(value) == "number", "Float IR value requires a number")
    return make_value("Float", "FloatValue", value)
end

---构造 Name 类型属性值。
---@param value string 待写入 FName 的稳定文本。
---@return SekiroAnimIRValue ir_value Name 类型 IR Value。
function IRValue.Name(value)
    assert(type(value) == "string", "Name IR value requires a string")
    return make_value("Name", "NameValue", value)
end

---构造 String 类型属性值。
---@param value string 待写入 IR 的普通文本。
---@return SekiroAnimIRValue ir_value String 类型 IR Value。
function IRValue.String(value)
    assert(type(value) == "string", "String IR value requires a string")
    return make_value("String", "StringValue", value)
end

---构造 SoftObjectPath 类型属性值。
---@param value string UE 对象软路径，例如动画序列资产路径。
---@return SekiroAnimIRValue ir_value SoftObjectPath 类型 IR Value。
function IRValue.SoftObjectPath(value)
    assert(type(value) == "string" and value ~= "", "SoftObjectPath IR value requires a non-empty string")
    return make_value("SoftObjectPath", "SoftObjectPathValue", value)
end

---构造 SoftClassPath 类型属性值。
---@param value string UE 类软路径，例如父 AnimInstance 类路径。
---@return SekiroAnimIRValue ir_value SoftClassPath 类型 IR Value。
function IRValue.SoftClassPath(value)
    assert(type(value) == "string" and value ~= "", "SoftClassPath IR value requires a non-empty string")
    return make_value("SoftClassPath", "SoftClassPathValue", value)
end

---按照节点注册契约把普通 Lua 值转换为显式类型 IR Value。
---该入口只在编译期处理节点属性赋值，不执行运行时隐式类型转换。
---@param value_type SekiroAnimIRValueType 节点注册表声明的属性类型。
---@param value boolean|number|string Lua 动画蓝图直接赋给节点属性的值。
---@return SekiroAnimIRValue ir_value 可交给 C++ 导入器的显式类型值。
function IRValue.From(value_type, value)
    local constructors = {
        Bool = IRValue.Bool,
        Integer = IRValue.Integer,
        Float = IRValue.Float,
        Name = IRValue.Name,
        String = IRValue.String,
        SoftObjectPath = IRValue.SoftObjectPath,
        SoftClassPath = IRValue.SoftClassPath,
    }
    local constructor = constructors[value_type]
    assert(constructor ~= nil, string.format("Unsupported IR value type '%s'", tostring(value_type)))
    return constructor(value)
end

return IRValue
