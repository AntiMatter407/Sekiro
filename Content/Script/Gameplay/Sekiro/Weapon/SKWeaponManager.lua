-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKWeaponManagerComponent，可直接读写 UPROPERTY 并调用 UFUNCTION。
-- 负责读取 Lua 武器配置、生成武器，并按原版 TAE 事件帧编排刀身与刀鞘的合并和分离。

local LuaLog = require("Gameplay.Base.LuaLog")
local WeaponConfig = require("Gameplay.Sekiro.Weapon.WeaponConfig")

---@class SKWeaponTransitionRuntime
---@field Name string 当前播放的动作名，值为 Sheathe 或 Draw。
---@field Config SKWeaponTransitionConfig 当前动作对应的只读配置。
---@field ElapsedSeconds number 从动作开始累计的秒数。
---@field bPresentationSwitched boolean 是否已经收到本动作的 WeaponEvent Notify 并提交展示状态。

---@class SKWeaponManager: USKWeaponManagerComponent
---@field ActiveWeaponId string|nil 当前已生成武器的稳定配置键。
---@field ActiveWeaponConfig SKWeaponDefinition|nil 当前已生成武器的只读 Lua 配置。
---@field ActiveTransition SKWeaponTransitionRuntime|nil 当前正在播放的武器切换动作。
---@field bRestrictedZoneObserved boolean 上一帧是否处于限制输入区域。
---@field PendingRestrictedTransition string|nil 等待稳定移动状态后执行的 Sheathe 或 Draw。
---@field CurrentPresentation string|nil Lua 已确认的当前武器展示状态。
local SKWeaponManager = UnLua.Class()
local Debug = true

---按稳定配置键读取武器定义，缺失时返回 nil，避免把无效路径传入 C++。
---@param weapon_id string|nil 武器配置键；nil 时使用模块默认武器键。
---@return string resolved_weapon_id 实际参与查询的武器配置键。
---@return SKWeaponDefinition|nil weapon_definition 找到的武器配置，未登记时为 nil。
local function resolve_weapon_definition(weapon_id)
    local resolved_weapon_id = weapon_id or WeaponConfig.DefaultWeaponId
    return resolved_weapon_id, WeaponConfig.Weapons[resolved_weapon_id]
end

---在 UnLua 绑定阶段初始化纯脚本状态；此时不访问 Owner、World 或其他尚未完成构造的 UObject。
---@param _initializer table|nil UnLua 可选初始化表；当前模块不读取该参数。
---@return nil 该生命周期入口只重置 Lua 私有状态。
function SKWeaponManager:Initialize(_initializer)
    self.ActiveWeaponId = nil
    self.ActiveWeaponConfig = nil
    self.ActiveTransition = nil
    self.bRestrictedZoneObserved = false
    self.PendingRestrictedTransition = nil
    self.CurrentPresentation = nil
end

---按 Lua 配置生成武器并提交初始展示状态；C++ 只负责资产加载、Actor 生命周期和组件挂载。
---@param weapon_id string|nil 武器配置键；nil 时生成默认武器。
---@return boolean spawned 武器是否已经存在或成功生成并完成初始挂载。
function SKWeaponManager:SpawnConfiguredWeapon(weapon_id)
    local resolved_weapon_id, weapon_definition = resolve_weapon_definition(weapon_id)
    if weapon_definition == nil then
        return false
    end

    local attachments = weapon_definition.Attachments
    local spawned = self:SpawnWeaponByClassPath(
        weapon_definition.ActorClassPath,
        attachments.HandSocket,
        attachments.HandBoneFallback,
        attachments.SheathSocket)
    if spawned ~= true then
        return false
    end

    if self:SetWeaponPresentationByName(weapon_definition.InitialPresentation) ~= true then
        return false
    end

    self.ActiveWeaponId = resolved_weapon_id
    self.ActiveWeaponConfig = weapon_definition
    self.CurrentPresentation = weapon_definition.InitialPresentation
    return true
end

---把刀身合并到刀鞘所在的角色 Dummy 147；该操作只在收刀 Type 715 起始帧调用一次。
---@return boolean merged 刀身是否成功切换到收纳挂点。
function SKWeaponManager:MergeBladeIntoSheath()
    local merged = self:SetWeaponPresentationByName("Sheathed") == true
    if merged then
        self.CurrentPresentation = "Sheathed"
    end
    return merged
end

---把刀身从 Dummy 147 分离并恢复到右手 Dummy 20；该操作只在拔刀 Type 715 结束帧调用一次。
---@return boolean separated 刀身是否成功切换到右手挂点。
function SKWeaponManager:SeparateBladeFromSheath()
    local separated = self:SetWeaponPresentationByName("Drawn") == true
    if separated then
        self.CurrentPresentation = "Drawn"
    end
    return separated
end

