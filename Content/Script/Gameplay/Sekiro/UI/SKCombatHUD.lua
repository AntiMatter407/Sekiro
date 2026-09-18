-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKCombatHUDWidget，不创建第二份 Widget 代理。
-- 仅绑定 Survival 已提交快照和既有锁定目标；Boss 由显式显示标记或 HUD 提供，不随脱锁清空。
local Style = require("Gameplay.Sekiro.UI.CombatHUDStyle")
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKCombatHUDBinding
---@field Actor AActor|nil 当前显示对象，销毁或换目标时解绑。
---@field Survival USKSurvivalComponent|nil 唯一资源与生命状态来源。
---@field Generation number 绑定代次，仅供诊断，不参与数值计算。
---@field Handlers table<string, function> 各原生事件独立且稳定的回调，解绑必须使用同一函数对象。

---@class SKCombatHUDRuntime
---@field HUD ASKHUD|nil 所属本地 HUD。
---@field Controller APlayerController|nil 已订阅换 Pawn 通知的控制器。
---@field Camera USKCameraManagerComponent|nil 玩家现有锁定来源，不进行目标搜索。
---@field Player SKCombatHUDBinding 玩家绑定。
---@field Target SKCombatHUDBinding 普通锁定目标绑定。
---@field Boss SKCombatHUDBinding 显式 Boss 绑定。
---@field bEnding boolean 是否已经开始解绑。
---@field bConfigured boolean 原图样式是否已完成配置。

---@class SKCombatHUD: USKCombatHUDWidget
---@field Runtime SKCombatHUDRuntime|nil 仅持有显示绑定及生命周期信息。
local SKCombatHUD = UnLua.Class()

local SlotGroups = {
    Player = { Health = "PlayerHealth", Posture = "PlayerPosture" },
    Target = { Health = "TargetHealth", Posture = "TargetPosture" },
    Boss = { Health = "BossHealth", Posture = "EnemyPosture" },
}
local ResourceEvents = {
    "OnSurvivalReady", "OnHealthChanged", "OnPostureChanged", "OnLifeStateChanged",
    "OnPostureBroken", "OnPostureRecovered", "OnPostureBreakCancelled",
}

---检查 UObject 是否仍有效，避免销毁通知之后访问残留的 Lua userdata。
---@param object UObject|nil 待检查的对象，不取得额外所有权。
---@return boolean valid 对象存在且未失效时为 true。
local function is_valid(object)
    return object ~= nil and UE.UKismetSystemLibrary.IsValid(object)
end

---读取角色蓝图配置的展示身份，不把普通 AI、血量大小或类名解释成 Boss。
---@param actor AActor|nil 待检查的角色；没有显式标记时按普通目标处理。
---@return boolean boss 是否具有样式配置指定的 Boss Actor Tag。
local function is_boss_actor(actor)
    return is_valid(actor) and actor:ActorHasTag(Style.BossActorTag)
end

---检查提交快照中的数值，拒绝 NaN、无穷或非数值字段。
---@param value number|nil 待验证的显示数据。
---@return boolean finite 是否为有限数值。
local function is_finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

---把配置中的二维坐标转换为反射结构；缺失配置保持零，由原生完整校验决定是否接受。
---@param value table<string, number>|nil 含 X/Y 的配置坐标。
---@return FVector2D vector 可传入 UFUNCTION 的独立值。
local function make_vector(value)
    return UE.FVector2D(value ~= nil and value.X or 0.0, value ~= nil and value.Y or 0.0)
end

---验证资源范围并计算显示比例；不修正或回写 GAS。
---@param current number 当前已提交资源。
---@param maximum number 当前已提交资源上限。
---@return boolean valid 上限为正且两个数值均有限时为 true。
---@return number ratio 夹取零到一的显示比例，非法数据返回零。
local function resource_ratio(current, maximum)
    if not is_finite(current) or not is_finite(maximum) or maximum <= 0.0 then
        return false, 0.0
    end
    return true, math.max(0.0, math.min(1.0, current / maximum))
end

---为单一槽位和单一资源事件创建独立函数身份，避免 UnLua 复用不同原生签名的委托处理器。
---@param slot_name string 此回调专属的显示槽位。
---@param event_name string 此回调专属的原生事件名；不能用于其他事件地址。
---@return function handler 由绑定表持有的稳定函数，移除委托时原样传回。
local function make_resource_handler(slot_name, event_name)
    ---忽略该事件的临时参数并重读当前快照；函数独立身份确保原生层先按正确签名解码。
    ---@param widget SKCombatHUD 委托接收 UObject，由 UnLua 作为首参数传入。
    ---@return nil result 仅刷新仍持有此回调的槽位。
    local function handle_resource_event(widget)
        local runtime = widget.Runtime
        if runtime ~= nil and runtime[slot_name].Handlers[event_name] == handle_resource_event then
            widget:RefreshSlot(slot_name)
        end
    end
    return handle_resource_event
