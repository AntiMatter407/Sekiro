-- Lua 类型：纯 Lua 配置模块。本文件不绑定 UObject，集中描述攻击、防御、弹反与玩家架势数据。
-- 碰撞和伤害来源只提交裁决结果；架势数值、恢复与打崩规则统一由战斗组件 Lua 编排。

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local Attack = AnimAssets.Attack
local Deflect = AnimAssets.Deflect
local DeflectFailed = AnimAssets.DeflectFailed
local AttackDeflected = AnimAssets.AttackDeflected

---@class SKCombatActionConfig
---@field AnimationPath string 动画序列对象路径。
---@field ActionId string|nil 动作稳定 ID；按刀侧索引的重攻击配置需要显式填写。
---@field NextAction string|nil 普通连段窗口内再次点击时进入的动作。
---@field StartSide string|nil 动作提交时的挥刀侧；缺失时沿用当前锁存侧。
---@field bAllowHeavy boolean|nil 当前动作是否允许后续输入升级为重攻击。

---@class SKDeflectSideConfig
---@field AnimationPath string 当前刀侧对应的弹反成功动画路径。
---@field EndSide string 弹反动作结束后锁存的新刀侧。

---@class SKCombatConfigModule
---@field SlotName string 全身战斗动作 Slot 名称。
---@field BlendInTime number 动作淡入时间，单位为秒。
---@field BlendOutTime number 动作淡出时间，单位为秒。
---@field HeavyHoldThreshold number 攻击键达到重攻击的持续时间，单位为秒。
---@field DefaultSide string 没有明确刀侧或当前攻击动画结束时使用的默认刀侧。
---@field DefenseSide string Guard 和当前阶段 Deflect 使用的锁存刀侧。
---@field DeflectSideResetDelay number 弹反成功后保留新刀侧的时间，单位为秒。
---@field DefaultIncomingWindow number 调试来袭窗口默认时长，单位为秒。
---@field Attacks table<string, SKCombatActionConfig> 稳定攻击动作 ID 到动作配置的映射。
---@field AirAttacks table<string, SKCombatActionConfig> 空中轻攻击动作 ID 到动作配置的映射。
---@field LandAttacks table<string, SKCombatActionConfig> 落地轻攻击动作 ID 到动作配置的映射。
---@field AirToLand table<string, string> 活动空中攻击到配对落地攻击动作 ID 的映射。
---@field HeavyBySide table<string, SKCombatActionConfig> 当前侧别到蓄力突刺动作的映射。
---@field GuardAttackStartupBySide table<string, SKCombatActionConfig> 防御攻击共用起手；首版复用同侧重攻击并在阈值处原地提交。
---@field DeflectBySide table<string, SKDeflectSideConfig> Raise 起始刀侧到成功弹反动作及结束刀侧的映射。
---@field DeflectFailedByType table<string, string[]> 模拟攻击类型到弹反失败动画序列链的映射。
---@field AttackDeflectedBySide table<string, SKDeflectSideConfig> 当前提交攻击刀侧到被弹开动作及保持刀侧的映射。
---@field Guard table<string, string> 地面与空中防御举刀、收刀动画路径。
---@field GuardImpactAnimation string 普通防御命中时播放的震刀动作路径。
---@field Posture SKPostureConfig 玩家架势增加、恢复与打崩状态配置。

---@class SKPostureGainConfig
---@field DeflectSuccess number 弹反成功的基础架势增加值。
---@field Guarded number 普通防御的基础架势增加值。
---@field DeflectFailed number 弹反失败的基础架势增加值。
---@field AttackSuccess number 攻击成功命中时攻击者的基础架势增加值。
---@field AttackGuarded number 攻击被普通防御时攻击者的基础架势增加值。
---@field AttackDeflected number 攻击被成功弹反时攻击者的基础架势增加值。

---@class SKPostureRecoveryConfig
---@field Delay number 满足恢复条件后开始恢复前的延迟，单位秒。
---@field RampDuration number 从最低恢复速率提升到最高速率所需时间，单位秒。
---@field RateMin number 刚开始恢复时每秒减少的架势值。
---@field RateMax number 连续脱离战斗后每秒最多减少的架势值。

---@class SKPostureBreakConfig
---@field AnimationPath string 架势条满时播放的全身打崩动画路径。
---@field MinimumLockDuration number 打崩后最短不可操作时间，单位秒。
---@field BlendInTime number 打崩动画淡入时间，单位秒。
---@field BlendOutTime number 打崩动画淡出时间，单位秒。

---@class SKPostureConfig
---@field MaxValue number 玩家架势最大值。
---@field SuccessCapNormalized number 弹反成功允许达到的最高归一化架势。
---@field AttackCapNormalized number 攻击行为允许达到的最高归一化架势。
---@field MinGainScale number 当前架势接近满值时仍保留的最低增加倍率。
---@field GainFalloffExponent number 当前架势对增加倍率的衰减指数。
---@field Gain SKPostureGainConfig 三种防御结果的基础架势增加值。
---@field AttackStrength table<string, number> 来袭类型对应的架势强度倍率。
---@field Recovery SKPostureRecoveryConfig 非战斗状态下的渐进恢复配置。
---@field Break SKPostureBreakConfig 架势打崩动画与输入锁配置。

