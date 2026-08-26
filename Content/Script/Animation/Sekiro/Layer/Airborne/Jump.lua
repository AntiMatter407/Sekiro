-- Lua 类型：动画蓝图编译描述模块。编译期对象是纯 Lua；Transition 规则生成 UE 原生属性与 Gate 节点。
-- Sekiro Jump 子状态机。
-- 非锁定有向跳固定使用前向资产；锁定有向跳经过八方向 Start/InAir 过渡段，长时间滞空才进入通用 FallLoop。

local LuaAnimStateMachine = require("Animation.Compiler.LuaAnimStateMachine")
local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Rule = require("Animation.Compiler.TransitionRule")
local Anim = require("Animation.Sekiro.AnimAssets").Jump
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")
local DirectionalPose = require("Animation.Sekiro.Shared.DirectionalPose")
local PoseSelectors = require("Animation.Sekiro.Shared.PoseSelectors")
local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class JumpLocomotion: LuaAnimStateMachine
local JumpLocomotion = LuaAnimStateMachine:Extend("JumpLocomotion")

local StartAssets = {
    Forward = Anim.Jump_Start_Forward, ForwardLeft = Anim.Jump_Start_ForwardLeft,
    Left = Anim.Jump_Start_Left, BackLeft = Anim.Jump_Start_BackLeft,
    Back = Anim.Jump_Start_Back, BackRight = Anim.Jump_Start_BackRight,
    Right = Anim.Jump_Start_Right, ForwardRight = Anim.Jump_Start_ForwardRight,
}
local InAirAssets = {
    Forward = Anim.Jump_InAir_Forward, ForwardLeft = Anim.Jump_InAir_ForwardLeft,
    Left = Anim.Jump_InAir_Left, BackLeft = Anim.Jump_InAir_BackLeft,
    Back = Anim.Jump_InAir_Back, BackRight = Anim.Jump_InAir_BackRight,
    Right = Anim.Jump_InAir_Right, ForwardRight = Anim.Jump_InAir_ForwardRight,
}
local LandAssets = {
    Forward = Anim.Jump_Land_Forward, ForwardLeft = Anim.Jump_Land_ForwardLeft,
    Left = Anim.Jump_Land_Left, BackLeft = Anim.Jump_Land_BackLeft,
    Back = Anim.Jump_Land_Back, BackRight = Anim.Jump_Land_BackRight,
    Right = Anim.Jump_Land_Right, ForwardRight = Anim.Jump_Land_ForwardRight,
}

---按起跳瞬间锁定模式选择非锁定前向姿势或锁定八方向姿势，空中切换锁定目标不会改变本次 Jump。
---@param Graph LuaAnimStateGraph 当前 Jump 状态的 Pose Graph。
---@param name string 选择器和属性节点使用的稳定语义前缀。
---@param unlocked_pose LuaAnimNode 非锁定模式使用的前向姿势节点。
---@param locked_pose LuaAnimNode 锁定模式使用的八方向姿势节点。
---@return LuaAnimNode selector 最终锁定模式选择节点。
local function select_jump_lock_mode(Graph, name, unlocked_pose, locked_pose)
    local started_locked_on = Graph:Property(name .. "StartedLockedOn", "bJumpStartedLockedOn")
    local selector = Graph:Node(
        name .. "LockModeSelector",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.JumpBlendDuration },
        "BlendListByBool")
    selector.FalsePose:Connect(unlocked_pose.Pose)
    selector.TruePose:Connect(locked_pose.Pose)
    selector.ActiveValue:Connect(started_locked_on.Value)
    return selector
end

---由锁存的 Jump 类型在原地姿势和八方向姿势之间选择，空中输入变化不会切换本次 Jump 资产。
---@param Graph LuaAnimStateGraph 当前 Jump 状态的 Pose Graph。
---@param name string 选择器和属性节点使用的稳定语义前缀。
---@param stationary_pose LuaAnimNode 原地 Jump 对应的姿势节点。
---@param directional_pose LuaAnimNode 八方向 Jump 对应的姿势节点。
---@return LuaAnimNode selector 最终 Jump 类型选择节点。
local function select_jump_type(Graph, name, stationary_pose, directional_pose)
    local directional_jump = Graph:Property(name .. "Directional", "bDirectionalJump")
    local selector = Graph:Node(
        name .. "TypeSelector",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.JumpBlendDuration },
        "BlendListByBool")
    selector.FalsePose:Connect(stationary_pose.Pose)
    selector.TruePose:Connect(directional_pose.Pose)
    selector.ActiveValue:Connect(directional_jump.Value)
    return selector
