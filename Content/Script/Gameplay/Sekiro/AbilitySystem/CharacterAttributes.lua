-- Lua 类型：纯 Lua 数据/工具模块。选择角色属性文件，校验完整表结构并复制成 GAS 初始化参数。
-- 本模块只保存字段契约和模块选择规则，不保存任何角色数值；不会修改 require 缓存中的配置表。

---@class SKCharacterAttributeConfig
---@field MaxHealth number 初始生命上限，必须为正有限值。
---@field InitialHealth number 初始生命绝对值，范围为零到 MaxHealth。
---@field AttackPower number 初始攻击力，非负有限值。
---@field Armor number 初始护甲，非负有限值，不表示可消耗护盾。
---@field MaxPosture number 躯干积累上限，必须为正有限值。
---@field InitialPosture number 初始躯干积累，范围为零到 MaxPosture。
---@field PostureRecoveryRate number 基础躯干恢复速度，单位为点/秒。
---@field PostureRecoveryDelay number 连续满足恢复条件后的等待秒数。
---@field PostureRecoveryRampDuration number 从最低恢复倍率提高到最高所需秒数，零表示立即使用最高倍率。
---@field PostureRecoveryMinRateScale number 自然恢复的起始速率倍率，非负且不大于最高倍率。
---@field PostureRecoveryMaxRateScale number 完成渐进恢复后的非负速率倍率。
---@field PostureDeflectSuccessCapRatio number 弹反成功的非崩溃积累上限比例，范围 [0,1)。
---@field PostureAttackCapRatio number 攻击方反馈的非崩溃积累上限比例，范围 [0,1)。
---@field PostureMinGainScale number 躯干接近满值时保留的最低增长倍率，范围 [0,1]。
---@field PostureGainFalloffExponent number 躯干增长随当前积累衰减的正指数。
---@field PostureGainDeflectSuccess number 弹反成功的基础躯干增长。
---@field PostureGainGuarded number 普通格挡的基础躯干增长。
---@field PostureGainDeflectFailed number 弹反失败的基础躯干增长。
---@field PostureGainAttackSuccess number 攻击命中的攻击方基础躯干增长。
---@field PostureGainAttackGuarded number 攻击被格挡的攻击方基础躯干增长。
---@field PostureGainAttackDeflected number 攻击被弹反的攻击方基础躯干增长。
---@field PostureStrengthLight number 轻攻击的躯干强度倍率。
---@field PostureStrengthHeavy number 重攻击的躯干强度倍率。
---@field PostureStrengthThrust number 突刺的躯干强度倍率。
---@field PostureStrengthSpecial number 特殊攻击的躯干强度倍率。
---@field PostureBreakMinimumDuration number 躯干崩溃的最短不可操作秒数。
---@field PostureBreakBlendInTime number 崩溃演出进入混合秒数。
---@field PostureBreakBlendOutTime number 崩溃演出退出混合秒数。
---@field PostureRecoveryTargetRatio number 崩溃恢复时的目标躯干积累比例，范围 [0,1)。
---@field RevivePostureRatio number 回生时的目标躯干积累比例，范围 [0,1)。

local CharacterAttributes = {}

-- 只登记字段名称。即使某项允许零，也必须在角色文件中显式填写，防止漏配被结构体零值掩盖。
---@type string[]
local AttributeFields = {
    "MaxHealth",
    "InitialHealth",
    "AttackPower",
    "Armor",
    "MaxPosture",
    "InitialPosture",
    "PostureRecoveryRate",
    "PostureRecoveryDelay",
    "PostureRecoveryRampDuration",
    "PostureRecoveryMinRateScale",
    "PostureRecoveryMaxRateScale",
    "PostureDeflectSuccessCapRatio",
    "PostureAttackCapRatio",
    "PostureMinGainScale",
    "PostureGainFalloffExponent",
    "PostureGainDeflectSuccess",
    "PostureGainGuarded",
    "PostureGainDeflectFailed",
    "PostureGainAttackSuccess",
    "PostureGainAttackGuarded",
    "PostureGainAttackDeflected",
    "PostureStrengthLight",
    "PostureStrengthHeavy",
    "PostureStrengthThrust",
    "PostureStrengthSpecial",
    "PostureBreakMinimumDuration",
    "PostureBreakBlendInTime",
    "PostureBreakBlendOutTime",
    "PostureRecoveryTargetRatio",
    "RevivePostureRatio",
}

---检查统一角色属性表是否完整且有限，拒绝旧分组、未知拼写及元表隐式默认值。
---@param config SKCharacterAttributeConfig|nil 角色文件直接返回的平铺属性表。
---@return string|nil error_message 非法配置的具体位置；结构合法返回 nil，数值范围稍后由 GAS 校验。
local function validate_config(config)
    if type(config) ~= "table" or getmetatable(config) ~= nil then
        return "模块必须返回平铺的 Character 属性表"
    end
    local allowed = {}
    for _, field_name in ipairs(AttributeFields) do
        allowed[field_name] = true
        local value = rawget(config, field_name)
        if type(value) ~= "number" or value ~= value or value == math.huge or value == -math.huge then
            return "Character." .. field_name .. " 缺失或不是有限数值"
        end
    end
    for field_name in pairs(config) do
        if allowed[field_name] ~= true then
            return "Character." .. tostring(field_name) .. " 不是受支持的属性字段"
        end
    end
    return nil
end

---读取角色指定的属性文件；未指定时按原生角色类型选主角/AI 文件，不依赖 Possess 时机或控制器状态。
---@param character ASKCharacter 已完成构造的真实角色，必须在游戏线程初始化阶段调用。
---@return FSKAttributeInitialization|nil values 本次独立的完整初始化结构；文件或字段错误返回 nil。
---@return string module_name 实际选择的模块名，显式模块失败时不改用其他文件。
---@return string error_message 失败原因；成功为空字符串，数值范围仍由 ASC 统一校验。
function CharacterAttributes.Load(character)
    local module_name = character:GetAttributeConfigModule()
    if module_name == "" then
        if character:IsA(UE.ASKAICharacter.StaticClass()) then
            module_name = "Gameplay.Sekiro.AI.Attributes"
        else
            module_name = "Gameplay.Sekiro.Character.Attributes"
        end
    end
    local loaded, config = pcall(require, module_name)
    if loaded ~= true then
        return nil, module_name, tostring(config)
    end
    local error_message = validate_config(config)
    if error_message ~= nil then
        return nil, module_name, error_message
    end

    -- 每个角色分配自己的初始化结构；配置表只读，不能把一名角色的实时生命写回共享模块缓存。
    local values = UE.FSKAttributeInitialization()
    for _, field_name in ipairs(AttributeFields) do
        values[field_name] = config[field_name]
    end
    return values, module_name, ""
end

return CharacterAttributes
