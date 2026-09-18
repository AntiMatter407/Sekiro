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
---@field StartSide userdata|number|nil ESKAttackSide 原生枚举；缺失时沿用当前锁存侧。
---@field bAllowHeavy boolean|nil 当前动作是否允许后续输入升级为重攻击。

---@class SKDeflectSideConfig
---@field ActionId string 反应动作的稳定字符串 ID。
---@field AnimationPath string 当前刀侧对应的弹反成功动画路径。
---@field EndSide userdata|number ESKAttackSide 原生枚举；表示动作结束后锁存的新刀侧。

---@class SKCombatConfigModule
---@field SlotName string 全身战斗动作 Slot 名称。
---@field BlendInTime number 动作淡入时间，单位为秒。
---@field BlendOutTime number 动作淡出时间，单位为秒。
---@field HeavyHoldThreshold number 攻击键达到重攻击的持续时间，单位为秒。
---@field DefaultSide userdata|number ESKAttackSide 原生枚举；没有明确刀侧或动作结束时使用。
---@field DefenseSide userdata|number ESKAttackSide 原生枚举；Guard 和当前阶段 Deflect 使用。
---@field DeflectSideResetDelay number 弹反成功后保留新刀侧的时间，单位为秒。
---@field DefaultIncomingWindow number 调试来袭窗口默认时长，单位为秒。
---@field Attacks table<string, SKCombatActionConfig> 稳定攻击动作 ID 到动作配置的映射。
---@field AirAttacks table<string, SKCombatActionConfig> 空中轻攻击动作 ID 到动作配置的映射。
---@field LandAttacks table<string, SKCombatActionConfig> 落地轻攻击动作 ID 到动作配置的映射。
---@field AirToLand table<string, string> 活动空中攻击到配对落地攻击动作 ID 的映射。
---@field LightActionIdBySide table<userdata|number, string> 原生刀侧枚举到基础轻攻击动作 ID 的映射。
---@field HeavyBySide table<userdata|number, SKCombatActionConfig> 原生刀侧枚举到蓄力突刺动作的映射。
---@field GuardAttackStartupBySide table<userdata|number, SKCombatActionConfig> 原生刀侧枚举到防御攻击共用起手的映射。
---@field DeflectBySide table<userdata|number, SKDeflectSideConfig> 原生 Raise 起始刀侧到成功弹反动作及结束刀侧的映射。
---@field DeflectFailedActionIdByType table<userdata|number, string> ESKIncomingAttackType 原生枚举到动作 ID 前缀的映射。
---@field DeflectFailedByType table<userdata|number, string[]> ESKIncomingAttackType 原生枚举到弹反失败动画链的映射。
---@field AttackDeflectedBySide table<userdata|number, SKDeflectSideConfig> 原生提交刀侧到被弹开动作及保持刀侧的映射。
---@field Guard table<string, string> 地面与空中防御举刀、收刀动画路径。
---@field GuardImpactAnimation string 普通防御命中时播放的震刀动作路径。

---@type SKCombatConfigModule
local CombatConfig = {
    SlotName = "CombatFullBodySlot",
    BlendInTime = 0.06,
    BlendOutTime = 0.10,
    HeavyHoldThreshold = 0.30,
    DefaultSide = UE.ESKAttackSide.Right,
    DefenseSide = UE.ESKAttackSide.Left,
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
            StartSide = UE.ESKAttackSide.Right,
            bAllowHeavy = true,
        },
        Left = {
            AnimationPath = Attack.Left,
            NextAction = "Combo_01",
            StartSide = UE.ESKAttackSide.Left,
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
    LightActionIdBySide = {
        [UE.ESKAttackSide.Right] = "Right",
        [UE.ESKAttackSide.Left] = "Left",
    },
    HeavyBySide = {
        [UE.ESKAttackSide.Right] = {
            ActionId = "Charged_Thrust_Right",
            AnimationPath = Attack.Charged_Thrust_Right,
            StartSide = UE.ESKAttackSide.Right,
            bAllowHeavy = true,
        },
        [UE.ESKAttackSide.Left] = {
            ActionId = "Charged_Thrust_Left",
            AnimationPath = Attack.Charged_Thrust_Left,
            StartSide = UE.ESKAttackSide.Left,
            bAllowHeavy = true,
        },
    },
    -- 防御左侧起手立即播放重攻击前摇；短按会在阈值前切入 Left，长按则沿当前 Montage 继续重攻击。
    GuardAttackStartupBySide = {
        [UE.ESKAttackSide.Left] = {
            ActionId = "Guard_Attack_Startup_Left",
            AnimationPath = Attack.Charged_Thrust_Left,
            StartSide = UE.ESKAttackSide.Left,
            bAllowHeavy = true,
        },
    },
    -- 成功弹反只在 Guard Raise 阶段成立：130101 负责右到左，130102 负责左到右。
    DeflectBySide = {
        [UE.ESKAttackSide.Right] = {
            ActionId = "Deflect_Right_To_Left",
            AnimationPath = Deflect.Type_01_Stage_02,
            EndSide = UE.ESKAttackSide.Left,
        },
        [UE.ESKAttackSide.Left] = {
            ActionId = "Deflect_Left_To_Right",
            AnimationPath = Deflect.Type_01_Stage_03,
            EndSide = UE.ESKAttackSide.Right,
        },
    },
    DeflectFailedActionIdByType = {
        [UE.ESKIncomingAttackType.Light] = "DeflectFailed_Light",
        [UE.ESKIncomingAttackType.Heavy] = "DeflectFailed_Heavy",
        [UE.ESKIncomingAttackType.Thrust] = "DeflectFailed_Thrust",
        [UE.ESKIncomingAttackType.Special] = "DeflectFailed_Special",
    },
    DeflectFailedByType = {
        [UE.ESKIncomingAttackType.Light] = {
            DeflectFailed.Type_01_Stage_01,
            DeflectFailed.Type_01_Stage_02,
            DeflectFailed.Type_01_Stage_03,
        },
        [UE.ESKIncomingAttackType.Heavy] = {
            DeflectFailed.Type_02_Stage_01,
            DeflectFailed.Type_02_Stage_02,
            DeflectFailed.Type_02_Stage_03,
        },
        [UE.ESKIncomingAttackType.Thrust] = {
            DeflectFailed.Type_03_Stage_01,
            DeflectFailed.Type_03_Stage_02,
            DeflectFailed.Type_03_Stage_03,
        },
        [UE.ESKIncomingAttackType.Special] = { DeflectFailed.Type_04_Stage_01 },
    },
    -- 攻击被成功弹开后保持原刀侧：150000 对应右侧，150001 对应左侧。
    AttackDeflectedBySide = {
        [UE.ESKAttackSide.Right] = {
            ActionId = "AttackDeflected_Right",
            AnimationPath = AttackDeflected.Right,
            EndSide = UE.ESKAttackSide.Right,
        },
        [UE.ESKAttackSide.Left] = {
            ActionId = "AttackDeflected_Left",
            AnimationPath = AttackDeflected.Left,
            EndSide = UE.ESKAttackSide.Left,
        },
    },
}

return CombatConfig
