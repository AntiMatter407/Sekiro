-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Slot 的 Lua 编译期节点类型。
-- 节点平时透传 Source；运行时由 AnimInstance 在具名 Slot 上播放 Montage 或动态 Montage。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaSlotNodeConfig: LuaAnimNodeConfig

---@class LuaSlotNode: LuaAnimNode
---@field Source LuaAnimPin 未播放 Slot 动画时持续输出的基础姿势输入 Pin。
---@field Pose LuaAnimPin Slot 合成后的姿势输出 Pin。
---@field SlotName string Skeleton Slot 名称，必须由调用方显式设置。
---@field bAlwaysUpdateSourcePose boolean 是否在 Slot 完全覆盖时仍更新基础姿势。
local LuaSlotNode = LuaAnimNode:Extend("LuaSlotNode")

---初始化 Slot 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaSlotNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不控制运行时 Montage。
function LuaSlotNode:Initialize(config)
    config.NodeType = "Slot"
    LuaAnimNode.Initialize(self, config)
end

return LuaSlotNode
