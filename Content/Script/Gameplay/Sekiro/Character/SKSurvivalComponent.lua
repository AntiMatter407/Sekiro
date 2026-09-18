-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKSurvivalComponent。
-- GAS 唯一持有生命与全部躯干参数；本模块只编排增长、恢复、崩溃演出及死亡/回生后的动作清理。
-- C++ 负责状态转换和令牌校验；资源通知中不再次写 GAS，后续提交移到安全 Tick。

local CharacterAttributes = require("Gameplay.Sekiro.AbilitySystem.CharacterAttributes")
local AnimAssets = require("Animation.Sekiro.AnimAssets")
local LuaLog = require("Gameplay.Base.LuaLog")

---@class SKSurvivalRuntime
---@field bEventsBound boolean 当前 Lua 实例是否已经绑定原生通知。
---@field bEnding boolean 是否已开始离场，禁止旧回调继续编排。
---@field bReadyHandled boolean 是否已经消费完整初始快照。
---@field RecoveryEligibleTime number 连续满足自然恢复条件的累计秒数，不是角色配置。
---@field LastHealth number|nil 仅用于检测资源通知边沿，不作为提交数值依据。
---@field LastPosture number|nil 仅用于检测资源通知边沿，不作为提交数值依据。
---@field BreakLifeSerial number 当前待处理崩溃所属生命轮次。
---@field BreakTransitionSerial number 当前待处理崩溃过程编号。
---@field BrokenElapsedTime number 当前崩溃流程已观察到的持续秒数。
---@field bBreakPresentationStarted boolean 是否已经尝试播放本次崩溃演出。
---@field bBreakResourcesReset boolean 本次崩溃是否已成功清空资源。
---@field DeathLifeSerial number 待收尾死亡所属的生命轮次。
---@field DeathTransitionSerial number 待收尾死亡的过程编号。
---@field bPendingResume boolean 是否等待安全调用点恢复动作与自己持有的输入锁。
---@field StoppedBrain UBrainComponent|nil 本模块实际停止的脑组件，仅恢复同一个仍有效的组件。
---@field LastTransitionFailure string|nil 上次记录的转换失败键，避免每帧刷屏。
---@field bRecoveryRangeWarning boolean 是否已记录当前恢复倍率倒置问题。

---@class SKSurvivalComponent: USKSurvivalComponent
---@field Runtime SKSurvivalRuntime
local SKSurvivalComponent = UnLua.Class()

local NotReadyInputLock = "Survival.NotReady"
local DeathInputLock = "Survival.Death"
local PostureInputLock = "Survival.PostureBroken"

-- 映射只包含语义到 GAS 字段名，不保存基础增长或倍率数值。
local GainAttributes = {
    DeflectSuccess = "PostureGainDeflectSuccess",
    Guarded = "PostureGainGuarded",
    DeflectFailed = "PostureGainDeflectFailed",
    AttackSuccess = "PostureGainAttackSuccess",
    AttackGuarded = "PostureGainAttackGuarded",
    AttackDeflected = "PostureGainAttackDeflected",
}

---检查脚本计算输入是否为有限数，阻止 NaN 或无穷值进入恢复计时和 GE 请求。
---@param value number|nil 待检查的数值。
---@return boolean valid 是否为有限 Lua 数值。
local function is_finite(value)
    return type(value) == "number" and value == value
        and value ~= math.huge and value ~= -math.huge
end

---把计算结果限制到数学闭区间；边界不是角色调参副本。
---@param value number 已检查的有限输入。
---@param minimum number 闭区间下界。
---@param maximum number 闭区间上界。
---@return number bounded 限制后的结果。
local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(value, maximum))
end

---根据通用攻击类型选择 GAS 强度字段；未知枚举失败关闭，不猜测轻攻击倍率。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@return string|nil attribute_name 对应 GAS 字段名，未知类型返回 nil。
local function strength_attribute(attack_type)
    if attack_type == UE.ESKIncomingAttackType.Light then
        return "PostureStrengthLight"
    end
    if attack_type == UE.ESKIncomingAttackType.Heavy then
        return "PostureStrengthHeavy"
    end
    if attack_type == UE.ESKIncomingAttackType.Thrust then
        return "PostureStrengthThrust"
    end
    if attack_type == UE.ESKIncomingAttackType.Special then
        return "PostureStrengthSpecial"
    end
    return nil
