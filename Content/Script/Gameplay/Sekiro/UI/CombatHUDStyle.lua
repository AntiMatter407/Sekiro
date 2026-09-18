-- Lua 类型：纯 Lua 配置模块。本文件只描述原版 UI 资源、图集区域和显示布局，不保存角色属性。
-- 原图与矩阵证据见 Docs/reference/original-combat-ui-assets.json 和 original-combat-ui-layout.md。
-- 原版舞台为 1920x1080；玩家血条暂选源动画第 600 帧宽度作为显示基线，不代表原版成长公式。

---@class SKCombatHUDSpriteStyle
---@field Id string 控件内唯一精灵名称。
---@field Group string 接收同一 Survival 快照比例的显示分组。
---@field Texture string 已导入纹理的 UE 对象路径。
---@field Position table<string, number> 原版参考坐标；目标跟随分组为投影点局部偏移。
---@field Size table<string, number> 原版参考坐标系中的显示尺寸。
---@field UVMin table<string, number> 图集归一化左上角，已经考虑必要裁切。
---@field UVMax table<string, number> 图集归一化右下角。
---@field Anchor table<string, number> 宽屏时保留的视口锚点。
---@field Fill number ESKHUDFillMode 原生枚举值。
---@field Tint table<string, number>|nil 显示颜色；不改变纹理源像素。
---@field BrokenOnly boolean 是否只在权威架势崩溃状态出现。
---@field MirrorX boolean 是否水平镜像原图，用于架势条左右端。

---@class SKCombatHUDStyle
---@field ReferenceSize table<string, number> 从 GFX RECT 读取的原版逻辑尺寸。
---@field Sprites SKCombatHUDSpriteStyle[] 按绘制顺序排列的显示元素。
---@field TargetAnchorHeightOffset number 世界目标包围盒顶部之外的偏移，UE 厘米；属于项目适配而非原版骨骼证据。
---@field TargetScreenOffset table<string, number> 玩家视口像素偏移，避免重复 DPI 换算。
---@field LockOn table 原版锁定标记的纹理与显示配置。
---@field BossActorTag string 角色蓝图显式配置的 Actor Tag，只描述 UI 展示身份，不作为 GAS 状态或阶段。

local AtlasTexture = "/Game/UI/Combat/Textures/T_OriginalCombatAtlas.T_OriginalCombatAtlas"
local HUDFillMode = UE.ESKHUDFillMode
-- 所有矩形来自高清 SB_FE.layout，half=0；不存在旋转打包。
local AtlasRects = {
    PlayerBase = {0, 606, 1654, 84},
    PlayerFill = {0, 931, 1370, 30},
    HealthEdge = {2493, 310, 18, 54},
    BossBase = {0, 728, 1618, 30},
    BossFill = {0, 694, 1618, 30},
    TargetBase = {1374, 931, 266, 22},
    TargetFill = {1644, 925, 256, 12},
    PostureBase = {1278, 965, 1052, 44},
    PostureFill = {0, 895, 1376, 32},
    PostureBroken = {1392, 843, 700, 78},
}

