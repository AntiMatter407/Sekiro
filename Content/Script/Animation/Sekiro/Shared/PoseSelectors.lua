-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Sekiro Locomotion 状态图复用的原生 Pose 选择器构建函数。
-- 本模块只在编译期创建明确的 SequencePlayer 和 BlendList 节点，不在运行时拼接动画名或动态播放资产。

local EditorNodeClass = require("Animation.Compiler.NodeClasses.EditorNodeClass")
local Tuning = require("Animation.Sekiro.Shared.Tuning")
local DirectionalPose = require("Animation.Sekiro.Shared.DirectionalPose")

local DirectionEnum = "/Script/Sekiro.ESKLocomotionDirection"
local GaitEnum = "/Script/Sekiro.ESKAnimGait"

---@class SekiroFourWayAssets
---@field Forward string 前向动画资产对象路径。
---@field Back string 后向动画资产对象路径。
---@field Left string 左向动画资产对象路径。
---@field Right string 右向动画资产对象路径。

---@class SekiroWalkRunAssets
---@field Walk SekiroFourWayAssets Walk 四方向资产。
---@field Run SekiroFourWayAssets Run 四方向资产。

---@class SekiroWalkRunSprintAssets
---@field Walk SekiroFourWayAssets Walk 四方向资产。
---@field Run SekiroFourWayAssets Run 四方向资产。
---@field Sprint string|SekiroFourWayAssets Sprint 单方向循环资产或四方向一次性资产。

---@class SekiroCardinalAlignmentConfig
---@field ForwardResidualVariable string Forward 分支相对真实移动方向的残差变量。
---@field BackResidualVariable string Back 分支相对真实移动方向的残差变量。
---@field LeftResidualVariable string Left 分支相对真实移动方向的残差变量。
---@field RightResidualVariable string Right 分支相对真实移动方向的残差变量。
---@field AlphaVariable string 四条方向分支共用的方向对齐权重变量。
---@field RotationInterpSpeed number|nil 分支内部角度插值速度；方向 Pose 自身混合时应使用 0。

---@class SekiroPoseSelectors
local PoseSelectors = {}

---在单条四向素材分支进入选择器前应用该分支自己的方向残差。
---只有 Cycle 这类运行中会改变方向枚举的选择器传入配置；锁存的 Start/Stop 保持选择器后的单节点对齐。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 方向分支对齐节点的语义名称。
---@param source_pose LuaAnimNode 当前方向的 SequencePlayer。
---@param residual_variable string 当前素材主轴对应的残差变量名。
---@param alignment SekiroCardinalAlignmentConfig|nil 分支对齐配置；nil 时保持原始姿势。
---@return LuaAnimNode pose_node 已按当前分支残差对齐的姿势，或未改变的原始姿势。
local function align_cardinal_branch(Graph, name, source_pose, residual_variable, alignment)
    if alignment == nil then
        return source_pose
    end

    return DirectionalPose.Align(
        Graph,
        name,
        source_pose,
        residual_variable,
        alignment.AlphaVariable,
        alignment.RotationInterpSpeed)
end

---创建单个 SequencePlayer，并统一设置循环、播放倍率和可选同步组。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 节点语义名称。
---@param sequence string 动画序列资产对象路径。
---@param loop_animation boolean 是否循环播放。
---@param sync_group string|nil 原生同步组名称；一次性动画传 nil。
---@return LuaAnimNode player 已声明的 SequencePlayer 节点。
function PoseSelectors.Sequence(Graph, name, sequence, loop_animation, sync_group)
    local properties = {
        Sequence = sequence,
        bLoopAnimation = loop_animation,
        PlayRate = 1.0,
    }
    if sync_group ~= nil then
        properties.GroupName = sync_group
        properties.GroupRole = UE.EAnimGroupRole.CanBeLeader
        properties.GroupMethod = UE.EAnimSyncMethod.SyncGroup
    end
    return Graph:Node(
        name,
        EditorNodeClass.SequencePlayer,
        properties,
        "SequencePlayer")
end

