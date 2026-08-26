-- Lua 类型：纯 Lua 类/数据/工具模块。本文件不绑定 UObject，self（如有）只表示 Lua 表实例。
-- 集中封装弦一郎 BehaviorTree Task 与 UE 对象交互时的安全查询、Blackboard 契约和向量计算。
-- 所有函数均允许离线 Mock 调用；模块加载阶段不访问 UE 全局，避免静态测试依赖编辑器世界。

local GenichiroRuntime = {}

GenichiroRuntime.Keys = {
    TargetActor = "TargetActor",
    BossPhase = "BossPhase",
    TacticalIntent = "TacticalIntent",
    SelectedActionId = "SelectedActionId",
    MoveGoal = "MoveGoal",
    ReactionType = "ReactionType",
    ReactionPriority = "ReactionPriority",
    DistanceCm = "DistanceCm",
    DistanceBand = "DistanceBand",
    bCanMoveLeft = "bCanMoveLeft",
    bCanMoveRight = "bCanMoveRight",
    bCanMoveBack = "bCanMoveBack",
    bProjectileCapabilityReady = "bProjectileCapabilityReady",
    bActionLocked = "bActionLocked",
    ActionSerial = "ActionSerial",
    bPhaseTransitionPending = "bPhaseTransitionPending",
    DebugDecisionSerial = "DebugDecisionSerial",
    DebugCandidateSummary = "DebugCandidateSummary",
    DebugFailureReason = "DebugFailureReason",
    LegacyPhaseFlagA = "LegacyPhaseFlagA",
    LegacyPhaseFlagB = "LegacyPhaseFlagB",
    TargetPunishWindow = "TargetPunishWindow",
    TargetRepositionRestricted = "TargetRepositionRestricted",
    TargetSpecialAction = "TargetSpecialAction",
    TargetInFront = "TargetInFront",
    TargetBehind = "TargetBehind",
    TargetStatePunish = "TargetStatePunish",
    TargetState110030 = "TargetState110030",
    SelfHealthRatio = "SelfHealthRatio",
    ExternalTimer7Ready = "ExternalTimer7Ready",
}

---判断对象引用是否可供本帧继续使用；离线 Mock 没有 UE.IsValid 时按非 nil 处理。
---@param object userdata|table|nil 待检查对象。
---@return boolean valid 对象是否可访问。
function GenichiroRuntime.IsObjectValid(object)
    if object == nil then
        return false
    end
    if UE ~= nil and UE.IsValid ~= nil then
        local ok, valid = pcall(UE.IsValid, object)
        if ok then
            return valid == true
        end
    end
    if object.IsActorBeingDestroyed ~= nil then
        local ok, destroying = pcall(object.IsActorBeingDestroyed, object)
        if ok and destroying == true then
            return false
        end
    end
    return true
end

---查询 Pawn 的战斗组件；非项目角色或失效对象安全返回 nil。
---@param pawn APawn|table|nil 当前行为树 Pawn。
---@return USKCombatComponent|table|nil combat_component 可用战斗组件。
function GenichiroRuntime.GetCombatComponent(pawn)
    if not GenichiroRuntime.IsObjectValid(pawn) or pawn.GetCombatComponent == nil then
        return nil
    end
    local ok, component = pcall(pawn.GetCombatComponent, pawn)
    if not ok then
        return nil
    end
    return component
end

---按函数名调用 Actor 的无参位置查询，并隔离 UnLua 反射查找或调用异常。
---该内部入口只服务 GetActorLocation；使用显式函数避免 pcall 匿名回调破坏 Lua 文档覆盖率。
---@param actor AActor|table 待查询 Actor。
---@param method_name string 运行时反射函数名。
---@return FVector|table|nil location 调用成功时的位置；接口缺失或抛错时为 nil。
local function call_actor_location_method(actor, method_name)
    local method = actor[method_name]
    if method == nil then
        return nil
    end
    -- UnLua 的反射函数通常表现为 Lua function，但这里也允许带 __call 的代理对象。
    return method(actor)
end

---安全读取 Actor 世界位置。
---UE 5.2 将蓝图可调用位置接口反射为 K2_GetActorLocation；旧 Mock 或兼容对象才回退 GetActorLocation。
---@param actor AActor|table|nil 待查询 Actor。
---@return FVector|table|nil location 可用世界位置；对象失效、接口缺失或调用异常时为 nil。
function GenichiroRuntime.GetActorLocation(actor)
    if not GenichiroRuntime.IsObjectValid(actor) then
        return nil
    end

    local ok_k2, k2_location = pcall(
        call_actor_location_method,
        actor,
        "K2_GetActorLocation")
    if ok_k2 and k2_location ~= nil then
        return k2_location
    end

    -- 兼容离线工具和尚未迁移的测试替身；正式 UE 运行时不依赖这个别名。
    local ok_legacy, legacy_location = pcall(
        call_actor_location_method,
        actor,
        "GetActorLocation")
    if ok_legacy then
        return legacy_location
    end
    return nil
