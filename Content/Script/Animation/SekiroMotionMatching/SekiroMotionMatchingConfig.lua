-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，集中声明正式 Motion Matching 搜索契约。
-- 节点配置由动画蓝图源码直接消费；数据库配置编译为通用反射 IR，并全量覆盖 UE5.8 正式动画条目数组。

local AnimAssets = require("Animation.SekiroMotionMatching.AnimAssets")

local AlphaBlendOption = UE.EAlphaBlendOption
local FootPlacementLockType = UE.EFootPlacementLockType
local InputQueryPose = UE.EInputQueryPose
local LocomotionGait = UE.ESKMotionMatchingGait
local LocomotionPhase = UE.ESKMotionMatchingLocomotionPhase
local LocomotionRotationMode = UE.ESKMotionMatchingRotationMode
local LocomotionStance = UE.ESKMotionMatchingStance
local PermutationTimeType = UE.EPermutationTimeType
local PoseSearchBoneFlags = UE.EPoseSearchBoneFlags
local PoseSearchDataPreprocessor = UE.EPoseSearchDataPreprocessor
local PoseSearchMirrorOption = UE.EPoseSearchMirrorOption
local PoseSearchMode = UE.EPoseSearchMode
local PoseSearchTrajectoryFlags = UE.EPoseSearchTrajectoryFlags
local WarpingEvaluationMode = UE.EWarpingEvaluationMode

---@class SKMotionMatchingNodeSettings
---@field BlendTime number 新姿势进入内部 Blend Stack 的混合时间，单位秒。
---@field MaxActiveBlends number Blend Stack 可同时保留的最大样本数。
---@field BlendProfileAssetPath string 骨骼 Blend Profile 资产路径；空字符串表示使用无 Profile 的全身统一混合。
---@field BlendOption number UE EAlphaBlendOption 原生枚举值；首版使用 Linear 便于观测实际过渡。
---@field PoseJumpThresholdTime string UE FFloatInterval 文本；首版不额外禁止同片段附近姿势。
---@field PoseReselectHistory number 禁止重新选择近期姿势的历史窗口，单位秒。
---@field SearchThrottleTime number 两次常规搜索之间的最短间隔，单位秒；持久请求仍可强制打断 Continuing Pose。
---@field PlayRate string UE FFloatInterval 文本；基础 Locomotion 固定 1.0，不用播放速率生成第二份速度适配。
---@field PlayRateMultiplier number 选择后的额外播放速率倍率；正式基线固定为 1.0。
---@field bUseInertialBlend boolean 是否使用额外惯性混合；当前由内部 Blend Stack 唯一负责切换。

---@class SKMotionMatchingOrientationWarpingSettings
---@field Mode number UE EWarpingEvaluationMode 原生枚举值，固定为 Manual。
---@field SpineBones string 使用竖线分隔的上身反向补偿骨骼名。
---@field IKFootRootBone string 原生节点有效性检查使用的 IK 脚根骨骼名。
---@field IKFootBones string 使用竖线分隔的 IK 脚骨骼名。
---@field RotationAxis string 角色水平转向使用的旋转轴。
---@field DistributedBoneOrientationAlpha number 根骨承担姿势残差的比例。
---@field RotationInterpSpeed number 跨帧残差插值速度；只平滑姿势，不修改移动。

---@class SKMotionMatchingFootIKSettings
---@field IKFootRootBone string Foot Placement 使用的稳定参考平面骨骼名。
---@field PelvisBone string 允许 Foot Placement 做垂直补偿的骨盆骨骼名。
---@field FootPlacementLegDefinitions string 双腿 FK 脚、IK 目标、脚趾和链长定义。
---@field LegIKLegDefinitions string 双腿 IK 目标、FK 脚和链长定义。
---@field PlantSpeedMode number UE EWarpingEvaluationMode 原生枚举值；Graph 表示从当前姿势计算。
---@field PlantLockType number UE EFootPlacementLockType 原生枚举值；Unlocked 不建立世界空间锁脚。
---@field PelvisMaxOffset number 骨盆最大垂直偏移，单位厘米。
---@field GroundBlendInSpeed number 接地后 Foot IK Alpha 每秒恢复速度。
---@field AirBlendOutSpeed number 离地后 Foot IK Alpha 每秒淡出速度。
---@field PelvisHorizontalRebalancingWeight number 骨盆水平重心补偿权重；正式基线固定为 0。
---@field PlantSpeedThreshold number 允许脚部种植的速度阈值，单位厘米每秒。
---@field PlantDistanceToGround number 脚部进入完整地面对齐的距离，单位厘米。
---@field TraceStartOffset number 地面检测起点相对脚部的竖直偏移，单位厘米。
---@field TraceEndOffset number 地面检测终点相对脚部的竖直偏移，单位厘米。
---@field TraceSweepRadius number 地面球扫半径，单位厘米。
---@field TraceMaxGroundPenetration number 允许脚底穿入碰撞面的最大深度，单位厘米。
---@field ReachPrecision number Leg IK 末端收敛精度，单位厘米。
---@field MaxIterations number Leg IK 最大迭代次数。

---@class SKPoseSearchTrajectorySampleContract
---@field Offset number 相对当前查询时刻的轨迹采样时间，单位秒。
---@field Features string[] 该时间点参与比较的 UE5.8 Trajectory 特征名称。
---@field Weight number 该时间点特征在 Trajectory Channel 组权重内的相对权重。

---@class SKPoseSearchBoneContract
---@field BoneName string Sekiro Skeleton 中参与当前 Pose Query 的精确骨骼名。
---@field Features string[] 该骨骼参与比较的 UE5.8 Pose 特征名称。
---@field Weight number 该骨骼特征在 Pose Channel 组权重内的相对权重。

---@class SKPoseSearchFeatureVectorSegmentContract
---@field Id string 按 UE5.8 Channel Finalize 顺序生成的稳定向量段标识。
---@field Dimension number 该向量段占用的浮点维数，必须为正整数。

---@class SKPoseSearchCoordinateSpaceContract
---@field QueryOriginSource string 查询原点的唯一来源；当前固定为最近完成的实际 Actor Transform。
---@field TrajectoryPositionSpace string Trajectory Position 进入 Pose Search 前所在的坐标空间。
---@field TrajectoryFacingSpace string Trajectory Facing 进入 Pose Search 前所在的坐标空间。
---@field PosePositionSpace string Pose 骨骼位置相对的原点空间。
---@field PoseVelocitySpace string Pose 骨骼速度的计算空间。
---@field TranslationAxisYawOffsetDegrees number 从 UE 角色前向轴到动画 RootMotion 位移轴的唯一水平适配角。
---@field WorldSpaceFeaturePolicy string 世界空间中间值进入最终特征向量时的处理策略；正式配置只允许 Reject。

---@class SKPoseSearchSchemaContract
---@field AssetPath string 正式 Pose Search Schema 的 UE 对象路径。
---@field SkeletonPath string 离线索引与运行时 Query 共同使用的 Skeleton 对象路径。
---@field SampleRate number 离线动画索引采样率，单位 Hz，必须与项目轨迹基线一致。
---@field DataPreprocessor number UE EPoseSearchDataPreprocessor 原生枚举值。
---@field NumberOfPermutations number 正式基线的索引排列数量。
---@field bAddDataPadding boolean 是否给最终向量增加对齐 Padding Channel。
---@field bInjectAdditionalDebugChannels boolean 是否注入只用于调试的额外 Channel。
---@field CoordinateSpace SKPoseSearchCoordinateSpaceContract 离线索引与运行时查询共享的坐标空间边界。
---@field TrajectoryChannelClassPath string UE5.8 Trajectory Channel 原生类路径。
---@field TrajectoryWeight number Trajectory Channel 组权重。
---@field TrajectorySamples SKPoseSearchTrajectorySampleContract[] 从稠密历史/未来轨迹中选取的固定查询时间点。
---@field PoseChannelClassPath string UE5.8 Pose Channel 原生类路径。
---@field PoseWeight number Pose Channel 组权重。
---@field InputQueryPose number UE EInputQueryPose 原生枚举值。
---@field bUseCharacterSpaceVelocities boolean 是否在角色空间计算骨骼速度。
---@field SampledBones SKPoseSearchBoneContract[] 按声明顺序展开的 Pose 骨骼特征。
---@field FeatureVectorLayout SKPoseSearchFeatureVectorSegmentContract[] 最终查询向量的严格展开顺序。
---@field ExpectedCardinality number 最终查询向量预期总维数，供后续物化和回读拒绝漂移。

---@class SKPoseSearchSchemaIR
---@field Version number 项目 Schema 契约版本。
---@field TargetObjectPath string 正式 Pose Search Schema 对象路径。
---@field SkeletonObjectPath string Schema 使用的 Skeleton 对象路径。
---@field ExpectedCardinality number Lua 契约推导出的严格向量维数。
---@field AssetPatch SKObjectPropertyPatchIR 可由通用反射资产操作器消费的 Schema 全量补丁。

---@class SKPoseSearchDatabaseEligibility
---@field Phases number[] ESKMotionMatchingLocomotionPhase 原生枚举数组。
---@field Gaits number[] ESKMotionMatchingGait 原生枚举数组。
---@field Modes number[] ESKMotionMatchingRotationMode 原生枚举数组。
---@field Stances number[] ESKMotionMatchingStance 原生枚举数组。

---@class SKPoseSearchDatabaseSettings
---@field AssetPath string 目标 Pose Search Database 的 UE 对象路径。
---@field Schema string 构建查询向量时必须使用的 Pose Search Schema 路径。
---@field NormalizationSet string 与其他阶段数据库共享统计尺度的 Pose Search Normalization Set 路径。
---@field MovementPolicy string|nil 项目侧移动所有权策略；省略时默认为 AnimationRootMotion，不写入通用资产补丁。
---@field PoseSearchMode number UE EPoseSearchMode 原生枚举值。
---@field ContinuingPoseCostBias number Continuing Pose 的附加代价；负值用于轻度偏向保持当前片段。
---@field BaseCostBias number 数据库全部候选姿势的基础附加代价。
---@field LoopingCostBias number 循环动画候选的附加代价；负值用于轻度偏向稳定循环。
---@field ExcludeFromDatabaseMin number 从每段动画起点排除的时长，单位秒。
---@field ExcludeFromDatabaseMax number 相对每段动画终点的上界偏移，负值用于排除结尾帧。
---@field AnimationAssets SKPoseSearchAnimationAssetSettings[] 由 Lua 全量拥有并覆盖的正式动画条目。
---@field Eligibility SKPoseSearchDatabaseEligibility Chooser 使用的合法候选范围，不参与 Motion Matching 排序。

---@class SKPoseSearchAnimationAssetSettings
---@field AssetPath string 动画资产的完整 UE 对象路径。
---@field bEnabled boolean 是否参与数据库索引与搜索。
---@field bDisableReselection boolean 是否禁止在 Continuing Pose 期间重选同一动画资产。
---@field MirrorOption number UE EPoseSearchMirrorOption 原生枚举值。
---@field SamplingRangeMin number 采样区间起点，0 与 SamplingRangeMax=0 表示整段动画。
---@field SamplingRangeMax number 采样区间终点，0 与 SamplingRangeMin=0 表示整段动画。

---@class SKPoseSearchDatabaseIR
---@field Version number IR 契约版本，使用正整数供导入器拒绝不兼容格式。
---@field Database SKPoseSearchDatabaseSettings 单个正式 Locomotion 阶段数据库的完整生成描述。
---@field AssetPatch SKObjectPropertyPatchIR 可由通用反射资产操作器消费的 UE5.8 属性补丁。

---@class SKPoseSearchNormalizationSetSettings
---@field AssetPath string 正式 Pose Search Normalization Set 的 UE 对象路径。
---@field SchemaContract string SchemaContracts 中所有成员必须共享的契约名称。
---@field Databases string[] 当前已启用并进入联合统计的数据库配置名称。
---@field DeferredDatabaseFamilies string[] 尚未加入最小动画集、但正式架构已预留的阶段数据库族。

---@class SKPoseSearchNormalizationSetIR
---@field Version number 项目 Normalization Set IR 版本。
---@field TargetObjectPath string 正式 Normalization Set 对象路径。
---@field SchemaContract string 所有成员共同使用的 Schema 契约名称。
---@field DatabaseObjectPaths string[] 参与联合统计的数据库对象路径。
---@field DeferredDatabaseFamilies string[] 等待动画数据接入的正式阶段族，不写入当前资产。
---@field AssetPatch SKObjectPropertyPatchIR 可由通用反射资产操作器消费的数据库数组补丁。

---@class SKLocomotionChooserRuleIR
---@field Phases number[] ESKMotionMatchingLocomotionPhase 原生枚举数组。
---@field Gaits number[] ESKMotionMatchingGait 原生枚举数组。
---@field Modes number[] ESKMotionMatchingRotationMode 原生枚举数组。
---@field Stances number[] ESKMotionMatchingStance 原生枚举数组。
---@field Databases string[] 规则命中时返回的 Pose Search Database 对象路径数组。

---@class SKLocomotionChooserSettings
---@field AssetPath string 正式 Chooser Table 的 UE 对象路径。
---@field ContextClassPath string Chooser 运行时反射上下文类路径。
---@field EmptyResultPolicy string 空结果策略；正式配置只允许 Error。

---@class SKMotionMatchingCoverageRequirement
---@field Database string 必须拥有该语义组合的数据库配置名。
---@field Phase number ESKMotionMatchingLocomotionPhase 原生枚举值。
---@field Gait number ESKMotionMatchingGait 原生枚举值。
---@field Mode number ESKMotionMatchingRotationMode 原生枚举值。
---@field Stance number ESKMotionMatchingStance 原生枚举值。
---@field RequiredAnimationAssets string[] 该语义组合至少必须启用的动画对象路径。

---@class SKMotionMatchingDirectionSectorContract
---@field Name string 项目稳定方向名；只用于动画覆盖和数据库内排序语义，不是 Chooser 维度。
---@field CenterYawDegrees number 相对查询原点前向的扇区中心角，正值向右、负值向左。

---@class SKMotionMatchingDirectionalCoverageContract
---@field Target string 需要方向覆盖的阶段或素材角色名。
---@field DirectionSet string DirectionSets 中登记的方向集合名称。