end

---判断 GE 数值请求是否被接受，包括资源已经达到目标导致的无变化。
---@param result FSKNumericResult 原生数值提交回执。
---@return boolean accepted 是否成功提交或合法地无变化。
local function numeric_accepted(result)
    return result.Code == UE.ESKNumericResultCode.Applied
        or result.Code == UE.ESKNumericResultCode.NoChange
end

---判断原生转换请求是否已成功，不把资源写入失败当作状态完成。
---@param result FSKSurvivalTransitionResult 原生状态转换回执。
---@return boolean accepted 是否已应用或已经处于该请求的完成状态。
local function transition_accepted(result)
    return result.Code == UE.ESKSurvivalResultCode.Applied
        or result.Code == UE.ESKSurvivalResultCode.NoChange
end

---检查当前原生令牌与 Lua 保存的纯序号是否对应；不持有回调中临时结构体的引用。
---@param token FSKSurvivalTransitionToken 从当前组件 getter 取得的原生令牌。
---@param life_serial number 本流程开始时的生命轮次。
---@param transition_serial number 本流程开始时的过程编号。
---@return boolean matches 是否仍是同一有效流程。
local function token_matches(token, life_serial, transition_serial)
    return transition_serial > 0 and token.LifeSerial == life_serial
        and token.TransitionSerial == transition_serial
end

---创建实例私有的编排状态，不访问 Owner、配置资产或 GAS。
---@param _initializer table|nil UnLua 初始化参数，此处不消费。
---@return nil result 仅创建计时、事件和流程序号缓存。
function SKSurvivalComponent:Initialize(_initializer)
    self.Runtime = {
        bEventsBound = false,
        bEnding = false,
        bReadyHandled = false,
        RecoveryEligibleTime = 0.0,
        LastHealth = nil,
        LastPosture = nil,
        BreakLifeSerial = 0,
        BreakTransitionSerial = 0,
        BrokenElapsedTime = 0.0,
        bBreakPresentationStarted = false,
        bBreakResourcesReset = false,
        DeathLifeSerial = 0,
        DeathTransitionSerial = 0,
        bPendingResume = false,
        StoppedBrain = nil,
        LastTransitionFailure = nil,
        bRecoveryRangeWarning = false,
    }
end

---记录需要人工关注的流程失败；正常等待最短崩溃时长不作为错误，重复失败只记录一次。
---@param operation string 发起转换的语义名称。
---@param result FSKSurvivalTransitionResult 原生返回的明确结果和令牌。
---@return nil result 不重试、不改变资源或状态。
function SKSurvivalComponent:ReportTransitionFailure(operation, result)
    if transition_accepted(result) or result.Code == UE.ESKSurvivalResultCode.Blocked then
        return
    end
    local key = operation .. ":" .. tostring(result.Token.LifeSerial)
        .. ":" .. tostring(result.Token.TransitionSerial) .. ":" .. tostring(result.Code)
    if self.Runtime.LastTransitionFailure ~= key then
        self.Runtime.LastTransitionFailure = key
        LuaLog.Debug(true, "SKSurvivalComponent", operation, "状态转换未完成，保持门禁：" .. key)
    end
end

---取得当前 Owner 的 AI 脑组件；玩家角色没有 AIController 时安全返回空。
---@return UBrainComponent|nil brain 当前 AIController 的脑组件，不创建或替换控制器。
function SKSurvivalComponent:GetOwnerBrainForScript()
    local controller = UE.UAIBlueprintHelperLibrary.GetAIController(self:GetOwner())
    return controller ~= nil and controller.BrainComponent or nil
end

