local StateMachine = {}

StateMachine.__index = StateMachine
StateMachine.__call = function(class, config)
    return class:new(config)
end
StateMachine.ClassName = "LuaAnimStateMachine"
StateMachine.IsLuaAnimStateMachine = true
StateMachine.LayerName = "Default"
StateMachine.InitialState = "Idle"
StateMachine.DefaultBlendTime = 0.15
StateMachine.DefaultLoop = true
StateMachine.Debug = false
StateMachine.DebugUpdateInterval = 0.5
StateMachine.DebugTransitions = true
StateMachine.DebugTransitionChecks = false
StateMachine.DebugPose = true
StateMachine.DebugPoseEveryFrame = false
StateMachine.DebugContext = true
StateMachine.States = {
    Idle = "Idle",
}
StateMachine.AnimationSettings = {}
StateMachine.Assets = {}

local function copy_table(source)
    local target = {}
    for key, value in pairs(source or {}) do
        target[key] = value
    end
    return target
end

local function define_table(target, source)
    for key, value in pairs(source or {}) do
        target[key] = value
    end

    return target
end

local function try_call(call)
    local ok, result = pcall(call)
    if ok then
        return result
    end

    return nil
end

local function resolve_context_object(context)
    if context == nil then
        return nil
    end

    local object = try_call(function()
        return context.Object
    end)
    if object ~= nil then
        return object
    end

    if type(context) ~= "table" then
        return context
    end

    return nil
end

local function copy_transition_order(source)
    local target = {}
    for index, edge in ipairs(source or {}) do
        target[index] = {
            From = edge.From,
            To = edge.To,
            Name = edge.Name,
        }
    end

    return target
end

local RuntimeFieldSkip = {
    Snapshot = true,
    Class = true,
    Super = true,
    super = true,
    State = true,
    States = true,
    StateList = true,
    Assets = true,
    AnimationSettings = true,
    Tuning = true,
    LayerName = true,
    EntryState = true,
    InitialState = true,
    LastFacts = true,
    LastDecision = true,
    Context = true,
    ContextObject = true,
    RuntimeContext = true,
    UpdateContext = true,
}

local function value_to_debug_string(value)
    if value == nil then
        return "nil"
    end

    if type(value) == "number" then
        return string.format("%.3f", value)
    end

    return tostring(value)
end

local function short_asset_path(path)
    if path == nil then
        return "nil"
    end

    local text = tostring(path)
    local name = string.match(text, "([^/%.]+)%.([^/%.]+)$")
    if name ~= nil then
        return name
    end

    return text
end

local function read_context_index(instance, key)
    local context = rawget(instance, "ContextObject")
    if context == nil then
        return nil
    end

    local ok, value = pcall(function()
        return context[key]
    end)
    if not ok or value == nil then
        return nil
    end

    if type(value) ~= "function" then
        return value
    end

    return function(_self, ...)
        return value(context, ...)
    end
end

local function make_instance_metatable(class)
    return {
        __index = function(instance, key)
            local value = class[key]
            if value ~= nil then
                return value
            end

            return read_context_index(instance, key)
        end,
    }
end

local function remember_transition(target, key, value)
    if type(value) ~= "function" or type(key) ~= "string" then
        return
    end

    local from_state, to_state = string.match(key, "^CanEnter_([^_]+)_(.+)$")
    if from_state == nil or to_state == nil then
        return
    end

    local transition_order = rawget(target, "__TransitionOrder")
    if transition_order == nil then
        transition_order = {}
        rawset(target, "__TransitionOrder", transition_order)
    end

    for _, edge in ipairs(transition_order) do
        if edge.Name == key then
            return
        end
    end

    table.insert(transition_order, {
        From = from_state,
        To = to_state,
        Name = key,
    })
end

StateMachine.__newindex = function(target, key, value)
    remember_transition(target, key, value)
    rawset(target, key, value)
end

function StateMachine:Extend(class_name, definition)
    local child = copy_table(self)
    child.__index = child
    child.super = self
    child.Super = self
    child.ClassName = class_name or "LuaAnimStateMachine"
    child.__TransitionOrder = copy_transition_order(rawget(self, "__TransitionOrder"))
    setmetatable(child, self)
    return define_table(child, definition)
end

function StateMachine:Class(class_name, definition)
    return self:Extend(class_name, definition)
end

function StateMachine:Derive(class_name, definition)
    return self:Extend(class_name, definition)
end

function StateMachine:Define(definition)
    return define_table(self, definition)
end

