-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- AnimGraph 节点 Pin 的编译期对象。
-- Pin 只记录类型和连接方向；Connect 最终生成原生 AnimGraph Link IR。
local CompilerClass = require("Animation.Compiler.CompilerClass")
local PinDirection = UE.ELuaAnimIRPinDirection

---@class LuaAnimPinConfig
---@field Node LuaAnimNode Pin 所属节点。
---@field Name string C++ 节点契约中的稳定 Pin 名。
---@field Direction SekiroAnimIRPinDirection Pin 的数据流方向。
---@field DataType string C++ 节点契约中的稳定数据类型。

---@class LuaAnimPin: CompilerClass
---@field Node LuaAnimNode Pin 所属节点。
---@field Graph LuaAnimGraph Pin 所属 Graph。
---@field Name string C++ 节点契约中的稳定 Pin 名。
---@field Direction SekiroAnimIRPinDirection Pin 的数据流方向。
---@field DataType string C++ 节点契约中的稳定数据类型。
local LuaAnimPin = CompilerClass:Extend("LuaAnimPin")

---初始化节点 Pin；Pin 身份完全来自注册契约，业务代码不能自行改变方向和类型。
---@param config LuaAnimPinConfig 节点、Pin 名、方向和数据类型。
---@return nil result 该函数只保存编译期 Pin 元数据。
function LuaAnimPin:Initialize(config)
    self.Node = assert(config.Node, "LuaAnimPin requires Node")
    self.Graph = self.Node.Graph
    self.Name = assert(config.Name, "LuaAnimPin requires Name")
    self.Direction = assert(config.Direction, "LuaAnimPin requires Direction")
    self.DataType = assert(config.DataType, "LuaAnimPin requires DataType")
end

---把一个输出 Pin 连接到当前输入 Pin，写法与动画蓝图中把连线接入目标 Pin 一致。
---@param source_pin LuaAnimPin 提供数据的输出 Pin，必须与当前 Pin 位于同一 Graph 且类型一致。
---@return SekiroAnimIRLink link 新建的类型化 Graph Link。
function LuaAnimPin:Connect(source_pin)
    assert(self.Direction == PinDirection.Input, string.format(
        "Connect target '%s.%s' must be an Input Pin",
        self.Node.Name,
        self.Name))
    assert(source_pin ~= nil and source_pin.Direction == PinDirection.Output, "Connect source must be an Output Pin")
    return self.Graph:LinkPins(source_pin, self)
end

return LuaAnimPin
