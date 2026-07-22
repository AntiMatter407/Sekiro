-- Lua 类型：纯 Lua 配置模块。本文件不绑定 UObject，集中描述首版攻击、防御与弹反动作数据。
-- 当前不包含碰撞、伤害、生命或躯干值；受击条件由测试入口写入的模拟来袭窗口提供。

local AnimAssets = require("Animation.Sekiro.AnimAssets")
local Attack = AnimAssets.Attack
local Deflect = AnimAssets.Deflect

---@class SKCombatActionConfig
---@field AnimationPath string 动画序列对象路径。
---@field NextAction string|nil 普通连段窗口内再次点击时进入的动作。
---@field Side string 动作侧别，用于首版调试显示。

---@class SKCombatConfigModule
---@field SlotName string 全身战斗动作 Slot 名称。
---@field BlendInTime number 动作淡入时间，单位为秒。
---@field BlendOutTime number 动作淡出时间，单位为秒。
---@field HeavyHoldThreshold number 攻击键达到重攻击的持续时间，单位为秒。
---@field DeflectChainResetTime number 同类型弹反段数重置间隔，单位为秒。
---@field DefaultIncomingWindow number 调试来袭窗口默认时长，单位为秒。
---@field Attacks table<string, SKCombatActionConfig> 稳定攻击动作 ID 到动作配置的映射。
---@field HeavyBySide table<string, SKCombatActionConfig> 当前侧别到蓄力突刺动作的映射。
---@field DeflectByType table<string, string[]> 模拟攻击类型到弹反动画序列链的映射。
---@field Guard table<string, string> 防御举刀和收刀动画路径。

---@type SKCombatConfigModule
local CombatConfig = {
    SlotName = "CombatFullBodySlot",
    BlendInTime = 0.06,
    BlendOutTime = 0.10,
    HeavyHoldThreshold = 0.30,
    DeflectChainResetTime = 0.75,
    DefaultIncomingWindow = 0.25,
    Guard = {
        Raise = AnimAssets.Guard.Raise,
        Lower = AnimAssets.Guard.Lower,
    },
    Attacks = {
        Right = { AnimationPath = Attack.Right, NextAction = "Left", Side = "Right" },
        Left = { AnimationPath = Attack.Left, NextAction = "Combo_01", Side = "Left" },
        Combo_01 = { AnimationPath = Attack.Combo_01, NextAction = "Combo_02", Side = "None" },
        Combo_02 = { AnimationPath = Attack.Combo_02, NextAction = "Combo_03", Side = "None" },
        Combo_03 = { AnimationPath = Attack.Combo_03, NextAction = nil, Side = "None" },
    },
    HeavyBySide = {
        Right = { AnimationPath = Attack.Charged_Thrust_Right, NextAction = "Left", Side = "Right" },
        Left = { AnimationPath = Attack.Charged_Thrust_Left, NextAction = "Right", Side = "Left" },
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
