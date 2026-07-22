-- Lua 类型：UnLua UObject 运行时类。self 是真实的 USKCombatComponent。
-- 负责首版攻击、防御、弹反动画编排；不处理碰撞、伤害、生命或躯干值。

local CombatConfig = require("Gameplay.Sekiro.Combat.CombatConfig")
local CurveNames = require("Animation.Sekiro.Shared.CurveNames")

---@class SKCombatBufferedAttack
---@field InputSerial integer 物理按键序列号。
---@field PressTime number Started 事件世界时间。
---@field SideSnapshot string 输入发生时锁存的下一攻击侧。
---@field bLightAccepted boolean Started 时是否位于轻攻击窗口。
---@field bHeavyAccepted boolean Started 时是否位于重攻击窗口。

---@class SKCombatRuntime
---@field ActionId string|nil 当前具体动作稳定 ID。
---@field ActionSerial integer 当前离散动作序列号。
---@field bWasAnimationPlaying boolean 上一帧是否仍在播放组件自有 Montage。
---@field PendingAttack SKCombatBufferedAttack|nil 等待 Completed 判定长短按的攻击输入。
---@field NextAttackSide string 下一动作侧别。
---@field bSideCurveArmed boolean AttackSide 是否已经离开零值。
---@field bSideCurveConsumed boolean 本动作是否已经提交过下一侧。
---@field LastDeflectType string|nil 上一次弹反类型。
---@field LastDeflectTime number 上一次弹反输入时间。
---@field DeflectStage integer 当前同类型弹反段数。

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

---创建或重置脚本运行时状态；此阶段不访问 Owner 或动画实例。
---@param _initializer table|nil UnLua 可选初始化表，当前实现不读取。
---@return nil result 仅重置 Lua 私有状态。
function SKCombatComponent:Initialize(_initializer)
    self.Runtime = {
        ActionId = nil,
        ActionSerial = 0,
        bWasAnimationPlaying = false,
        PendingAttack = nil,
        NextAttackSide = "Right",
        bSideCurveArmed = false,
        bSideCurveConsumed = false,
        LastDeflectType = nil,
        LastDeflectTime = -math.huge,
        DeflectStage = 0,
    }
end

---组件开始运行时恢复中立状态和首刀方向。
---@return nil result 状态直接写入 C++ 战斗宿主。
function SKCombatComponent:ReceiveBeginPlay()
    if self.Runtime == nil then
        self:Initialize(nil)
    end
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
    self:SetNextAttackSide(UE.ESKAttackSide.Right)
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
    self.Runtime.bSideCurveArmed = false
    self.Runtime.bSideCurveConsumed = false
    return true
end

---按配置启动普通攻击并提交本段固定攻击侧。
---@param action_id string Attacks 表中的动作 ID。
---@return boolean started 动画是否成功开始。
function SKCombatComponent:StartLightAttack(action_id)
    local action = CombatConfig.Attacks[action_id]
    if action == nil then
        return false
    end
    self:SetCommittedAttackSide(name_to_side(action.Side))
    self.Runtime.NextAttackSide = action.Side ~= "None" and action.Side or self.Runtime.NextAttackSide
    self:SetNextAttackSide(name_to_side(self.Runtime.NextAttackSide))
    return self:StartAction(UE.ESKCombatActionState.LightAttack, action_id, action.AnimationPath)
end

---按锁存侧别启动蓄力突刺；Combo 动作不调用本入口。
---@param side_name string 输入发生时锁存的 Left 或 Right。
---@return boolean started 动画是否成功开始。
function SKCombatComponent:StartHeavyAttack(side_name)
    local action = CombatConfig.HeavyBySide[side_name]
    if action == nil then
        return false
    end
    self:SetCommittedAttackSide(name_to_side(action.Side))
    self.Runtime.NextAttackSide = action.NextAction or side_name
    self:SetNextAttackSide(name_to_side(self.Runtime.NextAttackSide))
    return self:StartAction(
        UE.ESKCombatActionState.HeavyAttack,
        "Charged_Thrust_" .. side_name,
        action.AnimationPath)
end