function StateMachine:new(config)
    local instance = {}
    setmetatable(instance, make_instance_metatable(self))
    instance.Class = self
    instance.LastFacts = nil
    instance.LastDecision = nil

    for key, value in pairs(config or {}) do
        instance[key] = value
    end

    if type(instance.__init) == "function" then
        instance:__init(config)
    elseif type(instance.Initialize) == "function" then
        instance:Initialize(config)
    end

    return instance
end

function StateMachine:Create(config)
    return self:new(config)
end

function StateMachine:Initialize(_config)
end

function StateMachine:Configure(_context)
end

function StateMachine:IsDebugEnabled()
    return self.Debug == true
end

function StateMachine:IsDebugFlagEnabled(flag_name)
    if not self:IsDebugEnabled() then
        return false
    end

    local value = self[flag_name]
    if value == nil and self.Class ~= nil then
        value = self.Class[flag_name]
    end
    if value == nil then
        return true
    end

    return value == true
end

function StateMachine:GetDebugName()
    return self.DebugName or self.ClassName or self:GetLayerName()
end

function StateMachine:LogDebug(area, message)
    if not self:IsDebugEnabled() then
        return
    end

    print(string.format(
        "[LuaAnim][StateMachine][%s][%s][%s] %s",
        tostring(self:GetLayerName()),
        tostring(self:GetDebugName()),
        tostring(area or "Debug"),
        tostring(message or "")))
end

function StateMachine:LogDebugFlag(flag_name, area, message)
    if self:IsDebugFlagEnabled(flag_name) then
        self:LogDebug(area, message)
    end
end

function StateMachine:GetDebugUpdateInterval()
    return self:AsNumber(self.DebugUpdateInterval, 0.5)
end

function StateMachine:ShouldLogContext(facts)
    if not self:IsDebugFlagEnabled("DebugContext") then
        return false
    end

    local state_name = facts and facts.State or self.CurrentStateName or self:GetEntryState()
    if self.LastDebugContextState ~= state_name then
        self.LastDebugContextState = state_name
        self.DebugContextElapsed = 0
        return true
    end

    local interval = self:GetDebugUpdateInterval()
    if interval <= 0 then
        return true
    end

    self.DebugContextElapsed = (self.DebugContextElapsed or 0) + (self.DeltaSeconds or 0)
    if self.DebugContextElapsed >= interval then
        self.DebugContextElapsed = 0
        return true
    end

    return false
end

function StateMachine:LogContextSummary(facts, decision)
    if not self:ShouldLogContext(facts) then
        return
    end

    local decision_state = decision and decision.StateName or self.CurrentStateName or ""
    local animation_path = decision and decision.AnimationPath or nil
    local animation_name = decision and decision.AnimationName or nil
    self:LogDebug("Context", string.format(
        "state=%s decision=%s time=%.2f dt=%.3f speed=%.1f gait=%s desired=%s entry=%s locked=%s moveAngle=%.1f turnAngle=%.1f anim=%s path=%s blendX=%.1f",
        tostring(self.CurrentStateName or (facts and facts.State) or ""),
        tostring(decision_state),
        self:AsNumber(self.StateTime, 0),
        self:AsNumber(self.DeltaSeconds, 0),
        self:AsNumber(self.Speed, 0),
        value_to_debug_string(self.Gait),
        value_to_debug_string(self.DesiredGait),
        value_to_debug_string(self.GroundedEntryState),
        value_to_debug_string(self.bIsLockedOn),
        self:AsNumber(self.MoveDirectionAngle, 0),
        self:AsNumber(self.TurnAngle, 0),
        tostring(animation_name or ""),
        short_asset_path(animation_path),
        decision and self:AsNumber(decision.BlendInputX, 0) or self:AsNumber(self.CurrentBlendInputX, 0)))
end

function StateMachine:LogPoseSubmitted(pose_type, state_name, animation_name, animation_path, blend_input_x, blend_input_y, blend_input_z, blend_time, play_rate, loop, reset_time, start_position)
    if not self:IsDebugFlagEnabled("DebugPose") then
        return
    end

    local pose_key = table.concat({
        tostring(pose_type),
        tostring(state_name),
        tostring(animation_name),
        tostring(animation_path),
        tostring(reset_time),
    }, "|")

    if self.DebugPoseEveryFrame ~= true and reset_time ~= true and self.LastDebugPoseKey == pose_key then
        return
    end

    self.LastDebugPoseKey = pose_key
    local start_position_text = start_position ~= nil and string.format("%.2f", self:AsNumber(start_position, 0)) or "nil"
    self:LogDebug("Pose", string.format(
        "type=%s state=%s anim=%s path=%s blend=(%.1f, %.1f, %.1f) blendTime=%.2f rate=%.2f loop=%s reset=%s start=%s",
        tostring(pose_type),
        tostring(state_name),
        tostring(animation_name or ""),
        short_asset_path(animation_path),
        self:AsNumber(blend_input_x, 0),
        self:AsNumber(blend_input_y, 0),
        self:AsNumber(blend_input_z, 0),
        self:AsNumber(blend_time, 0),
        self:AsNumber(play_rate, 1),
        tostring(loop),
        tostring(reset_time),
        start_position_text))
