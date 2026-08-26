-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 集中保存弦一郎 710000 脚本中的原始 SpEffect、Interrupt、拼刀结果和符号化攻击标签。
-- 原始 ID 只在事件适配层解释；行为树和战斗记忆应消费 SemanticEvent，不得散落 Legacy 数字判断。

---@alias GenichiroLegacySignalSubject "Self"|"Target"|"SelfOrTarget"
---@alias GenichiroLegacySignalEdge "Rising"|"State"|"Event"|"SharedResult"|"Adapter"
---@alias GenichiroLegacySignalConfidence "A"|"B"|"C"|"A/C"
---@alias GenichiroLegacySignalInputKind "SpecialEffect"|"EngineInterrupt"|"Helper"|"SharedResult"|"SymbolicTag"

---@class GenichiroLegacySignalDefinition
---@field LegacyID number|string 原始 SpEffect 数值或符号化输入名，仅用于适配和追溯。
---@field Subject GenichiroLegacySignalSubject 信号描述的主体，Self 为弦一郎，Target 为当前目标。
---@field Edge GenichiroLegacySignalEdge 事件边沿或持续状态读取方式。
---@field SemanticEvent string UE 语义层消费的稳定名称。
---@field Confidence GenichiroLegacySignalConfidence 任务 3.3 定义的迁移语义确认度。
---@field InputKind GenichiroLegacySignalInputKind 原始输入类别。
---@field DrivesBehavior boolean 是否允许在第一阶段直接驱动候选、反应或状态变化。
---@field SourceLocations string[] 原始 710000 反编译脚本中的追溯位置。
---@field Notes string 中性行为说明；不得将推测写成原版正式命名。

local GenichiroLegacySignals = {}

GenichiroLegacySignals.Subject = {
    Self = "Self",
    Target = "Target",
    SelfOrTarget = "SelfOrTarget",
}

GenichiroLegacySignals.Edge = {
    Rising = "Rising",
    State = "State",
    Event = "Event",
    SharedResult = "SharedResult",
    Adapter = "Adapter",
}

GenichiroLegacySignals.Confidence = {
    A = "A",
    B = "B",
    C = "C",
    CategoryA_DetailC = "A/C",
}

GenichiroLegacySignals.InputKind = {
    SpecialEffect = "SpecialEffect",
    EngineInterrupt = "EngineInterrupt",
    Helper = "Helper",
    SharedResult = "SharedResult",
    SymbolicTag = "SymbolicTag",
}

local Subject = GenichiroLegacySignals.Subject
local Edge = GenichiroLegacySignals.Edge
local Confidence = GenichiroLegacySignals.Confidence
local InputKind = GenichiroLegacySignals.InputKind

---构造只读约定的信号定义，统一字段结构并避免映射条目漏写追溯信息。
---返回表由模块共享；调用方只能读取，不得修改字段或 SourceLocations。
---@param legacy_id number|string 原始数值 ID 或符号化输入名。
---@param subject GenichiroLegacySignalSubject 信号主体。
---@param edge GenichiroLegacySignalEdge 信号边沿或读取方式。
---@param semantic_event string UE 语义事件名称。
---@param confidence GenichiroLegacySignalConfidence 迁移语义确认度。
---@param input_kind GenichiroLegacySignalInputKind 原始输入类别。
---@param drives_behavior boolean 是否允许驱动第一阶段行为。
---@param source_locations string[] 原始脚本追溯位置。
---@param notes string 中性行为说明。
---@return GenichiroLegacySignalDefinition definition 完整信号定义。
local function signal(
    legacy_id,
    subject,
    edge,
    semantic_event,
    confidence,
    input_kind,
    drives_behavior,
    source_locations,
    notes)
    return {
        LegacyID = legacy_id,
        Subject = subject,
        Edge = edge,
        SemanticEvent = semantic_event,
        Confidence = confidence,
        InputKind = input_kind,
        DrivesBehavior = drives_behavior,
        SourceLocations = source_locations,
        Notes = notes,
    }
end

