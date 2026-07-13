-- Lua AnimGraph 本地空间 Pose 连接。
-- 该类对应 UE FPoseLink，在 FPoseLinkBase 的生命周期转发之外增加本地空间 Pose 求值入口。
local class = require("Animation.Base.Class")
local LuaPoseLinkBase = require("Animation.Base.AnimGraph.LuaPoseLinkBase")

---@class LuaPoseLink : LuaPoseLinkBase
local LuaPoseLink = class("LuaPoseLink", LuaPoseLinkBase, {})

---求值当前连接节点并返回是否生成了可发布的原生 PoseLink。
---@return boolean evaluated 源节点求值成功且提供原生 PoseLink 时为 true。
function LuaPoseLink:Evaluate()
    if self.LinkedNode == nil or self.LinkedNode:Evaluate() ~= true then
        return false
    end

    return self.LinkedNode:GetNativePoseLink() ~= nil
end

return LuaPoseLink
