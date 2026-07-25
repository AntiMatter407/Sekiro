-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 本工具只在动画蓝图编辑器生成期组装原生 Pose 节点。
-- 地面锁定移动只回正脊柱，ActorYaw 与四方向素材共同决定原始 Root Motion 世界轨迹；
-- Jump 等不旋转 Actor 的分支仍可使用 Orientation Warping 补齐量化残差。

local Tuning = require("Animation.Sekiro.Shared.Tuning")

---@class SekiroDirectionalPoseLibrary
local DirectionalPose = {}

---只对脊柱施加锁定目标回正，不旋转 Root 或改变 Root Motion。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前原生 Pose Graph。
---@param name string 节点语义前缀。
---@param source_pose LuaAnimNode 已选择的方向素材姿势。
---@param yaw_variable string 脊柱补偿角变量名。
---@param alpha_variable string 补偿权重变量名。
---@return LuaComponentToLocalSpaceNode pose_node 回正后的局部空间姿势。
function DirectionalPose.SpineYawCompensation(Graph, name, source_pose, yaw_variable, alpha_variable)
    local yaw_angle = Graph:Property(name .. "YawAngle", yaw_variable)
    local alpha = Graph:Property(name .. "Alpha", alpha_variable)
    local to_component = Graph:LocalToComponentSpace(name .. "LocalToComponent")
    local compensation = Graph:SpineYawCompensation(name .. "SpineYawCompensation")
    local to_local = Graph:ComponentToLocalSpace(name .. "ComponentToLocal")
    compensation.SpineBones = "Spine|Spine1|Spine2"
    compensation.RotationAxis = "Z"
    to_component.LocalPose:Connect(source_pose.Pose)
    compensation.ComponentPose:Connect(to_component.ComponentPose)
    compensation.YawAngle:Connect(yaw_angle.Value)
    compensation.Alpha:Connect(alpha.Value)
    to_local.ComponentPose:Connect(compensation.Pose)
    return to_local
end

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

return DirectionalPose