---停止当前正在运行的 AI 逻辑并记录实际停止对象，避免回生时误启动原本禁用的 AI。
---@return nil result 不修改 Blackboard、目标或 Boss 阶段。
function SKSurvivalComponent:StopOwnerBrainForScript()
    local brain = self:GetOwnerBrainForScript()
    if brain ~= nil and brain:IsRunning() == true then
        self.Runtime.StoppedBrain = brain
        brain:StopLogic("Survival")
    end
end

---按原因锁定当前角色并停止导航；需要时通过 Combat 的通用接口中断旧动作。
---@param reason string 本模块拥有的稳定输入锁原因。
---@param interrupt boolean 是否使旧 ActionSerial 失效并清理动画/输入。
---@return nil result 只执行外部编排，不在数值事件中写入 GAS。
function SKSurvivalComponent:SuppressGameplay(reason, interrupt)
    local combat = self:GetOwner():GetCombatComponent()
    if combat ~= nil then
        combat:SetOwnerExternalInputLock(reason, true)
        if interrupt == true then
            combat:InterruptForExternalTransition()
        else
            combat:DeactivateOwnerWeaponHitbox()
            combat:StopOwnerAIMovement()
        end
    end
    self:StopOwnerBrainForScript()
    self.Runtime.RecoveryEligibleTime = 0.0
end

---记录新崩溃的纯序号并立即失效旧动作，资源重置和演出留到安全 Tick。
---@param token FSKSurvivalTransitionToken 当前组件发出的崩溃令牌，仅当场读取标量字段。
---@return nil result 更新流程缓存，不保存临时结构体引用。
function SKSurvivalComponent:QueuePostureBreak(token)
    self.Runtime.BreakLifeSerial = token.LifeSerial
    self.Runtime.BreakTransitionSerial = token.TransitionSerial
    self.Runtime.BrokenElapsedTime = 0.0
    self.Runtime.bBreakPresentationStarted = false
    self.Runtime.bBreakResourcesReset = false
    self.Runtime.bPendingResume = false
    self:SuppressGameplay(PostureInputLock, true)
end

---消费完整初始快照，也兼容外部显式初始化先于 Lua 绑定的顺序。
---@return nil result 初始死亡只锁定角色，不补发死亡或奖励事件。
function SKSurvivalComponent:HandleSurvivalReady()
    if self.Runtime.bEnding == true or self.Runtime.bReadyHandled == true
        or self:IsSurvivalReady() ~= true then
        return
    end
    self.Runtime.bReadyHandled = true
    local snapshot = self:GetSurvivalSnapshot()
    self.Runtime.LastHealth = snapshot.Attributes.Health
    self.Runtime.LastPosture = snapshot.Attributes.Posture
    if self:IsAlive() ~= true then
        self:SuppressGameplay(DeathInputLock, true)
        if self:GetLifeState() == UE.ESKLifeState.Dying then
            local token = self:GetDeathToken()
            self.Runtime.DeathLifeSerial = token.LifeSerial
            self.Runtime.DeathTransitionSerial = token.TransitionSerial
        end
    elseif self:IsPostureBroken() == true then
        self:QueuePostureBreak(self:GetPostureBreakToken())
    else
        -- 属性等待期间可能已经暂停 AI；就绪后的恢复也必须离开原生通知栈。
        self.Runtime.bPendingResume = true
    end
    local combat = self:GetOwner():GetCombatComponent()
    if combat ~= nil then
        combat:SetOwnerExternalInputLock(NotReadyInputLock, false)
    end
end

---生命减少时重新等待自然躯干恢复；只比较通知缓存，不将缓存用于数值提交。
---@param current number 当前已提交生命值。
---@param _maximum number 当前生命上限，本处理器不参与 UI 更新。
---@param _ratio number 当前生命比例，本版不按生命比例改变恢复倍率。
---@return nil result 必要时清空恢复计时。
function SKSurvivalComponent:HandleHealthChanged(current, _maximum, _ratio)
    if self.Runtime.LastHealth ~= nil and current < self.Runtime.LastHealth then
        self.Runtime.RecoveryEligibleTime = 0.0
    end
    self.Runtime.LastHealth = current
end

