-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 集中声明弦一郎第一里程碑的 UE 语义动作、Legacy 追溯、动画链、移动策略和能力依赖。
-- 本模块不计算候选权重、不执行导航或播放动画；TacticalProfile、MoveGoal Task 和战斗 Task 分别消费对应字段。

---@alias GenichiroActionSourceKind "Act"|"Kengeki"|"SemanticAlias"
---@alias GenichiroActionBehaviorFamily "PressureAttack"|"PhaseOpening"|"Reposition"|"RepositionAttack"|"ClashResponse"|"Reaction"
---@alias GenichiroActionExecutionMode "CombatSequence"|"Navigation"|"MovementThenCombat"|"CombatThenNavigation"
---@alias GenichiroActionCandidatePolicy "Tactical"|"ReactionOnly"
---@alias GenichiroActionInterruptPolicy "CompleteCurrentSegment"|"ImmediateNavigationAbort"|"MovementThenSegmentBoundary"
---@alias GenichiroAnimationUsage "Locomotion"|"CombatAction"|"Reaction"
---@alias GenichiroDamageChannel "None"|"Melee"|"Projectile"
---@alias GenichiroActionCapability "ProjectileImpact"

---@class GenichiroActionRequestResolution
---@field ActionRequestID number 原 AI Goal 提交给行为图的请求编号，不等同于物理 HKX 编号。
---@field BehaviorStateName string c9997 行为图中承接请求的状态名。
---@field TAEAnimID number|nil TAE 时间线编号；行为请求没有独立 TAE 时为 nil。
---@field ReferenceType string Direct、ImportHKX、ImportOtherAnim 或 UEApproximation。
---@field MotionSourceAnimID number|nil 最终提供骨骼动作的物理 HKX 编号。
---@field EventSourceAnimID number|nil 提供 TAE 事件的时间线编号。
---@field ResolvedAssetID number UE 实际播放的 AnimSequence 编号；引用动画仍播放已物化的 TAE 别名资产。
---@field bUERepresentationApproximation boolean 是否使用 UE 表现近似代替原行为状态的独立动画。
---@field Notes string 解析依据与近似边界。

---@class GenichiroAnimationStep
---@field ActionRequestID number 原 AI Goal 提交给行为图的请求编号。
---@field BehaviorStateName string c9997 行为图状态名。
---@field TAEAnimID number|nil TAE 时间线编号。
---@field MotionSourceAnimID number|nil 最终动作源 HKX 编号。
---@field EventSourceAnimID number|nil TAE 事件源编号。
---@field ResolvedAssetID number UE 实际播放的 AnimSequence 编号。
---@field ReferenceType string TAE 引用类型或 UEApproximation。
---@field bUERepresentationApproximation boolean 是否为 UE 表现近似。
---@field AssetPath string 由 ResolvedAssetID 确定性生成的 UE 对象路径。
---@field Usage GenichiroAnimationUsage 动画的主消费者分类。
---@field DamageChannels GenichiroDamageChannel[] 该步骤允许消费的 TAE 伤害通道。
---@field Optional boolean 条件不成立或能力未接入时是否可跳过该步骤。
---@field Condition string|nil 条件步骤的中性表达；由执行器读取上下文判断，不在目录内求值。
---@field ProjectileCues GenichiroProjectileCue[] 从 c7100 TAE Type 2 事件提取的发射时点。

---@class GenichiroProjectileCue
---@field TimeSeconds number 相对 AnimSequence 起点的发射秒数。
---@field BehaviorJudgeID number 原始 Type 2 BehaviorJudgeID，保留追溯。
---@field DummyPolyID number 原始发射挂点编号，第一阶段由通用出生偏移近似。

---@class GenichiroActionVariant
---@field ID string 动作内部稳定分支名。
---@field LegacyWeight number 原脚本分支权重；只作任务 5.3 校准输入。
---@field Steps GenichiroAnimationStep[] 按顺序提交的离散动画步骤。
---@field StateWrites GenichiroActionStateWrite[] 分支完成时提交的短期状态副作用。

---@class GenichiroActionMovementPolicy
---@field Policy string UE 导航或离散位移策略名。
---@field DesiredRangeCm number|nil 进入动作前的目标距离，已按 1 Legacy 单位等于 100 厘米换算。
---@field LegacyRangeExpression string|nil 原脚本含角色半径的距离表达式，用于追溯。
---@field DurationMinSeconds number|nil 导航动作最短持续时间。
---@field DurationMaxSeconds number|nil 导航动作最长持续时间。
---@field DirectionAngleMinDegrees number|nil 侧移朝向容差下限。
---@field DirectionAngleMaxDegrees number|nil 侧移朝向容差上限。
---@field RequiredSpaceCm number|nil 提交位移前必须通过的导航空间检查距离。
---@field DirectionMemoryKey string|nil 成功解析的移动方向写入 CombatMemory 的语义键。
---@field FixedDirection string|nil 强制 Left 或 Right 的移动方向；为空时由空间与最近方向解析。

---@class GenichiroActionStateWrite
---@field Key string CombatMemory 使用的语义键；Legacy 索引保留在 Notes 中。
---@field Value number|string|boolean 需要写入的值或执行器解析令牌。
---@field CommitPoint string OnStart、OnMovementSuccess 或 OnComplete。
---@field Notes string 原脚本副作用追溯说明。

---@class GenichiroActionCooldown
---@field Key string UE CombatMemory 中的稳定冷却键。
---@field LegacyKey number|string 原版动画冷却键或 Timer 索引。
---@field Seconds number 冷却持续秒数。
---@field CommitPoint string 冷却提交时机。

---@class GenichiroActionDefinition
---@field ID string UE 语义动作稳定 ID，不暴露为 C++ 枚举。
---@field BehaviorFamily GenichiroActionBehaviorFamily 行为树可观察的语义行为族。
---@field SourceKind GenichiroActionSourceKind 原脚本来源类别。
---@field LegacyIndex number 原始 Act、Kengeki 或状态请求编号。
---@field SourceLocation string 原始 710000 反编译脚本位置。
---@field CandidatePolicy GenichiroActionCandidatePolicy 是否进入普通战术池或只由反应路由提交。
---@field ExecutionMode GenichiroActionExecutionMode 导航与离散动画步骤的所有权顺序。
---@field InterruptPolicy GenichiroActionInterruptPolicy BehaviorTree Abort 的保守处理策略。
---@field Movement GenichiroActionMovementPolicy|nil 导航或离散位移策略；纯战斗动作可为空。
---@field Variants GenichiroActionVariant[] 动画分支；纯导航动作可为空数组。
---@field ConditionalFollowUps GenichiroAnimationStep[] 条件成立后追加的步骤。
---@field CapabilityDependencies GenichiroActionCapability[] 完整表现依赖的后续能力。
---@field Cooldowns GenichiroActionCooldown[] 动作完成或指定时机写入的冷却。
---@field StateWrites GenichiroActionStateWrite[] 对 CombatMemory 的显式副作用。
---@field Notes string UE 转译边界和原脚本行为摘要。

---@class GenichiroLegacyStateAlias
---@field ActionRequestID number 原始 EzState 或动作请求编号。
---@field ActionID string UE 语义动作 ID。
---@field BehaviorStateName string c9997 行为图状态名。
---@field TAEAnimID number|nil TAE 时间线编号。
---@field MotionSourceAnimID number|nil 最终动作源 HKX 编号。
---@field EventSourceAnimID number|nil TAE 事件源编号。
---@field ResolvedAssetID number UE 实际播放的 AnimSequence 编号。
---@field bUERepresentationApproximation boolean 是否为 UE 表现近似。
---@field AssetPath string 实际加载的 UE 对象路径。
---@field MovementPolicy string 与表现源组合的 UE 位移策略。
---@field SourceLocation string 原脚本追溯位置。