---构造一个原版图集精灵，所有裁切均改变 UV，不预处理或重绘原图。
---@param id string 当前 Widget 内唯一名称。
---@param group string 资源比例和显隐所属分组。
---@param symbol string AtlasRects 中的原图映射键。
---@param x number 参考坐标横向位置。
---@param y number 参考坐标纵向位置。
---@param width number 参考坐标显示宽度。
---@param height number 参考坐标显示高度。
---@param anchor_x number 视口横向锚点，范围 0..1。
---@param anchor_y number 视口纵向锚点，范围 0..1。
---@param fill number ESKHUDFillMode 原生枚举值。
---@param tint table<string, number>|nil 颜色与透明度；nil 为原图白色乘数。
---@param source_x number|nil 子纹理内横向采样起点；nil 为零。
---@param source_width number|nil 子纹理采样宽度；nil 为整个原子纹理。
---@param mirror_x boolean|nil 是否对左右端进行水平镜像。
---@param broken_only boolean|nil 是否仅架势崩溃时绘制。
---@return SKCombatHUDSpriteStyle sprite 可传给 C++ 显示接口的纯数据。
local function atlas_sprite(id, group, symbol, x, y, width, height, anchor_x, anchor_y, fill, tint, source_x, source_width, mirror_x, broken_only)
    local rect = AtlasRects[symbol]
    local sample_x = rect[1] + (source_x or 0)
    return {
        Id = id,
        Group = group,
        Texture = AtlasTexture,
        Position = {X = x, Y = y},
        Size = {X = width, Y = height},
        UVMin = {X = sample_x / 4096, Y = rect[2] / 1024},
        UVMax = {X = (sample_x + (source_width or rect[3])) / 4096, Y = (rect[2] + rect[4]) / 1024},
        Anchor = {X = anchor_x, Y = anchor_y},
        Fill = fill,
        Tint = tint,
        MirrorX = mirror_x == true,
        BrokenOnly = broken_only == true,
    }
end

-- 黑底 alpha 来自原版 SPFrame/State_0；红色使用原白纹理的源 ColorTransform 白点近似。
-- 原版逐像素乘加色变换、延迟层和动画仍需独立 UI 材质/时序接入，不能称为像素级复现。
local PostureBaseTint = {R = 0, G = 0, B = 0, A = 0.6484375}
local PlayerHealthTint = {R = 0.21365, G = 0.0464, B = 0.02824, A = 1}