---躯干增加时重新等待恢复，兼容直接 ASC 数值请求和复合伤害入口。
---@param current number 当前已提交躯干积累。
---@param _maximum number 当前躯干上限，此处理器不判断崩溃。
---@param _ratio number 当前躯干比例，此处理器不保存比例副本。
---@return nil result 只更新通知缓存与累计时间。
function SKSurvivalComponent:HandlePostureChanged(current, _maximum, _ratio)
    if self.Runtime.LastPosture ~= nil and current > self.Runtime.LastPosture then
        self.Runtime.RecoveryEligibleTime = 0.0
    end
    self.Runtime.LastPosture = current
end

---处理死亡边沿：立即锁定动作，把死亡收尾延迟到原生资源调用栈之外。
---@param event FSKSurvivalTransitionEvent 原生死亡事件，含对应轮次和过程序号。
---@return nil result 不销毁角色、不发奖励、不扣回生次数。
function SKSurvivalComponent:HandleDeathStarted(event)
    self.Runtime.DeathLifeSerial = event.Token.LifeSerial
    self.Runtime.DeathTransitionSerial = event.Token.TransitionSerial
    self.Runtime.bPendingResume = false
    self:SuppressGameplay(DeathInputLock, true)
end

---回生准备阶段继续保持死亡输入锁，外部资格/演出流程负责显式调用 CompleteRevive。
---@param _event FSKSurvivalTransitionEvent 回生开始事件，此处理器不消费费用或恢复比例。
---@return nil result 清理旧流程但不自动完成回生。
function SKSurvivalComponent:HandleReviveStarted(_event)
    self.Runtime.DeathTransitionSerial = 0
    self.Runtime.bPendingResume = false
    self:SuppressGameplay(DeathInputLock, true)
end

---回生资源提交成功后安排动作恢复，避免在原生事件栈中重启 AI 后递归发起资源请求。
---@param _event FSKSurvivalTransitionEvent 已成功进入新生命轮次的通知。
---@return nil result 只安排下一安全 Tick 的动作与输入恢复。
function SKSurvivalComponent:HandleRevived(_event)
    self.Runtime.DeathTransitionSerial = 0
    self.Runtime.BreakTransitionSerial = 0
    self.Runtime.RecoveryEligibleTime = 0.0
    self.Runtime.bPendingResume = true
end

---回生取消后保持死亡流程收敛，不把取消解释为新一轮死亡或再次扣费。
---@param _event FSKSurvivalTransitionEvent 已取消的回生过程通知。
---@return nil result 保持本组件的死亡输入锁。
function SKSurvivalComponent:HandleReviveCancelled(_event)
    self.Runtime.bPendingResume = false
    self:SuppressGameplay(DeathInputLock, true)
end

---接收原生躯干崩溃边沿，不在委托回调中执行 ResetBrokenPosture。
---@param event FSKSurvivalTransitionEvent 本轮崩溃的不可变事实。
---@return nil result 保存纯序号并中断旧战斗动作。
function SKSurvivalComponent:HandlePostureBroken(event)
    self:QueuePostureBreak(event.Token)
end

---资源和状态都恢复成功后安排安全的动作恢复；旧令牌已由 C++ 验证。
---@param _event FSKSurvivalTransitionEvent 本次已完成的崩溃流程。
---@return nil result 清除流程缓存，输入锁稍后按原因释放。
function SKSurvivalComponent:HandlePostureRecovered(_event)
    self.Runtime.BreakTransitionSerial = 0
    self.Runtime.RecoveryEligibleTime = 0.0
    self.Runtime.bPendingResume = true
end

---取消崩溃时只移除自己拥有的崩溃锁，绝不把死亡取消当成正常恢复。
---@param _event FSKSurvivalTransitionEvent 崩溃被死亡等原因取消的事实。
---@return nil result 失效旧崩溃缓存，保留死亡和其他系统输入锁。
function SKSurvivalComponent:HandlePostureBreakCancelled(_event)
    self.Runtime.BreakTransitionSerial = 0
    self.Runtime.bPendingResume = false
    local combat = self:GetOwner():GetCombatComponent()
    if combat ~= nil then
        combat:SetOwnerExternalInputLock(PostureInputLock, false)
    end
