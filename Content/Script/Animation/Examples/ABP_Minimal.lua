-- Lua AnimGraph Function 模式的最小动画蓝图示例。
-- 主文件只描述 AnimGraph 节点和连接；状态机拓扑、状态图和规则拆分到独立模块。
local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local MinimalLocomotion = require("Animation.Examples.StateMachines.MinimalLocomotion")

---@class ABP_Minimal: LuaAnimBlueprint
local ABP_Minimal = LuaAnimBlueprint:Extend("ABP_Minimal", {
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "/Engine/EngineMeshes/SkeletalCube_Skeleton.SkeletalCube_Skeleton",
})

---声明主动画图；节点创建、属性设置和 Pin 连接与手动编辑 AnimGraph 的顺序一致。
---@param graph LuaAnimGraph 基类创建的主 Pose Graph。
---@return nil result 最终姿势直接连接到 graph.Result。
function ABP_Minimal:AnimGraph(graph)
    local locomotion = graph:StateMachine("MainStateMachine", MinimalLocomotion)

    local saved_locomotion = graph:SaveCachedPose("SavedLocomotion")
    saved_locomotion.Pose:Connect(locomotion.Pose)

    local reused_locomotion = graph:UseCachedPose("UseLocomotion", saved_locomotion)
    local inertialization = graph:Inertialization("FinalInertialization")
    inertialization.Source:Connect(reused_locomotion.Pose)

    graph.Result:Connect(inertialization.Pose)
end

return ABP_Minimal:Export()