end

---为一个显示槽位建立稳定回调集合；同一 Actor 同时是 Boss 与锁定目标时仍有独立订阅。
---@param slot_name string 已定义的显示槽位。
---@return SKCombatHUDBinding binding 尚未绑定 Actor 的槽位及其专属事件函数。
local function make_binding(slot_name)
    local binding = { Generation = 0, Handlers = {} }
    for _, event_name in ipairs(ResourceEvents) do
        binding.Handlers[event_name] = make_resource_handler(slot_name, event_name)
    end
    ---只清理本槽位对应的销毁对象，不复用其他槽位的原生委托地址。
    ---@param widget SKCombatHUD 真实 Widget 接收对象。
    ---@param actor AActor 引擎通知的销毁对象身份。
    ---@return nil result 不将销毁事件解释成死亡或奖励。
    binding.Handlers.OnDestroyed = function(widget, actor)
        widget:HandleSlotActorDestroyed(slot_name, actor)
    end
    ---覆盖未 Destroy 的关卡离场，独立回调保持 EndPlay 的双参数签名。
    ---@param widget SKCombatHUD 真实 Widget 接收对象。
    ---@param actor AActor 当前离场的对象身份。
    ---@param _reason EEndPlayReason 引擎离场原因，本层不据此改变游戏资源。
    ---@return nil result 只清理本槽位绑定。
    binding.Handlers.OnEndPlay = function(widget, actor, _reason)
        widget:HandleSlotActorDestroyed(slot_name, actor)
    end
    return binding
end

---从显式样式加载已导入纹理；缺资源只记录诊断，不生成替代图形。
---@return boolean configured 至少一个原图层有效且参考画布合法时为 true。
function SKCombatHUD:ConfigureOriginalSprites()
    self:ClearHUDSprites()
    if not self:SetReferenceSize(make_vector(Style.ReferenceSize)) then
        LuaLog.Debug(true, "SKCombatHUD", "ConfigureOriginalSprites", "原版参考画布配置无效，HUD 保持隐藏。")
        return false
    end
    local texture_cache = {}
    local accepted = 0
    for _, item in ipairs(Style.Sprites or {}) do
        local texture = texture_cache[item.Texture]
        if texture == nil and type(item.Texture) == "string" and item.Texture ~= "" then
            texture = UE.UObject.Load(item.Texture)
            texture_cache[item.Texture] = texture
        end
        if is_valid(texture) then
            local sprite = UE.FSKCombatHUDSprite()
            sprite.Id = item.Id
            sprite.Group = item.Group
            sprite.Texture = texture
            sprite.Position = make_vector(item.Position)
            sprite.Size = make_vector(item.Size)
            sprite.UVMin = make_vector(item.UVMin)
            sprite.UVMax = make_vector(item.UVMax)
            sprite.Anchor = make_vector(item.Anchor)
            sprite.Fill = item.Fill or UE.ESKHUDFillMode.None
            sprite.bBrokenOnly = item.BrokenOnly == true
            sprite.bMirrorX = item.MirrorX == true
            local tint = item.Tint or { R = 1.0, G = 1.0, B = 1.0, A = 1.0 }
            sprite.Tint = UE.FLinearColor(tint.R, tint.G, tint.B, tint.A)
            if self:AddHUDSprite(sprite) then
                accepted = accepted + 1
            else
                LuaLog.Debug(true, "SKCombatHUD", "ConfigureOriginalSprites", "原图图层参数无效：" .. tostring(item.Id))
            end
        else
            LuaLog.Debug(true, "SKCombatHUD", "ConfigureOriginalSprites", "原版纹理尚未导入或路径无效：" .. tostring(item.Texture))
        end
    end
    self:SetHUDGroupScreenPosition("TargetHealth", UE.FVector2D(), false)
    self:SetHUDGroupScreenPosition("TargetPosture", UE.FVector2D(), false)
    return accepted > 0
end

---解除指定槽位对旧角色及 Survival 的全部委托，换目标前总是先执行。
---@param slot_name string Player、Target 或 Boss，必须为已定义槽位。
---@return nil result 仅清理显示绑定，不改变目标或生命状态。
function SKCombatHUD:UnbindSlot(slot_name)
    local binding = self.Runtime[slot_name]
    if is_valid(binding.Survival) then
        for _, event_name in ipairs(ResourceEvents) do
            binding.Survival[event_name]:Remove(self, binding.Handlers[event_name])
        end
    end
    if is_valid(binding.Actor) then
        binding.Actor.OnDestroyed:Remove(self, binding.Handlers.OnDestroyed)
        binding.Actor.OnEndPlay:Remove(self, binding.Handlers.OnEndPlay)
    end
    binding.Actor = nil
    binding.Survival = nil
    binding.Generation = binding.Generation + 1
    self:SetHUDGroupState(SlotGroups[slot_name].Health, false, 0.0, false)
    self:SetHUDGroupState(SlotGroups[slot_name].Posture, false, 0.0, false)
