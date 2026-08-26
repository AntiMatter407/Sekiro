-- Lua 类型：纯 Lua 编译期定义。本文件生成原生 Blackboard 与 BehaviorTree 资产。
-- 树负责目标、反应、阶段、战术、导航和离散动作的可视化分层；710000 权重与状态保留在 Profile/Memory。

local LuaBehaviorTree = require("AI.Compiler.LuaBehaviorTree")

---@class BT_Genichiro: LuaBehaviorTreeDefinition
local BT_Genichiro = LuaBehaviorTree.New({
    SourceModule = "AI.Genichiro.BT_Genichiro",
})

local Value = LuaBehaviorTree.Value

---追加一个普通 Blackboard Key，保持跨系统契约集中且可审计。
---@param blackboard LuaBehaviorTreeDefinition Blackboard 编译期声明器。
---@param key_class string BlackboardKeyType 脚本类路径。
---@param key_name string Key 名称。
---@param instance_synced boolean 是否实例同步。
---@return nil 无返回值。
local function key(blackboard, key_class, key_name, instance_synced)
    blackboard:Key(key_class, key_name, {}, instance_synced == true)
end

---声明 AIController、行为树 Task、CombatComponent 与 AnimInstance 的完整共享契约。
---@param blackboard LuaBehaviorTreeDefinition Blackboard 编译期声明器。
---@return nil 无返回值。
function BT_Genichiro:DeclareBlackboard(blackboard)
    blackboard:Key("/Script/AIModule.BlackboardKeyType_Object", "TargetActor", {
        BaseClass = Value.Class("/Script/Engine.Pawn"),
    }, false)

    for _index, key_name in ipairs({
        "BossPhase", "ReactionPriority", "ActionSerial", "DebugDecisionSerial",
    }) do
        key(blackboard, "/Script/AIModule.BlackboardKeyType_Int", key_name, false)
    end
    for _index, key_name in ipairs({
        "TacticalIntent", "SelectedActionId", "ReactionType", "DistanceBand",
    }) do
        key(blackboard, "/Script/AIModule.BlackboardKeyType_Name", key_name, false)
    end
    for _index, key_name in ipairs({ "DistanceCm", "SelfHealthRatio" }) do
        key(blackboard, "/Script/AIModule.BlackboardKeyType_Float", key_name, false)
    end
    key(blackboard, "/Script/AIModule.BlackboardKeyType_Vector", "MoveGoal", false)
    for _index, key_name in ipairs({ "DebugCandidateSummary", "DebugFailureReason" }) do
        key(blackboard, "/Script/AIModule.BlackboardKeyType_String", key_name, false)
    end
    for _index, key_name in ipairs({
        "bCanMoveLeft", "bCanMoveRight", "bCanMoveBack",
        "bProjectileCapabilityReady", "bActionLocked", "bPhaseTransitionPending",
        "LegacyPhaseFlagA", "LegacyPhaseFlagB", "TargetPunishWindow",
        "TargetRepositionRestricted", "TargetSpecialAction", "TargetInFront",
        "TargetBehind", "TargetStatePunish", "TargetState110030",
        "ExternalTimer7Ready",
    }) do
        key(blackboard, "/Script/AIModule.BlackboardKeyType_Bool", key_name, false)
    end
end

---给移动分支追加 BuildMoveGoal、原生 MoveTo 与 Commit 三段所有权链。
---@param branch LuaBehaviorTreeNodeBuilder 当前 Sequence。
---@param prefix string 节点显示名前缀。
---@return nil 无返回值。
local function append_navigation(branch, prefix)
    branch:LuaTask(prefix .. "BuildMoveGoal", "AI.Tasks.SKGenichiroBuildMoveGoal", "Build")
    branch:Task("/Script/AIModule.BTTask_MoveTo", prefix .. "MoveToGoal", {
        BlackboardKey = Value.Struct({ SelectedKeyName = Value.Name("MoveGoal") }),
        AcceptableRadius = Value.Float(45.0),
        ObservedBlackboardValueTolerance = Value.Float(20.0),
        bObserveBlackboardValue = Value.Bool(true),
        bAllowStrafe = Value.Bool(true),
        bAllowPartialPath = Value.Bool(false),
        bTrackMovingGoal = Value.Bool(false),
        bProjectGoalLocation = Value.Bool(false),
        bReachTestIncludesAgentRadius = Value.Bool(true),
        bReachTestIncludesGoalRadius = Value.Bool(false),
    })
    branch:LuaTask(prefix .. "CommitMove", "AI.Tasks.SKGenichiroBuildMoveGoal", "Commit")
end