local GenichiroActionCatalog = {}

GenichiroActionCatalog.SourceKind = {
    Act = "Act",
    Kengeki = "Kengeki",
    SemanticAlias = "SemanticAlias",
}

GenichiroActionCatalog.BehaviorFamily = {
    PressureAttack = "PressureAttack",
    PhaseOpening = "PhaseOpening",
    Reposition = "Reposition",
    RepositionAttack = "RepositionAttack",
    ClashResponse = "ClashResponse",
    Reaction = "Reaction",
}

GenichiroActionCatalog.ExecutionMode = {
    CombatSequence = "CombatSequence",
    Navigation = "Navigation",
    MovementThenCombat = "MovementThenCombat",
    CombatThenNavigation = "CombatThenNavigation",
}

GenichiroActionCatalog.CandidatePolicy = {
    Tactical = "Tactical",
    ReactionOnly = "ReactionOnly",
}

GenichiroActionCatalog.InterruptPolicy = {
    CompleteCurrentSegment = "CompleteCurrentSegment",
    ImmediateNavigationAbort = "ImmediateNavigationAbort",
    MovementThenSegmentBoundary = "MovementThenSegmentBoundary",
}

GenichiroActionCatalog.Usage = {
    Locomotion = "Locomotion",
    CombatAction = "CombatAction",
    Reaction = "Reaction",
}

GenichiroActionCatalog.DamageChannel = {
    None = "None",
    Melee = "Melee",
    Projectile = "Projectile",
}

GenichiroActionCatalog.Capability = {
    ProjectileImpact = "ProjectileImpact",
}

local SourceKind = GenichiroActionCatalog.SourceKind
local BehaviorFamily = GenichiroActionCatalog.BehaviorFamily
local ExecutionMode = GenichiroActionCatalog.ExecutionMode
local CandidatePolicy = GenichiroActionCatalog.CandidatePolicy
local InterruptPolicy = GenichiroActionCatalog.InterruptPolicy
local Usage = GenichiroActionCatalog.Usage
local DamageChannel = GenichiroActionCatalog.DamageChannel
local Capability = GenichiroActionCatalog.Capability

-- 白名单描述 UE 中可播放的时间线资产。3013 等别名资产不等于其物理动作源，不能再称为“物理动画”。
local PlayableAnimationUsageByID = {
    [3000] = Usage.CombatAction,
    [3001] = Usage.CombatAction,
    [3002] = Usage.CombatAction,
    [3003] = Usage.CombatAction,
    [3004] = Usage.CombatAction,
    [3005] = Usage.CombatAction,
    [3006] = Usage.CombatAction,
    [3007] = Usage.CombatAction,
    [3009] = Usage.CombatAction,
    [3010] = Usage.CombatAction,
    [3011] = Usage.CombatAction,
    [3013] = Usage.CombatAction,
    [3014] = Usage.CombatAction,
    [3015] = Usage.CombatAction,
    [3016] = Usage.CombatAction,
    [3017] = Usage.CombatAction,
    [3018] = Usage.CombatAction,
    [3019] = Usage.CombatAction,
    [3020] = Usage.CombatAction,
    [3021] = Usage.CombatAction,
    [3022] = Usage.CombatAction,
    [3023] = Usage.CombatAction,
    [3025] = Usage.CombatAction,
    [3028] = Usage.CombatAction,
    [3029] = Usage.CombatAction,
    [3030] = Usage.CombatAction,
    [3031] = Usage.CombatAction,
    [3032] = Usage.CombatAction,
    [3034] = Usage.CombatAction,
    [3036] = Usage.CombatAction,
    [3037] = Usage.CombatAction,
    [3038] = Usage.CombatAction,
    [3039] = Usage.CombatAction,
    [3040] = Usage.CombatAction,
    [3041] = Usage.CombatAction,
    [3044] = Usage.CombatAction,
    [3045] = Usage.CombatAction,
    [3050] = Usage.CombatAction,
    [3055] = Usage.CombatAction,
    [3060] = Usage.CombatAction,
    [3062] = Usage.CombatAction,
    [3063] = Usage.CombatAction,
    [3065] = Usage.CombatAction,
    [3067] = Usage.CombatAction,
    [3068] = Usage.CombatAction,
    [3071] = Usage.CombatAction,
    [3075] = Usage.CombatAction,
    [3076] = Usage.CombatAction,
    [3092] = Usage.Reaction,
    [3100] = Usage.Reaction,
    [3101] = Usage.Reaction,
    [3102] = Usage.Reaction,
    [3103] = Usage.Reaction,
    [5201] = Usage.Locomotion,
    [5202] = Usage.Locomotion,
    [5203] = Usage.Locomotion,
}

