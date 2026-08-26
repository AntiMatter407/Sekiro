-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- AnimGraph IR（中间表示，Intermediate Representation）的稳定命名和 Lua 源码定位工具。
-- 这里定义的 ID 规则属于编译器前端，业务动画蓝图无需手工拼接路径。
---@class IRSchema
local IRSchema = {}

IRSchema.MaxLayoutCoordinate = 1000000 -- 限制画布坐标规模，避免意外数值使编辑器视图失去精度。

local lua_reserved_words = {
    ["and"] = true,
    ["break"] = true,
    ["do"] = true,
    ["else"] = true,
    ["elseif"] = true,
    ["end"] = true,
    ["false"] = true,
    ["for"] = true,
    ["function"] = true,
    ["goto"] = true,
    ["if"] = true,
    ["in"] = true,
    ["local"] = true,
    ["nil"] = true,
    ["not"] = true,
    ["or"] = true,
    ["repeat"] = true,
    ["return"] = true,
    ["then"] = true,
    ["true"] = true,
    ["until"] = true,
    ["while"] = true,
}

---校验用于生成稳定 ID 的语义名称，禁止路径分隔符破坏层级结构。
---@param name string 待校验的 Layer、Graph、Node 或 State 名称。
---@param kind string 错误消息中显示的实体类型。
---@return string valid_name 已验证且可安全写入稳定 ID 的名称。
function IRSchema.RequireSemanticName(name, kind)
    assert(type(name) == "string" and name ~= "", string.format("%s name must not be empty", kind or "IR entity"))
    assert(string.find(name, "/", 1, true) == nil, string.format("%s name '%s' must not contain '/'", kind or "IR entity", name))
    return name
end

---校验会参与 Lua 规则函数名生成的语义名称，确保生成结果可以直接作为 Lua 标识符使用。
---@param name string 待校验的 StateMachine Node、Transition Key 或规则函数名称。
---@param kind string 错误消息中显示的实体类型。
---@return string valid_name 已验证为 Lua 标识符的名称。
function IRSchema.RequireLuaIdentifier(name, kind)
    local valid_name = IRSchema.RequireSemanticName(name, kind)
    assert(string.match(valid_name, "^[A-Za-z_][A-Za-z0-9_]*$") ~= nil, string.format(
        "%s name '%s' must be a valid Lua identifier",
        kind or "IR entity",
        valid_name))
    assert(lua_reserved_words[valid_name] ~= true, string.format(
        "%s name '%s' must not be a Lua reserved word",
        kind or "IR entity",
        valid_name))
    return valid_name
end

---校验供 AnimBlueprint 顶层契约使用的 UE 资产对象路径，不允许磁盘路径、子对象或简写包路径。
---该前置检查只验证 Lua 可稳定表达的规范形态；资产存在性和 UObject 类型由后续 NodeFactory 解析。
---@param path string 待校验的 UE 顶层资产对象路径，例如 /Game/Characters/Hero/SK_Hero.SK_Hero。
---@param kind string 错误消息中显示的资产用途。
---@return string valid_path 已验证为“包路径.对象名”顶层资产形式的路径。
function IRSchema.RequireAssetObjectPath(path, kind)
    local asset_kind = kind or "Asset"
    assert(type(path) == "string" and path ~= "", string.format("%s path must not be empty", asset_kind))
    assert(string.sub(path, 1, 1) == "/", string.format("%s path '%s' must start with '/'", asset_kind, path))
    assert(string.find(path, "\\", 1, true) == nil, string.format(
        "%s path '%s' must not contain a disk path separator",
        asset_kind,
        path))
    assert(string.find(path, ":", 1, true) == nil, string.format(
        "%s path '%s' must reference a top-level asset",
        asset_kind,
        path))

    local package_path, object_name = string.match(path, "^(/.+)%.([^%./]+)$")
    assert(package_path ~= nil and object_name ~= nil, string.format(
        "%s path '%s' must use the canonical '/Package/Asset.Asset' form",
        asset_kind,
        path))
    assert(string.lower(string.sub(path, -7)) ~= ".uasset", string.format(
        "%s path '%s' must not use a .uasset file path",
        asset_kind,
        path))
    return path
end

---校验 UE 类软路径，支持原生类和 Blueprint GeneratedClass 的规范对象路径。
---该格式必须为“/Package/Object.ObjectName”，例如 /Script/Engine.AnimInstance
---或 /Game/Animation/ABP_Layers.ABP_Layers_C；类是否存在及是否派生自预期基类由 C++ 生成器校验。
---@param path string 待校验的原生类或 Blueprint GeneratedClass 软路径。
---@param kind string 错误消息中显示的类用途。
---@return string valid_path 已规范化验证、可写入 FSoftClassPath 的对象路径。
function IRSchema.RequireClassObjectPath(path, kind)
    return IRSchema.RequireAssetObjectPath(path, kind or "Class")
end

---校验并规范化 UE Graph 画布的精确像素坐标。
---允许 Lua 以 120 或 120.0 表达整数，但拒绝 NaN、无穷大、小数和超出合理画布范围的值。
---@param value number 待校验的 X 或 Y 像素坐标。
---@param axis_name string 错误消息中显示的坐标轴名称。
---@return number coordinate 可稳定导出为 C++ int32 的有限整数坐标。
function IRSchema.RequireLayoutCoordinate(value, axis_name)
    assert(type(value) == "number", string.format("Layout %s must be a number", axis_name))
    local coordinate = math.tointeger(value)
    assert(coordinate ~= nil, string.format("Layout %s must be a finite integer", axis_name))
    assert(math.abs(coordinate) <= IRSchema.MaxLayoutCoordinate, string.format(
        "Layout %s must be between -%d and %d",
        axis_name,
        IRSchema.MaxLayoutCoordinate,
        IRSchema.MaxLayoutCoordinate))
    return coordinate
end

---在父级稳定 ID 下追加实体类别和语义名。
---@param parent_id string 已验证的父实体稳定 ID，可为空字符串。
---@param category string 当前实体在 IR 中的类别名称。
---@param name string 当前实体的语义名称。
---@return string stable_id 可重复生成的稳定 ID。
function IRSchema.MakeStableId(parent_id, category, name)
    local valid_category = IRSchema.RequireSemanticName(category, "category")
    local valid_name = IRSchema.RequireSemanticName(name, valid_category)
    if parent_id == nil or parent_id == "" then
        return valid_category .. "/" .. valid_name
    end

    return parent_id .. "/" .. valid_category .. "/" .. valid_name
end

---捕获调用 DSL 的 Lua 行号；debug 库不可用时仍返回结构完整的未知位置。
---@param module_name string 当前动画蓝图的 require 模块名。
---@param stack_level number|nil 从本函数开始计算的 Lua 调用栈层级，默认读取直接调用者。
---@return SekiroAnimIRSourceLocation source_location 与 C++ FSekiroAnimIRSourceLocation 对应的定位表。
function IRSchema.CaptureSourceLocation(module_name, stack_level)
    local line = 0
    if debug ~= nil and type(debug.getinfo) == "function" then
        local source_info = debug.getinfo(stack_level or 2, "l")
        if source_info ~= nil then
            line = source_info.currentline or 0
        end
    end

    return {
        LuaModule = module_name or "",
        Line = line,
        Column = 0,
    }
end

return IRSchema