end

function StateMachine:MakeSubmittedPoseDecision(state_name, reset_time)
    return {
        StateName = state_name,
        ResetTime = reset_time == true,
        bLuaAnimPoseSubmitted = true,
        bInternalLuaAnimDecision = true,
    }
end

function StateMachine:CallSuper(method_name, ...)
    local class = rawget(self, "Class") or getmetatable(self)
    local super = class and rawget(class, "Super") or nil

    while super ~= nil do
        local method = rawget(super, method_name)
        if type(method) == "function" then
            return method(self, ...)
        end
        super = rawget(super, "Super")
    end

    return nil
end

function StateMachine:super(method_name, ...)
    return self:CallSuper(method_name, ...)
end

function StateMachine:GetLayerName()
    return self.LayerName or "Default"
end

function StateMachine:GetContextObject()
    return rawget(self, "ContextObject")
end

function StateMachine:GetRuntimeContext(context, runtime_context)
    if type(runtime_context) == "table" then
        return runtime_context
    end

    if type(context) == "table" and resolve_context_object(context) == nil then
        return context
    end

    return nil
end

function StateMachine:GetUpdateContext(context, runtime_context)
    return self:GetRuntimeContext(context, runtime_context) or context
end

function StateMachine:CallContextFunction(function_name, ...)
    local context = self:GetContextObject()
    if context == nil or function_name == nil then
        return nil
    end

    local ok, result1, result2, result3, result4 = pcall(function(...)
        local method = context[function_name]
        if type(method) ~= "function" then
            return nil
        end

        return method(context, ...)
    end, ...)

    if ok then
        return result1, result2, result3, result4
    end

    return nil
end

function StateMachine:CallCpp(function_name, ...)
    return self:CallContextFunction(function_name, ...)
end

function StateMachine:GetCurrentCurveValue(curve_name, fallback)
    local value = self:CallCpp(
        "GetLuaAnimCurveValue",
        self:GetLayerName(),
        curve_name,
        fallback or 0)
    return self:AsNumber(value, fallback or 0)
end

function StateMachine:GetCurrentCurveIntValue(curve_name, fallback)
    local value = self:CallCpp(
        "GetLuaAnimCurveIntValue",
        self:GetLayerName(),
        curve_name,
        fallback or 0)
    return math.floor(self:AsNumber(value, fallback or 0) + 0.5)
end

function StateMachine:HasCurrentCurveFlag(curve_name, flag_mask)
    return self:CallCpp(
        "HasLuaAnimCurveFlag",
        self:GetLayerName(),
        curve_name,
        flag_mask or 0) == true
end

function StateMachine:GetCurrentAnimationNormalizedTime(fallback)
    local value = self:CallCpp(
        "GetLuaAnimNormalizedTime",
        self:GetLayerName(),
        fallback or 0)
    return self:Clamp(self:AsNumber(value, fallback or 0), 0, 1)
end

function StateMachine:GetStateList()
    local state_list = self.StateList
    if type(state_list) == "table" and #state_list > 0 then
        return state_list
    end

    local states = self.States or self.State
    if type(states) == "table" and #states > 0 then
        return states
    end

    return nil
end

function StateMachine:IsKnownState(state_name)
    local text = self:NameToString(state_name)
    if text == "" then
        return false
    end

    local state_list = self:GetStateList()
    if state_list ~= nil then
        for _, value in ipairs(state_list) do
            if text == value then
                return true
            end
        end
    end

    for _, value in pairs(self.States or self.State or {}) do
        if text == value then
            return true
        end
    end

    return false
end

function StateMachine:GetEntryState()
    if self.EntryState ~= nil then
        return self.EntryState
    end

    if self.InitialState ~= nil then
        return self.InitialState
    end

    local state_list = self:GetStateList()
    if state_list ~= nil and state_list[1] ~= nil then
        return state_list[1]
    end

    local states = self.States or self.State or {}
    return states.Idle or "Idle"
end

function StateMachine:GetInitialState()
    return self:GetEntryState()
