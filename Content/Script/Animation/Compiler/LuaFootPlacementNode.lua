-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Foot Placement 的 Lua 编译期节点类型。
-- Lua 只声明双脚、骨盆和地面检测参数；射线、脚部锁定与骨盆求解由 UE 原生 AnimNode 完成。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaFootPlacementNodeConfig: LuaAnimNodeConfig

---@class LuaFootPlacementNode: LuaAnimNode
---@field ComponentPose LuaAnimPin 待处理的组件空间姿势输入 Pin。
---@field Alpha LuaAnimPin Foot Placement 强度输入 Pin；不连接时使用原生默认值 1。
---@field Pose LuaAnimPin 调整骨盆和双脚 IK 目标后的组件空间姿势输出 Pin。
---@field IKFootRootBone string 双脚 IK 目标共同所属的根骨骼名。
---@field PelvisBone string 允许原生节点垂直补偿的骨盆骨骼名。
---@field LegDefinitions string 使用竖线分隔双腿、逗号分隔 FK 脚/IK 脚/脚趾/链长的定义。
---@field PlantSpeedMode string 脚部种植速度来源，支持 Graph 或 Manual。
---@field PlantLockType string 脚部锁定方式，支持 Unlocked、PivotAroundBall、PivotAroundAnkle 或 LockRotation。
---@field PelvisMaxOffset number 骨盆相对输入姿势的最大偏移，单位为厘米。
---@field PelvisHorizontalRebalancingWeight number 骨盆水平重心补偿权重，范围为 0..1。
---@field PlantSpeedThreshold number 低于该组件空间速度时脚部可视为种植，单位为厘米每秒。
---@field PlantDistanceToGround number 脚部进入完整地面对齐的最大距离，单位为厘米。
---@field TraceStartOffset number 地面检测起点相对脚部的竖直偏移，单位为厘米。
---@field TraceEndOffset number 地面检测终点相对脚部的竖直偏移，单位为厘米。
---@field TraceSweepRadius number 地面球扫半径，单位为厘米。
---@field TraceMaxGroundPenetration number 允许脚部穿入碰撞面的最大深度，单位为厘米。
---@field bTraceEnabled boolean 是否启用原生双脚地面检测。
local LuaFootPlacementNode = LuaAnimNode:Extend("LuaFootPlacementNode")

---初始化 Foot Placement 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaFootPlacementNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不执行运行时地面检测。
function LuaFootPlacementNode:Initialize(config)
    config.NodeType = "FootPlacement"
    LuaAnimNode.Initialize(self, config)
end

return LuaFootPlacementNode