-- 事件边沿：这些 SpEffect 激活会产生一次性语义事件，由 ReactionRouter 按优先级消费。
local SignalByLegacyID = {
    [220010] = signal(220010, Subject.Self, Edge.Rising, "TargetInvalidated", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_logic.dec.lua:4,34" },
        "激活时清除敌对目标。"),
    [107710] = signal(107710, Subject.Self, Edge.Rising, "ForceReplan", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_logic.dec.lua:5,37" },
        "激活时请求重新规划。"),
    [110060] = signal(110060, Subject.Target, Edge.Rising, "LegacyTargetState110060Changed", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_logic.dec.lua:6,40", "710000_battle.dec.lua:36,51" },
        "激活时设置目标状态标记并改变转向、移动和接近权重。"),
    [110015] = signal(110015, Subject.Target, Edge.Rising, "LegacyTargetState110015Changed", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_logic.dec.lua:7,45" },
        "激活时清除对应目标状态标记并提交等待。"),
    [5029] = signal(5029, Subject.Self, Edge.Rising, "DamageReceived", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:21,641" },
        "进入受击处理；真实伤害事件落地后优先使用通用 DamageReceived 队列事实。"),
    [3710020] = signal(3710020, Subject.Self, Edge.Rising, "DeflectChainReset", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:25,633" },
        "清零连续拼刀计数。"),
    [3710030] = signal(3710030, Subject.Self, Edge.Rising, "ForcedReaction3092", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:26,636" },
        "仅在同时持有 3710032 时提交 3092，并写入 Timer(6)=50 秒。"),
    [5031] = signal(5031, Subject.Self, Edge.Rising, "ConditionalReaction3017", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:23,643" },
        "忍杀次数不高于 1 且目标距离不小于 4.1 时提交 3017。"),
    [3710050] = signal(3710050, Subject.Target, Edge.Rising, "TargetSpecialAction", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:29,648" },
        "按阶段修饰在 3023 反应与横移之间选择。"),
    [110620] = signal(110620, Subject.Target, Edge.Rising, "ForceReplan", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:30,656" },
        "目标状态激活时请求重新规划。"),

    -- 持续状态：决策快照读取这些标志，不把每帧状态误报成重复事件。
    [200004] = signal(200004, Subject.Self, Edge.State, "ReactionHandlingEnabled", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_logic.dec.lua:13,17,21", "710000_battle.dec.lua:623" },
        "不存在时拒绝 710000 的战斗中断处理。"),
    [200050] = signal(200050, Subject.Self, Edge.State, "LegacyPhaseFlagA", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:44,482,649,768,913,1001" },
        "修饰阶段开场、受击反击、拼刀候选和特殊目标响应。"),
    [200051] = signal(200051, Subject.Self, Edge.State, "LegacyPhaseFlagB", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:112,661,905" },
        "禁用多类动作与拼刀反应，并限制道具惩罚。"),
    [110010] = signal(110010, Subject.Target, Edge.State, "LegacyTargetState110010", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:28,36,51" },
        "与 110060 一起改变转向和移动权重。"),
    [110030] = signal(110030, Subject.Target, Edge.State, "LegacyTargetState110030", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:46" },
        "强制提高上下文重定位动作权重。"),
    [109031] = signal(109031, Subject.Target, Edge.State, "ClosePunishWindow", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:81" },
        "目标近距状态，与 110125 共用惩罚窗口语义。"),
    [110125] = signal(110125, Subject.Target, Edge.State, "ClosePunishWindow", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:81" },
        "目标近距状态，与 109031 共用惩罚窗口语义。"),
    [109900] = signal(109900, Subject.Target, Edge.State, "TargetPunishWindow", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:84" },
        "重写候选权重、启用 Act48，并压低或禁用部分动作。"),
    [110621] = signal(110621, Subject.Target, Edge.State, "TargetRepositionRestricted", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:131" },
        "禁用横移和后闪，同时提高 Act31。"),
    [109970] = signal(109970, Subject.Target, Edge.State, "LegacyParryBranch109970", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:677" },
        "进入特殊防御响应分支，需结合自身 221000/221001 模式解释。"),
    [110450] = signal(110450, Subject.Target, Edge.State, "ParryForbiddenAttackTag", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:688" },
        "目标攻击标签存在时拒绝防御响应。"),
    [110500] = signal(110500, Subject.Target, Edge.State, "ParryForbiddenAttackTag", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:688" },
        "目标攻击标签存在时拒绝防御响应。"),
    [110501] = signal(110501, Subject.Target, Edge.State, "ParryForbiddenAttackTag", Confidence.A,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:688" },
        "目标攻击标签存在时拒绝防御响应。"),
    [109980] = signal(109980, Subject.Target, Edge.State, "DodgePreferredAttackTag", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:724" },
        "防御响应时优先使用 5201 后闪。"),
    [221000] = signal(221000, Subject.Self, Edge.State, "LegacyGuardResponseMode0", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:680" },
        "选择 109970 分支中的响应模式 0。"),
    [221001] = signal(221001, Subject.Self, Edge.State, "LegacyGuardResponseMode1", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:682" },
        "选择 109970 分支中的响应模式 1。"),
    [3710032] = signal(3710032, Subject.Self, Edge.State, "ForcedReaction3092Enabled", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:636" },
        "允许 3710030 触发 3092 强制反应。"),
    [3710040] = signal(3710040, Subject.Self, Edge.State, "SpecialGuardVariant3102", Confidence.B,
        InputKind.SpecialEffect, true, { "710000_battle.dec.lua:699" },
        "防御响应时强制选择 3102 变体。"),

    -- 拼刀结果：类别可确认，但左右、强弱或完美弹刀等细义仍未知，因此使用 A/C 确认度。
    [200200] = signal(200200, Subject.Self, Edge.SharedResult, "LegacyKengeki200200",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:803" },
        "按距离和连续次数进入普通或高连续反击池，并在入口增加计数。"),
    [200201] = signal(200201, Subject.Self, Edge.SharedResult, "LegacyKengeki200201",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:825" },
        "候选结构接近 200200，但入口计数和权重不同。"),
    [200210] = signal(200210, Subject.Self, Edge.SharedResult, "LegacyKengeki200210",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:845" },
        "固定启用 K02/K17，并加入 K31/K38。"),
    [200211] = signal(200211, Subject.Self, Edge.SharedResult, "LegacyKengeki200211",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:850" },
        "固定启用 K02，并加入 K10/K31/K38。"),
    [200215] = signal(200215, Subject.Self, Edge.SharedResult, "LegacyKengeki200215",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:879" },
        "近距增加连续次数，进入较低权重的高连续反击池。"),
    [200216] = signal(200216, Subject.Self, Edge.SharedResult, "LegacyKengeki200216",
        Confidence.CategoryA_DetailC, InputKind.SharedResult, true, { "710000_battle.dec.lua:855" },
        "近距增加连续次数，候选更偏 K03/K38/K43。"),

    -- C 级保留：只供诊断和后续 Profile 追溯，禁止第一阶段直接选动作。
    [5025] = signal(5025, Subject.Self, Edge.Rising, "Legacy5025", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_battle.dec.lua:19" },
        "基础版只注册观察，没有显式处理分支。"),
    [5026] = signal(5026, Subject.Self, Edge.Rising, "Legacy5026", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_battle.dec.lua:20" },
        "基础版只注册观察；变体行为不反推回 710000。"),
    [5030] = signal(5030, Subject.Self, Edge.Rising, "Legacy5030", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_battle.dec.lua:22" },
        "基础版只注册观察；变体行为不反推回 710000。"),
    [3710010] = signal(3710010, Subject.Self, Edge.Rising, "Legacy3710010", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_battle.dec.lua:24" },
        "只注册观察，没有显式处理分支。"),
    [3710031] = signal(3710031, Subject.Self, Edge.Rising, "Legacy3710031", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_battle.dec.lua:27" },
        "只注册观察，没有显式处理分支。"),
    [200002] = signal(200002, Subject.Self, Edge.State, "Legacy200002", Confidence.C,
        InputKind.SpecialEffect, false, { "710000_logic.dec.lua:14,18,22" },
        "只出现在空分支或恒假条件，不迁移为空操作行为。"),
}