-- 任务 6.7 从原始 c7100.tae 的 Type 2 事件直接提取；索引键是事件源时间线，不是动作源 HKX。
local ProjectileCuesByEventSourceAnimID = {
    [3007] = { { TimeSeconds = 0.600000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3009] = { { TimeSeconds = 0.900000, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3011] = { { TimeSeconds = 1.066667, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3013] = { { TimeSeconds = 0.733333, BehaviorJudgeID = 540, DummyPolyID = 24 } },
    [3014] = { { TimeSeconds = 0.733333, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3017] = { { TimeSeconds = 0.666667, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3018] = {
        { TimeSeconds = 0.866667, BehaviorJudgeID = 500, DummyPolyID = 24 },
        { TimeSeconds = 1.133333, BehaviorJudgeID = 502, DummyPolyID = 24 },
    },
    [3020] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3021] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3022] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3023] = { { TimeSeconds = 1.533333, BehaviorJudgeID = 510, DummyPolyID = 24 } },
    [3025] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3031] = { { TimeSeconds = 0.800000, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3032] = { { TimeSeconds = 0.366667, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3034] = { { TimeSeconds = 0.633333, BehaviorJudgeID = 500, DummyPolyID = 24 } },
    [3036] = {
        { TimeSeconds = 0.633333, BehaviorJudgeID = 500, DummyPolyID = 24 },
        { TimeSeconds = 0.900000, BehaviorJudgeID = 502, DummyPolyID = 24 },
    },
    [3039] = { { TimeSeconds = 1.533333, BehaviorJudgeID = 520, DummyPolyID = 24 } },
    [3044] = {
        { TimeSeconds = 0.600000, BehaviorJudgeID = 530, DummyPolyID = 24 },
        { TimeSeconds = 0.833333, BehaviorJudgeID = 531, DummyPolyID = 24 },
        { TimeSeconds = 1.100000, BehaviorJudgeID = 532, DummyPolyID = 24 },
        { TimeSeconds = 1.566667, BehaviorJudgeID = 533, DummyPolyID = 24 },
    },
    [3062] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
    [3067] = { { TimeSeconds = 0.000000, BehaviorJudgeID = 980, DummyPolyID = 6 } },
}

---按 UE 可播放资产编号生成确定性对象路径；非整数或白名单外编号失败关闭。
---@param playable_asset_id number 已物化的 TAE 时间线或直接动画资产编号。
---@return string|nil asset_path 白名单内返回对象路径，否则返回 nil。
local function animation_path(playable_asset_id)
    if type(playable_asset_id) ~= "number"
        or playable_asset_id % 1 ~= 0
        or PlayableAnimationUsageByID[playable_asset_id] == nil then
        return nil
    end

    local asset_name = string.format(
        "Anim_Genichiro_a000_%06d",
        playable_asset_id)
    return string.format(
        "/Game/Characters/Genichiro/Animations/%s.%s",
        asset_name,
        asset_name)
end

---构造直接请求的解析记录；只用于已由 TAE 元数据确认三种编号相同的条目。
---@param action_request_id number 原 AI Goal 请求编号。
---@return GenichiroActionRequestResolution resolution 完整解析记录。
local function direct_request(action_request_id)
    local state_prefix = "W_Attack"
    if action_request_id >= 5200 and action_request_id < 5300 then
        state_prefix = "W_Step"
    end
    return {
        ActionRequestID = action_request_id,
        BehaviorStateName = state_prefix .. tostring(action_request_id),
        TAEAnimID = action_request_id,
        ReferenceType = "Direct",
        MotionSourceAnimID = action_request_id,
        EventSourceAnimID = action_request_id,
        ResolvedAssetID = action_request_id,
        bUERepresentationApproximation = false,
        Notes = "c9997 行为状态与 c7100 TAE Direct 条目已核对。",
    }
end

-- 先为全部可播放 Direct 条目建立请求清单，再用 TAE 引用和行为状态近似覆盖例外。
local ActionRequestResolutionByID = {}
for playable_asset_id, _usage in pairs(PlayableAnimationUsageByID) do
    ActionRequestResolutionByID[playable_asset_id] = direct_request(playable_asset_id)
end
ActionRequestResolutionByID[3013] = {
    ActionRequestID = 3013,
    BehaviorStateName = "W_Attack3013",
    TAEAnimID = 3013,
    ReferenceType = "ImportHKX",
    MotionSourceAnimID = 3014,
    EventSourceAnimID = 3013,
    ResolvedAssetID = 3013,
    bUERepresentationApproximation = false,
    Notes = "TAE 3013 保留自身事件，但从 3014 导入动作。",
}
ActionRequestResolutionByID[3092] = {
    ActionRequestID = 3092,
    BehaviorStateName = "W_Attack3092",
    TAEAnimID = 3092,
    ReferenceType = "ImportOtherAnim",
    MotionSourceAnimID = 8603,
    EventSourceAnimID = 8603,
    ResolvedAssetID = 3092,
    bUERepresentationApproximation = false,
    Notes = "TAE 3092 从 8603 同时导入动作与事件，UE 播放物化后的 3092 别名资产。",
}
for action_request_id = 3101, 3103 do
    ActionRequestResolutionByID[action_request_id] = {
        ActionRequestID = action_request_id,
        BehaviorStateName = "W_Attack" .. tostring(action_request_id),
        TAEAnimID = action_request_id,
        ReferenceType = "ImportHKX",
        MotionSourceAnimID = 3100,
        EventSourceAnimID = action_request_id,
        ResolvedAssetID = action_request_id,
        bUERepresentationApproximation = false,
        Notes = "TAE 保留独立事件时间线，并从 3100 导入动作。",
    }
end
ActionRequestResolutionByID[5211] = {
    ActionRequestID = 5211,
    BehaviorStateName = "W_Step5211",
    TAEAnimID = nil,
    ReferenceType = "UEApproximation",
    MotionSourceAnimID = nil,
    EventSourceAnimID = nil,
    ResolvedAssetID = 5201,
    bUERepresentationApproximation = true,
    Notes = "c9997 存在独立 W_Step5211，但 c7100 TAE 无 5211；UE 暂用 5201 动画配合长后撤导航表现。",
}

---构造动画步骤并立即解析行为请求，禁止调用方把请求编号直接格式化为资产路径。
---@param action_request_id number 原 AI Goal 提交给行为图的请求编号。
---@param damage_channels GenichiroDamageChannel[] 该步骤允许消费的伤害通道。
---@param optional boolean 条件或能力不满足时是否允许跳过。
---@param condition string|nil 条件步骤的中性表达。
---@return GenichiroAnimationStep step 完整动画步骤。
local function animation_step(action_request_id, damage_channels, optional, condition)
    local resolution = ActionRequestResolutionByID[action_request_id]
    assert(resolution ~= nil, string.format(
        "GenichiroActionCatalog: ActionRequestID %s 缺少权威解析记录",
        tostring(action_request_id)))
    local asset_path = animation_path(resolution.ResolvedAssetID)
    assert(asset_path ~= nil, string.format(
        "GenichiroActionCatalog: ResolvedAssetID %s 不在可播放资产白名单",
        tostring(resolution.ResolvedAssetID)))

    return {
        ActionRequestID = resolution.ActionRequestID,
        BehaviorStateName = resolution.BehaviorStateName,
        TAEAnimID = resolution.TAEAnimID,
        MotionSourceAnimID = resolution.MotionSourceAnimID,
        EventSourceAnimID = resolution.EventSourceAnimID,
        ResolvedAssetID = resolution.ResolvedAssetID,
        ReferenceType = resolution.ReferenceType,
        bUERepresentationApproximation = resolution.bUERepresentationApproximation,
        AssetPath = asset_path,
        Usage = PlayableAnimationUsageByID[resolution.ResolvedAssetID],
        DamageChannels = damage_channels,
        Optional = optional,
        Condition = condition,
        ProjectileCues = ProjectileCuesByEventSourceAnimID[resolution.EventSourceAnimID] or {},
    }
end

---构造动作分支；LegacyWeight 仅记录原脚本内部随机比例，不在目录加载阶段执行随机选择。
---@param variant_id string 动作内部稳定分支名。
---@param legacy_weight number 原脚本分支权重。
---@param steps GenichiroAnimationStep[] 按顺序提交的动画步骤。
---@param state_writes GenichiroActionStateWrite[]|nil 分支完成时提交的状态副作用。
---@return GenichiroActionVariant variant 动作分支定义。
local function variant(variant_id, legacy_weight, steps, state_writes)
    return {
        ID = variant_id,
        LegacyWeight = legacy_weight,
        Steps = steps,
        StateWrites = state_writes or {},
    }
end

---构造只由 ReactionRouter 提交的单段反应动作，统一使用通用 AIReaction 生命周期。
---@param action_id string UE 语义动作 ID。
---@param legacy_index number Legacy 动画或状态编号。
---@param animation_id number 反应动画物理编号。
---@param damage_channels GenichiroDamageChannel[] 动画步骤伤害通道。
---@param source_location string 原脚本追溯位置。
---@param capabilities GenichiroActionCapability[] 能力依赖。
---@param cooldowns GenichiroActionCooldown[] 冷却声明。
---@param movement GenichiroActionMovementPolicy|nil 可选前置移动策略。
---@return GenichiroActionDefinition action 完整反应动作定义。
local function reaction_action(
    action_id,
    legacy_index,
    animation_id,
    damage_channels,
    source_location,
    capabilities,
    cooldowns,
    movement)
    return {
        ID = action_id,
        BehaviorFamily = BehaviorFamily.Reaction,
        SourceKind = SourceKind.SemanticAlias,
        LegacyIndex = legacy_index,
        SourceLocation = source_location,
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = movement,
        Variants = {
            variant("Default", 100.0, {
                animation_step(animation_id, damage_channels, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = capabilities,
        Cooldowns = cooldowns,
        StateWrites = {},
        Notes = "由 ReactionRouter 提交；使用通用 AIReaction，避免复用玩家弹反或架势崩坏副作用。",
    }
end

local ActionByID = {
    ["ClosePressureCombo"] = {
        ID = "ClosePressureCombo",
        BehaviorFamily = BehaviorFamily.PressureAttack,
        SourceKind = SourceKind.Act,
        LegacyIndex = 1,
        SourceLocation = "710000_battle.dec.lua:195-230",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = {
            Policy = "ApproachTarget",
            DesiredRangeCm = 360.0,
            LegacyRangeExpression = "3.6 - SelfMapHitRadius",
        },
        Variants = {
            variant("FourHitCloseFinish", 30.0, {
                animation_step(3000, { DamageChannel.Melee }, false, nil),
                animation_step(3001, { DamageChannel.Melee }, false, nil),
                animation_step(3002, { DamageChannel.Melee }, false, nil),
                animation_step(3003, { DamageChannel.Melee }, false, nil),
            }),
            variant("MixedRangeFinish", 70.0, {
                animation_step(3000, { DamageChannel.Melee }, false, nil),
                animation_step(3001, { DamageChannel.Melee }, false, nil),
                animation_step(3010, { DamageChannel.Melee }, false, nil),
                animation_step(3025, { DamageChannel.Melee, DamageChannel.Projectile }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = { Capability.ProjectileImpact },
        Cooldowns = {
            { Key = "ClosePressureCombo", LegacyKey = 3000, Seconds = 15.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {},
        Notes = "Act01 的 30/70 四段分支；接近由 MoveGoal 执行，动画执行器只提交锁存后的分支。",
    },
    ["CloseBackAwarenessAttack"] = {
        ID = "CloseBackAwarenessAttack",
        BehaviorFamily = BehaviorFamily.PressureAttack,
        SourceKind = SourceKind.Act,
        LegacyIndex = 3,
        SourceLocation = "710000_battle.dec.lua:243-264",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = {
            Policy = "ApproachTarget",
            DesiredRangeCm = 220.0,
            LegacyRangeExpression = "2.2 - SelfMapHitRadius",
        },
        Variants = {
            variant("Default", 100.0, {
                animation_step(3005, { DamageChannel.Melee }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {
            { Key = "CloseBackAwarenessAttack", LegacyKey = 3005, Seconds = 15.0, CommitPoint = "OnComplete" },
            { Key = "CloseActionMutualExclusion", LegacyKey = "Timer0", Seconds = 7.0, CommitPoint = "OnStart" },
        },
        StateWrites = {},
        Notes = "Act03；原脚本还观察自身后方区域，UE 上下文采集负责空间事实，攻击目录不执行 Area Observe。",
    },
    ["FarGapCloser"] = {
        ID = "FarGapCloser",
        BehaviorFamily = BehaviorFamily.PressureAttack,
        SourceKind = SourceKind.Act,
        LegacyIndex = 10,
        SourceLocation = "710000_battle.dec.lua:318-336",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = {
            Policy = "ApproachTarget",
            DesiredRangeCm = 480.0,
            LegacyRangeExpression = "4.8 - SelfMapHitRadius",
        },
        Variants = {
            variant("Default", 100.0, {
                animation_step(3006, { DamageChannel.Melee }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {
            { Key = "FarGapCloser", LegacyKey = 3006, Seconds = 15.0, CommitPoint = "OnComplete" },
            { Key = "RepositionSuppression", LegacyKey = "Timer3", Seconds = 10.0, CommitPoint = "OnStart" },
        },
        StateWrites = {},
        Notes = "Act10；名称只描述 UE 战术职责，不把 3006 猜测为原版正式招式名。",
    },
    ["PhaseOpeningAssault"] = {
        ID = "PhaseOpeningAssault",
        BehaviorFamily = BehaviorFamily.PhaseOpening,
        SourceKind = SourceKind.Act,
        LegacyIndex = 15,
        SourceLocation = "710000_battle.dec.lua:350-373",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = {
            Policy = "ApproachTarget",
            DesiredRangeCm = 890.0,
            LegacyRangeExpression = "8.9 - SelfMapHitRadius",
        },
        Variants = {
            variant("Default", 100.0, {
                animation_step(3014, { DamageChannel.Projectile }, false, nil),
                animation_step(3015, { DamageChannel.Melee }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = { Capability.ProjectileImpact },
        Cooldowns = {
            { Key = "PhaseOpeningAssault", LegacyKey = 3014, Seconds = 15.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {
            { Key = "PhaseOpeningUsed", Value = true, CommitPoint = "OnStart", Notes = "Legacy Number(7)=1。" },
        },
        Notes = "Act15；阶段条件只修饰候选权重，3014/3015 仍属于普通 CombatAction 动画链。",
    },
    ["TimedStrafe"] = {
        ID = "TimedStrafe",
        BehaviorFamily = BehaviorFamily.Reposition,
        SourceKind = SourceKind.Act,
        LegacyIndex = 23,
        SourceLocation = "710000_battle.dec.lua:437-465",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.Navigation,
        InterruptPolicy = InterruptPolicy.ImmediateNavigationAbort,
        Movement = {
            Policy = "NavigationStrafe",
            DurationMinSeconds = 1.5,
            DurationMaxSeconds = 3.0,
            DirectionAngleMinDegrees = 30.0,
            DirectionAngleMaxDegrees = 45.0,
            RequiredSpaceCm = 100.0,
            DirectionMemoryKey = "RecentStrafeDirection",
        },
        Variants = {},
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {},
        StateWrites = {
            {
                Key = "RecentStrafeDirection",
                Value = "ResolvedNavigationSide",
                CommitPoint = "OnMovementSuccess",
                Notes = "Legacy Number(10) 保存左右选择。",
            },
        },
        Notes = "Act23 完全转译为 BuildMoveGoal + UE MoveTo，不允许行为树直接播放循环侧移动画。",
    },
    ["DefensiveBackstepCounter"] = {
        ID = "DefensiveBackstepCounter",
        BehaviorFamily = BehaviorFamily.RepositionAttack,
        SourceKind = SourceKind.Act,
        LegacyIndex = 24,
        SourceLocation = "710000_battle.dec.lua:467-489",
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = ExecutionMode.MovementThenCombat,
        InterruptPolicy = InterruptPolicy.MovementThenSegmentBoundary,
        Movement = {
            Policy = "NavigationSafeBackstep",
            RequiredSpaceCm = 400.0,
        },
        Variants = {
            variant("Backstep", 100.0, {
                animation_step(5201, { DamageChannel.None }, false, nil),
            }),
        },
        ConditionalFollowUps = {
            animation_step(3044, { DamageChannel.Projectile }, true,
                "SelfPostureRatio<=0.7 and LegacyPhaseFlagA"),
        },
        CapabilityDependencies = { Capability.ProjectileImpact },
        Cooldowns = {
            { Key = "RepositionSuppression", LegacyKey = "Timer3", Seconds = 30.0, CommitPoint = "OnMovementSuccess" },
        },
        StateWrites = {
            { Key = "ForceStrafeAfterAction", Value = true, CommitPoint = "OnStart", Notes = "Legacy Number(2)=1。" },
        },
        Notes = "Act24；5201 是物理表现源，空间验证和总位移由 UE 策略负责，3044 是可选弹射物反击。",
    },
    ["ClashAlternatingResponseA"] = {
        ID = "ClashAlternatingResponseA",
        BehaviorFamily = BehaviorFamily.ClashResponse,
        SourceKind = SourceKind.Kengeki,
        LegacyIndex = 1,
        SourceLocation = "710000_battle.dec.lua:989-994",
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = nil,
        Variants = {
            variant("Default", 100.0, {
                animation_step(3050, { DamageChannel.Melee }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {
            { Key = "ClashAlternatingResponseA", LegacyKey = 3050, Seconds = 8.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {
            { Key = "ClashAlternation", Value = 1, CommitPoint = "OnStart", Notes = "Legacy Number(3)=1。" },
        },
        Notes = "Kengeki01；作为 200200/200201 低连续次数的交替响应之一。",
    },
    ["ClashAlternatingResponseB"] = {
        ID = "ClashAlternatingResponseB",
        BehaviorFamily = BehaviorFamily.ClashResponse,
        SourceKind = SourceKind.Kengeki,
        LegacyIndex = 4,
        SourceLocation = "710000_battle.dec.lua:1032-1037",
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.CombatSequence,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = nil,
        Variants = {
            variant("Default", 100.0, {
                animation_step(3055, { DamageChannel.Melee }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {
            { Key = "ClashAlternatingResponseB", LegacyKey = 3055, Seconds = 8.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {
            { Key = "ClashAlternation", Value = 0, CommitPoint = "OnStart", Notes = "Legacy Number(3)=0。" },
        },
        Notes = "Kengeki04；与 Kengeki01 共同形成低连续次数交替。",
    },
    ["ClashProjectileReposition"] = {
        ID = "ClashProjectileReposition",
        BehaviorFamily = BehaviorFamily.ClashResponse,
        SourceKind = SourceKind.Kengeki,
        LegacyIndex = 3,
        SourceLocation = "710000_battle.dec.lua:1007-1030",
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.CombatThenNavigation,
        InterruptPolicy = InterruptPolicy.CompleteCurrentSegment,
        Movement = {
            Policy = "NavigationStrafe",
            DurationMinSeconds = 4.0,
            DurationMaxSeconds = 4.0,
            DirectionAngleMinDegrees = 30.0,
            DirectionAngleMaxDegrees = 45.0,
            RequiredSpaceCm = 100.0,
        },
        Variants = {
            variant("Default", 100.0, {
                animation_step(3009, { DamageChannel.Projectile }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = { Capability.ProjectileImpact },
        Cooldowns = {
            { Key = "ClashProjectileReposition", LegacyKey = 3009, Seconds = 10.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {},
        Notes = "Kengeki03；先提交 3009，再由 UE 导航完成四秒侧移。",
    },
    ["ClashBackstepCounter"] = {
        ID = "ClashBackstepCounter",
        BehaviorFamily = BehaviorFamily.ClashResponse,
        SourceKind = SourceKind.Kengeki,
        LegacyIndex = 20,
        SourceLocation = "710000_battle.dec.lua:1115-1138",
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.MovementThenCombat,
        InterruptPolicy = InterruptPolicy.MovementThenSegmentBoundary,
        Movement = {
            Policy = "NavigationSafeBackstep",
            RequiredSpaceCm = 100.0,
        },
        Variants = {
            variant("Default", 100.0, {
                animation_step(5202, { DamageChannel.None }, false, nil),
                animation_step(3007, { DamageChannel.Melee, DamageChannel.Projectile }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = { Capability.ProjectileImpact },
        Cooldowns = {
            { Key = "ClashBackstepCounter", LegacyKey = 5202, Seconds = 15.0, CommitPoint = "OnComplete" },
        },
        StateWrites = {},
        Notes = "Kengeki20；5202 保持离散位移主用途，3007 才进入战斗伤害通道。",
    },
    ["DefensiveLongBackstep"] = {
        ID = "DefensiveLongBackstep",
        BehaviorFamily = BehaviorFamily.Reposition,
        SourceKind = SourceKind.SemanticAlias,
        LegacyIndex = 5211,
        SourceLocation = "710000_battle.dec.lua:677-730",
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = ExecutionMode.MovementThenCombat,
        InterruptPolicy = InterruptPolicy.MovementThenSegmentBoundary,
        Movement = {
            Policy = "NavigationSafeLongBackstep",
            RequiredSpaceCm = 400.0,
        },
        Variants = {
            variant("Behavior5211Playable5201", 100.0, {
                animation_step(5211, { DamageChannel.None }, false, nil),
            }),
        },
        ConditionalFollowUps = {},
        CapabilityDependencies = {},
        Cooldowns = {},
        StateWrites = {},
        Notes = "5211 是独立行为请求 W_Step5211；因 TAE 缺少同号时间线，显式解析为 5201 表现并由 UE 承载可导航长后撤位移。",
    },
    ["StandardGuardReaction"] = reaction_action(
        "StandardGuardReaction", 3100, 3100, { DamageChannel.None },
        "710000_battle.dec.lua:Interrupt_Parry", {}, {
            { Key = "ReactionSuppression", LegacyKey = "ParryInterval", Seconds = 0.1, CommitPoint = "OnStart" },
        }, nil),
    ["StrongGuardReaction"] = reaction_action(
        "StrongGuardReaction", 3101, 3101, { DamageChannel.None },
        "710000_battle.dec.lua:Interrupt_Parry", {}, {}, nil),
    ["SpecialGuardReaction"] = reaction_action(
        "SpecialGuardReaction", 3102, 3102, { DamageChannel.None },
        "710000_battle.dec.lua:Interrupt_Parry", {}, {}, nil),
    ["RushGuardReaction"] = reaction_action(
        "RushGuardReaction", 3103, 3103, { DamageChannel.None },
        "710000_battle.dec.lua:Interrupt_Parry", {}, {}, nil),
    ["ForcedEndureReaction"] = reaction_action(
        "ForcedEndureReaction", 3092, 3092, { DamageChannel.None },
        "710000_battle.dec.lua:Interrupt 3710030/3710032", {}, {
            { Key = "LegacyTimer6", LegacyKey = "Timer6", Seconds = 50.0, CommitPoint = "OnStart" },
        }, nil),
    ["ConditionalProjectileReaction"] = reaction_action(
        "ConditionalProjectileReaction", 3017, 3017, { DamageChannel.Projectile },
        "710000_battle.dec.lua:Interrupt 5031", { Capability.ProjectileImpact }, {}, nil),
    ["TargetUseItemPunish"] = reaction_action(
        "TargetUseItemPunish", 3023, 3023, { DamageChannel.Projectile },
        "710000_battle.dec.lua:Interupt_Use_Item", { Capability.ProjectileImpact }, {
            { Key = "TargetUseItemPunish", LegacyKey = 3023, Seconds = 5.0, CommitPoint = "OnComplete" },
        }, { Policy = "ApproachTarget", DesiredRangeCm = 800.0 }),
}

-- 任务 10 的全量目录使用统一构造器，避免把 52 个 Legacy 函数复制成行为树分支。
-- 每个条目仍保留原函数索引、动画链、冷却键与状态副作用，运行时只消费 UE 语义字段。
local ProjectileOnlyActionRequests = {
    [3009] = true, [3011] = true, [3013] = true, [3014] = true,
    [3017] = true, [3018] = true, [3023] = true, [3031] = true,
    [3034] = true, [3036] = true, [3039] = true, [3044] = true,
}

local HybridDamageActionRequests = {
    [3007] = true, [3020] = true, [3021] = true, [3022] = true,
    [3025] = true, [3032] = true, [3062] = true, [3067] = true,
}

---按任务 3.4 的 Type 1/Type 2 审计生成行为请求的伤害通道。
---@param action_request_id number 原 AI Goal 提交给行为图的请求编号。
---@return GenichiroDamageChannel[] channels 明确的伤害通道数组。
local function damage_channels_for(action_request_id)
    local resolution = ActionRequestResolutionByID[action_request_id]
    assert(resolution ~= nil, string.format(
        "GenichiroActionCatalog: ActionRequestID %s 缺少伤害通道解析记录",
        tostring(action_request_id)))
    local usage = PlayableAnimationUsageByID[resolution.ResolvedAssetID]
    if usage == Usage.Locomotion or usage == Usage.Reaction then
        return { DamageChannel.None }
    end
    if ProjectileOnlyActionRequests[action_request_id] then
        return { DamageChannel.Projectile }
    end
    if HybridDamageActionRequests[action_request_id] then
        return { DamageChannel.Melee, DamageChannel.Projectile }
    end
    return { DamageChannel.Melee }
end

---把行为请求数组转换为经过权威解析和可播放资产白名单验证的步骤数组。
---@param action_request_ids number[] 原 AI Goal 请求编号数组。
---@return GenichiroAnimationStep[] steps 动画步骤数组。
local function steps_for(action_request_ids)
    local steps = {}
    for _index, action_request_id in ipairs(action_request_ids) do
        steps[#steps + 1] = animation_step(
            action_request_id,
            damage_channels_for(action_request_id),
            false,
            nil)
    end
    return steps
end

---登记任务 10 的 Act 或 Kengeki 数据定义，并拒绝重复语义 ID。
---@param definition GenichiroActionDefinition 完整动作定义。
---@return nil 无返回值。
local function register_action(definition)
    assert(ActionByID[definition.ID] == nil, string.format(
        "GenichiroActionCatalog: 重复动作 ID %s",
        definition.ID))
    ActionByID[definition.ID] = definition
end

---构造全量 Act 定义；移动与动画的执行顺序由 ExecutionMode 保持为 UE 可观察语义。
---@param action_id string UE 语义动作 ID。
---@param legacy_index number Act 索引。
---@param behavior_family GenichiroActionBehaviorFamily UE 行为族。
---@param execution_mode GenichiroActionExecutionMode 导航与动作执行顺序。
---@param movement GenichiroActionMovementPolicy|nil 可选移动策略。
---@param variants GenichiroActionVariant[] 动作分支。
---@param cooldowns GenichiroActionCooldown[] 冷却声明。
---@param state_writes GenichiroActionStateWrite[] 状态副作用。
---@param capabilities GenichiroActionCapability[] 能力依赖。
---@param notes string 转译边界说明。
---@return GenichiroActionDefinition action 完整动作定义。
local function full_act(action_id, legacy_index, behavior_family, execution_mode,
    movement, variants, cooldowns, state_writes, capabilities, notes)
    return {
        ID = action_id,
        BehaviorFamily = behavior_family,
        SourceKind = SourceKind.Act,
        LegacyIndex = legacy_index,
        SourceLocation = "710000_battle.dec.lua:Act" .. string.format("%02d", legacy_index),
        CandidatePolicy = CandidatePolicy.Tactical,
        ExecutionMode = execution_mode,
        InterruptPolicy = execution_mode == ExecutionMode.Navigation
            and InterruptPolicy.ImmediateNavigationAbort
            or InterruptPolicy.CompleteCurrentSegment,
        Movement = movement,
        Variants = variants,
        ConditionalFollowUps = {},
        CapabilityDependencies = capabilities,
        Cooldowns = cooldowns,
        StateWrites = state_writes,
        Notes = notes,
    }
end

---构造全量 Kengeki 定义；默认只允许 ReactionRouter 提交。
---@param action_id string UE 语义动作 ID。
---@param legacy_index number Kengeki 索引。
---@param execution_mode GenichiroActionExecutionMode 导航与动作执行顺序。
---@param movement GenichiroActionMovementPolicy|nil 可选移动策略。
---@param variants GenichiroActionVariant[] 动作分支。
---@param cooldowns GenichiroActionCooldown[] 冷却声明。
---@param state_writes GenichiroActionStateWrite[] 状态副作用。
---@param capabilities GenichiroActionCapability[] 能力依赖。
---@param notes string 转译边界说明。
---@return GenichiroActionDefinition action 完整动作定义。
local function full_kengeki(action_id, legacy_index, execution_mode, movement,
    variants, cooldowns, state_writes, capabilities, notes)
    return {
        ID = action_id,
        BehaviorFamily = BehaviorFamily.ClashResponse,
        SourceKind = SourceKind.Kengeki,
        LegacyIndex = legacy_index,
        SourceLocation = "710000_battle.dec.lua:Kengeki" .. string.format("%02d", legacy_index),
        CandidatePolicy = CandidatePolicy.ReactionOnly,
        ExecutionMode = execution_mode,
        InterruptPolicy = execution_mode == ExecutionMode.Navigation
            and InterruptPolicy.ImmediateNavigationAbort
            or InterruptPolicy.CompleteCurrentSegment,
        Movement = movement,
        Variants = variants,
        ConditionalFollowUps = {},
        CapabilityDependencies = capabilities,
        Cooldowns = cooldowns,
        StateWrites = state_writes,
        Notes = notes,
    }
end

---构造完成时提交的冷却。
---@param key string CombatMemory 冷却键。
---@param legacy_key number|string Legacy 动画键或 Timer。
---@param seconds number 冷却秒数。
---@return GenichiroActionCooldown result 冷却定义。
local function cooldown(key, legacy_key, seconds)
    return { Key = key, LegacyKey = legacy_key, Seconds = seconds, CommitPoint = "OnComplete" }
end

---构造短期状态写入。
---@param key string CombatMemory 状态键。
---@param value number|string|boolean 写入值。
---@param commit_point string 提交时点。
---@param notes string Legacy 追溯说明。
---@return GenichiroActionStateWrite result 状态写入定义。
local function state_write(key, value, commit_point, notes)
    return { Key = key, Value = value, CommitPoint = commit_point, Notes = notes }
end

-- 剩余 17 个普通 Act；第一里程碑六项保留上方逐项注释版本。
register_action(full_act("SingleSlashApproach", 2, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 320.0 },
    { variant("Default", 100.0, steps_for({ 3004 })) },
    { cooldown("Act02", 3004, 15.0) }, {}, {}, "Act02；受 Legacy Timer1 过滤。"))
register_action(full_act("LongRangeProjectile", 5, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 590.0 },
    { variant("Default", 100.0, steps_for({ 3007 })) },
    { cooldown("Act05", 3007, 15.0) }, {}, { Capability.ProjectileImpact }, "Act05；基础权重为零但保留外部入口。"))
register_action(full_act("MidPressureAttack", 6, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 520.0 },
    { variant("Default", 100.0, steps_for({ 3016 })) },
    { cooldown("Act06", 3016, 15.0), cooldown("CloseActionMutualExclusion", "Timer0", 5.0) }, {}, {}, "Act06；Timer0 未完成时降权。"))
register_action(full_act("PosturePressureCombo", 9, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 450.0 },
    { variant("Default", 100.0, steps_for({ 3040, 3041 })) },
    { cooldown("Act09LegacyKey", 3092, 15.0), cooldown("LegacyTimer6", "Timer6", 30.0) }, {}, {}, "Act09；保留与首动画不同的 3092 冷却键。"))
register_action(full_act("CloseTwoStepCombo", 11, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 320.0 },
    { variant("Default", 100.0, steps_for({ 3037, 3020 })) },
    { cooldown("Act11", 3037, 15.0) }, {}, { Capability.ProjectileImpact }, "Act11 两段动作。"))
register_action(full_act("TargetStatePunish", 16, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 250.0 },
    { variant("Default", 100.0, steps_for({ 3022 })) }, {}, {}, { Capability.ProjectileImpact }, "Act16 响应目标 109031/110125 状态。"))
register_action(full_act("ReusableReactionStrike", 20, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 320.0 },
    { variant("Default", 100.0, steps_for({ 3062 })) }, {}, {}, { Capability.ProjectileImpact }, "Act20；基础权重为零，供反应入口复用。"))
register_action(full_act("TurnToTarget", 21, BehaviorFamily.Reposition, ExecutionMode.Navigation,
    { Policy = "TurnToTarget", DurationMaxSeconds = 3.0, DirectionAngleMaxDegrees = 45.0 },
    {}, {}, {}, {}, "Act21 交给 UE Focus/控制器朝向，不播放战斗动画。"))
register_action(full_act("SideDodge", 22, BehaviorFamily.Reposition, ExecutionMode.MovementThenCombat,
    { Policy = "NavigationStrafe", RequiredSpaceCm = 200.0, DirectionAngleMaxDegrees = 45.0 },
    { variant("Left", 50.0, steps_for({ 5202 })), variant("Right", 50.0, steps_for({ 5203 })) },
    {}, {}, {}, "Act22；导航先验证侧向空间，再播放离散侧闪表现。"))
register_action(full_act("Retreat", 25, BehaviorFamily.Reposition, ExecutionMode.Navigation,
    { Policy = "RetreatFromTarget", RequiredSpaceCm = 100.0, DurationMinSeconds = 2.0, DurationMaxSeconds = 4.0 },
    {}, {}, {}, {}, "Act25；基础权重为零的普通后退。"))
register_action(full_act("TacticalHold", 26, BehaviorFamily.Reposition, ExecutionMode.Navigation,
    { Policy = "Hold", DurationMinSeconds = 0.5, DurationMaxSeconds = 0.5 },
    {}, {}, {}, {}, "Act26；由行为树 Wait 表现，基础权重为零。"))
register_action(full_act("MaintainTargetRange", 27, BehaviorFamily.Reposition, ExecutionMode.Navigation,
    { Policy = "MaintainTargetRange", DesiredRangeCm = 650.0, DurationMinSeconds = 1.5, DurationMaxSeconds = 3.0 },
    {}, {}, {}, {}, "Act27；原分支不可达但保留完整移动语义。"))
register_action(full_act("ContextReposition", 28, BehaviorFamily.Reposition, ExecutionMode.Navigation,
    { Policy = "ContextReposition", DesiredRangeCm = 300.0, DurationMinSeconds = 1.0, DurationMaxSeconds = 3.0 },
    {}, {}, {}, {}, "Act28；按目标特殊状态选择侧移或接近距离。"))
register_action(full_act("ProjectileVolleyFollowUp", 30, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    nil, { variant("Default", 100.0, steps_for({ 3009, 3044 })) },
    { cooldown("Act30LegacyKey", 3006, 15.0) }, {}, { Capability.ProjectileImpact }, "Act30；保留 3006 共享冷却键。"))
register_action(full_act("CloseCounterChain", 31, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 360.0 },
    { variant("Default", 100.0, steps_for({ 3003, 3045 })) },
    { cooldown("Act31", 3045, 15.0) }, {}, {}, "Act31 两段近距反击链。"))
register_action(full_act("FarProjectileChain", 34, BehaviorFamily.PressureAttack, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 590.0 },
    { variant("Default", 100.0, steps_for({ 3007, 3011 })) },
    { cooldown("Act34", 3007, 15.0) }, {}, { Capability.ProjectileImpact }, "Act34 两段远距弹射物链。"))
register_action(full_act("TargetStateOpeningAssault", 48, BehaviorFamily.PhaseOpening, ExecutionMode.CombatSequence,
    { Policy = "ApproachTarget", DesiredRangeCm = 890.0 },
    { variant("Default", 100.0, steps_for({ 3013, 3015 })) },
    { cooldown("Act48", 3013, 5.0) },
    { state_write("PhaseOpeningUsed", true, "OnComplete", "Legacy Number7=1") },
    { Capability.ProjectileImpact }, "Act48；目标 109900 时的远距惩罚。"))

-- 剩余 25 个 Kengeki；零权重实现和未注册 K47 仍可被 Legacy 适配器显式追溯。
register_action(full_kengeki("ClashResponseK02", 2, ExecutionMode.MovementThenCombat,
    { Policy = "NavigationSafeBackstep", RequiredSpaceCm = 200.0 },
    { variant("Default", 100.0, steps_for({ 5201 })) },
    { cooldown("Kengeki02", 5201, 10.0), cooldown("RepositionSuppression", "Timer3", 30.0) },
    { state_write("ForceStrafeAfterAction", true, "OnComplete", "Legacy Number2=1") }, {}, "K02 后闪。"))
ActionByID.ClashResponseK02.ConditionalFollowUps = {
    animation_step(3044, { DamageChannel.Projectile }, true, "SelfPostureRatio<=0.7 and LegacyPhaseFlagA"),
}
register_action(full_kengeki("ClashResponseK07", 7, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3060 })) }, { cooldown("Kengeki07", 3060, 8.0) }, {}, {}, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK09", 9, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3018, 3015 })) }, { cooldown("Kengeki09", 3018, 8.0) }, {}, { Capability.ProjectileImpact }, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK10", 10, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3065 })) }, { cooldown("Kengeki10", 3065, 8.0) }, {}, {}, "200211 响应。"))
register_action(full_kengeki("ClashResponseK13", 13, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3075 })) }, { cooldown("Kengeki13", 3075, 8.0) }, {}, {}, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK14", 14, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3076 })) }, { cooldown("Kengeki14", 3076, 8.0) }, {}, {}, "200215/200216 低连续次数交替响应。"))
register_action(full_kengeki("ClashResponseK15", 15, ExecutionMode.CombatSequence, nil, {
    variant("BranchAForceStrafe", 25.0, steps_for({ 3031, 3019, 3029 }), {
        state_write("ForceStrafeAfterAction", true, "OnComplete", "Legacy 50% Number2=1"),
    }),
    variant("BranchA", 25.0, steps_for({ 3031, 3019, 3029 })),
    variant("BranchBForceStrafe", 25.0, steps_for({ 3031, 3036 }), {
        state_write("ForceStrafeAfterAction", true, "OnComplete", "Legacy 50% Number2=1"),
    }),
    variant("BranchB", 25.0, steps_for({ 3031, 3036 })),
}, { cooldown("Kengeki15", 3031, 15.0) }, {}, { Capability.ProjectileImpact }, "保留两个独立 50% 分支的四种组合。"))
register_action(full_kengeki("ClashResponseK17", 17, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3071 })) }, { cooldown("Kengeki17", 3071, 8.0) }, {}, {}, "200210 响应。"))
register_action(full_kengeki("ClashResponseK18", 18, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3004 })) }, { cooldown("Kengeki18", 3004, 8.0) }, {}, {}, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK19", 19, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3044 })) }, {}, {}, { Capability.ProjectileImpact }, "零权重弹射物响应。"))
register_action(full_kengeki("ClashResponseK21", 21, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3010 })) }, {}, {}, {}, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK30", 30, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3063 })) },
    { cooldown("Kengeki30", 3063, 15.0), cooldown("ClashActionSuppression", "Timer1", 10.0) },
    { state_write("LegacyNumber5", 0, "OnComplete", "Legacy Number5=0") }, {}, "200200 高连续次数低权重响应。"))
register_action(full_kengeki("ClashResponseK31", 31, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3068 })) },
    { cooldown("Kengeki31", 3068, 15.0), cooldown("ClashActionSuppression", "Timer1", 10.0) },
    { state_write("LegacyNumber5", 0, "OnComplete", "Legacy Number5=0") }, {}, "200210/200211/200215 响应。"))
register_action(full_kengeki("ClashResponseK32", 32, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3018, 3015 })) }, { cooldown("Kengeki32", 3018, 15.0) },
    { state_write("HighClashAlternation", 1, "OnComplete", "Legacy Number6=1") }, { Capability.ProjectileImpact }, "高连续拼刀交替 A。"))
register_action(full_kengeki("ClashResponseK33", 33, ExecutionMode.CombatSequence, nil, {
    variant("Long", 50.0, steps_for({ 3018, 3019, 3029 })),
    variant("Short", 50.0, steps_for({ 3018, 3019 })),
}, { cooldown("Kengeki33LegacyKey", 3007, 15.0) },
    { state_write("HighClashAlternation", 0, "OnComplete", "Legacy Number6=0") }, { Capability.ProjectileImpact }, "高连续拼刀交替 B。"))
register_action(full_kengeki("ClashResponseK34", 34, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3037, 3020 })) }, { cooldown("Kengeki34", 3037, 15.0) }, {}, { Capability.ProjectileImpact }, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK35", 35, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3016 })) }, { cooldown("Kengeki35", 3016, 8.0) },
    { state_write("LegacyNumber5", 0, "OnComplete", "Legacy Number5=0") }, {}, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK38", 38, ExecutionMode.CombatSequence, nil, {
    variant("LongFollowUp", 75.0, steps_for({ 3030, 3067 })),
    variant("ShortFollowUp", 25.0, steps_for({ 3030, 3025 })),
}, { cooldown("Kengeki38", 3030, 8.0) }, {}, { Capability.ProjectileImpact }, "忍杀数大于一时应固定短分支。"))
register_action(full_kengeki("ClashResponseK39", 39, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3034, 3036, 3015 })) },
    { cooldown("Kengeki39", 3034, 15.0) }, {}, { Capability.ProjectileImpact }, "高连续三段弹射物响应。"))
register_action(full_kengeki("ClashResponseK40", 40, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3028 })) },
    { cooldown("Kengeki40", 3028, 15.0), cooldown("LegacyTimer6", "Timer6", 50.0) }, {}, {}, "低生命且 Timer6 完成时响应。"))
