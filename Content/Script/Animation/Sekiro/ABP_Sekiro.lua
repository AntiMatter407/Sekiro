local class = require("Animation.Base.Class")
local LuaAnimBlueprint = require("Animation.Base.LuaAnimBlueprint")

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local GroundLocomotionClass = require("Animation.Sekiro.GroundLocomotion")

local ABP_Sekiro = class("ABP_Sekiro", LuaAnimBlueprint, {
    AnimAssets = AnimAssets,
    Debug = true,
    DebugUpdateInterval = 0.5,
    DebugTransitions = true,
    DebugTransitionChecks = false,
    DebugPose = true,
    DebugContext = true,
})

---创建 Sekiro 主动画蓝图使用的 GroundLocomotion 状态机，并注册为默认 AnimGraph 输出层。
---@return nil 该生命周期入口只执行初始化，不返回业务值。
function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, {
        DebugName = "GroundLocomotion",
    }, true)
end

---声明 GroundLocomotion 状态机为 Sekiro AnimGraph 的唯一根输出。
---@return table root_state_machine 负责生成并发布最终 PoseLink 的 GroundLocomotion 状态机。
function ABP_Sekiro:AnimGraph()
    return self:OutputPose(self:UseStateMachine(self.GroundLocomotion))
end

return ABP_Sekiro:Export()
