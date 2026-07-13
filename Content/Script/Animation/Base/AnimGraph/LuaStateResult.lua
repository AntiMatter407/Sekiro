-- Lua 动画状态图输出节点。
-- 该类对应 UE FAnimNode_StateResult；它继承 LuaRoot，并作为每个状态内部 AnimGraph 的固定输出边界。
local class = require("Animation.Base.Class")
local LuaRoot = require("Animation.Base.AnimGraph.LuaRoot")

---@class LuaStateResult : LuaRoot
---@field StateName string 所属状态名称，仅用于拓扑索引和调试。
local LuaStateResult = class("LuaStateResult", LuaRoot, {
    NodeType = "StateResult",
    StateName = "",
})

---创建 StateResult，并保留所属状态名称供状态机建立稳定 StatePoseLinks。
---@param config table|nil 可选实例配置；StateName 指定所属状态。
---@return nil 该构造入口只初始化实例字段。
function LuaStateResult:__init(config)
    LuaRoot.__init(self, config)
    self.StateName = tostring(config and config.StateName or self.StateName or "")
end

---确保当前 StateResult 拥有独立 C++ 原生节点，而不是塌缩为 Result 上游播放器句柄。
---@return boolean ready 原生 StateResult 已存在或创建成功时为 true。
function LuaStateResult:EnsureNativeNode()
    if self:IsNativeNodeValid() then
        return true
    end

    if self.Owner == nil or self.NodeName == "" then
        return false
    end

    self.NativePoseLink = self.Owner:CallCpp(
        "CreateLuaStateResult",
        self.LayerName,
        self.NodeName,
        self.StateName)
    return self:IsNativeNodeValid()
end

---求值状态图上游节点，并把 Result 的原生连接提交给独立 C++ StateResult。
---@return boolean evaluated StateResult 和输入节点均有效且原生连接设置成功时为 true。
function LuaStateResult:Evaluate()
    if self.Result:Evaluate() ~= true or self:EnsureNativeNode() ~= true then
        return false
    end

    local input_pose_link = self.Result:GetNativePoseLink()
    if input_pose_link == nil then
        return false
    end

    return self.Owner:CallCpp(
        "SetLuaStateResultInput",
        self.NativePoseLink,
        input_pose_link) == true
end

---读取 StateResult 自身的原生 Pose 句柄，供状态机 StatePoseLinks 连接。
---@return SekiroNativePoseLink|nil native_pose_link 独立 StateResult 句柄；尚未创建时为 nil。
function LuaStateResult:GetNativePoseLink()
    return self.NativePoseLink
end

return LuaStateResult
