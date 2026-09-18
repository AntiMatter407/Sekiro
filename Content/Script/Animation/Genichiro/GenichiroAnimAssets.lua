-- Lua 类型：纯 Lua 资源表。本文件集中声明弦一郎专用基础移动、转身与战斗 Slot 资产契约。
-- 基础移动编号来自 c9997 行为图的 IdleBattle、MoveBattle 和 TurnBattle 状态，不再按相邻编号推测语义。

local AnimationRoot = "/Game/Characters/Genichiro/Animations/"

---按 UE 可播放动画编号生成完整 AnimSequence 对象路径。
---@param animation_id number 已导入的弦一郎动画编号。
---@return string asset_path UE AnimSequence 对象路径。
local function animation_path(animation_id)
    local asset_name = string.format(
        "Anim_Genichiro_a000_%06d",
        animation_id)
    return string.format("%s%s.%s", AnimationRoot, asset_name, asset_name)
end

---@class GenichiroLocomotionResolution
---@field SemanticName string UE 动画状态机使用的稳定语义名。
---@field BehaviorStateName string c9997 行为图中直接播放该动画的状态名。
---@field TAEAnimID number c7100 TAE 时间线编号。
---@field ReferenceType string TAE 引用类型；当前基础移动均为 Direct。
---@field MotionSourceAnimID number 最终提供骨骼动作的物理 HKX 编号。
---@field EventSourceAnimID number 提供 TAE 事件的时间线编号。
---@field ResolvedAssetID number UE 实际播放的 AnimSequence 编号。
---@field AssetPath string UE AnimSequence 对象路径。
---@field bLoop boolean 状态机 SequencePlayer 是否循环。

---构造经过 c9997 行为图和 c7100 TAE 双重核对的 Direct 基础动画解析记录。
---@param semantic_name string UE 基础动画语义名。
---@param behavior_state_name string c9997 行为图状态名。
---@param animation_id number 行为状态直接引用的动画编号。
---@param loop_animation boolean 是否循环播放。
---@return GenichiroLocomotionResolution resolution 完整解析记录。
local function direct_locomotion(
    semantic_name,
    behavior_state_name,
    animation_id,
    loop_animation)
    return {
        SemanticName = semantic_name,
        BehaviorStateName = behavior_state_name,
        TAEAnimID = animation_id,
        ReferenceType = "Direct",
        MotionSourceAnimID = animation_id,
        EventSourceAnimID = animation_id,
        ResolvedAssetID = animation_id,
        AssetPath = animation_path(animation_id),
        bLoop = loop_animation,
    }
end

-- c9997 权威状态映射：400000 是战斗待机；405xxx 是战斗移动与转身。
-- 7010、8010～8013、8400/8401 分别属于坠落、四向受击和弹反受击，禁止用于基础移动。
local LocomotionResolution = {
    Idle = direct_locomotion("Idle", "IdleBattle", 400000, true),
    MoveForward = direct_locomotion("MoveForward", "RunFrontBattle", 405010, true),
    MoveBackward = direct_locomotion("MoveBackward", "WalkBackBattle", 405001, true),
    MoveLeft = direct_locomotion("MoveLeft", "WalkLeftBattle", 405002, true),
    MoveRight = direct_locomotion("MoveRight", "WalkRightBattle", 405003, true),
    TurnLeft = direct_locomotion("TurnLeft", "TurnBattle_Left90", 405400, false),
    TurnRight = direct_locomotion("TurnRight", "TurnBattle_Right90", 405401, false),
}

local GenichiroAnimAssets = {
    Skeleton = "/Game/Characters/Genichiro/Genichiro_Skeleton.Genichiro_Skeleton",
    SlotName = "CombatFullBodySlot",
    RootMotionMode = UE.ERootMotionMode.RootMotionFromMontagesOnly,
    LocomotionResolution = LocomotionResolution,
    Locomotion = {
        Idle = LocomotionResolution.Idle.AssetPath,
        MoveForward = LocomotionResolution.MoveForward.AssetPath,
        MoveBackward = LocomotionResolution.MoveBackward.AssetPath,
        MoveLeft = LocomotionResolution.MoveLeft.AssetPath,
        MoveRight = LocomotionResolution.MoveRight.AssetPath,
        TurnLeft = LocomotionResolution.TurnLeft.AssetPath,
        TurnRight = LocomotionResolution.TurnRight.AssetPath,
    },
}

local LuaNameByNativeAsset = {}
for semantic_name, asset_path in pairs(GenichiroAnimAssets.Locomotion) do
    local native_name = string.match(asset_path, "%.([^%.]+)$")
    LuaNameByNativeAsset[native_name] = "Locomotion." .. semantic_name
end

---把原生资产短名转换为 Lua 资源表语义路径，供动画调试器显示。
---@param native_asset_name string AnimSequence UObject 短名。
---@return string|nil semantic_name 已登记语义名；未知资产返回 nil。
function GenichiroAnimAssets.GetLuaAssetName(native_asset_name)
    return LuaNameByNativeAsset[native_asset_name]
end

---查询 UE 基础动画语义到行为状态、TAE 来源和可播放资产的权威解析记录。
---@param semantic_name string Idle、四向移动或左右转身语义名。
---@return GenichiroLocomotionResolution|nil resolution 已核对的解析记录；未知语义返回 nil。
function GenichiroAnimAssets.GetLocomotionResolution(semantic_name)
    return LocomotionResolution[semantic_name]
end

return GenichiroAnimAssets