end

---自然恢复只消费最新 GAS 参数，Lua 只持有累计时间；不复制或回写任何角色参数。
---@param delta_seconds number 已验证为正有限的本帧秒数。
---@return nil result 条件满足时提交一次即时恢复 GE。
function SKSurvivalComponent:UpdatePostureRecovery(delta_seconds)
    local combat = self:GetOwner():GetCombatComponent()
    local attributes = self:GetSurvivalSnapshot().Attributes
    local can_recover = combat ~= nil
        and combat:GetCombatActionState() == UE.ESKCombatActionState.Neutral
        and combat:IsCombatAnimationPlaying() ~= true
        and combat:IsOwnerSprinting() ~= true
        and combat:IsOwnerDodgingOrStepActive() ~= true
    if attributes.Posture <= 0.0 or can_recover ~= true then
        self.Runtime.RecoveryEligibleTime = 0.0
        return
    end
    self.Runtime.RecoveryEligibleTime = self.Runtime.RecoveryEligibleTime + delta_seconds
    local elapsed = self.Runtime.RecoveryEligibleTime - attributes.PostureRecoveryDelay
    if elapsed <= 0.0 then
        return
    end
    local alpha = 1.0
    if attributes.PostureRecoveryRampDuration > 0.0 then
        alpha = clamp(elapsed / attributes.PostureRecoveryRampDuration, 0.0, 1.0)
    end
    local maximum_scale = attributes.PostureRecoveryMaxRateScale
    if attributes.PostureRecoveryMinRateScale > maximum_scale then
        if self.Runtime.bRecoveryRangeWarning ~= true then
            LuaLog.Debug(true, "SKSurvivalComponent", "UpdatePostureRecovery",
                "GAS 最低恢复倍率大于最高倍率，本次计算限制到最高倍率，不回写属性。")
            self.Runtime.bRecoveryRangeWarning = true
        end
    else
        self.Runtime.bRecoveryRangeWarning = false
    end
    local minimum_scale = math.min(attributes.PostureRecoveryMinRateScale, maximum_scale)
    local scale = minimum_scale + (maximum_scale - minimum_scale) * alpha
    local amount = attributes.PostureRecoveryRate * scale * delta_seconds
    if is_finite(amount) and amount > 0.0 then
        self:RestorePosture(amount, nil)
    end
end

---在安全 Tick 推进当前崩溃：校验令牌、清空资源、播放一次演出并等待恢复条件。
---@param delta_seconds number 已验证的本帧秒数。
---@return nil result 只有原生恢复操作成功后才退出 Broken。
function SKSurvivalComponent:UpdatePostureBreak(delta_seconds)
    local token = self:GetPostureBreakToken()
    if not token_matches(token, self.Runtime.BreakLifeSerial, self.Runtime.BreakTransitionSerial) then
        self:QueuePostureBreak(token)
    end
    if self.Runtime.BreakTransitionSerial <= 0 then
        return
    end
    self:SuppressGameplay(PostureInputLock, false)
    local combat = self:GetOwner():GetCombatComponent()
    local attributes = self:GetSurvivalSnapshot().Attributes
    if self.Runtime.bBreakResourcesReset ~= true then
        local result = self:ResetBrokenPosture(token)
        self.Runtime.bBreakResourcesReset = transition_accepted(result)
        self:ReportTransitionFailure("ResetBrokenPosture", result)
    end
    if self.Runtime.bBreakPresentationStarted ~= true and combat ~= nil then
        local action_serial = combat:BeginSurvivalPresentation(token, UE.ESKCombatActionState.PostureBroken)
        if action_serial > 0 then
            self.Runtime.bBreakPresentationStarted = true
            combat:PlayCombatAnimationByPath(
                AnimAssets.PostureBreak.Default,
                attributes.PostureBreakBlendInTime,
                attributes.PostureBreakBlendOutTime,
                1.0,
                1)
        end
    end
    self.Runtime.BrokenElapsedTime = self.Runtime.BrokenElapsedTime + delta_seconds
    local animation_finished = combat == nil or combat:IsCombatAnimationPlaying() ~= true
    if self.Runtime.bBreakResourcesReset == true
        and self.Runtime.BrokenElapsedTime >= attributes.PostureBreakMinimumDuration
        and animation_finished == true then
        self:ReportTransitionFailure("CompletePostureRecovery", self:CompletePostureRecovery(token))
    end
