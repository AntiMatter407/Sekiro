-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Layered Blend Per Bone 的 Lua 编译期节点类型。
-- 当前通用包装固定提供一个覆盖姿势；BranchFilters 决定该姿势影响的骨骼分支。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaLayeredBlendPerBoneNodeConfig: LuaAnimNodeConfig

---@class LuaLayeredBlendPerBoneNode: LuaAnimNode
---@field BasePose LuaAnimPin 全身基础姿势输入 Pin。
---@field BlendPose LuaAnimPin 按骨骼过滤器覆盖基础姿势的输入 Pin。
---@field BlendWeight LuaAnimPin 覆盖姿势权重输入 Pin；不连接时使用 UE 默认值 1。
---@field Pose LuaAnimPin 分骨骼合成后的姿势输出 Pin。
---@field BranchFilters string `BoneName,BlendDepth|...` 格式的一个或多个分支过滤器。
---@field bMeshSpaceRotationBlend boolean 是否在 Mesh Space 混合骨骼旋转。
---@field bMeshSpaceScaleBlend boolean 是否在 Mesh Space 混合骨骼缩放。
---@field CurveBlendOption string 曲线混合策略名称，默认 Override。
---@field bBlendRootMotionBasedOnRootBone boolean 是否按根骨骼权重混合 Root Motion。
local LuaLayeredBlendPerBoneNode = LuaAnimNode:Extend("LuaLayeredBlendPerBoneNode")

---初始化 Layered Blend Per Bone 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaLayeredBlendPerBoneNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不执行运行时姿势求值。
function LuaLayeredBlendPerBoneNode:Initialize(config)
    config.NodeType = "LayeredBlendPerBone"
    LuaAnimNode.Initialize(self, config)
end

return LuaLayeredBlendPerBoneNode
