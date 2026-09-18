-- Lua 类型：BehaviorTree Task 模块。本文件执行已选择的离散战斗动作或紧密连段。
-- Task 只持有 ActionCatalog 动画步骤、ActionSerial 与 Type 2 发射时点，不执行普通导航或感知循环。
-- Execute/Tick/Abort 的所有结束路径均清理弱键状态，并按 ExpectedSerial 防止旧任务完成新动作。

local ActionCatalog = require("AI.Genichiro.GenichiroActionCatalog")
local CombatMemory = require("AI.Genichiro.GenichiroCombatMemory")
local ProjectileProfile = require("AI.Genichiro.GenichiroProjectileProfile")
local Runtime = require("AI.Genichiro.GenichiroRuntime")
local TacticalProfile = require("AI.Genichiro.GenichiroTacticalProfile")

---@class SKGenichiroCombatActionTaskState
---@field Pawn APawn|table 当前 Pawn。
---@field Blackboard UBlackboardComponent|table 当前 Blackboard。
---@field CombatComponent USKCombatComponent|table 当前战斗组件。
---@field TargetActor AActor|table 当前锁存目标。
---@field Action GenichiroActionDefinition 当前动作定义。
---@field SelectedVariant GenichiroActionVariant 当前锁存动作分支。
---@field Steps GenichiroAnimationStep[] 锁存的分支步骤与有效条件追加步骤。
---@field StepIndex number 当前步骤索引。
---@field ExpectedSerial number 当前步骤的 ActionSerial。
---@field ElapsedSeconds number 整个任务累计秒数。
---@field SegmentElapsedSeconds number 当前步骤累计秒数。
---@field MaximumSeconds number 故障超时秒数。
---@field bStarted boolean 是否已成功启动首段。
---@field bStartCommitted boolean 是否已提交 OnStart 副作用。
---@field ProjectileCueIndex number 当前步骤下一条待发射 Type 2 事件索引。

local SKGenichiroExecuteCombatAction = {}

---@type table<ULuaBehaviorTreeTask, SKGenichiroCombatActionTaskState>
local TaskStates = setmetatable({}, { __mode = "k" })
local CachedProjectileClass = nil

---判断步骤是否声明指定伤害通道。
---@param step GenichiroAnimationStep 当前动画步骤。
---@param channel string Melee、Projectile 或 None。
---@return boolean declared 是否声明该通道。
local function has_damage_channel(step, channel)
    for _index, damage_channel in ipairs(step.DamageChannels or {}) do
        if damage_channel == channel then
            return true
        end
    end
    return false
end

---按动画用途选择无玩家副作用的通用动作状态。
---@param step GenichiroAnimationStep 当前动画步骤。
---@return userdata|number state ESKCombatActionState 原生枚举值。
local function resolve_step_state(step)
    if step.Usage == ActionCatalog.Usage.Locomotion then
        return UE.ESKCombatActionState.Dodging
    end
    if step.Usage == ActionCatalog.Usage.Reaction then
        return UE.ESKCombatActionState.AIReaction
    end
    return UE.ESKCombatActionState.LightAttack
end

---判断目录条件追加步骤是否满足当前决策快照；未知表达式失败关闭。
---@param condition string|nil 条件表达式。
---@param context GenichiroDecisionContext 当前决策快照。
---@return boolean allowed 是否允许追加步骤。
local function evaluate_condition(condition, context)
    if condition == nil or condition == "" then
        return true
    end
    if condition == "SelfPostureRatio<=0.7 and LegacyPhaseFlagA" then
        return (context.SelfPostureRatio or 0.0) <= 0.7
            and context.SelfFlags.LegacyPhaseFlagA == true
    end
    return false
end

