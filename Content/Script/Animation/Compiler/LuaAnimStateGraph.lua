-- UAnimationStateGraph 对应的 Lua 编译期 Graph。
-- State Graph 输出 Pose，但使用 StatePose GraphType 和 StateResult 根节点，与主 AnimGraph 明确区分。
local LuaAnimGraph = require("Animation.Compiler.LuaAnimGraph")

---@class LuaAnimStateGraphConfig: LuaAnimGraphConfig
---@field Id string 由所属 State 稳定 ID 派生的 Graph ID。

---@class LuaAnimStateGraph: LuaAnimGraph
local LuaAnimStateGraph = LuaAnimGraph:Extend("LuaAnimStateGraph")

---初始化 State 独占 Pose Graph，并选择原生 StateResult 语义的固定根节点。
---@param config LuaAnimStateGraphConfig 所属 Blueprint、Layer、State 名称和稳定 Graph ID。
---@return nil result 该函数只配置 StatePose Graph，不返回业务值。
function LuaAnimStateGraph:Initialize(config)
    config.GraphType = "StatePose"
    config.RootNodeName = "StateResult"
    config.RootNodeType = "StateResult"
    config.RootNodeDisplayName = "State Result"
    LuaAnimGraph.Initialize(self, config)
end

return LuaAnimStateGraph