end

---资源与状态都完成后，只解除本模块的锁并恢复原本由本模块停止的同一 AI 脑组件。
---@return nil result 不解锁剧情等其他原因，不启动原本就未运行的 AI。
function SKSurvivalComponent:ResumeGameplayAfterTransition()
    if self.Runtime.bPendingResume ~= true or self:CanAct() ~= true then
        return
    end
    local combat = self:GetOwner():GetCombatComponent()
    if combat ~= nil then
        if combat:ResetAfterExternalTransition() ~= true then
            return
        end
        combat:SetOwnerExternalInputLock(PostureInputLock, false)
        combat:SetOwnerExternalInputLock(DeathInputLock, false)
        combat:SetOwnerExternalInputLock(NotReadyInputLock, false)
    end
    self.Runtime.bPendingResume = false
    local stopped_brain = self.Runtime.StoppedBrain
    self.Runtime.StoppedBrain = nil
    local current_brain = self:GetOwnerBrainForScript()
    if stopped_brain ~= nil and current_brain == stopped_brain
        and UE.UKismetSystemLibrary.IsValid(stopped_brain)
        and stopped_brain:IsRunning() ~= true then
        stopped_brain:RestartLogic()
    end
end

---只读计算本次躯干增长，供统一命中入口在同一笔 GE 中提交生命与躯干。
---@param reason string|userdata 攻防结果 FName，包含防御结果和攻击方反馈。
---@param attack_type userdata|number 通用攻击类型枚举。
---@param additional_damage number 请求携带的额外最终躯干伤害，有限非负；在成功弹反/攻击反馈封顶之前相加。
---@return FSKPostureImpactEvaluation evaluation 只读计算结果，不写资源、恢复计时或动作状态；未知语义失败关闭。
function SKSurvivalComponent:EvaluatePostureImpact(reason, attack_type, additional_damage)
    local evaluation = UE.FSKPostureImpactEvaluation()
    if self:IsSurvivalReady() ~= true or self:IsAlive() ~= true
        or not is_finite(additional_damage) or additional_damage < 0.0 then
        return evaluation
    end
    local reason_name = tostring(reason)
    local gain_field = GainAttributes[reason_name]
    local strength_field = strength_attribute(attack_type)
    if gain_field == nil or strength_field == nil then
        return evaluation
    end
    if self:IsPostureBroken() == true then
        -- 崩溃时只抑制躯干通道，不能连带拒绝同笔命中的生命伤害。
        evaluation.bAccepted = true
        return evaluation
    end
    local attributes = self:GetSurvivalSnapshot().Attributes
    if not is_finite(attributes.MaxPosture) or attributes.MaxPosture <= 0.0 then
        return evaluation
    end
    local normalized = clamp(attributes.Posture / attributes.MaxPosture, 0.0, 1.0)
    local gain_scale = attributes.PostureMinGainScale
        + (1.0 - attributes.PostureMinGainScale)
        * ((1.0 - normalized) ^ attributes.PostureGainFalloffExponent)
    local amount = attributes[gain_field] * attributes[strength_field] * gain_scale + additional_damage
    if not is_finite(amount) or amount < 0.0 then
        return evaluation
    end
    if reason_name == "DeflectSuccess" then
        amount = math.min(amount, math.max(0.0,
            attributes.MaxPosture * attributes.PostureDeflectSuccessCapRatio - attributes.Posture))
    elseif reason_name == "AttackSuccess" or reason_name == "AttackGuarded" or reason_name == "AttackDeflected" then
        amount = math.min(amount, math.max(0.0,
            attributes.MaxPosture * attributes.PostureAttackCapRatio - attributes.Posture))
    end
    if not is_finite(amount) or amount < 0.0 then
        return evaluation
    end
    evaluation.bAccepted = true
    evaluation.PostureDamage = amount
    return evaluation