---按目录分支权重和实例随机种子锁存一个动作分支。
---@param action GenichiroActionDefinition 当前动作定义。
---@param memory GenichiroCombatMemoryState 当前战斗记忆。
---@param context GenichiroDecisionContext 当前决策快照。
---@return GenichiroActionVariant|nil variant 选中分支；无分支时返回 nil。
local function select_variant(action, memory, context)
    if action.ID == "SideDodge" then
        local pending_side = CombatMemory.GetValue(memory, "LastCommittedNavigationSide", "")
        for _index, action_variant in ipairs(action.Variants or {}) do
            if action_variant.ID == pending_side then
                return action_variant
            end
        end
    end
    if action.SourceKind == ActionCatalog.SourceKind.Kengeki
        and action.LegacyIndex == 38
        and (context.BossPhase or 0) > 1 then
        for _index, action_variant in ipairs(action.Variants or {}) do
            if action_variant.ID == "ShortFollowUp" then
                return action_variant
            end
        end
    end

    local candidates = {}
    for _index, action_variant in ipairs(action.Variants or {}) do
        candidates[#candidates + 1] = {
            ActionID = action_variant.ID,
            Weight = action_variant.LegacyWeight,
            LegacyWeight = action_variant.LegacyWeight,
            Source = action.ID,
        }
    end
    local seed = CombatMemory.GetValue(memory, "RandomSeed", 710000)
    local selected, next_seed = TacticalProfile.SelectWeighted(candidates, seed)
    CombatMemory.SetValue(memory, "RandomSeed", next_seed)
    if selected == nil then
        return nil
    end
    for _index, action_variant in ipairs(action.Variants or {}) do
        if action_variant.ID == selected.ActionID then
            return action_variant
        end
    end
    return nil
end

