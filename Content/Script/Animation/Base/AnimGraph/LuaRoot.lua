-- Lua AnimGraph 根节点基类。
-- 该类对应 UE FAnimNode_Root：自身不生成 Pose，只通过 Result PoseLink 转发上游节点输出。
local class = require("Animation.Base.Class")
local LuaAnimNodeBase = require("Animation.Base.AnimGraph.LuaAnimNodeBase")
local LuaPoseLink = require("Animation.Base.AnimGraph.LuaPoseLink")

---@class LuaRoot : LuaAnimNodeBase
---@field Result LuaPoseLink 根节点唯一的本地空间 Pose 输入。
local LuaRoot = class("LuaRoot", LuaAnimNodeBase, {
    NodeType = "Root",
    Result = nil,
})

---创建每个根节点实例独占的 Result，避免类级 PoseLink 被多个状态图共享。
---@param config table|nil 可选实例配置；ResultSource 可指定初始上游节点或 PoseLink。
---@return nil 该构造入口只初始化实例字段。
function LuaRoot:__init(config)
    self.Result = LuaPoseLink()
    local result_source = config and config.ResultSource or nil
    if result_source ~= nil then
        self:SetResult(result_source)
    end
end

---把根节点 Result 连接到动画节点或另一个 PoseLink 的源节点。
---@param pose_source LuaAnimNodeBase|LuaPoseLinkBase|nil 状态图或动画图的最终 Pose 来源。
---@return boolean linked 输入可解析为动画节点并完成连接时为 true。
function LuaRoot:SetResult(pose_source)
    local linked_node = pose_source
    if type(pose_source) == "table" and pose_source.IsLuaPoseLink == true then
        linked_node = pose_source:GetLinkNode()
    end
    if linked_node == nil or linked_node.IsLuaAnimNode ~= true then
        return false
    end

    self.Result:Link(linked_node)
    return true
end

---读取根节点 Result 当前连接的动画节点。
---@return LuaAnimNodeBase|nil linked_node 上游动画节点；Result 尚未连接时为 nil。
function LuaRoot:GetResultNode()
    return self.Result:GetLinkNode()
end

---将初始化生命周期转发到 Result，语义对应 FAnimNode_Root::Initialize_AnyThread。
---@param context table|userdata|nil Lua 动画图初始化上下文。
---@return boolean initialized Result 已连接并完成初始化时为 true。
function LuaRoot:Initialize(context)
    return self.Result:Initialize(context)
end

---将骨骼缓存生命周期转发到 Result，语义对应 FAnimNode_Root::CacheBones_AnyThread。
---@param context table|userdata|nil Lua 动画图骨骼缓存上下文。
---@return boolean cached Result 已连接并完成骨骼缓存时为 true。
function LuaRoot:CacheBones(context)
    return self.Result:CacheBones(context)
end

---将图更新生命周期转发到 Result，根节点不额外修改上游权重或播放时间。
---@param context table|userdata|nil Lua 动画图更新上下文。
---@param delta_seconds number|nil 本帧增量时间，单位为秒。
---@return boolean updated Result 已连接并完成更新时为 true。
function LuaRoot:UpdateNode(context, delta_seconds)
    return self.Result:Update(context, delta_seconds)
end

---求值 Result，并缓存它最终解析出的 C++ 原生 Pose 句柄。
---@return boolean evaluated Result 成功生成可发布原生 Pose 时为 true。
function LuaRoot:Evaluate()
    if self.Result:Evaluate() ~= true then
        self.NativePoseLink = nil
        return false
    end

    self.NativePoseLink = self.Result:GetNativePoseLink()
    return self.NativePoseLink ~= nil
end

---读取 Result 最终解析出的 C++ 原生 Pose 句柄。
---@return SekiroNativePoseLink|nil native_pose_link 上游节点的原生句柄；求值失败时为 nil。
function LuaRoot:GetNativePoseLink()
    return self.NativePoseLink or self.Result:GetNativePoseLink()
end

---图代次变化时同时清理根节点和上游节点缓存的原生句柄。
---@return nil 该操作只清理原生句柄缓存。
function LuaRoot:InvalidateNativeNode()
    self.NativePoseLink = nil
    local result_node = self:GetResultNode()
    if result_node ~= nil and type(result_node.InvalidateNativeNode) == "function" then
        result_node:InvalidateNativeNode()
    end
end

return LuaRoot
