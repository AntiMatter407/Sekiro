-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Sekiro 新 Lua 动画蓝图的集中调参表。
-- 数值只描述动画状态机和节点过渡，不在这里读取输入、修改 Movement 或选择动画资源。

---@class SekiroLockOnOrientationWarpingSettings
---@field SpineBones string 使用竖线分隔的脊柱补偿骨骼名。
---@field IKFootRootBone string 原生节点校验所需的 IK 脚根骨骼名。
---@field IKFootBones string 使用竖线分隔的 IK 脚骨骼名。
---@field RotationAxis string 角色朝向旋转轴。
---@field DistributedBoneOrientationAlpha number 根与脊柱承担的旋转比例；1 表示脚部不额外旋转。

---@class SekiroFootIKSettings
---@field IKFootRootBone string Foot Placement 使用的双脚 IK 根骨骼名。
---@field PelvisBone string 允许 Foot Placement 调整高度的骨盆骨骼名。
---@field FootPlacementLegDefinitions string 双腿 FK 脚、IK 目标、脚趾和链长定义。
---@field LegIKLegDefinitions string 双腿 IK 目标、FK 脚和链长定义。
---@field PlantSpeedMode string 脚部种植速度来源；Graph 表示由原生姿势运动计算。
---@field PlantLockType string 脚部锁定方式；Unlocked 保留贴地和骨盆求解，但不把脚固定在世界空间。
---@field PelvisMaxOffset number 骨盆最大偏移，单位为厘米。
---@field GroundBlendInSpeed number 接地后 Foot IK Alpha 每秒恢复速度。
---@field AirBlendOutSpeed number 离地后 Foot IK Alpha 每秒淡出速度。
---@field PelvisHorizontalRebalancingWeight number 骨盆水平重心补偿权重；0 禁止横向推移角色身体。
---@field PlantSpeedThreshold number 低于该脚部速度时允许种植，单位为厘米每秒。
---@field PlantDistanceToGround number 脚部进入完整地面对齐的距离，单位为厘米。
---@field TraceStartOffset number 地面检测起点相对脚部的竖直偏移，单位为厘米。
---@field TraceEndOffset number 地面检测终点相对脚部的竖直偏移，单位为厘米。
---@field TraceSweepRadius number 地面球扫半径，单位为厘米。
---@field TraceMaxGroundPenetration number 允许脚底穿入碰撞面的最大深度，单位为厘米。
---@field ReachPrecision number Leg IK 末端收敛精度，单位为厘米。
---@field MaxIterations number Leg IK 最大迭代次数。

---@class SekiroUpperBodySettings
---@field SlotName string 动态 Montage 与动画图共享的上半身 Slot 名称。
---@field BranchFilters string Layered Blend Per Bone 使用的骨骼分支过滤串。

---@class SekiroWeaponIKSettings
---@field IKBone string 右臂双骨骼链的末端手骨骼名。
---@field JointTargetBone string 用当前姿势保持弯曲方向的右肘参考骨骼名。
---@field EffectorSocket string 收拔刀换挂帧共用的右手目标 Socket 名。