end

---声明 Jump Start、锁定方向 InAir 过渡段、通用 FallLoop 和 Light/Heavy Land。
---锁定 InAir 不是循环动画：仍在空中时于曲线尾部进入 FallLoop，任何阶段物理接地都优先打断到 Land。
---@param Machine LuaStateMachineNode JumpLocomotion 原生状态机节点。
---@return nil result 只声明状态机拓扑。
function JumpLocomotion.StateMachine(Machine)
    Machine:Entry("Start")
    Machine:State("Start")
    Machine:State("DirectionalInAir")
    Machine:State("FallLoop")
    Machine:State("Land")
    -- Start 期间已经接地时直接进入 Land，处理极短腾空或碰撞提前接地。
    Machine:Transition("Start_Land", "Start", "Land", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsInAir", false),
    })
    -- 锁定有向跳仍在空中时，于 Start 曲线尾部进入八方向 InAir 过渡段。
    Machine:Transition("Start_DirectionalInAir", "Start", "DirectionalInAir", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bIsInAir", true),
            Rule.BoolProperty("bDirectionalJump", true),
            Rule.BoolProperty("bJumpStartedLockedOn", true),
            Rule.CurveGreaterEqual(CurveNames.CanEnterInAir, Tuning.CurveThreshold)),
    })
    -- 原地或非锁定有向跳在 Start 曲线尾部直接进入通用 FallLoop。
    Machine:Transition("Start_FallLoop", "Start", "FallLoop", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 2,
        Rule = Rule.All(
            Rule.BoolProperty("bIsInAir", true),
            Rule.Any(
                Rule.BoolProperty("bDirectionalJump", false),
                Rule.BoolProperty("bJumpStartedLockedOn", false)),
            Rule.CurveGreaterEqual(CurveNames.CanEnterLoop, Tuning.CurveThreshold)),
    })
    -- 锁定方向 InAir 过渡动画播放期间提前接地时立即打断到 Land。
    Machine:Transition("DirectionalInAir_Land", "DirectionalInAir", "Land", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsInAir", false),
    })
    -- 锁定方向 InAir 过渡段播放完成且仍在空中时进入通用循环姿势。
    Machine:Transition("DirectionalInAir_FallLoop", "DirectionalInAir", "FallLoop", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 1,
        Rule = Rule.All(
            Rule.BoolProperty("bIsInAir", true),
            Rule.CurveGreaterEqual(CurveNames.CanEnterLoop, Tuning.CurveThreshold)),
    })
    -- 通用 FallLoop 期间由 CharacterMovement 报告接地时进入 Land。
    Machine:Transition("FallLoop_Land", "FallLoop", "Land", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsInAir", false),
    })
    -- Land 过程中再次离地时重新进入 Start，支持连续跳跃和台阶边缘情况。
    Machine:Transition("Land_Start", "Land", "Start", {
        BlendDuration = Tuning.JumpBlendDuration,
        PriorityOrder = 0,
        Rule = Rule.BoolProperty("bIsInAir", true),
    })
end

---构建带 RootMotion 的八方向 Jump Start。
---@param Graph LuaAnimStateGraph Start 状态 Pose Graph。
---@return nil result Start 姿势连接 State Result。
function JumpLocomotion.StateGraph_Start(Graph)
    local standing = PoseSelectors.Sequence(Graph, "StandingJumpStart", Anim.Stand_Jump_Start, false, nil)
    local crouching = PoseSelectors.Sequence(Graph, "CrouchingJumpStart", Anim.Crouch_Jump_Start, false, nil)
    local started_crouched = Graph:Property("JumpStartedCrouched", "bJumpStartedCrouchedPose")
    local stationary = Graph:Node(
        "StationaryJumpStartStance",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.JumpBlendDuration },
        "BlendListByBool")
    stationary.FalsePose:Connect(standing.Pose)
    stationary.TruePose:Connect(crouching.Pose)
    stationary.ActiveValue:Connect(started_crouched.Value)

    local unlocked = PoseSelectors.Sequence(
        Graph,
        "UnlockedForwardJumpStart",
        Anim.Jump_Unlock_Forward_Start,
        false,
        nil)
    local locked = PoseSelectors.Octant(Graph, "LockedDirectionalJumpStart", StartAssets, "JumpDirection", false)
    local directional = select_jump_lock_mode(Graph, "JumpStart", unlocked, locked)
    local start = select_jump_type(Graph, "JumpStart", stationary, directional)
    local aligned = DirectionalPose.Align(
        Graph,
        "JumpStartAlignment",
        start,
        "JumpDirectionResidualAngle",
        "JumpWarpingAlpha")
    Graph.Result:Connect(aligned.Pose)