end

function StateMachine:Clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

function StateMachine:AsNumber(value, fallback)
    local number = tonumber(value)
    if number == nil then
        return fallback or 0
    end

    return number
end

function StateMachine:NameToString(value)
    if value == nil then
        return ""
    end

    local text = tostring(value)
    if text == "None" then
        return ""
    end

    return text
end

function StateMachine:NameKey(value)
    local text = string.lower(self:NameToString(value))
    return string.gsub(text, "[^%w]", "")
end

function StateMachine:Contains(text, pattern)
    if text == nil or pattern == nil then
        return false
    end

    return string.find(string.lower(tostring(text)), string.lower(tostring(pattern)), 1, true) ~= nil
end

function StateMachine:ReadContextField(context, field_name)
    if context == nil or field_name == nil then
        return nil
    end

    if type(context) == "table" and context[field_name] ~= nil then
        return context[field_name]
    end

    local ok, value = pcall(function()
        return context:GetLuaAnimPropertyText(field_name, "")
    end)
    if ok and value ~= nil and value ~= "" then
        return value
    end

    ok, value = pcall(function()
        return context[field_name]
    end)
    if ok and value ~= nil then
        return value
    end

    return nil
end

function StateMachine:ReadContextNumber(context, field_name, fallback)
    if type(context) == "table" and context[field_name] ~= nil then
        return self:AsNumber(context[field_name], fallback)
    end

    local ok, value = pcall(function()
        return context:GetLuaAnimNumberProperty(field_name, fallback or 0)
    end)
    if ok then
        return self:AsNumber(value, fallback)
    end

    local direct_number = tonumber(self:ReadContextField(context, field_name))
    if direct_number ~= nil then
        return direct_number
    end

    return fallback or 0
end

function StateMachine:ReadContextBool(context, field_name, fallback)
    local direct_value = self:ReadContextField(context, field_name)
    if direct_value == true or direct_value == "true" or direct_value == 1 then
        return true
    end
    if direct_value == false or direct_value == "false" or direct_value == 0 then
        return false
    end

    local ok, value = pcall(function()
        return context:GetLuaAnimBoolProperty(field_name, fallback == true)
    end)
    if ok then
        return value == true
    end

    return fallback == true
end

function StateMachine:GetSnapshot(context, runtime_context)
    local update_context = self:GetUpdateContext(context, runtime_context)
    if update_context == nil then
        return nil
    end

    if type(update_context) == "table" and type(update_context.Snapshot) == "table" then
        return update_context.Snapshot
    end

    local layer_name = self:GetLayerName()
    local ok, snapshot = pcall(function()
        return update_context:GetLuaAnimLayerSnapshot(layer_name)
    end)
    if ok then
        return snapshot
    end

    return nil
end

function StateMachine:GetCurrentState(snapshot)
    if snapshot == nil then
        return ""
    end

    return self:NameToString(snapshot.CurrentStateName)
end

function StateMachine:GetStateTime(snapshot)
    if snapshot == nil then
        return 0
    end

    return self:AsNumber(snapshot.CurrentTime, 0)
end

function StateMachine:NormalizeState(state_name)
    local text = self:NameToString(state_name)
    if text == "" then
        return self:GetEntryState()
    end

    if self:IsKnownState(text) then
        return text
    end

    return self:GetEntryState()
end

function StateMachine:BuildFacts(_context, snapshot, delta_seconds)
    return {
        State = self:NormalizeState(self:GetCurrentState(snapshot)),
        StateTime = self:GetStateTime(snapshot),
        DeltaSeconds = self:AsNumber(delta_seconds, 0),
        HasPose = snapshot ~= nil and (snapshot.HasPose == true or snapshot.bHasPose == true),
    }
end

function StateMachine:ApplyRuntimeContext(context, runtime_context, snapshot, delta_seconds)
    local update_context = self:GetUpdateContext(context, runtime_context)
    local runtime_table = self:GetRuntimeContext(context, runtime_context)
    local context_object = resolve_context_object(context) or resolve_context_object(runtime_context)

    self.Context = context_object or update_context
    self.ContextObject = context_object
    self.RuntimeContext = runtime_table
    self.UpdateContext = update_context
    self.Snapshot = snapshot
    self.DeltaSeconds = self:AsNumber(delta_seconds, 0)

    if type(runtime_table) ~= "table" then
        return
    end

    for key, value in pairs(runtime_table) do
        if type(key) == "string" and not RuntimeFieldSkip[key] then
            self[key] = value
        end
    end

    if type(runtime_table.Snapshot) == "table" then
        self.Snapshot = runtime_table.Snapshot
    end