register_action(full_kengeki("ClashResponseK43", 43, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3062 })) }, { cooldown("Kengeki43", 3062, 15.0) },
    { state_write("ForceStrafeAfterAction", true, "OnComplete", "Legacy Number2=1") }, { Capability.ProjectileImpact }, "高连续拼刀响应并强制下一次侧移。"))
register_action(full_kengeki("ClashResponseK44", 44, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3067 })) }, { cooldown("Kengeki44", 3067, 15.0) },
    { state_write("ForceStrafeAfterAction", true, "OnComplete", "Legacy Number2=1") }, { Capability.ProjectileImpact }, "零权重保留响应。"))
register_action(full_kengeki("ClashResponseK45", 45, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3045 })) }, { cooldown("Kengeki45LegacyKey", 3032, 15.0) },
    { state_write("DeflectChainCount", 0, "OnComplete", "Legacy Number0=0") }, {}, "保留与动作链不同的 3032 冷却键。"))
register_action(full_kengeki("ClashResponseK46", 46, ExecutionMode.CombatThenNavigation,
    { Policy = "NavigationStrafe", RequiredSpaceCm = 250.0, DurationMinSeconds = 2.5, DurationMaxSeconds = 2.5, FixedDirection = "Left" },
    { variant("Default", 100.0, steps_for({ 3039 })) }, { cooldown("LegacyTimer4", "Timer4", 10.0) }, {}, { Capability.ProjectileImpact }, "零权重；动画后固定左侧移。"))
