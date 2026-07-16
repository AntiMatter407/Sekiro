-- Inertialization 的 Lua 编译期节点类型。
-- 具名类型只为 Graph API 和 Rider 提供准确 Pin 补全，运行时仍由 UE 原生节点求值。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaInertializationNodeConfig: LuaAnimNodeConfig

---@class LuaInertializationNode: LuaAnimNode
---@field Source LuaAnimPin 待平滑姿势输入 Pin。
---@field Pose LuaAnimPin 惯性化后的姿势输出 Pin。
local LuaInertializationNode = LuaAnimNode:Extend("LuaInertializationNode")

---初始化 Inertialization 节点身份，具体 Pin 从 C++ 契约镜像生成。
---@param config LuaInertializationNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点。
function LuaInertializationNode:Initialize(config)
    config.NodeType = "Inertialization"
    LuaAnimNode.Initialize(self, config)
end

return LuaInertializationNode