-- 符号化输入不能伪造数值 ID；适配层按名称查询后再发布同一套稳定语义事件。
local NamedSignalByName = {
    INTERUPT_ParryTiming = signal("INTERUPT_ParryTiming", Subject.Self, Edge.Event, "AttackThreat",
        Confidence.A, InputKind.EngineInterrupt, true, { "710000_battle.dec.lua:626" },
        "进入正面、距离、攻击标签和抑制定时器共同约束的防御响应。"),
    INTERUPT_ShootImpact = signal("INTERUPT_ShootImpact", Subject.Self, Edge.Event, "ProjectileImpact",
        Confidence.A, InputKind.EngineInterrupt, true, { "710000_battle.dec.lua:629" },
        "清理当前链并进入 3100 射击命中反应。"),
    INTERUPT_ActivateSpecialEffect = signal("INTERUPT_ActivateSpecialEffect", Subject.SelfOrTarget, Edge.Adapter,
        "SpecialEffectActivated", Confidence.A, InputKind.EngineInterrupt, false,
        { "710000_logic.dec.lua:33", "710000_battle.dec.lua:632" },
        "只负责取得实际 SpEffect ID 并分派，不能脱离数值条目直接选择行为。"),
    Interupt_Use_Item = signal("Interupt_Use_Item", Subject.Target, Edge.Event, "TargetUseItem",
        Confidence.A, InputKind.Helper, true, { "710000_battle.dec.lua:661" },
        "目标道具动作成立后提交 3023；阶段和忍杀次数仍由上层过滤。"),
    COMMON_SP_EFFECT_PC_ATTACK_RUSH = signal("COMMON_SP_EFFECT_PC_ATTACK_RUSH", Subject.Target, Edge.State,
        "RushAttackTag", Confidence.A, InputKind.SymbolicTag, true, { "710000_battle.dec.lua:678" },
        "目标攻击标签存在时选择 3103 专用承受反应。"),
}

