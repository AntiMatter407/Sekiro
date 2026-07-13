-- Lua AnimGraph Pose 连接基类。
-- 该类对应 UE FPoseLinkBase：它不是动画节点，只保存并转发到一个 LuaAnimNodeBase 派生节点。
local class = require("Animation.Base.Class")

---@class LuaPoseLinkBase
---@field IsLuaPoseLink boolean 标识实例是 Pose 连接而不是动画节点。
---@field LinkedNode LuaAnimNodeBase|nil 当前连接的源动画节点，对应 C++ FPoseLinkBase::LinkedNode。
---@field LinkID integer Lua 图内连接编号；第一阶段只用于调试，不参与 C++ 节点寻址。
---@field SourceLinkID integer Lua 图内源连接编号；第一阶段只用于调试。
local LuaPoseLinkBase = class("LuaPoseLinkBase", nil, {
    IsLuaPoseLink = true,
    LinkedNode = nil,
    LinkID = -1,
    SourceLinkID = -1,
})

---把 PoseLink 连接到一个 LuaAnimNodeBase 派生节点。
---@param linked_node LuaAnimNodeBase|nil 要连接的动画节点；nil 表示断开连接。
---@return LuaPoseLinkBase pose_link 完成连接的当前 PoseLink。
function LuaPoseLinkBase:Link(linked_node)
    if linked_node ~= nil and linked_node.IsLuaAnimNode ~= true then
        return self
    end

    self.LinkedNode = linked_node
    return self
end

---使用与 C++ FPoseLink::SetLinkNode 接近的命名设置运行时连接节点。
---该方法与 Link 语义相同，供通用 AnimGraph 构建器避免感知 Lua 特有命名。
---@param linked_node LuaAnimNodeBase|nil 要连接的动画节点；nil 表示断开连接。
---@return LuaPoseLinkBase pose_link 完成连接的当前 PoseLink。
function LuaPoseLinkBase:SetLinkNode(linked_node)
    return self:Link(linked_node)
end

---检查当前 PoseLink 是否已经连接到一个有效动画节点。
---@return boolean linked LinkedNode 存在且明确标识为 Lua AnimNode 时为 true。
function LuaPoseLinkBase:IsLinked()
    return self.LinkedNode ~= nil and self.LinkedNode.IsLuaAnimNode == true
end

---断开当前 PoseLink 与源节点的连接。
---@return nil 该操作只清理连接，不返回业务值。
function LuaPoseLinkBase:Unlink()
    self.LinkedNode = nil
end

---读取当前 PoseLink 连接的动画节点。
---@return LuaAnimNodeBase|nil linked_node 当前源动画节点；未连接时为 nil。
function LuaPoseLinkBase:GetLinkNode()
    return self.LinkedNode
end

---把初始化阶段转发给连接节点，语义对应 FPoseLinkBase::Initialize。
---@param context table|userdata|nil Lua 动画图初始化上下文。
---@return boolean initialized 已连接节点并成功转发时为 true。
function LuaPoseLinkBase:Initialize(context)
    if self.LinkedNode == nil then
        return false
    end

    return self.LinkedNode:Initialize(context) ~= false
end

---把骨骼缓存阶段转发给连接节点，语义对应 FPoseLinkBase::CacheBones。
---@param context table|userdata|nil Lua 动画图骨骼缓存上下文。
---@return boolean cached 已连接节点并成功转发时为 true。
function LuaPoseLinkBase:CacheBones(context)
    if self.LinkedNode == nil then
        return false
    end

    return self.LinkedNode:CacheBones(context) ~= false
end

---把图更新阶段转发给连接节点，语义对应 FPoseLinkBase::Update。
---@param context table|userdata|nil Lua 动画图更新上下文。
---@param delta_seconds number|nil 本帧增量时间，单位为秒。
---@return boolean updated 已连接节点并成功转发时为 true。
function LuaPoseLinkBase:Update(context, delta_seconds)
    if self.LinkedNode == nil then
        return false
    end

    return self.LinkedNode:UpdateNode(context, delta_seconds) ~= false
end

---读取连接节点最终映射到 C++ Pose Graph 的原生 PoseLink。
---@return SekiroNativePoseLink|nil native_pose_link C++ 可消费的原生连接句柄；连接无效时为 nil。
function LuaPoseLinkBase:GetNativePoseLink()
    if self.LinkedNode == nil then
        return nil
    end

    return self.LinkedNode:GetNativePoseLink()
end

return LuaPoseLinkBase
