-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Local To Component Space 的 Lua 编译期节点类型。
-- 节点只标注姿势空间边界，实际骨骼空间转换由 UE 原生 AnimNode 执行。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaLocalToComponentSpaceNodeConfig: LuaAnimNodeConfig

---@class LuaLocalToComponentSpaceNode: LuaAnimNode
---@field LocalPose LuaAnimPin 局部空间姿势输入 Pin。
---@field ComponentPose LuaAnimPin 组件空间姿势输出 Pin。
local LuaLocalToComponentSpaceNode = LuaAnimNode:Extend("LuaLocalToComponentSpaceNode")

---初始化 Local To Component Space 节点身份与契约 Pin。
---@param config LuaLocalToComponentSpaceNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点。
function LuaLocalToComponentSpaceNode:Initialize(config)
    config.NodeType = "LocalToComponentSpace"
    LuaAnimNode.Initialize(self, config)
end

return LuaLocalToComponentSpaceNode
