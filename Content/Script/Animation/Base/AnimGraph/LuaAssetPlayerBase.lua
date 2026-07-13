-- Lua AnimGraph 资产播放器节点基类。
-- 该类对应 UE FAnimNode_AssetPlayerBase，集中保存资源播放器共有的时间、权重和循环状态。
local class = require("Animation.Base.Class")
local LuaAnimNodeBase = require("Animation.Base.AnimGraph.LuaAnimNodeBase")

---@class LuaAssetPlayerBase : LuaAnimNodeBase
---@field InternalTimeAccumulator number 当前播放器累计时间，单位为秒；权威时间仍由 C++ 原生节点维护。
---@field BlendWeight number 当前节点在图中的相关性权重，范围为 0..1。
local LuaAssetPlayerBase = class("LuaAssetPlayerBase", LuaAnimNodeBase, {
    NodeType = "AssetPlayerBase",
    InternalTimeAccumulator = 0,
    BlendWeight = 0,
})

---读取当前播放器引用的动画资源；抽象基类没有具体资源。
---@return string|table|userdata|nil animation_asset 当前动画资源引用；基类固定返回 nil。
function LuaAssetPlayerBase:GetAnimationAsset()
    return nil
end

return LuaAssetPlayerBase
