-- Sekiro 新 Lua 动画蓝图入口。
-- Lua 在编译期声明原生 Graph，在游戏线程更新生成变量；Pose、状态时间、混合和 RootMotion 均由 UE 原生节点执行。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local RootLocomotion = require("Animation.Sekiro.Layer.GroundLocomotion.Root")
local Direction = require("Animation.Sekiro.Shared.Direction")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

local DirectionEnum = "/Script/Sekiro.ESKLocomotionDirection"
local GaitEnum = "/Script/Sekiro.ESKAnimGait"

---@class ABP_Sekiro: LuaAnimBlueprint
local ABP_Sekiro = LuaAnimBlueprint:Extend("ABP_Sekiro", {
    SourceModule = "Animation.Sekiro.ABP_Sekiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
})

---声明本动画蓝图运行时需要的真实 GeneratedClass 变量。
---@return nil result 变量会随 IR 生成到 UAnimBlueprint GeneratedClass。
function ABP_Sekiro:DeclareVariables()
    self:Variable("CycleDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("LatchedActionDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("LatchedFreeStartDirection", "Enum", Direction.Cardinal.Forward, DirectionEnum)
    self:Variable("CycleGait", "Enum", 1, GaitEnum)
    self:Variable("LatchedActionGait", "Enum", 1, GaitEnum)
    self:Variable("DirectionResidualAngle", "Float", 0.0)
    self:Variable("bWasLockedOn", "Bool", false)
    self:Variable("bLatchedActionLockedOn", "Bool", false)
    self:Variable("bHadMovementInput", "Bool", false)
    self:Variable("bWasDodging", "Bool", false)
    self:Variable("bWasSprintRequested", "Bool", false)
    self:Variable("JumpDirection", "Enum", Direction.Octant.Forward, DirectionEnum)
    self:Variable("bWasInAir", "Bool", false)
end

---声明根动画图：原生 RootLocomotion 状态机经过 Inertialization 后输出最终 Pose。
---@param Graph LuaAnimGraph 基类创建的主 AnimGraph。
---@return nil result 最终姿势连接 Graph Result。
function ABP_Sekiro:AnimGraph(Graph)
    self:DeclareVariables()
    local locomotion = Graph:StateMachine("RootLocomotion", RootLocomotion)
    local inertialization = Graph:Inertialization("LocomotionInertialization")
    inertialization.Source:Connect(locomotion.Pose)
    Graph.Result:Connect(inertialization.Pose)
end

---每帧在游戏线程更新原生 Graph 消费的方向、步态和一次性动作锁存变量。
---非锁定移动始终选择 Forward 素材并由 Movement 朝输入方向旋转；锁定移动使用带滞回的四方向素材。
---@param Inst userdata 当前生成动画实例的 UnLua 代理，可直接访问 USKAnimInstance 字段和生成变量。
---@param _delta_seconds number 本帧时长，单位为秒；当前方向分类不依赖帧率但保留标准事件签名。
---@return nil result 直接写入生成变量，不返回业务值。
function ABP_Sekiro.BlueprintUpdateAnimation(Inst, _delta_seconds)
    local has_input = Inst.bHasMovementInput == true
    local locked_on = Inst.bIsLockedOn == true
    local direction = Direction.Cardinal.Forward
    if locked_on and has_input then
        direction = Direction.ResolveCardinalWithHysteresis(
            Inst.MoveDirectionAngle,
            Inst.CycleDirection,
            Tuning.LockedDirectionHysteresisAngle)
    end

    if has_input then
        Inst.CycleDirection = direction
        Inst.CycleGait = Inst.Gait
        if Inst.bHadMovementInput ~= true then
            Inst.LatchedActionDirection = locked_on and direction or Direction.Cardinal.Forward
            Inst.LatchedFreeStartDirection = Direction.ClassifyCardinal(Inst.MoveDirectionAngle)
            Inst.LatchedActionGait = Inst.DesiredGait
            Inst.bLatchedActionLockedOn = locked_on
        end
    elseif Inst.bHadMovementInput == true then
        Inst.LatchedActionDirection = locked_on and Inst.CycleDirection or Direction.Cardinal.Forward
        Inst.LatchedActionGait = Inst.CycleGait
        Inst.bLatchedActionLockedOn = locked_on
    end

    if Inst.bIsDodging == true and Inst.bWasDodging ~= true then
        Inst.LatchedActionDirection = locked_on
            and Direction.ClassifyCardinal(Inst.DodgeDirection)
            or Direction.Cardinal.Forward
        Inst.LatchedActionGait = Inst.DesiredGait
        Inst.bLatchedActionLockedOn = locked_on
    end

    local sprint_requested = Inst.bHasMovementInput == true and Inst.DesiredGait == UE.ESKAnimGait.Sprint
    if sprint_requested and Inst.bWasSprintRequested ~= true then
        Inst.LatchedActionDirection = Direction.ClassifyCardinal(Inst.MoveDirectionAngle)
        Inst.LatchedActionGait = Inst.DesiredGait
        Inst.bLatchedActionLockedOn = false
    elseif not sprint_requested and Inst.bWasSprintRequested == true then
        Inst.LatchedActionDirection = Direction.ClassifyCardinal(Inst.MoveDirectionAngle)
    end
    if Inst.bIsInAir == true and Inst.bWasInAir ~= true then
        Inst.JumpDirection = locked_on
            and Direction.ClassifyOctant(Inst.MoveDirectionAngle)
            or Direction.Octant.Forward
    end

    Inst.DirectionResidualAngle = locked_on
        and Direction.GetCardinalResidual(Inst.MoveDirectionAngle, direction)
        or 0.0
    Inst.bWasLockedOn = locked_on
    Inst.bHadMovementInput = has_input
    Inst.bWasDodging = Inst.bIsDodging == true
    Inst.bWasSprintRequested = sprint_requested
    Inst.bWasInAir = Inst.bIsInAir == true
end

return ABP_Sekiro:Export()