end

function StateMachine:ApplyFacts(facts)
    self.LastFacts = facts
    if type(facts) ~= "table" then
        return
    end

    if facts.State ~= nil then
        self.CurrentState = facts.State
        self.CurrentStateName = facts.State
    end

    for key, value in pairs(facts) do
        if key ~= "State" then
            self[key] = value
        end
    end
end

function StateMachine:GetAnimationSettings(state_name)
    return (self.AnimationSettings or {})[state_name] or {}
end

function StateMachine:GetAnimationPath(animation_key)
    if animation_key == nil then
        return nil
    end

    local assets = self.Assets or {}
    return assets[animation_key] or animation_key
end

function StateMachine:GetAnimationName(animation_ref)
    if type(animation_ref) ~= "string" then
        return nil
    end

    local assets = self.Assets or {}
    if assets[animation_ref] ~= nil then
        return animation_ref
    end

    for name, path in pairs(assets) do
        if path == animation_ref then
            return name
        end
    end

    if string.sub(animation_ref, 1, 1) ~= "/" then
        return animation_ref
    end

    return nil
end

function StateMachine:MakePoseOptionsWithAnimationName(options, animation_name)
    local pose_options = options or {}
    if animation_name == nil or animation_name == "" then
        return pose_options
    end

    if pose_options.AnimationName ~= nil or pose_options.AnimationAlias ~= nil then
        return pose_options
    end

    local copied_options = copy_table(pose_options)
    copied_options.AnimationName = animation_name
    return copied_options
end

function StateMachine:GetEvaluatingStateName()
    return self.EvaluatingStateName or self.CurrentState or self:GetEntryState()
end

function StateMachine:GetPoseBlendTime(state_name, options)
    local pose_options = options or {}
    if pose_options.BlendTime ~= nil then
        return pose_options.BlendTime
    end

    local settings = self:GetAnimationSettings(state_name)
    if settings.BlendTime ~= nil then
        return settings.BlendTime
    end

    return self.DefaultBlendTime or 0.15
end

function StateMachine:GetPoseLoop(state_name, options)
    local pose_options = options or {}
    if pose_options.Loop ~= nil then
        return pose_options.Loop ~= false
    end
    if pose_options.bLoop ~= nil then
        return pose_options.bLoop ~= false
    end

    local settings = self:GetAnimationSettings(state_name)
    return settings.Loop ~= false
end

function StateMachine:GetPoseResetTime(options)
    local pose_options = options or {}
    if pose_options.ResetTime ~= nil then
        return pose_options.ResetTime == true
    end
    if pose_options.bResetTime ~= nil then
        return pose_options.bResetTime == true
    end

    return self.EvaluatingResetTime == true
end

function StateMachine:GetPoseStartPosition(options)
    local pose_options = options or {}
    local start_position = pose_options.StartPosition
    if start_position == nil then
        start_position = pose_options.NormalizedStartPosition
    end

    if start_position == nil then
        return nil
    end

    return self:Clamp(self:AsNumber(start_position, 0), 0, 1)
end

function StateMachine:SubmitPoseByPath(animation_path, blend_input_x, blend_input_y, blend_input_z, options)
    local pose_options = options or {}
    local state_name = pose_options.StateName or self:GetEvaluatingStateName()
    local layer_name = pose_options.LayerName or self:GetLayerName()
    local play_rate = pose_options.PlayRate or 1.0
    local blend_time = self:GetPoseBlendTime(state_name, pose_options)
    local loop = self:GetPoseLoop(state_name, pose_options)
    local reset_time = self:GetPoseResetTime(pose_options)
    local start_position = self:GetPoseStartPosition(pose_options)
    local animation_name = pose_options.AnimationName or pose_options.AnimationAlias

    if animation_path == nil or animation_path == "" then
        self:LogDebugFlag("DebugPose", "Pose", string.format(
            "missing animation path state=%s layer=%s",
            tostring(state_name),
            tostring(layer_name)))
    end

    local ok = false
    if start_position ~= nil then
        ok = self:CallCpp(
            "SetLuaAnimPoseByPathWithNameAndStartPosition",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time,
            start_position)
    end

    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimPoseByPathWithName",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end
    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimPoseByPath",
            layer_name,
            state_name,
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end

    if ok == true then
        self.bLuaAnimPoseSubmitted = true
        self:LogPoseSubmitted("Pose", state_name, animation_name, animation_path, blend_input_x, blend_input_y, blend_input_z, blend_time, play_rate, loop, reset_time, start_position)
        return nil
    end

    self:LogPoseSubmitted("PoseDecision", state_name, animation_name, animation_path, blend_input_x, blend_input_y, blend_input_z, blend_time, play_rate, loop, reset_time, start_position)
    return {
        StateName = state_name,
        AnimationName = animation_name,
        AnimationPath = animation_path,
        BlendTime = blend_time,
        PlayRate = play_rate,
        Loop = loop,
        ResetTime = reset_time,
        StartPosition = start_position,
        BlendInputX = blend_input_x or 0,
        BlendInputY = blend_input_y or 0,
        BlendInputZ = blend_input_z or 0,
    }