---@type SKCombatConfigModule
local CombatConfig = {
    SlotName = "CombatFullBodySlot",
    BlendInTime = 0.06,
    BlendOutTime = 0.10,
    HeavyHoldThreshold = 0.30,
    DefaultSide = "Right",
    DefenseSide = "Left",
    DeflectSideResetDelay = 0.75,
    DefaultIncomingWindow = 0.25,
    Posture = {
        MaxValue = 100.0,
        SuccessCapNormalized = 0.98,
        AttackCapNormalized = 0.98,
        MinGainScale = 0.35,
        GainFalloffExponent = 1.25,
        Gain = {
            DeflectSuccess = 6.0,
            Guarded = 14.0,
            DeflectFailed = 24.0,
            AttackSuccess = 4.0,
            AttackGuarded = 10.0,
            AttackDeflected = 18.0,
        },
        AttackStrength = {
            Light = 1.0,
            Heavy = 1.35,
            Thrust = 1.5,
            Special = 1.75,
        },
        Recovery = {
            Delay = 0.75,
            RampDuration = 4.0,
            RateMin = 3.0,
            RateMax = 18.0,
        },
        Break = {
            AnimationPath = AnimAssets.PostureBreak.Default,
            MinimumLockDuration = 1.5,
            BlendInTime = 0.04,
            BlendOutTime = 0.12,
        },
    },
    Guard = {
        Raise = AnimAssets.Guard.Raise,
        Lower = AnimAssets.Guard.Lower,
        AirRaise = AnimAssets.Guard.Air_Raise,
        AirLower = AnimAssets.Guard.Air_Lower,
    },
    GuardImpactAnimation = AnimAssets.Guard.Shake,
    Attacks = {
        Right = {
            AnimationPath = Attack.Right,
            NextAction = "Left",
            StartSide = "Right",
            bAllowHeavy = true,
        },
        Left = {
            AnimationPath = Attack.Left,
            NextAction = "Combo_01",
            StartSide = "Left",
            bAllowHeavy = true,
        },
        Combo_01 = { AnimationPath = Attack.Combo_01, NextAction = "Combo_02", bAllowHeavy = false },
        Combo_02 = { AnimationPath = Attack.Combo_02, NextAction = "Combo_03", bAllowHeavy = false },
        Combo_03 = { AnimationPath = Attack.Combo_03, NextAction = nil, bAllowHeavy = false },
    },
    AirAttacks = {
        Air_Combo_01 = {
            AnimationPath = Attack.Air_Combo_01,
            NextAction = "Air_Combo_02",
            bAllowHeavy = false,
        },
        Air_Combo_02 = {
            AnimationPath = Attack.Air_Combo_02,
            NextAction = "Air_Combo_03",
            bAllowHeavy = false,
        },
        Air_Combo_03 = {
            AnimationPath = Attack.Air_Combo_03,
            NextAction = nil,
            bAllowHeavy = false,
        },
    },
    LandAttacks = {
        Land_Combo_01 = {
            AnimationPath = Attack.Land_Combo_01,
            NextAction = "Land_Combo_02",
            bAllowHeavy = false,
        },
        Land_Combo_02 = {
            AnimationPath = Attack.Land_Combo_02,
            NextAction = "Land_Combo_03",
            bAllowHeavy = false,
        },
        Land_Combo_03 = {
            AnimationPath = Attack.Land_Combo_03,
            NextAction = nil,
            bAllowHeavy = false,
        },
    },
    AirToLand = {
        Air_Combo_01 = "Land_Combo_01",
        Air_Combo_02 = "Land_Combo_02",
        Air_Combo_03 = "Land_Combo_03",
    },
    HeavyBySide = {
        Right = {
            ActionId = "Charged_Thrust_Right",
            AnimationPath = Attack.Charged_Thrust_Right,
            StartSide = "Right",
            bAllowHeavy = true,
        },
        Left = {
            ActionId = "Charged_Thrust_Left",
            AnimationPath = Attack.Charged_Thrust_Left,
            StartSide = "Left",
            bAllowHeavy = true,
        },
    },
    -- 防御左侧起手立即播放重攻击前摇；短按会在阈值前切入 Left，长按则沿当前 Montage 继续重攻击。
    GuardAttackStartupBySide = {
        Left = {
            ActionId = "Guard_Attack_Startup_Left",
            AnimationPath = Attack.Charged_Thrust_Left,
            StartSide = "Left",
            bAllowHeavy = true,
        },
    },
    -- 成功弹反只在 Guard Raise 阶段成立：130101 负责右到左，130102 负责左到右。
    DeflectBySide = {
        Right = {
            AnimationPath = Deflect.Type_01_Stage_02,
            EndSide = "Left",
        },
        Left = {
            AnimationPath = Deflect.Type_01_Stage_03,
            EndSide = "Right",
        },
    },
    DeflectFailedByType = {
        Light = {
            DeflectFailed.Type_01_Stage_01,
            DeflectFailed.Type_01_Stage_02,
            DeflectFailed.Type_01_Stage_03,
        },
        Heavy = {
            DeflectFailed.Type_02_Stage_01,
            DeflectFailed.Type_02_Stage_02,
            DeflectFailed.Type_02_Stage_03,
        },
        Thrust = {
            DeflectFailed.Type_03_Stage_01,
            DeflectFailed.Type_03_Stage_02,
            DeflectFailed.Type_03_Stage_03,
        },
        Special = { DeflectFailed.Type_04_Stage_01 },
    },
    -- 攻击被成功弹开后保持原刀侧：150000 对应右侧，150001 对应左侧。
    AttackDeflectedBySide = {
        Right = {
            AnimationPath = AttackDeflected.Right,
            EndSide = "Right",
        },
        Left = {
            AnimationPath = AttackDeflected.Left,
            EndSide = "Left",
        },
    },
}

return CombatConfig
