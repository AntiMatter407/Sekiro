-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKCombatComponent。
-- 负责攻击、防御、弹反裁决及通用动作；躯干数值、恢复与崩溃流程由 Survival 统一处理。

local CombatConfig = require("Gameplay.Sekiro.Combat.CombatConfig")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")

---@class SKCombatBufferedAttack
---@field InputSerial number 物理按键序列号。
---@field PressTime number Started 事件的世界时间。
---@field SideSnapshot userdata|number ESKAttackSide 输入发生时锁存的原生刀侧枚举。
---@field AttackDomain string 本次输入所属攻击域：Ground、Air 或 Land。
---@field LightActionId string|nil 本次输入允许衔接的轻攻击动作 ID。
---@field bLightAccepted boolean 本次输入是否允许判定为轻攻击。
---@field bHeavyAccepted boolean 本次输入是否允许判定为重攻击。
---@field bStartedFromGuard boolean 候选是否从防御动作或持续防御姿态创建。
---@field bGuardStartupPlaying boolean 是否已立即播放防御攻击共用起手。
---@field StartupActionSerial number|nil 共用起手 Montage 对应的动作序列号。

---@class SKCombatRuntime
---@field ActionId string|nil 当前具体动作稳定 ID。
---@field ActionSerial number 当前离散动作序列号。
---@field ActiveAttackKind string|nil 当前攻击种类：Light 或 Heavy。
---@field ActiveAttackDomain string|nil 当前轻攻击动作域：Ground、Air 或 Land。
---@field bWasAnimationPlaying boolean 上一帧是否仍在播放组件自有 Montage。
---@field PendingAttack SKCombatBufferedAttack|nil 等待 Completed 或长按阈值提交的攻击输入。
---@field RestingSide userdata|number ESKAttackSide 动画曲线已提交、供下一动作使用的原生刀侧枚举。
---@field ComboNextAction string|nil 当前动作允许衔接的下一段轻攻击 ID。
---@field bSideCurveConsumed boolean 当前攻击是否已经提交过一次非零 AttackSide。
---@field bReturnToGuardAfterAttack boolean 当前攻击结束时是否允许按 Held 意图返回防御姿态。
---@field DeflectRaiseStartSide userdata|number|nil 当前 Guard Raise 开始时锁存的 ESKAttackSide。
---@field DeflectSideResetRemaining number 弹反成功后的刀侧回默认倒计时，单位秒。
---@field LastDeflectType userdata|number|nil 上一次 ESKIncomingAttackType 原生枚举。
---@field DeflectStage number 当前同类型弹反段数。
---@field bWeaponHitboxActive boolean Lua 最近一次成功提交的武器攻击碰撞状态。

---@class SKCombatComponent: USKCombatComponent
---@field Runtime SKCombatRuntime
local SKCombatComponent = UnLua.Class()

local CurveThreshold = 0.5

---判断刀侧是否为可以参与攻击配置索引的原生枚举值。
---@param side userdata|number ESKAttackSide 原生枚举值。
---@return boolean valid 仅 Left 与 Right 返回 true。
local function is_valid_attack_side(side)
    return side == UE.ESKAttackSide.Left or side == UE.ESKAttackSide.Right
end

---创建一份不访问 UObject 的 Lua 运行时初始状态。
---@param initial_side userdata|number|nil C++ 当前记录的 ESKAttackSide 原生枚举。
---@return SKCombatRuntime runtime 新的运行时状态。
local function create_runtime(initial_side)
    local resting_side = initial_side
    if not is_valid_attack_side(resting_side) then
        resting_side = CombatConfig.DefaultSide
    end
    return {
        ActionId = nil,
        ActionSerial = 0,
        ActiveAttackKind = nil,
        ActiveAttackDomain = nil,
        bWasAnimationPlaying = false,
        PendingAttack = nil,
        RestingSide = resting_side,
        ComboNextAction = nil,
        bSideCurveConsumed = false,
        bReturnToGuardAfterAttack = false,
        DeflectRaiseStartSide = nil,
        DeflectSideResetRemaining = -1.0,
        LastDeflectType = nil,
        DeflectStage = 0,
        bWeaponHitboxActive = false,
    }
end

---创建或重置脚本运行时状态；此阶段不访问 Owner 或动画实例。
---@param _initializer table|nil UnLua 可选初始化表，当前实现不读取。
---@return nil result 仅重置 Lua 私有状态。
function SKCombatComponent:Initialize(_initializer)
    self.Runtime = create_runtime(CombatConfig.DefaultSide)
end

---按边沿切换 Owner 当前武器的攻击碰撞，开启前清空单次攻击命中记录。
---碰撞物理状态由 C++ 承载；Lua 只在原版攻击曲线跨越零值边界时调用，避免每帧重复修改组件。
---@param active boolean true 开启攻击查询，false 关闭攻击查询。
---@return boolean applied 目标状态已经成立或本次切换已成功提交。
function SKCombatComponent:SetWeaponHitboxActive(active)
    local target_active = active == true
    if self.Runtime.bWeaponHitboxActive == target_active then
        return true
    end

    if target_active == true then
        local activated = self:ActivateOwnerWeaponHitbox(true) == true
        if activated == true then
            self.Runtime.bWeaponHitboxActive = true
        end
        return activated
    end

    local deactivated = self:DeactivateOwnerWeaponHitbox() == true
    self.Runtime.bWeaponHitboxActive = false
    return deactivated
end

---把刀侧规范化后同步到 Lua 与 C++ 战斗宿主。
---@param side_name userdata|number|nil 期望锁存的 ESKAttackSide 原生枚举。
---@return userdata|number resolved_side 最终采用的 Left 或 Right 枚举值。
function SKCombatComponent:SetRestingSide(side_name)
    local resolved_side = side_name
    if not is_valid_attack_side(resolved_side) then
        resolved_side = CombatConfig.DefaultSide
    end
    self.Runtime.RestingSide = resolved_side
    self:SetNextAttackSide(resolved_side)
    return resolved_side
end

---根据地面状态写入独立防御姿态；离散动作状态不会再决定 Slot 下方的基础 Pose。
---@param force_air boolean|nil true 表示物理 Jump 已请求但 Falling 尚未在本帧更新。
---@return nil result C++ 战斗宿主被写入 GuardGround 或 GuardAir。
function SKCombatComponent:SetGuardCombatPosture(force_air)
    local posture = UE.ESKCombatPostureState.GuardGround
    if force_air == true or self:IsOwnerFalling() == true then
        posture = UE.ESKCombatPostureState.GuardAir
    end
    self:SetCombatPostureState(posture)
end

---清除持续防御姿态；全身 Slot 淡出后将回到普通移动状态机。
---@return nil result C++ 战斗宿主被写入 Normal 姿态。
function SKCombatComponent:ClearCombatPosture()
    self:SetCombatPostureState(UE.ESKCombatPostureState.Normal)