---开始普通防御举刀；稳定防御 Pose 在 Raise 播放完毕后由 AnimBlueprint 接管。
---@return boolean started 举刀动画是否成功开始。
function SKCombatComponent:StartGuardRaise()
    self.Runtime.PendingAttack = nil
    return self:StartAction(
        UE.ESKCombatActionState.GuardRaise,
        "Guard_Raise",
        CombatConfig.Guard.Raise)
end

---开始防御收刀；播放结束后回到 Neutral。
---@return boolean started 收刀动画是否成功开始。
function SKCombatComponent:StartGuardLower()
    return self:StartAction(
        UE.ESKCombatActionState.GuardLower,
        "Guard_Lower",
        CombatConfig.Guard.Lower)
end

---按来袭类型和同类型连续段数选择弹反动画。
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
    self.Runtime.PendingAttack = nil
    return self:StartAction(
        UE.ESKCombatActionState.DeflectReaction,
        "Deflect_" .. type_name .. "_" .. tostring(self.Runtime.DeflectStage),
        chain[self.Runtime.DeflectStage])
end

---尝试让新的 Guard Started 消费当前模拟来袭；只有成功消费才会触发弹反。
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

---处理攻击 Started：中立状态进入按键判定，动作中仅在曲线窗口锁存候选。
---@param input_event FSKCombatInputEvent 攻击按下事件。
---@return nil result 候选写入 Runtime，等待同 Serial 的 Completed。
function SKCombatComponent:HandleAttackStarted(input_event)
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.Neutral then
        self:BeginCombatAction(UE.ESKCombatActionState.PendingAttack)
        self.Runtime.PendingAttack = {
            InputSerial = input_event.InputSerial,
            PressTime = input_event.EventTimeSeconds,
            SideSnapshot = self.Runtime.NextAttackSide,
            bLightAccepted = true,
            bHeavyAccepted = true,
        }
        return
    end

    if state ~= UE.ESKCombatActionState.LightAttack and state ~= UE.ESKCombatActionState.HeavyAttack then
        return
    end
    self.Runtime.PendingAttack = {
        InputSerial = input_event.InputSerial,
        PressTime = input_event.EventTimeSeconds,
        SideSnapshot = self.Runtime.NextAttackSide,
        bLightAccepted = self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanAcceptLightAttack,
            input_event.EventTimeSeconds) >= CurveThreshold,
        bHeavyAccepted = self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanAcceptHeavyAttack,
            input_event.EventTimeSeconds) >= CurveThreshold,
    }
end

---处理攻击 Completed：只消费相同 InputSerial，并依据按住时长和锁存窗口选择后续动作。
---@param input_event FSKCombatInputEvent 攻击释放事件。
---@return nil result 成功时启动一个新动作，否则丢弃候选。
function SKCombatComponent:HandleAttackCompleted(input_event)
    local pending = self.Runtime.PendingAttack
    if pending == nil or pending.InputSerial ~= input_event.InputSerial then
        return
    end
    self.Runtime.PendingAttack = nil

    local state = self:GetCombatActionState()
    local is_heavy = input_event.HoldDuration >= CombatConfig.HeavyHoldThreshold
    if state == UE.ESKCombatActionState.PendingAttack then
        if is_heavy then
            self:StartHeavyAttack(pending.SideSnapshot)
        else
            self:StartLightAttack(pending.SideSnapshot)
        end
        return
    end

    if is_heavy and pending.bHeavyAccepted == true then
        self:StartHeavyAttack(pending.SideSnapshot)
        return
    end
    if pending.bLightAccepted ~= true then
        return
    end
    local current = CombatConfig.Attacks[self.Runtime.ActionId]
    if current ~= nil and current.NextAction ~= nil then
        self:StartLightAttack(current.NextAction)
    end
end

