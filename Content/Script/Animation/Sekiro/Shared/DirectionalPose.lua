-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 本工具只在动画蓝图编辑器生成期组装原生 Pose 节点。
-- 地面锁定移动使用 Graph Orientation Warping 同步重定向 Root Motion 与下半身；
-- Jump 等不修改 Root Motion 的分支继续使用 Manual 模式补齐量化残差。

local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SekiroDirectionalPoseLibrary
local DirectionalPose = {}

---在方向素材之后追加量化残差对齐链。
---Alpha 为 0 时原生节点完全旁路，因此同一张状态图可安全混合非锁定、原地和定向分支。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前原生 Pose Graph。
---@param name string 空间转换、属性和 Orientation Warping 节点的语义前缀。
---@param source_pose LuaAnimNode 已选出最近方向动画的局部空间 Pose 节点。
---@param residual_angle_variable string 已声明的有符号量化残差变量名，单位为度。
---@param alpha_variable string 已声明的方向对齐强度变量名，范围为 0..1。
---@param rotation_interp_speed number|nil 节点内部角度插值速度；nil 使用集中配置，0 表示立即应用目标角。
---@return LuaComponentToLocalSpaceNode aligned_pose 已对齐下半身并保留上半身目标朝向的局部空间 Pose。
function DirectionalPose.Align(
    Graph,
    name,
    source_pose,
    residual_angle_variable,
    alpha_variable,
    rotation_interp_speed)
    local residual_angle = Graph:Property(name .. "ResidualAngle", residual_angle_variable)
    local alignment_alpha = Graph:Property(name .. "Alpha", alpha_variable)
    local warping_settings = Tuning.LockOnOrientationWarping
    local to_component = Graph:LocalToComponentSpace(name .. "LocalToComponent")
    local warping = Graph:OrientationWarping(name .. "OrientationWarping")
    local to_local = Graph:ComponentToLocalSpace(name .. "ComponentToLocal")

    warping.SpineBones = warping_settings.SpineBones
    warping.IKFootRootBone = warping_settings.IKFootRootBone
    warping.IKFootBones = warping_settings.IKFootBones
    warping.RotationAxis = warping_settings.RotationAxis
    warping.DistributedBoneOrientationAlpha = warping_settings.DistributedBoneOrientationAlpha
    warping.RotationInterpSpeed = rotation_interp_speed ~= nil
        and rotation_interp_speed
        or Tuning.LockOnWarpingInterpSpeed
    to_component.LocalPose:Connect(source_pose.Pose)
    warping.ComponentPose:Connect(to_component.ComponentPose)
    warping.OrientationAngle:Connect(residual_angle.Value)
    warping.Alpha:Connect(alignment_alpha.Value)
    to_local.ComponentPose:Connect(warping.Pose)
    return to_local
end

---在锁定四向素材之后追加 Graph Orientation Warping。
---节点从输入 Pose 的 RootMotionDelta 属性读取动画原方向，把位移和下半身一起转向 LocomotionAngle；
---SpineBones 由原生节点反向补偿。Alpha 必须使用严格的 0/1，0 时整节点旁路，避免非锁定分支改写 Root Motion。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前原生 Pose Graph。
---@param name string 空间转换、属性和 Orientation Warping 节点的语义前缀。
---@param source_pose LuaAnimNode 已选出四向动画的局部空间 Pose 节点。
---@param locomotion_angle_variable string 实际移动方向相对当前 Actor 的角度变量名，单位为度。
---@param alpha_variable string Graph 模式启用权重变量名；当前约定只写入 0 或 1。
---@return LuaComponentToLocalSpaceNode aligned_pose Root Motion 与下半身已同步重定向的局部空间 Pose。
function DirectionalPose.GraphAlign(
    Graph,
    name,
    source_pose,
    locomotion_angle_variable,
    alpha_variable)
    local locomotion_angle = Graph:Property(name .. "LocomotionAngle", locomotion_angle_variable)
    local alignment_alpha = Graph:Property(name .. "Alpha", alpha_variable)
    local warping_settings = Tuning.LockOnOrientationWarping
    local to_component = Graph:LocalToComponentSpace(name .. "LocalToComponent")
    local warping = Graph:OrientationWarping(name .. "OrientationWarping")
    local to_local = Graph:ComponentToLocalSpace(name .. "ComponentToLocal")

    warping.Mode = "Graph"
    warping.SpineBones = warping_settings.SpineBones
    warping.IKFootRootBone = warping_settings.IKFootRootBone
    warping.IKFootBones = warping_settings.IKFootBones
    warping.RotationAxis = warping_settings.RotationAxis
    warping.DistributedBoneOrientationAlpha = warping_settings.DistributedBoneOrientationAlpha
    warping.RotationInterpSpeed = Tuning.LockOnWarpingInterpSpeed
    -- 编辑器可能已缓存旧版 Tuning 模块；回退值保证首次热生成不因新增字段为 nil 而中断。
    warping.MinRootMotionSpeedThreshold = warping_settings.MinRootMotionSpeedThreshold or 3.0
    warping.LocomotionAngleDeltaThreshold = warping_settings.LocomotionAngleDeltaThreshold or 90.0
    warping.WarpingAlpha = 1.0
    warping.OffsetAlpha = 0.0
    to_component.LocalPose:Connect(source_pose.Pose)
    warping.ComponentPose:Connect(to_component.ComponentPose)
    warping.LocomotionAngle:Connect(locomotion_angle.Value)
    warping.Alpha:Connect(alignment_alpha.Value)
    to_local.ComponentPose:Connect(warping.Pose)
    return to_local
end

return DirectionalPose