end

---清除动画内攻击链状态，并恢复默认刀侧。
---@param clear_pending boolean 是否同时丢弃尚未 Completed 的攻击输入。
---@return nil result 运行时状态和 C++ 刀侧被同步重置。
function SKCombatComponent:ResetAttackRetention(clear_pending)
    self:SetWeaponHitboxActive(false)
    if clear_pending == true then
        self.Runtime.PendingAttack = nil
    end
    self.Runtime.ActiveAttackKind = nil
    self.Runtime.ActiveAttackDomain = nil
    self.Runtime.ComboNextAction = nil
    self.Runtime.bSideCurveConsumed = false
    self.Runtime.bReturnToGuardAfterAttack = false
    local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
    self:SetCommittedAttackSide(default_side)
end

---组件开始运行时恢复中立动作与刀侧；属性初始化和生命周期锁由 Survival 独立管理。
---@return nil result 仅初始化动作宿主，不写生命或躯干状态。
function SKCombatComponent:ReceiveBeginPlay()
    if self.Runtime == nil then
        self:Initialize(nil)
    end
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    self:ClearCombatPosture()
    local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
    self:SetCommittedAttackSide(default_side)
    self:DeactivateOwnerWeaponHitbox()
    self.Runtime.bWeaponHitboxActive = false
end

---供外部生命周期流程中断动作；先使序列号失效，防止 StopMontage 的旧结束回调续接动作。
---此接口只清理战斗、动画和输入意图，不计算躯干，也不修改 Survival 状态或外部输入锁。
---@return nil result 所有旧战斗意图和碰撞窗口均被清理。
function SKCombatComponent:InterruptForExternalTransition()
    if self.Runtime == nil then
        self:Initialize(nil)
    end
    self:InvalidateCombatAction(0)
    self:DeactivateOwnerWeaponHitbox()
    self:StopOwnerAIMovement()
    self:StopCombatAnimation(0.0)
    self:ClearCombatInputEvents()
    self:ClearAICombatEvents()
    self:ClearOwnerGameplayInputForScript()
    self:ResetAttackRetention(true)
    self:ClearCombatPosture()
    self.Runtime.ActionId = nil
    self.Runtime.ActionSerial = 0
    self.Runtime.bWasAnimationPlaying = false
    self.Runtime.DeflectSideResetRemaining = -1.0
end

---外部状态转换成功后恢复普通动作；只在 Survival 已允许行动时执行，不负责解锁其他系统。
---@return boolean reset 是否已恢复到中立动作状态。
function SKCombatComponent:ResetAfterExternalTransition()
    local survival = self:GetOwner():GetSurvivalComponent()
    if survival == nil or survival:CanAct() ~= true then
        return false
    end
    self:InterruptForExternalTransition()
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    return true
end

---根据当前防御阶段收敛外部裁决，确保弹反成功只可能发生在 Guard Raise。
---外部错误提交的成功结果会在稳定防御时降级为 Guarded，其他状态降级为 DeflectFailed。
---@param result_name string DeflectSuccess、Guarded 或 DeflectFailed。
---@return string resolved_result 结合当前动作状态得到的最终防御结果。
function SKCombatComponent:ResolvePostureImpactResult(result_name)
    if result_name ~= "DeflectSuccess" then
        return result_name
    end

    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.GuardRaise then
        return "DeflectSuccess"
    end
    if state == UE.ESKCombatActionState.Guarding then
        return "Guarded"
    end
    return "DeflectFailed"
end

---把攻防结果交给 Survival，并根据状态结果选择对应战斗反应；本函数不计算任何躯干数值。
---@param result_name string 攻击方反馈或防御结果语义，由接触裁决提供。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@return boolean handled 是否成功接收结果并处理所需反应。
function SKCombatComponent:ProcessPostureImpact(result_name, attack_type)
    local survival = self:GetOwner():GetSurvivalComponent()
    if survival == nil or survival:IsAlive() ~= true then
        return false
    end
    if result_name == "AttackDeflected"
        or result_name == "AttackSuccess"
        or result_name == "AttackGuarded" then
        if survival:ApplyPostureImpact(result_name, attack_type, nil) ~= true
            or survival:IsPostureBroken() == true then
            return false
        end
        if result_name == "AttackDeflected" then
            return self:StartAttackDeflected()
        end
        return true
    end

    local resolved_result = self:ResolvePostureImpactResult(result_name)
    if resolved_result == "DeflectSuccess" then
        if self:StartDeflect() ~= true then
            return false
        end
        return survival:ApplyPostureImpact(resolved_result, attack_type, nil)
    end
    if survival:ApplyPostureImpact(resolved_result, attack_type, nil) ~= true then
        return false
    end
    if survival:IsPostureBroken() ~= true and resolved_result == "Guarded" then
        self:StartGuardImpact()
    end
    if survival:IsPostureBroken() ~= true and resolved_result == "DeflectFailed" then
        self:StartDeflectFailed(attack_type)
    end
    return true
end

---接收 C++ 或其他脚本提交的拼刀裁决，统一进入可复用的 Lua 结果处理流程。
---@param result_name string 三种攻击结果或 DeflectSuccess、Guarded、DeflectFailed。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@return nil result 架势和必要的动作状态已经更新。
function SKCombatComponent:HandlePostureImpact(result_name, attack_type)
    self:ProcessPostureImpact(result_name, attack_type)
end

---根据当前攻击动作向碰撞桥接报告抽象强度；蓄力突刺沿用 Thrust，其余攻击使用 Light。
---@return userdata|number attack_type ESKIncomingAttackType 枚举值。
function SKCombatComponent:ResolveOutgoingAttackType()
    if self.Runtime ~= nil and self.Runtime.ActiveAttackKind == "Heavy" then
        return UE.ESKIncomingAttackType.Thrust
    end
    return UE.ESKIncomingAttackType.Light
end

---只读判定一次武器接触，不在结果提交前改变攻防双方的资源、动作或动画。
---Guard Raise 整段沿用当前弹反窗口；成功演出是否存在不参与权威命中类型裁决。
---@param attacker_combat USKCombatComponent|nil 攻击者战斗组件，可为空。
---@param _attack_type userdata|number ESKIncomingAttackType 枚举值；当前防御判据尚不按攻击类型区分。
---@return userdata|number contact_result ESKWeaponContactResult 枚举值。
function SKCombatComponent:ResolveIncomingWeaponContact(attacker_combat, _attack_type)
    local survival = self:GetOwner():GetSurvivalComponent()
    if attacker_combat == self or survival == nil or survival:IsAlive() ~= true then
        return UE.ESKWeaponContactResult.Ignored
    end

    local state = self:GetCombatActionState()
    if self:IsPostureBroken() ~= true
        and state == UE.ESKCombatActionState.GuardRaise then
        return UE.ESKWeaponContactResult.Deflected
    end

    if self:IsPostureBroken() ~= true
        and state == UE.ESKCombatActionState.Guarding then
        return UE.ESKWeaponContactResult.Guarded
    end

    return UE.ESKWeaponContactResult.Hit