end

---确认完整接触已经受理后重置恢复渐进计时，包含封顶或免疫导致的零增量。
---@param reason string|userdata 已受理的攻防结果；未知语义不改变恢复计时。
---@return nil result 不再写 GAS 或触发第二次躯干伤害。
function SKSurvivalComponent:HandlePostureImpactCommitted(reason)
    if self.Runtime ~= nil and self.Runtime.bEnding ~= true and GainAttributes[tostring(reason)] ~= nil then
        self.Runtime.RecoveryEligibleTime = 0.0
    end
end

---保留独立姿态调试入口；真实武器与投射物必须使用统一命中提交，不能在提交后再调用本函数。
---@param reason string|userdata 攻防结果 FName，包含防御结果和攻击方反馈。
---@param attack_type userdata|number 通用攻击类型枚举。
---@param source_actor AActor|nil 可空的真实来源，透传给 GAS 数值结果。
---@return boolean accepted 合法语义且数值提交成功、封顶无变化或姿态免疫时为 true。
function SKSurvivalComponent:ApplyPostureImpact(reason, attack_type, source_actor)
    if self:IsPostureBroken() == true then
        return false
    end
    local evaluation = self:EvaluatePostureImpact(reason, attack_type, 0.0)
    if evaluation.bAccepted ~= true then
        return false
    end
    if evaluation.PostureDamage == 0.0 then
        self:HandlePostureImpactCommitted(reason)
        return true
    end
    local result = self:ApplyPostureDamage(evaluation.PostureDamage, source_actor)
    -- 躯干免疫只抑制数值，不把已经裁决成功的弹反降级成格挡，也不吞掉攻击被弹反的反应。
    local accepted = numeric_accepted(result)
        or (result.Code == UE.ESKNumericResultCode.PolicyRejected
            and tostring(result.RejectionReason) == "PostureImmune")
    if accepted then
        self:HandlePostureImpactCommitted(reason)
    end
    return accepted
end

---绑定事件后读取本角色的平铺 Lua 属性表，一次性通过 GE 初始化统一 Character 属性集。
---@return nil result 配置错误时保持未就绪门禁；已就绪角色不重读配置或重复回血。
function SKSurvivalComponent:ReceiveBeginPlay()
    if self.Runtime == nil then
        self:Initialize(nil)
    end
    if self.Runtime.bEventsBound ~= true then
        self.OnSurvivalReady:Add(self, self.HandleSurvivalReady)
        self.OnHealthChanged:Add(self, self.HandleHealthChanged)
        self.OnPostureChanged:Add(self, self.HandlePostureChanged)
        self.OnDeathStarted:Add(self, self.HandleDeathStarted)
        self.OnReviveStarted:Add(self, self.HandleReviveStarted)
        self.OnRevived:Add(self, self.HandleRevived)
        self.OnReviveCancelled:Add(self, self.HandleReviveCancelled)
        self.OnPostureBroken:Add(self, self.HandlePostureBroken)
        self.OnPostureRecovered:Add(self, self.HandlePostureRecovered)
        self.OnPostureBreakCancelled:Add(self, self.HandlePostureBreakCancelled)
        self.Runtime.bEventsBound = true
    end
    local owner = self:GetOwner()
    local ability_system = owner:GetSKAbilitySystemComponent()
    if ability_system:IsAttributesReady() ~= true then
        local values, module_name, error_message = CharacterAttributes.Load(owner)
        if values == nil then
            LuaLog.Debug(true, "SKSurvivalComponent", "ReceiveBeginPlay",
                "角色属性配置加载失败：" .. module_name .. "；" .. error_message)
        elseif ability_system:InitializeFromValues(values) ~= true then
            LuaLog.Debug(true, "SKSurvivalComponent", "ReceiveBeginPlay",
                "角色属性初始化被 GAS 拒绝：" .. module_name .. "；请检查数值范围、属性集和资源策略。")
        end
    end
    if self:IsSurvivalReady() == true then
        self:HandleSurvivalReady()
    else
        self:SuppressGameplay(NotReadyInputLock, true)
        LuaLog.Debug(true, "SKSurvivalComponent", "ReceiveBeginPlay",
            "GAS 或资源策略未就绪，保持生存与输入门禁；请检查该角色的 Lua 属性配置。")
    end
