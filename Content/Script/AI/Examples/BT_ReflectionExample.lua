-- Lua 类型：纯 Lua 数据示例。仅展示反射行为树声明，不包含项目角色、Boss 或运行时业务逻辑。

local LuaBehaviorTree = require("AI.Compiler.LuaBehaviorTree")

---@class LuaBehaviorTreeReflectionExample: LuaBehaviorTreeDefinition
local Example = LuaBehaviorTree.New({
    SourceModule = "AI.Examples.BT_ReflectionExample",
})

---声明一个不包含项目业务的 Bool Blackboard Key。
---@param blackboard LuaBehaviorTreeDefinition Blackboard 声明器。
---@return nil result 本函数只追加声明，不返回值。
function Example:DeclareBlackboard(blackboard)
    blackboard:Key(
        "/Script/AIModule.BlackboardKeyType_Bool",
        "bExampleEnabled",
        nil,
        false)
end

---声明 Selector 与 Wait，用于验证任意原生节点无需注册即可生成。
---@param tree LuaBehaviorTreeDefinition 行为树声明器。
---@return nil result 本函数只追加声明，不返回值。
function Example:BehaviorTree(tree)
    local root = tree:Composite(
        "/Script/AIModule.BTComposite_Selector",
        "Root")

    root:Task(
        "/Script/AIModule.BTTask_Wait",
        "Wait",
        {
            WaitTime = LuaBehaviorTree.Value.Float(0.25),
            RandomDeviation = LuaBehaviorTree.Value.Float(0.0),
        })
end

return Example