GenichiroLegacySignals.SignalByLegacyID = SignalByLegacyID
GenichiroLegacySignals.NamedSignalByName = NamedSignalByName

-- 持续状态由外部 SpEffect 适配器写入 Blackboard；映射集中在此，Task 不散落 Legacy 数字。
local BlackboardKeyByLegacyStateID = {
    [200050] = "LegacyPhaseFlagA",
    [200051] = "LegacyPhaseFlagB",
    [110060] = "TargetSpecialAction",
    [110010] = "TargetSpecialAction",
    [110030] = "TargetState110030",
    [109031] = "TargetStatePunish",
    [110125] = "TargetStatePunish",
    [109900] = "TargetPunishWindow",
    [110621] = "TargetRepositionRestricted",
}

GenichiroLegacySignals.BlackboardKeyByLegacyStateID = BlackboardKeyByLegacyStateID

---按原始数值 ID 查询集中定义；未知 ID 返回 nil，由适配层记录一次诊断后忽略。
---@param legacy_id number 原始 SpEffect 或拼刀结果 ID。
---@return GenichiroLegacySignalDefinition|nil definition 已登记定义；未知 ID 返回 nil。
function GenichiroLegacySignals.GetByLegacyID(legacy_id)
    return SignalByLegacyID[legacy_id]
end

---按符号化输入名查询 Interrupt、共享助手或攻击标签定义。
---@param signal_name string 原脚本中的符号化常量或助手名。
---@return GenichiroLegacySignalDefinition|nil definition 已登记定义；未知名称返回 nil。
function GenichiroLegacySignals.GetByName(signal_name)
    return NamedSignalByName[signal_name]
end

---查询持续 Legacy SpEffect 对应的 Blackboard 语义键；事件型信号返回 nil。
---@param legacy_id number 原始持续 SpEffect ID。
---@return string|nil blackboard_key 已登记的稳定语义键；非持续上下文输入返回 nil。
function GenichiroLegacySignals.GetBlackboardKeyForState(legacy_id)
    return BlackboardKeyByLegacyStateID[legacy_id]
end

---判断定义是否可以直接参与第一阶段反应或决策。
---C 级和纯适配器条目即使被观察到也只允许诊断，防止未知语义被误当成动作入口。
---@param definition GenichiroLegacySignalDefinition|nil 待检查的集中映射定义。
---@return boolean can_drive_behavior 是否允许驱动第一阶段行为。
function GenichiroLegacySignals.CanDriveBehavior(definition)
    return definition ~= nil
        and definition.DrivesBehavior == true
        and definition.Confidence ~= Confidence.C
end

---统计数值信号数量，供离线契约检查确认任务 3.3 的 40 项清单未意外增删。
---@return number count 已登记数值信号数量。
function GenichiroLegacySignals.GetNumericSignalCount()
    local count = 0
    for _legacy_id, _definition in pairs(SignalByLegacyID) do
        count = count + 1
    end
    return count
end

return GenichiroLegacySignals