---@type SKCombatHUDStyle
local CombatHUDStyle = {
    ReferenceSize = {X = 1920, Y = 1080},
    BossActorTag = "UI.Display.Boss", -- 弦一郎蓝图显式标记；不按类名、血量或模型名猜测 Boss。
    TargetAnchorHeightOffset = 0, -- 使用现有目标包围盒顶点，具体 Socket 对齐尚待原版角色证据。
    TargetScreenOffset = {X = 0, Y = 0},
    LockOn = {
        Texture = "/Game/UI/Combat/Textures/T_OriginalLockOn.T_OriginalLockOn",
        UVMin = {X = 0, Y = 0},
        UVMax = {X = 0.5, Y = 1}, -- DefineShape 1042 只覆盖源纹理左侧 32x32。
        Size = 32,
        Tint = {R = 1, G = 1, B = 1, A = 1},
        TargetBoneName = "Pelvis", -- 已核对主角与弦一郎共有的腰部骨骼；直接跟随动画，无需额外 Socket。
        PositionInterpSpeed = 24, -- 保留现有投影平滑；原版完整 90 帧动画尚未接入。
    },
    Sprites = {
        -- PlayerHUD(960,537)/Gauge(-885,400)/HP(125.85,45.65)/Offset(-89,0)。
        atlas_sprite("PlayerHealthBase", "PlayerHealth", "PlayerBase", 90.4, 957.8839, 354.3, 48.047, 0, 1, HUDFillMode.None, {R = 1, G = 1, B = 1, A = 0.6484375}, 0, 525.278),
        atlas_sprite("PlayerHealthFill", "PlayerHealth", "PlayerFill", 111.85, 975.9621, 332.75, 13.1998, 0, 1, HUDFillMode.LeftToRight, PlayerHealthTint, 0, 665.5),
        atlas_sprite("PlayerHealthLeft", "PlayerHealth", "HealthEdge", 100.95, 959.3303, 18, 47.5194, 0, 1, HUDFillMode.None, {R = 1, G = 1, B = 1, A = 0.890625}),
        atlas_sprite("PlayerHealthRight", "PlayerHealth", "HealthEdge", 437.65, 958.29, 18, 49.42, 0, 1, HUDFillMode.None),
        -- 玩家 SP 原点 (960,937.5)，显示满架势采用源 1009 最后一帧的两侧遮罩边界。
        atlas_sprite("PlayerPostureBase", "PlayerPosture", "PostureBase", 621.53, 920.3, 663.84, 33, 0.5, 1, HUDFillMode.None, PostureBaseTint),
        atlas_sprite("PlayerPostureLeft", "PlayerPosture", "PostureFill", 757.5, 926.32, 202.5, 20.8, 0.5, 1, HUDFillMode.RightToLeft, nil, 338, 1038, true),
        atlas_sprite("PlayerPostureRight", "PlayerPosture", "PostureFill", 960, 926.32, 202.5, 20.8, 0.5, 1, HUDFillMode.LeftToRight, nil, 338, 1038),
        atlas_sprite("PlayerPostureBroken", "PlayerPosture", "PostureBroken", 609.65, 919.89, 700, 34.63, 0.5, 1, HUDFillMode.None, nil, nil, nil, false, true),
        -- EnemyTag 的实际局部矩阵；由同一投影锚点驱动生命与架势。
        atlas_sprite("TargetHealthBase", "TargetHealth", "TargetBase", -66.1, -4.4, 133, 11, 0.5, 0.5, HUDFillMode.None),
        atlas_sprite("TargetHealthFill", "TargetHealth", "TargetFill", -63.75, -1.95, 128, 6, 0.5, 0.5, HUDFillMode.LeftToRight),
        atlas_sprite("TargetPostureBase", "TargetPosture", "PostureBase", -74.79, 5.48, 160.23, 13.21, 0.5, 0.5, HUDFillMode.None, PostureBaseTint),
        atlas_sprite("TargetPostureLeft", "TargetPosture", "PostureFill", -61.04, 7.5, 61.6, 8.8, 0.5, 0.5, HUDFillMode.RightToLeft, nil, 916, 460, true),
        atlas_sprite("TargetPostureRight", "TargetPosture", "PostureFill", 0.56, 7.5, 61.6, 8.8, 0.5, 0.5, HUDFillMode.LeftToRight, nil, 916, 460),
        atlas_sprite("TargetPostureBroken", "TargetPosture", "PostureBroken", -77.65, 2.78, 168.95, 18.73, 0.5, 0.5, HUDFillMode.None, nil, nil, nil, false, true),
        -- BossList/Item_0_0；固定血条不随锁定切换，也不伪造忍杀节点。
        atlas_sprite("BossHealthBase", "BossHealth", "BossBase", 112.305, 90.4565, 410.6475, 17.8976, 0, 0, HUDFillMode.None),
        atlas_sprite("BossHealthFill", "BossHealth", "BossFill", 112.45, 90.9076, 410.6475, 17.8972, 0, 0, HUDFillMode.LeftToRight),
        atlas_sprite("BossHealthLeft", "BossHealth", "HealthEdge", 102.5, 77.3503, 18, 43.1993, 0, 0, HUDFillMode.None),
        atlas_sprite("BossHealthRight", "BossHealth", "HealthEdge", 516.1, 77.3503, 18, 43.1993, 0, 0, HUDFillMode.None),
        atlas_sprite("EnemyPostureBase", "EnemyPosture", "PostureBase", 573.57, 50.95, 763.41, 33, 0.5, 0, HUDFillMode.None, PostureBaseTint),
        atlas_sprite("EnemyPostureLeft", "EnemyPosture", "PostureFill", 609.32, 57.52, 349.18, 20.6, 0.5, 0, HUDFillMode.RightToLeft, nil, nil, nil, true),
        atlas_sprite("EnemyPostureRight", "EnemyPosture", "PostureFill", 958.5, 57.52, 349.18, 20.6, 0.5, 0, HUDFillMode.LeftToRight),
        atlas_sprite("EnemyPostureBroken", "EnemyPosture", "PostureBroken", 559.91, 50.54, 805, 34.63, 0.5, 0, HUDFillMode.None, nil, nil, nil, false, true),
    },
}

return CombatHUDStyle