end

---读取世界秒数；无 World 的离线环境使用调用方提供的回退值。
---@param context_object UObject|table|nil 能解析 UWorld 的上下文对象。
---@param fallback_seconds number|nil 无世界时使用的秒数。
---@return number seconds 当前世界秒数或非负回退值。
function GenichiroRuntime.GetWorldTimeSeconds(context_object, fallback_seconds)
    local fallback = math.max(tonumber(fallback_seconds) or 0.0, 0.0)
    if context_object == nil or context_object.GetWorld == nil then
        return fallback
    end
    local ok_world, world = pcall(context_object.GetWorld, context_object)
    if not ok_world or world == nil or world.GetTimeSeconds == nil then
        return fallback
    end
    local ok_time, seconds = pcall(world.GetTimeSeconds, world)
    if not ok_time or type(seconds) ~= "number" then
        return fallback
    end
    return math.max(seconds, 0.0)
end

---将 UE FName、字符串或 nil 归一化为稳定文本。
---@param value userdata|string|nil 待转换值。
---@return string text nil 和 None 返回空字符串，其余返回 tostring 文本。
function GenichiroRuntime.NameToString(value)
    if value == nil then
        return ""
    end
    local text = tostring(value)
    if text == "None" then
        return ""
    end
    return text
end

---比较 UE 枚举值；编辑器中优先比较真实枚举，离线测试回退为末段名称比较。
---@param value userdata|number|string 待比较枚举值。
---@param enum_table_name string UE 全局下的枚举表名。
---@param member_name string 枚举成员名。
---@return boolean equal 是否为指定枚举成员。
function GenichiroRuntime.EnumEquals(value, enum_table_name, member_name)
    if UE ~= nil and UE[enum_table_name] ~= nil
        and UE[enum_table_name][member_name] ~= nil then
        return value == UE[enum_table_name][member_name]
    end
    local text = tostring(value)
    return text == member_name or string.match(text, "%.([%w_]+)$") == member_name
end

---创建 UE FVector；离线环境返回带 X/Y/Z 字段的普通表。
---@param x number X 分量。
---@param y number Y 分量。
---@param z number Z 分量。
---@return FVector|table vector 新向量。
function GenichiroRuntime.MakeVector(x, y, z)
    if UE ~= nil and UE.FVector ~= nil then
        return UE.FVector(x, y, z)
    end
    return { X = x, Y = y, Z = z }
end

---读取 FVector 分量；同时兼容 UE 大写字段和离线小写字段。
---@param vector FVector|table 向量值。
---@param component string X、Y 或 Z。
---@return number value 数值分量；缺失时为零。
function GenichiroRuntime.GetVectorComponent(vector, component)
    if vector == nil then
        return 0.0
    end
    local value = vector[component]
    if value == nil then
        value = vector[string.lower(component)]
    end
    return tonumber(value) or 0.0
end

---计算起点沿方向偏移后的世界位置，不依赖 FVector 运算符重载。
---@param origin FVector|table 起点。
---@param direction FVector|table 方向向量。
---@param distance_cm number 偏移厘米数，可为负。
---@return FVector|table location 偏移后的世界位置。
function GenichiroRuntime.OffsetLocation(origin, direction, distance_cm)
    return GenichiroRuntime.MakeVector(
        GenichiroRuntime.GetVectorComponent(origin, "X")
            + GenichiroRuntime.GetVectorComponent(direction, "X") * distance_cm,
        GenichiroRuntime.GetVectorComponent(origin, "Y")
            + GenichiroRuntime.GetVectorComponent(direction, "Y") * distance_cm,
        GenichiroRuntime.GetVectorComponent(origin, "Z")
            + GenichiroRuntime.GetVectorComponent(direction, "Z") * distance_cm)
end

---计算 From 指向 To 的水平单位向量；重合时返回零向量。
---@param from_location FVector|table 起点。
---@param to_location FVector|table 终点。
---@return FVector|table direction 水平单位方向。
function GenichiroRuntime.HorizontalDirection(from_location, to_location)
    local delta_x = GenichiroRuntime.GetVectorComponent(to_location, "X")
        - GenichiroRuntime.GetVectorComponent(from_location, "X")
    local delta_y = GenichiroRuntime.GetVectorComponent(to_location, "Y")
        - GenichiroRuntime.GetVectorComponent(from_location, "Y")
    local length = math.sqrt(delta_x * delta_x + delta_y * delta_y)
    if length <= 0.001 then
        return GenichiroRuntime.MakeVector(0.0, 0.0, 0.0)
    end
    return GenichiroRuntime.MakeVector(delta_x / length, delta_y / length, 0.0)
end

