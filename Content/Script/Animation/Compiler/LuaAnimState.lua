-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- StateMachine Graph 中的编译期 State 顶点。
-- 每个 State 独占一个 LuaAnimStateGraph；GraphId 是所有权边，不允许多个 State 共享。
local CompilerClass = require("Animation.Compiler.CompilerClass")

---@class LuaAnimStateConfig
---@field MachineGraph LuaAnimStateMachineGraph 所属内部状态机 Graph。
---@field Id string State 稳定 ID。
---@field Name string State 语义名称。
---@field PoseGraph LuaAnimStateGraph State 独占的 Pose Graph。
---@field Settings LuaAnimStateSettings|nil State 编译设置。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation State 源码位置。

---@class LuaAnimState: CompilerClass
---@field MachineGraph LuaAnimStateMachineGraph 所属内部状态机 Graph。
---@field Id string State 稳定 ID。
---@field Name string State 语义名称。
---@field PoseGraph LuaAnimStateGraph State 独占的 Pose Graph。
---@field GraphId string State 独占 Pose Graph 的稳定 ID。
---@field bAlwaysResetOnEntry boolean 重新进入状态时是否重置 Pose Graph。
---@field DeclarationOrder number 源码中的确定性声明顺序整数。
---@field SourceLocation SekiroAnimIRSourceLocation State 源码位置。
local LuaAnimState = CompilerClass:Extend("LuaAnimState")

---初始化 State 顶点并绑定已经创建的独占 StatePose Graph。
---@param config LuaAnimStateConfig StateMachine Graph、稳定身份、Pose Graph 和编译设置。
---@return nil result 该函数只初始化编译期 State，不返回业务值。
function LuaAnimState:Initialize(config)
    self.MachineGraph = assert(config.MachineGraph, "LuaAnimState requires MachineGraph")
    self.Id = assert(config.Id, "LuaAnimState requires Id")
    self.Name = assert(config.Name, "LuaAnimState requires Name")
    self.PoseGraph = assert(config.PoseGraph, "LuaAnimState requires PoseGraph")
    self.GraphId = self.PoseGraph.Id
    self.bAlwaysResetOnEntry = config.Settings ~= nil
        and config.Settings.bAlwaysResetOnEntry == true
    self.DeclarationOrder = config.DeclarationOrder or 0
    self.SourceLocation = assert(config.SourceLocation, "LuaAnimState requires SourceLocation")
end

---导出与 FSekiroAnimIRState 对应的状态及其独占 Graph 引用。
---@return SekiroAnimIRState ir_state 可由 C++ 导入器解析的 State 声明。
function LuaAnimState:ToIR()
    return {
        Id = self.Id,
        Name = self.Name,
        GraphId = self.GraphId,
        bAlwaysResetOnEntry = self.bAlwaysResetOnEntry,
        DeclarationOrder = self.DeclarationOrder,
        SourceLocation = self.SourceLocation,
    }
end

return LuaAnimState
