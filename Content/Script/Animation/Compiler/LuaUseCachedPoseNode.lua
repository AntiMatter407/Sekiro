-- Use Cached Pose 的 Lua 编译期节点类型。
-- 节点按 CacheName 引用同一主 Pose Graph 中的原生 Save Cached Pose 节点。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaUseCachedPoseNodeConfig: LuaAnimNodeConfig

---@class LuaUseCachedPoseNode: LuaAnimNode
---@field Pose LuaAnimPin 已缓存姿势输出 Pin。
---@field CacheName string 目标 Save Cached Pose 的缓存名。
local LuaUseCachedPoseNode = LuaAnimNode:Extend("LuaUseCachedPoseNode")

---初始化 Use Cached Pose 节点身份，Pose Pin 和 CacheName 契约由注册表提供。
---@param config LuaUseCachedPoseNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点。
function LuaUseCachedPoseNode:Initialize(config)
    config.NodeType = "UseCachedPose"
    LuaAnimNode.Initialize(self, config)
end

return LuaUseCachedPoseNode