---@class SekiroAnimBlueprintTuning
---@field InputThreshold number 预留的 Lua 输入阈值；当前有效输入由 C++ 计算为 bHasMovementInput，Lua 尚未消费。
---@field LockedDirectionForwardBoundaryAngle number 锁定移动 Forward 扇区的绝对角上限，单位为度。
---@field LockedDirectionBackBoundaryAngle number 锁定移动 Back 扇区的绝对角下限，单位为度。
---@field LockedDirectionHysteresisAngle number 锁定 Forward/Back 离开自身扇区时保留的防抖容差，单位为度。
---@field IdleTurnEnterAngle number 预留的非锁定 Idle Turn 进入角，单位为度；当前尚无独立 Idle Turn 状态。
---@field SprintLargeTurnEnterAngle number 预留的 Sprint 大角度制动阈值，单位为度；当前尚未接入状态逻辑。
---@field StartBlendDuration number 进入 Start 的过渡时长，单位为秒。
---@field CycleBlendDuration number Start 与 Cycle 之间的过渡时长，单位为秒。
---@field DirectionBlendDuration number 四方向素材分支切换的过渡时长，单位为秒。
---@field StopBlendDuration number Cycle 与 Stop 之间的过渡时长，单位为秒。
---@field IdleBlendDuration number Stop 与 Idle 之间的过渡时长，单位为秒。
---@field StepBlendDuration number Step 进入与退出的过渡时长，单位为秒。
---@field GaitBlendDuration number Cycle 内 Walk/Run/Sprint 步态分支切换的过渡时长，单位为秒。
---@field TurnBlendDuration number Idle、Turn 之间以及左右转向素材选择的过渡时长，单位为秒。
---@field StanceBlendDuration number 同一运动阶段内 Standing/Crouching 姿态切换的过渡时长，单位为秒。
---@field JumpBlendDuration number Jump Start、InAir 与 Land 之间的过渡时长，单位为秒。
---@field JumpDirectionalSpeedThreshold number 离地时判定有向 Jump 的最小实际水平速度，单位 cm/s。
---@field JumpWarpingMaxAngle number Jump 八方向素材允许的最大量化残差，单位为度。
---@field LockOnWarpingInterpSpeed number Jump 等单节点 Orientation Warping 的默认原生插值速度。
---@field StopTurnMinResidualAngle number Stop 结束后需要播放换脚回正动作的最小残差角，单位为度。
---@field StopTurnAlignmentCurveReadyThreshold number StopTurn 曲线被视为已由当前 Turn Sequence 完整接管的权重阈值。
---@field LockOnOrientationWarping SekiroLockOnOrientationWarpingSettings Jump 等方向扭曲节点使用的骨骼与轴配置。
---@field FootIK SekiroFootIKSettings 最终 Locomotion Pose 使用的原生双脚落地与腿部求解配置。
---@field UpperBody SekiroUpperBodySettings 收拔刀等上半身动作使用的 Slot 与骨骼范围。
---@field WeaponIK SekiroWeaponIKSettings 收拔刀换挂窗口使用的右手 IK 骨骼与目标 Socket。
---@field CurveThreshold number CanEnterStop 等门控曲线被视为开启的阈值。
---@field DirectionSyncGroup string Standing/Crouch Cycle Sequence 使用的原生同步组名称。
local Tuning = {
    InputThreshold = 0.1,
    LockedDirectionForwardBoundaryAngle = 60,
    LockedDirectionBackBoundaryAngle = 120,
    LockedDirectionHysteresisAngle = 10,
    IdleTurnEnterAngle = 25,
    SprintLargeTurnEnterAngle = 100,

    StartBlendDuration = 0.10,
    CycleBlendDuration = 0.12,
    DirectionBlendDuration = 0.06,
    StopBlendDuration = 0.08,
    IdleBlendDuration = 0.16,
    StepBlendDuration = 0.06,
    GaitBlendDuration = 0.05,
    TurnBlendDuration = 0.08,
    StanceBlendDuration = 0.08,
    JumpBlendDuration = 0.08,
    JumpDirectionalSpeedThreshold = 3.0,
    JumpWarpingMaxAngle = 22.5,
    LockOnWarpingInterpSpeed = 12.0,
    StopTurnMinResidualAngle = 15.0,
    StopTurnAlignmentCurveReadyThreshold = 0.9,
    UpperBody = {
        SlotName = "DefaultSlot",
        -- Spine 是 Pelvis 之上的第一段躯干骨；深度 0 覆盖其后代，同时保留下半身移动姿势。
        BranchFilters = "Spine,0",
    },
    WeaponIK = {
        IKBone = "R_Hand",
        JointTargetBone = "R_Elbow",
        EffectorSocket = "WeaponHandIKTarget",
    },
    Combat = {
        FullBodySlotName = "CombatFullBodySlot",
        GuardPoseBlendDuration = 0.08,
    },
    LockOnOrientationWarping = {
        SpineBones = "Spine|Spine1|Spine2",
        -- Sekiro 骨架没有标准 UE ik_foot_root；占比为 1 时脚部不参与旋转，这些引用只满足原生节点有效性检查。
        IKFootRootBone = "Master",
        IKFootBones = "L_Foot_Target|R_Foot_Target",
        RotationAxis = "Z",
        DistributedBoneOrientationAlpha = 1.0,
    },
    FootIK = {
        -- 专用参考骨骼只提供稳定的向上轴，不蒙皮，也不改变 Master、RootPos 或双腿的既有层级。
        IKFootRootBone = "IK_Foot_Plane",
        PelvisBone = "Pelvis",
        FootPlacementLegDefinitions = "L_Foot,L_Foot_Target,L_Toe0,2|R_Foot,R_Foot_Target,R_Toe0,2",
        LegIKLegDefinitions = "L_Foot_Target,L_Foot,2|R_Foot_Target,R_Foot,2",
        PlantSpeedMode = "Graph",
        -- 当前骨架的脚目标动画不适合 UE 默认世界空间锁脚；关闭锁定仍保留射线、斜面旋转和骨盆高度补偿。
        PlantLockType = "Unlocked",
        -- 限制斜坡边缘和落地瞬间的骨盆下沉，避免双腿被过度压缩后带动全身扭曲。
        PelvisMaxOffset = 20.0,
        GroundBlendInSpeed = 8.0,
        AirBlendOutSpeed = 20.0,
        -- 禁止水平重心补偿，避免 Foot IK 把整条骨盆链横向推移并放大第三人称摄像机观感。
        PelvisHorizontalRebalancingWeight = 0.0,
        PlantSpeedThreshold = 60.0,
        PlantDistanceToGround = 10.0,
        TraceStartOffset = -75.0,
        TraceEndOffset = 100.0,
        TraceSweepRadius = 5.0,
        TraceMaxGroundPenetration = 8.0,
        ReachPrecision = 0.01,
        MaxIterations = 12,
    },

    CurveThreshold = 0.5,
    DirectionSyncGroup = "SekiroLocomotion",
}

return Tuning
