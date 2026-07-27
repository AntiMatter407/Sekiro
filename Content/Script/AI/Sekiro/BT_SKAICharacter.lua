-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self 只表示行为树编译期定义。
-- 声明复用主角外观与战斗组件的基础 AI 行为：感知到目标时追击，未发现目标时巡逻。
-- 节点结构在编辑器阶段生成 UE 原生 Blackboard 与 BehaviorTree，运行时不解释本 Lua 文件。

local LuaBehaviorTree = require("AI.Compiler.LuaBehaviorTree")

---@class BT_SKAICharacter: LuaBehaviorTreeDefinition
local BT_SKAICharacter = LuaBehaviorTree.New({
    SourceModule = "AI.Sekiro.BT_SKAICharacter",
})

local Value = LuaBehaviorTree.Value

---声明 AIController、感知系统和原生 MoveTo 共享的运行时状态。
---@param blackboard LuaBehaviorTreeDefinition Blackboard 编译期声明器。
---@return nil result 本函数只追加目标对象和巡逻位置声明。
function BT_SKAICharacter:DeclareBlackboard(blackboard)
    blackboard:Key(
        "/Script/AIModule.BlackboardKeyType_Object",
        "TargetActor",
        {
            BaseClass = Value.Class("/Script/Engine.Pawn"),
        },
        false)

    blackboard:Key(
        "/Script/AIModule.BlackboardKeyType_Vector",
        "PatrolLocation",
        nil,
        false)
end

---声明“战斗反应、近身攻击、目标追击、无目标巡逻”的优先级 Selector。
---TargetActor 改变时 Blackboard Decorator 会中止整个目标分支；精确攻击生命周期由 Lua Task 等待。
---@param tree LuaBehaviorTreeDefinition 行为树编译期声明器。
---@return nil result 本函数只追加原生行为树节点声明。
function BT_SKAICharacter:BehaviorTree(tree)
    local root = tree:Composite(
        "/Script/AIModule.BTComposite_Selector",
        "AIBehavior")

    local chase_target = root:Composite(
        "/Script/AIModule.BTComposite_Sequence",
        "ChaseTarget")

    chase_target:Decorator(
        "/Script/AIModule.BTDecorator_Blackboard",
        "HasTarget",
        {
            BlackboardKey = Value.Struct({
                SelectedKeyName = Value.Name("TargetActor"),
            }),
            FlowAbortMode = Value.Enum("Both"),
        })

    chase_target:Service(
        "/Script/AIModule.BTService_DefaultFocus",
        "FocusTarget",
        {
            BlackboardKey = Value.Struct({
                SelectedKeyName = Value.Name("TargetActor"),
            }),
        })

    local combat_decision = chase_target:Composite(
        "/Script/AIModule.BTComposite_Selector",
        "CombatDecision")

    combat_decision:LuaTask(
        "CombatReactionWait",
        "AI.Tasks.SKCombatReactionWait",
        "")

    local attack_target = combat_decision:Composite(
        "/Script/AIModule.BTComposite_Sequence",
        "AttackTarget")

    attack_target:LuaTask(
        "CombatAttack",
        "AI.Tasks.SKCombatAttack",
        "230.0,40.0,0.25")

    attack_target:Task(
        "/Script/AIModule.BTTask_Wait",
        "AttackCooldown",
        {
            WaitTime = Value.Float(0.45),
            RandomDeviation = Value.Float(0.20),
        })

    combat_decision:Task(
        "/Script/AIModule.BTTask_MoveTo",
        "MoveToTarget",
        {
            BlackboardKey = Value.Struct({
                SelectedKeyName = Value.Name("TargetActor"),
            }),
            AcceptableRadius = Value.Float(180.0),
            ObservedBlackboardValueTolerance = Value.Float(50.0),
            bObserveBlackboardValue = Value.Bool(true),
            bAllowStrafe = Value.Bool(false),
            bAllowPartialPath = Value.Bool(true),
            bTrackMovingGoal = Value.Bool(true),
            bProjectGoalLocation = Value.Bool(true),
            bReachTestIncludesAgentRadius = Value.Bool(true),
            bReachTestIncludesGoalRadius = Value.Bool(true),
        })

    chase_target:Task(
        "/Script/AIModule.BTTask_Wait",
        "ChaseInterval",
        {
            WaitTime = Value.Float(0.1),
            RandomDeviation = Value.Float(0.0),
        })

    local patrol = root:Composite(
        "/Script/AIModule.BTComposite_Sequence",
        "Patrol")

    patrol:Task(
        "/Script/AIModule.BTTask_MoveTo",
        "MoveToPatrolLocation",
        {
            BlackboardKey = Value.Struct({
                SelectedKeyName = Value.Name("PatrolLocation"),
            }),
            AcceptableRadius = Value.Float(100.0),
            bAllowStrafe = Value.Bool(false),
            bAllowPartialPath = Value.Bool(true),
            bProjectGoalLocation = Value.Bool(true),
            bReachTestIncludesAgentRadius = Value.Bool(true),
            bReachTestIncludesGoalRadius = Value.Bool(false),
        })

    patrol:LuaTask(
        "PatrolWait",
        "AI.Tasks.LuaWait",
        "2.0")
end

return BT_SKAICharacter