end

---在原生资源提交调用栈之外推进流程；普通战斗 Tick 以此组件为前置依赖。
---@param delta_seconds number 本帧时间，必须为正有限秒数。
---@return nil result 只推进当前有效生命轮次，不自动消费回生费用或完成回生。
function SKSurvivalComponent:HandleSurvivalTick(delta_seconds)
    if self.Runtime == nil or self.Runtime.bEnding == true or not is_finite(delta_seconds) or delta_seconds <= 0.0 then
        return
    end
    if self:IsSurvivalReady() ~= true then
        self:SuppressGameplay(NotReadyInputLock, false)
        return
    end
    self:HandleSurvivalReady()
    if self:IsAlive() ~= true then
        self:SuppressGameplay(DeathInputLock, false)
        if self:GetLifeState() == UE.ESKLifeState.Dying then
            local token = self:GetDeathToken()
            if token_matches(token, self.Runtime.DeathLifeSerial, self.Runtime.DeathTransitionSerial) then
                -- 当前未配置死亡演出，下一安全 Tick 收尾；不伪造动画等待或自动销毁角色。
                local result = self:FinishDeath(token)
                if transition_accepted(result) then
                    self.Runtime.DeathTransitionSerial = 0
                else
                    self:ReportTransitionFailure("FinishDeath", result)
                end
            end
        end
        return
    end
    if self:IsPostureBroken() == true then
        self:UpdatePostureBreak(delta_seconds)
        return
    end
    self:ResumeGameplayAfterTransition()
    self:UpdatePostureRecovery(delta_seconds)
end

---只解绑本 Lua 实例的订阅并释放本模块输入锁；离场不是死亡，不重启 AI 或发奖励。
---@param _end_play_reason userdata|number 引擎传入的 EEndPlayReason 枚举，仅作为生命周期参数保留。
---@return nil result 清空延迟流程和保存的对象引用，原生组件另行注销策略与标签。
function SKSurvivalComponent:ReceiveEndPlay(_end_play_reason)
    if self.Runtime == nil then
        return
    end
    self.Runtime.bEnding = true
    if self.Runtime.bEventsBound == true then
        self.OnSurvivalReady:Remove(self, self.HandleSurvivalReady)
        self.OnHealthChanged:Remove(self, self.HandleHealthChanged)
        self.OnPostureChanged:Remove(self, self.HandlePostureChanged)
        self.OnDeathStarted:Remove(self, self.HandleDeathStarted)
        self.OnReviveStarted:Remove(self, self.HandleReviveStarted)
        self.OnRevived:Remove(self, self.HandleRevived)
        self.OnReviveCancelled:Remove(self, self.HandleReviveCancelled)
        self.OnPostureBroken:Remove(self, self.HandlePostureBroken)
        self.OnPostureRecovered:Remove(self, self.HandlePostureRecovered)
        self.OnPostureBreakCancelled:Remove(self, self.HandlePostureBreakCancelled)
        self.Runtime.bEventsBound = false
    end
    local owner = self:GetOwner()
    local combat = owner ~= nil and owner:GetCombatComponent() or nil
    if combat ~= nil then
        combat:SetOwnerExternalInputLock(PostureInputLock, false)
        combat:SetOwnerExternalInputLock(DeathInputLock, false)
        combat:SetOwnerExternalInputLock(NotReadyInputLock, false)
    end
    self.Runtime.StoppedBrain = nil
    self.Runtime.BreakTransitionSerial = 0
    self.Runtime.DeathTransitionSerial = 0
    self.Runtime.bPendingResume = false
end

return SKSurvivalComponent
