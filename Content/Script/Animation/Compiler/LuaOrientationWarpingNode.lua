-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Orientation Warping 的 Lua 编译期节点类型。
-- Lua 只声明输入、骨骼配置和调参；姿势旋转、脊柱反向补偿与线程安全求值均由 UE 原生 AnimNode 完成。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaOrientationWarpingNodeConfig: LuaAnimNodeConfig

---@class LuaOrientationWarpingNode: LuaAnimNode
---@field ComponentPose LuaAnimPin 待做方向扭曲的组件空间姿势输入 Pin。
---@field OrientationAngle LuaAnimPin 相对所选四方向动画的剩余角度输入 Pin，单位为度。
---@field Alpha LuaAnimPin 节点强度输入 Pin，0 表示完全旁路，1 表示完整应用。
---@field Pose LuaAnimPin 方向扭曲后的姿势输出 Pin。
---@field SpineBones string 使用竖线分隔的脊柱骨骼名，原生节点会把根旋转反向分配到这些骨骼。
---@field IKFootRootBone string IK 脚根骨骼名；即使脚部旋转占比为零，原生节点仍要求有效引用。
---@field IKFootBones string 使用竖线分隔的 IK 脚骨骼名。
---@field RotationAxis string 旋转轴名称，支持 X、Y、Z。
---@field DistributedBoneOrientationAlpha number 根与脊柱承担的旋转比例，范围为 0..1。
---@field RotationInterpSpeed number 方向扭曲角的原生插值速度，0 表示不插值。
local LuaOrientationWarpingNode = LuaAnimNode:Extend("LuaOrientationWarpingNode")

---初始化 Orientation Warping 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaOrientationWarpingNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不执行运行时姿势求值。
function LuaOrientationWarpingNode:Initialize(config)
    config.NodeType = "OrientationWarping"
    LuaAnimNode.Initialize(self, config)
end

return LuaOrientationWarpingNode
