-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- 可拆分到独立文件的 Lua 状态机描述基类。
-- 子类只声明 StateMachine 拓扑、StateGraph_* 与强类型 Rule；旧模块可保留 CanEnter_*，构建顺序由 LuaAnimBlueprint 统一管理。
local CompilerClass = require("Animation.Compiler.CompilerClass")

---@class LuaAnimStateMachine: CompilerClass
local LuaAnimStateMachine = CompilerClass:Extend("LuaAnimStateMachine")

return LuaAnimStateMachine
