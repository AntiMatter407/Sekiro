-- Lua 类型：BehaviorTree Task 模块。本文件由 USekiroLuaBehaviorTreeTask 在决策边界调用。
-- 采集距离、导航空间、阶段信号、战斗状态和能力快照，并写入跨层 Blackboard 契约。
-- 本任务同步完成，不选择动作、不移动 Pawn，也不消费战斗事件。

local Runtime = require("AI.Genichiro.GenichiroRuntime")
local TacticalProfile = require("AI.Genichiro.GenichiroTacticalProfile")

local SKGenichiroUpdateContext = {}

local SpaceProbeSideCm = 200.0
local SpaceProbeBackCm = 400.0
local NavigationQueryExtent = Runtime.MakeVector(60.0, 60.0, 160.0)

---投影一个相对方向的导航候选，并返回空间是否可用。
---@param controller AAIController|table 当前 AIController。
---@param origin FVector|table Pawn 世界位置。
---@param direction FVector|table 探测方向。
---@param distance_cm number 探测距离，单位厘米。
---@return boolean available 候选点是否可投影到导航网格。
local function probe_navigation_space(controller, origin, direction, distance_cm)
    local candidate = Runtime.OffsetLocation(origin, direction, distance_cm)
    local projected = Runtime.ProjectNavigationPoint(
        controller,
        candidate,
        NavigationQueryExtent)
    return projected == true
end

---采集一次决策上下文并同步写入 Blackboard；目标无效时失败，让外层 Selector 进入无目标分支。
---@param task USekiroLuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前行为树所属控制器。
---@param pawn APawn|table|nil 当前控制器拥有的 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前行为树黑板。
---@param configuration string|nil 保留配置字符串；当前不使用。
---@return string result Succeeded 或 Failed。
function SKGenichiroUpdateContext.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    local _unused = task or configuration
    if controller == nil or pawn == nil or blackboard == nil then
        return "Failed"
    end

    local keys = Runtime.Keys
    local target_actor = blackboard:GetValueAsObject(keys.TargetActor)
    if not Runtime.IsObjectValid(target_actor) then
        blackboard:SetValueAsBool(keys.bActionLocked, false)
        blackboard:SetValueAsName(keys.TacticalIntent, "Hold")
        blackboard:SetValueAsName(keys.SelectedActionId, "")
        return "Failed"
    end

    local combat_component = Runtime.GetCombatComponent(pawn)
    if combat_component == nil then
        blackboard:SetValueAsString(keys.DebugFailureReason, "MissingCombatComponent")
        return "Failed"
    end

    local distance_cm = tonumber(pawn:GetHorizontalDistanceTo(target_actor)) or 0.0
    local origin = Runtime.GetActorLocation(pawn)
    if origin == nil then
        blackboard:SetValueAsString(keys.DebugFailureReason, "ActorLocationUnavailable")
        return "Failed"
    end
    local right = pawn:GetActorRightVector()
    local forward = pawn:GetActorForwardVector()
    local can_move_left = probe_navigation_space(
        controller,
        origin,
        right,
        -SpaceProbeSideCm)
    local can_move_right = probe_navigation_space(
        controller,
        origin,
        right,
        SpaceProbeSideCm)
    local can_move_back = probe_navigation_space(
        controller,
        origin,
        forward,
        -SpaceProbeBackCm)

    local action_state = combat_component:GetCombatActionState()
    local action_locked = combat_component:IsCombatFullBodyActionActive() == true
        or not Runtime.EnumEquals(action_state, "ESKCombatActionState", "Neutral")

    blackboard:SetValueAsFloat(keys.DistanceCm, distance_cm)
    blackboard:SetValueAsName(
        keys.DistanceBand,
        TacticalProfile.ResolveDistanceBand(distance_cm))
    blackboard:SetValueAsBool(keys.bCanMoveLeft, can_move_left)
    blackboard:SetValueAsBool(keys.bCanMoveRight, can_move_right)
    blackboard:SetValueAsBool(keys.bCanMoveBack, can_move_back)
    blackboard:SetValueAsBool(
        keys.bProjectileCapabilityReady,
        combat_component.SpawnAIBattleProjectile ~= nil)
    blackboard:SetValueAsBool(keys.bActionLocked, action_locked)
    blackboard:SetValueAsInt(keys.ActionSerial, combat_component:GetActionSerial())
    if (tonumber(blackboard:GetValueAsFloat(keys.SelfHealthRatio)) or 0.0) <= 0.0 then
        -- 当前通用角色尚无生命比例接口；默认值 1 避免错误触发低生命 Kengeki40，外部属性适配器可覆盖。
        blackboard:SetValueAsFloat(keys.SelfHealthRatio, 1.0)
    end
    blackboard:SetValueAsString(keys.DebugFailureReason, "")
    return "Succeeded"
end

return SKGenichiroUpdateContext
