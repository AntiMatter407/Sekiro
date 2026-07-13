-- Lua AnimGraph SequencePlayer 节点。
-- 继承链对应 UE：FAnimNode_Base -> FAnimNode_AssetPlayerBase -> FAnimNode_SequencePlayerBase。
local class = require("Animation.Base.Class")
local LuaAssetPlayerBase = require("Animation.Base.AnimGraph.LuaAssetPlayerBase")

---@class LuaSequencePlayer : LuaAssetPlayerBase
---@field Sequence string|table|userdata|nil 当前播放的动画序列资源引用。
---@field AnimationName string|nil 调试显示使用的 Lua 动画别名；缺失时从资源表反查。
---@field PlayRate number 播放倍率，RootMotion Locomotion 通常保持 1。
---@field bLoop boolean|nil 是否循环；nil 表示读取当前状态的 AnimationSettings。
---@field bResetTime boolean|nil 是否重置时间；nil 表示跟随状态机 Enter/Keep 生命周期。
---@field StartPosition number|nil 重置时的归一化起播位置，范围为 0..1。
local LuaSequencePlayer = class("LuaSequencePlayer", LuaAssetPlayerBase, {
    NodeType = "SequencePlayer",
    Sequence = nil,
    AnimationName = nil,
    PlayRate = 1.0,
    bLoop = nil,
    bResetTime = nil,
    StartPosition = nil,
})

---读取当前 SequencePlayer 引用的动画资源。
---@return string|table|userdata|nil animation_asset 当前 Sequence 资源引用。
function LuaSequencePlayer:GetAnimationAsset()
    return self.Sequence
end

---确保当前 Lua 节点拥有与动画层和稳定名称对应的 C++ SequencePlayer。
---@return boolean ready 原生节点存在或创建成功时为 true。
function LuaSequencePlayer:EnsureNativeNode()
    if self:IsNativeNodeValid() then
        return true
    end

    if self.Owner == nil or self.NodeName == "" then
        return false
    end

    self.NativePoseLink = self.Owner:CallCpp(
        "CreateLuaSequencePlayer",
        self.LayerName,
        self.NodeName)
    return self:IsNativeNodeValid()
end

---读取当前节点是否应在本帧重置播放时间。
---@return boolean reset_time 显式配置优先，否则进入新状态时为 true、保持状态时为 false。
function LuaSequencePlayer:ShouldResetTime()
    if self.bResetTime ~= nil then
        return self.bResetTime == true
    end

    return self.Owner ~= nil and self.Owner.EvaluatingResetTime == true
end

---读取当前节点的循环策略；类内显式字段优先于状态机动画设置。
---@return boolean loop 当前动画是否循环播放。
function LuaSequencePlayer:ShouldLoop()
    if self.bLoop ~= nil then
        return self.bLoop == true
    end

    local state_name = self.Owner and self.Owner:GetEvaluatingStateName() or nil
    return self.Owner ~= nil and self.Owner:GetPoseLoop(state_name, self) == true
end

---把 SequencePlayer 节点成员同步给 C++ 持久播放器。
---@return boolean evaluated 动画路径和原生节点有效、全部参数写入成功时为 true。
function LuaSequencePlayer:Evaluate()
    if self.Owner == nil or not self:EnsureNativeNode() then
        return false
    end

    local animation_path = self.Owner:GetAnimationPath(self.Sequence)
    if animation_path == nil or animation_path == "" then
        self.Owner:LogDebugFlag("DebugPose", "PoseGraph", string.format(
            "SequencePlayer has no animation layer=%s node=%s",
            tostring(self.LayerName),
            tostring(self.NodeName)))
        return false
    end

    local animation_name = self.AnimationName
        or self.Owner:GetAnimationName(self.Sequence)
        or self.NodeName
    local start_position = self.StartPosition ~= nil
        and self.Owner:Clamp(self.Owner:AsNumber(self.StartPosition, 0), 0, 1)
        or 0
    local play_rate = self.Owner:AsNumber(self.PlayRate, 1)
    local asset_set = self.Owner:CallCpp(
        "SetLuaSequencePlayerAssetByPath",
        self.NativePoseLink,
        animation_path,
        animation_name,
        self:ShouldResetTime(),
        start_position)
    local parameters_set = self.Owner:CallCpp(
        "SetLuaSequencePlayerParameters",
        self.NativePoseLink,
        play_rate,
        self:ShouldLoop())

    return asset_set == true and parameters_set == true
end

return LuaSequencePlayer