end

function StateMachine:SubmitSequencePoseByPath(animation_path, options)
    local pose_options = options or {}
    local state_name = pose_options.StateName or self:GetEvaluatingStateName()
    local layer_name = pose_options.LayerName or self:GetLayerName()
    local play_rate = pose_options.PlayRate or 1.0
    local blend_time = self:GetPoseBlendTime(state_name, pose_options)
    local loop = self:GetPoseLoop(state_name, pose_options)
    local reset_time = self:GetPoseResetTime(pose_options)
    local start_position = self:GetPoseStartPosition(pose_options)
    local animation_name = pose_options.AnimationName or pose_options.AnimationAlias

    if animation_path == nil or animation_path == "" then
        self:LogDebugFlag("DebugPose", "Pose", string.format(
            "missing sequence path state=%s layer=%s",
            tostring(state_name),
            tostring(layer_name)))
    end

    local ok = false
    if start_position ~= nil then
        ok = self:CallCpp(
            "SetLuaAnimSequencePoseByPathWithNameAndStartPosition",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_time,
            play_rate,
            loop,
            reset_time,
            start_position)
    end

    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimSequencePoseByPathWithName",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end
    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimSequencePoseByPath",
            layer_name,
            state_name,
            animation_path,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end

    if ok == true then
        self.bLuaAnimPoseSubmitted = true
        self:LogPoseSubmitted("Sequence", state_name, animation_name, animation_path, 0, 0, 0, blend_time, play_rate, loop, reset_time, start_position)
        return nil
    end

    return self:SubmitPoseByPath(animation_path, 0, 0, 0, pose_options)
end

function StateMachine:SubmitBlendSpacePoseByPath(animation_path, blend_input_x, blend_input_y, blend_input_z, options)
    local pose_options = options or {}
    local state_name = pose_options.StateName or self:GetEvaluatingStateName()
    local layer_name = pose_options.LayerName or self:GetLayerName()
    local play_rate = pose_options.PlayRate or 1.0
    local blend_time = self:GetPoseBlendTime(state_name, pose_options)
    local loop = self:GetPoseLoop(state_name, pose_options)
    local reset_time = self:GetPoseResetTime(pose_options)
    local start_position = self:GetPoseStartPosition(pose_options)
    local animation_name = pose_options.AnimationName or pose_options.AnimationAlias

    if animation_path == nil or animation_path == "" then
        self:LogDebugFlag("DebugPose", "Pose", string.format(
            "missing blendspace path state=%s layer=%s",
            tostring(state_name),
            tostring(layer_name)))
    end

    local ok = false
    if start_position ~= nil then
        ok = self:CallCpp(
            "SetLuaAnimBlendSpacePoseByPathWithNameAndStartPosition",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time,
            start_position)
    end

    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimBlendSpacePoseByPathWithName",
            layer_name,
            state_name,
            animation_name or "",
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end
    if ok ~= true then
        ok = self:CallCpp(
            "SetLuaAnimBlendSpacePoseByPath",
            layer_name,
            state_name,
            animation_path,
            blend_input_x or 0,
            blend_input_y or 0,
            blend_input_z or 0,
            blend_time,
            play_rate,
            loop,
            reset_time)
    end

    if ok == true then
        self.bLuaAnimPoseSubmitted = true
        self:LogPoseSubmitted("BlendSpace", state_name, animation_name, animation_path, blend_input_x, blend_input_y, blend_input_z, blend_time, play_rate, loop, reset_time, start_position)
        return nil
    end

    return self:SubmitPoseByPath(animation_path, blend_input_x, blend_input_y, blend_input_z, pose_options)
end

function StateMachine:PlaySequence(animation_ref, options)
    local pose_options = self:MakePoseOptionsWithAnimationName(options, self:GetAnimationName(animation_ref))
    return self:SubmitSequencePoseByPath(self:GetAnimationPath(animation_ref), pose_options)
end

