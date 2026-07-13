-- Lua AnimGraph 动画节点基类。
-- 该类对应 UE FAnimNode_Base；SequencePlayer、StateMachine、Blend 等运行时节点都必须继承它。
local class = require("Animation.Base.Class")
local LuaPoseLink = require("Animation.Base.AnimGraph.LuaPoseLink")

---@class LuaAnimNodeBase
---@field IsLuaAnimNode boolean 标识实例是 AnimGraph 节点而不是 Pose 连接。
---@field NodeType string 动画节点类型名称，用于调试和派生类识别。
---@field Owner table|nil 拥有该节点的 Lua 动画蓝图或状态机。
---@field NodeName string 节点在所属动画层内的稳定名称。
---@field LayerName string 节点所属 Lua 动画层名称。
---@field NativePoseLink SekiroNativePoseLink|nil C++ Pose Graph 为该节点提供的原生输出连接句柄。
---@field OutputPoseLink LuaPoseLink|nil 指向当前节点的 Lua 输出连接。
---@field BlendTime number|nil 当前节点成为输出目标时使用的过渡时间，单位为秒。
local LuaAnimNodeBase = class("LuaAnimNodeBase", nil, {
    IsLuaAnimNode = true,
    NodeType = "AnimNodeBase",
    Owner = nil,
    NodeName = "",
    LayerName = "Default",
    NativePoseLink = nil,
    OutputPoseLink = nil,
    BlendTime = nil,
})

---绑定节点所有者、稳定节点名和动画层。
---@param owner table 拥有该节点的 Lua 动画蓝图或状态机。
---@param node_name string 节点在动画层内的稳定名称。
---@param layer_name string|nil 节点所属动画层；缺失时读取所有者默认层。
---@return LuaAnimNodeBase node 完成绑定的当前动画节点。
function LuaAnimNodeBase:Bind(owner, node_name, layer_name)
    self.Owner = owner
    self.NodeName = tostring(node_name or "")
    self.LayerName = layer_name
        or (owner and owner.GetLayerName and owner:GetLayerName())
        or "Default"
    return self
end

---取得连接到当前节点输出的 LuaPoseLink，供根图、状态或其他节点持有。
---@return LuaPoseLink output_pose_link 指向当前节点且可被消费者缓存的输出连接。
function LuaAnimNodeBase:GetOutputPoseLink()
    if self.OutputPoseLink == nil then
        self.OutputPoseLink = LuaPoseLink()
        self.OutputPoseLink:Link(self)
    end

    return self.OutputPoseLink
end

---读取当前节点映射到 C++ Pose Graph 的原生 PoseLink。
---@return SekiroNativePoseLink|nil native_pose_link C++ 可消费句柄；节点尚未创建原生实现时为 nil。
function LuaAnimNodeBase:GetNativePoseLink()
    return self.NativePoseLink
end

---判断当前节点的 C++ 原生 PoseLink 是否仍属于当前图代次。
---@return boolean valid 原生句柄存在且 C++ 仍能解析时为 true。
function LuaAnimNodeBase:IsNativeNodeValid()
    if self.Owner == nil or self.NativePoseLink == nil then
        return false
    end

    return self.Owner:CallCpp("IsLuaPoseLinkValid", self.NativePoseLink) == true
end

---执行节点初始化生命周期；派生节点需要初始化额外状态时 override。
---@param _context table|userdata|nil Lua 动画图初始化上下文。
---@return boolean initialized 基类固定返回 true。
function LuaAnimNodeBase:Initialize(_context)
    return true
end

---执行节点骨骼缓存生命周期；派生节点需要骨骼引用时 override。
---@param _context table|userdata|nil Lua 动画图骨骼缓存上下文。
---@return boolean cached 基类固定返回 true。
function LuaAnimNodeBase:CacheBones(_context)
    return true
end

---执行节点图更新生命周期；Lua 只在游戏线程配置参数，动画线程读取 C++ 快照。
---@param _context table|userdata|nil Lua 动画图更新上下文。
---@param _delta_seconds number|nil 本帧增量时间，单位为秒。
---@return boolean updated 基类固定返回 true。
function LuaAnimNodeBase:UpdateNode(_context, _delta_seconds)
    return true
end

---求值节点并生成原生 PoseLink；抽象基类没有具体姿势实现。
---@return boolean evaluated 基类固定返回 false，派生节点必须 override。
function LuaAnimNodeBase:Evaluate()
    return false
end

---读取节点成为输出目标时使用的过渡时间。
---@param state_name string|nil 当前或目标状态名称，用于读取状态机 AnimationSettings。
---@return number transition_time 非负过渡时间，单位为秒。
function LuaAnimNodeBase:GetTransitionTime(state_name)
    if self.BlendTime ~= nil then
        return math.max(tonumber(self.BlendTime) or 0, 0)
    end

    if self.Owner ~= nil and type(self.Owner.GetPoseBlendTime) == "function" then
        return math.max(tonumber(self.Owner:GetPoseBlendTime(state_name, self)) or 0, 0)
    end

    return 0
end

---丢弃当前节点的原生句柄；图代次重建后由派生节点重新创建。
---@return nil 该操作只清理原生状态，不返回业务值。
function LuaAnimNodeBase:InvalidateNativeNode()
    self.NativePoseLink = nil
end

return LuaAnimNodeBase