end

---构建锁定有向跳的八方向 InAir 过渡姿势；该动画只播放一次，曲线尾部进入通用 FallLoop。
---@param Graph LuaAnimStateGraph DirectionalInAir 状态 Pose Graph。
---@return nil result 八方向 InAir 过渡姿势连接 State Result。
function JumpLocomotion.StateGraph_DirectionalInAir(Graph)
    local in_air = PoseSelectors.Octant(Graph, "LockedDirectionalJumpInAir", InAirAssets, "JumpDirection", false)
    local aligned = DirectionalPose.Align(
        Graph,
        "JumpInAirAlignment",
        in_air,
        "JumpDirectionResidualAngle",
        "JumpWarpingAlpha")
    Graph.Result:Connect(aligned.Pose)
end

---构建原地、非锁定有向跳和锁定长时间滞空共同使用的循环姿势。
---@param Graph LuaAnimStateGraph FallLoop 状态 Pose Graph。
---@return nil result 通用 Jump Loop 姿势连接 State Result。
function JumpLocomotion.StateGraph_FallLoop(Graph)
    local loop = PoseSelectors.Sequence(Graph, "JumpLoop", Anim.Jump_Loop, true, nil)
    Graph.Result:Connect(loop.Pose)
end

---构建 Light/Heavy Land；轻落地保留锁定八方向资产，重落地按起跳姿态选择站立或蹲姿资产。
---LandPredictionAmount 在接地前锁存 bHeavyLand，使落地首帧即可稳定选择强度，不随接地后 FallSpeed 清零而翻转。
---@param Graph LuaAnimStateGraph Land 状态 Pose Graph。
---@return nil result Land 姿势连接 State Result。
function JumpLocomotion.StateGraph_Land(Graph)
    local stationary = PoseSelectors.Sequence(Graph, "StationaryJumpLand", Anim.Jump_Light_Stand, false, nil)
    local unlocked = PoseSelectors.Sequence(
        Graph,
        "UnlockedForwardJumpLand",
        Anim.Jump_Light_Stand,
        false,
        nil)
    local locked = PoseSelectors.Octant(Graph, "LockedDirectionalJumpLand", LandAssets, "JumpDirection", false)
    local directional = select_jump_lock_mode(Graph, "JumpLand", unlocked, locked)
    local light_land = select_jump_type(Graph, "JumpLand", stationary, directional)
    local aligned_light_land = DirectionalPose.Align(
        Graph,
        "JumpLandAlignment",
        light_land,
        "JumpDirectionResidualAngle",
        "JumpWarpingAlpha")

    local heavy_standing = PoseSelectors.Sequence(
        Graph,
        "HeavyStandingJumpLand",
        Anim.Jump_Heavy_Stand,
        false,
        nil)
    local heavy_crouching = PoseSelectors.Sequence(
        Graph,
        "HeavyCrouchingJumpLand",
        Anim.Jump_Heavy_Crouch,
        false,
        nil)
    local started_crouched = Graph:Property(
        "HeavyJumpLandStartedCrouched",
        "bJumpStartedCrouchedPose")
    local heavy_land = Graph:Node(
        "HeavyJumpLandStance",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.JumpBlendDuration },
        "BlendListByBool")
    heavy_land.FalsePose:Connect(heavy_standing.Pose)
    heavy_land.TruePose:Connect(heavy_crouching.Pose)
    heavy_land.ActiveValue:Connect(started_crouched.Value)

    local heavy_land_requested = Graph:Property("HeavyJumpLandRequested", "bHeavyLand")
    local land = Graph:Node(
        "JumpLandIntensity",
        EditorNodeClass.BlendListByBool,
        { BlendTime = Tuning.JumpBlendDuration },
        "BlendListByBool")
    land.FalsePose:Connect(aligned_light_land.Pose)
    land.TruePose:Connect(heavy_land.Pose)
    land.ActiveValue:Connect(heavy_land_requested.Value)
    Graph.Result:Connect(land.Pose)
end

return JumpLocomotion