end

---读取本角色 GAS 攻击力作为当前近战伤害输入，不在武器 C++ 中保留固定伤害常量。
---@param _attack_type ESKIncomingAttackType 来袭语义；具体招式倍率尚未配置，本版不伪造轻重攻击倍率。
---@return number damage 有限非负基础伤害；GAS 未就绪或非法属性时返回负值以使原生入口拒绝。
function SKCombatComponent:ResolveOutgoingHealthDamage(_attack_type)
    local survival = self:GetOwner():GetSurvivalComponent()
    if survival == nil or survival:IsSurvivalReady() ~= true or survival:CanAct() ~= true then
        return -1.0
    end
    local damage = survival:GetSurvivalSnapshot().Attributes.AttackPower
    if type(damage) ~= "number" or damage ~= damage or damage == math.huge or damage < 0.0 then
        return -1.0
    end
    return damage
end

---为统一命中入口构造纯裁决结果；生命与躯干免疫由原生数值策略分别处理，不在此合并成全免疫。
---@param request FSKCombatHitRequest 已通过原生来源、动作、身份和有限值校验的接触请求。
---@return FSKCombatHitEvaluation evaluation 仅包含结果和资源计算语义，不写 GAS、不播放动画。
function SKCombatComponent:EvaluateCombatHit(request)
    local evaluation = UE.FSKCombatHitEvaluation()
    local survival = self:GetOwner():GetSurvivalComponent()
    if survival == nil or survival:IsSurvivalReady() ~= true or survival:IsAlive() ~= true
        or request.SourceActor == nil or not UE.UKismetSystemLibrary.IsValid(request.SourceActor) then
        return evaluation
    end
    if self:IsOwnerDodgingOrStepActive() == true then
        -- 本轮使用已有闪避/垫步状态；精确无敌帧仍由后续动作窗口基建替换，不能宣称还原原版帧数。
        evaluation.bAccepted = true
        evaluation.Outcome = UE.ESKCombatHitOutcome.Dodged
        return evaluation
    end
    local attacker = request.SourceActor:GetComponentByClass(UE.USKCombatComponent.StaticClass())
    local contact = self:ResolveIncomingWeaponContact(attacker, request.AttackType)
    if contact == UE.ESKWeaponContactResult.Ignored then
        return evaluation
    end
    evaluation.bAccepted = true
    if contact == UE.ESKWeaponContactResult.Deflected then
        evaluation.Outcome = UE.ESKCombatHitOutcome.Deflected
        evaluation.TargetPostureReason = "DeflectSuccess"
        evaluation.SourcePostureReason = "AttackDeflected"
    elseif contact == UE.ESKWeaponContactResult.Guarded then
        evaluation.Outcome = UE.ESKCombatHitOutcome.Guarded
        evaluation.TargetPostureReason = "Guarded"
        evaluation.SourcePostureReason = "AttackGuarded"
    else
        evaluation.Outcome = UE.ESKCombatHitOutcome.Hit
        -- Request.HealthDamage 当前就是调用方提供的伤害；未配置护甲曲线前不另造减伤公式。
        evaluation.HealthDamage = request.HealthDamage
        evaluation.TargetPostureReason = "DeflectFailed"
        evaluation.SourcePostureReason = "AttackSuccess"
    end
    return evaluation
end

---只消费已完成提交的真实结果并选择演出，禁止再次调用旧姿态结算路径。
---@param request FSKCombatHitRequest 本次接触上下文；来源动作号用于防止迟到的箭覆盖射手新动作。
---@param result FSKCombatHitResult 已提交的实际伤害与死亡/崩溃标志。
---@param as_source boolean true 表示本组件为攻击方，false 表示守方。
---@return nil completed 动画失败不撤回资源变化，也不把已提交 Deflected 降级成 Hit。
function SKCombatComponent:HandleCombatHitCommitted(request, result, as_source)
    local survival = self:GetOwner():GetSurvivalComponent()
    if result.Code ~= UE.ESKCombatHitResultCode.Committed or self.Runtime == nil
        or survival == nil or survival:CanAct() ~= true then
        return
    end
    if as_source then
        if self:GetActionSerial() ~= request.SourceActionSerial
            or survival:GetLifeSerial() ~= request.SourceLifeSerial then
            return
        end
        if result.Outcome == UE.ESKCombatHitOutcome.Deflected then
            self:StartAttackDeflected()
        end
        return
    end
    if self:GetActionSerial() ~= result.TargetActionSerial then
        return
    end
    if result.Outcome == UE.ESKCombatHitOutcome.Deflected then
        self:StartDeflect()
    elseif result.Outcome == UE.ESKCombatHitOutcome.Guarded then
        self:StartGuardImpact()
    elseif result.Outcome == UE.ESKCombatHitOutcome.Hit and result.AppliedHealthDamage > 0.0 then
        self:StartDeflectFailed(request.AttackType)
    end
end

---接收行为树提交的抽象攻击请求，并复用玩家战斗状态机启动一次地面轻攻击。
---AI 不生成玩家输入序列；请求只在完全中立、动画空闲且架势未崩坏时成立。
---@param attack_request string|userdata 行为树提交的稳定请求名，当前只支持 AutoLight。
---@return boolean started 是否成功开始了一次 AI 攻击。
function SKCombatComponent:RequestAIAttack(attack_request)
    local survival = self:GetOwner():GetSurvivalComponent()
    if tostring(attack_request) ~= "AutoLight"
        or survival == nil or survival:CanAct() ~= true
        or self:GetCombatActionState() ~= UE.ESKCombatActionState.Neutral
        or self:IsCombatAnimationPlaying() == true then
        return false
    end

    if self.Runtime == nil then
        self.Runtime = create_runtime(self:GetNextAttackSide())
    end

    local attack_side = self:GetNextAttackSide()
    if not is_valid_attack_side(attack_side) then
        attack_side = self.Runtime.RestingSide
    end
    if not is_valid_attack_side(attack_side) then
        attack_side = CombatConfig.DefaultSide
    end

    self:StopOwnerAIMovement()
    return self:StartLightAttack(CombatConfig.LightActionIdBySide[attack_side], "Ground")
end