function StateMachine:SampleBlendSpace1D(animation_ref, input_x, options)
    local pose_options = self:MakePoseOptionsWithAnimationName(options, self:GetAnimationName(animation_ref))
    return self:SubmitBlendSpacePoseByPath(self:GetAnimationPath(animation_ref), input_x or 0, 0, 0, pose_options)
end

function StateMachine:MakeDecision(state_name, animation_key, _facts, options)
    local settings = self:GetAnimationSettings(state_name)
    local decision_options = options or {}
    local key = animation_key or state_name
    local animation_name = decision_options.AnimationName or decision_options.AnimationAlias or self:GetAnimationName(key) or key

    local blend_time = decision_options.BlendTime
    if blend_time == nil then
        blend_time = settings.BlendTime
    end
    if blend_time == nil then
        blend_time = self.DefaultBlendTime
    end

    local loop = settings.Loop ~= false
    if decision_options.Loop ~= nil then
        loop = decision_options.Loop ~= false
    end

    local start_position = self:GetPoseStartPosition(decision_options)
    return {
        StateName = state_name,
        AnimationName = animation_name,
        AnimationPath = decision_options.AnimationPath or self:GetAnimationPath(key),
        BlendTime = blend_time or 0.15,
        PlayRate = decision_options.PlayRate or 1.0,
        Loop = loop,
        ResetTime = decision_options.ResetTime == true or decision_options.bResetTime == true,
        StartPosition = start_position,
        BlendInputX = decision_options.BlendInputX or decision_options.BlendX or 0,
        BlendInputY = decision_options.BlendInputY or decision_options.BlendY or 0,
        BlendInputZ = decision_options.BlendInputZ or decision_options.BlendZ or 0,
    }
end

function StateMachine:GetAnimationKeyFromResult(state_name, animation_result)
    if type(animation_result) == "string" then
        return animation_result
    end

    if type(animation_result) ~= "table" then
        return state_name
    end

    return animation_result.Animation
        or animation_result.AnimationName
        or animation_result.Asset
        or animation_result.AssetName
        or animation_result.BlendSpace
        or animation_result[1]
        or state_name
end

function StateMachine:MakeDecisionFromAnimationResult(state_name, facts, animation_result)
    if animation_result == nil and self.bLuaAnimPoseSubmitted == true then
        return nil
    end

    if type(animation_result) == "table" and animation_result.StateName ~= nil then
        return animation_result
    end

    local animation_key = self:GetAnimationKeyFromResult(state_name, animation_result)
    local options = type(animation_result) == "table" and animation_result or nil
    return self:MakeDecision(state_name, animation_key, facts, options)
end

function StateMachine:UpdateAnimation(state_name, facts)
    local updater = self["UpdateAnimation_" .. state_name]
    if type(updater) ~= "function" then
        updater = self["Update" .. state_name .. "Animation"]
    end
    if type(updater) == "function" then
        self.EvaluatingStateName = state_name
        self.bLuaAnimPoseSubmitted = false
        local animation_result = updater(self, facts)
        self.EvaluatingStateName = nil
        return self:MakeDecisionFromAnimationResult(state_name, facts, animation_result)
    end

    return self:MakeDecision(state_name, state_name, facts)
end

function StateMachine:Keep(state_name, facts)
    self.EvaluatingResetTime = false
    local decision = self:UpdateAnimation(state_name, facts)
    self.EvaluatingResetTime = nil
    if decision == nil then
        if self.bLuaAnimPoseSubmitted == true then
            return self:MakeSubmittedPoseDecision(state_name, false)
        end

        return nil
    end

    decision.StateName = state_name
    decision.ResetTime = false
    return decision
end

function StateMachine:Enter(state_name, facts)
    local from_state = self.CurrentStateName or self.CurrentState or (facts and facts.State) or ""
    self:LogDebugFlag("DebugTransitions", "Enter", string.format(
        "%s -> %s reset=true",
        tostring(from_state),
        tostring(state_name)))

    self.EvaluatingResetTime = true
    local decision = self:UpdateAnimation(state_name, facts)
    self.EvaluatingResetTime = nil
    if decision == nil then
        if self.bLuaAnimPoseSubmitted == true then
            return self:MakeSubmittedPoseDecision(state_name, true)
        end

        return nil
    end

    decision.StateName = state_name
    decision.ResetTime = true
    return decision
end

function StateMachine:GetTransitionFunctionName(from_state, to_state)
    return "CanEnter_" .. from_state .. "_" .. to_state
end

function StateMachine:GetLegacyTransitionFunctionName(from_state, to_state)
    return "CanEnter" .. to_state .. "From" .. from_state
