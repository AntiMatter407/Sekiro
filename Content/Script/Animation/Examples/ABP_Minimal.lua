-- Lua 类型：动画蓝图编译描述/状态机模块；编译对象是纯 Lua 表，运行时规则仅通过显式 Inst 访问 AnimInstance。
-- 新版 Lua AnimGraph Function 模式的最小动画蓝图示例。
-- 主文件声明生成变量、每帧更新入口、AnimGraph 节点和连接；状态机拓扑与原生过渡规则拆分到独立模块。
local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local MinimalLocomotion = require("Animation.Examples.StateMachines.MinimalLocomotion")

---@class ABP_Minimal: LuaAnimBlueprint
local ABP_Minimal = LuaAnimBlueprint:Extend("ABP_Minimal", {
    SourceModule = "Animation.Examples.ABP_Minimal",
    ParentAnimInstanceClass = "/Script/Engine.AnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
})

---声明主动画图；节点创建、属性设置和 Pin 连接与手动编辑 AnimGraph 的顺序一致。
---@param graph LuaAnimGraph 基类创建的主 Pose Graph。
---@return nil result 最终姿势直接连接到 graph.Result。
function ABP_Minimal:AnimGraph(graph)
    -- 这两个变量会真正生成到 UAnimBlueprintGeneratedClass，EventGraph 中的 Lua 更新桥接负责逐帧写入。
    self:Variable("DemoElapsedSeconds", "Float", 0.0)
    self:Variable("bShouldMove", "Bool", false)

    local locomotion = graph:StateMachine("MainStateMachine", MinimalLocomotion)

    local saved_locomotion = graph:SaveCachedPose("SavedLocomotion")
    saved_locomotion.Pose:Connect(locomotion.Pose)

    local reused_locomotion = graph:UseCachedPose("UseLocomotion", saved_locomotion)
    local inertialization = graph:Inertialization("FinalInertialization")
    inertialization.Source:Connect(reused_locomotion.Pose)

    graph.Result:Connect(inertialization.Pose)
end

---更新最小示例的生成变量，每两秒在 Idle 与 Move 之间切换一次。
---该函数由工厂生成的 BlueprintUpdateAnimation Event 在游戏线程调用；这里只演示数据如何送入原生规则。
---@param Inst userdata 当前生成动画蓝图的真实 UAnimInstance UnLua 代理。
---@param delta_seconds number|nil 当前动画更新帧间隔，单位秒。
---@return nil result 直接写入生成变量，不返回业务值。
function ABP_Minimal.BlueprintUpdateAnimation(Inst, delta_seconds)
    local elapsed_seconds = (Inst.DemoElapsedSeconds or 0.0) + math.max(delta_seconds or 0.0, 0.0)
    Inst.DemoElapsedSeconds = elapsed_seconds
    Inst.bShouldMove = math.floor(elapsed_seconds / 2.0) % 2 == 1
end

return ABP_Minimal:Export()