register_action(full_kengeki("ClashResponseK47", 47, ExecutionMode.CombatSequence, nil,
    { variant("Default", 100.0, steps_for({ 3038 })) }, { cooldown("LegacyTimer4", "Timer4", 10.0) }, {}, {}, "有实现但原注册表未注册。"))

local FirstMilestoneActionIDs = {
    "ClosePressureCombo",
    "CloseBackAwarenessAttack",
    "FarGapCloser",
    "PhaseOpeningAssault",
    "TimedStrafe",
    "DefensiveBackstepCounter",
    "ClashAlternatingResponseA",
    "ClashAlternatingResponseB",
    "ClashProjectileReposition",
    "ClashBackstepCounter",
    "DefensiveLongBackstep",
}

local ActionIDByLegacyKey = {}
for action_id, definition in pairs(ActionByID) do
    local legacy_key = string.format(
        "%s:%d",
        definition.SourceKind,
        definition.LegacyIndex)
    assert(ActionIDByLegacyKey[legacy_key] == nil, string.format(
        "GenichiroActionCatalog: 重复 Legacy 映射 %s",
        legacy_key))
    ActionIDByLegacyKey[legacy_key] = action_id
end

local LegacyStateAliasByID = {
    [5211] = {
        ActionRequestID = 5211,
        ActionID = "DefensiveLongBackstep",
        BehaviorStateName = ActionRequestResolutionByID[5211].BehaviorStateName,
        TAEAnimID = ActionRequestResolutionByID[5211].TAEAnimID,
        MotionSourceAnimID = ActionRequestResolutionByID[5211].MotionSourceAnimID,
        EventSourceAnimID = ActionRequestResolutionByID[5211].EventSourceAnimID,
        ResolvedAssetID = ActionRequestResolutionByID[5211].ResolvedAssetID,
        bUERepresentationApproximation = true,
        AssetPath = assert(animation_path(5201)),
        MovementPolicy = "NavigationSafeLongBackstep",
        SourceLocation = "710000_battle.dec.lua:677-730",
    },
}

