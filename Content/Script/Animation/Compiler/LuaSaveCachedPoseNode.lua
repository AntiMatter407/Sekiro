-- Save Cached Pose 的 Lua 编译期节点类型。
-- 节点只声明原生缓存入口和名字，Pose 缓存生命周期由 UE AnimInstance 管理。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaSaveCachedPoseNodeConfig: LuaAnimNodeConfig

---@class LuaSaveCachedPoseNode: LuaAnimNode
---@field Pose LuaAnimPin 待缓存姿势输入 Pin。
---@field CacheName string 当前 Pose Graph 内唯一的缓存名。
local LuaSaveCachedPoseNode = LuaAnimNode:Extend("LuaSaveCachedPoseNode")

---初始化 Save Cached Pose 节点身份，Pose Pin 和 CacheName 契约由注册表提供。
---@param config LuaSaveCachedPoseNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点。
function LuaSaveCachedPoseNode:Initialize(config)
    config.NodeType = "SaveCachedPose"
    LuaAnimNode.Initialize(self, config)
end

return LuaSaveCachedPoseNode
