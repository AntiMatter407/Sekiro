local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaPropertyGetterNodeConfig: LuaAnimNodeConfig
---@field Variable SekiroAnimIRVariable 要读取的 GeneratedClass 变量声明。

---@class LuaPropertyGetterNode: LuaAnimNode
---@field Value LuaAnimPin 变量的只读数据输出 Pin。
local LuaPropertyGetterNode = LuaAnimNode:Extend("LuaPropertyGetterNode")

---创建 AnimInstance 成员变量 Getter；生成后由原生 UK2Node_VariableGet 在动画线程读取快照。
---@param config LuaPropertyGetterNodeConfig Graph、节点名和变量声明。
---@return nil result 仅初始化编译期节点。
function LuaPropertyGetterNode:Initialize(config)
    local variable = assert(config.Variable, "PropertyGetter requires Variable")
    local node_types = {
        Bool = "BoolPropertyGetter",
        Float = "FloatPropertyGetter",
        Byte = "BytePropertyGetter",
        Enum = "EnumPropertyGetter",
    }
    config.NodeType = assert(node_types[variable.DataType], "Unsupported PropertyGetter variable type")
    LuaAnimNode.Initialize(self, config)
    self.PropertyName = variable.Name
end

return LuaPropertyGetterNode
