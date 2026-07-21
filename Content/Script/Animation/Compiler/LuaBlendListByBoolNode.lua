-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaBlendListByBoolNodeConfig: LuaAnimNodeConfig

---@class LuaBlendListByBoolNode: LuaAnimNode
---@field TruePose LuaAnimPin 条件为 true 时使用的 Pose。
---@field FalsePose LuaAnimPin 条件为 false 时使用的 Pose。
---@field ActiveValue LuaAnimPin Bool 选择条件。
---@field Pose LuaAnimPin 混合后的 Pose。
---@field BlendTime number 原生交叉混合时长，单位秒。
local LuaBlendListByBoolNode = LuaAnimNode:Extend("LuaBlendListByBoolNode")

---创建原生 BlendListByBool 声明。
---@param config LuaBlendListByBoolNodeConfig Graph 与节点身份。
---@return nil result 仅初始化编译期节点。
function LuaBlendListByBoolNode:Initialize(config)
    config.NodeType = "BlendListByBool"
    LuaAnimNode.Initialize(self, config)
end

return LuaBlendListByBoolNode