end

---切换一个显示槽位；订阅完成后立刻读取快照，覆盖初始化早于 UI 的顺序。
---@param slot_name string 已定义的显示槽位。
---@param actor AActor|nil 新的显式对象，空值只隐藏并解绑。
---@return nil result 不缓存另一份当前血量或上限。
function SKCombatHUD:BindSlot(slot_name, actor)
    local binding = self.Runtime[slot_name]
    if binding.Actor == actor and (actor == nil or is_valid(binding.Survival)) then
        return
    end
    self:UnbindSlot(slot_name)
    if not is_valid(actor) then
        return
    end
    binding.Actor = actor
    binding.Survival = actor:GetComponentByClass(UE.USKSurvivalComponent.StaticClass())
    actor.OnDestroyed:Add(self, binding.Handlers.OnDestroyed)
    actor.OnEndPlay:Add(self, binding.Handlers.OnEndPlay)
    if is_valid(binding.Survival) then
        for _, event_name in ipairs(ResourceEvents) do
            binding.Survival[event_name]:Add(self, binding.Handlers[event_name])
        end
    end
    self:RefreshSlot(slot_name)
end

---重读当前槽位的最终 Survival 快照；旧事件参数不能覆盖新目标数据。
---@param slot_name string 已定义槽位名。
---@return nil result 非法或未就绪数据隐藏；玩家死亡仍显示空血，敌人死亡隐藏但保留回生订阅。
function SKCombatHUD:RefreshSlot(slot_name)
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    local binding = self.Runtime[slot_name]
    local groups = SlotGroups[slot_name]
    if not is_valid(binding.Actor) or not is_valid(binding.Survival) then
        self:SetHUDGroupState(groups.Health, false, 0.0, false)
        self:SetHUDGroupState(groups.Posture, false, 0.0, false)
        return
    end
    local snapshot = binding.Survival:GetSurvivalSnapshot()
    local health_valid, health_ratio = resource_ratio(snapshot.Attributes.Health, snapshot.Attributes.MaxHealth)
    local posture_valid, posture_ratio = resource_ratio(snapshot.Attributes.Posture, snapshot.Attributes.MaxPosture)
    local visible = snapshot.bReady == true and (slot_name == "Player" or snapshot.LifeState == UE.ESKLifeState.Alive)
    if slot_name == "Target" and (binding.Actor == self.Runtime.Boss.Actor or is_boss_actor(binding.Actor)) then
        visible = false
    end
    self:SetHUDGroupState(groups.Health, visible and health_valid, health_ratio, false)
    self:SetHUDGroupState(groups.Posture, visible and posture_valid, posture_ratio, snapshot.bPostureBroken)
end

---控制器换 Pawn 时解绑旧玩家和普通目标，并缓存新角色现有相机组件。
---@param _old_pawn APawn|nil 旧 Pawn，绑定清理由当前槽位记录完成。
---@param new_pawn APawn|nil 新 Pawn；空值隐藏玩家区域。
---@return nil result Boss 显式展示对象不受玩家换 Pawn 影响。
function SKCombatHUD:HandlePlayerPawnChanged(_old_pawn, new_pawn)
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    self:BindSlot("Target", nil)
    self:BindSlot("Player", new_pawn)
    self.Runtime.Camera = is_valid(new_pawn) and new_pawn:GetComponentByClass(UE.USKCameraManagerComponent.StaticClass()) or nil
end

---对象销毁或离场时只清理对应槽位，独立委托不会误删同一 Actor 的其他展示订阅。
---@param slot_name string 接收通知的原始槽位。
---@param actor AActor 销毁通知携带的原对象身份。
---@return nil result 玩家离场额外释放相机与普通锁定展示，Boss 保持独立生命周期。
function SKCombatHUD:HandleSlotActorDestroyed(slot_name, actor)
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    if self.Runtime[slot_name].Actor ~= actor then
        return
    end
    self:UnbindSlot(slot_name)
    if slot_name == "Player" then
        self.Runtime.Camera = nil
        self:BindSlot("Target", nil)
    end
end

---设置独立 Boss 展示对象；初始化标记、锁定发现或外部 Encounter 均经 HUD 显式提交。
---@param actor AActor|nil 需要展示的 Boss 对象，nil 显式清空。
---@return nil result 同一对象的普通锁定条去重，但不修改相机锁定。
function SKCombatHUD:SetBossTarget(actor)
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    self:BindSlot("Boss", actor)
    self:RefreshSlot("Target")