---@class SKMotionMatchingCoverageProfileContract
---@field Id string 稳定覆盖族标识，不等同于必须独占一个 Pose Search Database。
---@field Phases number[] ESKMotionMatchingLocomotionPhase 原生枚举数组。
---@field Gaits number[] ESKMotionMatchingGait 原生枚举数组；蹲伏保持进入前 Gait。
---@field Modes number[] ESKMotionMatchingRotationMode 原生枚举数组。
---@field Stances number[] ESKMotionMatchingStance 原生枚举数组。
---@field DirectionalCoverage SKMotionMatchingDirectionalCoverageContract[] 需要方向素材的阶段或角色。
---@field ClipRoles string[] 该族完成时必须有素材承担的语义角色，不参与 Chooser 行匹配。

---@class SKMotionMatchingTargetCoverageContract
---@field Version number 项目目标覆盖契约版本。
---@field ChooserDimensions string[] 只负责候选合法性的硬门禁维度。
---@field RankingDimensions string[] 只在合法数据库内参与 Motion Matching 排序的连续特征维度。
---@field DatabaseSplitPolicy string 数据库拆分策略；目标覆盖不要求为每个组合建立独占资产。
---@field DirectionSets table<string, SKMotionMatchingDirectionSectorContract[]> 可复用方向扇区集合。
---@field Profiles SKMotionMatchingCoverageProfileContract[] 正式 Locomotion 最终覆盖族。
---@field NonLocomotionPhases number[] 不应映射到基础 Locomotion 数据库的原生阶段枚举数组。

---@class SKMotionMatchingTrajectoryValidationPolicy
---@field QueryOriginSource string 轨迹查询原点必须使用的已完成实际状态字段。
---@field PositionSpace string Position 特征必须使用的相对空间。
---@field FacingSpace string Facing 特征必须使用的相对空间。
---@field PosePositionSpace string Pose Position 必须使用的相对空间。
---@field PoseVelocitySpace string Pose Velocity 必须使用的相对空间。
---@field TranslationAxisYawOffsetDegrees number 只狼 RootMotion Translation 唯一允许的水平轴适配角。
---@field WorldSpaceFeaturePolicy string 世界空间中间值进入最终向量时的强制策略。
---@field ExpectedCardinality number 正式最小组 Schema 必须保持的维数。
---@field MinimumFutureHorizon number 至少需要覆盖的未来轨迹时长，单位秒。
---@field PastRequiredFeatures string[] 任一历史样本必须共同包含的特征。
---@field CurrentRequiredFeatures string[] 零时刻样本必须共同包含的意图特征。
---@field FutureRequiredFeatures string[] 每个未来样本必须共同包含的目标特征。
---@field FarthestRequiredFeatures string[] 最远未来样本额外必须包含的趋势特征。

---@class SKMotionMatchingAuthoringValidationPolicy
---@field RequiredCoverage SKMotionMatchingCoverageRequirement[] 当前验收范围内必须存在且不得重叠的 Chooser 覆盖。
---@field Trajectory SKMotionMatchingTrajectoryValidationPolicy 独立于权重的轨迹语义门禁。
---@field AirborneTrajectory SKMotionMatchingTrajectoryValidationPolicy 保留 Z 分量的空中轨迹语义门禁。

---@class SKGenericChooserColumnIR
---@field Id string 调用方定义的稳定列标识；插件不解释其业务语义。
---@field Type string 通用列类型；首版固定为 EnumAny。
---@field BindingPath string[] 从 ContextClass 开始解析的反射属性链。

---@class SKGenericChooserConditionIR
---@field ColumnId string 对应 Columns 中的稳定列标识。
---@field Values number[] 该行允许匹配的原生枚举数值；由目标上下文属性的 UEnum 校验。

---@class SKGenericChooserRowIR
---@field ResultObjectPath string 该行命中时返回的 UObject 资产路径。
---@field Conditions SKGenericChooserConditionIR[] 该行按列声明的全部过滤条件。

---@class SKGenericChooserAssetIR
---@field Version number 通用 Chooser 资产 IR 版本。
---@field AssetType string 固定为 ChooserTable，供通用物化器拒绝错误资产类型。
---@field TargetObjectPath string 待创建或全量替换的 Chooser Table 对象路径。
---@field ContextClassPath string 反射属性绑定的 UObject 上下文类路径。
---@field ResultClassPath string 每个结果对象必须匹配的 UObject 类路径。
---@field ResultMode string ObjectArray 表示运行时允许返回全部命中行。
---@field Columns SKGenericChooserColumnIR[] 通用过滤列声明。
---@field Rows SKGenericChooserRowIR[] 通用结果行声明。

---@class SKLocomotionChooserIR
---@field Version number 项目侧 Chooser 契约版本。
---@field TargetObjectPath string 正式 Chooser Table 的 UE 对象路径。
---@field ResultClassPath string 每个数组元素必须匹配的 UObject 类路径。
---@field bResultIsArray boolean 固定为 true，禁止退化成单数据库输出。
---@field EmptyResultPolicy string 空集合处理策略；Error 表示不得沿用上一帧数据库。
---@field Rules SKLocomotionChooserRuleIR[] 按声明顺序生成的合法候选集合规则。

---@class SKObjectPropertyAssignmentIR
---@field Name string UObject 反射属性名，由项目 Lua 根据目标引擎版本声明。
---@field ValueType string 通用反射值类型，不携带 Motion Matching 业务语义。
---@field Value any 属性纯值；Struct 使用属性赋值数组，Array 使用带 ValueType 的元素数组。

---@class SKObjectTypedValueIR
---@field ValueType string 通用反射值类型标签。
---@field Value any 由 ValueType 与目标 FProperty 共同约束的纯值；InstancedObject 使用 Class 与 Properties 描述内联对象。

---@class SKObjectPropertyPatchIR
---@field Version number 通用属性补丁契约版本。
---@field AssetType string 固定为 ObjectPropertyPatch，防止误用其他 IR。
---@field TargetObjectPath string 待修改资产的完整 UE 对象路径。
---@field ExpectedClassPath string 目标 UObject 必须匹配的原生类路径。
---@field Properties SKObjectPropertyAssignmentIR[] 按声明顺序应用的通用反射属性赋值。

local SekiroMotionMatchingConfig = {}

local supported_search_modes = {
    [PoseSearchMode.BruteForce] = true,
    [PoseSearchMode.PCAKDTree] = true,
    [PoseSearchMode.VPTree] = true,
}

local supported_mirror_options = {
    [PoseSearchMirrorOption.UnmirroredOnly] = true,
    [PoseSearchMirrorOption.MirroredOnly] = true,
    [PoseSearchMirrorOption.UnmirroredAndMirrored] = true,
}

-- 移动策略只约束项目运行时语义，不属于 PoseSearch 资产字段，也不要求通用插件理解。
local supported_movement_policies = {
    AnimationRootMotion = true,
    InheritedAirborneMomentum = true,
}

local pose_only_airborne_phases = {
    [LocomotionPhase.Ascending] = true,
    [LocomotionPhase.Apex] = true,
    [LocomotionPhase.Falling] = true,
}

-- 位标记直接引用 UE5.8 原生 UENUM；组合后的整数才进入通用反射补丁。
local trajectory_feature_definitions = {
    Velocity = { Mask = PoseSearchTrajectoryFlags.Velocity, Dimension = 3, FinalizeOrder = 2 },
    Position = { Mask = PoseSearchTrajectoryFlags.Position, Dimension = 3, FinalizeOrder = 1 },
    VelocityXY = { Mask = PoseSearchTrajectoryFlags.VelocityXY, Dimension = 2, FinalizeOrder = 2 },
    PositionXY = { Mask = PoseSearchTrajectoryFlags.PositionXY, Dimension = 2, FinalizeOrder = 1 },
    FacingDirectionXY = {
        Mask = PoseSearchTrajectoryFlags.FacingDirectionXY,
        Dimension = 2,
        FinalizeOrder = 4,
    },
}

local pose_feature_definitions = {
    Velocity = { Mask = PoseSearchBoneFlags.Velocity, Dimension = 3, FinalizeOrder = 3 },
    Position = { Mask = PoseSearchBoneFlags.Position, Dimension = 3, FinalizeOrder = 1 },
}

---@type SKMotionMatchingNodeSettings
SekiroMotionMatchingConfig.Node = {
    BlendTime = 0.18,
    MaxActiveBlends = 3,
    BlendProfileAssetPath = "",
    BlendOption = AlphaBlendOption.Linear,
    PoseJumpThresholdTime = "(Min=0.0,Max=0.0)",
    PoseReselectHistory = 0.30,
    SearchThrottleTime = 0.10,
    -- RootMotion 是基础水平位移的唯一数值来源，首版不对选中动画做速率拉伸。
    PlayRate = "(Min=1.0,Max=1.0)",
    PlayRateMultiplier = 1.0,
    bUseInertialBlend = false,
}

---@type SKMotionMatchingOrientationWarpingSettings
SekiroMotionMatchingConfig.OrientationWarping = {
    -- UE5.8 Graph 模式会覆盖 RootMotion 属性；本项目只允许 Manual 模式消费 Coordinator 的已施加残差。
    Mode = WarpingEvaluationMode.Manual,
    SpineBones = "Spine|Spine1|Spine2",
    -- Sekiro 骨架没有标准 UE ik_foot_root；根骨承担全部姿势残差时，这些引用只满足原生节点有效性检查。
    IKFootRootBone = "Master",
    IKFootBones = "L_Foot_Target|R_Foot_Target",
    RotationAxis = "Z",
    DistributedBoneOrientationAlpha = 1.0,
    RotationInterpSpeed = 12.0,
}

---@type SKMotionMatchingFootIKSettings
SekiroMotionMatchingConfig.FootIK = {
    -- 专用无蒙皮参考骨提供稳定向上的局部 Z，不使用会随动画旋转的 Master 或 Pelvis。
    IKFootRootBone = "IK_Foot_Plane",
    PelvisBone = "Pelvis",
    FootPlacementLegDefinitions =
        "L_Foot,L_Foot_Target,L_Toe0,2|R_Foot,R_Foot_Target,R_Toe0,2",
    LegIKLegDefinitions =
        "L_Foot_Target,L_Foot,2|R_Foot_Target,R_Foot,2",
    PlantSpeedMode = WarpingEvaluationMode.Graph,
    -- 当前脚目标不适合原生世界空间锁定；Unlocked 仍保留地面检测、坡面旋转与骨盆补偿。
    PlantLockType = FootPlacementLockType.Unlocked,
    PelvisMaxOffset = 20.0,
    GroundBlendInSpeed = 8.0,
    AirBlendOutSpeed = 20.0,
    -- 水平补偿会制造额外的 Mesh 横移观感，正式 RootMotion 基线禁止启用。
    PelvisHorizontalRebalancingWeight = 0.0,
    PlantSpeedThreshold = 60.0,
    PlantDistanceToGround = 10.0,
    TraceStartOffset = -75.0,
    TraceEndOffset = 100.0,
    TraceSweepRadius = 5.0,
    TraceMaxGroundPenetration = 8.0,
    ReachPrecision = 0.01,
    MaxIterations = 12,
}

-- Locomotion 保持已验证的 UE5.8 水平基线；Airborne 独立保留轨迹 Z，但复用相同骨骼姿势集合。
-- 两种 Schema 都由同一编译入口和通用反射 IR 物化，不为最小验证另建动画图或运行时实现。
---@type table<string, SKPoseSearchSchemaContract>
SekiroMotionMatchingConfig.SchemaContracts = {
    Locomotion = {
        AssetPath = AnimAssets.PoseSearch.LocomotionSchema,
        SkeletonPath = AnimAssets.PoseSearch.Skeleton,
        SampleRate = 30,
        DataPreprocessor = PoseSearchDataPreprocessor.Normalize,
        NumberOfPermutations = 1,
        bAddDataPadding = false,
        bInjectAdditionalDebugChannels = false,
        CoordinateSpace = {
            -- TargetIntentVelocityWS 与 QueryOriginWS 只允许作为构建中间值，不能直接写入最终特征向量。
            QueryOriginSource = "CompletedActualState.ActorTransformWS",
            TrajectoryPositionSpace = "QueryOrigin",
            TrajectoryFacingSpace = "QueryOrigin",
            PosePositionSpace = "Root",
            PoseVelocitySpace = "Character",
            -- 只狼源动画的 RootMotion 位移前向为本地 -Y；该适配只旋转 Translation 派生特征。
            TranslationAxisYawOffsetDegrees = -90.0,
            WorldSpaceFeaturePolicy = "Reject",
        },
        TrajectoryChannelClassPath =
            "/Script/PoseSearch.PoseSearchFeatureChannel_Trajectory",
        TrajectoryWeight = 7.0,
        TrajectorySamples = {
            {
                Offset = -0.40,
                Features = { "PositionXY" },
                Weight = 0.40,
            },
            {
                Offset = 0.0,
                Features = { "VelocityXY", "FacingDirectionXY" },
                Weight = 2.0,
            },
            {
                Offset = 0.35,
                Features = { "PositionXY", "FacingDirectionXY" },
                Weight = 0.70,
            },
            {
                Offset = 0.70,
                Features = { "PositionXY", "VelocityXY", "FacingDirectionXY" },
                Weight = 0.50,
            },
        },
        PoseChannelClassPath =
            "/Script/PoseSearch.PoseSearchFeatureChannel_Pose",
        PoseWeight = 1.0,
        InputQueryPose = InputQueryPose.UseContinuingPose,
        bUseCharacterSpaceVelocities = true,
        SampledBones = {
            {
                BoneName = "Pelvis",
                Features = { "Position", "Velocity" },
                Weight = 0.50,
            },
            {
                BoneName = "L_Foot",
                Features = { "Position", "Velocity" },
                Weight = 1.0,
            },
            {
                BoneName = "R_Foot",
                Features = { "Position", "Velocity" },
                Weight = 1.0,
            },
        },
        FeatureVectorLayout = {
            { Id = "Trajectory[-0.40].PositionXY", Dimension = 2 },
            { Id = "Trajectory[0.00].VelocityXY", Dimension = 2 },
            { Id = "Trajectory[0.00].FacingDirectionXY", Dimension = 2 },
            { Id = "Trajectory[0.35].PositionXY", Dimension = 2 },
            { Id = "Trajectory[0.35].FacingDirectionXY", Dimension = 2 },
            { Id = "Trajectory[0.70].PositionXY", Dimension = 2 },
            { Id = "Trajectory[0.70].VelocityXY", Dimension = 2 },
            { Id = "Trajectory[0.70].FacingDirectionXY", Dimension = 2 },
            { Id = "Pose.Pelvis.Position", Dimension = 3 },
            { Id = "Pose.Pelvis.Velocity", Dimension = 3 },
            { Id = "Pose.L_Foot.Position", Dimension = 3 },
            { Id = "Pose.L_Foot.Velocity", Dimension = 3 },
            { Id = "Pose.R_Foot.Position", Dimension = 3 },
            { Id = "Pose.R_Foot.Velocity", Dimension = 3 },
        },
        ExpectedCardinality = 34,
    },
    Airborne = {
        AssetPath = AnimAssets.PoseSearch.AirborneSchema,
        SkeletonPath = AnimAssets.PoseSearch.Skeleton,
        SampleRate = 30,
        DataPreprocessor = PoseSearchDataPreprocessor.Normalize,
        NumberOfPermutations = 1,
        bAddDataPadding = false,
        bInjectAdditionalDebugChannels = false,
        CoordinateSpace = {
            -- 空中与地面共享查询原点和位移轴适配，区别只在完整保留 Position/Velocity 的 Z 分量。
            QueryOriginSource = "CompletedActualState.ActorTransformWS",
            TrajectoryPositionSpace = "QueryOrigin",
            TrajectoryFacingSpace = "QueryOrigin",
            PosePositionSpace = "Root",
            PoseVelocitySpace = "Character",
            TranslationAxisYawOffsetDegrees = -90.0,
            WorldSpaceFeaturePolicy = "Reject",
        },
        TrajectoryChannelClassPath =
            "/Script/PoseSearch.PoseSearchFeatureChannel_Trajectory",
        TrajectoryWeight = 7.0,
        TrajectorySamples = {
            {
                Offset = -0.20,
                Features = { "Position" },
                Weight = 0.50,
            },
            {
                Offset = 0.0,
                Features = { "Velocity", "FacingDirectionXY" },
                Weight = 2.0,
            },
            {
                Offset = 0.25,
                Features = { "Position", "Velocity", "FacingDirectionXY" },
                Weight = 0.80,
            },
            {
                Offset = 0.55,
                Features = { "Position", "Velocity", "FacingDirectionXY" },
                Weight = 0.50,
            },
        },
        PoseChannelClassPath =
            "/Script/PoseSearch.PoseSearchFeatureChannel_Pose",
        PoseWeight = 1.0,
        InputQueryPose = InputQueryPose.UseContinuingPose,
        bUseCharacterSpaceVelocities = true,
        SampledBones = {
            {
                BoneName = "Pelvis",
                Features = { "Position", "Velocity" },
                Weight = 0.50,
            },
            {
                BoneName = "L_Foot",
                Features = { "Position", "Velocity" },
                Weight = 1.0,
            },
            {
                BoneName = "R_Foot",
                Features = { "Position", "Velocity" },
                Weight = 1.0,
            },
        },
        FeatureVectorLayout = {
            { Id = "Trajectory[-0.20].Position", Dimension = 3 },
            { Id = "Trajectory[0.00].Velocity", Dimension = 3 },
            { Id = "Trajectory[0.00].FacingDirectionXY", Dimension = 2 },
            { Id = "Trajectory[0.25].Position", Dimension = 3 },
            { Id = "Trajectory[0.25].Velocity", Dimension = 3 },
            { Id = "Trajectory[0.25].FacingDirectionXY", Dimension = 2 },
            { Id = "Trajectory[0.55].Position", Dimension = 3 },
            { Id = "Trajectory[0.55].Velocity", Dimension = 3 },
            { Id = "Trajectory[0.55].FacingDirectionXY", Dimension = 2 },
            { Id = "Pose.Pelvis.Position", Dimension = 3 },
            { Id = "Pose.Pelvis.Velocity", Dimension = 3 },
            { Id = "Pose.L_Foot.Position", Dimension = 3 },
            { Id = "Pose.L_Foot.Velocity", Dimension = 3 },
            { Id = "Pose.R_Foot.Position", Dimension = 3 },
            { Id = "Pose.R_Foot.Velocity", Dimension = 3 },
        },
        ExpectedCardinality = 42,
    },
}

