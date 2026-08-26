-- Lua 类型：动画蓝图编译描述模块；编译对象为纯 Lua 表，运行时仅显式更新 Inst 表现变量。
-- 弦一郎专用 AnimBlueprint 使用原生状态机表现导航移动，并以 CombatFullBodySlot 接收战斗组件动态 Montage。
-- Root Motion 固定为 RootMotionFromMontagesOnly：导航由 CharacterMovement 拥有，离散战斗 Montage 才抽取位移。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local AnimAssets = require("Animation.Genichiro.GenichiroAnimAssets")
local GenichiroLocomotion = require("Animation.Genichiro.StateMachines.GenichiroLocomotion")

local RootMotionMode = {
    FromMontagesOnly = 3,
}

---@class ABP_Genichiro: LuaAnimBlueprint
local ABP_Genichiro = LuaAnimBlueprint:Extend("ABP_Genichiro", {
    SourceModule = "Animation.Genichiro.ABP_Genichiro",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = AnimAssets.Skeleton,
})

---声明弦一郎导航状态机和调试需要的 GeneratedClass 变量。
---@return nil 无返回值。
function ABP_Genichiro:DeclareVariables()
    self:Variable("MovementDirection", "Float", 0.0)
    self:Variable("bHasMovementIntent", "Bool", false)
    self:Variable("bMoveForward", "Bool", false)
    self:Variable("bMoveBackward", "Bool", false)
    self:Variable("bMoveLeft", "Bool", false)
    self:Variable("bMoveRight", "Bool", false)
    self:Variable("bTurnLeftRequested", "Bool", false)
    self:Variable("bTurnRightRequested", "Bool", false)
    self:Variable("bCombatActionActive", "Bool", false)
end

---声明“导航基础状态机→战斗全身 Slot→惯性化→OutputPose”的原生 AnimGraph。
---@param graph LuaAnimGraph 主动画图。
---@return nil 无返回值。
function ABP_Genichiro:AnimGraph(graph)
    self:DeclareVariables()
    local locomotion = graph:StateMachine(
        "GenichiroLocomotion",
        GenichiroLocomotion)
    local combat_slot = graph:Node(
        "CombatFullBodySlot",
        EditorNodeClass.Slot,
        {
            SlotName = AnimAssets.SlotName,
            bAlwaysUpdateSourcePose = true,
        },
        "Slot")
    combat_slot.Source:Connect(locomotion.Pose)

    local inertialization = graph:Node(
        "FinalInertialization",
        EditorNodeClass.Inertialization,
        nil,
        "Inertialization")
    inertialization.Source:Connect(combat_slot.Pose)
    graph.Result:Connect(inertialization.Pose)

    local layout = graph:Grid("GenichiroAnimGraphFlow", {
        RegionColumn = 0,
        RegionRow = 0,
    })
    layout:Place(locomotion, 0, 0)
    layout:Place(combat_slot, 1, 0)
    layout:Place(inertialization, 2, 0)
    layout:Place(graph.OutputNode, 3, 0)
end

---把 USKAnimInstance 采集的速度方向转换为七状态机的互斥布尔请求，并保持单一 Root Motion 所有者。
---@param Inst userdata 当前弦一郎 AnimInstance UnLua 代理。
---@param delta_seconds number|nil 当前动画更新步长；本函数只使用已采集的瞬时数据。
---@return nil 无返回值。
function ABP_Genichiro.BlueprintUpdateAnimation(Inst, delta_seconds)
    local _unused = delta_seconds
    local speed = math.max(Inst.Speed or 0.0, 0.0)
    local angle = Inst.Angle or 0.0
    local moving = Inst.bIsMoving == true or speed > 3.0
    local absolute_angle = math.abs(angle)

    Inst.MovementDirection = angle
    Inst.bHasMovementIntent = moving
    Inst.bMoveForward = moving and absolute_angle <= 45.0
    Inst.bMoveBackward = moving and absolute_angle >= 135.0
    Inst.bMoveLeft = moving and angle < -45.0 and angle > -135.0
    Inst.bMoveRight = moving and angle > 45.0 and angle < 135.0

    local aim_yaw_delta = Inst.AimYawDelta or 0.0
    local turn_requested = not moving
        and math.abs(aim_yaw_delta) >= 35.0
    Inst.bTurnLeftRequested = turn_requested and aim_yaw_delta < 0.0
    Inst.bTurnRightRequested = turn_requested and aim_yaw_delta >= 0.0
    Inst.bCombatActionActive = Inst.bIsCombatFullBodyActionActive == true
    Inst.RootMotionMode = RootMotionMode.FromMontagesOnly
end

---把原生 SequencePlayer 资产短名解析为弦一郎 Lua 资源表语义名。
---@param native_asset_name string 当前原生 AnimSequence UObject 短名。
---@return string|nil lua_asset_name 已登记基础动画语义；未知战斗动画返回 nil。
function ABP_Genichiro.ResolveDebugAnimationName(native_asset_name)
    return AnimAssets.GetLuaAssetName(native_asset_name)
end

return ABP_Genichiro:Export()