---通过 ASKAIController 通用接口投影导航点；接口缺失时失败关闭。
---@param controller AAIController|table|nil 当前行为树控制器。
---@param candidate_location FVector|table 候选世界位置。
---@param query_extent FVector|table 导航查询范围。
---@return boolean projected 是否投影成功。
---@return FVector|table|nil location 成功时的导航位置。
function GenichiroRuntime.ProjectNavigationPoint(controller, candidate_location, query_extent)
    if controller == nil or controller.ProjectNavigationPoint == nil then
        return false, nil
    end
    local ok, projected, location = pcall(
        controller.ProjectNavigationPoint,
        controller,
        candidate_location,
        query_extent)
    if not ok or projected ~= true then
        return false, nil
    end
    return true, location
end

---从 Blackboard 读取当前决策快照，字段与 TacticalProfile 数据契约一一对应。
---@param pawn APawn|table 当前弦一郎 Pawn。
---@param blackboard UBlackboardComponent|table 当前黑板。
---@param fallback_seconds number|nil 离线时间回退。
---@return GenichiroDecisionContext context 决策快照。
function GenichiroRuntime.CaptureDecisionContext(pawn, blackboard, fallback_seconds)
    local keys = GenichiroRuntime.Keys
    local combat_component = GenichiroRuntime.GetCombatComponent(pawn)
    local posture_ratio = 0.0
    if combat_component ~= nil and combat_component.GetPostureNormalized ~= nil then
        posture_ratio = tonumber(combat_component:GetPostureNormalized()) or 0.0
    end
    return {
        DistanceCm = tonumber(blackboard:GetValueAsFloat(keys.DistanceCm)) or 0.0,
        NowSeconds = GenichiroRuntime.GetWorldTimeSeconds(pawn, fallback_seconds),
        SelfFlags = {
            LegacyPhaseFlagA = blackboard:GetValueAsBool(keys.LegacyPhaseFlagA) == true,
            LegacyPhaseFlagB = blackboard:GetValueAsBool(keys.LegacyPhaseFlagB) == true,
        },
        TargetFlags = {
            TargetPunishWindow = blackboard:GetValueAsBool(keys.TargetPunishWindow) == true,
            TargetRepositionRestricted = blackboard:GetValueAsBool(keys.TargetRepositionRestricted) == true,
            TargetSpecialAction = blackboard:GetValueAsBool(keys.TargetSpecialAction) == true,
            TargetInFront = blackboard:GetValueAsBool(keys.TargetInFront) == true,
            TargetBehind = blackboard:GetValueAsBool(keys.TargetBehind) == true,
            TargetStatePunish = blackboard:GetValueAsBool(keys.TargetStatePunish) == true,
            TargetState110030 = blackboard:GetValueAsBool(keys.TargetState110030) == true,
        },
        Space = {
            Left = blackboard:GetValueAsBool(keys.bCanMoveLeft) == true,
            Right = blackboard:GetValueAsBool(keys.bCanMoveRight) == true,
            Back = blackboard:GetValueAsBool(keys.bCanMoveBack) == true,
        },
        Capabilities = {
            ProjectileImpact = blackboard:GetValueAsBool(keys.bProjectileCapabilityReady) == true,
        },
        ExternalTimer7Ready = blackboard:GetValueAsBool(keys.ExternalTimer7Ready) == true,
        SelfPostureRaw = posture_ratio * 1000.0,
        SelfPostureRatio = posture_ratio,
        SelfHealthRatio = math.max(
            tonumber(blackboard:GetValueAsFloat(keys.SelfHealthRatio)) or 1.0,
            0.001),
        BossPhase = blackboard:GetValueAsInt(keys.BossPhase),
    }
end

---将动作定义映射为 UE 行为树可观察意图，保留移动和战斗的执行顺序。
---@param action GenichiroActionDefinition 动作目录定义。
---@return string intent TacticalIntent 名称。
function GenichiroRuntime.ResolveTacticalIntent(action)
    if action.ExecutionMode == "Navigation" then
        return "Reposition"
    end
    if action.ExecutionMode == "MovementThenCombat" then
        return "MoveThenCombat"
    end
    if action.ExecutionMode == "CombatThenNavigation" then
        return "CombatThenMove"
    end
    if action.Movement ~= nil then
        return "ApproachThenCombat"
    end
    return "CombatAction"
end

---把候选和选中结果压缩成单行调试摘要，避免默认每帧输出日志。
---@param candidates GenichiroWeightedCandidate[] 候选数组。
---@param selected GenichiroWeightedCandidate|nil 选中候选。
---@return string summary 可写入 CombatMemory 或 Blackboard 的摘要。
function GenichiroRuntime.BuildCandidateSummary(candidates, selected)
    local entries = {}
    for _index, candidate in ipairs(candidates) do
        entries[#entries + 1] = string.format("%s=%.2f", candidate.ActionID, candidate.Weight)
    end
    return string.format(
        "Candidates[%s];Selected=%s",
        table.concat(entries, ","),
        selected ~= nil and selected.ActionID or "None")
end

return GenichiroRuntime