---更新成功弹反后的现有刀侧保留时间，并只在安全的 Neutral 状态恢复默认刀侧。
---倒计时从弹反动画结束后开始；攻击、防御或其他战斗 Montage 活动时不会强制翻侧。
---@param delta_seconds number 本帧时长，单位秒。
---@return nil result 到期且满足安全条件时同步恢复 RestingSide、NextAttackSide 与 CommittedAttackSide。
function SKCombatComponent:UpdateDeflectSideReset(delta_seconds)
    local remaining = self.Runtime.DeflectSideResetRemaining
    if remaining < 0.0 then
        return
    end
    if self:GetCombatActionState() == UE.ESKCombatActionState.DeflectReaction then
        -- 保留时间从成功弹反动画结束后开始，避免动画尚未完成便恢复默认刀侧。
        return
    end

    if remaining > 0.0 then
        remaining = math.max(0.0, remaining - math.max(delta_seconds or 0.0, 0.0))
        self.Runtime.DeflectSideResetRemaining = remaining
    end
    if remaining > 0.0
        or self:GetCombatActionState() ~= UE.ESKCombatActionState.Neutral
        or self:IsCombatAnimationPlaying() == true then
        return
    end

    local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
    self:SetCommittedAttackSide(default_side)
    self.Runtime.DeflectRaiseStartSide = nil
    self.Runtime.DeflectSideResetRemaining = -1.0
end

---播放一个离散全身动作，并用新的 ActionSerial 使旧回调失效。
---@param state userdata|number ESKCombatActionState 动作状态。
---@param action_id string 调试和后续衔接使用的稳定动作 ID。
---@param animation_path string UAnimSequence 对象路径。
---@return boolean started 动作是否成功加载并开始播放。
function SKCombatComponent:StartAction(state, action_id, animation_path)
    self:SetWeaponHitboxActive(false)
    local serial = self:BeginCombatAction(state)
    if self:PlayCombatAnimationByPath(
        animation_path,
        CombatConfig.BlendInTime,
        CombatConfig.BlendOutTime,
        1.0,
        1) ~= true then
        self:InvalidateCombatAction(serial)
        self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
        return false
    end

    self.Runtime.ActionId = action_id
    self.Runtime.ActionSerial = serial
    self.Runtime.bWasAnimationPlaying = true
    self.Runtime.bSideCurveConsumed = false
    return true
end

---按指定动作域的配置启动轻攻击；当前侧保持不变，下一侧等待动画内 AttackSide 提交。
---@param action_id string 对应攻击配置表中的动作 ID。
---@param attack_domain string 动作所属攻击域：Ground、Air 或 Land。
---@return boolean started 动画是否成功开始。
function SKCombatComponent:StartLightAttack(action_id, attack_domain)
    local attack_table = CombatConfig.Attacks
    if attack_domain == "Air" then
        attack_table = CombatConfig.AirAttacks
    elseif attack_domain == "Land" then
        attack_table = CombatConfig.LandAttacks
    end
    local action = attack_table[action_id]
    if action == nil then
        return false
    end

    local start_side = action.StartSide or self.Runtime.RestingSide
    self:SetCommittedAttackSide(start_side)
    self:SetRestingSide(start_side)
    self.Runtime.ActiveAttackKind = "Light"
    self.Runtime.ActiveAttackDomain = attack_domain
    self.Runtime.ComboNextAction = action.NextAction

    if self:StartAction(
        UE.ESKCombatActionState.LightAttack,
        action_id,
        action.AnimationPath) == true then
        return true
    end
    self:ResetAttackRetention(false)
    return false
end

---按输入时锁存的刀侧启动重攻击，并在动画不可打断区间保持当前攻击侧。
---@param side_name userdata|number 输入发生时锁存的 ESKAttackSide 原生枚举。
---@return boolean started 动画是否成功开始。
function SKCombatComponent:StartHeavyAttack(side_name)
    local locked_side = side_name
    if not is_valid_attack_side(locked_side) then
        locked_side = CombatConfig.DefaultSide
    end
    local action = CombatConfig.HeavyBySide[locked_side]
    if action == nil then
        return false
    end

    self:SetCommittedAttackSide(action.StartSide or locked_side)
    self:SetRestingSide(locked_side)
    self.Runtime.ActiveAttackKind = "Heavy"
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = CombatConfig.LightActionIdBySide[locked_side]

    if self:StartAction(
        UE.ESKCombatActionState.HeavyAttack,
        action.ActionId,
        action.AnimationPath) == true then
        return true
    end
    self:ResetAttackRetention(false)
    return false
end

---立即播放防御攻击的共用起手；首版复用同侧重攻击前摇，短按时可切入轻攻击。
---@param pending SKCombatBufferedAttack 防御按下边沿创建的地面攻击候选。
---@return boolean started 共用起手是否成功取代当前 Guard 动作。
function SKCombatComponent:StartGuardAttackStartup(pending)
    local side_name = pending.SideSnapshot
    local startup = CombatConfig.GuardAttackStartupBySide[side_name]
    if startup == nil then
        return false
    end

    self.Runtime.bReturnToGuardAfterAttack = self:IsGuardHeld() == true
    if self.Runtime.bReturnToGuardAfterAttack == true then
        self:SetGuardCombatPosture(false)
    else
        self:ClearCombatPosture()
    end

    local start_side = startup.StartSide or side_name
    self:SetCommittedAttackSide(start_side)
    self:SetRestingSide(start_side)
    self.Runtime.ActiveAttackKind = nil
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = nil
    if self:StartAction(
        UE.ESKCombatActionState.PendingAttack,
        startup.ActionId,
        startup.AnimationPath) == true then
        pending.bGuardStartupPlaying = true
        pending.StartupActionSerial = self.Runtime.ActionSerial
        self.Runtime.PendingAttack = pending
        return true
    end

    self.Runtime.PendingAttack = nil
    self:ResetAttackRetention(false)
    if pending.bStartedFromGuard == true and self:IsGuardHeld() == true then
        local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
        self:SetCommittedAttackSide(defense_side)
        self:SetGuardCombatPosture(false)
        self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
    else
        self:ClearCombatPosture()
        self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    end
    return false
end

---把正在播放的防御共用起手原地提交为重攻击，不重启 Montage 或重置播放位置。
---@param pending SKCombatBufferedAttack 当前仍有效的防御起手候选。
---@return boolean committed 当前动作和同一 ActionSerial 是否成功升级为重攻击。
function SKCombatComponent:CommitGuardAttackStartupToHeavy(pending)
    if pending.bGuardStartupPlaying ~= true
        or pending.StartupActionSerial == nil
        or self:IsActionSerialValid(pending.StartupActionSerial) ~= true
        or self:GetCombatActionState() ~= UE.ESKCombatActionState.PendingAttack
        or self:IsCombatAnimationPlaying() ~= true then
        return false
    end

    local side_name = pending.SideSnapshot
    local action = CombatConfig.HeavyBySide[side_name]
    if action == nil then
        return false
    end

    self:SetCommittedAttackSide(action.StartSide or side_name)
    self:SetRestingSide(side_name)
    self.Runtime.ActiveAttackKind = "Heavy"
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = CombatConfig.LightActionIdBySide[side_name]
    self.Runtime.ActionId = action.ActionId
    self.Runtime.PendingAttack = nil
    self:SetCombatActionState(UE.ESKCombatActionState.HeavyAttack)
    return true
