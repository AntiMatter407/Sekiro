-- 可拆分到独立文件的 Lua 状态机描述基类。
-- 子类只声明 StateMachine 拓扑、StateGraph_* 和 CanEnter_*，构建顺序由 LuaAnimBlueprint 统一管理。
local CompilerClass = require("Animation.Compiler.CompilerClass")

---@class LuaAnimStateMachine: CompilerClass
local LuaAnimStateMachine = CompilerClass:Extend("LuaAnimStateMachine")

return LuaAnimStateMachine
