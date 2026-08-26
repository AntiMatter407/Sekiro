-- Lua 类型：BehaviorTree Task 模块。本文件只计算或提交移动阶段的 Blackboard 状态。
-- Build 模式将动作目录的移动策略解析为可达 MoveGoal；实际寻路由 UE 原生 MoveTo 执行。
-- Commit 模式必须放在 MoveTo 成功之后，用于提交 OnMovementSuccess 冷却和 CombatMemory 副作用。

local ActionCatalog = require("AI.Genichiro.GenichiroActionCatalog")
local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")
local Runtime = require("AI.Genichiro.GenichiroRuntime")

local SKGenichiroBuildMoveGoal = {}

local NavigationQueryExtent = Runtime.MakeVector(80.0, 80.0, 180.0)

---根据可用空间和最近移动方向选择侧移符号；左为 -1，右为 1。
---@param context GenichiroDecisionContext 当前决策快照。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@return number|nil side 有可用侧向空间时返回 -1 或 1，否则 nil。
local function choose_strafe_side(context, memory)
    if context.Space.Left ~= true and context.Space.Right ~= true then
        return nil
    end
    if context.Space.Left == true and context.Space.Right ~= true then
        return -1
    end
    if context.Space.Right == true and context.Space.Left ~= true then
        return 1
    end

    local recent = CombatMemory.GetValue(memory, "RecentStrafeDirection", "")
    if recent == "Left" then
        return 1
    end
    if recent == "Right" then
        return -1
    end
    local seed = CombatMemory.GetValue(memory, "RandomSeed", 710000)
    return seed % 2 == 0 and 1 or -1
end

---计算接近目标但保留期望攻击距离的候选点。
---@param pawn APawn|table 当前 Pawn。
---@param target_actor AActor|table 当前目标。
---@param desired_range_cm number 希望与目标保持的距离。
---@return FVector|table|nil candidate 候选世界位置；任一 Actor 位置不可读时为 nil。
local function build_approach_candidate(pawn, target_actor, desired_range_cm)
    local owner_location = Runtime.GetActorLocation(pawn)
    local target_location = Runtime.GetActorLocation(target_actor)
    if owner_location == nil or target_location == nil then
        return nil
    end
    local direction = Runtime.HorizontalDirection(owner_location, target_location)
    return Runtime.OffsetLocation(target_location, direction, -desired_range_cm)
end

---按动作移动策略建立原始候选点和解析后的方向名称。
---@param pawn APawn|table 当前 Pawn。
---@param target_actor AActor|table 当前目标。
---@param action GenichiroActionDefinition 当前动作。
---@param context GenichiroDecisionContext 当前快照。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@return FVector|table|nil candidate 可供导航投影的候选点。
---@return string|nil direction_name 侧移方向或移动策略摘要。
local function build_candidate(pawn, target_actor, action, context, memory)
    local movement = action.Movement
    if movement == nil then
        return nil, nil
    end

    local policy = movement.Policy
    local owner_location = Runtime.GetActorLocation(pawn)
    if owner_location == nil then
        return nil, nil
    end
    if policy == "ApproachTarget" then
        return build_approach_candidate(
            pawn,
            target_actor,
            movement.DesiredRangeCm or 250.0), "Forward"
    end
    if policy == "NavigationStrafe" then
        local side = movement.FixedDirection == "Left" and -1
            or movement.FixedDirection == "Right" and 1
            or choose_strafe_side(context, memory)
        if side == nil then
            return nil, nil
        end
        local distance_cm = math.max(movement.RequiredSpaceCm or 200.0, 200.0)
        return Runtime.OffsetLocation(
            owner_location,
            pawn:GetActorRightVector(),
            distance_cm * side), side < 0 and "Left" or "Right"
    end
    if policy == "NavigationSafeBackstep"
        or policy == "NavigationSafeLongBackstep"
        or policy == "RetreatFromTarget" then
        if context.Space.Back ~= true then
            return nil, nil
        end
        return Runtime.OffsetLocation(
            owner_location,
            pawn:GetActorForwardVector(),
            -(movement.RequiredSpaceCm or 300.0)), "Back"
    end
    if policy == "ContextReposition" then
        if context.DistanceCm <= 300.0 then
            local side = choose_strafe_side(context, memory)
            if side == nil then
                return nil, nil
            end
            return Runtime.OffsetLocation(
                owner_location,
                pawn:GetActorRightVector(),
                200.0 * side), side < 0 and "Left" or "Right"
        end
        local desired_range = context.DistanceCm > 800.0 and 800.0 or 300.0
        return build_approach_candidate(pawn, target_actor, desired_range), "Range"
    end
    if policy == "MaintainTargetRange" then
        local desired_range = movement.DesiredRangeCm or 500.0
        return build_approach_candidate(pawn, target_actor, desired_range), "Range"
    end
    if policy == "TurnToTarget" then
        return owner_location, "Turn"
    end
    return nil, nil