end

---达到长按阈值时原地提交防御起手重攻击；阈值前释放由 Completed 精确判定为轻攻击。
---@return boolean committed 本帧是否把 PendingAttack 提交为 HeavyAttack。
function SKCombatComponent:TryCommitGuardAttackStartup()
    local pending = self.Runtime.PendingAttack
    if pending == nil
        or pending.bGuardStartupPlaying ~= true
        or pending.bHeavyAccepted ~= true
        or self:GetCombatAnimationPosition() < CombatConfig.HeavyHoldThreshold then
        return false
    end
    return self:CommitGuardAttackStartupToHeavy(pending)
end

---开始防御举刀，并按离地状态选择地面或空中动作。
---@param force_air boolean|nil true 表示物理 Jump 已请求，本帧直接使用空中举刀动作。
---@return boolean started 举刀动画是否成功开始。
function SKCombatComponent:StartGuardRaise(force_air)
    self.Runtime.DeflectRaiseStartSide = self.Runtime.RestingSide
    self:ResetAttackRetention(true)
    self:SetGuardCombatPosture(force_air)
    local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
    self:SetCommittedAttackSide(defense_side)
    local is_air_guard = force_air == true or self:IsOwnerFalling() == true
    local action_id = is_air_guard and "Guard_Air_Raise" or "Guard_Raise"
    local animation_path = is_air_guard and CombatConfig.Guard.AirRaise or CombatConfig.Guard.Raise
    if self:StartAction(
        UE.ESKCombatActionState.GuardRaise,
        action_id,
        animation_path) == true then
        return true
    end
    self:ClearCombatPosture()
    self:SetRestingSide(CombatConfig.DefaultSide)
    return false
end

---开始防御收刀；离地时使用空中收刀，防御左侧保持到动画完整结束。
---@return boolean started 收刀动画是否成功开始。
function SKCombatComponent:StartGuardLower()
    self:SetRestingSide(CombatConfig.DefenseSide)
    -- Lower 的 Slot 下方提前切回 Normal，确保动作结束时与 Idle/Locomotion 混合，而不是重新露出 Guard Idle。
    self:ClearCombatPosture()
    local is_air_guard = self:IsOwnerFalling() == true
    local action_id = is_air_guard and "Guard_Air_Lower" or "Guard_Lower"
    local animation_path = is_air_guard and CombatConfig.Guard.AirLower or CombatConfig.Guard.Lower
    if self:StartAction(
        UE.ESKCombatActionState.GuardLower,
        action_id,
        animation_path) == true then
        return true
    end
    self:SetRestingSide(CombatConfig.DefaultSide)
    return false
end

---播放稳定防御被刀命中时的震刀动作，并在动作结束后按防御键状态恢复 Guarding 或 Neutral。
---@return boolean started 防御震刀动作是否成功开始。
function SKCombatComponent:StartGuardImpact()
    if self:GetCombatActionState() ~= UE.ESKCombatActionState.Guarding then
        return false
    end

    self:ResetAttackRetention(true)
    self:SetGuardCombatPosture(false)
    local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
    self:SetCommittedAttackSide(defense_side)
    return self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        "Guard_Impact",
        CombatConfig.GuardImpactAnimation)
end

---只在 Guard Raise 阶段按起始刀侧选择成功弹反动画，并锁存动作结束后的相反刀侧。
---@return boolean started 弹反动画是否成功开始。
function SKCombatComponent:StartDeflect()
    if self:GetCombatActionState() ~= UE.ESKCombatActionState.GuardRaise then
        return false
    end

    local start_side = self.Runtime.DeflectRaiseStartSide or self.Runtime.RestingSide
    local deflect_config = CombatConfig.DeflectBySide[start_side]
    if deflect_config == nil then
        return false
    end

    self:ResetAttackRetention(true)
    self:SetGuardCombatPosture(false)
    local end_side = self:SetRestingSide(deflect_config.EndSide)
    self:SetCommittedAttackSide(end_side)
    self.Runtime.DeflectRaiseStartSide = nil
    self.Runtime.DeflectSideResetRemaining = CombatConfig.DeflectSideResetDelay
    if self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        deflect_config.ActionId,
        deflect_config.AnimationPath) == true then
        return true
    end

    self.Runtime.DeflectSideResetRemaining = -1.0
    self:ClearCombatPosture()
    local restored_side = self:SetRestingSide(start_side)
    self:SetCommittedAttackSide(restored_side)
    return false
end

---中断当前攻击并按已提交的攻击刀侧播放被弹开动作；反应结束后仍保持原刀侧。
---必须读取 CommittedAttackSide，而不是可能已被 AttackSide 曲线提前更新的下一刀侧。
---@return boolean started 对应的攻击被弹开动画是否成功开始。
function SKCombatComponent:StartAttackDeflected()
    local state = self:GetCombatActionState()
    local is_attacking = state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack
        or state == UE.ESKCombatActionState.PendingAttack
    if is_attacking ~= true then
        return false
    end

    local attack_side = self:GetCommittedAttackSide()
    if not is_valid_attack_side(attack_side) then
        attack_side = self.Runtime.RestingSide
    end
    local reaction_config = CombatConfig.AttackDeflectedBySide[attack_side]
    if reaction_config == nil then
        return false
    end

    self:StopOwnerAIMovement()
    self:ClearCombatInputEvents()
    self:ResetAttackRetention(true)
    self:ClearCombatPosture()
    local retained_side = self:SetRestingSide(reaction_config.EndSide)
    self:SetCommittedAttackSide(retained_side)
    self.Runtime.DeflectRaiseStartSide = nil
    self.Runtime.DeflectSideResetRemaining = CombatConfig.DeflectSideResetDelay
    if self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        reaction_config.ActionId,
        reaction_config.AnimationPath) == true then
        return true
    end

    self.Runtime.DeflectSideResetRemaining = -1.0
    return false
end

