-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Component To Local Space 的 Lua 编译期节点类型。
-- 节点把骨骼控制节点输出重新转换为普通 Pose Graph 可继续消费的局部空间姿势。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaComponentToLocalSpaceNodeConfig: LuaAnimNodeConfig

---@class LuaComponentToLocalSpaceNode: LuaAnimNode
---@field ComponentPose LuaAnimPin 组件空间姿势输入 Pin。
---@field Pose LuaAnimPin 局部空间姿势输出 Pin。
local LuaComponentToLocalSpaceNode = LuaAnimNode:Extend("LuaComponentToLocalSpaceNode")

---初始化 Component To Local Space 节点身份与契约 Pin。
---@param config LuaComponentToLocalSpaceNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点。
function LuaComponentToLocalSpaceNode:Initialize(config)
    config.NodeType = "ComponentToLocalSpace"
    LuaAnimNode.Initialize(self, config)
end

return LuaComponentToLocalSpaceNode
