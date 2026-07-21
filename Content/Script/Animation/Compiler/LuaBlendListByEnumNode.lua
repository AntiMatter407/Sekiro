-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaBlendListByEnumNodeConfig: LuaAnimNodeConfig
---@field EnumType string UEnum 资产或原生枚举对象路径。
---@field EnumEntries string[] 要显式生成 Pose Pin 的枚举项短名，最多八项。

---@class LuaBlendListByEnumNode: LuaAnimNode
---@field DefaultPose LuaAnimPin 未映射枚举值使用的 Pose。
---@field ActiveValue LuaAnimPin Enum 选择输入。
---@field Pose LuaAnimPin 混合后的 Pose。
---@field BlendTime number 原生交叉混合时长，单位秒。
local LuaBlendListByEnumNode = LuaAnimNode:Extend("LuaBlendListByEnumNode")

---创建原生 BlendListByEnum 声明，并保存需要暴露的枚举项。
---@param config LuaBlendListByEnumNodeConfig Graph、节点身份、枚举路径和枚举项。
---@return nil result 仅初始化编译期节点。
function LuaBlendListByEnumNode:Initialize(config)
    assert(type(config.EnumType) == "string" and config.EnumType ~= "", "BlendListByEnum requires EnumType")
    assert(type(config.EnumEntries) == "table" and #config.EnumEntries <= 8, "BlendListByEnum supports at most eight entries")
    config.NodeType = "BlendListByEnum"
    LuaAnimNode.Initialize(self, config)
    self.EnumType = config.EnumType
    self.EnumEntries = table.concat(config.EnumEntries, "|")
end

return LuaBlendListByEnumNode