---声明反应优先、阶段切换、普通战术与安全留守的 UE 原生树拓扑。
---@param tree LuaBehaviorTreeDefinition 行为树编译期声明器。
---@return nil 无返回值。
function BT_Genichiro:BehaviorTree(tree)
    local root = tree:Composite("/Script/AIModule.BTComposite_Selector", "GenichiroBossBehavior")
    local engage = root:Composite("/Script/AIModule.BTComposite_Sequence", "EngageTarget")
    engage:Decorator("/Script/AIModule.BTDecorator_Blackboard", "HasTarget", {
        BlackboardKey = Value.Struct({ SelectedKeyName = Value.Name("TargetActor") }),
        FlowAbortMode = Value.Enum("Both"),
    })
    engage:Service("/Script/AIModule.BTService_DefaultFocus", "FocusTarget", {
        BlackboardKey = Value.Struct({ SelectedKeyName = Value.Name("TargetActor") }),
    })
    engage:LuaTask("UpdateContext", "AI.Tasks.SKGenichiroUpdateContext", "")

    local priority = engage:Composite("/Script/AIModule.BTComposite_Selector", "ReactionPhaseTacticalPriority")

    local reaction = priority:Composite("/Script/AIModule.BTComposite_Sequence", "ImmediateReaction")
    reaction:LuaTask("RouteReaction", "AI.Tasks.SKGenichiroReactionRouter", "")
    reaction:LuaTask("ExecuteReaction", "AI.Tasks.SKGenichiroExecuteCombatAction", "")
    reaction:LuaTask("ClearReaction", "AI.Tasks.SKGenichiroReactionRouter", "Clear")

    local phase = priority:Composite("/Script/AIModule.BTComposite_Sequence", "PhaseTransition")
    phase:LuaTask("ConsumePhaseTransition", "AI.Tasks.SKGenichiroPhaseTransition", "")
    phase:Task("/Script/AIModule.BTTask_Wait", "PhaseReassess", {
        WaitTime = Value.Float(0.05), RandomDeviation = Value.Float(0.0),
    })

    local tactical = priority:Composite("/Script/AIModule.BTComposite_Sequence", "TacticalDecision")
    tactical:LuaTask("SelectIntent", "AI.Tasks.SKGenichiroSelectIntent", "")
    local intent = tactical:Composite("/Script/AIModule.BTComposite_Selector", "ExecuteTacticalIntent")

    local combat = intent:Composite("/Script/AIModule.BTComposite_Sequence", "CombatAction")
    combat:LuaTask("GateCombatAction", "AI.Tasks.SKGenichiroIntentGate", "CombatAction")
    combat:LuaTask("ExecuteCombatAction", "AI.Tasks.SKGenichiroExecuteCombatAction", "")

    local approach_combat = intent:Composite("/Script/AIModule.BTComposite_Sequence", "ApproachThenCombat")
    approach_combat:LuaTask("GateApproachCombat", "AI.Tasks.SKGenichiroIntentGate", "ApproachThenCombat")
    append_navigation(approach_combat, "Approach")
    approach_combat:LuaTask("ExecuteApproachCombat", "AI.Tasks.SKGenichiroExecuteCombatAction", "")

    local move_combat = intent:Composite("/Script/AIModule.BTComposite_Sequence", "MoveThenCombat")
    move_combat:LuaTask("GateMoveCombat", "AI.Tasks.SKGenichiroIntentGate", "MoveThenCombat")
    append_navigation(move_combat, "PreCombat")
    move_combat:LuaTask("ExecuteMoveCombat", "AI.Tasks.SKGenichiroExecuteCombatAction", "")

    local combat_move = intent:Composite("/Script/AIModule.BTComposite_Sequence", "CombatThenMove")
    combat_move:LuaTask("GateCombatMove", "AI.Tasks.SKGenichiroIntentGate", "CombatThenMove")
    combat_move:LuaTask("ExecuteBeforeMove", "AI.Tasks.SKGenichiroExecuteCombatAction", "")
    append_navigation(combat_move, "PostCombat")

    local reposition = intent:Composite("/Script/AIModule.BTComposite_Sequence", "Reposition")
    reposition:LuaTask("GateReposition", "AI.Tasks.SKGenichiroIntentGate", "Reposition|PostCombatMove")
    append_navigation(reposition, "Reposition")

    local hold = intent:Composite("/Script/AIModule.BTComposite_Sequence", "DefensiveHold")
    hold:LuaTask("GateHold", "AI.Tasks.SKGenichiroIntentGate", "Hold")
    hold:Task("/Script/AIModule.BTTask_Wait", "HoldAndReassess", {
        WaitTime = Value.Float(0.15), RandomDeviation = Value.Float(0.05),
    })

    root:Task("/Script/AIModule.BTTask_Wait", "HoldArenaWithoutTarget", {
        WaitTime = Value.Float(0.40), RandomDeviation = Value.Float(0.10),
    })
end

return BT_Genichiro