GenichiroActionCatalog.PlayableAnimationUsageByID = PlayableAnimationUsageByID
GenichiroActionCatalog.ProjectileCuesByEventSourceAnimID = ProjectileCuesByEventSourceAnimID
GenichiroActionCatalog.ActionRequestResolutionByID = ActionRequestResolutionByID
GenichiroActionCatalog.ActionByID = ActionByID
GenichiroActionCatalog.LegacyStateAliasByID = LegacyStateAliasByID

---查询 UE 语义动作；未知 ID 返回 nil，调用方必须安全结束当前候选而不是使用默认攻击替代。
---@param action_id string UE 语义动作稳定 ID。
---@return GenichiroActionDefinition|nil action 已登记动作；未知 ID 返回 nil。
function GenichiroActionCatalog.GetAction(action_id)
    return ActionByID[action_id]
end

---按原始 Act、Kengeki 或语义别名编号查询动作，供追溯适配层使用。
---@param source_kind GenichiroActionSourceKind 原始来源类别。
---@param legacy_index number 原始函数或状态请求编号。
---@return GenichiroActionDefinition|nil action 已登记动作；尚未迁移的条目返回 nil。
function GenichiroActionCatalog.GetActionByLegacy(source_kind, legacy_index)
    local legacy_key = string.format(
        "%s:%d",
        source_kind,
        legacy_index)
    local action_id = ActionIDByLegacyKey[legacy_key]
    return ActionByID[action_id]
