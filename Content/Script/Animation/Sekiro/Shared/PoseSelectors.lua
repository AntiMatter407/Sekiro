-- Sekiro Locomotion 状态图复用的原生 Pose 选择器构建函数。
-- 本模块只在编译期创建明确的 SequencePlayer 和 BlendList 节点，不在运行时拼接动画名或动态播放资产。

local Tuning = require("Animation.Sekiro.Shared.Tuning")

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

---@class SekiroPoseSelectors
local PoseSelectors = {}

---创建单个 SequencePlayer，并统一设置循环、播放倍率和可选同步组。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 节点语义名称。
---@param sequence string 动画序列资产对象路径。
---@param loop_animation boolean 是否循环播放。
---@param sync_group string|nil 原生同步组名称；一次性动画传 nil。
---@return LuaSequencePlayerNode player 已声明的 SequencePlayer 节点。
function PoseSelectors.Sequence(Graph, name, sequence, loop_animation, sync_group)
    local player = Graph:SequencePlayer(name)
    player.Sequence = sequence
    player.bLoopAnimation = loop_animation
    player.PlayRate = 1.0
    if sync_group ~= nil then
        player.GroupName = sync_group
        player.GroupRole = "CanBeLeader"
        player.GroupMethod = "SyncGroup"
    end
    return player
end

---创建四个明确 SequencePlayer，并由 ESKLocomotionDirection 原生枚举选择输出姿势。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 选择器和子节点使用的语义前缀。
---@param assets SekiroFourWayAssets 四方向明确资产引用。
---@param direction_variable string 已声明的方向生成变量名。
---@param loop_animation boolean 是否循环四个 SequencePlayer。
---@param sync_group string|nil 循环动画使用的同步组名称。
---@return LuaBlendListByEnumNode selector 四方向原生 BlendListByEnum 节点。
function PoseSelectors.Cardinal(Graph, name, assets, direction_variable, loop_animation, sync_group)
    local forward = PoseSelectors.Sequence(Graph, name .. "Forward", assets.Forward, loop_animation, sync_group)
    local back = PoseSelectors.Sequence(Graph, name .. "Back", assets.Back, loop_animation, sync_group)
    local left = PoseSelectors.Sequence(Graph, name .. "Left", assets.Left, loop_animation, sync_group)
    local right = PoseSelectors.Sequence(Graph, name .. "Right", assets.Right, loop_animation, sync_group)

    local direction = Graph:Property(name .. "Direction", direction_variable)
    local selector = Graph:BlendListByEnum(name .. "Selector", DirectionEnum, { "Fwd", "Bwd", "L", "R" })
    selector.BlendTime = Tuning.CycleBlendDuration
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
---@return LuaBlendListByEnumNode selector Walk/Run 原生枚举选择节点。
function PoseSelectors.WalkRun(Graph, name, assets, direction_variable, gait_variable, loop_animation, sync_group)
    local walk = PoseSelectors.Cardinal(
        Graph, name .. "Walk", assets.Walk, direction_variable, loop_animation, sync_group)
    local run = PoseSelectors.Cardinal(
        Graph, name .. "Run", assets.Run, direction_variable, loop_animation, sync_group)
    local gait = Graph:Property(name .. "Gait", gait_variable)
    local selector = Graph:BlendListByEnum(name .. "GaitSelector", GaitEnum, { "Walk", "Run" })
    selector.BlendTime = Tuning.CycleBlendDuration
    selector.DefaultPose:Connect(walk.Pose)
    selector.Pose0:Connect(walk.Pose)
    selector.Pose1:Connect(run.Pose)
    selector.ActiveValue:Connect(gait.Value)
    return selector
end

---创建八个明确 SequencePlayer，并按 ESKLocomotionDirection 的完整八方向枚举选择姿势。
---@param Graph LuaAnimGraph|LuaAnimStateGraph 当前状态的原生 Pose Graph。
---@param name string 节点语义前缀。
---@param assets table<string, string> 包含 Forward、ForwardLeft、Left、BackLeft、Back、BackRight、Right、ForwardRight 的资产表。
---@param direction_variable string 已声明的八方向生成变量名。
---@param loop_animation boolean 是否循环播放。
---@return LuaBlendListByEnumNode selector 八方向原生 BlendListByEnum 节点。
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
    local selector = Graph:BlendListByEnum(name .. "Selector", DirectionEnum, {
        "Fwd", "Fwd_L", "L", "Bwd_L", "Bwd", "Bwd_R", "R", "Fwd_R",
    })
    selector.BlendTime = Tuning.JumpBlendDuration
    selector.DefaultPose:Connect(players[1].Pose)
    for index, player in ipairs(players) do
        selector["Pose" .. tostring(index - 1)]:Connect(player.Pose)
    end
    selector.ActiveValue:Connect(direction.Value)
    return selector
end

return PoseSelectors
