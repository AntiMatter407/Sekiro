-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Leg IK 的 Lua 编译期节点类型。
-- 节点消费 Foot Placement 生成的 IK 脚目标，并由 UE 原生多骨骼腿部求解器驱动 FK 双腿。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaLegIKNodeConfig: LuaAnimNodeConfig

---@class LuaLegIKNode: LuaAnimNode
---@field ComponentPose LuaAnimPin 包含双脚 IK 目标的组件空间姿势输入 Pin。
---@field Alpha LuaAnimPin Leg IK 强度输入 Pin；不连接时使用原生默认值 1。
---@field Pose LuaAnimPin 双腿完成 IK 求解后的组件空间姿势输出 Pin。
---@field LegDefinitions string 使用竖线分隔双腿、逗号分隔 IK 脚/FK 脚/链长的定义。
---@field ReachPrecision number 腿部末端到目标的收敛精度，单位为厘米。
---@field MaxIterations number 多骨骼腿部求解允许的最大迭代次数。
local LuaLegIKNode = LuaAnimNode:Extend("LuaLegIKNode")

---初始化 Leg IK 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaLegIKNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不执行运行时腿部求解。
function LuaLegIKNode:Initialize(config)
    config.NodeType = "LegIK"
    LuaAnimNode.Initialize(self, config)
end

return LuaLegIKNode
