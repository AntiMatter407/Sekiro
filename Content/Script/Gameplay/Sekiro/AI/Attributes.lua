-- Lua 类型：纯 Lua 配置模块。通用 AI 角色独立的生命和战斗属性。
-- 当前保留迁移前的接入基线，不随主角配置变更；不同敌人可复制本文件并在 AttributeConfigModule 指定自己的模块。
-- 不引用主角配置或共享默认数值表；此表只用于初始化，不承载任何 AI 实例的运行时生命或躯干。

---@type SKCharacterAttributeConfig
local Character = {
    MaxHealth = 100.0, -- AI 生命上限。
    InitialHealth = 100.0, -- AI 初始生命绝对值。
    AttackPower = 100.0, -- AI 基础攻击力。
    Armor = 0.0, -- AI 初始护甲。
    MaxPosture = 100.0, -- AI 躯干积累上限。
    InitialPosture = 0.0, -- AI 初始躯干积累。
    PostureRecoveryRate = 18.0, -- 基础躯干恢复点数/秒。
    PostureRecoveryDelay = 0.75, -- 满足恢复条件后的等待秒数。
    PostureRecoveryRampDuration = 4.0, -- 恢复倍率渐进秒数，零表示直接使用最高倍率。
    PostureRecoveryMinRateScale = 1.0 / 6.0, -- 起始恢复倍率。
    PostureRecoveryMaxRateScale = 1.0, -- 最高恢复倍率。
    PostureDeflectSuccessCapRatio = 0.98, -- 弹反成功非崩溃封顶比例。
    PostureAttackCapRatio = 0.98, -- 攻击方反馈非崩溃封顶比例。
    PostureMinGainScale = 0.35, -- 满躯干附近的最低增长倍率。
    PostureGainFalloffExponent = 1.25, -- 躯干增长衰减正指数。
    PostureGainDeflectSuccess = 6.0, -- 弹反成功基础增长。
    PostureGainGuarded = 14.0, -- 普通格挡基础增长。
    PostureGainDeflectFailed = 24.0, -- 弹反失败基础增长。
    PostureGainAttackSuccess = 4.0, -- 攻击命中的攻击方基础增长。
    PostureGainAttackGuarded = 10.0, -- 攻击被格挡的攻击方基础增长。
    PostureGainAttackDeflected = 18.0, -- 攻击被弹反的攻击方基础增长。
    PostureStrengthLight = 1.0, -- 轻攻击强度倍率。
    PostureStrengthHeavy = 1.35, -- 重攻击强度倍率。
    PostureStrengthThrust = 1.5, -- 突刺强度倍率。
    PostureStrengthSpecial = 1.75, -- 特殊攻击强度倍率。
    PostureBreakMinimumDuration = 1.5, -- 崩溃最短持续秒数。
    PostureBreakBlendInTime = 0.04, -- 崩溃演出混入秒数。
    PostureBreakBlendOutTime = 0.12, -- 崩溃演出混出秒数。
    PostureRecoveryTargetRatio = 0.0, -- 崩溃恢复后的目标躯干比例，范围 [0,1)。
    RevivePostureRatio = 0.0, -- 显式回生后的目标躯干比例，范围 [0,1)。
}

return Character