end

---配置原图、绑定本地控制器和当前 Pawn，并恢复初始化前已设置的 Boss 展示对象。
---@param hud ASKHUD 本地 HUD 宿主；只在活跃期间保存引用。
---@return boolean ready 纹理配置已接受时为 true；资源缺失时仍安全绑定但不制造占位素材。
function SKCombatHUD:InitializeCombatHUD(hud)
    if self.Runtime ~= nil and self.Runtime.bEnding ~= true then
        return self.Runtime.bConfigured
    end
    self.Runtime = {
        HUD = hud,
        Controller = nil,
        Camera = nil,
        Player = make_binding("Player"),
        Target = make_binding("Target"),
        Boss = make_binding("Boss"),
        bEnding = false,
        bConfigured = false,
    }
    self.Runtime.bConfigured = self:ConfigureOriginalSprites()
    local controller = hud:GetHUDPlayerController()
    self.Runtime.Controller = controller
    if is_valid(controller) then
        controller.OnPossessedPawnChanged:Add(self, self.HandlePlayerPawnChanged)
        -- GetPawn 仅供 C++ 调用；Lua 必须使用已暴露为 UFUNCTION 的 K2_GetPawn，避免初始化中断后 HUD 一直隐藏。
        self:HandlePlayerPawnChanged(nil, controller:K2_GetPawn())
    end
    local explicit_boss = hud:GetBossDisplayTarget()
    if is_valid(explicit_boss) then
        self:SetBossTarget(explicit_boss)
    else
        -- 当前单 Boss 场景只在初始化查询一次。多 Boss 时不按遍历顺序猜测，由显式入口或首次锁定选择。
        local bosses = UE.TArray(UE.AActor)
        UE.UGameplayStatics.GetAllActorsWithTag(hud, Style.BossActorTag, bosses)
        if bosses:Length() == 1 then
            hud:SetBossDisplayTarget(bosses:Get(1), hud:GetBossDisplayName())
        end
    end
    return self.Runtime.bConfigured
end

---每帧只读取既有锁定指针变化并投影普通目标，不遍历世界或轮询资源数值。
---@param _delta_seconds number 本帧秒数；本版直接展示提交结果，没有伪造原版补间时长。
---@return nil result 只在新锁定 Boss 且没有展示对象时接入；脱锁或切换普通敌人不覆盖已有 Boss。
function SKCombatHUD:RefreshBindingsFromTick(_delta_seconds)
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    local camera = self.Runtime.Camera
    local target = is_valid(camera) and camera:GetLockTarget() or nil
    if not is_valid(target) then
        target = nil
    end
    if target ~= self.Runtime.Target.Actor and is_boss_actor(target) and not is_valid(self.Runtime.Boss.Actor) then
        -- 覆盖 HUD 初始化之后才生成的 Boss；已有显式 Encounter 展示对象拥有优先权。
        local hud = self.Runtime.HUD
        hud:SetBossDisplayTarget(target, hud:GetBossDisplayName())
    end
    self:BindSlot("Target", target)
    if is_valid(target) and target ~= self.Runtime.Boss.Actor and not is_boss_actor(target) then
        local projected, position = self:ProjectActorAnchor(target, Style.TargetAnchorHeightOffset or 0.0)
        if projected then
            local offset = make_vector(Style.TargetScreenOffset)
            position = position + offset
        end
        self:SetHUDGroupScreenPosition("TargetHealth", position, projected)
        self:SetHUDGroupScreenPosition("TargetPosture", position, projected)
    else
        self:SetHUDGroupScreenPosition("TargetHealth", UE.FVector2D(), false)
        self:SetHUDGroupScreenPosition("TargetPosture", UE.FVector2D(), false)
    end
end

---移除 Widget 或 HUD 离场前解绑全部 UObject 委托，重复调用无副作用。
---@return nil result 释放纹理和角色引用，不触及游戏输入模式。
function SKCombatHUD:ShutdownCombatHUD()
    if self.Runtime == nil or self.Runtime.bEnding then
        return
    end
    self.Runtime.bEnding = true
    if is_valid(self.Runtime.Controller) then
        self.Runtime.Controller.OnPossessedPawnChanged:Remove(self, self.HandlePlayerPawnChanged)
    end
    for slot_name in pairs(SlotGroups) do
        self:UnbindSlot(slot_name)
    end
    self.Runtime.Controller = nil
    self.Runtime.Camera = nil
    self.Runtime.HUD = nil
    self:ClearHUDSprites()
end

---UMG 控件从树中释放时执行同一解绑路径，覆盖外部通过 UIManager 移除的情形。
---@return nil result 不依赖 HUD EndPlay 一定先于 Widget Destruct。
function SKCombatHUD:Destruct()
    self:ShutdownCombatHUD()
end

return SKCombatHUD