-- 地面阶段共享 34 维 Locomotion Schema；空中阶段共享保留 Z 的 42 维 Airborne Schema。
-- JumpStart、共享空中姿势库与 Landing 均使用同一 Airborne Schema。
---@type table<string, string>
SekiroMotionMatchingConfig.PhaseSchemaAssignments = {
    Stationary = "Locomotion",
    WalkStarts = "Locomotion",
    WalkLoops = "Locomotion",
    WalkStops = "Locomotion",
    RunStarts = "Locomotion",
    RunLoops = "Locomotion",
    RunStops = "Locomotion",
    SprintStarts = "Locomotion",
    SprintLoops = "Locomotion",
    SprintStops = "Locomotion",
    SprintPivots = "Locomotion",
    CrouchStationary = "Locomotion",
    CrouchWalkStarts = "Locomotion",
    CrouchWalkLoops = "Locomotion",
    CrouchWalkStops = "Locomotion",
    CrouchRunStarts = "Locomotion",
    CrouchRunLoops = "Locomotion",
    CrouchRunStops = "Locomotion",
    Pivots = "Locomotion",
    JumpStarts = "Airborne",
    InAirPoseOnly = "Airborne",
    Landing = "Airborne",
}

---@type table<string, SKPoseSearchNormalizationSetSettings>
SekiroMotionMatchingConfig.NormalizationSets = {
    Locomotion = {
        AssetPath = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        SchemaContract = "Locomotion",
        Databases = {
            "Stationary",
            "WalkStarts",
            "WalkLoops",
            "WalkStops",
            "RunStarts",
            "RunLoops",
            "RunStops",
            "SprintStarts",
            "SprintLoops",
            "SprintStops",
            "SprintPivots",
            "CrouchStationary",
            "CrouchWalkStarts",
            "CrouchWalkLoops",
            "CrouchWalkStops",
            "CrouchRunStarts",
            "CrouchRunLoops",
            "CrouchRunStops",
        },
        DeferredDatabaseFamilies = {},
    },
    Airborne = {
        AssetPath = AnimAssets.PoseSearch.NormalizationSets.Airborne,
        SchemaContract = "Airborne",
        Databases = {
            "JumpStarts",
            "InAirPoseOnly",
            "Landing",
        },
        DeferredDatabaseFamilies = {},
    },
}

---构造正式数据库动画条目，统一镜像策略和采样起点，避免扩展候选时遗漏必要字段。
---采样终点为 0 时使用整段动画；非零值来自编辑器内 RootMotion 有效区间核对。
---@param asset_path string 已在 Motion Matching AnimAssets 中登记的动画对象路径。
---@param disable_reselection boolean 是否禁止在当前动画仍激活时重新选择同一动画。
---@param sampling_range_max number 允许进入 Pose Search 索引的动画终点秒数，0 表示整段。
---@return SKPoseSearchAnimationAssetSettings animation_asset 完整的数据库动画条目。
local function make_database_animation_asset(
    asset_path,
    disable_reselection,
    sampling_range_max)
    return {
        AssetPath = asset_path,
        bEnabled = true,
        bDisableReselection = disable_reselection,
        MirrorOption = PoseSearchMirrorOption.UnmirroredOnly,
        SamplingRangeMin = 0.0,
        SamplingRangeMax = sampling_range_max,
    }
end

-- 这些动画没有 RootMotion，只负责表现 Ascending/Apex/Falling；水平与竖直移动继续继承
-- CMC 已完成的空中动量和重力结果。三个阶段共享同一数据库，由 Chooser 阶段门禁触发重搜。
local in_air_pose_only_asset_paths = {
    AnimAssets.Jump.InAir_Forward,
    AnimAssets.Jump.InAir_Back,
    AnimAssets.Jump.InAir_Left,
    AnimAssets.Jump.InAir_Right,
    AnimAssets.Jump.InAir_ForwardLeft,
    AnimAssets.Jump.InAir_ForwardRight,
    AnimAssets.Jump.InAir_BackLeft,
    AnimAssets.Jump.InAir_BackRight,
    AnimAssets.Jump.InAir_Loop,
}

---构造共享空中姿势数据库的完整动画条目，避免三个阶段复制并漂移候选集合。
---@return SKPoseSearchAnimationAssetSettings[] animation_assets 九个无 RootMotion 的方向姿势与循环姿势。
local function make_in_air_pose_only_animation_assets()
    local animation_assets = {}
    for index, asset_path in ipairs(in_air_pose_only_asset_paths) do
        animation_assets[index] = make_database_animation_asset(asset_path, false, 0.0)
    end
    return animation_assets
end

-- Phase、Gait 与 Stance 是数据库硬边界；Free/Locked 只在同一 Gait 内共享。
-- 方向仍由 DesiredMove 的连续特征在合法数据库内排序，DesiredFacing 由独立 Steering 链处理。
    -- 斜向、非前向 Sprint Loop/Stop 和非 Sprint Pivot 仍是显式素材缺口，不用现有候选冒充完成。
---@type table<string, SKPoseSearchDatabaseSettings>
SekiroMotionMatchingConfig.Databases = {
    Stationary = {
        AssetPath = AnimAssets.PoseSearch.Databases.Stationary,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Idle, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Stationary },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    WalkStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.WalkStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Forward_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Back_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Left_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Right_Start, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StartRequested },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    RunStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.RunStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Run_Forward_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Back_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Left_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Right_Start, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StartRequested },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    SprintStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.SprintStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Forward_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Back_Turn_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Left_Turn_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Right_Turn_Start, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StartRequested },
            Gaits = { LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    WalkLoops = {
        AssetPath = AnimAssets.PoseSearch.Databases.WalkLoops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Forward_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Back_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Left_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Right_Loop, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Moving },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    RunLoops = {
        AssetPath = AnimAssets.PoseSearch.Databases.RunLoops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Run_Forward_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Back_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Left_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Right_Loop, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Moving },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    SprintLoops = {
        AssetPath = AnimAssets.PoseSearch.Databases.SprintLoops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Forward_Loop, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Moving },
            Gaits = { LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    WalkStops = {
        AssetPath = AnimAssets.PoseSearch.Databases.WalkStops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Forward_Stop, false, 0.8),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Back_Stop, false, 0.2667),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Left_Stop, false, 0.2333),
            make_database_animation_asset(AnimAssets.Locomotion.Walk_Right_Stop, false, 0.2333),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StopRequested },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    RunStops = {
        AssetPath = AnimAssets.PoseSearch.Databases.RunStops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Run_Forward_Stop, false, 1.0),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Back_Stop, false, 0.2667),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Left_Stop, false, 0.6),
            make_database_animation_asset(AnimAssets.Locomotion.Run_Right_Stop, false, 0.6),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StopRequested },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    SprintStops = {
        AssetPath = AnimAssets.PoseSearch.Databases.SprintStops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Forward_Stop, false, 0.1667),
            make_database_animation_asset(
                AnimAssets.Locomotion.Sprint_Forward_Left_Turn_Stop,
                false,
                0.1333),
            make_database_animation_asset(
                AnimAssets.Locomotion.Sprint_Forward_Right_Turn_Stop,
                false,
                0.1333),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StopRequested },
            Gaits = { LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
        },
    },
    SprintPivots = {
        AssetPath = AnimAssets.PoseSearch.Databases.SprintPivots,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Left_180_Pivot, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Sprint_Right_180_Pivot, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.PivotRequested },
            Gaits = { LocomotionGait.Sprint },
            -- 该素材会实际旋转根骨；Locked 必须保持面向目标，不能共享此库。
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Standing },
        },
    },
    CrouchStationary = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchStationary,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Idle, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Stationary },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchWalkStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchWalkStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Forward_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Back_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Left_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Right_Start, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StartRequested },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchRunStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchRunStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Forward_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Back_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Left_Start, false, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Right_Start, false, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StartRequested },
            -- 蹲伏没有独立 Sprint 动画；Sprint 意图显式降级到蹲跑，不与蹲走候选混合。
            Gaits = { LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchWalkLoops = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchWalkLoops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Forward_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Back_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Left_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Right_Loop, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Moving },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchRunLoops = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchRunLoops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.3,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Forward_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Back_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Left_Loop, true, 0.0),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Right_Loop, true, 0.0),
        },
        Eligibility = {
            Phases = { LocomotionPhase.Moving },
            Gaits = { LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchWalkStops = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchWalkStops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Forward_Stop, false, 0.6333),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Back_Stop, false, 0.2667),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Left_Stop, false, 0.9),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Walk_Right_Stop, false, 0.2667),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StopRequested },
            Gaits = { LocomotionGait.Walk },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    CrouchRunStops = {
        AssetPath = AnimAssets.PoseSearch.Databases.CrouchRunStops,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Locomotion.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Locomotion,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Forward_Stop, false, 1.2),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Back_Stop, false, 0.4333),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Left_Stop, false, 0.5),
            make_database_animation_asset(AnimAssets.Locomotion.Crouch_Run_Right_Stop, false, 0.5333),
        },
        Eligibility = {
            Phases = { LocomotionPhase.StopRequested },
            Gaits = { LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
        },
    },
    JumpStarts = {
        AssetPath = AnimAssets.PoseSearch.Databases.JumpStarts,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Airborne.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Airborne,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Jump.Start_Forward, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_Back, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_Left, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_Right, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_ForwardLeft, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_ForwardRight, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_BackLeft, false, 0.0),
            make_database_animation_asset(AnimAssets.Jump.Start_BackRight, false, 0.0),
        },
        Eligibility = {
            -- 先保持与当前 Landing 门禁相同的已验收组合；扩大组合不改变数据库结构。
            Phases = { LocomotionPhase.JumpStart },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Standing },
        },
    },
    InAirPoseOnly = {
        AssetPath = AnimAssets.PoseSearch.Databases.InAirPoseOnly,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Airborne.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Airborne,
        MovementPolicy = "InheritedAirborneMomentum",
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = -0.01,
        BaseCostBias = 0.0,
        LoopingCostBias = -0.005,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = make_in_air_pose_only_animation_assets(),
        Eligibility = {
            -- 首轮只开放既定最小验收组合；三个阶段共享候选，但每次阶段变化都会发布新请求。
            Phases = { LocomotionPhase.Ascending, LocomotionPhase.Apex, LocomotionPhase.Falling },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Standing },
        },
    },
    Landing = {
        AssetPath = AnimAssets.PoseSearch.Databases.Landing,
        Schema = SekiroMotionMatchingConfig.SchemaContracts.Airborne.AssetPath,
        NormalizationSet = AnimAssets.PoseSearch.NormalizationSets.Airborne,
        PoseSearchMode = PoseSearchMode.BruteForce,
        ContinuingPoseCostBias = 0.0,
        BaseCostBias = 0.0,
        LoopingCostBias = 0.0,
        ExcludeFromDatabaseMin = 0.0,
        ExcludeFromDatabaseMax = -0.05,
        AnimationAssets = {
            make_database_animation_asset(AnimAssets.Jump.Land_Forward, false, 0.7333),
            make_database_animation_asset(AnimAssets.Jump.Land_Back, false, 0.3667),
            make_database_animation_asset(AnimAssets.Jump.Land_Left, false, 0.2333),
            make_database_animation_asset(AnimAssets.Jump.Land_Right, false, 0.6333),
            make_database_animation_asset(AnimAssets.Jump.Land_ForwardLeft, false, 0.4),
            make_database_animation_asset(AnimAssets.Jump.Land_ForwardRight, false, 0.4),
            make_database_animation_asset(AnimAssets.Jump.Land_BackLeft, false, 0.5),
            make_database_animation_asset(AnimAssets.Jump.Land_BackRight, false, 0.8),
        },
        Eligibility = {
            -- 首轮只开放既定最小验收组合；完整 Gait/Mode/Stance 覆盖仍由 TargetCoverage 追踪。
            Phases = { LocomotionPhase.Landing },
            Gaits = { LocomotionGait.Run },
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Standing },
        },
    },
}

