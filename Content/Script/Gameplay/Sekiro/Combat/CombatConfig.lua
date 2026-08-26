-- Lua 类型：纯 Lua 配置模块。本文件不绑定 UObject，只描述攻击、防御与弹反动作。
-- 躯干数值全部由 GAS 保存，增长、恢复和崩溃流程由 Survival 编排；本模块不保留躯干参数副本。

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