---按来袭类型选择对应的弹反失败动作；编号与成功弹反保持同段对应关系。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@return boolean started 弹反失败动画是否成功开始。
function SKCombatComponent:StartDeflectFailed(attack_type)
    local chain = CombatConfig.DeflectFailedByType[attack_type]
    local action_id_prefix = CombatConfig.DeflectFailedActionIdByType[attack_type]
    if chain == nil or #chain == 0 or action_id_prefix == nil then
        return false
    end

    local stage = self.Runtime.DeflectStage
    if self.Runtime.LastDeflectType ~= attack_type or stage <= 0 then
        stage = 1
    else
        stage = (stage - 1) % #chain + 1
    end
    self.Runtime.LastDeflectType = attack_type
    self.Runtime.DeflectStage = stage
    self:ResetAttackRetention(true)
    self:ClearCombatPosture()
    local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
    self:SetCommittedAttackSide(defense_side)
    if self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        action_id_prefix .. "_" .. tostring(stage),
        chain[stage]) == true then
        return true
    end

    self:SetRestingSide(CombatConfig.DefaultSide)
    return false
end

---在 Guard Raise 已经成立后尝试消费当前模拟来袭；其他动作阶段绝不判定弹反成功。
---@param input_event FSKCombatInputEvent 启动本次 Guard Raise 的防御按下事件。
---@return boolean deflected 是否成功消费来袭并开始弹反。
function SKCombatComponent:TryStartDeflect(input_event)
    if self:GetCombatActionState() ~= UE.ESKCombatActionState.GuardRaise then
        return false
    end

    local has_context, context = self:GetIncomingAttackAnimationContext()
    if has_context ~= true or context == nil then
        return false
    end
    local consumed, attack_type = self:TryConsumeIncomingAttackAnimation(
        context.ContextSerial,
        input_event.InputSerial,
        input_event.EventTimeSeconds)
    if consumed ~= true then
        return false
    end
    return self:ProcessPostureImpact("DeflectSuccess", attack_type)
end

---读取当前攻击配置是否允许在曲线窗口内判定后续重攻击。
---@return boolean allowed 当前攻击配置是否允许后续重攻击。
function SKCombatComponent:DoesCurrentAttackAllowHeavy()
    if self.Runtime.ActiveAttackDomain ~= "Ground" then
        return false
    end
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.HeavyAttack then
        return true
    end
    local action = CombatConfig.Attacks[self.Runtime.ActionId]
    return action ~= nil and action.bAllowHeavy == true
end

---在物理 Jump 前裁决当前战斗动作是否允许起跳，并在允许时清理被取消动作。
---Neutral 直接放行；地面攻击只读取 CanCancelToJump；普通防御始终放行并报告是否需要续接空中防御。
---@param event_time number Jump Started 的世界绝对时间。
---@return boolean allowed 当前状态是否允许执行物理跳跃。
---@return boolean resume_air_guard 防御键持续按住时是否应在 Jump 后播放空中举刀。
function SKCombatComponent:TryPrepareJump(event_time)
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.Neutral then
        return true, false
    end

    local is_attack = state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack
    if is_attack == true then
        if self:IsOwnerFalling() == true
            or self:SampleActiveSequenceCurveAtTime(
                CurveNames.CanCancelToJump,
                event_time) < CurveThreshold then
            return false, false
        end
    else
        local is_guard = state == UE.ESKCombatActionState.GuardRaise
            or state == UE.ESKCombatActionState.Guarding
            or state == UE.ESKCombatActionState.GuardLower
        if is_guard ~= true then
            return false, false
        end
    end

    local resume_air_guard = not is_attack and self:IsGuardHeld() == true
    self:StopCombatAnimation(CombatConfig.BlendOutTime)
    self:InvalidateCombatAction(self.Runtime.ActionSerial)
    self.Runtime.ActionId = nil
    self.Runtime.bWasAnimationPlaying = false
    self:ResetAttackRetention(true)
    self:ClearCombatPosture()
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    return true, resume_air_guard
end

---根据活动 Sequence 的侧别与防御取消曲线尝试提交下一刀侧。
---AttackSide 只修改下一动作侧；CommittedAttackSide 在当前攻击中始终不变。
---@param side_curve_value number 当前采样到的 AttackSide。
---@param guard_curve_value number 当前采样到的 CanCancelToGuard。
---@return boolean committed 本次采样是否提交了新的下一刀侧。
function SKCombatComponent:TryCommitNextAttackSide(side_curve_value, guard_curve_value)
    if self.Runtime.bSideCurveConsumed == true
        or guard_curve_value < CurveThreshold then
        return false
    end

    local next_side = nil
    if side_curve_value >= CurveThreshold then
        next_side = UE.ESKAttackSide.Right
    elseif side_curve_value <= -CurveThreshold then
        next_side = UE.ESKAttackSide.Left
    end
    if next_side == nil then
        return false
    end

    self:SetRestingSide(next_side)
    self.Runtime.bSideCurveConsumed = true
    return true
end

---按输入事件时间采样侧别曲线，确保同帧输入使用已经开放的下一刀侧。
---@param event_time number 输入事件的世界绝对时间。
---@return nil result 必要时更新下一攻击侧。
function SKCombatComponent:UpdateAttackSideAtTime(event_time)
    self:TryCommitNextAttackSide(
        self:SampleActiveSequenceCurveAtTime(CurveNames.AttackSide, event_time),
        self:SampleActiveSequenceCurveAtTime(CurveNames.CanCancelToGuard, event_time))
end

---处理攻击 Started；活动攻击锁存后续候选，防御地面候选则立即播放共用攻击起手。
---@param input_event FSKCombatInputEvent 攻击按下事件。
---@return nil result 候选写入 Runtime，等待同 Serial 的 Completed。
function SKCombatComponent:HandleAttackStarted(input_event)
    local state = self:GetCombatActionState()
    local is_active_attack = state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack

    if is_active_attack == true then
        self:UpdateAttackSideAtTime(input_event.EventTimeSeconds)
        local can_accept_light = self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanAcceptLightAttack,
            input_event.EventTimeSeconds) >= CurveThreshold
        local can_accept_heavy = self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanAcceptHeavyAttack,
            input_event.EventTimeSeconds) >= CurveThreshold
        self.Runtime.PendingAttack = {
            InputSerial = input_event.InputSerial,
            PressTime = input_event.EventTimeSeconds,
            SideSnapshot = self.Runtime.RestingSide,
            AttackDomain = self.Runtime.ActiveAttackDomain or "Ground",
            LightActionId = self.Runtime.ComboNextAction,
            bLightAccepted = self.Runtime.ComboNextAction ~= nil and can_accept_light,
            bHeavyAccepted = self:DoesCurrentAttackAllowHeavy() and can_accept_heavy,
            -- 防御起手的攻击链在后续轻攻击候选中继续携带回防意图，直到 Guard Completed 显式清除。
            bStartedFromGuard = self.Runtime.bReturnToGuardAfterAttack == true,
            bGuardStartupPlaying = false,
            StartupActionSerial = nil,
        }
        return
    end

    local is_guard_state = state == UE.ESKCombatActionState.GuardRaise
        or state == UE.ESKCombatActionState.Guarding
        or state == UE.ESKCombatActionState.GuardLower
    if state ~= UE.ESKCombatActionState.Neutral and is_guard_state ~= true then
        return
    end

    if is_guard_state ~= true then
        self:ResetAttackRetention(false)
        self:BeginCombatAction(UE.ESKCombatActionState.PendingAttack)
    end
    local attack_domain = "Ground"
    local initial_side = is_guard_state and CombatConfig.DefenseSide or CombatConfig.DefaultSide
    local light_action_id = CombatConfig.LightActionIdBySide[initial_side]
    if self:IsOwnerFalling() == true then
        attack_domain = "Air"
        light_action_id = "Air_Combo_01"
    end
    local pending = {
        InputSerial = input_event.InputSerial,
        PressTime = input_event.EventTimeSeconds,
        SideSnapshot = initial_side,
        AttackDomain = attack_domain,
        LightActionId = light_action_id,
        bLightAccepted = true,
        bHeavyAccepted = attack_domain == "Ground",
        bStartedFromGuard = is_guard_state,
        bGuardStartupPlaying = false,
        StartupActionSerial = nil,
    }
    if is_guard_state == true then
        if attack_domain == "Air" then
            -- 空中攻击不支持重击，无需等待 Completed，按下边沿即可立即进入首段轻攻击。
            self:ResolveAttackInput(pending, false)
        else
            self:StartGuardAttackStartup(pending)
        end
        return
    end
    self.Runtime.PendingAttack = pending