---处理防御 Started：优先裁决弹反，未命中来袭窗口时再尝试普通防御或曲线取消。
---@param input_event FSKCombatInputEvent 防御按下事件。
---@return nil result 根据裁决启动弹反或 Guard Raise。
function SKCombatComponent:HandleGuardStarted(input_event)
    if self:TryStartDeflect(input_event) == true then
        return
    end

    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.Neutral or state == UE.ESKCombatActionState.PendingAttack then
        self:StartGuardRaise()
        return
    end
    if (state == UE.ESKCombatActionState.LightAttack or state == UE.ESKCombatActionState.HeavyAttack)
        and self:SampleActiveSequenceCurveAtTime(
            CurveNames.CanCancelToGuard,
            input_event.EventTimeSeconds) >= CurveThreshold then
        self:StartGuardRaise()
    end
end

---处理防御 Completed：举刀或稳定防御状态都转入收刀；弹反结束时会根据实时按住状态退出。
---@return nil result 必要时启动 Guard Lower。
function SKCombatComponent:HandleGuardCompleted()
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.GuardRaise or state == UE.ESKCombatActionState.Guarding then
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

---采样活动 Sequence 的 AttackSide，并保证每段动作最多提交一次非零侧别。
---@return nil result 必要时更新 Runtime 和 C++ NextAttackSide。
function SKCombatComponent:UpdateAttackSideCurve()
    local state = self:GetCombatActionState()
    if state ~= UE.ESKCombatActionState.LightAttack and state ~= UE.ESKCombatActionState.HeavyAttack then
        return
    end
    local curve_value = self:SampleActiveSequenceCurve(CurveNames.AttackSide)
    if self.Runtime.bSideCurveArmed ~= true then
        self.Runtime.bSideCurveArmed = math.abs(curve_value) >= CurveThreshold
    end
    if self.Runtime.bSideCurveArmed ~= true or self.Runtime.bSideCurveConsumed == true then
        return
    end
    if curve_value >= CurveThreshold then
        self.Runtime.NextAttackSide = "Right"
    elseif curve_value <= -CurveThreshold then
        self.Runtime.NextAttackSide = "Left"
    else
        return
    end
    self:SetNextAttackSide(name_to_side(self.Runtime.NextAttackSide))
    self.Runtime.bSideCurveConsumed = true
end

---处理当前全身动画自然结束后的状态收敛。
---@return nil result 进入稳定 Guard、收刀或 Neutral。
function SKCombatComponent:HandleAnimationFinished()
    local state = self:GetCombatActionState()
    if state == UE.ESKCombatActionState.GuardRaise then
        if self:IsGuardHeld() == true then
            self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        else
            self:StartGuardLower()
        end
        return
    end
    if state == UE.ESKCombatActionState.DeflectReaction and self:IsGuardHeld() == true then
        self:SetCombatActionState(UE.ESKCombatActionState.Guarding)
        return
    end
    self.Runtime.PendingAttack = nil
    self.Runtime.ActionId = nil
    self:SetCombatActionState(UE.ESKCombatActionState.Neutral)
end

---每帧按确定顺序消费输入、提交侧别曲线并检测自有 Montage 结束。
---该入口由原生组件显式 require 后调用，不依赖纯原生组件的 Blueprint ReceiveTick 分发。
---@param _delta_seconds number 本帧时长；当前状态机使用事件绝对时间，不累计该值。
---@return boolean handled 始终返回 true，表示本帧战斗动画逻辑已执行。
function SKCombatComponent:Tick(_delta_seconds)
    if self.Runtime == nil then
        self.Runtime = {
            ActionId = nil,
            ActionSerial = 0,
            bWasAnimationPlaying = false,
            PendingAttack = nil,
            NextAttackSide = side_to_name(self:GetNextAttackSide()),
            bSideCurveArmed = false,
            bSideCurveConsumed = false,
            LastDeflectType = nil,
            LastDeflectTime = -math.huge,
            DeflectStage = 0,
        }
    end
    while true do
        local has_event, input_event = self:ConsumeCombatInputEvent()
        if has_event ~= true or input_event == nil then
            break
        end
        self:HandleInputEvent(input_event)
    end

    self:UpdateAttackSideCurve()
    local is_playing = self:IsCombatAnimationPlaying() == true
    if self.Runtime.bWasAnimationPlaying == true and is_playing ~= true then
        self:HandleAnimationFinished()
    end
    self.Runtime.bWasAnimationPlaying = is_playing
    return true
end

return SKCombatComponent
