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

function ABP_Sekiro:Initialize()
    self.GroundLocomotion = self:CreateStateMachine(GroundLocomotionClass, {
        DebugName = "GroundLocomotion",
    }, true)
end

function ABP_Sekiro:AnimGraph()
    return self:OutputPose(self:UseStateMachine(self.GroundLocomotion))
end

local Runtime = ABP_Sekiro()
return Runtime:Export()