---创建四个明确 SequencePlayer，并由 ESKLocomotionDirection 原生枚举选择输出姿势。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 选择器和子节点使用的语义前缀。
---@param assets SekiroFourWayAssets 四方向明确资产引用。
---@param direction_variable string 已声明的方向生成变量名。
---@param loop_animation boolean 是否循环四个 SequencePlayer。
---@param sync_group string|nil 循环动画使用的同步组名称。
---@param alignment SekiroCardinalAlignmentConfig|nil 可选的混合前逐分支方向对齐配置。
---@return LuaAnimNode selector 四方向原生 BlendListByEnum 节点。
function PoseSelectors.Cardinal(
    Graph,
    name,
    assets,
    direction_variable,
    loop_animation,
    sync_group,
    alignment)
    local forward = PoseSelectors.Sequence(Graph, name .. "Forward", assets.Forward, loop_animation, sync_group)
    local back = PoseSelectors.Sequence(Graph, name .. "Back", assets.Back, loop_animation, sync_group)
    local left = PoseSelectors.Sequence(Graph, name .. "Left", assets.Left, loop_animation, sync_group)
    local right = PoseSelectors.Sequence(Graph, name .. "Right", assets.Right, loop_animation, sync_group)

    if alignment ~= nil then
        forward = align_cardinal_branch(
            Graph,
            name .. "ForwardAlignment",
            forward,
            alignment.ForwardResidualVariable,
            alignment)
        back = align_cardinal_branch(
            Graph,
            name .. "BackAlignment",
            back,
            alignment.BackResidualVariable,
            alignment)
        left = align_cardinal_branch(
            Graph,
            name .. "LeftAlignment",
            left,
            alignment.LeftResidualVariable,
            alignment)
        right = align_cardinal_branch(
            Graph,
            name .. "RightAlignment",
            right,
            alignment.RightResidualVariable,
            alignment)
    end

    local direction = Graph:Property(name .. "Direction", direction_variable)
    local selector = Graph:Node(
        name .. "Selector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = DirectionEnum,
            EnumEntries = "Fwd|Bwd|L|R",
            BlendTime = Tuning.DirectionBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(forward.Pose)
    selector.Pose0:Connect(forward.Pose)
    selector.Pose1:Connect(back.Pose)
    selector.Pose2:Connect(left.Pose)
    selector.Pose3:Connect(right.Pose)
    selector.ActiveValue:Connect(direction.Value)
    return selector
end

---创建 Walk/Run 两套四方向节点，并由 ESKAnimGait 原生枚举选择最终姿势。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 节点语义前缀。
---@param assets SekiroWalkRunAssets Walk 与 Run 的明确四方向资产。
---@param direction_variable string 方向生成变量名。
---@param gait_variable string 步态生成变量名。
---@param loop_animation boolean 是否循环播放；Cycle 为 true，Start/Stop 为 false。
---@param sync_group string|nil Cycle 使用的原生同步组名称。
---@param alignment SekiroCardinalAlignmentConfig|nil 可选的混合前逐分支方向对齐配置。
---@return LuaAnimNode selector Walk/Run 原生枚举选择节点。
function PoseSelectors.WalkRun(
    Graph,
    name,
    assets,
    direction_variable,
    gait_variable,
    loop_animation,
    sync_group,
    alignment)
    local walk = PoseSelectors.Cardinal(
        Graph, name .. "Walk", assets.Walk, direction_variable, loop_animation, sync_group, alignment)
    local run = PoseSelectors.Cardinal(
        Graph, name .. "Run", assets.Run, direction_variable, loop_animation, sync_group, alignment)
    local gait = Graph:Property(name .. "Gait", gait_variable)
    local selector = Graph:Node(
        name .. "GaitSelector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = GaitEnum,
            EnumEntries = "Walk|Run",
            BlendTime = Tuning.GaitBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(walk.Pose)
    selector.Pose0:Connect(walk.Pose)
    selector.Pose1:Connect(run.Pose)
    selector.ActiveValue:Connect(gait.Value)
    return selector
end

---创建只播放左转或右转的枚举选择器；Forward/Back 输入稳定回退到右转，不实例化无关 Turn 资产。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 选择器和子节点使用的语义前缀。
---@param left_sequence string 左转动画序列资产对象路径。
---@param right_sequence string 右转动画序列资产对象路径。
---@param direction_variable string 已锁存的左右方向生成变量名。
---@return LuaAnimNode selector 左右转原生枚举选择节点。
function PoseSelectors.LeftRight(Graph, name, left_sequence, right_sequence, direction_variable)
    local left = PoseSelectors.Sequence(Graph, name .. "Left", left_sequence, false, nil)
    local right = PoseSelectors.Sequence(Graph, name .. "Right", right_sequence, false, nil)
    local direction = Graph:Property(name .. "Direction", direction_variable)
    local selector = Graph:Node(
        name .. "Selector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = DirectionEnum,
            EnumEntries = "Fwd|Bwd|L|R",
            BlendTime = Tuning.TurnBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(right.Pose)
    selector.Pose0:Connect(right.Pose)
    selector.Pose1:Connect(right.Pose)
    selector.Pose2:Connect(left.Pose)
    selector.Pose3:Connect(right.Pose)
    selector.ActiveValue:Connect(direction.Value)
    return selector
end

---创建 Walk/Run/Sprint 三套姿势，并由 ESKAnimGait 原生枚举在同一运动阶段内切换。
---Sprint Cycle 可以只提供前向序列；Start/Stop 则提供四方向资产，避免为了步态变化重进子状态机。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 选择器和子节点使用的语义前缀。
---@param assets SekiroWalkRunSprintAssets Walk、Run 与 Sprint 的明确资产引用。
---@param direction_variable string 方向生成变量名；只提供单向 Sprint 时不会消费该变量。
---@param gait_variable string 步态生成变量名，允许 Walk、Run 与 Sprint。
---@param loop_animation boolean 是否循环播放各个 SequencePlayer。
---@param sync_group string|nil 循环动画使用的原生同步组名称。
---@param alignment SekiroCardinalAlignmentConfig|nil 可选的混合前逐分支方向对齐配置；单向 Sprint 不消费该配置。
---@return LuaAnimNode selector Walk/Run/Sprint 原生枚举选择节点。
function PoseSelectors.WalkRunSprint(
    Graph,
    name,
    assets,
    direction_variable,
    gait_variable,
    loop_animation,
    sync_group,
    alignment)
    local walk = PoseSelectors.Cardinal(
        Graph, name .. "Walk", assets.Walk, direction_variable, loop_animation, sync_group, alignment)
    local run = PoseSelectors.Cardinal(
        Graph, name .. "Run", assets.Run, direction_variable, loop_animation, sync_group, alignment)
    local sprint
    if type(assets.Sprint) == "string" then
        sprint = PoseSelectors.Sequence(
            Graph, name .. "Sprint", assets.Sprint, loop_animation, sync_group)
    else
        sprint = PoseSelectors.Cardinal(
            Graph, name .. "Sprint", assets.Sprint, direction_variable, loop_animation, sync_group, alignment)
    end

    local gait = Graph:Property(name .. "Gait", gait_variable)
    local selector = Graph:Node(
        name .. "GaitSelector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = GaitEnum,
            EnumEntries = "Walk|Run|Sprint",
            BlendTime = Tuning.GaitBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(run.Pose)
    selector.Pose0:Connect(walk.Pose)
    selector.Pose1:Connect(run.Pose)
    selector.Pose2:Connect(sprint.Pose)
    selector.ActiveValue:Connect(gait.Value)
    return selector
end

---创建八个明确 SequencePlayer，并按 ESKLocomotionDirection 的完整八方向枚举选择姿势。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 节点语义前缀。
---@param assets table<string, string> 包含 Forward、ForwardLeft、Left、BackLeft、Back、BackRight、Right、ForwardRight 的资产表。
---@param direction_variable string 已声明的八方向生成变量名。
---@param loop_animation boolean 是否循环播放。
---@return LuaAnimNode selector 八方向原生 BlendListByEnum 节点。
function PoseSelectors.Octant(Graph, name, assets, direction_variable, loop_animation)
    local players = {
        PoseSelectors.Sequence(Graph, name .. "Forward", assets.Forward, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "ForwardLeft", assets.ForwardLeft, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "Left", assets.Left, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "BackLeft", assets.BackLeft, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "Back", assets.Back, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "BackRight", assets.BackRight, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "Right", assets.Right, loop_animation, nil),
        PoseSelectors.Sequence(Graph, name .. "ForwardRight", assets.ForwardRight, loop_animation, nil),
    }
    local direction = Graph:Property(name .. "Direction", direction_variable)
    local selector = Graph:Node(
        name .. "Selector",
        EditorNodeClass.BlendListByEnum,
        {
            EnumType = DirectionEnum,
            EnumEntries = "Fwd|Fwd_L|L|Bwd_L|Bwd|Bwd_R|R|Fwd_R",
            BlendTime = Tuning.JumpBlendDuration,
        },
        "BlendListByEnum")
    selector.DefaultPose:Connect(players[1].Pose)
    for index, player in ipairs(players) do
        selector["Pose" .. tostring(index - 1)]:Connect(player.Pose)
    end
    selector.ActiveValue:Connect(direction.Value)
    return selector
end

return PoseSelectors
