-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Two Bone IK 的 Lua 编译期节点类型。
-- Lua 只声明骨骼链、目标空间与求解参数；手臂求解由 UE 原生 AnimNode 执行。
local LuaAnimNode = require("Animation.Compiler.LuaAnimNode")

---@class LuaTwoBoneIKNodeConfig: LuaAnimNodeConfig

---@class LuaTwoBoneIKNode: LuaAnimNode
---@field ComponentPose LuaAnimPin 待处理的组件空间姿势输入 Pin。
---@field Alpha LuaAnimPin IK 强度输入 Pin；使用曲线 Alpha 时无需连接。
---@field Pose LuaAnimPin 完成双骨骼链求解后的组件空间姿势输出 Pin。
---@field IKBone string 双骨骼链末端骨骼名；父骨骼和祖父骨骼组成完整求解链。
---@field EffectorLocationSpace string Effector 位置空间，支持 WorldSpace、ComponentSpace、ParentBoneSpace 或 BoneSpace。
---@field EffectorTargetBoneName string Effector 使用 BoneSpace 时的目标骨骼名；与 Socket 目标互斥。
---@field EffectorTargetSocketName string Effector 使用 BoneSpace 时的目标 Socket 名；与骨骼目标互斥。
---@field EffectorLocationX number Effector 在目标空间中的 X 偏移，单位为厘米。
---@field EffectorLocationY number Effector 在目标空间中的 Y 偏移，单位为厘米。
---@field EffectorLocationZ number Effector 在目标空间中的 Z 偏移，单位为厘米。
---@field JointTargetLocationSpace string 关节方向目标空间。
---@field JointTargetBoneName string 关节方向使用 BoneSpace 时的目标骨骼名；与 Socket 目标互斥。
---@field JointTargetSocketName string 关节方向使用 BoneSpace 时的目标 Socket 名；与骨骼目标互斥。
---@field JointTargetLocationX number 关节方向目标在目标空间中的 X 偏移，单位为厘米。
---@field JointTargetLocationY number 关节方向目标在目标空间中的 Y 偏移，单位为厘米。
---@field JointTargetLocationZ number 关节方向目标在目标空间中的 Z 偏移，单位为厘米。
---@field bTakeRotationFromEffectorSpace boolean 是否让末端骨骼继承 Effector 目标空间旋转。
---@field bAllowStretching boolean 是否允许双骨骼链超出原长度。
---@field StartStretchRatio number 开始拉伸时目标距离与原链长的比值。
---@field MaxStretchScale number 最大允许链长相对原长度的倍数。
---@field AlphaInputType string Alpha 来源，支持 Float、Bool 或 Curve。
---@field AlphaCurveName string AlphaInputType 为 Curve 时读取的动画曲线名。
local LuaTwoBoneIKNode = LuaAnimNode:Extend("LuaTwoBoneIKNode")

---初始化 Two Bone IK 节点身份；Pin 与可写属性由 C++ 契约镜像生成。
---@param config LuaTwoBoneIKNodeConfig Graph、节点名和源码位置。
---@return nil result 该函数只初始化编译期节点，不执行运行时 IK 求解。
function LuaTwoBoneIKNode:Initialize(config)
    config.NodeType = "TwoBoneIK"
    LuaAnimNode.Initialize(self, config)
end

return LuaTwoBoneIKNode
