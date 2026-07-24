-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKCombatComponent。
-- 负责首版攻击、防御、弹反动画编排；不处理碰撞、伤害、生命或躯干值。

local CombatConfig = require("Gameplay.Sekiro.Combat.CombatConfig")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")

---@class SKCombatBufferedAttack
---@field InputSerial number 物理按键序列号。
---@field PressTime number Started 事件的世界时间。
---@field SideSnapshot string 输入发生时锁存的刀侧。
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
---@field RestingSide string 动画曲线已经提交、供下一动作使用的刀侧。
---@field ComboNextAction string|nil 当前动作允许衔接的下一段轻攻击 ID。
---@field bSideCurveConsumed boolean 当前攻击是否已经提交过一次非零 AttackSide。
---@field bReturnToGuardAfterAttack boolean 当前攻击结束时是否允许按 Held 意图返回防御姿态。
---@field LastDeflectType string|nil 上一次弹反类型。
---@field LastDeflectTime number 上一次弹反输入时间。
---@field DeflectStage number 当前同类型弹反段数。

---@class SKCombatComponent: USKCombatComponent
---@field Runtime SKCombatRuntime
local SKCombatComponent = UnLua.Class()

local CurveThreshold = 0.5

---把 C++ 攻击侧枚举转换为 Lua 稳定名称。
---@param side userdata|number ESKAttackSide 枚举值。
---@return string side_name Left、Right 或 None。
local function side_to_name(side)
    if side == UE.ESKAttackSide.Left then
        return "Left"
    end
    if side == UE.ESKAttackSide.Right then
        return "Right"
    end
    return "None"
end

---把 Lua 稳定名称转换为 C++ 攻击侧枚举。
---@param side_name string|nil Left、Right 或其他值。
---@return userdata|number side ESKAttackSide 枚举值。
local function name_to_side(side_name)
    if side_name == "Left" then
        return UE.ESKAttackSide.Left
    end
    if side_name == "Right" then
        return UE.ESKAttackSide.Right
    end
    return UE.ESKAttackSide.None
end

---把 C++ 模拟攻击类型转换为 Lua 配置键。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@return string type_name Light、Heavy、Thrust 或 Special。
local function incoming_type_to_name(attack_type)
    if attack_type == UE.ESKIncomingAttackType.Heavy then
        return "Heavy"
    end
    if attack_type == UE.ESKIncomingAttackType.Thrust then
        return "Thrust"
    end
    if attack_type == UE.ESKIncomingAttackType.Special then
        return "Special"
    end
    return "Light"
end

---创建一份不访问 UObject 的 Lua 运行时初始状态。
---@param initial_side string|nil C++ 当前记录的刀侧。
---@return SKCombatRuntime runtime 新的运行时状态。
local function create_runtime(initial_side)
    local resting_side = initial_side
    if resting_side ~= "Left" and resting_side ~= "Right" then
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
        LastDeflectType = nil,
        LastDeflectTime = -math.huge,
        DeflectStage = 0,
    }
end

---创建或重置脚本运行时状态；此阶段不访问 Owner 或动画实例。
---@param _initializer table|nil UnLua 可选初始化表，当前实现不读取。
---@return nil result 仅重置 Lua 私有状态。
function SKCombatComponent:Initialize(_initializer)
    self.Runtime = create_runtime(CombatConfig.DefaultSide)
end

---把刀侧规范化后同步到 Lua 与 C++ 战斗宿主。
---@param side_name string|nil 期望锁存的刀侧。
---@return string resolved_side 最终采用的 Left 或 Right。
function SKCombatComponent:SetRestingSide(side_name)
    local resolved_side = side_name
    if resolved_side ~= "Left" and resolved_side ~= "Right" then
        resolved_side = CombatConfig.DefaultSide
    end
    self.Runtime.RestingSide = resolved_side
    self:SetNextAttackSide(name_to_side(resolved_side))
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
    if clear_pending == true then
        self.Runtime.PendingAttack = nil
    end
    self.Runtime.ActiveAttackKind = nil
    self.Runtime.ActiveAttackDomain = nil
    self.Runtime.ComboNextAction = nil
    self.Runtime.bSideCurveConsumed = false
    self.Runtime.bReturnToGuardAfterAttack = false
    local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
    self:SetCommittedAttackSide(name_to_side(default_side))