end

---提交 MoveTo 已成功的移动阶段副作用，并保持后续战斗动作的 SelectedActionId。
---@param pawn APawn|table 当前 Pawn。
---@param blackboard UBlackboardComponent|table 当前 Blackboard。
---@param action GenichiroActionDefinition 当前动作。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@return string result 成功结果 Succeeded。
local function commit_movement(pawn, blackboard, action, memory)
    local direction_name = CombatMemory.GetValue(memory, "PendingNavigationSide", "")
    CombatMemory.ApplyCooldowns(
        memory,
        action,
        "OnMovementSuccess",
        Runtime.GetWorldTimeSeconds(pawn, 0.0))
    CombatMemory.ApplyStateWrites(memory, action, "OnMovementSuccess", {
        ResolvedNavigationSide = direction_name,
    })
    CombatMemory.SetValue(memory, "LastCommittedNavigationSide", direction_name)
    CombatMemory.SetValue(memory, "PendingNavigationSide", nil)

    if action.ExecutionMode == ActionCatalog.ExecutionMode.Navigation
        or action.ExecutionMode == ActionCatalog.ExecutionMode.CombatThenNavigation then
        blackboard:SetValueAsName(Runtime.Keys.SelectedActionId, "")
        blackboard:SetValueAsName(Runtime.Keys.TacticalIntent, "Hold")
        if action.ExecutionMode == ActionCatalog.ExecutionMode.Navigation then
            CombatMemory.RecordCompletedAction(memory, action)
        end
    end
    return "Succeeded"
end

---在 Build 或 Commit 模式执行移动阶段；配置字符串为 Commit 时只提交 MoveTo 成功结果。
---@param task USekiroLuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController。
---@param pawn APawn|table|nil 当前 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil Build 或 Commit；空值视为 Build。
---@return string result Succeeded 或 Failed。
function SKGenichiroBuildMoveGoal.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    local _unused = task
    if controller == nil or pawn == nil or blackboard == nil then
        return "Failed"
    end

    local action_id = Runtime.NameToString(
        blackboard:GetValueAsName(Runtime.Keys.SelectedActionId))
    local action = ActionCatalog.GetAction(action_id)
    if action == nil or action.Movement == nil then
        blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "MoveActionMissing")
        return "Failed"
    end

    local memory = CombatMemory.GetOrCreate(pawn)
    if Runtime.NameToString(configuration) == "Commit" then
        return commit_movement(pawn, blackboard, action, memory)
    end

    local target_actor = blackboard:GetValueAsObject(Runtime.Keys.TargetActor)
    if not Runtime.IsObjectValid(target_actor) then
        return "Failed"
    end
    if Runtime.GetActorLocation(pawn) == nil
        or Runtime.GetActorLocation(target_actor) == nil then
        blackboard:SetValueAsString(
            Runtime.Keys.DebugFailureReason,
            "ActorLocationUnavailable")
        return "Failed"
    end
    local context = Runtime.CaptureDecisionContext(pawn, blackboard, 0.0)
    local candidate, direction_name = build_candidate(
        pawn,
        target_actor,
        action,
        context,
        memory)
    if candidate == nil then
        blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "NoNavigationSpace")
        return "Failed"
    end

    local projected, move_goal = Runtime.ProjectNavigationPoint(
        controller,
        candidate,
        NavigationQueryExtent)
    if not projected or move_goal == nil then
        blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "NavigationProjectionFailed")
        return "Failed"
    end

    blackboard:SetValueAsVector(Runtime.Keys.MoveGoal, move_goal)
    CombatMemory.SetValue(memory, "PendingNavigationSide", direction_name or "")
    blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "")
    return "Succeeded"
end

return SKGenichiroBuildMoveGoal
