-- Lua 类型：BehaviorTree Task 模块。本文件只校验当前 Blackboard 战术意图。
-- 使用显式 Gate 可让 UE BehaviorTree 编辑器稳定显示各语义分支，同时避免依赖 Name Key 的反射比较细节。

local Runtime = require("AI.Genichiro.GenichiroRuntime")

local SKGenichiroIntentGate = {}

---校验当前 TacticalIntent 是否与配置相同；配置可用竖线声明多个允许值。
---@param task USekiroLuaBehaviorTreeTask 当前运行时 Task 实例；本任务不持有状态。
---@param controller AAIController|table|nil 当前 AIController；本任务不直接调用。
---@param pawn APawn|table|nil 当前 Pawn；本任务不直接调用。
---@param blackboard UBlackboardComponent|table|nil 当前 Blackboard。
---@param configuration string|nil 允许的 TacticalIntent，多个值用竖线分隔。
---@return string result 匹配时 Succeeded，否则 Failed。
function SKGenichiroIntentGate.Execute(task, controller, pawn, blackboard, configuration)
    local _unused = task or controller or pawn
    if blackboard == nil then
        return "Failed"
    end
    local current_intent = Runtime.NameToString(
        blackboard:GetValueAsName(Runtime.Keys.TacticalIntent))
    for expected_intent in string.gmatch(configuration or "", "[^|]+") do
        if current_intent == expected_intent then
            return "Succeeded"
        end
    end
    return "Failed"
end

return SKGenichiroIntentGate