end

function StateMachine:CanEnterState(from_state, to_state, facts)
    local method = self[self:GetTransitionFunctionName(from_state, to_state)]
    if type(method) ~= "function" then
        method = self[self:GetLegacyTransitionFunctionName(from_state, to_state)]
    end
    if type(method) == "function" then
        local result = method(self, facts) == true
        if self:IsDebugFlagEnabled("DebugTransitionChecks") then
            self:LogDebug("TransitionCheck", string.format(
                "%s -> %s = %s",
                tostring(from_state),
                tostring(to_state),
                tostring(result)))
        end
        return result
    end

    return false
end

function StateMachine:TryEnter(from_state, to_state, facts)
    if self:CanEnterState(from_state, to_state, facts) then
        return self:Enter(to_state, facts)
    end

    return nil
end

function StateMachine:TryTransitions(from_state, facts, to_states)
    for _, to_state in ipairs(to_states or {}) do
        local decision = self:TryEnter(from_state, to_state, facts)
        if decision ~= nil then
            return decision
        end
    end

    return nil
end

function StateMachine:GetDeclaredTransitionEdges()
    local edges = {}
    local seen = {}

    for _, edge in ipairs(rawget(self.Class or {}, "__TransitionOrder") or {}) do
        if type(edge.Name) == "string" and type(self[edge.Name]) == "function" then
            table.insert(edges, edge)
            seen[edge.Name] = true
        end
    end

    for key, value in pairs(self.Class or getmetatable(self) or self) do
        if type(key) == "string" and type(value) == "function" and not seen[key] then
            local from_state, to_state = string.match(key, "^CanEnter_([^_]+)_(.+)$")
            if from_state ~= nil and to_state ~= nil then
                table.insert(edges, {
                    From = from_state,
                    To = to_state,
                    Name = key,
                })
                seen[key] = true
            end
        end
    end

    return edges
end

function StateMachine:GetAutoTransitionEdges(from_state)
    local edges = {}

    for _, edge in ipairs(self:GetDeclaredTransitionEdges()) do
        if edge.From == "Any" and edge.To ~= from_state and self:IsKnownState(edge.To) then
            table.insert(edges, edge)
        end
    end

    for _, edge in ipairs(self:GetDeclaredTransitionEdges()) do
        if edge.From == from_state and edge.To ~= from_state and self:IsKnownState(edge.To) then
            table.insert(edges, edge)
        end
    end

    return edges
end

function StateMachine:TryAutoTransitions(from_state, facts)
    for _, edge in ipairs(self:GetAutoTransitionEdges(from_state)) do
        if self:CanEnterState(edge.From, edge.To, facts) then
            self:LogDebugFlag("DebugTransitions", "Transition", string.format(
                "current=%s rule=%s from=%s to=%s",
                tostring(from_state),
                tostring(edge.Name),
                tostring(edge.From),
                tostring(edge.To)))
            return self:Enter(edge.To, facts)
        end
    end

    return nil
end

function StateMachine:SelectEntryState(facts)
    self:LogDebugFlag("DebugTransitions", "Entry", string.format(
        "entry=%s hasPose=%s",
        tostring(self:GetEntryState()),
        tostring(facts and facts.HasPose)))
    return self:Enter(self:GetEntryState(), facts)
end

function StateMachine:UpdateState(facts)
    local state_name = self:NormalizeState(facts and facts.State or nil)
    local transition_decision = self:TryAutoTransitions(state_name, facts)
    if transition_decision ~= nil then
        return transition_decision
    end

    local updater = self["UpdateState_" .. state_name]
    if type(updater) == "function" then
        return updater(self, facts)
    end

    return self:Keep(state_name, facts)
end

function StateMachine:Update(context, delta_seconds, runtime_context)
    local update_context = self:GetUpdateContext(context, runtime_context)
    local snapshot = self:GetSnapshot(context, runtime_context)
    self:ApplyRuntimeContext(context, runtime_context, snapshot, delta_seconds)

    local facts = self:BuildFacts(update_context, snapshot, delta_seconds)
    self:ApplyFacts(facts)

    local decision = nil
    if not facts.HasPose then
        decision = self:SelectEntryState(facts)
    else
        decision = self:UpdateState(facts)
    end
    if decision == nil and self.bLuaAnimPoseSubmitted ~= true then
        decision = self:SelectEntryState(facts)
    end

    self.LastDecision = decision
    self:LogContextSummary(facts, decision)
    if type(decision) == "table" and decision.bInternalLuaAnimDecision == true then
        return nil
    end

    return decision
end

return StateMachine
