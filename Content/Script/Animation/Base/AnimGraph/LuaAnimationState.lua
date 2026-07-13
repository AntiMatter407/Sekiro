-- Lua 动画状态机的状态描述数据。
-- 该类对应 UE FBakedAnimationState：State 本身不是 AnimNode，真正输出 Pose 的是 StateResult。
local class = require("Animation.Base.Class")
local LuaStateResult = require("Animation.Base.AnimGraph.LuaStateResult")

---@class LuaAnimationState
---@field StateName string 状态稳定名称。
---@field StateIndex integer 状态在 StateList 和 StatePoseLinks 中的零基索引。
---@field StateResult LuaStateResult 状态内部 AnimGraph 的固定输出根节点。
---@field Transitions LuaAnimationTransition[] 从该状态出发的转换描述。
---@field bAlwaysResetOnEntry boolean 每次重新进入时是否强制重置状态图播放器。
local LuaAnimationState = class("LuaAnimationState", nil, {
    StateName = "",
    StateIndex = -1,
    StateResult = nil,
    Transitions = nil,
    bAlwaysResetOnEntry = false,
})

---创建状态描述和独占 StateResult；状态只保存数据，不参与 Pose 生命周期。
---@param config table|nil 状态名称、索引和重入策略配置。
---@return nil 该构造入口只初始化状态数据和 StateResult。
function LuaAnimationState:__init(config)
    local state_config = config or {}
    self.StateName = tostring(state_config.StateName or "")
    self.StateIndex = math.floor(tonumber(state_config.StateIndex) or -1)
    self.bAlwaysResetOnEntry = state_config.bAlwaysResetOnEntry == true
    self.Transitions = {}
    self.StateResult = LuaStateResult({
        StateName = self.StateName,
    })
end

---向状态追加一条稳定顺序的出边转换。
---@param transition LuaAnimationTransition 转换描述对象。
---@return boolean added 转换有效且尚未登记时为 true。
function LuaAnimationState:AddTransition(transition)
    if transition == nil then
        return false
    end

    for _, existing_transition in ipairs(self.Transitions) do
        if existing_transition.RuleName == transition.RuleName then
            return false
        end
    end

    table.insert(self.Transitions, transition)
    return true
end

---把状态图的 StateResult.Result 连接到具体动画节点或子状态机。
---@param pose_source LuaAnimNodeBase|LuaPoseLinkBase|nil 状态内部 AnimGraph 的最终输出来源。
---@return boolean linked StateResult 成功连接上游节点时为 true。
function LuaAnimationState:SetResult(pose_source)
    return self.StateResult:SetResult(pose_source)
end

return LuaAnimationState