-- 数组固定生成顺序；新增项目数据库只需登记配置和顺序，插件不需要增加领域类型。
SekiroMotionMatchingConfig.DatabaseOrder = {
    "Stationary",
    "WalkStarts",
    "RunStarts",
    "SprintStarts",
    "WalkLoops",
    "RunLoops",
    "SprintLoops",
    "WalkStops",
    "RunStops",
    "SprintStops",
    "SprintPivots",
    "CrouchStationary",
    "CrouchWalkStarts",
    "CrouchRunStarts",
    "CrouchWalkLoops",
    "CrouchRunLoops",
    "CrouchWalkStops",
    "CrouchRunStops",
    "JumpStarts",
    "InAirPoseOnly",
    "Landing",
}

---@type SKLocomotionChooserSettings
SekiroMotionMatchingConfig.Chooser = {
    AssetPath = AnimAssets.Chooser.LocomotionDatabases,
    ContextClassPath = "/Script/Sekiro.SKMotionMatchingAnimInstance",
    EmptyResultPolicy = "Error",
}

-- 这是正式系统的最终数据覆盖目标，不是当前资产生成清单。Databases/DatabaseOrder 只登记已经有
-- 合法动画的资产；未完成的族不得因此生成空数据库。方向属于连续轨迹/姿势特征的库内排序语义，
-- 不能增加为第五个 Chooser 硬门禁，也不能用八向标签替代 Motion Matching 的连续 Cost 比较。
---@type SKMotionMatchingTargetCoverageContract
SekiroMotionMatchingConfig.TargetCoverage = {
    Version = 1,
    ChooserDimensions = {
        "Phase",
        "Gait",
        "Mode",
        "Stance",
    },
    RankingDimensions = {
        "Trajectory.PositionXY",
        "Trajectory.VelocityXY",
        "Trajectory.FacingDirectionXY",
        "Pose",
    },
    DatabaseSplitPolicy = "EvidenceDrivenSharing",
    DirectionSets = {
        Planar8 = {
            { Name = "Forward", CenterYawDegrees = 0.0 },
            { Name = "ForwardRight", CenterYawDegrees = 45.0 },
            { Name = "Right", CenterYawDegrees = 90.0 },
            { Name = "BackwardRight", CenterYawDegrees = 135.0 },
            { Name = "Backward", CenterYawDegrees = 180.0 },
            { Name = "BackwardLeft", CenterYawDegrees = -135.0 },
            { Name = "Left", CenterYawDegrees = -90.0 },
            { Name = "ForwardLeft", CenterYawDegrees = -45.0 },
        },
    },
    Profiles = {
        {
            Id = "Standing.Free",
            Phases = {
                LocomotionPhase.Stationary,
                LocomotionPhase.StartRequested,
                LocomotionPhase.Moving,
                LocomotionPhase.StopRequested,
                LocomotionPhase.PivotRequested,
            },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Standing },
            DirectionalCoverage = {
                { Target = "StartRequested", DirectionSet = "Planar8" },
                { Target = "Moving", DirectionSet = "Planar8" },
                { Target = "StopRequested", DirectionSet = "Planar8" },
                { Target = "PivotRequested", DirectionSet = "Planar8" },
            },
            ClipRoles = { "Idle", "Start", "Loop", "Stop", "Pivot" },
        },
        {
            Id = "Standing.Locked",
            Phases = {
                LocomotionPhase.Stationary,
                LocomotionPhase.StartRequested,
                LocomotionPhase.Moving,
                LocomotionPhase.StopRequested,
                LocomotionPhase.PivotRequested,
            },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing },
            DirectionalCoverage = {
                { Target = "StartRequested", DirectionSet = "Planar8" },
                { Target = "Moving", DirectionSet = "Planar8" },
                { Target = "StopRequested", DirectionSet = "Planar8" },
                { Target = "PivotRequested", DirectionSet = "Planar8" },
            },
            ClipRoles = { "Idle", "Start", "Loop", "Stop", "Pivot" },
        },
        {
            Id = "Crouching.Free",
            Phases = {
                LocomotionPhase.Stationary,
                LocomotionPhase.StartRequested,
                LocomotionPhase.Moving,
                LocomotionPhase.StopRequested,
                LocomotionPhase.PivotRequested,
            },
            -- Crouch 输入只改变 Stance，不覆盖进入蹲伏前持久保存的 Gait。
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free },
            Stances = { LocomotionStance.Crouching },
            DirectionalCoverage = {
                { Target = "StartRequested", DirectionSet = "Planar8" },
                { Target = "Moving", DirectionSet = "Planar8" },
                { Target = "StopRequested", DirectionSet = "Planar8" },
                { Target = "PivotRequested", DirectionSet = "Planar8" },
            },
            ClipRoles = { "Idle", "Start", "Loop", "Stop", "Pivot" },
        },
        {
            Id = "Crouching.Locked",
            Phases = {
                LocomotionPhase.Stationary,
                LocomotionPhase.StartRequested,
                LocomotionPhase.Moving,
                LocomotionPhase.StopRequested,
                LocomotionPhase.PivotRequested,
            },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Crouching },
            DirectionalCoverage = {
                { Target = "StartRequested", DirectionSet = "Planar8" },
                { Target = "Moving", DirectionSet = "Planar8" },
                { Target = "StopRequested", DirectionSet = "Planar8" },
                { Target = "PivotRequested", DirectionSet = "Planar8" },
            },
            ClipRoles = { "Idle", "Start", "Loop", "Stop", "Pivot" },
        },
        {
            Id = "Airborne",
            Phases = {
                LocomotionPhase.JumpStart,
                LocomotionPhase.Ascending,
                LocomotionPhase.Apex,
                LocomotionPhase.Falling,
                LocomotionPhase.Landing,
            },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing, LocomotionStance.Crouching },
            DirectionalCoverage = {
                { Target = "JumpStart", DirectionSet = "Planar8" },
                { Target = "Ascending", DirectionSet = "Planar8" },
                { Target = "Falling", DirectionSet = "Planar8" },
                { Target = "Landing", DirectionSet = "Planar8" },
            },
            ClipRoles = { "JumpStart", "Ascending", "Apex", "Falling", "Landing" },
        },
        {
            Id = "Recovery",
            Phases = { LocomotionPhase.RecoveryRequested },
            Gaits = { LocomotionGait.Walk, LocomotionGait.Run, LocomotionGait.Sprint },
            Modes = { LocomotionRotationMode.Free, LocomotionRotationMode.Locked },
            Stances = { LocomotionStance.Standing, LocomotionStance.Crouching },
            DirectionalCoverage = {
                { Target = "RecoverToMove", DirectionSet = "Planar8" },
            },
            ClipRoles = { "RecoverToIdle", "RecoverToMove" },
        },
    },
    NonLocomotionPhases = {
        LocomotionPhase.ActionOwned,
    },
}

-- 该策略是独立于数据库 Eligibility、Schema Layout 和所有 Cost/Feature Weight 的验收边界。
-- 修改权重不会改变覆盖或轨迹门禁；扩展动画集时必须显式更新这里，而不能让错误配置自行“自洽”。
---@type SKMotionMatchingAuthoringValidationPolicy
SekiroMotionMatchingConfig.AuthoringValidation = {
    RequiredCoverage = {
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "WalkStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Start,
                AnimAssets.Locomotion.Walk_Back_Start,
                AnimAssets.Locomotion.Walk_Left_Start,
                AnimAssets.Locomotion.Walk_Right_Start,
            },
        },
        {
            Database = "RunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Start,
                AnimAssets.Locomotion.Run_Back_Start,
                AnimAssets.Locomotion.Run_Left_Start,
                AnimAssets.Locomotion.Run_Right_Start,
            },
        },
        {
            Database = "SprintStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Start,
                AnimAssets.Locomotion.Sprint_Back_Turn_Start,
                AnimAssets.Locomotion.Sprint_Left_Turn_Start,
                AnimAssets.Locomotion.Sprint_Right_Turn_Start,
            },
        },
        {
            Database = "WalkLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Loop,
                AnimAssets.Locomotion.Walk_Back_Loop,
                AnimAssets.Locomotion.Walk_Left_Loop,
                AnimAssets.Locomotion.Walk_Right_Loop,
            },
        },
        {
            Database = "RunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Loop,
                AnimAssets.Locomotion.Run_Back_Loop,
                AnimAssets.Locomotion.Run_Left_Loop,
                AnimAssets.Locomotion.Run_Right_Loop,
            },
        },
        {
            Database = "SprintLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Loop,
            },
        },
        {
            Database = "WalkStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Stop,
                AnimAssets.Locomotion.Walk_Back_Stop,
                AnimAssets.Locomotion.Walk_Left_Stop,
                AnimAssets.Locomotion.Walk_Right_Stop,
            },
        },
        {
            Database = "RunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Stop,
                AnimAssets.Locomotion.Run_Back_Stop,
                AnimAssets.Locomotion.Run_Left_Stop,
                AnimAssets.Locomotion.Run_Right_Stop,
            },
        },
        {
            Database = "SprintStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Stop,
                AnimAssets.Locomotion.Sprint_Forward_Left_Turn_Stop,
                AnimAssets.Locomotion.Sprint_Forward_Right_Turn_Stop,
            },
        },
        {
            Database = "SprintPivots",
            Phase = LocomotionPhase.PivotRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Left_180_Pivot,
                AnimAssets.Locomotion.Sprint_Right_180_Pivot,
            },
        },
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "Stationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Idle,
            },
        },
        {
            Database = "WalkStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Start,
                AnimAssets.Locomotion.Walk_Back_Start,
                AnimAssets.Locomotion.Walk_Left_Start,
                AnimAssets.Locomotion.Walk_Right_Start,
            },
        },
        {
            Database = "RunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Start,
                AnimAssets.Locomotion.Run_Back_Start,
                AnimAssets.Locomotion.Run_Left_Start,
                AnimAssets.Locomotion.Run_Right_Start,
            },
        },
        {
            Database = "SprintStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Start,
                AnimAssets.Locomotion.Sprint_Back_Turn_Start,
                AnimAssets.Locomotion.Sprint_Left_Turn_Start,
                AnimAssets.Locomotion.Sprint_Right_Turn_Start,
            },
        },
        {
            Database = "WalkLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Loop,
                AnimAssets.Locomotion.Walk_Back_Loop,
                AnimAssets.Locomotion.Walk_Left_Loop,
                AnimAssets.Locomotion.Walk_Right_Loop,
            },
        },
        {
            Database = "RunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Loop,
                AnimAssets.Locomotion.Run_Back_Loop,
                AnimAssets.Locomotion.Run_Left_Loop,
                AnimAssets.Locomotion.Run_Right_Loop,
            },
        },
        {
            Database = "SprintLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Loop,
            },
        },
        {
            Database = "WalkStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Walk_Forward_Stop,
                AnimAssets.Locomotion.Walk_Back_Stop,
                AnimAssets.Locomotion.Walk_Left_Stop,
                AnimAssets.Locomotion.Walk_Right_Stop,
            },
        },
        {
            Database = "RunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Run_Forward_Stop,
                AnimAssets.Locomotion.Run_Back_Stop,
                AnimAssets.Locomotion.Run_Left_Stop,
                AnimAssets.Locomotion.Run_Right_Stop,
            },
        },
        {
            Database = "SprintStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Sprint_Forward_Stop,
                AnimAssets.Locomotion.Sprint_Forward_Left_Turn_Stop,
                AnimAssets.Locomotion.Sprint_Forward_Right_Turn_Stop,
            },
        },
        -- Crouch 只改变 Stance 并保留进入前 Gait；Sprint 在没有专用蹲冲刺素材时
        -- 显式降级到 Crouch Run，而不是错误回到 Standing Sprint 数据库。
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchWalkStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Start,
                AnimAssets.Locomotion.Crouch_Walk_Back_Start,
                AnimAssets.Locomotion.Crouch_Walk_Left_Start,
                AnimAssets.Locomotion.Crouch_Walk_Right_Start,
            },
        },
        {
            Database = "CrouchRunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Start,
                AnimAssets.Locomotion.Crouch_Run_Back_Start,
                AnimAssets.Locomotion.Crouch_Run_Left_Start,
                AnimAssets.Locomotion.Crouch_Run_Right_Start,
            },
        },
        {
            Database = "CrouchRunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Start,
                AnimAssets.Locomotion.Crouch_Run_Back_Start,
                AnimAssets.Locomotion.Crouch_Run_Left_Start,
                AnimAssets.Locomotion.Crouch_Run_Right_Start,
            },
        },
        {
            Database = "CrouchWalkLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Back_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Left_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Right_Loop,
            },
        },
        {
            Database = "CrouchRunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Run_Back_Loop,
                AnimAssets.Locomotion.Crouch_Run_Left_Loop,
                AnimAssets.Locomotion.Crouch_Run_Right_Loop,
            },
        },
        {
            Database = "CrouchRunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Run_Back_Loop,
                AnimAssets.Locomotion.Crouch_Run_Left_Loop,
                AnimAssets.Locomotion.Crouch_Run_Right_Loop,
            },
        },
        {
            Database = "CrouchWalkStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Back_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Left_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Right_Stop,
            },
        },
        {
            Database = "CrouchRunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Run_Back_Stop,
                AnimAssets.Locomotion.Crouch_Run_Left_Stop,
                AnimAssets.Locomotion.Crouch_Run_Right_Stop,
            },
        },
        {
            Database = "CrouchRunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Run_Back_Stop,
                AnimAssets.Locomotion.Crouch_Run_Left_Stop,
                AnimAssets.Locomotion.Crouch_Run_Right_Stop,
            },
        },
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchStationary",
            Phase = LocomotionPhase.Stationary,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Idle,
            },
        },
        {
            Database = "CrouchWalkStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Start,
                AnimAssets.Locomotion.Crouch_Walk_Back_Start,
                AnimAssets.Locomotion.Crouch_Walk_Left_Start,
                AnimAssets.Locomotion.Crouch_Walk_Right_Start,
            },
        },
        {
            Database = "CrouchRunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Start,
                AnimAssets.Locomotion.Crouch_Run_Back_Start,
                AnimAssets.Locomotion.Crouch_Run_Left_Start,
                AnimAssets.Locomotion.Crouch_Run_Right_Start,
            },
        },
        {
            Database = "CrouchRunStarts",
            Phase = LocomotionPhase.StartRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Start,
                AnimAssets.Locomotion.Crouch_Run_Back_Start,
                AnimAssets.Locomotion.Crouch_Run_Left_Start,
                AnimAssets.Locomotion.Crouch_Run_Right_Start,
            },
        },
        {
            Database = "CrouchWalkLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Back_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Left_Loop,
                AnimAssets.Locomotion.Crouch_Walk_Right_Loop,
            },
        },
        {
            Database = "CrouchRunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Run_Back_Loop,
                AnimAssets.Locomotion.Crouch_Run_Left_Loop,
                AnimAssets.Locomotion.Crouch_Run_Right_Loop,
            },
        },
        {
            Database = "CrouchRunLoops",
            Phase = LocomotionPhase.Moving,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Loop,
                AnimAssets.Locomotion.Crouch_Run_Back_Loop,
                AnimAssets.Locomotion.Crouch_Run_Left_Loop,
                AnimAssets.Locomotion.Crouch_Run_Right_Loop,
            },
        },
        {
            Database = "CrouchWalkStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Walk,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Walk_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Back_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Left_Stop,
                AnimAssets.Locomotion.Crouch_Walk_Right_Stop,
            },
        },
        {
            Database = "CrouchRunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Run_Back_Stop,
                AnimAssets.Locomotion.Crouch_Run_Left_Stop,
                AnimAssets.Locomotion.Crouch_Run_Right_Stop,
            },
        },
        {
            Database = "CrouchRunStops",
            Phase = LocomotionPhase.StopRequested,
            Gait = LocomotionGait.Sprint,
            Mode = LocomotionRotationMode.Locked,
            Stance = LocomotionStance.Crouching,
            RequiredAnimationAssets = {
                AnimAssets.Locomotion.Crouch_Run_Forward_Stop,
                AnimAssets.Locomotion.Crouch_Run_Back_Stop,
                AnimAssets.Locomotion.Crouch_Run_Left_Stop,
                AnimAssets.Locomotion.Crouch_Run_Right_Stop,
            },
        },
        {
            Database = "JumpStarts",
            Phase = LocomotionPhase.JumpStart,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Jump.Start_Forward,
                AnimAssets.Jump.Start_Back,
                AnimAssets.Jump.Start_Left,
                AnimAssets.Jump.Start_Right,
                AnimAssets.Jump.Start_ForwardLeft,
                AnimAssets.Jump.Start_ForwardRight,
                AnimAssets.Jump.Start_BackLeft,
                AnimAssets.Jump.Start_BackRight,
            },
        },
        {
            Database = "InAirPoseOnly",
            Phase = LocomotionPhase.Ascending,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = in_air_pose_only_asset_paths,
        },
        {
            Database = "InAirPoseOnly",
            Phase = LocomotionPhase.Apex,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = in_air_pose_only_asset_paths,
        },
        {
            Database = "InAirPoseOnly",
            Phase = LocomotionPhase.Falling,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = in_air_pose_only_asset_paths,
        },
        {
            Database = "Landing",
            Phase = LocomotionPhase.Landing,
            Gait = LocomotionGait.Run,
            Mode = LocomotionRotationMode.Free,
            Stance = LocomotionStance.Standing,
            RequiredAnimationAssets = {
                AnimAssets.Jump.Land_Forward,
                AnimAssets.Jump.Land_Back,
                AnimAssets.Jump.Land_Left,
                AnimAssets.Jump.Land_Right,
                AnimAssets.Jump.Land_ForwardLeft,
                AnimAssets.Jump.Land_ForwardRight,
                AnimAssets.Jump.Land_BackLeft,
                AnimAssets.Jump.Land_BackRight,
            },
        },
    },
    Trajectory = {
        QueryOriginSource = "CompletedActualState.ActorTransformWS",
        PositionSpace = "QueryOrigin",
        FacingSpace = "QueryOrigin",
        PosePositionSpace = "Root",
        PoseVelocitySpace = "Character",
        TranslationAxisYawOffsetDegrees = -90.0,
        WorldSpaceFeaturePolicy = "Reject",
        ExpectedCardinality = 34,
        MinimumFutureHorizon = 0.70,
        PastRequiredFeatures = { "PositionXY" },
        CurrentRequiredFeatures = { "VelocityXY", "FacingDirectionXY" },
        FutureRequiredFeatures = { "PositionXY", "FacingDirectionXY" },
        FarthestRequiredFeatures = { "VelocityXY" },
    },
    AirborneTrajectory = {
        QueryOriginSource = "CompletedActualState.ActorTransformWS",
        PositionSpace = "QueryOrigin",
        FacingSpace = "QueryOrigin",
        PosePositionSpace = "Root",
        PoseVelocitySpace = "Character",
        TranslationAxisYawOffsetDegrees = -90.0,
        WorldSpaceFeaturePolicy = "Reject",
        ExpectedCardinality = 42,
        MinimumFutureHorizon = 0.55,
        PastRequiredFeatures = { "Position" },
        CurrentRequiredFeatures = { "Velocity", "FacingDirectionXY" },
        FutureRequiredFeatures = { "Position", "Velocity", "FacingDirectionXY" },
        FarthestRequiredFeatures = { "Position", "Velocity" },
    },
}

