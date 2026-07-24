-- Lua 类型：纯 Lua 配置模块。本文件不绑定 UObject，集中描述首版攻击、防御与弹反动作数据。
-- 当前不包含碰撞、伤害、生命或躯干值；受击条件由测试入口写入的模拟来袭窗口提供。

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local Attack = AnimAssets.Attack
local Deflect = AnimAssets.Deflect

---@class SKCombatActionConfig
---@field AnimationPath string 动画序列对象路径。
---@field ActionId string|nil 动作稳定 ID；按刀侧索引的重攻击配置需要显式填写。
---@field NextAction string|nil 普通连段窗口内再次点击时进入的动作。
---@field StartSide string|nil 动作提交时的挥刀侧；缺失时沿用当前锁存侧。
---@field bAllowHeavy boolean|nil 当前动作是否允许后续输入升级为重攻击。

---@class SKCombatConfigModule
---@field SlotName string 全身战斗动作 Slot 名称。
---@field BlendInTime number 动作淡入时间，单位为秒。
---@field BlendOutTime number 动作淡出时间，单位为秒。
---@field HeavyHoldThreshold number 攻击键达到重攻击的持续时间，单位为秒。
---@field DefaultSide string 没有明确刀侧或当前攻击动画结束时使用的默认刀侧。
---@field DefenseSide string Guard 和当前阶段 Deflect 使用的锁存刀侧。
---@field DeflectChainResetTime number 同类型弹反段数重置间隔，单位为秒。
---@field DefaultIncomingWindow number 调试来袭窗口默认时长，单位为秒。
---@field Attacks table<string, SKCombatActionConfig> 稳定攻击动作 ID 到动作配置的映射。
---@field AirAttacks table<string, SKCombatActionConfig> 空中轻攻击动作 ID 到动作配置的映射。
---@field LandAttacks table<string, SKCombatActionConfig> 落地轻攻击动作 ID 到动作配置的映射。
---@field AirToLand table<string, string> 活动空中攻击到配对落地攻击动作 ID 的映射。
---@field HeavyBySide table<string, SKCombatActionConfig> 当前侧别到蓄力突刺动作的映射。
---@field GuardAttackStartupBySide table<string, SKCombatActionConfig> 防御攻击共用起手；首版复用同侧重攻击并在阈值处原地提交。
---@field DeflectByType table<string, string[]> 模拟攻击类型到弹反动画序列链的映射。
---@field Guard table<string, string> 地面与空中防御举刀、收刀动画路径。

---@type SKCombatConfigModule
local CombatConfig = {
    SlotName = "CombatFullBodySlot",
    BlendInTime = 0.06,
    BlendOutTime = 0.10,
    HeavyHoldThreshold = 0.30,
    DefaultSide = "Right",
    DefenseSide = "Left",
    DeflectChainResetTime = 0.75,
    DefaultIncomingWindow = 0.25,
    Guard = {
        Raise = AnimAssets.Guard.Raise,
        Lower = AnimAssets.Guard.Lower,
        AirRaise = AnimAssets.Guard.Air_Raise,
        AirLower = AnimAssets.Guard.Air_Lower,
    },
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
    -- 语义映射暂按素材编号接入，后续通过动作预览确认语义后只需调整本表。
    DeflectByType = {
        Light = { Deflect.Type_01_Stage_01, Deflect.Type_01_Stage_02, Deflect.Type_01_Stage_03 },
        Heavy = { Deflect.Type_02_Stage_01, Deflect.Type_02_Stage_02, Deflect.Type_02_Stage_03 },
        Thrust = { Deflect.Type_03_Stage_01, Deflect.Type_03_Stage_02, Deflect.Type_03_Stage_03 },
        Special = { Deflect.Type_04_Stage_01 },
    },
}

return CombatConfig