end

---组件开始运行时恢复中立状态和默认右侧刀位。
---@return nil result 状态直接写入 C++ 战斗宿主。
function SKCombatComponent:ReceiveBeginPlay()
    if self.Runtime == nil then
        self:Initialize(nil)
    end
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    self:ClearCombatPosture()
    local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
    self:SetCommittedAttackSide(name_to_side(default_side))
end

---播放一个离散全身动作，并用新的 ActionSerial 使旧回调失效。
---@param state userdata|number ESKCombatActionState 动作状态。
---@param action_id string 调试和后续衔接使用的稳定动作 ID。
---@param animation_path string UAnimSequence 对象路径。
---@return boolean started 动作是否成功加载并开始播放。
function SKCombatComponent:StartAction(state, action_id, animation_path)
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
    self:SetCommittedAttackSide(name_to_side(start_side))
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
---@param side_name string 输入发生时锁存的 Left 或 Right。
---@return boolean started 动画是否成功开始。
function SKCombatComponent:StartHeavyAttack(side_name)
    local locked_side = side_name
    if locked_side ~= "Left" and locked_side ~= "Right" then
        locked_side = CombatConfig.DefaultSide
    end
    local action = CombatConfig.HeavyBySide[locked_side]
    if action == nil then
        return false
    end

    self:SetCommittedAttackSide(name_to_side(action.StartSide or locked_side))
    self:SetRestingSide(locked_side)
    self.Runtime.ActiveAttackKind = "Heavy"
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = locked_side

    if self:StartAction(
        UE.ESKCombatActionState.HeavyAttack,
        action.ActionId or ("Charged_Thrust_" .. locked_side),
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
    self:SetCommittedAttackSide(name_to_side(start_side))
    self:SetRestingSide(start_side)
    self.Runtime.ActiveAttackKind = nil
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = nil
    if self:StartAction(
        UE.ESKCombatActionState.PendingAttack,
        startup.ActionId or ("Guard_Attack_Startup_" .. side_name),
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
        self:SetCommittedAttackSide(name_to_side(defense_side))
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

    self:SetCommittedAttackSide(name_to_side(action.StartSide or side_name))
    self:SetRestingSide(side_name)
    self.Runtime.ActiveAttackKind = "Heavy"
    self.Runtime.ActiveAttackDomain = "Ground"
    self.Runtime.ComboNextAction = side_name
    self.Runtime.ActionId = action.ActionId or ("Charged_Thrust_" .. side_name)
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
    self:ResetAttackRetention(true)
    self:SetGuardCombatPosture(force_air)
    local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
    self:SetCommittedAttackSide(name_to_side(defense_side))
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

---按来袭类型和同类型连续段数选择弹反动画，并暂时使用左侧刀位。
---@param attack_type userdata|number ESKIncomingAttackType 枚举值。
---@param event_time number 触发弹反的 Guard Started 世界时间。
---@return boolean started 弹反动画是否成功开始。
function SKCombatComponent:StartDeflect(attack_type, event_time)
    local type_name = incoming_type_to_name(attack_type)
    local chain = CombatConfig.DeflectByType[type_name]
    if chain == nil or #chain == 0 then
        return false
    end

    if self.Runtime.LastDeflectType ~= type_name
        or event_time - self.Runtime.LastDeflectTime > CombatConfig.DeflectChainResetTime then
        self.Runtime.DeflectStage = 1
    else
        self.Runtime.DeflectStage = self.Runtime.DeflectStage % #chain + 1
    end
    self.Runtime.LastDeflectType = type_name
    self.Runtime.LastDeflectTime = event_time
    self:ResetAttackRetention(true)
    self:SetGuardCombatPosture(false)
    local defense_side = self:SetRestingSide(CombatConfig.DefenseSide)
    self:SetCommittedAttackSide(name_to_side(defense_side))
    if self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        "Deflect_" .. type_name .. "_" .. tostring(self.Runtime.DeflectStage),
        chain[self.Runtime.DeflectStage]) == true then
        return true
    end
    self:ClearCombatPosture()
    self:SetRestingSide(CombatConfig.DefaultSide)
    return false
end

---尝试让新的 Guard Started 消费当前模拟来袭；只有成功消费才触发弹反。
---@param input_event FSKCombatInputEvent 新的防御按下事件。
---@return boolean deflected 是否成功消费来袭并开始弹反。
function SKCombatComponent:TryStartDeflect(input_event)
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
    return self:StartDeflect(attack_type, input_event.EventTimeSeconds)
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
        next_side = "Right"
    elseif side_curve_value <= -CurveThreshold then
        next_side = "Left"
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
    local light_action_id = is_guard_state and CombatConfig.DefenseSide or CombatConfig.DefaultSide
    if self:IsOwnerFalling() == true then
        attack_domain = "Air"
        light_action_id = "Air_Combo_01"
    end
    local pending = {
        InputSerial = input_event.InputSerial,
        PressTime = input_event.EventTimeSeconds,
        SideSnapshot = is_guard_state and CombatConfig.DefenseSide or CombatConfig.DefaultSide,
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

---处理防御 Started，弹反始终优先；普通防御严格采样动画内取消曲线。
---@param input_event FSKCombatInputEvent 防御按下事件。
---@return nil result 根据裁决启动弹反或 Guard Raise。
function SKCombatComponent:HandleGuardStarted(input_event)
    if self:TryStartDeflect(input_event) == true then
        return
    end

    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.Neutral
        or state == UE.ESKCombatActionState.PendingAttack then
        self:StartGuardRaise()
        return
    end
    if (state == UE.ESKCombatActionState.LightAttack
        or state == UE.ESKCombatActionState.HeavyAttack)
        and self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanCancelToGuard,
            input_event.EventTimeSeconds) >= CurveThreshold then
        self:StartGuardRaise()
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
        self:SetCommittedAttackSide(name_to_side(defense_side))
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
            self:SetCommittedAttackSide(name_to_side(defense_side))
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
        self.Runtime.ActionId = nil
        if self:IsGuardHeld() == true then
            self:SetGuardCombatPosture(false)
            self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        else
            self:ClearCombatPosture()
            self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
            local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
            self:SetCommittedAttackSide(name_to_side(default_side))
        end
        return
    end
    if state == UE.ESKCombatActionState.GuardLower then
        self.Runtime.ActionId = nil
        self:ClearCombatPosture()
        self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
        local default_side = self:SetRestingSide(CombatConfig.DefaultSide)
        self:SetCommittedAttackSide(name_to_side(default_side))
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

---每帧按确定顺序消费输入、提交动画内下一刀侧并检测自有 Montage 结束。
---该入口由原生组件显式 require 后调用，不依赖纯原生组件的 Blueprint ReceiveTick 分发。
---@param _delta_seconds number 本帧时长；当前实现使用 Sequence 曲线和事件绝对时间。
---@return boolean handled 始终返回 true，表示本帧战斗动画逻辑已执行。
function SKCombatComponent:Tick(_delta_seconds)
    if self.Runtime == nil then
        self.Runtime = create_runtime(side_to_name(self:GetNextAttackSide()))
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
    self:UpdateAttackSideCurve()
    local is_playing = self:IsCombatAnimationPlaying() == true
    if self.Runtime.bWasAnimationPlaying == true and is_playing ~= true then
        self:HandleAnimationFinished()
        is_playing = self:IsCombatAnimationPlaying() == true
    end
    self.Runtime.bWasAnimationPlaying = is_playing
    return true
end

return SKCombatComponent