---验证必填字符串并返回原值，使 IR 构建错误在任何资产修改前终止。
---@param value any 待验证的 Lua 值。
---@param field_name string 用于错误信息的稳定字段路径。
---@return string validated_value 非空且至少包含一个非空白字符的字符串。
local function require_non_empty_string(value, field_name)
    if type(value) ~= "string" or value:match("%S") == nil then
        error(field_name .. " must be a non-empty string", 2)
    end
    return value
end

---验证数据库数值为有限数，阻止 NaN 或无穷值进入 UE 反射属性。
---@param value any 待验证的 Lua 值。
---@param field_name string 用于错误信息的稳定字段路径。
---@return number validated_value 可安全交给编辑器导入器的有限数值。
local function require_finite_number(value, field_name)
    local is_finite = type(value) == "number"
        and value == value
        and value ~= math.huge
        and value ~= -math.huge
    if not is_finite then
        error(field_name .. " must be a finite number", 2)
    end
    return value
end

---验证 UnLua 暴露的原生 UENUM 值，禁止配置重新退化为枚举名称字符串。
---@param value any 待验证的原生枚举值。
---@param field_name string 用于错误信息的稳定字段路径。
---@return number validated_value 可直接交给 C++ 反射属性或作为 Lua 表键使用的枚举数值。
local function require_native_enum(value, field_name)
    if type(value) ~= "number" or value % 1 ~= 0 then
        error(field_name .. " must be a native UE enum value", 2)
    end
    return value
end

---验证并复制非空原生枚举数组，防止重复门禁值和枚举名称字符串进入运行时契约。
---@param values any 待验证的原生枚举数组。
---@param field_name string 用于错误信息的稳定字段路径。
---@return number[] copied_values 保持声明顺序的原生枚举数组。
local function copy_required_enum_array(values, field_name)
    if type(values) ~= "table" or #values == 0 then
        error(field_name .. " must be a non-empty array", 2)
    end

    local copied_values = {}
    local seen_values = {}
    for index, value in ipairs(values) do
        local validated_value = require_native_enum(
            value,
            field_name .. "[" .. index .. "]")
        if seen_values[validated_value] == true then
            error(field_name .. " contains a duplicate enum value", 2)
        end
        seen_values[validated_value] = true
        copied_values[index] = validated_value
    end
    return copied_values
end

---验证并复制非空字符串数组，防止空集合、重复语义或导入器回写污染正式配置。
---@param values any 待验证的数组值。
---@param field_name string 用于错误信息的稳定字段路径。
---@return string[] copied_values 保持顺序的新数组。
local function copy_required_string_array(values, field_name)
    if type(values) ~= "table" or #values == 0 then
        error(field_name .. " must be a non-empty array", 2)
    end

    local copied_values = {}
    local seen_values = {}
    for index, value in ipairs(values) do
        local validated_value = require_non_empty_string(
            value,
            field_name .. "[" .. index .. "]")
        if seen_values[validated_value] == true then
            error(field_name .. " contains duplicate value " .. validated_value, 2)
        end
        seen_values[validated_value] = true
        copied_values[index] = validated_value
    end
    return copied_values
end

---验证并复制可为空的字符串数组，供已完成全部阶段接入后的 Deferred 列表使用。
---@param values any 待验证的数组值；允许空表但不允许 nil、非字符串或重复值。
---@param field_name string 用于错误信息的稳定字段路径。
---@return string[] copied_values 保持声明顺序的新数组。
local function copy_optional_string_array(values, field_name)
    if type(values) ~= "table" then
        error(field_name .. " must be an array", 2)
    end

    local copied_values = {}
    local seen_values = {}
    for index, value in ipairs(values) do
        local validated_value = require_non_empty_string(
            value,
            field_name .. "[" .. index .. "]")
        if seen_values[validated_value] == true then
            error(field_name .. " contains duplicate value " .. validated_value, 2)
        end
        seen_values[validated_value] = true
        copied_values[index] = validated_value
    end
    return copied_values
end

---把已验证字符串数组转换为集合，供独立验收策略执行顺序无关的包含检查。
---@param values string[] 已由 copy_required_string_array 验证的字符串数组。
---@return table<string, boolean> value_set 以字符串为键的只读语义集合。
local function make_string_set(values)
    local value_set = {}
    for _, value in ipairs(values) do
        value_set[value] = true
    end
    return value_set
end

---构造 Chooser 四维语义组合的稳定键，避免显示权重或数据库路径参与覆盖判定。
---@param phase number ESKMotionMatchingLocomotionPhase 原生枚举值。
---@param gait number ESKMotionMatchingGait 原生枚举值。
---@param mode number ESKMotionMatchingRotationMode 原生枚举值。
---@param stance number ESKMotionMatchingStance 原生枚举值。
---@return number coverage_key 可用于查重和精确覆盖比较的无字符串往返稳定键。
local function make_coverage_key(phase, gait, mode, stance)
    return (((phase * 256) + gait) * 256 + mode) * 256 + stance
end

---确认实际特征集合包含策略要求的全部特征；额外特征仍由 Cardinality 和 Layout 门禁约束。
---@param actual_features string[] 实际 Schema 样本声明的特征数组。
---@param required_features string[] 独立验收策略要求的最小特征数组。
---@param field_name string 用于错误信息的实际字段路径。
---@return nil result 校验成功时无返回值，失败时直接抛出字段级错误。
local function require_feature_coverage(
    actual_features,
    required_features,
    field_name)
    local actual_set = make_string_set(actual_features)
    for _, required_feature in ipairs(required_features) do
        if actual_set[required_feature] ~= true then
            error(field_name .. " is missing required feature " .. required_feature, 2)
        end
    end
end

---校验指定 Schema 的空间、历史、当前和未来语义，防止用 Layout 或权重掩盖错误查询域。
---地面与空中分别传入独立策略；本门禁只检查查询可表达性，不用任何 Weight 判定合法性。
---@param schema_name string SchemaContracts 中待检查的稳定名称。
---@param policy_name string AuthoringValidation 中对应的轨迹策略名称。
---@return nil result 校验成功时无返回值，失败时直接抛出策略错误。
local function validate_trajectory_authoring_policy(schema_name, policy_name)
    local authoring_policy = SekiroMotionMatchingConfig.AuthoringValidation
    local policy = type(authoring_policy) == "table"
        and authoring_policy[policy_name]
        or nil
    local schema = SekiroMotionMatchingConfig.SchemaContracts[schema_name]
    local policy_path = "AuthoringValidation." .. policy_name
    local schema_path = "SchemaContracts." .. schema_name
    if type(policy) ~= "table" or type(schema) ~= "table" then
        error(policy_path .. " requires " .. schema_path, 2)
    end

    local coordinate_space = schema.CoordinateSpace
    if type(coordinate_space) ~= "table" then
        error(schema_path .. ".CoordinateSpace must be a table", 2)
    end
    local coordinate_requirements = {
        { "QueryOriginSource", "QueryOriginSource" },
        { "TrajectoryPositionSpace", "PositionSpace" },
        { "TrajectoryFacingSpace", "FacingSpace" },
        { "PosePositionSpace", "PosePositionSpace" },
        { "PoseVelocitySpace", "PoseVelocitySpace" },
        { "WorldSpaceFeaturePolicy", "WorldSpaceFeaturePolicy" },
    }
    for _, requirement in ipairs(coordinate_requirements) do
        local schema_field = requirement[1]
        local policy_field = requirement[2]
        local expected_value = require_non_empty_string(
            policy[policy_field],
            policy_path .. "." .. policy_field)
        if coordinate_space[schema_field] ~= expected_value then
            error(
                schema_path .. ".CoordinateSpace." .. schema_field
                    .. " violates " .. policy_path,
                2)
        end
    end

    local required_axis_offset = require_finite_number(
        policy.TranslationAxisYawOffsetDegrees,
        policy_path .. ".TranslationAxisYawOffsetDegrees")
    if coordinate_space.TranslationAxisYawOffsetDegrees ~= required_axis_offset then
        error(
            schema_path .. ".CoordinateSpace.TranslationAxisYawOffsetDegrees"
                .. " violates " .. policy_path,
            2)
    end
    local required_cardinality = require_finite_number(
        policy.ExpectedCardinality,
        policy_path .. ".ExpectedCardinality")
    if required_cardinality % 1 ~= 0 or required_cardinality <= 0 then
        error(policy_path .. ".ExpectedCardinality must be positive integer", 2)
    end
    if schema.ExpectedCardinality ~= required_cardinality then
        error(
            schema_path .. ".ExpectedCardinality violates " .. policy_path,
            2)
    end
    if schema.bUseCharacterSpaceVelocities ~= true then
        error(schema_path .. ".bUseCharacterSpaceVelocities must remain true", 2)
    end

    local minimum_future_horizon = require_finite_number(
        policy.MinimumFutureHorizon,
        policy_path .. ".MinimumFutureHorizon")
    if minimum_future_horizon <= 0.0 then
        error(policy_path .. ".MinimumFutureHorizon must be positive", 2)
    end
    local past_required_features = copy_required_string_array(
        policy.PastRequiredFeatures,
        policy_path .. ".PastRequiredFeatures")
    local current_required_features = copy_required_string_array(
        policy.CurrentRequiredFeatures,
        policy_path .. ".CurrentRequiredFeatures")
    local future_required_features = copy_required_string_array(
        policy.FutureRequiredFeatures,
        policy_path .. ".FutureRequiredFeatures")
    local farthest_required_features = copy_required_string_array(
        policy.FarthestRequiredFeatures,
        policy_path .. ".FarthestRequiredFeatures")

    if type(schema.TrajectorySamples) ~= "table"
        or #schema.TrajectorySamples == 0 then
        error(schema_path .. ".TrajectorySamples must be non-empty", 2)
    end
    local previous_offset = nil
    local has_past_sample = false
    local has_current_sample = false
    local farthest_future_sample = nil
    local farthest_future_features = nil
    for index, sample in ipairs(schema.TrajectorySamples) do
        local sample_path = schema_path .. ".TrajectorySamples[" .. index .. "]"
        if type(sample) ~= "table" then
            error(sample_path .. " must be a table", 2)
        end
        local offset = require_finite_number(sample.Offset, sample_path .. ".Offset")
        if previous_offset ~= nil and offset <= previous_offset then
            error(schema_path .. ".TrajectorySamples offsets must be strictly increasing", 2)
        end
        previous_offset = offset
        local features = copy_required_string_array(
            sample.Features,
            sample_path .. ".Features")
        if offset < 0.0 then
            require_feature_coverage(features, past_required_features, sample_path .. ".Features")
            has_past_sample = true
        elseif offset == 0.0 then
            require_feature_coverage(
                features,
                current_required_features,
                sample_path .. ".Features")
            has_current_sample = true
        else
            require_feature_coverage(features, future_required_features, sample_path .. ".Features")
            farthest_future_sample = offset
            farthest_future_features = features
        end
    end
    if not has_past_sample then
        error(schema_path .. ".TrajectorySamples requires a past sample", 2)
    end
    if not has_current_sample then
        error(schema_path .. ".TrajectorySamples requires a zero-time sample", 2)
    end
    if farthest_future_sample == nil
        or farthest_future_sample < minimum_future_horizon then
        error(schema_path .. ".TrajectorySamples future horizon is insufficient", 2)
    end
    require_feature_coverage(
        farthest_future_features,
        farthest_required_features,
        schema_path .. ".TrajectorySamples[farthest].Features")
