-- Lua 动画状态转换描述。
-- 该类对应 UE 烘焙转换数据；规则在游戏线程由 Lua 调用，Pose 交叉混合由 C++ 活动过渡链执行。
local class = require("Animation.Base.Class")

---@class LuaAnimationTransition
---@field PreviousState string 转换源状态；Any 表示全局转换。
---@field NextState string 转换目标状态。
---@field RuleName string CanEnter_<From>_<To> 规则函数名。
---@field CrossfadeDuration number 进入目标状态时的标准交叉混合时长，单位为秒。
---@field Priority integer 同一源状态下的稳定优先级，数值越小越先检查。
local LuaAnimationTransition = class("LuaAnimationTransition", nil, {
    PreviousState = "",
    NextState = "",
    RuleName = "",
    CrossfadeDuration = 0.0,
    Priority = 0,
})

---创建不可依赖 table 遍历顺序的转换描述。
---@param config table|nil 源状态、目标状态、规则名、混合时长和优先级配置。
---@return nil 该构造入口只初始化转换描述字段。
function LuaAnimationTransition:__init(config)
    local transition_config = config or {}
    self.PreviousState = tostring(transition_config.PreviousState or transition_config.From or "")
    self.NextState = tostring(transition_config.NextState or transition_config.To or "")
    self.RuleName = tostring(transition_config.RuleName or transition_config.Name or "")
    self.CrossfadeDuration = math.max(tonumber(transition_config.CrossfadeDuration) or 0, 0)
    self.Priority = math.floor(tonumber(transition_config.Priority) or 0)
end

---检查转换描述是否包含可调用的源、目标和规则名。
---@return boolean valid 三个稳定名称均非空时为 true。
function LuaAnimationTransition:IsValid()
    return self.PreviousState ~= ""
        and self.NextState ~= ""
        and self.RuleName ~= ""
end

return LuaAnimationTransition