---启动收刀或拔刀动作，并在播放前恢复该动作要求的起始挂载状态。
---挂载切换完全由动画资产内的 WeaponEvent Notify 驱动，配置帧只用于资产写入和验证。
---@param transition_name string 动作名，只接受 Sheathe 或 Draw。
---@return boolean started 动画和起始挂载状态是否均已成功提交。
function SKWeaponManager:StartWeaponTransition(transition_name)
    local weapon_definition = self.ActiveWeaponConfig
    if weapon_definition == nil then
        return false
    end

    local transition_config = weapon_definition.Animations[transition_name]
    if transition_config == nil then
        return false
    end

    if self:SetWeaponPresentationByName(transition_config.StartPresentation) ~= true then
        return false
    end
    self.CurrentPresentation = transition_config.StartPresentation

    local animation_path = transition_config.AnimationPath
    if WeaponConfig.RestrictedTransitionMode == "Additive" then
        animation_path = transition_config.AdditiveAnimationPath
    end

    -- Slot 动态 Montage 只进入 AnimGraph 的上半身分支；Additive 资源会在 Slot 内叠加到当前 Run/Sprint Pose。
    if self:PlayCharacterSlotAnimationByPath(
        animation_path,
        transition_config.SlotName,
        transition_config.BlendInTime,
        transition_config.BlendOutTime,
        1.0,
        1) ~= true then
        return false
    end

    self.ActiveTransition = {
        Name = transition_name,
        Config = transition_config,
        ElapsedSeconds = 0.0,
        bPresentationSwitched = false,
    }
    LuaLog.Debug(
        Debug,
        "SKWeaponManager",
        "Transition",
        string.format(
            "%s mode=%s animation=%s notify=%s source_frame=%d source_time=%.6f",
            transition_name,
            WeaponConfig.RestrictedTransitionMode,
            animation_path,
            transition_config.EventName,
            transition_config.SwitchFrame,
            transition_config.SwitchTime))
    return true
end

---播放配置中的收刀动作，并在原版第 14 帧把刀身合并到刀鞘。
---@return boolean started 收刀流程是否成功开始。
function SKWeaponManager:PlaySheathe()
    return self:StartWeaponTransition("Sheathe")
end

---播放配置中的拔刀动作，并在原版第 7 帧把刀身从刀鞘分离到右手。
---@return boolean started 拔刀流程是否成功开始。
function SKWeaponManager:PlayDraw()
    return self:StartWeaponTransition("Draw")
end

---完整播放一次收刀预览；保留旧入口名供现有调用方兼容，挂载切换仍只由 WeaponEvent Notify 驱动。
---@return boolean started 收刀检查流程是否成功开始。
function SKWeaponManager:PlaySheathePreview()
    return self:StartWeaponTransition("Sheathe")
end

---在组件 BeginPlay 后读取默认武器配置并生成武器；预览开关启用时自动播放一次收刀动作。
---@return nil 该生命周期入口只提交武器生成和可选预览流程。
function SKWeaponManager:ReceiveBeginPlay()
    self.ActiveTransition = nil
    if self:SpawnConfiguredWeapon(nil) ~= true then
        return
    end

    self.bRestrictedZoneObserved = self:IsRestrictedZoneActive() == true
    if self.bRestrictedZoneObserved then
        self.PendingRestrictedTransition = "Sheathe"
    end

    if WeaponConfig.AutoPlaySheathePreview == true then
        self:PlaySheathePreview()
    end
end

---接收自定义 AnimNotify 转发的武器动画事件，并在该动画帧提交刀身合并或分离。
---@param event_name string 动画资产中配置的 WeaponEvent 名称。
---@param _animation UAnimSequenceBase|nil 触发事件的动画资产；当前仅保留给诊断扩展使用。
---@return boolean handled 事件与当前切换动作匹配且展示状态已提交时返回 true。
function SKWeaponManager:OnWeaponAnimationEvent(event_name, _animation)
    local transition = self.ActiveTransition
    if transition == nil or event_name ~= transition.Config.EventName then
        return false
    end
    if transition.bPresentationSwitched == true then
        return true
    end

    local switched = false
    if transition.Config.TargetPresentation == "Sheathed" then
        switched = self:MergeBladeIntoSheath()
    elseif transition.Config.TargetPresentation == "Drawn" then
        switched = self:SeparateBladeFromSheath()
    end
    if switched ~= true then
        return false
    end

    transition.bPresentationSwitched = true
    LuaLog.Debug(
        Debug,
        "SKWeaponManager",
        "Presentation",
        string.format(
            "%s handled notify=%s at animation_time=%.6f source_frame=%d",
            transition.Name,
            event_name,
            self:GetCharacterAnimationPosition(),
            transition.Config.SwitchFrame))
    return true
end

---推进动画预览计时；正式的合并和分离只由 OnWeaponAnimationEvent 处理。
---@param delta_seconds number|nil C++ Tick 传入的本帧秒数；nil 时按 0 处理。
---@return boolean handled 始终返回 true，表示 Lua 已消费 WeaponManager 本帧编排。
function SKWeaponManager:Tick(delta_seconds)
    local restricted = self:IsRestrictedZoneActive() == true
    if restricted ~= self.bRestrictedZoneObserved then
        self.bRestrictedZoneObserved = restricted
        self.PendingRestrictedTransition = restricted and "Sheathe" or "Draw"
    end

    local transition = self.ActiveTransition
    if transition ~= nil then
        local delta = delta_seconds or 0.0
        transition.ElapsedSeconds = transition.ElapsedSeconds + delta
        if transition.ElapsedSeconds >= transition.Config.Duration then
            self.ActiveTransition = nil
        end
    end

    if self.ActiveTransition == nil
        and self.PendingRestrictedTransition ~= nil
        and self:IsOwnerReadyForRestrictedWeaponTransition() == true then
        local pending = self.PendingRestrictedTransition
        local target_presentation = pending == "Sheathe" and "Sheathed" or "Drawn"
        if self.CurrentPresentation == target_presentation then
            self.PendingRestrictedTransition = nil
        elseif self:StartWeaponTransition(pending) == true then
            self.PendingRestrictedTransition = nil
        end
    end
    return true
end

return SKWeaponManager