end

---查询 5211 等 Legacy 状态请求到行为状态、TAE 来源、UE 表现和位移策略的显式别名。
---@param legacy_state_id number 原始 EzState 或动作请求编号。
---@return GenichiroLegacyStateAlias|nil alias 已登记别名；未知状态请求返回 nil。
function GenichiroActionCatalog.GetLegacyStateAlias(legacy_state_id)
    return LegacyStateAliasByID[legacy_state_id]
end

---查询行为请求到行为状态、TAE、动作源、事件源和 UE 可播放资产的权威解析记录。
---@param action_request_id number 原 AI Goal 提交给行为图的请求编号。
---@return GenichiroActionRequestResolution|nil resolution 已核对的解析记录；未知请求返回 nil。
function GenichiroActionCatalog.GetActionRequestResolution(action_request_id)
    return ActionRequestResolutionByID[action_request_id]
end

---返回 UE 可播放动画资产的对象路径；行为请求必须先调用 GetActionRequestResolution。
---@param playable_asset_id number 已物化的 TAE 时间线或直接动画资产编号。
---@return string|nil asset_path 白名单内返回对象路径，否则返回 nil。
function GenichiroActionCatalog.GetAnimationPath(playable_asset_id)
    return animation_path(playable_asset_id)
end

---查询 UE 可播放动画资产的主用途，供执行器决定导航、战斗动作或反应所有权。
---@param playable_asset_id number 已物化的 TAE 时间线或直接动画资产编号。
---@return GenichiroAnimationUsage|nil usage 任务 3.5 用途；白名单外返回 nil。
function GenichiroActionCatalog.GetAnimationUsage(playable_asset_id)
    return PlayableAnimationUsageByID[playable_asset_id]
end

---返回第一里程碑动作 ID 的副本，避免调用方修改模块内部稳定顺序。
---@return string[] action_ids 第一里程碑六个 Act、四个拼刀响应和一个 5211 别名动作。
function GenichiroActionCatalog.GetFirstMilestoneActionIDs()
    local action_ids = {}
    for index, action_id in ipairs(FirstMilestoneActionIDs) do
        action_ids[index] = action_id
    end
    return action_ids
end

return GenichiroActionCatalog