end

---校验当前正式数据库的动画覆盖和 Chooser 四维门禁，拒绝遗漏、重叠、越权组合及跨库复用。
---该函数直接比较独立 RequiredCoverage，不从 Eligibility 反推期望值，也不读取任何 Cost Bias。
---@return nil result 校验成功时无返回值，失败时直接抛出覆盖或所有权错误。
local function validate_database_coverage_policy()
    local policy = SekiroMotionMatchingConfig.AuthoringValidation
    if type(policy) ~= "table"
        or type(policy.RequiredCoverage) ~= "table"
        or #policy.RequiredCoverage == 0 then
        error("AuthoringValidation.RequiredCoverage must be a non-empty array", 2)
    end

    local expected_by_key = {}
    local requirements_by_database = {}
    for index, requirement in ipairs(policy.RequiredCoverage) do
        local field_root = "AuthoringValidation.RequiredCoverage[" .. index .. "]"
        if type(requirement) ~= "table" then
            error(field_root .. " must be a table", 2)
        end
        local database_name = require_non_empty_string(
            requirement.Database,
            field_root .. ".Database")
        if type(SekiroMotionMatchingConfig.Databases[database_name]) ~= "table" then
            error(field_root .. ".Database is not registered: " .. database_name, 2)
        end
        local phase = require_native_enum(requirement.Phase, field_root .. ".Phase")
        local gait = require_native_enum(requirement.Gait, field_root .. ".Gait")
        local mode = require_native_enum(requirement.Mode, field_root .. ".Mode")
        local stance = require_native_enum(requirement.Stance, field_root .. ".Stance")
        local coverage_key = make_coverage_key(phase, gait, mode, stance)
        if expected_by_key[coverage_key] ~= nil then
            error(field_root .. " duplicates required coverage " .. coverage_key, 2)
        end
        local required_animation_assets = copy_required_string_array(
            requirement.RequiredAnimationAssets,
            field_root .. ".RequiredAnimationAssets")
        expected_by_key[coverage_key] = database_name
        requirements_by_database[database_name] =
            requirements_by_database[database_name] or {}
        table.insert(requirements_by_database[database_name], required_animation_assets)
    end

    local database_order = copy_required_string_array(
        SekiroMotionMatchingConfig.DatabaseOrder,
        "DatabaseOrder")
    local ordered_databases = make_string_set(database_order)
    for database_name, _ in pairs(SekiroMotionMatchingConfig.Databases) do
        if ordered_databases[database_name] ~= true then
            error("Databases." .. database_name .. " is missing from DatabaseOrder", 2)
        end
    end

    local actual_by_key = {}
    local enabled_animation_owner = {}
    for _, database_name in ipairs(database_order) do
        local database = SekiroMotionMatchingConfig.Databases[database_name]
        local field_root = "Databases." .. database_name
        if type(database) ~= "table" then
            error(field_root .. " is not registered", 2)
        end
        if type(database.AnimationAssets) ~= "table"
            or #database.AnimationAssets == 0 then
            error(field_root .. ".AnimationAssets must be a non-empty array", 2)
        end

        local enabled_animation_paths = {}
        for index, animation_asset in ipairs(database.AnimationAssets) do
            local asset_path = field_root .. ".AnimationAssets[" .. index .. "]"
            if type(animation_asset) ~= "table" then
                error(asset_path .. " must be a table", 2)
            end
            local object_path = require_non_empty_string(
                animation_asset.AssetPath,
                asset_path .. ".AssetPath")
            if type(animation_asset.bEnabled) ~= "boolean" then
                error(asset_path .. ".bEnabled must be boolean", 2)
            end
            if animation_asset.bEnabled == true then
                if enabled_animation_paths[object_path] == true then
                    error(field_root .. " repeats enabled animation " .. object_path, 2)
                end
                local previous_owner = enabled_animation_owner[object_path]
                if previous_owner ~= nil and previous_owner ~= database_name then
                    error(
                        "Enabled animation " .. object_path .. " is shared by databases "
                            .. previous_owner .. " and " .. database_name,
                        2)
                end
                enabled_animation_paths[object_path] = true
                enabled_animation_owner[object_path] = database_name
            end
        end
        if next(enabled_animation_paths) == nil then
            error(field_root .. " has no enabled animation coverage", 2)
        end

        local database_requirements = requirements_by_database[database_name] or {}
        for _, required_animation_assets in ipairs(database_requirements) do
            for _, required_animation in ipairs(required_animation_assets) do
                if enabled_animation_paths[required_animation] ~= true then
                    error(
                        field_root .. " is missing required enabled animation "
                            .. required_animation,
                        2)
                end
            end
        end

        if type(database.Eligibility) ~= "table" then
            error(field_root .. ".Eligibility must be a table", 2)
        end
        local phases = copy_required_enum_array(
            database.Eligibility.Phases,
            field_root .. ".Eligibility.Phases")
        local gaits = copy_required_enum_array(
            database.Eligibility.Gaits,
            field_root .. ".Eligibility.Gaits")
        local modes = copy_required_enum_array(
            database.Eligibility.Modes,
            field_root .. ".Eligibility.Modes")
        local stances = copy_required_enum_array(
            database.Eligibility.Stances,
            field_root .. ".Eligibility.Stances")
        for _, phase in ipairs(phases) do
            for _, gait in ipairs(gaits) do
                for _, mode in ipairs(modes) do
                    for _, stance in ipairs(stances) do
                        local coverage_key = make_coverage_key(phase, gait, mode, stance)
                        local previous_owner = actual_by_key[coverage_key]
                        if previous_owner ~= nil then
                            error(
                                "Chooser coverage " .. coverage_key
                                    .. " overlaps databases " .. previous_owner
                                    .. " and " .. database_name,
                                2)
                        end
                        local expected_owner = expected_by_key[coverage_key]
                        if expected_owner == nil then
                            error(
                                field_root .. ".Eligibility adds unsupported coverage "
                                    .. coverage_key,
                                2)
                        end
                        if expected_owner ~= database_name then
                            error(
                                field_root .. ".Eligibility routes " .. coverage_key
                                    .. " to " .. database_name .. " instead of "
                                    .. expected_owner,
                                2)
                        end
                        actual_by_key[coverage_key] = database_name
                    end
                end
            end
        end
    end

    for coverage_key, expected_owner in pairs(expected_by_key) do
        if actual_by_key[coverage_key] ~= expected_owner then
            error(
                "Chooser is missing required coverage " .. coverage_key
                    .. " for database " .. expected_owner,
                2)
        end
    end
end

---执行与所有权重无关的项目级作者配置门禁，任一失败都必须阻止 IR 和资产生成。
---@return nil result 全部作者契约通过时无返回值，任一失败会中断后续生成。
local function validate_authoring_contracts()
    validate_trajectory_authoring_policy("Locomotion", "Trajectory")
    validate_trajectory_authoring_policy("Airborne", "AirborneTrajectory")
    validate_database_coverage_policy()
end

---把项目特征名编译为 UE5.8 位掩码，并拒绝与原生 Finalize 展开顺序不一致的声明。
---@param features any 特征名数组。
---@param definitions table<string, table> 目标 Channel 的特征位、维数和顺序定义。
---@param field_name string 用于错误信息的稳定字段路径。
---@return integer flags 合并后的 UE5.8 位掩码。
---@return table[] compiled_features 已验证、保持原生展开顺序的特征描述。
local function compile_feature_flags(features, definitions, field_name)
    local feature_names = copy_required_string_array(features, field_name)
    local flags = 0
    local last_finalize_order = 0
    local compiled_features = {}
    for index, feature_name in ipairs(feature_names) do
        local definition = definitions[feature_name]
        if type(definition) ~= "table" then
            error(field_name .. " contains unsupported UE5.8 feature " .. feature_name, 2)
        end
        if definition.FinalizeOrder <= last_finalize_order then
            error(field_name .. " does not follow UE5.8 Channel Finalize order", 2)
        end
        flags = flags + definition.Mask
        last_finalize_order = definition.FinalizeOrder
        compiled_features[index] = {
            Name = feature_name,
            Dimension = definition.Dimension,
        }
    end
    return flags, compiled_features
end

---校验 Schema 声明的向量布局与 Channel 可推导布局逐段一致，防止离线与运行时静默错位。
---@param declared_layout any Schema 中人工可读的严格布局。
---@param derived_layout table[] 从实际 Channel 配置推导的布局。
---@param expected_cardinality any 预期总维数。
---@param field_root string 当前 Schema 的稳定字段路径，用于精确诊断。
---@return nil result 布局与维数一致时无返回值，漂移时直接抛出错误。
local function validate_feature_vector_layout(
    declared_layout,
    derived_layout,
    expected_cardinality,
    field_root)
    if type(declared_layout) ~= "table" or #declared_layout ~= #derived_layout then
        error(field_root .. ".FeatureVectorLayout segment count mismatch", 2)
    end
    local cardinality = 0
    for index, derived_segment in ipairs(derived_layout) do
        local declared_segment = declared_layout[index]
        if type(declared_segment) ~= "table"
            or declared_segment.Id ~= derived_segment.Id
            or declared_segment.Dimension ~= derived_segment.Dimension then
            error(
                field_root .. ".FeatureVectorLayout[" .. index
                    .. "] does not match the UE5.8 Channel layout",
                2)
        end
        cardinality = cardinality + derived_segment.Dimension
    end
    if type(expected_cardinality) ~= "number"
        or expected_cardinality % 1 ~= 0
        or expected_cardinality <= 0
        or cardinality ~= expected_cardinality then
        error(field_root .. ".ExpectedCardinality mismatch", 2)
    end
end