end

---执行已经完成长短按判定的攻击；有效窗口内立即混合到下一动作。
---@param pending SKCombatBufferedAttack Started 时锁存的候选。
---@param is_heavy boolean 是否达到重攻击长按阈值。
---@return nil result 对应轻击或重击被立即启动。
function SKCombatComponent:ResolveAttackInput(pending, is_heavy)
    local kind = "Light"
    local action_id = pending.LightActionId
    if is_heavy == true and pending.bHeavyAccepted == true then
        kind = "Heavy"
    elseif pending.bLightAccepted ~= true or action_id == nil then
        return
    end

    self.Runtime.bReturnToGuardAfterAttack = pending.bStartedFromGuard == true
        and self:IsGuardHeld() == true
    if self.Runtime.bReturnToGuardAfterAttack == true then
        self:SetGuardCombatPosture(false)
    else
        self:ClearCombatPosture()
    end

    if kind == "Heavy"
        and pending.bGuardStartupPlaying == true
        and self:CommitGuardAttackStartupToHeavy(pending) == true then
        return
    end
    if kind == "Heavy" then
        self:StartHeavyAttack(pending.SideSnapshot)
    else
        self:StartLightAttack(action_id, pending.AttackDomain)
    end
end

---检测活动空中攻击的落地边沿，并立即切换到同段落地攻击。
---只有 Air 域的攻击会触发本转换；普通跳跃落地继续使用原有移动动画逻辑。
---@return boolean transitioned 本帧是否成功开始了配对落地攻击。
function SKCombatComponent:TryTransitionAirAttackToLand()
    if self.Runtime.ActiveAttackDomain ~= "Air"
        or self:IsOwnerFalling() == true then
        return false
    end

    local land_action_id = CombatConfig.AirToLand[self.Runtime.ActionId]
    if land_action_id == nil then
        return false
    end

    self.Runtime.PendingAttack = nil
    return self:StartLightAttack(land_action_id, "Land")
end

---处理攻击 Completed，仅消费相同 InputSerial，并在有效窗口内立即切换动作。
---@param input_event FSKCombatInputEvent 攻击释放事件。
---@return nil result 成功时立即启动对应的下一攻击。
function SKCombatComponent:HandleAttackCompleted(input_event)
    local pending = self.Runtime.PendingAttack
    if pending == nil or pending.InputSerial ~= input_event.InputSerial then
        return
    end
    self.Runtime.PendingAttack = nil

    local is_heavy = input_event.HoldDuration >= CombatConfig.HeavyHoldThreshold
    self:ResolveAttackInput(pending, is_heavy)
end

---处理防御 Started：先实际进入 Guard Raise，再允许当前模拟来袭在 Raise 阶段判定成功。
---攻击取消仍严格采样动画曲线；未能进入 Raise 时不会越过当前动作直接弹反。
---@param input_event FSKCombatInputEvent 防御按下事件。
---@return nil result 根据当前动作启动 Guard Raise，并在 Raise 成功建立后尝试弹反。
function SKCombatComponent:HandleGuardStarted(input_event)
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.Neutral
        or state == UE.ESKCombatActionState.PendingAttack then
        if self:StartGuardRaise() == true then
            self:TryStartDeflect(input_event)
        end
        return
    end
    if (state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack)
        and self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanCancelToGuard,
            input_event.EventTimeSeconds) >= CurveThreshold then
        if self:StartGuardRaise() == true then
            self:TryStartDeflect(input_event)
        end
    end
end

---处理防御 Completed，举刀或稳定防御状态都进入完整收刀动作。
---@return nil result 必要时启动 Guard Lower。
function SKCombatComponent:HandleGuardCompleted()
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.PendingAttack
        and self.Runtime.PendingAttack ~= nil
        and self.Runtime.PendingAttack.bStartedFromGuard == true then
        self.Runtime.bReturnToGuardAfterAttack = false
        self:ClearCombatPosture()
        return
    end
    if state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack then
        self.Runtime.bReturnToGuardAfterAttack = false
        self:ClearCombatPosture()
        return
    end
    if state == UE.ESKCombatActionState.GuardRaise
        or state == UE.ESKCombatActionState.Guarding then
        self:StartGuardLower()
    end
end

---分发一个带边沿、序列号和时间戳的战斗输入事件。
---@param input_event FSKCombatInputEvent C++ 输入层发布的不可变事件。
---@return nil result 事件由对应处理器消费或安全丢弃。
function SKCombatComponent:HandleInputEvent(input_event)
    if input_event.Action == UE.ESKCombatInputAction.Attack then
        if input_event.Phase == UE.ESKCombatInputPhase.Started then
            self:HandleAttackStarted(input_event)
        else
            self:HandleAttackCompleted(input_event)
        end
        return
    end
    if input_event.Phase == UE.ESKCombatInputPhase.Started then
        self:HandleGuardStarted(input_event)
    else
        self:HandleGuardCompleted()
    end
end

---处理未被续段切换的攻击动画自然结束，并立即恢复默认状态。
---@return nil result 清空当前攻击并恢复 Neutral。
function SKCombatComponent:HandleAttackAnimationFinished()
    local return_to_guard = self.Runtime.bReturnToGuardAfterAttack == true
        and self:IsGuardHeld() == true
    self.Runtime.PendingAttack = nil
    self.Runtime.ActionId = nil

    self:ResetAttackRetention(false)
    if return_to_guard == true then
        local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
        self:SetCommittedAttackSide(defense_side)
        self:SetGuardCombatPosture(false)
        self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
    else
        self:ClearCombatPosture()
        self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    end
