-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- SequencePlayer 的 Lua 编译期节点类。
-- 它声明 UE 原生 SequencePlayer 所需参数，真正的资产播放仍由后续 NodeFactory 生成的原生节点完成。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaSequencePlayerNodeConfig: LuaAnimNodeConfig

---@class LuaSequencePlayerNode: LuaAnimNode
---@field Pose LuaAnimPin 动画姿势输出 Pin。
---@field Sequence string 动画序列资产软路径；必须由业务 Graph 直接赋值。
---@field bLoopAnimation boolean 是否循环播放；未赋值时使用 UE 节点默认值。
---@field PlayRate number 播放倍率；未赋值时使用 UE 节点默认值。
---@field StartPosition number 起始播放时间，单位秒；未赋值时使用 UE 节点默认值。
local LuaSequencePlayerNode = LuaAnimNode:Extend("LuaSequencePlayerNode")

---初始化空的原生 SequencePlayer 声明；动画资产和播放属性由业务 Graph 像编辑节点详情面板一样直接赋值。
---@param config LuaSequencePlayerNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化节点身份和 Pose Pin。
function LuaSequencePlayerNode:Initialize(config)
    config.NodeType = "SequencePlayer"
    LuaAnimNode.Initialize(self, config)
end

return LuaSequencePlayerNode