---构建指定 Pose Search Schema 的版本化全量配置 IR。
---Skeleton、Channel、采样点和骨骼均来自同一 Lua 契约，数据库离线索引与运行时 BuildQuery 因而共享结构。
---@param schema_name string SchemaContracts 中登记的稳定名称。
---@return SKPoseSearchSchemaIR schema_ir 可由通用反射入口物化的 Schema 描述。
function SekiroMotionMatchingConfig.CompileSchemaIR(schema_name)
    validate_authoring_contracts()
    local validated_name = require_non_empty_string(schema_name, "SchemaName")
    local schema = SekiroMotionMatchingConfig.SchemaContracts[validated_name]
    if type(schema) ~= "table" then
        error("SchemaContracts." .. validated_name .. " is not registered", 2)
    end
    local field_root = "SchemaContracts." .. validated_name
    local target_object_path = require_non_empty_string(
        schema.AssetPath,
        field_root .. ".AssetPath")
    local skeleton_path = require_non_empty_string(
        schema.SkeletonPath,
        field_root .. ".SkeletonPath")
    local sample_rate = require_finite_number(
        schema.SampleRate,
        field_root .. ".SampleRate")
    if sample_rate % 1 ~= 0 or sample_rate < 1 or sample_rate > 240 then
        error(field_root .. ".SampleRate must be an integer in [1, 240]", 2)
    end
    if schema.DataPreprocessor ~= PoseSearchDataPreprocessor.Normalize then
        error(field_root .. ".DataPreprocessor must be Normalize", 2)
    end
    if schema.InputQueryPose ~= InputQueryPose.UseContinuingPose then
        error(field_root .. ".InputQueryPose must be UseContinuingPose", 2)
    end
    if schema.NumberOfPermutations ~= 1 then
        error(field_root .. ".NumberOfPermutations must be 1", 2)
    end
    if schema.bAddDataPadding ~= false
        or schema.bInjectAdditionalDebugChannels ~= false then
        error(field_root .. " must not add padding or debug channels", 2)
    end
    if type(schema.bUseCharacterSpaceVelocities) ~= "boolean" then
        error(field_root .. ".bUseCharacterSpaceVelocities must be boolean", 2)
    end
    local trajectory_weight = require_finite_number(
        schema.TrajectoryWeight,
        field_root .. ".TrajectoryWeight")
    local pose_weight = require_finite_number(
        schema.PoseWeight,
        field_root .. ".PoseWeight")
    if trajectory_weight <= 0.0 or pose_weight <= 0.0 then
        error(field_root .. " Channel weights must be positive", 2)
    end

    local trajectory_samples = {}
    local derived_layout = {}
    for sample_index, sample in ipairs(schema.TrajectorySamples) do
        local sample_path = field_root .. ".TrajectorySamples[" .. sample_index .. "]"
        local offset = require_finite_number(sample.Offset, sample_path .. ".Offset")
        local weight = require_finite_number(sample.Weight, sample_path .. ".Weight")
        if weight <= 0.0 then
            error(sample_path .. ".Weight must be positive", 2)
        end
        local flags, features = compile_feature_flags(
            sample.Features,
            trajectory_feature_definitions,
            sample_path .. ".Features")
        trajectory_samples[sample_index] = {
            ValueType = "Struct",
            Value = {
                { Name = "Offset", ValueType = "Float", Value = offset },
                { Name = "Flags", ValueType = "Integer", Value = flags },
                { Name = "Weight", ValueType = "Float", Value = weight },
            },
        }
        for _, feature in ipairs(features) do
            table.insert(derived_layout, {
                Id = "Trajectory[" .. string.format("%.2f", offset) .. "]." .. feature.Name,
                Dimension = feature.Dimension,
            })
        end
    end

    local sampled_bones = {}
    for bone_index, bone in ipairs(schema.SampledBones) do
        local bone_path = field_root .. ".SampledBones[" .. bone_index .. "]"
        local bone_name = require_non_empty_string(bone.BoneName, bone_path .. ".BoneName")
        local weight = require_finite_number(bone.Weight, bone_path .. ".Weight")
        if weight <= 0.0 then
            error(bone_path .. ".Weight must be positive", 2)
        end
        local flags, features = compile_feature_flags(
            bone.Features,
            pose_feature_definitions,
            bone_path .. ".Features")
        sampled_bones[bone_index] = {
            ValueType = "Struct",
            Value = {
                {
                    Name = "Reference",
                    ValueType = "Struct",
                    Value = {
                        { Name = "BoneName", ValueType = "Name", Value = bone_name },
                    },
                },
                { Name = "Flags", ValueType = "Integer", Value = flags },
                { Name = "Weight", ValueType = "Float", Value = weight },
            },
        }
        for _, feature in ipairs(features) do
            table.insert(derived_layout, {
                Id = "Pose." .. bone_name .. "." .. feature.Name,
                Dimension = feature.Dimension,
            })
        end
    end
    validate_feature_vector_layout(
        schema.FeatureVectorLayout,
        derived_layout,
        schema.ExpectedCardinality,
        field_root)

    local asset_patch = {
        Version = 1,
        AssetType = "ObjectPropertyPatch",
        TargetObjectPath = target_object_path,
        ExpectedClassPath = "/Script/PoseSearch.PoseSearchSchema",
        Properties = {
            { Name = "SampleRate", ValueType = "Integer", Value = sample_rate },
            {
                Name = "Skeletons",
                ValueType = "Array",
                Value = {
                    {
                        ValueType = "Struct",
                        Value = {
                            { Name = "Skeleton", ValueType = "Object", Value = skeleton_path },
                            { Name = "MirrorDataTable", ValueType = "Object", Value = "" },
                            { Name = "Role", ValueType = "Name", Value = "" },
                        },
                    },
                },
            },
            {
                Name = "Channels",
                ValueType = "Array",
                Value = {
                    {
                        ValueType = "InstancedObject",
                        Value = {
                            Class = require_non_empty_string(
                                schema.TrajectoryChannelClassPath,
                                field_root .. ".TrajectoryChannelClassPath"),
                            Properties = {
                                {
                                    Name = "Weight",
                                    ValueType = "Float",
                                    Value = trajectory_weight,
                                },
                                { Name = "Samples", ValueType = "Array", Value = trajectory_samples },
                            },
                        },
                    },
                    {
                        ValueType = "InstancedObject",
                        Value = {
                            Class = require_non_empty_string(
                                schema.PoseChannelClassPath,
                                field_root .. ".PoseChannelClassPath"),
                            Properties = {
                                {
                                    Name = "Weight",
                                    ValueType = "Float",
                                    Value = pose_weight,
                                },
                                { Name = "SampledBones", ValueType = "Array", Value = sampled_bones },
                                {
                                    Name = "InputQueryPose",
                                    ValueType = "Enum",
                                    Value = require_native_enum(
                                        schema.InputQueryPose,
                                        field_root .. ".InputQueryPose"),
                                },
                                {
                                    Name = "bUseCharacterSpaceVelocities",
                                    ValueType = "Bool",
                                    Value = schema.bUseCharacterSpaceVelocities,
                                },
                                {
                                    Name = "PermutationTimeType",
                                    ValueType = "Enum",
                                    Value = PermutationTimeType.UseSampleTime,
                                },
                            },
                        },
                    },
                },
            },
            {
                Name = "DataPreprocessor",
                ValueType = "Enum",
                Value = schema.DataPreprocessor,
            },
            {
                Name = "NumberOfPermutations",
                ValueType = "Integer",
                Value = schema.NumberOfPermutations,
            },
            {
                Name = "bAddDataPadding",
                ValueType = "Bool",
                Value = schema.bAddDataPadding,
            },
            {
                Name = "bInjectAdditionalDebugChannels",
                ValueType = "Bool",
                Value = schema.bInjectAdditionalDebugChannels,
            },
        },
    }
    return {
        Version = 1,
        TargetObjectPath = target_object_path,
        SkeletonObjectPath = skeleton_path,
        ExpectedCardinality = schema.ExpectedCardinality,
        AssetPatch = asset_patch,
    }
end

---构建正式 Locomotion Schema 的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Skeleton 与全部 Channel 的全量补丁。
function SekiroMotionMatchingConfig.CompileLocomotionSchemaAssetPatch()
    return SekiroMotionMatchingConfig.CompileSchemaIR("Locomotion").AssetPatch
end

---构建正式 Airborne Schema 的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch 保留三维轨迹 Position/Velocity 的空中 Schema 全量补丁。
function SekiroMotionMatchingConfig.CompileAirborneSchemaAssetPatch()
    return SekiroMotionMatchingConfig.CompileSchemaIR("Airborne").AssetPatch
end

---验证并复制数据库动画条目，确保路径唯一且所有引擎枚举、布尔和区间值在修改资产前确定。
---@param values any 待验证的动画条目数组。
---@param field_name string 用于错误信息的稳定字段路径。
---@return SKPoseSearchAnimationAssetSettings[] copied_values 保持声明顺序的独立动画条目数组。
local function copy_database_animation_assets(values, field_name)
    if type(values) ~= "table" or #values == 0 then
        error(field_name .. " must be a non-empty array", 2)
    end

    local copied_values = {}
    local seen_paths = {}
    for index, value in ipairs(values) do
        local value_path = field_name .. "[" .. index .. "]"
        if type(value) ~= "table" then
            error(value_path .. " must be a table", 2)
        end
        local asset_path = require_non_empty_string(
            value.AssetPath,
            value_path .. ".AssetPath")
        if seen_paths[asset_path] == true then
            error(field_name .. " contains duplicate asset " .. asset_path, 2)
        end
        if type(value.bEnabled) ~= "boolean"
            or type(value.bDisableReselection) ~= "boolean" then
            error(value_path .. " boolean settings must be explicit", 2)
        end
        local mirror_option = require_native_enum(
            value.MirrorOption,
            value_path .. ".MirrorOption")
        if supported_mirror_options[mirror_option] ~= true then
            error(value_path .. ".MirrorOption is unsupported: " .. mirror_option, 2)
        end
        local sampling_range_min = require_finite_number(
            value.SamplingRangeMin,
            value_path .. ".SamplingRangeMin")
        local sampling_range_max = require_finite_number(
            value.SamplingRangeMax,
            value_path .. ".SamplingRangeMax")
        if sampling_range_min ~= 0.0
            and sampling_range_max ~= 0.0
            and sampling_range_max < sampling_range_min then
            error(value_path .. ".SamplingRange must not be inverted", 2)
        end

        seen_paths[asset_path] = true
        copied_values[index] = {
            AssetPath = asset_path,
            bEnabled = value.bEnabled,
            bDisableReselection = value.bDisableReselection,
            MirrorOption = mirror_option,
            SamplingRangeMin = sampling_range_min,
            SamplingRangeMax = sampling_range_max,
        }
    end
    return copied_values
end

---把项目动画条目编译为通用 Array<Struct> 反射值；字段名只存在于项目 Lua，插件无需依赖 PoseSearch。
---@param animation_assets SKPoseSearchAnimationAssetSettings[] 已完成语义校验的动画条目。
---@return SKObjectTypedValueIR[] reflected_values 可直接作为 DatabaseAnimationAssets 数组 Value 的元素。
local function compile_database_animation_asset_values(animation_assets)
    local reflected_values = {}
    for index, animation_asset in ipairs(animation_assets) do
        reflected_values[index] = {
            ValueType = "Struct",
            Value = {
                {
                    Name = "AnimAsset",
                    ValueType = "Object",
                    Value = animation_asset.AssetPath,
                },
                {
                    Name = "bEnabled",
                    ValueType = "Bool",
                    Value = animation_asset.bEnabled,
                },
                {
                    Name = "bDisableReselection",
                    ValueType = "Bool",
                    Value = animation_asset.bDisableReselection,
                },
                {
                    Name = "MirrorOption",
                    ValueType = "Enum",
                    Value = animation_asset.MirrorOption,
                },
                {
                    Name = "BranchInId",
                    ValueType = "Integer",
                    Value = 0,
                },
                {
                    Name = "bUseSingleSample",
                    ValueType = "Bool",
                    Value = false,
                },
                {
                    Name = "bUseGridForSampling",
                    ValueType = "Bool",
                    Value = false,
                },
                {
                    Name = "NumberOfHorizontalSamples",
                    ValueType = "Integer",
                    Value = 9,
                },
                {
                    Name = "NumberOfVerticalSamples",
                    ValueType = "Integer",
                    Value = 2,
                },
                {
                    Name = "BlendParamX",
                    ValueType = "Float",
                    Value = 0.0,
                },
                {
                    Name = "BlendParamY",
                    ValueType = "Float",
                    Value = 0.0,
                },
                {
                    Name = "SamplingRange",
                    ValueType = "Struct",
                    Value = {
                        {
                            Name = "Min",
                            ValueType = "Float",
                            Value = animation_asset.SamplingRangeMin,
                        },
                        {
                            Name = "Max",
                            ValueType = "Float",
                            Value = animation_asset.SamplingRangeMax,
                        },
                    },
                },
            },
        }
    end
    return reflected_values
end

---构建指定正式阶段数据库的版本化 Pose Search IR。
---每次调用都返回独立表；该函数只编排纯值，不加载、创建或保存任何 UObject。
---@param database_name string Databases 与 DatabaseOrder 中登记的稳定数据库名称。
---@return SKPoseSearchDatabaseIR database_ir 指定地面或空中 Locomotion 阶段数据库的完整生成描述。
function SekiroMotionMatchingConfig.CompileDatabaseIR(database_name)
    validate_authoring_contracts()
    local validated_name = require_non_empty_string(database_name, "DatabaseName")
    local database = SekiroMotionMatchingConfig.Databases[validated_name]
    if type(database) ~= "table" then
        error("Databases." .. validated_name .. " is not registered", 2)
    end
    local field_root = "Databases." .. validated_name
    local asset_path = require_non_empty_string(
        database.AssetPath,
        field_root .. ".AssetPath")
    local schema_path = require_non_empty_string(
        database.Schema,
        field_root .. ".Schema")
    local normalization_set_path = require_non_empty_string(
        database.NormalizationSet,
        field_root .. ".NormalizationSet")
    local registered_normalization_set = false
    for _, normalization_set in pairs(SekiroMotionMatchingConfig.NormalizationSets) do
        if normalization_set.AssetPath == normalization_set_path then
            registered_normalization_set = true
            break
        end
    end
    if not registered_normalization_set then
        error(field_root .. ".NormalizationSet is not registered", 2)
    end
    local schema_contract_name = require_non_empty_string(
        SekiroMotionMatchingConfig.PhaseSchemaAssignments[validated_name],
        "PhaseSchemaAssignments." .. validated_name)
    local schema_contract = SekiroMotionMatchingConfig.SchemaContracts[schema_contract_name]
    if type(schema_contract) ~= "table"
        or schema_contract.AssetPath ~= schema_path then
        error(field_root .. ".Schema does not match its phase Schema contract", 2)
    end
    local search_mode = require_native_enum(
        database.PoseSearchMode,
        field_root .. ".PoseSearchMode")
    if supported_search_modes[search_mode] ~= true then
        error(field_root .. ".PoseSearchMode is unsupported: " .. search_mode, 2)
    end
    local continuing_pose_cost_bias = require_finite_number(
        database.ContinuingPoseCostBias,
        field_root .. ".ContinuingPoseCostBias")
    local base_cost_bias = require_finite_number(
        database.BaseCostBias,
        field_root .. ".BaseCostBias")
    local looping_cost_bias = require_finite_number(
        database.LoopingCostBias,
        field_root .. ".LoopingCostBias")
    local exclude_from_database_min = require_finite_number(
        database.ExcludeFromDatabaseMin,
        field_root .. ".ExcludeFromDatabaseMin")
    local exclude_from_database_max = require_finite_number(
        database.ExcludeFromDatabaseMax,
        field_root .. ".ExcludeFromDatabaseMax")
    local animation_assets = copy_database_animation_assets(
        database.AnimationAssets,
        field_root .. ".AnimationAssets")
    local movement_policy = database.MovementPolicy or "AnimationRootMotion"
    if supported_movement_policies[movement_policy] ~= true then
        error(field_root .. ".MovementPolicy is unsupported: " .. tostring(movement_policy), 2)
    end
    local eligibility = {
        Phases = copy_required_enum_array(
            database.Eligibility.Phases,
            field_root .. ".Eligibility.Phases"),
        Gaits = copy_required_enum_array(
            database.Eligibility.Gaits,
            field_root .. ".Eligibility.Gaits"),
        Modes = copy_required_enum_array(
            database.Eligibility.Modes,
            field_root .. ".Eligibility.Modes"),
        Stances = copy_required_enum_array(
            database.Eligibility.Stances,
            field_root .. ".Eligibility.Stances"),
    }
    if movement_policy == "InheritedAirborneMomentum" then
        if schema_contract_name ~= "Airborne" then
            error(field_root .. ".MovementPolicy requires the Airborne Schema contract", 2)
        end
        for index, phase in ipairs(eligibility.Phases) do
            if pose_only_airborne_phases[phase] ~= true then
                error(
                    field_root .. ".Eligibility.Phases[" .. index
                        .. "] is invalid for InheritedAirborneMomentum: " .. phase,
                    2)
            end
        end
    end

    return {
        Version = 5,
        Database = {
            AssetPath = asset_path,
            Schema = schema_path,
            NormalizationSet = normalization_set_path,
            MovementPolicy = movement_policy,
            PoseSearchMode = search_mode,
            ContinuingPoseCostBias = continuing_pose_cost_bias,
            BaseCostBias = base_cost_bias,
            LoopingCostBias = looping_cost_bias,
            ExcludeFromDatabaseMin = exclude_from_database_min,
            ExcludeFromDatabaseMax = exclude_from_database_max,
            AnimationAssets = animation_assets,
            Eligibility = eligibility,
        },
        AssetPatch = {
            Version = 1,
            AssetType = "ObjectPropertyPatch",
            TargetObjectPath = asset_path,
            ExpectedClassPath = "/Script/PoseSearch.PoseSearchDatabase",
            Properties = {
                {
                    Name = "Schema",
                    ValueType = "Object",
                    Value = schema_path,
                },
                {
                    Name = "NormalizationSet",
                    ValueType = "Object",
                    Value = normalization_set_path,
                },
                {
                    Name = "PoseSearchMode",
                    ValueType = "Enum",
                    Value = search_mode,
                },
                {
                    Name = "ContinuingPoseCostBias",
                    ValueType = "Float",
                    Value = continuing_pose_cost_bias,
                },
                {
                    Name = "BaseCostBias",
                    ValueType = "Float",
                    Value = base_cost_bias,
                },
                {
                    Name = "LoopingCostBias",
                    ValueType = "Float",
                    Value = looping_cost_bias,
                },
                {
                    Name = "ExcludeFromDatabaseParameters",
                    ValueType = "Struct",
                    Value = {
                        {
                            Name = "Min",
                            ValueType = "Float",
                            Value = exclude_from_database_min,
                        },
                        {
                            Name = "Max",
                            ValueType = "Float",
                            Value = exclude_from_database_max,
                        },
                    },
                },
                {
                    Name = "DatabaseAnimationAssets",
                    ValueType = "Array",
                    Value = compile_database_animation_asset_values(animation_assets),
                },
            },
        },
    }
