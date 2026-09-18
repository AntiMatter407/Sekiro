-- Lua 类型：动画蓝图编译描述模块；编译对象是纯 Lua 表，运行时更新通过显式 Inst 访问 AI 的 AnimInstance。
-- AI 专用动画蓝图继承主角动画蓝图，完整复用主角的 AnimGraph、状态机、动画资源与生成变量。
-- 本模块只把导航产生的真实移动转换为动画移动意图，并保持 AI 导航对角色位移的所有权。

local LuaAnimBlueprint = require("Animation.Compiler.LuaAnimBlueprint")
local ABP_Sekiro = require("Animation.Sekiro.ABP_Sekiro")

local RootMotionMode = UE.ERootMotionMode
local AnimationLayerInterface =
    "/Game/Characters/Sekiro/ALI_Sekiro.ALI_Sekiro_C"

---@class ABP_SKAICharacter: LuaAnimBlueprint
local ABP_SKAICharacter = LuaAnimBlueprint:Extend("ABP_SKAICharacter", {
    SourceModule = "Animation.Sekiro.ABP_SKAICharacter",
    ParentAnimInstanceClass = "/Script/Sekiro.SKAnimInstance",
    TargetSkeleton = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton",
    ImplementedInterfaces = {
        AnimationLayerInterface,
    },
    AnimGraph = ABP_Sekiro.AnimGraph,
    DeclareAnimationLayers = ABP_Sekiro.DeclareAnimationLayers,
    ResolveDebugAnimationName = ABP_Sekiro.ResolveDebugAnimationName,
})

---导入父动画图的变量类型契约，供 Lua 前端解析 Property Getter 与 Transition Rule。
---生成器识别父 GeneratedClass 已有的同名同类型属性，只把这些声明作为继承引用，不在子类重复创建成员。
---@return nil result 变量契约交给生成器与父类反射属性匹配，不返回业务值。
function ABP_SKAICharacter:DeclareVariables()
    ABP_Sekiro.DeclareVariables(self)
end

---更新 AI 专用动画参数，并继续执行主角动画蓝图的通用运行时编排。
---AIController 的 MoveTo 不经过玩家输入管理器，因此用实际水平移动状态补齐 bHasMovementInput；
---父逻辑完成后忽略动画 Root Motion 位移，避免其与导航组件同时驱动角色位置。
---@param Inst userdata 当前 AI 动画实例的 UnLua 代理，可访问父动画实例属性与生成变量。
---@param delta_seconds number 本帧时长，单位为秒；原样传给父动画更新逻辑。
---@return nil result 直接更新动画实例字段，不返回业务值。
function ABP_SKAICharacter.BlueprintUpdateAnimation(Inst, delta_seconds)
    local has_navigation_movement = Inst.bIsMoving == true or (Inst.Speed or 0.0) > 3.0
    Inst.bHasMovementInput = has_navigation_movement
    Inst.MovementInputAmount = has_navigation_movement and 1.0 or 0.0

    ABP_Sekiro.BlueprintUpdateAnimation(Inst, delta_seconds)

    Inst.RootMotionMode = RootMotionMode.IgnoreRootMotion
end

return ABP_SKAICharacter:Export()
