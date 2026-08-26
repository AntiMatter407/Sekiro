-- Lua 类型：纯 Lua 配置模块。主角的生命和战斗属性统一在本文件配置。
-- 这些是当前项目接入基线，不代表原版只狼数值；仅在每个角色 ASC 首次初始化时读取。
-- 所有字段平铺在 Character 表内，统一初始化 USKCharacterAttributeSet；运行时修改应通过 GAS 效果。

---@type SKCharacterAttributeConfig
local Character = {
    MaxHealth = 100.0, -- 生命上限。
    InitialHealth = 100.0, -- 初始生命绝对值；重复 Possess 不会再次补满。
    AttackPower = 100.0, -- 初始攻击力，统一命中协议尚未接入。
    Armor = 0.0, -- 护甲减伤曲线的输入，不是护盾值。
    MaxPosture = 100.0, -- 躯干积累上限。
    InitialPosture = 0.0, -- 初始躯干积累。
    PostureRecoveryRate = 18.0, -- 基础恢复点数/秒，乘以下方渐进倍率。
    PostureRecoveryDelay = 0.75, -- 开始自然恢复前的等待秒数。
    PostureRecoveryRampDuration = 4.0, -- 恢复倍率渐进时长，零表示立即达到最高倍率。
    PostureRecoveryMinRateScale = 1.0 / 6.0, -- 最低恢复倍率。
    PostureRecoveryMaxRateScale = 1.0, -- 最高恢复倍率。
    PostureDeflectSuccessCapRatio = 0.98, -- 弹反成功的非崩溃封顶比例。
    PostureAttackCapRatio = 0.98, -- 攻击方反馈的非崩溃封顶比例。
    PostureMinGainScale = 0.35, -- 接近满躯干时保留的最低增长倍率。
    PostureGainFalloffExponent = 1.25, -- 增长衰减指数，必须为正。
    PostureGainDeflectSuccess = 6.0, -- 弹反成功基础增长。
    PostureGainGuarded = 14.0, -- 普通格挡基础增长。
    PostureGainDeflectFailed = 24.0, -- 弹反失败基础增长。
    PostureGainAttackSuccess = 4.0, -- 攻击命中时攻击方基础增长。
    PostureGainAttackGuarded = 10.0, -- 攻击被格挡时攻击方基础增长。
    PostureGainAttackDeflected = 18.0, -- 攻击被弹反时攻击方基础增长。
    PostureStrengthLight = 1.0, -- 轻攻击躯干强度倍率。
    PostureStrengthHeavy = 1.35, -- 重攻击躯干强度倍率。
    PostureStrengthThrust = 1.5, -- 突刺躯干强度倍率。
    PostureStrengthSpecial = 1.75, -- 特殊攻击躯干强度倍率。
    PostureBreakMinimumDuration = 1.5, -- 躯干崩溃最短秒数。
    PostureBreakBlendInTime = 0.04, -- 崩溃动画混入秒数。
    PostureBreakBlendOutTime = 0.12, -- 崩溃动画混出秒数。
    PostureRecoveryTargetRatio = 0.0, -- 崩溃结束时目标躯干比例，必须小于一。
    RevivePostureRatio = 0.0, -- 回生后的目标躯干比例，必须小于一。
}

return Character