end

---处理当前全身动画自然结束后的状态收敛。
---@return nil result 进入稳定 Guard、收刀、下一攻击或 Neutral。
function SKCombatComponent:HandleAnimationFinished()
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.PostureBroken then
        -- 躯干崩溃流程由 Survival 收尾；Combat 不因动画结束擅自恢复行动。
        return
    end
    if state == UE.ESKCombatActionState.PendingAttack then
        local pending = self.Runtime.PendingAttack
        local return_to_guard = pending ~= nil
            and pending.bStartedFromGuard == true
            and self:IsGuardHeld() == true
        self.Runtime.PendingAttack = nil
        self.Runtime.ActionId = nil
        self:ResetAttackRetention(false)
        if return_to_guard == true then
            local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
            self:SetCommittedAttackSide(defense_side)
            self:SetGuardCombatPosture(false)
            self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        else
            self:ClearCombatPosture()
            self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
        end
        return
    end
    if state == UE.ESKCombatActionState.GuardRaise then
        self.Runtime.ActionId = nil
        self.Runtime.DeflectRaiseStartSide = nil
        if self:IsGuardHeld() == true then
            self:SetGuardCombatPosture(false)
            self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        else
            self:ClearCombatPosture()
            self:StartGuardLower()
        end
        return
    end
    if state == UE.ESKCombatActionState.DeflectReaction then
        local should_preserve_success_side =
            self.Runtime.DeflectSideResetRemaining >= 0.0
        self.Runtime.ActionId = nil
        if self:IsGuardHeld() == true then
            self:SetGuardCombatPosture(false)
            self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        else
            self:ClearCombatPosture()
            self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
            if should_preserve_success_side ~= true then
                local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
                self:SetCommittedAttackSide(default_side)
            end
        end
        return
    end
    if state == UE.ESKCombatActionState.GuardLower then
        self.Runtime.ActionId = nil
        self:ClearCombatPosture()
        self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
        local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
        self:SetCommittedAttackSide(default_side)
        self.Runtime.DeflectSideResetRemaining = -1.0
        return
    end
    if state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack then
        self:HandleAttackAnimationFinished()
        return
    end

    self.Runtime.ActionId = nil
    self:ClearCombatPosture()
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
end

---同步可持续防御姿态的地面/空中分支；离散动作状态仍由各自的结束回调收敛。
---该同步覆盖持续防御、弹反底层和从防御起手的攻击，处理走下边缘等非 Jump 导致的 Falling 变化。
---@return nil result 必要时更新独立 CombatPostureState。
function SKCombatComponent:UpdateSustainedCombatPosture()
    local state = self:GetCombatActionState()
    local should_keep_guard = state == UE.ESKCombatActionState.GuardRaise
        or state == UE.ESKCombatActionState.Guarding
        or (state == UE.ESKCombatActionState.PendingAttack
            and self.Runtime.PendingAttack ~= nil
            and self.Runtime.PendingAttack.bStartedFromGuard == true
            and self.Runtime.bReturnToGuardAfterAttack == true)
        or (state == UE.ESKCombatActionState.DeflectReaction and self:IsGuardHeld() == true)
        or ((state == UE.ESKCombatActionState.LightAttack
            or state == UE.ESKCombatActionState.HeavyAttack)
            and self.Runtime.bReturnToGuardAfterAttack == true)
    if should_keep_guard == true then
        self:SetGuardCombatPosture(false)
    end
end

---每帧采样活动 Sequence 曲线，在可取消阶段提交下一刀侧。
---@return nil result 必要时更新下一攻击侧。
function SKCombatComponent:UpdateAttackSideCurve()
    local state = self:GetCombatActionState()
    if state ~= UE.ESKCombatActionState.LightAttack
        and state ~= UE.ESKCombatActionState.HeavyAttack then
        return
    end
    self:TryCommitNextAttackSide(
        self:SampleActiveSequenceCurve(CurveNames.AttackSide),
        self:SampleActiveSequenceCurve(CurveNames.CanCancelToGuard))
end

---每帧采样原版 TAE 攻击框曲线，只在 Light/Heavy 动作的非零窗口开启武器碰撞。
---状态离开攻击或曲线缺失时失败关闭；每次上升沿都会清空目标去重集合，允许下一刀重新命中。
---@return nil result 必要时提交一次武器碰撞开启或关闭边沿。
function SKCombatComponent:UpdateWeaponHitboxCurve()
    local state = self:GetCombatActionState()
    local is_attacking = state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack
    if is_attacking ~= true then
        self:SetWeaponHitboxActive(false)
        return
    end

    local curve_value = self:SampleActiveSequenceCurve(CurveNames.AttackHitbox)
    self:SetWeaponHitboxActive(curve_value >= CurveThreshold)
end

---每帧先检查 Survival 行动许可，再消费输入、提交攻击曲线状态并检测自有 Montage 结束。
---该入口由原生组件显式 require 后调用，不依赖纯原生组件的 Blueprint ReceiveTick 分发。
---@param delta_seconds number 本帧时长，单位秒。
---@return boolean handled 始终返回 true，表示本帧战斗动画逻辑已执行。
function SKCombatComponent:HandleCombatTick(delta_seconds)
    if self.Runtime == nil then
        self.Runtime = create_runtime(self:GetNextAttackSide())
    end
    local survival = self:GetOwner():GetSurvivalComponent()
    if survival == nil or survival:CanAct() ~= true then
        -- 未就绪、死亡、回生或崩溃期间丢弃输入；演出和恢复由 Survival 独立推进。
        self:SetWeaponHitboxActive(false)
        self:ClearCombatInputEvents()
        self.Runtime.bWasAnimationPlaying = self:IsCombatAnimationPlaying() == true
        return true
    end
    self:TryTransitionAirAttackToLand()
    self:UpdateSustainedCombatPosture()
    while true do
        local has_event, input_event = self:ConsumeCombatInputEvent()
        if has_event ~= true or input_event == nil then
            break
        end
        self:HandleInputEvent(input_event)
    end

    self:TryCommitGuardAttackStartup()
    self:UpdateWeaponHitboxCurve()
    self:UpdateAttackSideCurve()
    local is_playing = self:IsCombatAnimationPlaying() == true
    if self.Runtime.bWasAnimationPlaying == true and is_playing ~= true then
        self:HandleAnimationFinished()
        is_playing = self:IsCombatAnimationPlaying() == true
    end
    self.Runtime.bWasAnimationPlaying = is_playing
    self:UpdateDeflectSideReset(delta_seconds)
    return true
end

return SKCombatComponent