---构造锁存步骤数组，并在执行前过滤条件追加动作。
---@param action GenichiroActionDefinition 当前动作定义。
---@param variant GenichiroActionVariant 当前锁存分支。
---@param context GenichiroDecisionContext 当前决策快照。
---@return GenichiroAnimationStep[] steps 本次执行的稳定步骤数组。
local function build_steps(action, variant, context)
    local steps = {}
    for _index, step in ipairs(variant.Steps or {}) do
        steps[#steps + 1] = step
    end
    for _index, step in ipairs(action.ConditionalFollowUps or {}) do
        if evaluate_condition(step.Condition, context) then
            steps[#steps + 1] = step
        end
    end
    return steps
end

---加载项目侧弹射物 Blueprint Class；失败时返回 nil 并由动作能力契约失败关闭。
---@return UClass|nil projectile_class 弦一郎箭矢类。
local function resolve_projectile_class()
    if CachedProjectileClass ~= nil then
        return CachedProjectileClass
    end
    if UE == nil or UE.UClass == nil or UE.UClass.Load == nil then
        return nil
    end
    local ok, loaded_class = pcall(
        UE.UClass.Load,
        ProjectileProfile.ProjectileClassPath)
    if ok then
        CachedProjectileClass = loaded_class
    end
    return CachedProjectileClass
end

---停止并失效仅由当前任务 ExpectedSerial 持有的动作，用于目标失效、播放失败和故障超时。
---@param state SKGenichiroCombatActionTaskState 当前任务状态。
---@return nil 无返回值。
local function invalidate_owned_action(state)
    local combat_component = state.CombatComponent
    if state.ExpectedSerial > 0
        and combat_component:IsActionSerialValid(state.ExpectedSerial) == true then
        combat_component:StopCombatAnimation(0.05)
        combat_component:InvalidateCombatAction(state.ExpectedSerial)
        combat_component:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    end
end

---统一清理 Task 和 Blackboard 的调度状态；动作锁由组件当前事实派生。
---@param task ULuaBehaviorTreeTask 当前 Task 实例。
---@param state SKGenichiroCombatActionTaskState 当前任务状态。
---@param clear_selection boolean 是否清除已选择动作。
---@return nil 无返回值。
local function clear_task_state(task, state, clear_selection)
    local combat_component = state.CombatComponent
    local action_locked = combat_component:IsCombatFullBodyActionActive() == true
        or combat_component:GetCombatActionState()
            ~= UE.ESKCombatActionState.Neutral
    state.Blackboard:SetValueAsBool(Runtime.Keys.bActionLocked, action_locked)
    state.Blackboard:SetValueAsInt(Runtime.Keys.ActionSerial, combat_component:GetActionSerial())
    if clear_selection then
        state.Blackboard:SetValueAsName(Runtime.Keys.SelectedActionId, "")
    end
    TaskStates[task] = nil
end

---启动当前步骤，按 StopMovement→Begin→Play 顺序提交并锁存新 Serial。
---@param state SKGenichiroCombatActionTaskState 当前任务状态。
---@param controller AAIController|table 当前 AIController。
---@return boolean started 当前步骤是否成功播放。
local function start_current_segment(state, controller)
    local step = state.Steps[state.StepIndex]
    if step == nil then
        return false
    end
    if state.CombatComponent:IsCombatAnimationPlaying() == true then
        return false
    end

    controller:StopMovement()
    local serial = state.CombatComponent:BeginCombatAction(resolve_step_state(step))
    if type(serial) ~= "number" or serial <= 0 then
        return false
    end
    state.ExpectedSerial = serial
    if state.CombatComponent:PlayCombatAnimationByPath(
        step.AssetPath,
        0.05,
        0.05,
        1.0,
        1) ~= true then
        state.CombatComponent:InvalidateCombatAction(serial)
        state.CombatComponent:SetCombatActionState(UE.ESKCombatActionState.Neutral)
        return false
    end

    state.SegmentElapsedSeconds = 0.0
    state.ProjectileCueIndex = 1
    state.Blackboard:SetValueAsBool(Runtime.Keys.bActionLocked, true)
    state.Blackboard:SetValueAsInt(Runtime.Keys.ActionSerial, serial)
    return true
end

---在当前步骤所有已到达的 Type 2 时间点发射通用弹射物，并让命中生产者发布 ProjectileImpact。
---@param state SKGenichiroCombatActionTaskState 当前任务状态。
---@return boolean ready 尚未到发射点或发射成功时为 true；能力失败时为 false。
---@return string|nil failure_reason 失败时写入 Blackboard 的稳定原因；成功时为 nil。
local function update_projectile_cue(state)
    local step = state.Steps[state.StepIndex]
    if step == nil or not has_damage_channel(step, ActionCatalog.DamageChannel.Projectile) then
        return true
    end
    local cues = step.ProjectileCues or {}
    if #cues == 0 then
        cues = {
            {
                TimeSeconds = ProjectileProfile.DefaultCueTimeSeconds,
                BehaviorJudgeID = 0,
                DummyPolyID = 0,
            },
        }
    end
    if state.ProjectileCueIndex > #cues
        or state.SegmentElapsedSeconds
            < cues[state.ProjectileCueIndex].TimeSeconds then
        return true
    end

    local projectile_class = resolve_projectile_class()
    if projectile_class == nil
        or state.CombatComponent.SpawnAIBattleProjectile == nil then
        return step.Optional == true
    end
    while state.ProjectileCueIndex <= #cues do
        local cue = cues[state.ProjectileCueIndex]
        if state.SegmentElapsedSeconds < cue.TimeSeconds then
            break
        end
        local owner_location = Runtime.GetActorLocation(state.Pawn)
        local target_location = Runtime.GetActorLocation(state.TargetActor)
        if owner_location == nil or target_location == nil then
            return false, "ActorLocationUnavailable"
        end
        local spawn_location = Runtime.OffsetLocation(
            owner_location,
            state.Pawn:GetActorForwardVector(),
            ProjectileProfile.SpawnForwardOffsetCm)
        spawn_location = Runtime.OffsetLocation(
            spawn_location,
            Runtime.MakeVector(0.0, 0.0, 1.0),
            ProjectileProfile.SpawnHeightOffsetCm)
        local event_tag = string.format(
            "%s_R%d_E%s_B%d_D%d",
            ProjectileProfile.EventTag,
            step.ActionRequestID,
            tostring(step.EventSourceAnimID or "None"),
            cue.BehaviorJudgeID or 0,
            cue.DummyPolyID or 0)
        local spawned = state.CombatComponent:SpawnAIBattleProjectile(
            projectile_class,
            spawn_location,
            state.TargetActor,
            target_location,
            ProjectileProfile.Speed,
            ProjectileProfile.GravityScale,
            ProjectileProfile.Damage,
            ProjectileProfile.LifeSeconds,
            event_tag)
        if spawned ~= true then
            if step.Optional == true then
                return true, nil
            end
            return false, "ProjectileSpawnFailed"
        end
        state.ProjectileCueIndex = state.ProjectileCueIndex + 1
    end
    return true
end

---验证动作并启动锁存分支的第一段；纯导航动作拒绝进入本执行器。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController。
---@param pawn APawn|table|nil 当前 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil 可选故障超时秒数。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；InProgress 或 Failed。
function SKGenichiroExecuteCombatAction.Execute(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    if task == nil or controller == nil or pawn == nil or blackboard == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local target_actor = blackboard:GetValueAsObject(Runtime.Keys.TargetActor)
    if not Runtime.IsObjectValid(target_actor) then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local action_id = Runtime.NameToString(
        blackboard:GetValueAsName(Runtime.Keys.SelectedActionId))
    local action = ActionCatalog.GetAction(action_id)
    if action == nil or action.ExecutionMode == ActionCatalog.ExecutionMode.Navigation then
        blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "CombatActionMissing")
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local combat_component = Runtime.GetCombatComponent(pawn)
    if combat_component == nil
        or combat_component:IsPostureBroken() == true
        or combat_component:IsCombatAnimationPlaying() == true
        or combat_component:GetCombatActionState()
            ~= UE.ESKCombatActionState.Neutral then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local memory = CombatMemory.GetOrCreate(pawn)
    local context = Runtime.CaptureDecisionContext(pawn, blackboard, 0.0)
    local variant = select_variant(action, memory, context)
    if variant == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local steps = build_steps(action, variant, context)
    if #steps == 0 then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    local state = {
        Pawn = pawn,
        Blackboard = blackboard,
        CombatComponent = combat_component,
        TargetActor = target_actor,
        Action = action,
        SelectedVariant = variant,
        Steps = steps,
        StepIndex = 1,
        ExpectedSerial = 0,
        ElapsedSeconds = 0.0,
        SegmentElapsedSeconds = 0.0,
        MaximumSeconds = math.max(tonumber(configuration) or (#steps * 8.0 + 2.0), 1.0),
        bStarted = false,
        bStartCommitted = false,
        ProjectileCueIndex = 1,
    }
    TaskStates[task] = state
    if not start_current_segment(state, controller) then
        invalidate_owned_action(state)
        clear_task_state(task, state, true)
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end

    state.bStarted = true
    state.bStartCommitted = true
    local now_seconds = Runtime.GetWorldTimeSeconds(pawn, 0.0)
    CombatMemory.ApplyCooldowns(memory, action, "OnStart", now_seconds)
    CombatMemory.ApplyStateWrites(memory, action, "OnStart", nil)
    blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "")
    return UE.ELuaBehaviorTreeTaskResult.InProgress
end

---监控 Serial、Type 2 发射点和 Montage 完成，顺序推进锁存连段并提交完成副作用。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController。
---@param pawn APawn|table|nil 当前 Pawn。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil 原 Execute 配置；Tick 不重新解析。
---@param delta_seconds number 当前帧步长。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；InProgress、Succeeded 或 Failed。
function SKGenichiroExecuteCombatAction.Tick(
    task,
    controller,
    pawn,
    blackboard,
    configuration,
    delta_seconds)
    local _unused = pawn or blackboard or configuration
    local state = TaskStates[task]
    if state == nil or controller == nil then
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local delta = math.max(tonumber(delta_seconds) or 0.0, 0.0)
    state.ElapsedSeconds = state.ElapsedSeconds + delta
    state.SegmentElapsedSeconds = state.SegmentElapsedSeconds + delta

    if state.ElapsedSeconds > state.MaximumSeconds
        or not Runtime.IsObjectValid(state.TargetActor) then
        state.Blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "CombatActionFaultTimeoutOrTargetLost")
        invalidate_owned_action(state)
        clear_task_state(task, state, true)
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    if state.CombatComponent:IsActionSerialValid(state.ExpectedSerial) ~= true then
        state.Blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "CombatActionSuperseded")
        clear_task_state(task, state, true)
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    local projectile_ready, projectile_failure_reason = update_projectile_cue(state)
    if not projectile_ready then
        state.Blackboard:SetValueAsString(
            Runtime.Keys.DebugFailureReason,
            projectile_failure_reason or "ProjectileSpawnFailed")
        invalidate_owned_action(state)
        clear_task_state(task, state, true)
        return UE.ELuaBehaviorTreeTaskResult.Failed
    end
    if state.CombatComponent:IsCombatAnimationPlaying() == true then
        return UE.ELuaBehaviorTreeTaskResult.InProgress
    end

    if state.CombatComponent:GetCombatActionState()
        ~= UE.ESKCombatActionState.Neutral then
        return UE.ELuaBehaviorTreeTaskResult.InProgress
    end
    if state.StepIndex < #state.Steps then
        state.StepIndex = state.StepIndex + 1
        if not start_current_segment(state, controller) then
            state.Blackboard:SetValueAsString(Runtime.Keys.DebugFailureReason, "NextCombatSegmentFailed")
            invalidate_owned_action(state)
            clear_task_state(task, state, true)
            return UE.ELuaBehaviorTreeTaskResult.Failed
        end
        return UE.ELuaBehaviorTreeTaskResult.InProgress
    end

    local memory = CombatMemory.GetOrCreate(state.Pawn)
    local now_seconds = Runtime.GetWorldTimeSeconds(state.Pawn, state.ElapsedSeconds)
    CombatMemory.ApplyCooldowns(memory, state.Action, "OnComplete", now_seconds)
    CombatMemory.ApplyStateWrites(memory, state.Action, "OnComplete", nil)
    CombatMemory.ApplyStateWrites(memory, state.SelectedVariant, "OnComplete", nil)
    CombatMemory.RecordCompletedAction(memory, state.Action)
    local combat_then_navigation = state.Action.ExecutionMode
        == ActionCatalog.ExecutionMode.CombatThenNavigation
    if combat_then_navigation then
        state.Blackboard:SetValueAsName(Runtime.Keys.TacticalIntent, "PostCombatMove")
    end
    clear_task_state(task, state, not combat_then_navigation)
    return UE.ELuaBehaviorTreeTaskResult.Succeeded
end

---同步释放行为树 Task；立即策略停止自有动作，其余策略移交仍在播放的 Montage 给 CombatComponent。
---@param task ULuaBehaviorTreeTask 当前运行时 Task 实例。
---@param controller AAIController|table|nil 当前 AIController；本函数不直接调用。
---@param pawn APawn|table|nil 当前 Pawn；本函数不直接调用。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard；状态中已锁存引用。
---@param configuration string|nil 原 Execute 配置；Abort 不重新解析。
---@return userdata|number result ELuaBehaviorTreeTaskResult 原生枚举；中止结果 Aborted。
function SKGenichiroExecuteCombatAction.Abort(
    task,
    controller,
    pawn,
    blackboard,
    configuration)
    local _unused = controller or pawn or blackboard or configuration
    local state = TaskStates[task]
    if state == nil then
        return UE.ELuaBehaviorTreeTaskResult.Aborted
    end
    if state.Action.InterruptPolicy == ActionCatalog.InterruptPolicy.ImmediateNavigationAbort then
        invalidate_owned_action(state)
    end
    clear_task_state(task, state, true)
    return UE.ELuaBehaviorTreeTaskResult.Aborted
end

return SKGenichiroExecuteCombatAction