end

---构建 Stationary 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Idle 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileStationaryDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("Stationary").AssetPatch
end

---构建 Standing Walk Start 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Walk Start 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileWalkStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("WalkStarts").AssetPatch
end

---构建 Standing Run Start 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Run Start 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileRunStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("RunStarts").AssetPatch
end

---构建 Standing Sprint Start 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Sprint Start 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileSprintStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("SprintStarts").AssetPatch
end

---构建 Standing Walk Loop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Walk Loop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileWalkLoopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("WalkLoops").AssetPatch
end

---构建 Standing Run Loop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Run Loop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileRunLoopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("RunLoops").AssetPatch
end

---构建 Standing Sprint Loop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Sprint Loop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileSprintLoopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("SprintLoops").AssetPatch
end

---构建 Standing Walk Stop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Walk Stop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileWalkStopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("WalkStops").AssetPatch
end

---构建 Standing Run Stop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Run Stop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileRunStopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("RunStops").AssetPatch
end

---构建 Standing Sprint Stop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Standing Sprint Stop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileSprintStopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("SprintStops").AssetPatch
end

---构建 Standing Sprint 180 度 Pivot 数据库的通用反射补丁。
---@return SKObjectPropertyPatchIR asset_patch 两个带真实根旋转的 Sprint Pivot 数据库补丁。
function SekiroMotionMatchingConfig.CompileSprintPivotsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("SprintPivots").AssetPatch
end

---构建 CrouchStationary 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Idle 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchStationaryDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchStationary").AssetPatch
end

---构建 Crouching Walk Start 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Walk Start 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchWalkStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchWalkStarts").AssetPatch
end

---构建 Crouching Run Start 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Run Start 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchRunStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchRunStarts").AssetPatch
end

---构建 Crouching Walk Loop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Walk Loop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchWalkLoopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchWalkLoops").AssetPatch
end

---构建 Crouching Run Loop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Run Loop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchRunLoopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchRunLoops").AssetPatch
end

---构建 Crouching Walk Stop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Walk Stop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchWalkStopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchWalkStops").AssetPatch
end

---构建 Crouching Run Stop 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch Crouching Run Stop 正式数据库补丁。
function SekiroMotionMatchingConfig.CompileCrouchRunStopsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("CrouchRunStops").AssetPatch
end

---构建 JumpStart 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch 使用 Airborne Schema 的八方向 JumpStart 数据库补丁。
function SekiroMotionMatchingConfig.CompileJumpStartsDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("JumpStarts").AssetPatch
end

---构建共享空中姿势数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch 使用 Airborne Schema、但不提供 RootMotion 的空中姿势数据库补丁。
function SekiroMotionMatchingConfig.CompileInAirPoseOnlyDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("InAirPoseOnly").AssetPatch
end

---构建 Landing 数据库的通用反射补丁，供无参数插件入口调用。
---@return SKObjectPropertyPatchIR asset_patch 使用 Airborne Schema 的八方向 Landing 数据库补丁。
function SekiroMotionMatchingConfig.CompileLandingDatabaseAssetPatch()
    return SekiroMotionMatchingConfig.CompileDatabaseIR("Landing").AssetPatch
end

---构建指定跨阶段 Normalization Set 的版本化纯值 IR。
---只纳入已经建立正式数据库配置的阶段；延后阶段必须先完成数据契约，才可加入联合统计。
---@param normalization_set_name string NormalizationSets 中登记的稳定配置名称。
---@return SKPoseSearchNormalizationSetIR normalization_set_ir Schema 一致且可由通用反射入口消费的资产描述。
function SekiroMotionMatchingConfig.CompileNormalizationSetIR(normalization_set_name)
    validate_authoring_contracts()
    local validated_name = require_non_empty_string(
        normalization_set_name,
        "NormalizationSetName")
    local settings = SekiroMotionMatchingConfig.NormalizationSets[validated_name]
    if type(settings) ~= "table" then
        error("NormalizationSets." .. validated_name .. " is not registered", 2)
    end

    local field_root = "NormalizationSets." .. validated_name
    local target_object_path = require_non_empty_string(
        settings.AssetPath,
        field_root .. ".AssetPath")
    local schema_contract_name = require_non_empty_string(
        settings.SchemaContract,
        field_root .. ".SchemaContract")
    local schema_contract = SekiroMotionMatchingConfig.SchemaContracts[schema_contract_name]
    if type(schema_contract) ~= "table" then
        error(field_root .. ".SchemaContract is not registered", 2)
    end
    local schema_path = require_non_empty_string(
        schema_contract.AssetPath,
        "SchemaContracts." .. schema_contract_name .. ".AssetPath")
    local database_names = copy_required_string_array(
        settings.Databases,
        field_root .. ".Databases")
    local deferred_families = copy_optional_string_array(
        settings.DeferredDatabaseFamilies,
        field_root .. ".DeferredDatabaseFamilies")

    local database_object_paths = {}
    local database_values = {}
    for index, database_name in ipairs(database_names) do
        local database_ir = SekiroMotionMatchingConfig.CompileDatabaseIR(database_name)
        if database_ir.Database.Schema ~= schema_path then
            error(field_root .. ".Databases contains a different Schema: " .. database_name, 2)
        end
        if database_ir.Database.NormalizationSet ~= target_object_path then
            error(field_root .. ".Databases contains a different NormalizationSet: " .. database_name, 2)
        end
        database_object_paths[index] = database_ir.Database.AssetPath
        database_values[index] = {
            ValueType = "Object",
            Value = database_ir.Database.AssetPath,
        }
    end
    for _, deferred_family in ipairs(deferred_families) do
        if SekiroMotionMatchingConfig.PhaseSchemaAssignments[deferred_family]
            ~= schema_contract_name then
            error(field_root .. ".DeferredDatabaseFamilies has a different Schema: " .. deferred_family, 2)
        end
        if SekiroMotionMatchingConfig.Databases[deferred_family] ~= nil then
            error(field_root .. ".DeferredDatabaseFamilies is already active: " .. deferred_family, 2)
        end
    end

    return {
        Version = 1,
        TargetObjectPath = target_object_path,
        SchemaContract = schema_contract_name,
        DatabaseObjectPaths = database_object_paths,
        DeferredDatabaseFamilies = deferred_families,
        AssetPatch = {
            Version = 1,
            AssetType = "ObjectPropertyPatch",
            TargetObjectPath = target_object_path,
            ExpectedClassPath = "/Script/PoseSearch.PoseSearchNormalizationSet",
            Properties = {
                {
                    Name = "Databases",
                    ValueType = "Array",
                    Value = database_values,
                },
            },
        },
    }
end

---构建正式 Locomotion Normalization Set 的通用反射补丁，供后续资产生成脚本调用。
---@return SKObjectPropertyPatchIR asset_patch 当前四个阶段数据库的联合统计成员补丁。
function SekiroMotionMatchingConfig.CompileLocomotionNormalizationSetAssetPatch()
    return SekiroMotionMatchingConfig.CompileNormalizationSetIR("Locomotion").AssetPatch
end

---构建正式 Airborne Normalization Set 的通用反射补丁，供后续资产生成脚本调用。
---@return SKObjectPropertyPatchIR asset_patch 包含 JumpStart、共享空中姿势与 Landing 的空中归一化补丁。
function SekiroMotionMatchingConfig.CompileAirborneNormalizationSetAssetPatch()
    return SekiroMotionMatchingConfig.CompileNormalizationSetIR("Airborne").AssetPatch
end

---构建项目侧 Locomotion Chooser 的版本化纯值 IR。
---规则只决定合法数据库数组；具体 Pose、播放时间和 Cost 始终由 Motion Matching 搜索决定。
---@return SKLocomotionChooserIR chooser_ir 当前正式 Chooser Table 的语义门禁描述。
function SekiroMotionMatchingConfig.CompileChooserIR()
    validate_authoring_contracts()
    local chooser = SekiroMotionMatchingConfig.Chooser
    local empty_result_policy = require_non_empty_string(
        chooser.EmptyResultPolicy,
        "Chooser.EmptyResultPolicy")
    if empty_result_policy ~= "Error" then
        error("Chooser.EmptyResultPolicy must be Error", 2)
    end

    local rules = {}
    for index, database_name in ipairs(SekiroMotionMatchingConfig.DatabaseOrder) do
        local database_ir = SekiroMotionMatchingConfig.CompileDatabaseIR(database_name)
        local eligibility = database_ir.Database.Eligibility
        local rule_path = "Chooser.Rules[" .. index .. "]"
        rules[index] = {
            Phases = copy_required_enum_array(
                eligibility.Phases,
                rule_path .. ".Phases"),
            Gaits = copy_required_enum_array(
                eligibility.Gaits,
                rule_path .. ".Gaits"),
            Modes = copy_required_enum_array(
                eligibility.Modes,
                rule_path .. ".Modes"),
            Stances = copy_required_enum_array(
                eligibility.Stances,
                rule_path .. ".Stances"),
            Databases = {
                require_non_empty_string(
                    database_ir.Database.AssetPath,
                    rule_path .. ".Databases[1]"),
            },
        }
    end

    return {
        Version = 2,
        TargetObjectPath = require_non_empty_string(
            chooser.AssetPath,
            "Chooser.AssetPath"),
        ResultClassPath = "/Script/PoseSearch.PoseSearchDatabase",
        bResultIsArray = true,
        EmptyResultPolicy = empty_result_policy,
        Rules = rules,
    }
end

---把项目侧 Chooser 语义规则编译为项目无关的列、行和反射绑定 IR。
---一个业务规则包含多个数据库时会展开为多行，使 EvaluateChooserMulti 返回全部合法结果；
---插件只解析通用 EnumAny 列，不认识 Phase、Gait、Mode、Stance 或 Pose Search。
---@return SKGenericChooserAssetIR chooser_asset_ir 可由通用 Chooser 资产物化器消费的完整纯值树。
function SekiroMotionMatchingConfig.CompileChooserAssetIR()
    local chooser_ir = SekiroMotionMatchingConfig.CompileChooserIR()
    local rows = {}
    for _, rule in ipairs(chooser_ir.Rules) do
        for _, database_path in ipairs(rule.Databases) do
            table.insert(rows, {
                ResultObjectPath = database_path,
                Conditions = {
                    {
                        ColumnId = "Phase",
                        Values = copy_required_enum_array(
                            rule.Phases,
                            "ChooserAsset.Rows.Phase"),
                    },
                    {
                        ColumnId = "Gait",
                        Values = copy_required_enum_array(
                            rule.Gaits,
                            "ChooserAsset.Rows.Gait"),
                    },
                    {
                        ColumnId = "Mode",
                        Values = copy_required_enum_array(
                            rule.Modes,
                            "ChooserAsset.Rows.Mode"),
                    },
                    {
                        ColumnId = "Stance",
                        Values = copy_required_enum_array(
                            rule.Stances,
                            "ChooserAsset.Rows.Stance"),
                    },
                },
            })
        end
    end

    return {
        Version = 1,
        AssetType = "ChooserTable",
        TargetObjectPath = chooser_ir.TargetObjectPath,
        ContextClassPath = require_non_empty_string(
            SekiroMotionMatchingConfig.Chooser.ContextClassPath,
            "Chooser.ContextClassPath"),
        ResultClassPath = chooser_ir.ResultClassPath,
        ResultMode = "ObjectArray",
        Columns = {
            {
                Id = "Phase",
                Type = "EnumAny",
                BindingPath = {
                    "MotionMatchingSearchRequest",
                    "RequestedPhase",
                },
            },
            {
                Id = "Gait",
                Type = "EnumAny",
                BindingPath = {
                    "MotionMatchingSearchRequest",
                    "RequestedGait",
                },
            },
            {
                Id = "Mode",
                Type = "EnumAny",
                BindingPath = {
                    "MotionMatchingSearchRequest",
                    "RequestedMode",
                },
            },
            {
                Id = "Stance",
                Type = "EnumAny",
                BindingPath = {
                    "MotionMatchingSearchRequest",
                    "RequestedStance",
                },
            },
        },
        Rows = rows,
    }
end

return SekiroMotionMatchingConfig
